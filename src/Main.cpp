#include "PCH.h"

#include "EsmReader.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <type_traits>
#include <unordered_map>

// Address Library IDs for Starfield 1.16.236.0–1.16.244.0 — discovered via Ghidra.
// See memory/re_progress.md for the derivation and the knowledge-DB architecture.
//
// ID_126578: getter for the per-save knowledge-manager singleton.
//            The knowledge DB pointer lives at manager+0x8B0.
// ID_52155 : SetTraitKnown inner impl. (uint32 planet_id, BGSKeyword*, bool known).
//            Internally calls ID_52156 + fires a progress event. Safe for trait forms.
//
// Planet form's knowledge key is a uint32 at offset 0x54 on the planet form.
namespace Engine
{
    using fn_get_manager_t     = std::uintptr_t (*)();
    using fn_set_trait_known_t = void (*)(std::uint32_t planetId, std::uintptr_t keyword, bool known);
    // ID_126806: BSTHashMap lookup. Signature (container, out_buf[4 ulongs], &key_u64)
    //   out_buf layout on success: out[2] = entry base ptr, out[3] = entry index.
    //   Failure sentinel: out[3] == 0xfe0 and out[2] == 0.
    using fn_db_lookup_t = void* (*)(std::uintptr_t* container, std::uintptr_t out[4], const std::uint64_t* key);
    // ID_124898: per-species flag increment on a "subobj" (value + 0x20).
    //   Signature (subobj*, species_id, delta_byte, ?).
    //   Finds/creates entry for species_id and increments the scan-flag byte at entry+0x21.
    using fn_incr_flag_t = void (*)(void* subobj, std::uint32_t species_id, std::uint8_t delta, std::uint64_t zero);
    // ID_124899: per-species PERCENT-byte writer on the same subobj as IncrementScanFlag.
    //   Signature (subobj*, species_id, percent_byte, ?). Writes the scanned-% byte at the
    //   entry's +0x20 (sibling of the scan-flag at +0x21). The real scan (ID_52158) sets BOTH;
    //   GetSurveyPercent counts on +0x21, but the UI/other categories read +0x20, so we set both.
    //   NOTE: param_3 is a BYTE — pass a literal 0..100, never a float.
    using fn_set_percent_t = void (*)(void* subobj, std::uint32_t species_id, std::uint8_t percent, std::uint64_t zero);

    inline REL::Relocation<fn_get_manager_t>     GetKnowledgeManager {REL::ID(126578)};
    inline REL::Relocation<fn_set_trait_known_t> SetTraitKnownNative {REL::ID(52155)};
    inline REL::Relocation<fn_db_lookup_t>       DbLookup {REL::ID(126806)};
    inline REL::Relocation<fn_incr_flag_t>       IncrementScanFlag {REL::ID(124898)};
    inline REL::Relocation<fn_set_percent_t>     SetPercentByte {REL::ID(124899)};
    inline REL::Relocation<std::uint16_t*>       TraitDiscriminator {REL::ID(938333)};

    // ID_1016657: per-planet survey aggregator constructor.
    //   (buffer, planet_id) — populates buffer with all tracked form IDs for the planet
    //   across four arrays (two uint-arrays for flora/trait ids, two ptr-arrays for resource/other).
    //   Buffer size seen in callers: >= 0x250 bytes. We allocate 0x400 to be safe.
    using fn_aggregator_t = void (*)(void* buffer, std::uint32_t planet_id);
    // ID_65318: cleanup for the aggregator buffer.
    using fn_buffer_free_t = void (*)(void* buffer);

    inline REL::Relocation<fn_aggregator_t>  SurveyAggregator {REL::ID(1016657)};
    inline REL::Relocation<fn_buffer_free_t> SurveyBufferFree {REL::ID(65318)};

    // ID_97853: survey check-and-dispatch. Called by SetTraitKnown/SetScanned flows after a write.
    //   Signature: (struct*) where the struct starts with { uint32 planet_id, float prev_pct, u8 flag, u8 skip }.
    //   Fires PlayerPlanetSurveyProgressEvent (conditional) and PlayerPlanetSurveyCompleteEvent
    //   if the planet's survey is now 100%. The Complete event is what generates the in-world
    //   "<Planet> Survey Data" slate in the player's inventory.
    using fn_survey_notify_t = void (*)(void* ctx);
    inline REL::Relocation<fn_survey_notify_t> SurveyCheckNotify {REL::ID(97853)};

    // ID_102650: the engine's ref-free "scan & fully survey a planet" entry point —
    // what a starmap/orbital scan ultimately drives. It resolves the knowledge DB
    // itself, then (via ID_102651) sets the surveyed bit, CREATES the entry if
    // missing (ID_52204), fires the survey-complete event (→ the Survey Data slate
    // reward), and recurses over the planet's moons. Self-contained: no spawn, no
    // teleport, no async two-phase. Args: (unused-context, planetId, fullFlag=1).
    using fn_scan_complete_t = void (*)(std::int64_t context, std::uint32_t planetId, std::uint8_t fullFlag);
    inline REL::Relocation<fn_scan_complete_t> ScanCompletePlanet {REL::ID(102650)};

    // Offsets within knowledge-manager / DB structs (Starfield 1.16.236.0–1.16.244.0, Ghidra-derived).
    constexpr std::size_t  kPlanetIdOffset       = 0x54;   // uint32 knowledge key at planetForm+0x54
    constexpr std::size_t  kManagerDbOffset      = 0x8B0;  // knowledge DB ptr at manager+0x8B0 (ID_126578 result)
    constexpr std::size_t  kDbContainerOffset    = 0x268;  // BSTHashMap<> start within the DB object
    constexpr std::size_t  kBucketOffsetTableOff = 0x12;   // uint16[] offset table start within a bucket base
    constexpr std::size_t  kEntrySubobjOffset    = 0x20;   // species subobj relative to the resolved entry ptr
    constexpr std::size_t  kFormPtrFormIdOffset  = 0x28;   // formID field in a TESForm* (aggregator ptr-arrays)
    constexpr std::uint8_t kBiomeScanCategory    = 0x0d;   // category byte for PlanetProgressNative (ID_52157)

    // BSTHashMap lookup sentinel: out[3] value when the key is not found.
    constexpr std::uintptr_t kDbLookupNotFound     = 0xfe0;   // db+0x268 survey-container miss sentinel (ID_126806)
    // Invalid/sentinel form ID used in aggregator arrays for empty slots.
    constexpr std::uint32_t  kInvalidFormId         = 0xFFFFFFFFu;
    // Default delta for scan-flag increment (marks species fully scanned in one pass).
    constexpr std::uint8_t   kDefaultScanDelta      = 100;
    // Per-species percent byte value for "fully scanned" (ID_124899). Survey % counts on the
    // scan-flag byte, but the UI/secondary categories read this percent — set it to complete.
    constexpr std::uint8_t   kScanPercentComplete   = 100;
    // Maximum delta value (uint8 ceiling).
    constexpr std::uint8_t   kMaxScanDelta           = 255;

    // BSTArray header offsets within TESObjectCELL (Starfield 1.16.236.0–1.16.244.0).
    constexpr std::size_t kCellRefArraySize     = 0x080;
    constexpr std::size_t kCellRefArrayCapacity = 0x084;
    constexpr std::size_t kCellRefArrayData     = 0x088;

    // x86-64 CALL instruction: opcode E8 followed by a 4-byte relative displacement.
    constexpr std::uint8_t kX86CallOpcode       = 0xE8;
    constexpr std::size_t  kX86CallInsnLength   = 5;

    // Aggregator buffer (ID_1016657) layout — four {begin*, end*} span descriptors.
    // Two uint32[] spans (inline form IDs: traits / flora) and two TESForm*[] spans.
    constexpr std::size_t kAggUintSpan0Begin = 0x218;
    constexpr std::size_t kAggUintSpan0End   = 0x220;
    constexpr std::size_t kAggUintSpan1Begin = 0x230;
    constexpr std::size_t kAggUintSpan1End   = 0x238;
    constexpr std::size_t kAggPtrSpan0Begin  = 0x1e8;
    constexpr std::size_t kAggPtrSpan0End    = 0x1f0;
    constexpr std::size_t kAggPtrSpan1Begin  = 0x200;
    constexpr std::size_t kAggPtrSpan1End    = 0x208;

    std::uintptr_t GetKnowledgeDB()
    {
        const auto manager = GetKnowledgeManager();
        if (!manager)
            return 0;
        return *reinterpret_cast<std::uintptr_t*>(manager + kManagerDbOffset);
    }

