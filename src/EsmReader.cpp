#include "EsmReader.h"

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>  // GetModuleFileNameW, MAX_PATH

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <spdlog/spdlog.h>  // logging — also via the forced PCH, but include-what-you-use

namespace
{
    // Record header flag: record data is a u32 decompressed-size + zlib stream.
    constexpr std::uint32_t kCompressedFlag = 0x00040000;

    // 4-char signatures as little-endian u32 (how they read out of the file).
    constexpr std::uint32_t kSigGRUP = 0x50555247;  // 'GRUP'
    constexpr std::uint32_t kSigPNDT = 0x54444E50;  // 'PNDT'
    constexpr std::uint32_t kSigPPBD = 0x44425050;  // 'PPBD'
    constexpr std::uint32_t kSigXXXX = 0x58585858;  // 'XXXX' (large-subrecord size override)
    constexpr std::uint32_t kSigNPC_ = 0x5F43504E;  // 'NPC_'
    constexpr std::uint32_t kSigFLOR = 0x524F4C46;  // 'FLOR'
    constexpr std::uint32_t kSigOMOD = 0x444F4D4F;  // 'OMOD'
    constexpr std::uint32_t kSigFLST = 0x54534C46;  // 'FLST'
    constexpr std::uint32_t kSigCNDF = 0x46444E43;  // 'CNDF' (reusable condition forms; func-837 targets)
    // For the func-699 actor ability/resistance/weakness chain (NPC_->OMOD->NPRK->PERK->SPEL->MGEF).
    constexpr std::uint32_t kSigMGEF = 0x4645474D;  // 'MGEF' (magic effect; KWDA carries scanner kw)
    constexpr std::uint32_t kSigSPEL = 0x4C455053;  // 'SPEL' (spell; EFID references MGEFs)
    constexpr std::uint32_t kSigPERK = 0x4B524550;  // 'PERK' (perk; type-1 entry adds a self-spell)

    // Subrecord 4-char signatures (little-endian u32).
    constexpr std::uint32_t kSubOBTS = 0x5354424F;  // 'OBTS' (O=0x4F,B,T,S little-endian)
    constexpr std::uint32_t kSubDATA = 0x41544144;  // 'DATA'
    constexpr std::uint32_t kSubNKEY = 0x59454B4E;  // 'NKEY'
    constexpr std::uint32_t kSubNPRK = 0x4B52504E;  // 'NPRK' (OMOD perk grant; same table as NKEY)
    constexpr std::uint32_t kSubLNAM = 0x4D414E4C;  // 'LNAM'
    constexpr std::uint32_t kSubINAM = 0x4D414E49;  // 'INAM'
    constexpr std::uint32_t kSubCTDA = 0x41445443;  // 'CTDA'
    constexpr std::uint32_t kSubPRPS = 0x53505250;  // 'PRPS'
    constexpr std::uint32_t kSubKWDA = 0x4144574B;  // 'KWDA' (PNDT planet-trait / MGEF keyword array)
    constexpr std::uint32_t kSubGNAM = 0x4D414E47;  // 'GNAM' (PNDT galaxy data; the 12-byte variant leads with the parent-star id)
    constexpr std::uint32_t kSubEFID = 0x44494645;  // 'EFID' (SPEL effect -> MGEF form id)
    constexpr std::uint32_t kSubPRKE = 0x454B5250;  // 'PRKE' (PERK entry header; byte0 == 1 == Ability)
    constexpr std::uint32_t kSubMAST = 0x5453414D;  // 'MAST' (TES4 header master filename)

    // The three func-699 scanner effect keywords and the catalog markers they gate, as a 3-bit mask.
    // (Abilities/Resistances/Weaknesses — the live HandScanner attributes a spawned creature carries.)
    constexpr std::uint8_t  kAbilBitAbilities   = 1;
    constexpr std::uint8_t  kAbilBitResistances = 2;
    constexpr std::uint8_t  kAbilBitWeaknesses  = 4;
    constexpr std::uint32_t kKwAbilityEffect    = 0x001D3B47;  // -> 0x002634BF Abilities
    constexpr std::uint32_t kKwResistanceEffect = 0x001D3B48;  // -> 0x002634C1 Resistances
    constexpr std::uint32_t kKwWeaknessEffect   = 0x001D3B46;  // -> 0x002634C0 Weaknesses
    constexpr std::uint32_t kMarkerAbilities    = 0x002634BF;
    constexpr std::uint32_t kMarkerResistances  = 0x002634C1;
    constexpr std::uint32_t kMarkerWeaknesses   = 0x002634C0;

    constexpr std::uint32_t kMaxListLen = 0x10000;  // sanity bound on a PPBD sub-array

    // --- Marker-derivation constants -------------------------------------------------------------
    // Validated offline model: re/ghidra/output/species-scan-complete-model-2026-06-23.md +
    // re/tools/esm_derive_markers.py (17/17 exact vs in-game ground truth, no per-species tables).
    //
    // The two HandScanner catalog FLSTs hold, per green marker, the inline CTDA membership conditions
    // that decide whether that marker is emitted for a given species on a given planet. We evaluate
    // those AUTHORED conditions directly through a faithful port of the engine's TESCondition::IsTrue
    // (ID_71422 list-walk / ID_71429 per-item). The only hard-coded ids are the two catalog form-ids,
    // the reproduction AVIF, and the perk/display leaf-function ids — all catalog-level constants.
    constexpr std::uint32_t kCatalogFlora = 0x00160C96;  // HandScannerPlantKeywords  (flora markers)
    constexpr std::uint32_t kCatalogFauna = 0x00160C97;  // HandScannerActorKeywords  (fauna markers)

    // CTDA condition functions we resolve offline. Anything else (incl. 448 HasPerk / 699
    // HasMagicEffectKeyword) marks the marker display/perk-gated -> dropped (never in the green set).
    constexpr std::uint16_t kFuncConstAnchor    = 882;  // constant 1.0 anchor
    constexpr std::uint16_t kFuncGetPlanetTrait = 858;  // GetIsPlanetTrait(param1) -> planet PNDT KWDA
    constexpr std::uint16_t kFuncHasKeyword     = 560;  // HasKeyword(species, param1) -> granted-kw set
    constexpr std::uint16_t kFuncGetActorValue  = 14;   // GetActorValue(0x0023E905) -> FLOR PRPS repro N
    constexpr std::uint16_t kFuncEvalCondForm   = 837;  // EvaluateConditionForm(param1) -> recurse CNDF

    // CTDA payload (32 bytes): op@+0x00 u8, comp f32@+0x04, func u16@+0x08, param1 u32@+0x0C.
    constexpr std::size_t kCtdaSize      = 32;
    constexpr std::size_t kCtdaOpOff     = 0x00;
    constexpr std::size_t kCtdaCompOff   = 0x04;
    constexpr std::size_t kCtdaFuncOff   = 0x08;
    constexpr std::size_t kCtdaParam1Off = 0x0C;

