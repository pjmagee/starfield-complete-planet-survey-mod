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
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
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
    constexpr std::uint32_t kSubEFID = 0x44494645;  // 'EFID' (SPEL effect -> MGEF form id)
    constexpr std::uint32_t kSubPRKE = 0x454B5250;  // 'PRKE' (PERK entry header; byte0 == 1 == Ability)

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

    // Sanity ceiling on a single PNDT record's *decompressed* size, read straight from the file
    // before we allocate for it. A real planet record inflates to tens of KB; this 64 MiB cap
    // turns a corrupt/hostile decompSize (e.g. 0xFFFFFFFF) into a skipped record instead of a
    // multi-GiB bad_alloc / DoS on game launch.
    constexpr std::uint32_t kMaxDecompSize = 64u * 1024u * 1024u;

    // Resolve <game-root>/Data/Starfield.esm from the running executable path. An explicit
    // CPS_ESM_PATH environment override takes precedence — handy for a non-standard game layout and
    // for the offline validation harness (test/ValidateMarkers.cpp).
    std::filesystem::path ResolveEsmPath()
    {
        wchar_t envbuf[MAX_PATH] {};
        if (const auto en = GetEnvironmentVariableW(L"CPS_ESM_PATH", envbuf, MAX_PATH); en > 0 && en < MAX_PATH)
            return std::filesystem::path {envbuf};
        wchar_t buf[MAX_PATH] {};
        const auto n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return std::filesystem::path {buf}.parent_path() / L"Data" / L"Starfield.esm";
    }

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

    // Parse one PPBD payload (a single biome) and append its flora + fauna FormIDs.
    //   u32 biome | u32 chance | u32 unk | u32 RSGD
    //   u32 nFauna | nFauna x u32 (NPC_)
    //   u32 nKw    | nKw    x u32 (KYWD)
    //   u32 nFlora | u32 entrySize(>=9) | nFlora x { u32 FLOR, u32 MISC, u8 freq, pad to entrySize }
    void ParsePpbd(const std::uint8_t* data, std::size_t size, std::vector<std::uint32_t>& out)
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
            if (const auto f = c.u32(); f != 0)
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
            if (flor != 0)
                out.push_back(flor);
            c.off += entrySize;
        }
    }

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

    // All one-time lookup tables for marker derivation. The per-species result + the two catalogs +
    // the CNDF map + the per-planet trait sets persist (consumed at GetSpeciesMarkers time); the
    // heavy OMOD NKEY map stays local to BuildMarkers.
    struct MarkerTables
    {
        std::unordered_map<std::uint32_t, SpeciesMarkerInfo>          species;
        Catalog                                                      floraCatalog;
        Catalog                                                      faunaCatalog;
        std::unordered_map<std::uint32_t, std::vector<Ctda>>         cndf;          // CNDF id -> CTDA list
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> planetTraits;  // planet -> KWDA traits
    };

    // Walk every record in a top-level group body (records + nested GRUPs), decompressing as needed,
    // calling fn(sig, formid, decompressedData, dataSize). The record body is borrowed from either
    // `group` (uncompressed) or `decomp` (a caller-owned scratch buffer reused per record) — do not
    // retain the pointer past the callback.
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

    // Parse a 32-byte CTDA payload.
    Ctda ParseCtda(const std::uint8_t* p)
    {
        Ctda c;
        c.op = p[kCtdaOpOff];
        std::memcpy(&c.comp, p + kCtdaCompOff, 4);
        std::memcpy(&c.func, p + kCtdaFuncOff, 2);
        std::memcpy(&c.param1, p + kCtdaParam1Off, 4);
        return c;
    }

    // Parse one catalog FLST record's subrecords into (lnam[], condByIdx). Sequential walk: each LNAM
    // appends a marker id (in catalog order); each INAM opens a conditioned block for the LNAM index
    // it carries; CTDA payloads accumulate into the currently-open block until the next INAM/end.
    // (Mirrors parse_flst in esm_derive_markers.py; CITC and other subrecords are ignored.)
    void ParseCatalogRecord(const std::uint8_t* data, std::size_t dataSize, Catalog& out)
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
                out.lnam.push_back(m);
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
                cur.push_back(ParseCtda(p));
            }
        });
        flush();
    }

    // Find + parse the flora (0x00160C96) and fauna (0x00160C97) catalog FLSTs from the FLST group.
    void BuildCatalogs(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                       Catalog& flora, Catalog& fauna)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigFLST)
                return;
            if (formid == kCatalogFlora)
                ParseCatalogRecord(data, dataSize, flora);
            else if (formid == kCatalogFauna)
                ParseCatalogRecord(data, dataSize, fauna);
        });
    }

    // Parse every CNDF record's CTDA list into the map (func-837 EvaluateConditionForm targets).
    void BuildCndf(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                   std::unordered_map<std::uint32_t, std::vector<Ctda>>& cndf)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigCNDF)
                return;
            std::vector<Ctda> items;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSubCTDA && sz >= kCtdaSize)
                    items.push_back(ParseCtda(p));
            });
            cndf[formid] = std::move(items);
        });
    }

    // One pass over OMOD: build (a) omod -> [NKEY keyword ids] (func-560 grants: temperament + enviro)
    // and (b) omod -> ability mask (func-699: NPRK -> perk -> ability mask, via the prebuilt perkMask).
    // NKEY and NPRK are inline 4CC tags inside the DATA blob, each followed by a u32 (keyword / perk id).
    // (Mirrors EsmDB.omod_nkey + omod_nprk.)
    void BuildOmodTables(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                         const std::unordered_map<std::uint32_t, std::uint8_t>&          perkMask,
                         std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>&  omodNkey,
                         std::unordered_map<std::uint32_t, std::uint8_t>&                omodAbilityMask)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigOMOD)
                return;
            std::vector<std::uint32_t> nk;
            std::uint8_t               mask = 0;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig != kSubDATA)
                    return;
                for (std::size_t off = 0; off + 8 <= sz;)
                {
                    std::uint32_t tag;
                    std::memcpy(&tag, p + off, 4);
                    if (tag == kSubNKEY)
                    {
                        std::uint32_t kw;
                        std::memcpy(&kw, p + off + 4, 4);
                        nk.push_back(kw);
                        off += 8;
                    }
                    else if (tag == kSubNPRK)
                    {
                        std::uint32_t perk;
                        std::memcpy(&perk, p + off + 4, 4);
                        if (const auto it = perkMask.find(perk); it != perkMask.end())
                            mask |= it->second;
                        off += 8;
                    }
                    else
                        off += 1;
                }
            });
            if (!nk.empty())
                omodNkey.emplace(formid, std::move(nk));
            if (mask)
                omodAbilityMask.emplace(formid, mask);
        });
    }

    // The keyword set the engine GRANTS one NPC_ at build time, via OBTS -> OMOD -> DATA 'NKEY'. This
    // is exactly what func-560 HasKeyword(species) tests against in the fauna catalog. (Mirrors
    // npc_granted_keywords.)
    std::vector<std::uint32_t> NpcGranted(const std::uint8_t* data, std::size_t dataSize,
                                          const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& omodNkey)
    {
        std::vector<std::uint32_t> granted;
        ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
            if (ssig != kSubOBTS || sz < kObtsEntriesOff)
                return;
            for (std::size_t off = kObtsEntriesOff; off + kObtsEntryStride <= sz; off += kObtsEntryStride)
            {
                std::uint32_t omodId;
                std::memcpy(&omodId, p + off, 4);
                const auto it = omodNkey.find(omodId);
                if (it == omodNkey.end())
                    continue;
                for (const auto kw : it->second)
                    if (std::find(granted.begin(), granted.end(), kw) == granted.end())
                        granted.push_back(kw);
            }
        });
        return granted;
    }

    // The FLOR's reproduction value N from the first PRPS triple with AVIF == 0x0023E905 (func-14
    // input). 0 if none (which is also what func 14 yields for an absent value). (Mirrors flor_prps_n.)
    std::int32_t FloraReproN(const std::uint8_t* data, std::size_t dataSize)
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
                if (avif == kAvifPlantReproduction)
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
    // KWDA scanner keyword. Built bottom-up as a 3-bit mask. (Mirrors esm_derive_markers.py actor path.)

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

    // MGEF form id -> ability mask (from the scanner keywords in its KWDA).
    void BuildMgefMask(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                       std::unordered_map<std::uint32_t, std::uint8_t>& mgefMask)
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
                    mask |= KwToAbilityBit(kw);
                }
            });
            if (mask)
                mgefMask.emplace(formid, mask);
        });
    }

    // SPEL form id -> ability mask (OR over its EFID effect MGEFs).
    void BuildSpelMask(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                       const std::unordered_map<std::uint32_t, std::uint8_t>& mgefMask,
                       std::unordered_map<std::uint32_t, std::uint8_t>&       spelMask)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigSPEL)
                return;
            std::uint8_t mask = 0;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig != kSubEFID || sz < 4)
                    return;
                std::uint32_t mgef;
                std::memcpy(&mgef, p, 4);
                if (const auto it = mgefMask.find(mgef); it != mgefMask.end())
                    mask |= it->second;
            });
            if (mask)
                spelMask.emplace(formid, mask);
        });
    }

    // PERK form id -> ability mask. A PRKE entry whose first byte == 1 is an Ability entry whose
    // following DATA's first u32 is a SPEL added to the perk OWNER (the creature). OR that spell's mask.
    void BuildPerkMask(const std::uint8_t* group, std::size_t groupSize, std::vector<std::uint8_t>& decomp,
                       const std::unordered_map<std::uint32_t, std::uint8_t>& spelMask,
                       std::unordered_map<std::uint32_t, std::uint8_t>&       perkMask)
    {
        ForEachRecordInGroup(group, groupSize, decomp,
                             [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                 std::size_t dataSize) {
            if (sig != kSigPERK)
                return;
            std::uint8_t mask = 0;
            int          cur  = -1;  // current PRKE entry type byte (-1 = none / already consumed)
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSubPRKE && sz >= 1)
                    cur = p[0];
                else if (ssig == kSubDATA && cur == 1 && sz >= 4)
                {
                    std::uint32_t spel;
                    std::memcpy(&spel, p, 4);
                    if (const auto it = spelMask.find(spel); it != spelMask.end())
                        mask |= it->second;
                    cur = -1;  // consume this entry's DATA
                }
            });
            if (mask)
                perkMask.emplace(formid, mask);
        });
    }

    // One NPC_'s ability mask: OBTS -> OMOD -> (omodAbilityMask). (Mirrors npc_actor_magfx_keywords.)
    std::uint8_t NpcAbilityMask(const std::uint8_t* data, std::size_t dataSize,
                                const std::unordered_map<std::uint32_t, std::uint8_t>& omodAbilityMask)
    {
        std::uint8_t mask = 0;
        ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
            if (ssig != kSubOBTS || sz < kObtsEntriesOff)
                return;
            for (std::size_t off = kObtsEntriesOff; off + kObtsEntryStride <= sz; off += kObtsEntryStride)
            {
                std::uint32_t omodId;
                std::memcpy(&omodId, p + off, 4);
                if (const auto it = omodAbilityMask.find(omodId); it != omodAbilityMask.end())
                    mask |= it->second;
            }
        });
        return mask;
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

    // Parse the PNDT top-level group's records into the species map + per-planet trait keyword
    // sets from KWDA.
    void ParsePndtGroup(const std::vector<std::uint8_t>& group, Esm::PlanetSpeciesMap& map,
                        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& planetTraits)
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

            std::vector<std::uint32_t> species;
            std::vector<std::uint32_t> traitKwds;
            ForEachSubrecord(data, dataSize, [&](std::uint32_t ssig, const std::uint8_t* p, std::size_t sz) {
                if (ssig == kSigPPBD)
                    ParsePpbd(p, sz, species);
                else if (ssig == kSubKWDA && traitKwds.empty())  // first KWDA only (matches pndt_traits)
                    for (std::size_t off = 0; off + 4 <= sz; off += 4)
                    {
                        std::uint32_t kw;
                        std::memcpy(&kw, p + off, 4);
                        traitKwds.push_back(kw);
                    }
            });
            // Planet traits gate flora genetics/reproduction (func 858) regardless of species presence.
            if (!traitKwds.empty())
                planetTraits.emplace(formid, std::move(traitKwds));
            if (species.empty())
                continue;  // barren / resource-only body — nothing to add to the species map
            std::sort(species.begin(), species.end());
            species.erase(std::unique(species.begin(), species.end()), species.end());
            map.emplace(formid, std::move(species));
        }
    }

    // Build the per-species marker tables from the in-memory group bodies. Order: catalogs (FLST) +
    // CNDF first, then OMOD NKEY (local), then the NPC_/FLOR per-species inputs that the catalog
    // evaluation consumes. The OMOD NKEY map is local — only the per-species inputs + catalogs + CNDF
    // outlive this function (kept in `tables`). All inputs are owned by the caller's group buffers.
    void BuildMarkers(const std::vector<std::uint8_t>& flstGroup, const std::vector<std::uint8_t>& cndfGroup,
                      const std::vector<std::uint8_t>& omodGroup, const std::vector<std::uint8_t>& npcGroup,
                      const std::vector<std::uint8_t>& florGroup, const std::vector<std::uint8_t>& mgefGroup,
                      const std::vector<std::uint8_t>& spelGroup, const std::vector<std::uint8_t>& perkGroup,
                      MarkerTables& tables)
    {
        std::vector<std::uint8_t> decomp;  // reused per-record scratch buffer

        // 1) The two HandScanner catalogs (flora 0x00160C96 / fauna 0x00160C97).
        if (!flstGroup.empty())
            BuildCatalogs(flstGroup.data(), flstGroup.size(), decomp, tables.floraCatalog, tables.faunaCatalog);

        // 2) CNDF condition forms (func-837 targets: the genetics + reproduction sub-trees).
        if (!cndfGroup.empty())
            BuildCndf(cndfGroup.data(), cndfGroup.size(), decomp, tables.cndf);

        // 3) func-699 ability chain, bottom-up: MGEF -> SPEL -> PERK -> (OMOD NPRK) ability masks.
        std::unordered_map<std::uint32_t, std::uint8_t> mgefMask, spelMask, perkMask, omodAbilityMask;
        if (!mgefGroup.empty())
            BuildMgefMask(mgefGroup.data(), mgefGroup.size(), decomp, mgefMask);
        if (!spelGroup.empty())
            BuildSpelMask(spelGroup.data(), spelGroup.size(), decomp, mgefMask, spelMask);
        if (!perkGroup.empty())
            BuildPerkMask(perkGroup.data(), perkGroup.size(), decomp, spelMask, perkMask);

        // 4) OMOD -> NKEY keyword grants (func-560) + NPRK ability masks (func-699), in one pass.
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> omodNkey;
        if (!omodGroup.empty())
            BuildOmodTables(omodGroup.data(), omodGroup.size(), decomp, perkMask, omodNkey, omodAbilityMask);

        // 5) Per-NPC_: granted keyword set (func-560) + ability mask (func-699).
        std::size_t faunaTotal = 0, faunaWithAbility = 0;
        if (!npcGroup.empty())
        {
            ForEachRecordInGroup(npcGroup.data(), npcGroup.size(), decomp,
                                 [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                     std::size_t dataSize) {
                if (sig != kSigNPC_)
                    return;
                auto& info       = tables.species[formid];
                info.kingdom     = Kingdom::kFauna;
                info.grantedKw   = NpcGranted(data, dataSize, omodNkey);
                info.abilityMask = NpcAbilityMask(data, dataSize, omodAbilityMask);
                ++faunaTotal;
                if (info.abilityMask)
                    ++faunaWithAbility;
            });
        }

        // 6) Per-FLOR reproduction value N (FLOR PRPS) for func-14.
        std::size_t floraTotal = 0;
        if (!florGroup.empty())
        {
            ForEachRecordInGroup(florGroup.data(), florGroup.size(), decomp,
                                 [&](std::uint32_t sig, std::uint32_t formid, const std::uint8_t* data,
                                     std::size_t dataSize) {
                if (sig != kSigFLOR)
                    return;
                auto& info   = tables.species[formid];
                info.kingdom = Kingdom::kFlora;
                info.reproAv = FloraReproN(data, dataSize);
                ++floraTotal;
            });
        }

        spdlog::info("EsmReader: markers built — floraCatalog={} faunaCatalog={} cndf={} omodNkey={} "
                     "mgef={} spel={} perk={} | NPC_ {} ({} with ability) FLOR {}",
                     tables.floraCatalog.lnam.size(), tables.faunaCatalog.lnam.size(), tables.cndf.size(),
                     omodNkey.size(), mgefMask.size(), spelMask.size(), perkMask.size(), faunaTotal,
                     faunaWithAbility, floraTotal);
    }

    void BuildMap(Esm::PlanetSpeciesMap& map, MarkerTables& tables)
    {
        const auto path = ResolveEsmPath();
        if (path.empty())
        {
            spdlog::warn("EsmReader: could not resolve Starfield.esm path");
            return;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            spdlog::warn("EsmReader: failed to open {}", path.string());
            return;
        }

        std::uint8_t hdr[24];
        f.read(reinterpret_cast<char*>(hdr), 24);
        if (!f || std::memcmp(hdr, "TES4", 4) != 0)
        {
            spdlog::warn("EsmReader: not a TES4 plugin");
            return;
        }
        std::uint32_t tes4Size;
        std::memcpy(&tes4Size, hdr + 4, 4);
        f.seekg(tes4Size, std::ios::cur);  // skip TES4 record data

        // One pass over the top-level type-groups: capture the bodies the PNDT parse + the marker
        // derivation need (PNDT, FLST, CNDF, OMOD, NPC_, FLOR, + MGEF/SPEL/PERK for the func-699 ability
        // chain). Each is read once into its own buffer.
        std::vector<std::uint8_t> pndtGroup, flstGroup, cndfGroup, omodGroup, npcGroup, florGroup,
            mgefGroup, spelGroup, perkGroup;
        auto readGroupBody = [&](std::uint32_t gsize, std::vector<std::uint8_t>& dst) {
            dst.assign(gsize - 24, 0);
            if (!f.read(reinterpret_cast<char*>(dst.data()), dst.size()))
                dst.clear();
        };

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

        if (!pndtGroup.empty())
            ParsePndtGroup(pndtGroup, map, tables.planetTraits);

        BuildMarkers(flstGroup, cndfGroup, omodGroup, npcGroup, florGroup, mgefGroup, spelGroup, perkGroup,
                     tables);

        std::size_t speciesCount = 0;
        for (const auto& [_, v] : map)
            speciesCount += v.size();
        spdlog::info("EsmReader: loaded {} biome planets, {} species refs, {} planet-trait sets from Starfield.esm",
                     map.size(), speciesCount, tables.planetTraits.size());
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

    const PlanetSpeciesMap& GetPlanetSpecies()
    {
        EnsureParsed();
        return g_map;
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