    std::uint32_t ReadPlanetId(const RE::TESForm* planetForm)
    {
        if (!planetForm)
            return 0;
        return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(planetForm) +
                                                       kPlanetIdOffset);
    }

    // ID_124901: the engine's species-slot hash (FNV-1a of the 4-byte species id) -> slot index in a
    // subobj's species hashmap. Used to dump the RAW per-species slot bytes so we can DIFF a full
    // scan (green+info+XP) vs a +0x21 byte-poke (half) and find the missing "species catalogued/known"
    // field the real scan writes and we don't.
    using fn_species_slot_hash_t = std::uint64_t (*)(std::uintptr_t hashmap, const void* key4);
    inline REL::Relocation<fn_species_slot_hash_t> SpeciesSlotHash {REL::ID(124901)};

    // ID_35755: BSTArray<u32>::push_back grow path — (header{begin,end,cap}, pos, &value). Allocates
    // via the engine allocator and updates the header + frees the old buffer, so the array is
    // engine-OWNED and safe to free on teardown. This is how the real scan fills slot+0x08; we use
    // it to build that array ref-free — the GREEN fix.
    using fn_bstarray_grow_t = std::uint32_t* (*)(std::int64_t* header, std::uint32_t* pos, const std::uint32_t* value);
    inline REL::Relocation<fn_bstarray_grow_t> BSTArrayU32Grow {REL::ID(35755)};

    // push_back one u32 onto a species slot's +0x08 BSTArray, matching the engine's inline push_back
    // (grow via ID_35755 when full, else in-place). slotAddr = the slot base (subobj+0x40 + idx*0x30);
    // header {begin@+0x08, end@+0x10, cap@+0x18}. Engine-owned alloc -> safe teardown.
    void PushSpeciesAttr(std::uintptr_t slotAddr, std::uint32_t id)
    {
        auto* const end = *reinterpret_cast<std::uint32_t**>(slotAddr + 0x10);
        auto* const cap = *reinterpret_cast<std::uint32_t**>(slotAddr + 0x18);
        if (end == cap)  // full (incl. empty 0==0) -> engine grow + insert
        {
            std::uint32_t v = id;
            BSTArrayU32Grow(reinterpret_cast<std::int64_t*>(slotAddr + 0x08), end, &v);
        }
        else  // spare capacity -> in-place append
        {
            *end                                                = id;
            *reinterpret_cast<std::uint32_t**>(slotAddr + 0x10) = end + 1;
        }
    }

    bool MarkTraitKnown(std::uint32_t planetId, RE::BGSKeyword* keyword)
    {
        if (!planetId || !keyword)
            return false;
        SetTraitKnownNative(planetId, reinterpret_cast<std::uintptr_t>(keyword), true);
        return true;
    }

    // ID_83006: resolve a species base FORM to its CANONICAL form. This is the missing piece.
    // The outline renderer hashes the canonical id stamped into a scanned instance's
    // ScannableComponent +0x24, which the engine computes as *(uint32*)(ID_83006(base)+0x28)
    // (scan-component-lifecycle.txt:33-35, scan-inner.txt:63-84). We were writing +0x21 under the
    // RAW ESM form id, but the renderer keys on this canonical id instead -> survey reads 100%
    // (authored-array walk) yet the outline stays BLUE. ID_83006 is FORM-level (gate ID_64338,
    // then form+0xC8 base component -> vtable[0x428] -> canonical form), so it is computable
    // OFF-PLANET from the ESM species form with NO live instance. If the canonical is
    // species-stable (shared across a species' biome variants), writing +0x21 under it greens the
    // wild creatures galaxy-wide. Returns 0 when the form isn't scannable / has no canonical.
    using fn_resolve_canonical_form_t = std::uintptr_t (*)(void* form);
    inline REL::Relocation<fn_resolve_canonical_form_t> ResolveCanonicalForm {REL::ID(83006)};

    std::uint32_t CanonicalFormId(RE::TESForm* form)
    {
        if (!form)
            return 0;
        // ID_83006 faults on forms that don't satisfy its internal gate in this build (it took
        // an access violation and latched the whole session's natives). Catch LOCALLY (/EHa) and
        // fall back to the raw id, so one bad form degrades to "no canonical" instead of killing
        // the pass. This converts the crash into per-species diagnostics.
        try
        {
            const auto canonForm = ResolveCanonicalForm(form);
            if (!canonForm)
                return 0;
            return *reinterpret_cast<std::uint32_t*>(canonForm + 0x28);
        }
        catch (...)
        {
            return 0;
        }
    }

    // Directly increment the scan-flag byte at per-planet component value's
    // species array entry for `speciesFormId`. This is the byte that ID_97851
    // reads to compute GetSurveyPercent — the only thing that matters.
    //
    // Returns 1 on success, 0 if planet not found in DB, -1 on null/invalid inputs.
    // Resolve the per-planet survey component "subobj" in the knowledge DB.
    //   subobj = entry + 0x20, where entry = out[2] + *(u16*)(out[2] + 0x12 + out[3]*4).
    // The species scan table lives at subobj+0x18 (keys) / subobj+0x40 (data); each 0x30-stride
    // entry has the PERCENT byte at +0x20 and the SCAN-FLAG byte at +0x21. subobj+0x00 itself is
    // the planet-level "attribute known" bitmask (bits read by ID_52151 / GetSurveyPercent).
    // Returns nullptr if the planet has no entry yet (create still pending / never discovered).
    std::uint8_t* ResolvePlanetSubobj(std::uintptr_t db, std::uint32_t planetId)
    {
        if (!db || !planetId)
            return nullptr;
        // 64-bit key: (survey_discriminator << 48) | (planet_id << 16).
        const std::uint16_t disc = *TraitDiscriminator.get();
        const std::uint64_t key =
            (static_cast<std::uint64_t>(disc) << 48) | (static_cast<std::uint64_t>(planetId) << 16);

        std::uintptr_t out[4]    = {0, 0, 0, kDbLookupNotFound};
        auto           container = reinterpret_cast<std::uintptr_t*>(db + kDbContainerOffset);
        DbLookup(container, out, &key);
        if (out[3] == kDbLookupNotFound && out[2] == 0)
            return nullptr;

        const auto base            = reinterpret_cast<std::uint8_t*>(out[2]);
        const auto ushortOffsetPtr = reinterpret_cast<std::uint16_t*>(base + kBucketOffsetTableOff + out[3] * 4);
        const auto entryPtr        = base + *ushortOffsetPtr;
        return entryPtr + kEntrySubobjOffset;
    }

    // Set the planet-level "attribute known" bits (magnetosphere / resources / atmosphere /
    // gravity / temperature / water etc.) that gate a large cluster of GetSurveyPercent's
    // categories. ID_97851 reads bits 0,1,2 of the bitmask at entry+0x20 (== subobj+0x00):
    // bit0 selects the +0x24 "DV" value, bits 1,2 (-> struct 0x1b0/0x1b1) gate the binary
    // attribute categories. Setting the low 3 bits marks every scan-revealed attribute known.
    // This is what a barren body (no species) needs to reach 100%. Returns true if applied.
    bool SetPlanetAttributeBits(std::uint32_t planetId)
    {
        const auto db     = GetKnowledgeDB();
        auto       subobj = ResolvePlanetSubobj(db, planetId);
        if (!subobj)
            return false;
        // subobj+0x00 is the attribute bitmask. OR in the low 3 "known" bits. Idempotent.
        *reinterpret_cast<std::uint32_t*>(subobj) |= 0x7u;
        return true;
    }

    int MarkSpeciesScannedForPlanet(std::uint32_t planetId, std::uint32_t speciesFormId, std::uint8_t delta)
    {
        if (!planetId || !speciesFormId)
            return -1;
        const auto db = GetKnowledgeDB();
        if (!db)
            return -1;

        auto subobj = ResolvePlanetSubobj(db, planetId);
        if (!subobj)
            return 0;

        // Set BOTH bytes the engine's real scan sets: the scan-flag (+0x21, what
        // GetSurveyPercent counts on) and the percent byte (+0x20, the per-species %).
        IncrementScanFlag(subobj, speciesFormId, delta, 0);
        SetPercentByte(subobj, speciesFormId, Engine::kScanPercentComplete, 0);
        return 1;
    }

    // Inverse of Esm::GetPlanetSpecies() (planetId -> [speciesFid]): speciesFid -> [planetId].
    // Built once and cached. The atomic galaxy green spawns ONE live instance per unique species,
    // then greens it on every planet that hosts it — this map is that "every planet" list.
    const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& GetSpeciesToPlanets()
    {
        static std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> inv;
        static std::once_flag                                               once;
        std::call_once(once, [] {
            for (const auto& [planetId, species] : Esm::GetPlanetSpecies())
                for (const auto fid : species)
                    inv[fid].push_back(planetId);
            spdlog::info("GetSpeciesToPlanets: {} unique species -> planet lists", inv.size());
        });
        return inv;
    }

    // Iterate every form ID the engine's aggregator tracks for a planet.
    // Runs the aggregator, walks the four span pairs (two uint32[], two TESForm*[]),
    // calls `fn(formId)` for each valid entry, then frees the buffer.
    template <typename Fn>
    void ForEachAggregatedFormId(std::uint32_t planetId, Fn&& fn, const char* diagTag = nullptr)
    {
        alignas(16) std::uint8_t buf[0x400] {};
        SurveyAggregator(buf, planetId);

        // Diagnostic: raw element count per span. A wrong span offset on a new
        // game build shows up here as 0 or an absurd length while sibling spans
        // look normal — the signature of planet-specific partial completion.
        std::size_t spanLen[4] = {0, 0, 0, 0};
        int         spanIdx    = 0;

        auto scanUint = [&](std::size_t beginOff, std::size_t endOff)
        {
            const auto* begin   = *reinterpret_cast<std::uint32_t* const*>(buf + beginOff);
            const auto* end     = *reinterpret_cast<std::uint32_t* const*>(buf + endOff);
            spanLen[spanIdx++]  = (begin && end && end >= begin) ? static_cast<std::size_t>(end - begin) : 0;
            for (auto p = begin; p && p != end; ++p)
            {
                if (*p && *p != kInvalidFormId)
                    fn(*p);
            }
        };
        auto scanPtr = [&](std::size_t beginOff, std::size_t endOff)
        {
            const auto* begin   = *reinterpret_cast<std::uintptr_t* const*>(buf + beginOff);
            const auto* end     = *reinterpret_cast<std::uintptr_t* const*>(buf + endOff);
            spanLen[spanIdx++]  = (begin && end && end >= begin) ? static_cast<std::size_t>(end - begin) : 0;
            for (auto p = begin; p && p != end; ++p)
            {
                if (!*p)
                    continue;
                auto fid = *reinterpret_cast<const std::uint32_t*>(*p + kFormPtrFormIdOffset);
                if (fid && fid != kInvalidFormId)
                    fn(fid);
            }
        };

        scanUint(kAggUintSpan0Begin, kAggUintSpan0End);
        scanUint(kAggUintSpan1Begin, kAggUintSpan1End);
        scanPtr(kAggPtrSpan0Begin, kAggPtrSpan0End);
        scanPtr(kAggPtrSpan1Begin, kAggPtrSpan1End);

        if (diagTag)
            spdlog::debug("{}: aggregator spans uint0={} uint1={} ptr0={} ptr1={}",
                         diagTag, spanLen[0], spanLen[1], spanLen[2], spanLen[3]);

        SurveyBufferFree(buf);
    }

    // Mark every species & resource the game tracks for this planet as scanned, by
    // bumping each one's scan-flag byte. Uses the engine's own aggregator to enumerate
    // the tracked form IDs. Traits are deliberately NOT touched here — they live in a
    // separate "known" bitmask and are handled by the trait path (SetTraitKnown /
    // Papyrus MarkTraits); a keyword's scan-flag byte is meaningless to the survey %.
    //
    // Returns the number of species/resource forms marked.
    int MarkEverythingForPlanet(std::uint32_t planetId, std::uint8_t delta, bool includeSpecies = true)
    {
        if (!planetId)
            return 0;

        int marked = 0;
        int seen   = 0;
        ForEachAggregatedFormId(planetId, [&](std::uint32_t fid)
        {
            ++seen;
            auto* form = RE::TESForm::LookupByID(fid);
            if (form)
            {
                const auto ft = form->GetFormType();
                if (ft == RE::FormType::kKYWD)
                    return;  // trait keyword — handled by the trait path, not as a species
                // "resources" category purity: when species are excluded, skip flora (FLOR) and
                // fauna (NPC_) so a resources-only completion never marks species scan flags. The
                // green path (MarkEsmSpeciesForPlanet / +0x08 build) is the ONLY thing that should
                // touch species — otherwise "resources" leaves flora/fauna scanned-but-blue.
                if (!includeSpecies && (ft == RE::FormType::kFLOR || ft == RE::FormType::kNPC_))
                    return;
            }
            else if (!includeSpecies)
            {
                // Resources-purity null-form guard: an aggregator fid that does NOT resolve can be a
                // species the live lookup can't see — the leak that let "resources" scan SOME flora/fauna
                // ("and not all of them" = only the unresolved ones; resolved species are skipped above).
                // In the resources path, mark ONLY resolved non-species forms. (The full sweep includes
                // species, so it still marks unresolved fids — that path wants them.)
                return;
            }
            if (MarkSpeciesScannedForPlanet(planetId, fid, delta) == 1)
                ++marked;
        });
        // seen > marked => the DB lookup missed some forms; seen == 0 => the aggregator
        // returned nothing for this planet. debug-level: the galaxy sweep calls this
        // ~1798x, so keep it out of the default log.
        spdlog::debug("MarkEverythingForPlanet: planetId=0x{:08X} seen={} marked={}",
                      planetId, seen, marked);
        return marked;
    }

    // Fire the survey check/notify routine. Triggers the completion event that
    // generates the "<Planet> Survey Data" slate when the survey hits 100%.
    //
    // The ctx struct matches ID_97853's expectations: planet_id at +0, float prev-pct
    // at +4, byte flag at +8 (0 = skip progress event dispatch), byte skip at +9 (0 = run).
    void NotifySurveyProgress(std::uint32_t planetId)
    {
        if (!planetId)
            return;
        struct Ctx
        {
            std::uint32_t planetId;
            float         prevPct;
            std::uint8_t  flag;
            std::uint8_t  skip;
            std::uint16_t pad {};
        } ctx {planetId, 0.0f, 0, 0, 0};
        SurveyCheckNotify(&ctx);
    }

    // Write a planet's ref-free survey STATE: the attribute "known" bits (magneto-
    // sphere / resources / atmosphere / gravity / temperature / water) + every
    // species & resource scan flag the engine tracks. This is the player-independent
    // core of "complete a planet" — no refs, no spawn. It does NOT fire the
    // completion event; callers choose when (the slate timing matters at scale).
    // Returns the number of species/resource forms marked.
    int WritePlanetSurveyState(std::uint32_t planetId, std::uint8_t delta = kDefaultScanDelta, bool includeSpecies = true)
    {
        SetPlanetAttributeBits(planetId);
        return MarkEverythingForPlanet(planetId, delta, includeSpecies);
    }

    // Fully complete one planet's survey ref-free: write the state, then fire the
    // survey-complete event so the "<Planet> Survey Data" slate drops. This is the
    // single shared "complete one planet" entry point used by BOTH the on-planet
    // path (MarkResourcesForPlanet) and the galaxy sweep's per-planet finalize
    // (FinalizeSweptPlanet). Returns the marked-form count. Idempotent.
    int CompletePlanetSurveyState(std::uint32_t planetId, std::uint8_t delta = kDefaultScanDelta, bool includeSpecies = true)
    {
        const int marked = WritePlanetSurveyState(planetId, delta, includeSpecies);
        NotifySurveyProgress(planetId);
        return marked;
    }

    // Mark a planet's canonical flora/fauna (authored in Starfield.esm's PNDT PPBD data,
    // see EsmReader) as scanned. This is the ONE thing the runtime can't give us for a
    // never-visited planet — the engine materialises its biome species only on landing —
    // so we read them from the ESM and pre-write their scan flags. When the player later
    // lands, those species spawn already-scanned (green) and the survey reads a true 100%.
    // planetId == the planet's FormID == the key in the ESM map. Returns species marked.
    // Species "kind" filter for the green path. 0 = both, 1 = FLORA (FLOR forms), 2 = FAUNA (NPC_ forms).
    // So "fauna" greens only creatures and "flora" only plants, instead of both (the documented bug).
    inline bool SpeciesMatchesKind(std::uint32_t speciesFormId, int kind)
    {
        if (kind == 0)
            return true;
        auto* const form = RE::TESForm::LookupByID(speciesFormId);
        if (!form)
            return false;
        const auto ft = form->GetFormType();
        if (kind == 1)
            return ft == RE::FormType::kFLOR;  // flora
        if (kind == 2)
            return ft == RE::FormType::kNPC_;  // fauna
        return true;
    }

    int MarkEsmSpeciesForPlanet(std::uint32_t planetId, int kind = 0)
    {
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(planetId);
        if (it == m.end())
            return 0;  // barren / resource-only body — no authored flora/fauna
        int marked = 0;
        for (const auto speciesFormId : it->second)
        {
            if (!SpeciesMatchesKind(speciesFormId, kind))
                continue;  // kind filter: flora-only / fauna-only
            // The GREEN outline reads +0x21 keyed by the species' CANONICAL id (ID_83006-derived),
            // NOT the raw ESM form id. Writing the raw id completes the survey % (authored-array
            // walk) but leaves the outline blue. Write under the canonical so the outline lights
            // up too. Fall back to the raw id when the form has no resolvable canonical.
            std::uint32_t key = CanonicalFormId(RE::TESForm::LookupByID(speciesFormId));
            if (key == 0)
                key = speciesFormId;
            if (MarkSpeciesScannedForPlanet(planetId, key, kDefaultScanDelta) == 1)
                ++marked;
        }
        return marked;
    }

    // The engine's global form registry — a BSTScatterTable<FormID, TESForm*>.
    // ID_883341 is the global that holds the map pointer; it's what
    // TESForm::LookupByID (ID_47401) reads. Starfield does NOT keep planets in
    // TESDataHandler::formArrays (those are empty for galaxy types like PNDT), so
    // this registry is the only place to enumerate all planet forms.
    inline REL::Relocation<std::uintptr_t*> AllFormsMapHolder {REL::ID(883341)};

    // Iterate every loaded form of a given type. Layout derived from the
    // LookupByID disassembly + CommonLibSF's BSTScatterTable iterator:
    //   map+0x10 -> entries base; map+0x18 -> slot count; slot stride 0x18;
    //   TESForm* at slot+0x08; nextIndex (int32) at slot+0x10, -1 == empty.
    // Defensive: bail (logging) on a null/absurd map rather than walking garbage.
    template <typename Fn>
    void ForEachFormOfType(RE::FormType type, Fn&& fn)
    {
        const auto map = *AllFormsMapHolder.get();
        if (!map)
        {
            spdlog::warn("ForEachFormOfType: global form map is null");
            return;
        }
        const auto entries  = *reinterpret_cast<std::uintptr_t*>(map + 0x10);
        const auto capacity = *reinterpret_cast<std::uint64_t*>(map + 0x18);
        if (!entries || capacity == 0 || capacity > 8'000'000)
        {
            spdlog::warn("ForEachFormOfType: suspicious form map (entries=0x{:X} capacity={})", entries, capacity);
            return;
        }

        constexpr std::size_t kSlotStride = 0x18;
        constexpr std::size_t kValueOff   = 0x08;
        constexpr std::size_t kNextOff    = 0x10;
        for (std::uint64_t i = 0; i < capacity; ++i)
        {
            const auto slot = entries + i * kSlotStride;
            if (*reinterpret_cast<std::int32_t*>(slot + kNextOff) == -1)
                continue;  // empty slot
            auto* form = *reinterpret_cast<RE::TESForm**>(slot + kValueOff);
            if (form && form->GetFormType() == type)
                fn(form);
        }
    }

    // ---- Galaxy-wide "complete all survey data" -----------------------------
    //
    // Drive the engine's own ref-free scan-complete (ID_102650) for every planet.
    // One synchronous call per planet does the genuine completion an on-planet
    // scan would: sets the surveyed bit, creates the entry if missing, fires the
    // survey-complete event (the Survey Data slate reward), recurses over moons,
    // and updates the map UI. No spawn-and-scan, no teleport, no async two-phase,
    // no per-species/trait fiddling — the engine handles all of it.
    //
    // Cap is now just a runaway guard (well above the ~1798 real planet count);
    // the engine call (ID_102650) proved safe at scale, so we process them all.
    constexpr int kMaxScansPerRun = 5000;

    // Form IDs of the planets the last sweep scan-completed. Consumed by the
    // Papyrus trait pass (Planet.GetKeywordTypeList(44) -> MarkTraitKnownForPlanet)
    // — the original mod's proven trait path, which ID_102650 alone doesn't apply
    // to every planet (it skips already-discovered ones).
    inline std::vector<std::uint32_t> g_sweepPlanetForms;
    inline std::mutex                 g_sweepMtx;

    // Returns the number of planets scan-completed (and records them for the
    // Papyrus trait pass).
    int CompleteAllPlanetsSurveyData_Phase1(std::uint8_t /*delta*/, bool writeState)
    {
        const auto                 t0 = std::chrono::steady_clock::now();
        std::vector<std::uint32_t> sweptForms;
        int                        total       = 0;
        int                        completed   = 0;
        int                        skipped     = 0;
        int                        markedTotal   = 0;  // resource/attribute scan flags set
        int                        skippedLiving = 0;  // planets WITH flora/fauna — left for on-planet green

        // Parse Starfield.esm up front (cached). This map tells us which planets HAVE authored
        // flora/fauna — the ones we must NOT ref-free "complete", because marking their species
        // scanned leaves the outline blue (an invalid state). See re_green_outline.
        const auto& planetSpecies = Esm::GetPlanetSpecies();

        // Enumerate every planet (PNDT) form from the global form registry —
        // TESDataHandler::formArrays[kPNDT] is empty in Starfield (galaxy forms
        // live in the registry / galaxy DB, not the per-type arrays).
        ForEachFormOfType(RE::FormType::kPNDT, [&](RE::TESForm* form)
        {
            ++total;
            const auto planetId = ReadPlanetId(form);
            if (!planetId)
                return;
            if (completed >= kMaxScansPerRun)
            {
                ++skipped;
                return;
            }
            // Skip any planet that HAS authored flora/fauna. Completing it ref-free would mark its
            // flora/fauna "scanned" while the outline stays BLUE — the engine keys the green on a
            // per-(planet,species) CANONICAL id that only exists once the biome materializes the
            // creature on-planet, which we cannot write from here. That is an invalid state. Living
            // worlds are left for the on-planet CompleteSurvey command; here we ONLY complete bodies
            // with no flora/fauna (genuinely completable ref-free to a TRUE 100%).
            if (planetSpecies.count(planetId))
            {
                ++skippedLiving;
                return;
            }
            // writeState=false (the traits-only path): just RECORD the barren world — do NOT discover or
            // write resources. The Papyrus traits pass marks trait-known (self-sufficient; needs no entry),
            // so "traits" never writes a single resource flag.
            if (writeState)
            {
                // Engine discover (ID_102650): create the per-planet knowledge entry if missing + mark
                // it discovered (async create; the Papyrus finalize pass mops up stragglers + fires the
                // slate). Then write the ref-free survey state (attribute bits + resources). With no
                // flora/fauna on these bodies, this reaches a genuine 100%.
                ScanCompletePlanet(0, planetId, 1);
                markedTotal += WritePlanetSurveyState(planetId, kDefaultScanDelta);
            }
            sweptForms.push_back(form->GetFormID());
            ++completed;
        });

        {
            std::lock_guard lock(g_sweepMtx);
            g_sweepPlanetForms = std::move(sweptForms);
        }

        const auto phase1Ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t0)
                                  .count();
        spdlog::info("CompleteAllPlanetsSurveyData: Phase 1 swept {} PNDT forms, {} barren completed, {} living skipped (flora/fauna left for on-planet), {} resource/attribute flags set, {} over cap in {} ms",
                     total, completed, skippedLiving, markedTotal, skipped, phase1Ms);
        return completed;
    }

    // Pending CompleteSurvey dispatch. Set by the scan hook via Papyrus's
    // CompleteSurveyIfEnabled; consumed by the poller when scanner UI is closed.
    // Deferring CompleteSurvey out of the active-scanner state avoids a race
    // between PlaceAtMe and the scanner UI's ref-list rendering.
    inline std::atomic<bool> g_pendingCompleteSurvey {false};

    // Countdowns owned by the poller (main-thread-only writes). Grace periods
    // from flag-set to actually running the dispatch, so the scanner UI has time
    // to dismiss and its rendering pipeline to quiesce.
    inline int g_completeSurveyCountdown {0};

    // Latched true when a bound native catches a fault (a C++ exception, or — because src/ is
    // built /EHa — an access violation), almost always a wrong/garbage offset deref. Once set,
    // the guarded natives short-circuit to safe defaults so the feature disables cleanly instead
    // of re-faulting on the same bad offset every call. Cleared only by a game restart.
    inline std::atomic<bool> g_degraded {false};

    // Patch the scanner's per-species required-count GMSTs so each individual scan
    // counts as a full completion. Matches the "Instant Scan" mod's approach (Nexus
    // mods/759) — just two SetGS calls, no ESM, no CCR dependency.
    //
    // Without this: the engine requires N scans per species (N varies — often 6)
    // before marking the species complete. Our CompleteSurvey post-scan flag-flip
    // usually overrides that anyway by bumping the flag past any threshold, but
    // species that slip through our iteration (rare spawns, sub-biomes not returned
    // by GetBiomeFlora/GetBiomeActors) still cap at whatever natural scan count
    // the player reached. Setting the threshold to 1 closes that gap.
    void ApplyInstantScanGameSettings()
    {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (!settings)
        {
            spdlog::warn("ApplyInstantScanGameSettings: GameSettingCollection singleton null");
            return;
        }
        const bool animalOk = settings->SetSetting<std::int32_t>("iHandScannerAnimalCountBase", 1);
        const bool plantOk  = settings->SetSetting<std::int32_t>("iHandScannerPlantsCountBase", 1);
        spdlog::info("ApplyInstantScanGameSettings: animal={} plants={}", animalOk, plantOk);
    }
}  // namespace Engine