    // OBTS header: u32 entryCount @0, fixed 18-byte prefix, then entryCount x 7-byte entries
    // (u32 OMOD form id + 3 bytes). Entries begin at 0x12.
    constexpr std::size_t kObtsEntriesOff  = 0x12;
    constexpr std::size_t kObtsEntryStride = 7;
    // FLOR PRPS: stride-12 triples (u32 AVIF, f32 value, u32). The reproduction AVIF = func-14 input.
    constexpr std::uint32_t kAvifPlantReproduction = 0x0023E905;
    constexpr std::size_t   kPrpsTripleStride      = 12;
    // CNDF recursion (func 837) depth cap — guards a hostile/cyclic ESM from overflowing the stack.
    constexpr int kMaxCondFormDepth = 16;

    // Sanity ceiling on a single record's *decompressed* size, read straight from the file
    // before we allocate for it. A real planet record inflates to tens of KB; this 64 MiB cap
    // turns a corrupt/hostile decompSize (e.g. 0xFFFFFFFF) into a skipped record instead of a
    // multi-GiB bad_alloc / DoS on game launch.
    constexpr std::uint32_t kMaxDecompSize = 64u * 1024u * 1024u;

    // Bounds-checked little-endian cursor over a byte buffer.
    struct Cursor
    {
        const std::uint8_t* p;
        std::size_t         size;
        std::size_t         off {0};

        bool          can(std::size_t n) const { return n <= size && off <= size - n; }
        std::uint32_t u32()
        {
            std::uint32_t v;
            std::memcpy(&v, p + off, sizeof v);
            off += sizeof v;
            return v;
        }
    };

    // Walk subrecords in a record's (decompressed) data; call fn(sig, payload, size).
    template <typename Fn>
    void ForEachSubrecord(const std::uint8_t* data, std::size_t size, Fn&& fn)
    {
        std::size_t   i        = 0;
        std::uint32_t realSize = 0;
        bool          haveReal = false;
        while (i + 6 <= size)
        {
            std::uint32_t sig;
            std::uint16_t sz;
            std::memcpy(&sig, data + i, 4);
            std::memcpy(&sz, data + i + 4, 2);
            i += 6;
            if (sig == kSigXXXX)
            {
                if (i + 4 > size)
                    break;
                std::memcpy(&realSize, data + i, 4);
                haveReal = true;
                i += sz;
                continue;
            }
            std::size_t dsz = sz;
            if (haveReal)
            {
                dsz      = realSize;
                haveReal = false;
            }
            if (dsz > size - i)
                break;
            fn(sig, data + i, dsz);
            i += dsz;
        }
    }

    // ---- Multi-file sources + FormID remapping ---------------------------------------------------
    // Every plugin stores its records' FormIDs in FILE-LOCAL form: the high bits index the file's
    // OWN master list (per type-space), not the runtime load order. We remap every FormID we read —
    // record ids AND cross-references — into runtime form before it enters any table, so lookups
    // match the runtime FormIDs the natives receive from live forms. Encodings:
    //   full   II XXXXXX  (byte slot, 24-bit record id)
    //   medium FD IIXXXX  (8-bit slot, 16-bit record id)
    //   small  FE IIIXXX  (12-bit slot, 12-bit record id)
    // A slot at/past the file's master count of that space resolves to the file ITSELF. That is how
    // a file's own new records are encoded, and it also covers small-flagged third-party files that
    // encode their own records full-style (e.g. 01xxxxxx with one master).

    constexpr std::uint32_t kFlagSmall  = 0x00000100;  // TES4 header flag: small (ESL-style) master
    constexpr std::uint32_t kFlagMedium = 0x00000400;  // TES4 header flag: medium master

    std::vector<Esm::SourceFile> g_sources;  // configured load order (SetSources / env fallbacks)
    std::mutex                   g_sourcesMtx;

    Esm::MasterType TypeFromFlags(std::uint32_t flags)
    {
        if (flags & kFlagMedium)
            return Esm::MasterType::kMedium;
        if (flags & kFlagSmall)
            return Esm::MasterType::kSmall;
        return Esm::MasterType::kFull;
    }

    // ASCII case-insensitive filename comparison (plugin names are ASCII).
    bool IEquals(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            const auto ca = static_cast<unsigned char>(a[i]);
            const auto cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb))
                return false;
        }
        return true;
    }

    // Per-file FormID remapper: local type-space slot -> the target file's (type, runtime index).
    struct Remapper
    {
        struct Slot
        {
            Esm::MasterType type {Esm::MasterType::kFull};
            std::uint16_t   index {0};
            bool            valid {false};
        };
        std::vector<Slot> full, medium, small;
        Slot              self;

        std::uint32_t operator()(std::uint32_t raw) const
        {
            if (raw == 0)
                return 0;
            const std::uint32_t hi = raw >> 24;
            const Slot*         s;
            if (hi == 0xFE)
            {
                const auto i = (raw >> 12) & 0xFFFu;
                s            = i < small.size() ? &small[i] : &self;
            }
            else if (hi == 0xFD)
            {
                const auto i = (raw >> 16) & 0xFFu;
                s            = i < medium.size() ? &medium[i] : &self;
            }
            else
            {
                s = hi < full.size() ? &full[hi] : &self;
            }
            if (!s->valid)
                return 0;  // unknown master -> unresolvable ref; 0 reads as "absent" on every path
            switch (s->type)
            {
            case Esm::MasterType::kMedium:
                return 0xFD000000u | (static_cast<std::uint32_t>(s->index) << 16) | (raw & 0xFFFFu);
            case Esm::MasterType::kSmall:
                return 0xFE000000u | (static_cast<std::uint32_t>(s->index) << 12) | (raw & 0xFFFu);
            default:
                return (static_cast<std::uint32_t>(s->index) << 24) | (raw & 0x00FFFFFFu);
            }
        }
    };

    // Read a plugin's TES4 header from an already-open stream: flags + MAST master names. Leaves the
    // stream positioned at the first top-level GRUP. Returns false on a malformed file.
    bool ReadTes4Header(std::ifstream& f, std::uint32_t& flags, std::vector<std::string>& masters)
    {
        std::uint8_t hdr[24];
        if (!f.read(reinterpret_cast<char*>(hdr), 24) || std::memcmp(hdr, "TES4", 4) != 0)
            return false;
        std::uint32_t tes4Size;
        std::memcpy(&tes4Size, hdr + 4, 4);
        std::memcpy(&flags, hdr + 8, 4);
        if (tes4Size > kMaxDecompSize)
            return false;  // corrupt header size
        std::vector<std::uint8_t> data(tes4Size);
        if (tes4Size != 0 && !f.read(reinterpret_cast<char*>(data.data()), data.size()))
            return false;
        ForEachSubrecord(data.data(), data.size(),
                         [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
            if (ssig == kSubMAST && sz > 0)
            {
                const auto* s = reinterpret_cast<const char*>(p);
                masters.emplace_back(s, strnlen(s, sz));
            }
        });
        return true;
    }

    // Classify a master we could not find in the configured sources by opening its header next to
    // `dir` — the slot must still land in the correct type-space or later slots misalign.
    Esm::MasterType ClassifyMasterOnDisk(const std::filesystem::path& dir, const std::string& name)
    {
        std::ifstream f(dir / name, std::ios::binary);
        std::uint32_t flags = 0;
        std::vector<std::string> unused;
        if (f && ReadTes4Header(f, flags, unused))
            return TypeFromFlags(flags);
        return Esm::MasterType::kFull;
    }

    Remapper BuildRemapper(const Esm::SourceFile& self, const std::vector<std::string>& masters,
                           const std::vector<Esm::SourceFile>& sources)
    {
        Remapper r;
        r.self = {self.type, self.runtimeIndex, true};
        for (const auto& m : masters)
        {
            Remapper::Slot slot;  // invalid unless matched
            for (const auto& s : sources)
            {
                if (IEquals(s.path.filename().string(), m))
                {
                    slot = {s.type, s.runtimeIndex, true};
                    break;
                }
            }
            auto space = slot.valid ? slot.type : ClassifyMasterOnDisk(self.path.parent_path(), m);
            if (!slot.valid)
                spdlog::warn("EsmReader: {}: master '{}' not in configured load order; its refs are dropped",
                             self.path.filename().string(), m);
            switch (space)
            {
            case Esm::MasterType::kMedium: r.medium.push_back(slot); break;
            case Esm::MasterType::kSmall:  r.small.push_back(slot);  break;
            default:                       r.full.push_back(slot);   break;
            }
        }
        return r;
    }

    // The files to parse, in load order. Precedence: SetSources (the running game's own load order)
    // -> CPS_ESM_PATHS (';'-separated offline load order; runtime indices assigned per type-space in
    // list order, exactly how the engine numbers that same load order) -> CPS_ESM_PATH (single file,
    // legacy harness) -> <exe>\Data\Starfield.esm.
    std::vector<Esm::SourceFile> ResolveSources()
    {
        {
            std::lock_guard lock(g_sourcesMtx);
            if (!g_sources.empty())
                return g_sources;
        }
        constexpr std::size_t kEnvMax = 0x4000;
        static wchar_t        envbuf[kEnvMax] {};
        if (const auto en = GetEnvironmentVariableW(L"CPS_ESM_PATHS", envbuf, kEnvMax); en > 0 && en < kEnvMax)
        {
            std::vector<Esm::SourceFile> out;
            std::uint16_t                nFull = 0, nMedium = 0, nSmall = 0;
            const std::wstring           all {envbuf};
            for (std::size_t pos = 0; pos < all.size();)
            {
                const auto semi = all.find(L';', pos);
                const auto one  = all.substr(pos, semi == std::wstring::npos ? std::wstring::npos : semi - pos);
                pos             = (semi == std::wstring::npos) ? all.size() : semi + 1;
                if (one.empty())
                    continue;
                const std::filesystem::path path {one};
                std::ifstream            f(path, std::ios::binary);
                std::uint32_t            flags = 0;
                std::vector<std::string> unused;
                if (!f || !ReadTes4Header(f, flags, unused))
                {
                    spdlog::warn("EsmReader: CPS_ESM_PATHS entry unreadable, skipped: {}", path.string());
                    continue;
                }
                const auto type = TypeFromFlags(flags);
                const auto idx  = (type == Esm::MasterType::kMedium) ? nMedium++
                                : (type == Esm::MasterType::kSmall)  ? nSmall++
                                                                     : nFull++;
                out.push_back({path, type, idx});
            }
            if (!out.empty())
                return out;
        }
        if (const auto en = GetEnvironmentVariableW(L"CPS_ESM_PATH", envbuf, MAX_PATH); en > 0 && en < MAX_PATH)
            return {{std::filesystem::path {envbuf}, Esm::MasterType::kFull, 0}};
        wchar_t buf[MAX_PATH] {};
        const auto n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return {{std::filesystem::path {buf}.parent_path() / L"Data" / L"Starfield.esm",
                 Esm::MasterType::kFull, 0}};
    }

    // Parse one PPBD payload (a single biome) and append its flora + fauna FormIDs (remapped).
    //   u32 biome | u32 chance | u32 unk | u32 RSGD
    //   u32 nFauna | nFauna x u32 (NPC_)
    //   u32 nKw    | nKw    x u32 (KYWD)
    //   u32 nFlora | u32 entrySize(>=9) | nFlora x { u32 FLOR, u32 MISC, u8 freq, pad to entrySize }
    void ParsePpbd(const std::uint8_t* data, std::size_t size, const Remapper& remap,
                   std::vector<std::uint32_t>& out)
    {
        Cursor c {data, size};
        if (!c.can(16))
            return;
        c.off += 16;  // biome, chance, unk, resource-gen

        if (!c.can(4))
            return;
        const auto nFauna = c.u32();
        if (nFauna > kMaxListLen || !c.can(static_cast<std::size_t>(nFauna) * 4))
            return;
        for (std::uint32_t i = 0; i < nFauna; ++i)
            if (const auto f = remap(c.u32()); f != 0)
                out.push_back(f);

        if (!c.can(4))
            return;
        const auto nKw = c.u32();
        if (nKw > kMaxListLen || !c.can(static_cast<std::size_t>(nKw) * 4))
            return;
        c.off += static_cast<std::size_t>(nKw) * 4;  // skip keywords

        if (!c.can(8))
            return;
        const auto nFlora    = c.u32();
        auto       entrySize = c.u32();
        if (entrySize < 9)
            entrySize = 9;
        if (nFlora > kMaxListLen || !c.can(static_cast<std::size_t>(nFlora) * entrySize))
            return;
        for (std::uint32_t i = 0; i < nFlora; ++i)
        {
            std::uint32_t flor;
            std::memcpy(&flor, c.p + c.off, sizeof flor);  // first FormID in the entry
            if (const auto f = remap(flor); f != 0)
                out.push_back(f);
            c.off += entrySize;
        }
    }

    // ---- Pure-ESM marker derivation -------------------------------------------------------------
    // Faithful port of re/tools/esm_derive_markers.py (validated 17/17). ALL pure file reads (no
    // engine call), built in the SAME one-time parse under the same try/catch degrade, so a parse
    // failure never escapes the Papyrus native boundary.

    enum class Kingdom : std::uint8_t { kUnknown = 0, kFauna, kFlora };

    // Per-species derived inputs to the catalog CTDA evaluation, keyed by species form id. We store
    // only what isn't a catalog-level constant: which kingdom, the fauna granted-keyword set (func
    // 560), and the flora reproduction value N (func 14).
    struct SpeciesMarkerInfo
    {
        Kingdom                    kingdom {Kingdom::kUnknown};
        std::vector<std::uint32_t> grantedKw;    // FAUNA: keywords granted via OBTS->OMOD->NKEY (func 560)
        std::int32_t               reproAv {0};  // FLORA: FLOR PRPS reproduction value N (func 14); 0 if none
        std::uint8_t               abilityMask {0};  // FAUNA: func-699 Abilities/Resistances/Weaknesses bitmask
    };

    // One parsed CTDA condition.
    struct Ctda
    {
        std::uint8_t  op {};
        float         comp {};
        std::uint16_t func {};
        std::uint32_t param1 {};
        bool          orWithNext() const { return (op & 1) != 0; }
        int           cmpOp() const { return (op >> 5) & 7; }
    };

    // A parsed HandScanner catalog FLST: ordered marker ids + per-marker CTDA condition blocks.
    // A marker whose lnam index has NO block is unconditional (always emitted).
    struct Catalog
    {
        std::vector<std::uint32_t>                           lnam;       // marker ids in catalog order
        std::unordered_map<std::int32_t, std::vector<Ctda>> condByIdx;  // lnam index -> condition list
    };

    // The lookup tables consumed at GetSpeciesMarkers time. Built once across ALL source files.
    struct MarkerTables
    {
        std::unordered_map<std::uint32_t, SpeciesMarkerInfo>          species;
        Catalog                                                      floraCatalog;
        Catalog                                                      faunaCatalog;
        std::unordered_map<std::uint32_t, std::vector<Ctda>>         cndf;          // CNDF id -> CTDA list
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> planetTraits;  // planet -> KWDA traits
        Esm::PlanetStarMap                                            planetStars;   // planet -> GNAM parent-star id (Sol == 0 is VALID)
    };

    // Cross-file intermediates, keyed by RUNTIME FormID. A species' OBTS can reference OMODs from an
    // EARLIER master (a Shattered Space creature grants base-game temperament OMODs), and any link of
    // the func-699 chain (MGEF/SPEL/PERK/OMOD) can live in — or be overridden by — a different file
    // than its consumer. So each file's parse only COLLECTS remapped raw links (last file wins), and
    // the chains are resolved globally after every file has been read.
    struct RawTables
    {
        struct OmodRaw
        {
            std::vector<std::uint32_t> nkey;  // granted keywords (func-560 input)
            std::vector<std::uint32_t> nprk;  // granted perks (func-699 ability chain)
        };
        std::unordered_map<std::uint32_t, std::uint8_t>                mgefMask;   // MGEF -> scanner-kw mask
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>  spelEfids;  // SPEL -> [MGEF]
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>  perkSpels;  // PERK -> [type-1 SPEL]
        std::unordered_map<std::uint32_t, OmodRaw>                     omod;       // OMOD -> raw grants
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>  npcObts;    // NPC_ -> [OBTS OMOD]
        std::unordered_map<std::uint32_t, std::int32_t>                florRepro;  // FLOR -> PRPS repro N
    };

    // Walk every record in a top-level group body (records + nested GRUPs), decompressing as needed,
    // calling fn(sig, formid, decompressedData, dataSize). The record body is borrowed from either
    // `group` (uncompressed) or `decomp` (a caller-owned scratch buffer reused per record) — do not
    // retain the pointer past the callback. `formid` is passed RAW (file-local); callers remap.
    template <typename Fn>
    void ForEachRecordInGroup(const std::uint8_t* group, std::size_t groupSize,
                              std::vector<std::uint8_t>& decomp, Fn&& fn)
    {
        std::size_t i = 0;
        while (i + 24 <= groupSize)
        {
            const std::uint8_t* rec = group + i;
            std::uint32_t       sig, size, flags, formid;
            std::memcpy(&sig, rec + 0, 4);
            std::memcpy(&size, rec + 4, 4);
            std::memcpy(&flags, rec + 8, 4);
            std::memcpy(&formid, rec + 12, 4);
            i += 24;
            if (sig == kSigGRUP)
            {
                // Nested group: recurse into its body (header is 24 bytes).
                if (size >= 24 && size - 24 <= groupSize - i)
                    ForEachRecordInGroup(group + i, size - 24, decomp, fn);
                i += (size < 24) ? 0 : size - 24;
                continue;
            }
            if (size > groupSize - i)
                break;
            const std::uint8_t* data     = rec + 24;
            std::size_t         dataSize = size;
            i += size;

            if (flags & kCompressedFlag)
            {
                if (size < 4)
                    continue;
                std::uint32_t decompSize;
                std::memcpy(&decompSize, data, 4);
                if (decompSize == 0 || decompSize > kMaxDecompSize)
                    continue;
                decomp.assign(decompSize, 0);
                uLongf destLen = decompSize;
                if (uncompress(decomp.data(), &destLen, reinterpret_cast<const Bytef*>(data + 4),
                               size - 4) != Z_OK)
                    continue;
                data     = decomp.data();
                dataSize = destLen;
            }
            fn(sig, formid, data, dataSize);
        }
    }

    // Parse a 32-byte CTDA payload. param1 is remapped when the function's parameter is a FormID
    // (858 planet-trait keyword / 560 granted keyword / 837 CNDF); other funcs either ignore param1
    // (882, 14) or drop the marker entirely (perk/display gated), so their raw value is kept.
    Ctda ParseCtda(const std::uint8_t* p, const Remapper& remap)
    {
        Ctda c;
        c.op = p[kCtdaOpOff];
        std::memcpy(&c.comp, p + kCtdaCompOff, 4);
        std::memcpy(&c.func, p + kCtdaFuncOff, 2);
        std::memcpy(&c.param1, p + kCtdaParam1Off, 4);
        if (c.func == kFuncGetPlanetTrait || c.func == kFuncHasKeyword || c.func == kFuncEvalCondForm)
            c.param1 = remap(c.param1);
        return c;
    }

    // Parse one catalog FLST record's subrecords into (lnam[], condByIdx). Sequential walk: each LNAM
    // appends a marker id (in catalog order); each INAM opens a conditioned block for the LNAM index
    // it carries; CTDA payloads accumulate into the currently-open block until the next INAM/end.
    // (Mirrors parse_flst in esm_derive_markers.py; CITC and other subrecords are ignored.)
    void ParseCatalogRecord(const std::uint8_t* data, std::size_t dataSize, const Remapper& remap,
                            Catalog& out)
    {
        std::int32_t      curIdx = -1;
        std::vector<Ctda> cur;
        bool              started = false;
        auto flush = [&] {
            if (started)
                out.condByIdx[curIdx] = cur;  // last-wins if an index repeats (matches the Python dict)
        };
        ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
            if (ssig == kSubLNAM && sz >= 4)
            {
                std::uint32_t m;
                std::memcpy(&m, p, 4);
                out.lnam.push_back(remap(m));
            }
            else if (ssig == kSubINAM && sz >= 4)
            {
                flush();
                std::memcpy(&curIdx, p, 4);
                cur.clear();
                started = true;
            }
            else if (ssig == kSubCTDA && sz >= kCtdaSize)
            {
                cur.push_back(ParseCtda(p, remap));
            }
        });
        flush();
    }

    // Find + parse the flora (0x00160C96) and fauna (0x00160C97) catalog FLSTs from a FLST group.
    // An override in a later file REPLACES the earlier catalog wholesale (records replace, not merge).
    void BuildCatalogs(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                       const Remapper& remap, Catalog& flora, Catalog& fauna)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigFLST)
                return;
            const auto rt = remap(formid);
            if (rt == kCatalogFlora)
            {
                flora = Catalog {};
                ParseCatalogRecord(data, dataSize, remap, flora);
            }
            else if (rt == kCatalogFauna)
            {
                fauna = Catalog {};
                ParseCatalogRecord(data, dataSize, remap, fauna);
            }
        });
    }

    // Parse every CNDF record's CTDA list into the map (func-837 EvaluateConditionForm targets).
    void BuildCndf(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                   const Remapper& remap, std::unordered_map<std::uint32_t, std::vector<Ctda>>& cndf)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigCNDF)
                return;
            std::vector<Ctda> items;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSubCTDA && sz >= kCtdaSize)
                    items.push_back(ParseCtda(p, remap));
            });
            cndf[remap(formid)] = std::move(items);
        });
    }

    // One pass over OMOD: collect the raw remapped NKEY keyword grants (func-560) and NPRK perk
    // grants (func-699) per OMOD. NKEY and NPRK are inline 4CC tags inside the DATA blob, each
    // followed by a u32 (keyword / perk id). (Mirrors EsmDB.omod_nkey + omod_nprk.)
    void CollectOmodRaw(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                        const Remapper& remap, std::unordered_map<std::uint32_t, RawTables::OmodRaw>& omod)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigOMOD)
                return;
            RawTables::OmodRaw raw;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig != kSubDATA)
                    return;
                for (std::size_t off = 0; off + 8 <= sz;)
                {
                    std::uint32_t tag;
                    std::memcpy(&tag, p + off, 4);
                    if (tag == kSubNKEY || tag == kSubNPRK)
                    {
                        std::uint32_t id;
                        std::memcpy(&id, p + off + 4, 4);
                        if (const auto rt = remap(id); rt != 0)
                            (tag == kSubNKEY ? raw.nkey : raw.nprk).push_back(rt);
                        off += 8;
                    }
                    else
                        off += 1;
                }
            });
            omod.insert_or_assign(remap(formid), std::move(raw));
        });
    }

    // The remapped OMOD ids one NPC_ attaches via OBTS. Resolved to keyword grants + ability masks
    // AFTER all files are parsed (the OMODs can live in any master).
    std::vector<std::uint32_t> NpcObtsOmods(const std::uint8_t* data, std::size_t dataSize,
                                            const Remapper& remap)
    {
        std::vector<std::uint32_t> omods;
        ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
            if (ssig != kSubOBTS || sz < kObtsEntriesOff)
                return;
            for (std::size_t off = kObtsEntriesOff; off + kObtsEntryStride <= sz; off += kObtsEntryStride)
            {
                std::uint32_t omodId;
                std::memcpy(&omodId, p + off, 4);
                if (const auto rt = remap(omodId); rt != 0)
                    omods.push_back(rt);
            }
        });
        return omods;
    }

    // The FLOR's reproduction value N from the first PRPS triple with AVIF == 0x0023E905 (func-14
    // input). 0 if none (which is also what func 14 yields for an absent value). (Mirrors flor_prps_n.)
    std::int32_t FloraReproN(const std::uint8_t* data, std::size_t dataSize, const Remapper& remap)
    {
        std::int32_t n     = 0;
        bool         found = false;
        ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
            if (ssig != kSubPRPS || found)
                return;
            for (std::size_t off = 0; off + kPrpsTripleStride <= sz; off += kPrpsTripleStride)
            {
                std::uint32_t avif;
                float         value;
                std::memcpy(&avif, p + off, 4);
                std::memcpy(&value, p + off + 4, 4);
                if (remap(avif) == kAvifPlantReproduction)
                {
                    n     = static_cast<std::int32_t>(std::lround(value));
                    found = true;
                    return;
                }
            }
        });
        return n;
    }

    // ---- func-699 actor Abilities/Resistances/Weaknesses chain ----------------------------------
    // The live HandScanner shows extra Abilities/Resistances/Weaknesses attributes when a SPAWNED
    // creature carries a magic effect with the matching scanner keyword (func 699 HasMagicEffectKeyword).
    // These are NOT in the per-species slot+0x08 dump (no live actor), but ARE resolvable offline from
    // the creature's static ability attachments and ARE part of the full in-game scan display, so the mod
    // writes them too. Chain: NPC_ OBTS -> OMOD 'NPRK' -> PERK type-1 Ability entry -> SPEL EFID -> MGEF
    // KWDA scanner keyword. Each link is COLLECTED per file (last override wins) and the mask is built
    // bottom-up once all files are in. (Mirrors esm_derive_markers.py actor path.)

    std::uint8_t KwToAbilityBit(std::uint32_t kw)
    {
        if (kw == kKwAbilityEffect)    return kAbilBitAbilities;
        if (kw == kKwResistanceEffect) return kAbilBitResistances;
        if (kw == kKwWeaknessEffect)   return kAbilBitWeaknesses;
        return 0;
    }

    std::vector<std::uint32_t> AbilityMaskToMarkers(std::uint8_t mask)
    {
        std::vector<std::uint32_t> out;
        if (mask & kAbilBitAbilities)   out.push_back(kMarkerAbilities);
        if (mask & kAbilBitResistances) out.push_back(kMarkerResistances);
        if (mask & kAbilBitWeaknesses)  out.push_back(kMarkerWeaknesses);
        return out;
    }

    // MGEF form id -> ability mask (from the scanner keywords in its KWDA). Stored even when 0 so a
    // later override that REMOVES the keywords also removes the mask.
    void CollectMgefMask(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                         const Remapper& remap, std::unordered_map<std::uint32_t, std::uint8_t>& mgefMask)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigMGEF)
                return;
            std::uint8_t mask = 0;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig != kSubKWDA)
                    return;
                for (std::size_t off = 0; off + 4 <= sz; off += 4)
                {
                    std::uint32_t kw;
                    std::memcpy(&kw, p + off, 4);
                    mask |= KwToAbilityBit(remap(kw));
                }
            });
            mgefMask.insert_or_assign(remap(formid), mask);
        });
    }

    // SPEL form id -> its EFID effect MGEF ids (mask resolved after all files).
    void CollectSpelEfids(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                          const Remapper& remap,
                          std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& spelEfids)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigSPEL)
                return;
            std::vector<std::uint32_t> efids;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig != kSubEFID || sz < 4)
                    return;
                std::uint32_t mgef;
                std::memcpy(&mgef, p, 4);
                if (const auto rt = remap(mgef); rt != 0)
                    efids.push_back(rt);
            });
            spelEfids.insert_or_assign(remap(formid), std::move(efids));
        });
    }

    // PERK form id -> its type-1 Ability entry SPEL ids. A PRKE entry whose first byte == 1 is an
    // Ability entry whose following DATA's first u32 is a SPEL added to the perk OWNER (the creature).
    void CollectPerkSpels(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                          const Remapper& remap,
                          std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& perkSpels)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigPERK)
                return;
            std::vector<std::uint32_t> spels;
            int                        cur = -1;  // current PRKE entry type byte (-1 = none / consumed)
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSubPRKE && sz >= 1)
                    cur = p[0];
                else if (ssig == kSubDATA && cur == 1 && sz >= 4)
                {
                    std::uint32_t spel;
                    std::memcpy(&spel, p, 4);
                    if (const auto rt = remap(spel); rt != 0)
                        spels.push_back(rt);
                    cur = -1;  // consume this entry's DATA
                }
            });
            perkSpels.insert_or_assign(remap(formid), std::move(spels));
        });
    }

    // ---- The CTDA evaluator (engine TESCondition::IsTrue: ID_71422 list-walk / ID_71429 per-item) --
    struct EvalCtx
    {
        const std::unordered_map<std::uint32_t, std::vector<Ctda>>* cndf;    // func 837 targets
        const std::vector<std::uint32_t>*                           skw;     // species keywords (func 560)
        const std::vector<std::uint32_t>*                           traits;  // planet traits (func 858)
        std::int32_t                                                av;      // GetActorValue (func 14)
    };

    bool ContainsId(const std::vector<std::uint32_t>& v, std::uint32_t id)
    {
        return std::find(v.begin(), v.end(), id) != v.end();
    }

    bool ApplyCmp(int op, float a, float b)
    {
        switch (op)
        {
        case 0: return a == b;
        case 1: return a != b;
        case 2: return a > b;
        case 3: return a >= b;
        case 4: return a < b;
        case 5: return a <= b;
        default: return false;
        }
    }

    std::optional<bool> EvalCondList(const std::vector<Ctda>& items, const EvalCtx& ctx, int depth);

    // The raw leaf value BEFORE the comparison op. nullopt = unresolvable (perk/display/unknown) ->
    // the whole marker is display-gated and excluded from the green set.
    std::optional<float> EvalLeaf(const Ctda& it, const EvalCtx& ctx, int depth)
    {
        switch (it.func)
        {
        case kFuncConstAnchor:
            return 1.0f;
        case kFuncGetPlanetTrait:
            return ContainsId(*ctx.traits, it.param1) ? 1.0f : 0.0f;
        case kFuncHasKeyword:
            return ContainsId(*ctx.skw, it.param1) ? 1.0f : 0.0f;
        case kFuncGetActorValue:
            return static_cast<float>(ctx.av);
        case kFuncEvalCondForm:
        {
            if (depth >= kMaxCondFormDepth || ctx.cndf == nullptr)
                return 0.0f;
            const auto cit = ctx.cndf->find(it.param1);
            if (cit == ctx.cndf->end())
                return 0.0f;
            const auto r = EvalCondList(cit->second, ctx, depth + 1);
            return (r && *r) ? 1.0f : 0.0f;
        }
        default:
            return std::nullopt;  // 448 HasPerk / 699 HasMagicEffectKeyword / unknown -> drop marker
        }
    }

    std::optional<bool> EvalItem(const Ctda& it, const EvalCtx& ctx, int depth)
    {
        const auto v = EvalLeaf(it, ctx, depth);
        if (!v)
            return std::nullopt;
        return ApplyCmp(it.cmpOp(), *v, it.comp);
    }

    // Faithful port of ID_71422: consecutive OR-with-next (op&1) items form an OR-run; runs AND
    // together. Returns nullopt if any *contributing* OR-group is entirely perk/display gated (so the
    // marker is dropped). An empty list is vacuously true (unconditional marker).
    std::optional<bool> EvalCondList(const std::vector<Ctda>& items, const EvalCtx& ctx, int depth)
    {
        if (items.empty())
            return true;
        bool acc      = true;
        bool anyKnown = false;  // state of the OR-group currently being accumulated
        bool anyTrue  = false;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (const auto r = EvalItem(items[i], ctx, depth); r.has_value())
            {
                anyKnown = true;
                if (*r)
                    anyTrue = true;
            }
            if (!items[i].orWithNext())  // this item closes the current OR-group
            {
                if (!anyKnown)
                    return std::nullopt;  // entire OR-group is perk/display gated -> drop marker
                acc      = acc && anyTrue;
                anyKnown = false;
                anyTrue  = false;
            }
        }
        // A trailing OR-run (last item had orWithNext) forms its own group with nothing after it.
        if (items.back().orWithNext())
        {
            if (!anyKnown)
                return std::nullopt;
            acc = acc && anyTrue;
        }
        return acc;
    }

    // Evaluate a whole catalog for one species: emit each unconditional marker, plus each conditioned
    // marker whose CTDA block evaluates true. Markers in catalog (lnam) order.
    std::vector<std::uint32_t> EvalCatalog(const Catalog& cat, const EvalCtx& ctx)
    {
        std::vector<std::uint32_t> out;
        for (std::size_t i = 0; i < cat.lnam.size(); ++i)
        {
            const auto cit = cat.condByIdx.find(static_cast<std::int32_t>(i));
            if (cit == cat.condByIdx.end())
            {
                out.push_back(cat.lnam[i]);  // unconditional (no INAM block) -> always emit
                continue;
            }
            if (const auto r = EvalCondList(cit->second, ctx, 0); r.has_value() && *r)
                out.push_back(cat.lnam[i]);
            // nullopt (perk/display gated) -> excluded
        }
        return out;
    }

    // Parse a PNDT top-level group's records into the species map + per-planet trait keyword sets
    // from KWDA. Override semantics: a record REPLACES any earlier file's version wholesale — an
    // override whose species/trait lists come out empty ERASES the earlier entry.
    void ParsePndtGroup(const std::vector<std::uint8_t>& group, const Remapper& remap,
                        Esm::PlanetSpeciesMap& map,
                        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& planetTraits,
                        Esm::PlanetStarMap& planetStars)
    {
        std::vector<std::uint8_t> decomp;
        std::size_t               i = 0;
        while (i + 24 <= group.size())
        {
            const std::uint8_t* rec = group.data() + i;
            std::uint32_t       sig, size, flags, formid;
            std::memcpy(&sig, rec + 0, 4);
            std::memcpy(&size, rec + 4, 4);
            std::memcpy(&flags, rec + 8, 4);
            std::memcpy(&formid, rec + 12, 4);
            i += 24;
            if (sig == kSigGRUP)
            {
                i += (size < 24) ? 0 : size - 24;  // nested group: skip its body
                continue;
            }
            if (size > group.size() - i)
                break;
            const std::uint8_t* data     = rec + 24;
            std::size_t         dataSize = size;
            i += size;
            if (sig != kSigPNDT)
                continue;

            if (flags & kCompressedFlag)
            {
                if (size < 4)
                    continue;
                std::uint32_t decompSize;
                std::memcpy(&decompSize, data, 4);
                if (decompSize == 0 || decompSize > kMaxDecompSize)
                {
                    spdlog::warn("EsmReader: PNDT 0x{:08X} decompSize {} out of range (max {}); skipping record",
                                 formid, decompSize, kMaxDecompSize);
                    continue;
                }
                decomp.assign(decompSize, 0);
                uLongf destLen = decompSize;
                if (uncompress(decomp.data(), &destLen,
                               reinterpret_cast<const Bytef*>(data + 4), size - 4) != Z_OK)
                    continue;
                data     = decomp.data();
                dataSize = destLen;
            }

            const auto rtId = remap(formid);
            if (rtId == 0)
                continue;

            std::vector<std::uint32_t> species;
            std::vector<std::uint32_t> traitKwds;
            std::uint32_t              starId  = 0;
            bool                       hasStar = false;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSigPPBD)
                    ParsePpbd(p, sz, remap, species);
                else if (ssig == kSubKWDA && traitKwds.empty())  // first KWDA only (matches pndt_traits)
                    for (std::size_t off = 0; off + 4 <= sz; off += 4)
                    {
                        std::uint32_t kw;
                        std::memcpy(&kw, p + off, 4);
                        if (const auto rt = remap(kw); rt != 0)
                            traitKwds.push_back(rt);
                    }
                else if (ssig == kSubGNAM && sz == 12 && !hasStar)
                {
                    // 12-byte GNAM only (PNDT also carries a 4-byte float GNAM): first u32 is the
                    // parent-star id, matching the star's STDT DNAM. A star id, NOT a FormID — no
                    // remap. Sol is star id 0, so presence (hasStar) is the validity signal.
                    std::memcpy(&starId, p, 4);
                    hasStar = true;
                }
            });
            // Planet traits gate flora genetics/reproduction (func 858) regardless of species presence.
            if (!traitKwds.empty())
                planetTraits.insert_or_assign(rtId, std::move(traitKwds));
            else
                planetTraits.erase(rtId);
            // Star id: LAST-NON-EMPTY-WINS (PR #26 review) — unlike the species/trait tables, a
            // missing 12-byte GNAM in an override must NOT erase the earlier entry. DLC/Creation
            // overrides that only patch species/traits/biomes routinely omit galaxy data; erasing
            // here would orphan those planets from the system scope (CompleteSystem would refuse on
            // them). There is no authored "remove this body's parent star" to honour — every base
            // PNDT carries a GNAM — so retention is always the correct read.
            if (hasStar)
                planetStars.insert_or_assign(rtId, starId);
            if (species.empty())
            {
                map.erase(rtId);  // barren / resource-only body (or an override that emptied it)
                continue;
            }
            std::sort(species.begin(), species.end());
            species.erase(std::unique(species.begin(), species.end()), species.end());
            map.insert_or_assign(rtId, std::move(species));
        }
    }

    // Parse ONE source file: read its header + remapper, scan its top-level groups, and merge every
    // relevant record into the global tables (later files override earlier ones — insert_or_assign /
    // erase throughout). Returns false if the file could not be read at all.
    bool ParseSourceFile(const Esm::SourceFile& spec, const std::vector<Esm::SourceFile>& sources,
                         Esm::PlanetSpeciesMap& map, MarkerTables& tables, RawTables& raw)
    {
        std::ifstream f(spec.path, std::ios::binary);
        if (!f)
        {
            spdlog::warn("EsmReader: failed to open {}", spec.path.string());
            return false;
        }
        std::uint32_t            hdrFlags = 0;
        std::vector<std::string> masters;
        if (!ReadTes4Header(f, hdrFlags, masters))
        {
            spdlog::warn("EsmReader: not a TES4 plugin: {}", spec.path.string());
            return false;
        }
        const auto remap = BuildRemapper(spec, masters, sources);

        // One pass over the top-level type-groups: capture the bodies the PNDT parse + the marker
        // derivation need. Each is read once into its own buffer.
        std::vector<std::uint8_t> pndtGroup, flstGroup, cndfGroup, omodGroup, npcGroup, florGroup,
            mgefGroup, spelGroup, perkGroup;
        auto readGroupBody = [&](std::uint32_t gsize, std::vector<std::uint8_t>& dst) {
            dst.assign(gsize - 24, 0);
            if (!f.read(reinterpret_cast<char*>(dst.data()), dst.size()))
                dst.clear();
        };

        std::uint8_t hdr[24];
        while (f.read(reinterpret_cast<char*>(hdr), 24))
        {
            if (std::memcmp(hdr, "GRUP", 4) != 0)
                break;
            std::uint32_t gsize, label, gtype;
            std::memcpy(&gsize, hdr + 4, 4);
            std::memcpy(&label, hdr + 8, 4);
            std::memcpy(&gtype, hdr + 12, 4);
            if (gsize < 24)
                break;
            if (gtype == 0 && label == kSigPNDT)
                readGroupBody(gsize, pndtGroup);
            else if (gtype == 0 && label == kSigFLST)
                readGroupBody(gsize, flstGroup);
            else if (gtype == 0 && label == kSigCNDF)
                readGroupBody(gsize, cndfGroup);
            else if (gtype == 0 && label == kSigOMOD)
                readGroupBody(gsize, omodGroup);
            else if (gtype == 0 && label == kSigNPC_)
                readGroupBody(gsize, npcGroup);
            else if (gtype == 0 && label == kSigFLOR)
                readGroupBody(gsize, florGroup);
            else if (gtype == 0 && label == kSigMGEF)
                readGroupBody(gsize, mgefGroup);
            else if (gtype == 0 && label == kSigSPEL)
                readGroupBody(gsize, spelGroup);
            else if (gtype == 0 && label == kSigPERK)
                readGroupBody(gsize, perkGroup);
            else
                f.seekg(gsize - 24, std::ios::cur);
        }

        std::vector<std::uint8_t> decomp;  // reused per-record scratch buffer

        if (!pndtGroup.empty())
            ParsePndtGroup(pndtGroup, remap, map, tables.planetTraits, tables.planetStars);
        if (!flstGroup.empty())
            BuildCatalogs(flstGroup.data(), flstGroup.size(), decomp, remap,
                          tables.floraCatalog, tables.faunaCatalog);
        if (!cndfGroup.empty())
            BuildCndf(cndfGroup.data(), cndfGroup.size(), decomp, remap, tables.cndf);
        if (!mgefGroup.empty())
            CollectMgefMask(mgefGroup.data(), mgefGroup.size(), decomp, remap, raw.mgefMask);
        if (!spelGroup.empty())
            CollectSpelEfids(spelGroup.data(), spelGroup.size(), decomp, remap, raw.spelEfids);
        if (!perkGroup.empty())
            CollectPerkSpels(perkGroup.data(), perkGroup.size(), decomp, remap, raw.perkSpels);
        if (!omodGroup.empty())
            CollectOmodRaw(omodGroup.data(), omodGroup.size(), decomp, remap, raw.omod);
        if (!npcGroup.empty())
        {
            ForEachRecordInGroup(npcGroup.data(), npcGroup.size(), decomp,
                                 [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                     std::size_t dataSize) {
                if (sig != kSigNPC_)
                    return;
                if (const auto rt = remap(formid); rt != 0)
                    raw.npcObts.insert_or_assign(rt, NpcObtsOmods(data, dataSize, remap));
            });
        }
        if (!florGroup.empty())
        {
            ForEachRecordInGroup(florGroup.data(), florGroup.size(), decomp,
                                 [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                     std::size_t dataSize) {
                if (sig != kSigFLOR)
                    return;
                if (const auto rt = remap(formid); rt != 0)
                    raw.florRepro.insert_or_assign(rt, FloraReproN(data, dataSize, remap));
            });
        }

        spdlog::debug("EsmReader: parsed {} (type={}, index={}): PNDT={}B FLST={}B CNDF={}B OMOD={}B "
                      "NPC_={}B FLOR={}B MGEF={}B SPEL={}B PERK={}B",
                      spec.path.filename().string(), static_cast<int>(spec.type), spec.runtimeIndex,
                      pndtGroup.size(), flstGroup.size(), cndfGroup.size(), omodGroup.size(),
                      npcGroup.size(), florGroup.size(), mgefGroup.size(), spelGroup.size(),
                      perkGroup.size());
        return true;
    }

    // Resolve the cross-file chains once every source file has been merged: MGEF -> SPEL -> PERK ->
    // OMOD ability masks, then per-species granted keywords + ability mask (fauna) / repro N (flora).
    void ResolveMarkerChains(const RawTables& raw, MarkerTables& tables)
    {
        std::unordered_map<std::uint32_t, std::uint8_t> spelMask;
        for (const auto& [spel, efids] : raw.spelEfids)
        {
            std::uint8_t mask = 0;
            for (const auto mgef : efids)
                if (const auto it = raw.mgefMask.find(mgef); it != raw.mgefMask.end())
                    mask |= it->second;
            if (mask)
                spelMask.emplace(spel, mask);
        }
        std::unordered_map<std::uint32_t, std::uint8_t> perkMask;
        for (const auto& [perk, spels] : raw.perkSpels)
        {
            std::uint8_t mask = 0;
            for (const auto spel : spels)
                if (const auto it = spelMask.find(spel); it != spelMask.end())
                    mask |= it->second;
            if (mask)
                perkMask.emplace(perk, mask);
        }
        std::unordered_map<std::uint32_t, std::uint8_t> omodMask;
        for (const auto& [omodId, entry] : raw.omod)
        {
            std::uint8_t mask = 0;
            for (const auto perk : entry.nprk)
                if (const auto it = perkMask.find(perk); it != perkMask.end())
                    mask |= it->second;
            if (mask)
                omodMask.emplace(omodId, mask);
        }

        std::size_t faunaWithAbility = 0;
        for (const auto& [npc, omods] : raw.npcObts)
        {
            auto& info   = tables.species[npc];
            info.kingdom = Kingdom::kFauna;
            info.grantedKw.clear();
            info.abilityMask = 0;
            for (const auto omodId : omods)
            {
                if (const auto it = raw.omod.find(omodId); it != raw.omod.end())
                    for (const auto kw : it->second.nkey)
                        if (std::find(info.grantedKw.begin(), info.grantedKw.end(), kw) ==
                            info.grantedKw.end())
                            info.grantedKw.push_back(kw);
                if (const auto it = omodMask.find(omodId); it != omodMask.end())
                    info.abilityMask |= it->second;
            }
            if (info.abilityMask)
                ++faunaWithAbility;
        }
        for (const auto& [flor, repro] : raw.florRepro)
        {
            auto& info   = tables.species[flor];
            info.kingdom = Kingdom::kFlora;
            info.reproAv = repro;
        }

        spdlog::info("EsmReader: markers resolved — floraCatalog={} faunaCatalog={} cndf={} omod={} "
                     "mgef={} spel={} perk={} | NPC_ {} ({} with ability) FLOR {}",
                     tables.floraCatalog.lnam.size(), tables.faunaCatalog.lnam.size(),
                     tables.cndf.size(), raw.omod.size(), raw.mgefMask.size(), raw.spelEfids.size(),
                     raw.perkSpels.size(), raw.npcObts.size(), faunaWithAbility, raw.florRepro.size());
    }

    void BuildMap(Esm::PlanetSpeciesMap& map, MarkerTables& tables)
    {
        const auto sources = ResolveSources();
        if (sources.empty())
        {
            spdlog::warn("EsmReader: no source files resolved");
            return;
        }

        RawTables   raw;
        std::size_t parsed = 0;
        for (const auto& spec : sources)
            parsed += ParseSourceFile(spec, sources, map, tables, raw) ? 1 : 0;

        ResolveMarkerChains(raw, tables);

        std::size_t speciesCount = 0;
        for (const auto& [_, v] : map)
            speciesCount += v.size();
        spdlog::info("EsmReader: loaded {} biome planets, {} species refs, {} planet-trait sets, "
                     "{} planet-star ids from {}/{} source files",
                     map.size(), speciesCount, tables.planetTraits.size(), tables.planetStars.size(),
                     parsed, sources.size());
    }
}