namespace Papyrus
{
    // Every bound native is registered through GuardedNative<>::call (see the CPS_GUARDED macro
    // used in Register) rather than bound directly. This traps any C++ exception OR — because
    // src/ is compiled /EHa — any structured fault such as an access violation, logs it, and
    // returns a safe default. An uncaught fault crossing into the Papyrus VM's C call frame is
    // undefined behaviour / a silent CTD with no log line. After the first fault we latch
    // Engine::g_degraded so later natives bail to defaults instead of re-faulting on a bad offset.
    template <class T, T fn>
    struct GuardedNative;

    template <class Ret, class... Args, Ret (*fn)(std::monostate, Args...)>
    struct GuardedNative<Ret (*)(std::monostate, Args...), fn>
    {
        static Ret call(std::monostate self, Args... args)
        {
            if (Engine::g_degraded.load(std::memory_order_acquire))
            {
                if constexpr (!std::is_void_v<Ret>)
                    return Ret {};
                else
                    return;
            }
            try
            {
                return fn(self, args...);
            }
            catch (const std::exception& e)
            {
                Engine::g_degraded.store(true, std::memory_order_release);
                spdlog::error("[native] caught exception: {} — disabling further native calls this session", e.what());
            }
            catch (...)
            {
                Engine::g_degraded.store(true, std::memory_order_release);
                spdlog::error("[native] caught access violation / unknown fault — disabling further native calls this session");
            }
            if constexpr (!std::is_void_v<Ret>)
                return Ret {};
        }
    };

#define CPS_GUARDED(FN) (&GuardedNative<decltype(&FN), &FN>::call)