namespace Esm
{
    namespace
    {
        PlanetSpeciesMap  g_map;
        MarkerTables      g_markers;
        std::once_flag    g_once;

        // Parse once. Never let a failure escape: a throw out of call_once leaves the once_flag UNSET
        // (so every later call retries the failing parse) and would propagate across the Papyrus
        // native boundary. Degrade to empty maps + one error line instead.
        void EnsureParsed()
        {
            std::call_once(g_once, [] {
                try
                {
                    BuildMap(g_map, g_markers);
                }
                catch (const std::exception& e)
                {
                    spdlog::error("EsmReader: BuildMap failed ({}); species maps left empty", e.what());
                }
                catch (...)
                {
                    spdlog::error("EsmReader: BuildMap failed (unknown); species maps left empty");
                }
            });
        }
    }

    void SetSources(std::vector<SourceFile> sources)
    {
        std::lock_guard lock(g_sourcesMtx);
        g_sources = std::move(sources);
    }

    const PlanetSpeciesMap& GetPlanetSpecies()
    {
        EnsureParsed();
        return g_map;
    }

    const PlanetStarMap& GetPlanetStarIds()
    {
        EnsureParsed();
        return g_markers.planetStars;
    }

    std::vector<std::uint32_t> GetSpeciesMarkers(std::uint32_t speciesFormId, std::uint32_t planetFormId)
    {
        EnsureParsed();

        const auto it = g_markers.species.find(speciesFormId);
        if (it == g_markers.species.end())
            return {};  // unknown form (not an NPC_/FLOR we parsed) -> caller leaves it blue
        const SpeciesMarkerInfo& info = it->second;

        // Planet traits gate flora genetics/reproduction (func 858). Empty for an unknown/0 planet —
        // which yields the trait-free default set (Standard/Carbon genetics, direct reproduction by N).
        static const std::vector<std::uint32_t> kNoIds;
        const auto                               tit = g_markers.planetTraits.find(planetFormId);
        const std::vector<std::uint32_t>&        traits =
            (tit != g_markers.planetTraits.end()) ? tit->second : kNoIds;

        EvalCtx ctx;
        ctx.cndf   = &g_markers.cndf;
        ctx.traits = &traits;
        if (info.kingdom == Kingdom::kFauna)
        {
            ctx.skw = &info.grantedKw;   // func-560 tests the NPC_'s granted keyword set
            ctx.av  = 0;
            return EvalCatalog(g_markers.faunaCatalog, ctx);
        }
        if (info.kingdom == Kingdom::kFlora)
        {
            ctx.skw = &kNoIds;           // flora markers do not gate on a species keyword set
            ctx.av  = info.reproAv;      // func-14 GetActorValue(reproduction)
            return EvalCatalog(g_markers.floraCatalog, ctx);
        }
        return {};
    }

    std::vector<std::uint32_t> GetSpeciesActorMarkers(std::uint32_t speciesFormId)
    {
        EnsureParsed();
        const auto it = g_markers.species.find(speciesFormId);
        if (it == g_markers.species.end() || it->second.kingdom != Kingdom::kFauna)
            return {};
        return AbilityMaskToMarkers(it->second.abilityMask);
    }
}