    // Mark a trait keyword as known on the planet. Fires the trait progress event
    // (so UI notifications behave like a natural scan discovery).
    bool MarkTraitKnownForPlanet(std::monostate, RE::TESForm* planetForm, RE::BGSKeyword* keyword)
    {
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId || !keyword)
            return false;
        return Engine::MarkTraitKnown(planetId, keyword);
    }

    void DebugLog(std::monostate, RE::BSFixedString msg)
    {
        spdlog::info("[papyrus] {}", msg.c_str());
    }

    // PROBE (isolation test): write the +0x21 scan-flag / +0x20 percent DIRECTLY under the ESM
    // species id for `planetForm` — via MarkEsmSpeciesForPlanet -> MarkSpeciesScannedForPlanet ->
    // ID_124898/ID_124899. NO spawn, NO SetScanned, NO ID_52157. Run it on the planet you are
    // STANDING ON (its PlayerKnowledge entry is already loaded) to isolate one variable: does a
    // pure direct +0x21 write under esmFid green a planet whose entry exists? Returns species
    // written: n>0 => the writes landed (entry resolved); n==0 => ResolvePlanetSubobj found no
    // entry (silent no-op — meaning the remote-planet blue is an entry-lifecycle problem).
    std::int32_t TestDirectGreen(std::monostate, RE::TESForm* planetForm, std::int32_t kind)
    {
        if (!planetForm)
            return -1;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId)
            return -1;

        // Diagnostic: log each species' esm id vs the ID_83006 canonical id BEFORE the write, so
        // the result is self-explaining. (REMAPPED) = canonical differs from esm (the case the raw
        // write was missing); (NO-CANON) = ID_83006 returned 0 (we fall back to the raw id).
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(planetId);
        if (it != m.end())
        {
            int remapped = 0, nocanon = 0;
            for (const auto sf : it->second)
            {
                const auto canon = Engine::CanonicalFormId(RE::TESForm::LookupByID(sf));
                const char* tag  = (canon == 0) ? " (NO-CANON)" : (canon != sf ? " (REMAPPED)" : "");
                if (canon == 0)
                    ++nocanon;
                else if (canon != sf)
                    ++remapped;
                spdlog::debug("TestDirectGreen species: esm=0x{:08X} canonical=0x{:08X}{}", sf, canon, tag);
            }
            spdlog::debug("TestDirectGreen: {} remapped, {} no-canon of {} species on planet 0x{:08X}",
                         remapped, nocanon, it->second.size(), planetId);
        }

        const auto n = Engine::MarkEsmSpeciesForPlanet(planetId, kind);
        spdlog::debug("TestDirectGreen: planetId=0x{:08X} kind={} -> +0x21 written (canonical key) for {} species", planetId, kind, n);
        return n;
    }

    // THE FIX, validation step: build the slot+0x08 attribute array (engine-allocated) for each of the
    // current planet's species that currently has an EMPTY +0x08 (i.e. after a TestDirectGreen poke).
    // Pushes the two universal attribute ids present in EVERY dumped species (0x0023E90D, 0x002634BE) —
    // enough to make the array non-empty. If the species then render PROPERLY green (outline + info)
    // after this + a reload, the gate IS slot+0x08 and the engine-allocated build is sound — then we
    // derive the full per-species attribute ids from the ESM and write them. Returns slots built.
    std::int32_t TestBuildArray(std::monostate, RE::TESForm* planetForm, std::int32_t kind)
    {
        if (!planetForm)
            return -1;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto db       = Engine::GetKnowledgeDB();
        if (!db)
            return -1;
        const auto subobj = Engine::ResolvePlanetSubobj(db, planetId);
        if (!subobj)
        {
            spdlog::debug("TestBuildArray: no knowledge entry for planet 0x{:08X} (run TestDirectGreen first)", planetId);
            return 0;
        }
        const auto base    = reinterpret_cast<std::uintptr_t>(subobj);
        const auto hashmap = base + 0x18;
        auto       hashEnd = *reinterpret_cast<std::uint64_t*>(base + 0x48);   // re-read after a slot recreate
        auto       slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);  // (a recreate can grow/rehash the map)
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(planetId);
        if (it == m.end())
            return 0;

        // Marker source: Esm::GetSpeciesMarkers — the per-species slot+0x08 set derived PURELY from
        // Starfield.esm (no game, no visiting, no live instance). Fauna resolves its temperament X via
        // NPC_->OBTS->temperament OMOD->NKEY->FLST 0x00160C97 (proven 100% on ground truth); flora gets
        // the 4-marker skeleton with correct per-species reproduction (PRPS) + the >=3-biome 5th marker.
        // This replaces the materialization-bound live-member read (kept below as an optional validator),
        // so fauna now greens REMOTELY too. Unknown forms come back empty -> left blue.
        // ROBUSTNESS + DIAGNOSTIC pass (2026-06-25). "Some creatures green, others not" is NOT a command-
        // ordering symptom — it is per-species HERE: a species was left BLUE when either (a) its slot did
        // not resolve in the map (the old silent `continue`), or (b) the ESM derivation returned no markers
        // (empty +0x08). Both are now recovered, and EACH per-species outcome logs at INFO so a single
        // in-game run shows exactly which species hit which path (the release build otherwise hides this).
        int built = 0, slotMiss = 0, fallbackUsed = 0, alreadyComplete = 0;
        for (const auto sf : it->second)
        {
            if (!Engine::SpeciesMatchesKind(sf, kind))
                continue;  // kind filter: flora-only / fauna-only
            auto* const form = RE::TESForm::LookupByID(sf);
            if (!form)
                continue;
            // Slot keyed by the SAME key TestDirectGreen wrote +0x21 under (green needs +0x21 AND +0x08
            // on ONE slot). CanonicalFormId is 0/NO-CANON for bare forms, so both fall back to the raw id.
            std::uint32_t key = Engine::CanonicalFormId(form);
            if (key == 0)
                key = sf;
            auto idx = Engine::SpeciesSlotHash(hashmap, &key);
            if (idx == hashEnd || !slots)
            {
                // (a) Slot missing — TestDirectGreen's create for this key did not land, or the map moved.
                // Re-create it via the SAME engine path (no hand allocator poke), then re-read the possibly
                // grown/rehashed slots/hashEnd and re-resolve. Earlier slots' data survives a rehash (the
                // engine moves entries), and each species re-resolves its own slot, so no stale write.
                Engine::MarkSpeciesScannedForPlanet(planetId, key, Engine::kDefaultScanDelta);
                hashEnd = *reinterpret_cast<std::uint64_t*>(base + 0x48);
                slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);
                idx     = Engine::SpeciesSlotHash(hashmap, &key);
                if (idx == hashEnd || !slots)
                {
                    ++slotMiss;
                    spdlog::info("TestBuildArray: species 0x{:08X} key 0x{:08X} SLOT-MISS (unresolved after recreate) -> left blue", sf, key);
                    continue;
                }
            }
            const auto slotAddr = slots + idx * 0x30;

            // Compute the FULL expected marker set for this species FIRST (before touching the slot).
            std::vector<std::uint32_t> markers    = Esm::GetSpeciesMarkers(sf, planetId);
            const auto                 actorMark  = Esm::GetSpeciesActorMarkers(sf);
            markers.insert(markers.end(), actorMark.begin(), actorMark.end());
            if (markers.empty())
            {
                // ESM derivation produced nothing for this species. Rather than leave it BLUE, write the
                // two universal attribute ids present in every dumped species, so the outline still greens
                // with a non-empty +0x08. Logged so we can see which species the derivation misses.
                markers = {0x0023E90Du, 0x002634BEu};
                ++fallbackUsed;
                spdlog::info("TestBuildArray: species 0x{:08X} had NO esm-derived markers -> FALLBACK universal set (would have been blue)", sf);
            }

            // IDEMPOTENT GREEN: if this slot is ALREADY complete — scan-flag set (+0x21 != 0) AND its
            // +0x08 already holds the full expected marker count — leave it untouched. Re-writing a
            // complete species is the clobber source: the clear below momentarily EMPTIES +0x08, and on a
            // loaded creature the engine can reconcile a marker away (e.g. Abilities) in that window ->
            // green flips to blue. So only (re)build species that are MISSING or INCOMPLETE; never re-touch
            // one that is already correct. (Pairs with skipping DiscoverPlanetEntry on the current planet.)
            const auto flagByte = *reinterpret_cast<std::uint8_t*>(slotAddr + 0x21);
            const auto arrBegin = *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x08);
            const auto arrEnd   = *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x10);
            const std::size_t curCount =
                (arrBegin != 0 && arrEnd > arrBegin && (arrEnd - arrBegin) <= 0x400) ? (arrEnd - arrBegin) / 4 : 0;
            if (flagByte != 0 && curCount == markers.size())
            {
                ++alreadyComplete;
                continue;  // already green with the full set — do NOT re-write it
            }

            // Missing/incomplete -> clear the (stale/partial) +0x08 and write the full set cleanly.
            *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x08) = 0;
            *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x10) = 0;
            *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x18) = 0;
            for (const auto id : markers)
                Engine::PushSpeciesAttr(slotAddr, id);
            spdlog::debug("TestBuildArray: 0x{:08X} {} markers ({} actor) (first=0x{:08X})",
                         sf, markers.size(), actorMark.size(), markers[0]);
            ++built;
        }
        spdlog::info("TestBuildArray: planet 0x{:08X} kind={} -> {} (re)built, {} already-complete (skipped), {} slot-miss, {} marker-fallback",
                     planetId, kind, built, alreadyComplete, slotMiss, fallbackUsed);
        // The green STATE (+0x21/+0x08) is written, but the scanner OUTLINE only repaints when a survey
        // recompute fires (ID_97853). The resources path fires it; a species-only completion ("fauna"/
        // "flora") otherwise leaves creatures green-in-state but BLUE on screen until something else
        // recomputes (e.g. a later "all"). Log-confirmed 2026-06-25: "fauna" wrote 9/9 fauna (0 miss,
        // 0 fallback) yet a creature stayed blue until "all" ran resources -> notify. Fire it here so
        // EVERY green path repaints immediately. Idempotent — the same call resources/finalize make.
        Engine::NotifySurveyProgress(planetId);
        return built;
    }

    // Queue a deferred CompleteSurvey dispatch. The scan-hook path calls this
    // instead of running CompleteSurvey immediately, so PlaceAtMe doesn't race
    // with the active scanner UI. The poller picks up the flag, waits until
    // menusVisible == false + a grace period, then dispatches Papyrus CompleteSurvey.
    void QueueCompleteSurvey(std::monostate)
    {
        Engine::g_pendingCompleteSurvey.store(true, std::memory_order_release);
    }

    // Cancel any pending auto-complete-on-scan dispatch. A MANUAL completion command calls this at
    // its start so an explicit category command (e.g. CompletePlanet "traits") is NOT overridden by
    // a queued _AutoCompleteCurrentPlanet -> CompletePlanet("all") left over from an earlier real
    // scan (QA: 'CompletePlanet "traits" was ignored and "All" was completed instead' — the log
    // showed CompletePlanet[TRAITS] then the poller firing CompletePlanet[All] 271ms later). Only
    // the atomic flag is cleared (poller-thread-owned countdown resets itself when the flag is false).
    void CancelPendingAutoComplete(std::monostate)
    {
        Engine::g_pendingCompleteSurvey.store(false, std::memory_order_release);
        spdlog::info("CancelPendingAutoComplete: cleared pending auto-complete (manual command wins)");
    }

    // Ensure a planet's knowledge entry exists (ref-free) so subsequent ref-free writes — resource
    // flags, attribute bits, species green — actually LAND. Drives the engine's own discover path
    // ID_102650 (creates the entry if missing via ID_52204, sets the surveyed bit, fires the Survey
    // Data slate, recurses moons). REQUIRED before completing a NEVER-VISITED planet: ResolvePlanetSubobj
    // is a pure lookup, so MarkSpeciesScannedForPlanet / SetPlanetAttributeBits silently no-op until the
    // entry is created here. Same call the galaxy sweep uses (proven safe at scale). Returns 1 on
    // success, 0 if the planet id didn't resolve.
    std::int32_t DiscoverPlanetEntry(std::monostate, RE::TESForm* planetForm)
    {
        if (!planetForm)
            return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId)
            return 0;
        Engine::ScanCompletePlanet(0, planetId, 1);
        spdlog::debug("DiscoverPlanetEntry: planet=0x{:08X} planetId=0x{:08X} -> ID_102650 discover",
                     planetForm->GetFormID(), planetId);
        return 1;
    }

    // RESOURCES category — pure. Runs the engine's per-planet aggregator (ID_1016657) and marks
    // the attribute bits + every NON-species tracked form (resources). Flora (FLOR) and fauna
    // (NPC_) are deliberately EXCLUDED (includeSpecies=false) so a "resources" completion never
    // touches species scan flags — species are greened only by the dedicated green path. Trait
    // keywords are skipped too (handled by MarkTraits). Fires the survey-complete event.
    std::int32_t MarkResourcesForPlanet(std::monostate, RE::TESForm* planetForm, std::int32_t delta)
    {
        if (!planetForm)
            return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto d        = static_cast<std::uint8_t>(delta <= 0 ? Engine::kDefaultScanDelta : (delta > Engine::kMaxScanDelta ? Engine::kMaxScanDelta : delta));
        // Resources-only: attribute bits + resource scan flags + the survey-complete slate.
        // includeSpecies=false keeps flora/fauna out of the resources category (QA: "resources
        // touched species when it should not").
        const auto n = Engine::CompletePlanetSurveyState(planetId, d, /*includeSpecies=*/false);
        spdlog::debug("MarkResourcesForPlanet: planet=0x{:08X} planetId=0x{:08X} delta={} -> marked={}",
                     planetForm->GetFormID(), planetId, d, n);
        return n;
    }

    // Sweep every planet/moon in the galaxy and complete its survey ref-free (no
    // teleport, no spawn): discover it (creating the knowledge entry), then write the
    // attribute bits + species/resource scan flags. The Papyrus finalize pass mops up
    // the few async-create stragglers and fires each planet's completion event (slate).
    // Returns the number of planets processed. Console:
    //   cgf "CompletePlanetSurveyQuest.CompleteAllPlanetsSurveyData"
    std::int32_t CompleteAllPlanetsSurveyData(std::monostate, bool writeResources)
    {
        return Engine::CompleteAllPlanetsSurveyData_Phase1(Engine::kDefaultScanDelta, writeResources);
    }

    // Index-based accessor over the planets the last sweep scan-completed, so the
    // Papyrus trait pass can re-resolve each as a Planet and mark its traits via
    // the original mod's proven GetKeywordTypeList(44) -> MarkTraitKnownForPlanet.
    std::int32_t GetSweepPlanetCount(std::monostate)
    {
        std::lock_guard lock(Engine::g_sweepMtx);
        return static_cast<std::int32_t>(Engine::g_sweepPlanetForms.size());
    }

    std::int32_t GetSweepPlanetFormIdAt(std::monostate, std::int32_t index)
    {
        std::lock_guard lock(Engine::g_sweepMtx);
        if (index < 0 || static_cast<std::size_t>(index) >= Engine::g_sweepPlanetForms.size())
            return 0;
        return static_cast<std::int32_t>(Engine::g_sweepPlanetForms[index]);
    }

    // Re-apply the attribute "known" bits + per-species scan flags for one swept
    // planet (resolved from its form ID). The C++ sweep creates the knowledge entry
    // via ID_102650 ASYNCHRONOUSLY, so a few planets' entries aren't ready when the
    // sweep writes in the same frame (ResolvePlanetSubobj returns null -> skipped).
    // The Papyrus pass calls this per planet across later frames, by which point the
    // deferred creates have flushed — catching those stragglers. Idempotent.
    // Returns 1 if the attribute bits are now set, else 0.
    std::int32_t FinalizeSweptPlanet(std::monostate, std::int32_t formId)
    {
        auto* form = RE::TESForm::LookupByID(static_cast<std::uint32_t>(formId));
        if (!form)
            return 0;
        const auto planetId = Engine::ReadPlanetId(form);
        if (!planetId)
            return 0;
        // Re-run the shared single-planet completion now the knowledge entry is ready.
        // The sweep's ID_102650 create is async, so a few entries weren't ready during
        // the same-frame C++ pass; this Papyrus pass runs a few frames later and catches
        // them. It also (re-)fires the survey-complete event POST-completion — the sweep's
        // ID_102650 fires it at discover time, before our writes finish the planet, so the
        // slate wouldn't otherwise drop. The engine awards a planet's survey reward once,
        // so re-firing is idempotent (one slate per planet).
        // Only barren bodies (no flora/fauna) reach the finalize pass now — the sweep skips living
        // worlds — so we deliberately do NOT mark any flora/fauna here. That would write the
        // invalid "scanned but blue" state. Living worlds are greened on-planet via CompleteSurvey.
        const auto marked = Engine::CompletePlanetSurveyState(planetId);
        return marked;
    }

    // --- Parameterized completion-menu support (read-only; no writes) ---------------------------
    // Unique life-bearing planet ids. planetId == PNDT FormID (the +0x54 identity), so Papyrus can
    // resolve each back with Game.GetForm(planetId) as Planet. Built from the species->planets
    // inversion. The galaxy sweep (CompleteAllPlanetsSurveyData) deliberately skips living worlds,
    // so the parameterized CompleteLifePlanets command uses THIS list to reach their traits.
    static std::vector<std::uint32_t> g_lifePlanetCache;
    static std::mutex                 g_lifePlanetMtx;

    std::int32_t EnumerateLifePlanets(std::monostate)
    {
        std::lock_guard lock(g_lifePlanetMtx);
        g_lifePlanetCache.clear();
        for (const auto& [species, planets] : Engine::GetSpeciesToPlanets())
            g_lifePlanetCache.insert(g_lifePlanetCache.end(), planets.begin(), planets.end());
        std::sort(g_lifePlanetCache.begin(), g_lifePlanetCache.end());
        g_lifePlanetCache.erase(std::unique(g_lifePlanetCache.begin(), g_lifePlanetCache.end()),
                                g_lifePlanetCache.end());
        spdlog::info("EnumerateLifePlanets: {} unique life-bearing planets", g_lifePlanetCache.size());
        return static_cast<std::int32_t>(g_lifePlanetCache.size());
    }

    std::int32_t GetLifePlanetAt(std::monostate, std::int32_t index)
    {
        std::lock_guard lock(g_lifePlanetMtx);
        if (index < 0 || static_cast<std::size_t>(index) >= g_lifePlanetCache.size())
            return 0;
        return static_cast<std::int32_t>(g_lifePlanetCache[index]);
    }

    // Case-insensitive: does the comma-list `csv` contain whole token `token` (or the dedicated token "all")?
    // Lets Papyrus parse a "resources,traits,fauna,flora" category string — base Papyrus has no split.
    bool CategoryEnabled(std::monostate, RE::BSFixedString csv, RE::BSFixedString token)
    {
        const auto lower = [](std::string s) {
            for (auto& c : s)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return s;
        };
        const auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
        };

        const std::string list = lower((csv.c_str() != nullptr) ? csv.c_str() : "");
        const std::string want = lower((token.c_str() != nullptr) ? token.c_str() : "");

        std::string piece;
        for (std::size_t i = 0; i <= list.size(); ++i)
        {
            const char ch = (i < list.size()) ? list[i] : ',';
            if (ch == ',')
            {
                trim(piece);
                if (!piece.empty())
                {
                    if (piece == "all")
                        return true;
                    if (!want.empty() && piece == want)
                        return true;
                }
                piece.clear();
            }
            else
                piece += ch;
        }
        return false;
    }

    // Validate a category CSV: true IFF every comma-separated token is a recognized option AND at least
    // one token is present. A typo ("res", "creature", "resource") or an empty/blank string returns
    // false, so a command can no-op cleanly instead of silently doing nothing-but-looking-like-it-ran.
    bool CategoriesValid(std::monostate, RE::BSFixedString csv)
    {
        const auto lower = [](std::string s) {
            for (auto& c : s)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return s;
        };
        const auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
        };

        const std::string list = lower((csv.c_str() != nullptr) ? csv.c_str() : "");
        bool              any  = false;
        std::string       piece;
        for (std::size_t i = 0; i <= list.size(); ++i)
        {
            const char ch = (i < list.size()) ? list[i] : ',';
            if (ch == ',')
            {
                trim(piece);
                if (!piece.empty())
                {
                    if (piece != "all" && piece != "resources" && piece != "traits" && piece != "fauna" &&
                        piece != "flora" && piece != "species" && piece != "creatures")
                        return false;  // an unrecognized token -> the whole CSV is invalid
                    any = true;
                }
                piece.clear();
            }
            else
                piece += ch;
        }
        return any;  // true only when >=1 token AND none unrecognized
    }

    void Register()
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm)
        {
            spdlog::error("Failed to get VM singleton");
            return;
        }
        auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "DebugLog"sv, CPS_GUARDED(DebugLog), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "MarkTraitKnownForPlanet"sv, CPS_GUARDED(MarkTraitKnownForPlanet),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "TestDirectGreen"sv, CPS_GUARDED(TestDirectGreen),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "TestBuildArray"sv, CPS_GUARDED(TestBuildArray),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "MarkResourcesForPlanet"sv, CPS_GUARDED(MarkResourcesForPlanet),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "DiscoverPlanetEntry"sv, CPS_GUARDED(DiscoverPlanetEntry),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "EnumerateLifePlanets"sv, CPS_GUARDED(EnumerateLifePlanets),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetLifePlanetFormIdAt"sv, CPS_GUARDED(GetLifePlanetAt),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CategoryEnabled"sv, CPS_GUARDED(CategoryEnabled),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CategoriesValid"sv, CPS_GUARDED(CategoriesValid),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "QueueCompleteSurvey"sv, CPS_GUARDED(QueueCompleteSurvey), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CancelPendingAutoComplete"sv, CPS_GUARDED(CancelPendingAutoComplete), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CompleteAllPlanetsSurveyData"sv, CPS_GUARDED(CompleteAllPlanetsSurveyData),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetSweepPlanetCount"sv, CPS_GUARDED(GetSweepPlanetCount),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetSweepPlanetFormIdAt"sv, CPS_GUARDED(GetSweepPlanetFormIdAt),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "FinalizeSweptPlanet"sv, CPS_GUARDED(FinalizeSweptPlanet),
            std::optional<bool> {true}, false);

        spdlog::info("Bound Papyrus natives: DebugLog, MarkTraitKnownForPlanet, TestDirectGreen, TestBuildArray, "
                     "MarkResourcesForPlanet, DiscoverPlanetEntry, EnumerateLifePlanets, GetLifePlanetFormIdAt, "
                     "CategoryEnabled, CategoriesValid, QueueCompleteSurvey, CancelPendingAutoComplete, "
                     "CompleteAllPlanetsSurveyData, GetSweepPlanetCount, GetSweepPlanetFormIdAt, FinalizeSweptPlanet");
    }

#undef CPS_GUARDED
}  // namespace Papyrus

namespace Hook
{
    // Dispatch a zero-argument static Papyrus call. Shared by the scan hook and
    // the per-frame poller — both call CompletePlanetSurveyQuest functions.
    void DispatchPapyrusStatic(const char* functionName)
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm)
            return;
        auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);

        using VarArray = RE::BSScrapArray<RE::BSScript::Variable>;
        static const std::function<bool(VarArray&)> kNoArgs = [](VarArray&) -> bool { return true; };
        static const RE::BSFixedString              kScriptName {"CompletePlanetSurveyQuest"};
        static const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> kNoCallback;

        const RE::BSFixedString fnName {functionName};
        ivm->DispatchStaticCall(kScriptName, fnName, kNoArgs, kNoCallback, 0);
    }

    // Intercept the survey-completion check (ID_97853) when called from within
    // the species-scan progress updater (ID_52157).
    //
    // ID_52157 is reached for every successful biome species scan regardless of path:
    //   flora:  ID_83008 → ID_83038 → (local_res8[0] != 0) → ID_52157
    //   fauna:  ID_83008 → ID_52160 → ID_52157
    // Hooking the CALL site of ID_97853 inside ID_52157 therefore covers the
    // player's E-key scan, Papyrus SetScanned, and any other caller.
    //
    // We use write_call<5> at the CALL SITE (not write_jmp at a function start).
    // write_call5 reads the existing E8 rel32 instruction to decode the original
    // function address as `func` — correct and non-crashing, unlike write_jmp5
    // which would read garbage prologue bytes as a fake JMP target.
    struct ScanHook
    {
        using fn_t = void (*)(void*);  // ID_97853: void(undefined4* ctx)

        static void thunk(void* ctx)
        {
            func(ctx);  // call original SurveyCheckNotify (ID_97853)
            DispatchPapyrusStatic("CompleteSurveyIfEnabled");
        }

        static inline fn_t func = nullptr;
    };

    // 1 KiB is larger than ID_52157's body; bigger than any real function we'd
    // ever want to hook a single CALL within.
    constexpr std::size_t kScanHookSearchWindow = 0x400;

    // Scan the first `scan_limit` bytes of `outer` for an E8 rel32 CALL whose
    // resolved absolute target equals `inner`.  Returns the address of that CALL
    // instruction, or 0 if not found.
    static std::uintptr_t FindCallSite(std::uintptr_t outer,
                                       std::uintptr_t inner,
                                       std::size_t    scan_limit = kScanHookSearchWindow)
    {
        for (std::size_t i = 0; i < scan_limit; ++i)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(outer + i);
            if (*p == Engine::kX86CallOpcode)
            {
                const auto disp        = *reinterpret_cast<const std::int32_t*>(p + 1);
                const auto call_target = static_cast<std::uintptr_t>(static_cast<std::int64_t>(outer + i + Engine::kX86CallInsnLength) + disp);
                if (call_target == inner)
                {
                    return outer + i;
                }
            }
        }
        return 0;
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> outer {REL::ID(52157)};  // planet-progress updater
        REL::Relocation<std::uintptr_t> inner {REL::ID(97853)};  // survey check/notify

        const auto call_site = FindCallSite(outer.address(), inner.address());
        if (!call_site)
        {
            spdlog::error("ScanHook: CALL to ID_97853 not found inside ID_52157 — hook skipped");
            return;
        }

        ScanHook::func = reinterpret_cast<ScanHook::fn_t>(
            REL::GetTrampoline().write_call<5>(call_site, reinterpret_cast<std::uintptr_t>(ScanHook::thunk)));

        spdlog::info("ScanHook: installed at call-site 0x{:016X} (ID_52157 → ID_97853)", call_site);
    }

    // Per-frame poll: waits for the pending CompleteSurvey flag + scanner menu closed,
    // then dispatches the deferred Papyrus CompleteSurvey from a clean (non-scanner) state.
    //
    // History: tried event sink on UI's BSTEventSource<MenuOpenCloseEvent>.
    // CommonLibSF's shared REL::ID(123821) for BSTEventSource::RegisterSink
    // doesn't line up with the MenuOpenCloseEvent specialization on 1.16.236.0–1.16.244.0
    // and crashes DLL init. Polling via SFSE's permanent-task is the pragmatic
    // alternative — runs every frame, but the hot path is a single atomic load
    // that returns false 99.9% of the time.
    //
    // Safety gate combines two signals:
    //   1. menusVisible == false (no menus open right now)
    //   2. at least kScannerDismissGraceFrames have elapsed since the flag was
    //      set (defensive: scanner animation may persist beyond menu close)
    constexpr int kScannerDismissGraceFrames = 30;  // ~0.5s at 60fps

    void InstallScanSweepPoller()
    {
        auto* task = SFSE::GetTaskInterface();
        if (!task) {
            spdlog::error("InstallScanSweepPoller: no task interface");
            return;
        }
        task->AddPermanentTask([]() {
            auto* ui = RE::UI::GetSingleton();
            const bool menusOpen = ui && ui->menusVisible;

            // Gate helper: advance countdown while menus are open/just closed.
            // Returns true once safe to proceed (countdown elapsed + menus closed).
            auto readyToFire = [&](int& countdown) -> bool {
                if (countdown == 0) {
                    countdown = kScannerDismissGraceFrames;
                    return false;
                }
                if (menusOpen) {
                    countdown = kScannerDismissGraceFrames;
                    return false;
                }
                return --countdown <= 0;
            };

            // === Pending CompleteSurvey dispatch ===
            // Scan hook sets this via QueueCompleteSurvey (Papyrus). We dispatch
            // Papyrus CompleteSurvey from here, well after the scanner UI has
            // closed, so PlaceAtMe doesn't race with the active scanner.
            if (Engine::g_pendingCompleteSurvey.load(std::memory_order_acquire)) {
                if (readyToFire(Engine::g_completeSurveyCountdown)) {
                    if (Engine::g_pendingCompleteSurvey.exchange(false, std::memory_order_acq_rel)) {
                        DispatchPapyrusStatic("_AutoCompleteCurrentPlanet");
                        spdlog::info("Poller: dispatched _AutoCompleteCurrentPlanet (scanner closed)");
                    }
                }
            }
            else {
                Engine::g_completeSurveyCountdown = 0;
            }
        });
        spdlog::info("InstallScanSweepPoller: per-frame poller registered");
    }
}  // namespace Hook

namespace
{
    void MessageCallback(SFSE::MessagingInterface::Message* a_msg) noexcept
    {
        if (a_msg->type == SFSE::MessagingInterface::kPostDataLoad)
        {
            Papyrus::Register();
            Hook::Install();
            Hook::InstallScanSweepPoller();
            Engine::ApplyInstantScanGameSettings();
            spdlog::info("CompletePlanetSurvey initialized");
        }
    }
}  // namespace

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    SFSE::Init(a_sfse, {.trampoline = true, .trampolineSize = 64});
    // Pin a timestamped log format (date + ms) so phase durations read straight from the log,
    // e.g. "[2026-06-21 14:31:50.598] [tid] [I] …". CommonLibSF already timestamps by default;
    // this makes the format explicit and adds the date for cross-session clarity.
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%L] %v");
    // Flush every logged line straight to disk. Starfield terminates the process abruptly on exit
    // (no clean spdlog shutdown), so buffered lines written shortly before quit are LOST. Flushing
    // at the active level guarantees the last diagnostics survive.
    //
    // Log level by build mode: a release / releasedbg build (NDEBUG) ships at INFO — the per-planet
    // and per-species green/resource lines log at DEBUG, so a whole-galaxy completion does NOT spew
    // ~20k lines into a player's log; only the per-stage summaries (sweep totals, timings, faults)
    // remain. A debug build (xmake f -m debug) drops to DEBUG for the full per-body trace.
#ifdef NDEBUG
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
#else
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);
#endif
    spdlog::info("{} v{} loading", Plugin::Name, Plugin::Version.string());

    const auto* messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageCallback))
    {
        spdlog::critical("Failed to register messaging listener");
        return false;
    }
    return true;
}
