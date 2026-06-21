#include "PCH.h"

#include "EsmReader.h"

#include <chrono>
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
    // ID_83008: SetScanned inner, dispatches to flora (ID_83038+52157) or actor (ID_52160).
    //   (ObjectReference* ref, bool scannedFlag, byte=0xd, byte=0)
    //   The ref must be a real in-world reference placed in the current biome so its
    //   (ID_939118, ref->formID) component exists.
    using fn_scan_ref_t = void (*)(void* ref, char scannedFlag, std::uint8_t a, std::uint8_t b);
    // ID_52157: per-planet progress updater. Called by ID_83008 only when ID_83038 found
    //   the ref-component. Signature: (ref, int count, byte=0xd, byte, byte).
    //   We call this DIRECTLY after SetScanned on spawn-and-scan'd flora refs so the
    //   biome progress ticks even though ID_83038 no-ops on PlaceAtMe'd refs.
    using fn_planet_progress_t =
        void (*)(void* ref, std::int32_t count, std::uint8_t b3, std::uint8_t b4, std::uint8_t b5);
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
    inline REL::Relocation<fn_scan_ref_t>        ScanRefNative {REL::ID(83008)};
    inline REL::Relocation<fn_planet_progress_t> PlanetProgressNative {REL::ID(52157)};
    inline REL::Relocation<fn_db_lookup_t>       DbLookup {REL::ID(126806)};
    inline REL::Relocation<fn_incr_flag_t>       IncrementScanFlag {REL::ID(124898)};
    inline REL::Relocation<fn_set_percent_t>     SetPercentByte {REL::ID(124899)};
    inline REL::Relocation<std::uint16_t*>       TraitDiscriminator {REL::ID(938333)};
    // ID_52161: the engine's per-TYPE completion writer (resolves the species' canonical key
    //   from a LIVE instance, then writes the planet's "scanned species" tree — the green state).
    //   Normally fed the current planet by the scan path; we drive it directly with an EXPLICIT
    //   target planet so any planet can be greened from one spot. Context: {uint32 planetId @0x00,
    //   uint32 liveInstanceFormID @0x10}; param_2 -> &db. The instance must be live + dynamic-id.
    using fn_type_scan_inner_t = void (*)(void* ctx, std::uintptr_t* dbPtr);
    inline REL::Relocation<fn_type_scan_inner_t> TypeScanInner {REL::ID(52161)};

    // ID_52158: the per-species COUNT completion — the OTHER half of the green (paired with the
    //   tree write ID_52161). It sets the +0x21 scan-flag / +0x20 percent for (planet, species)
    //   and runs the biome propagation. Normally reached via ID_52157, which resolves the planet
    //   from the ref's location (ID_52188 = "the planet you're on"). We drive it DIRECTLY with an
    //   EXPLICIT target planet in the context so the count completes for ANY planet from one spot.
    //   Context confirmed from ID_52157's stack build: {uint32 planetId @0x00, uint32 species @0x04,
    //   byte delta @0x08, void* ref @0x10, byte* outScanned @0x18, byte* outPercent @0x20,
    //   byte flag @0x28}; param_2 -> &db. (NB: ID_52158 reads the CURRENT biome at ID_937609+0x160
    //   for its propagation, so the biome half may no-op for a never-visited target.)
    using fn_progress_inner_t = void (*)(void* ctx, std::uintptr_t* dbPtr);
    inline REL::Relocation<fn_progress_inner_t> PlanetProgressInner {REL::ID(52158)};

    // ID_1016657: per-planet survey aggregator constructor.
    //   (buffer, planet_id) — populates buffer with all tracked form IDs for the planet
    //   across four arrays (two uint-arrays for flora/trait ids, two ptr-arrays for resource/other).
    //   Buffer size seen in callers: >= 0x250 bytes. We allocate 0x400 to be safe.
    using fn_aggregator_t = void (*)(void* buffer, std::uint32_t planet_id);
    // ID_65318: cleanup for the aggregator buffer.
    using fn_buffer_free_t = void (*)(void* buffer);

    inline REL::Relocation<fn_aggregator_t>  SurveyAggregator {REL::ID(1016657)};
    inline REL::Relocation<fn_buffer_free_t> SurveyBufferFree {REL::ID(65318)};

    // ID_83007: returns 0 = not a biome species, 1 = biome species unscanned, 2 = already scanned.
    // Reads (939118, ref_formID) component — used as our "is this ref safe to pass to ID_83008" gate.
    using fn_is_biome_ref_t = char (*)(void* ref);
    inline REL::Relocation<fn_is_biome_ref_t> IsBiomeRef {REL::ID(83007)};

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
    constexpr std::uint8_t kBiomeScanCategory    = 0x0d;   // category byte for ScanRefNative / PlanetProgressNative

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

    bool MarkTraitKnown(std::uint32_t planetId, RE::BGSKeyword* keyword)
    {
        if (!planetId || !keyword)
            return false;
        SetTraitKnownNative(planetId, reinterpret_cast<std::uintptr_t>(keyword), true);
        return true;
    }

    // Skip ID_83038 (which no-ops for un-registered refs) and go straight to the
    // per-planet progress updater. count = the base form's formID (the "known species id").
    // Used by spawn-and-scan: after PlaceAtMe + SetScanned, the flora path's
    // ID_83038 no-ops because the spawned ref lacks the (939118, ref_formID)
    // component. This updater fires ID_52157 directly — what ID_83038 would have.
    void UpdatePlanetProgress(void* ref, std::uint32_t speciesFormId)
    {
        if (!ref || !speciesFormId)
            return;
        PlanetProgressNative(ref, static_cast<std::int32_t>(speciesFormId), kBiomeScanCategory, 0, 0);
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

    // Green a single species TYPE on an EXPLICIT target planet by driving the engine's own
    // type-completion writer (ID_52161) directly. `ref` must be a LIVE, dynamic-id instance of
    // the species (spawned via PlaceAtMe) — ID_52161 resolves the species' canonical key off it
    // and writes `planetId`'s scanned-species tree (the green state). Because the planet is an
    // explicit ARGUMENT here (not "wherever the player is"), one live instance can green that
    // species on ANY planet. The planet's survey entry must already exist (discover/MarkResources).
    void GreenTypeForPlanet(RE::TESObjectREFR* ref, std::uint32_t planetId)
    {
        if (!ref || !planetId)
            return;
        const auto db = GetKnowledgeDB();
        if (!db)
            return;
        // Context ID_52161 consumes: planetId @0x00, live-instance FormID @0x10.
        struct alignas(8) TypeScanCtx
        {
            std::uint32_t planetId;    // +0x00
            std::uint32_t pad04 {0};
            std::uint64_t pad08 {0};   // +0x08
            std::uint32_t instanceId;  // +0x10
            std::uint32_t pad14 {0};
        } ctx {planetId, 0, 0, ref->GetFormID(), 0};
        std::uintptr_t dbPtr = db;
        TypeScanInner(&ctx, &dbPtr);
    }

    // Complete a species TYPE's COUNT on an EXPLICIT target planet by driving ID_52158 directly,
    // bypassing ID_52157's ID_52188 "current planet" resolution. Pairs with GreenTypeForPlanet:
    // in testing, the tree write ALONE stayed blue (#6); tree write + this count completion (#7)
    // greened the planet, fresh instances included. `ref` is the live spawned instance (ID_52158
    // reads it for biome propagation + notify; the count itself credits `planetId`). The planet's
    // survey entry must already exist (the data sweep creates it) or ID_52158 early-returns.
    void CompleteTypeForPlanet(std::uint32_t planetId, std::uint32_t species, RE::TESObjectREFR* ref)
    {
        if (!planetId || !species || !ref)
            return;
        const auto db = GetKnowledgeDB();
        if (!db)
            return;
        std::uint8_t outScanned = 0;  // ID_52158 writes "was-unscanned" flag here (param_1+6)
        std::uint8_t outPercent = 0;  // ID_52158 writes the new percent byte here (param_1+8)
        struct alignas(8) ProgressCtx
        {
            std::uint32_t planetId;   // +0x00
            std::uint32_t species;    // +0x04
            std::uint8_t  delta;      // +0x08  (ID_52157 passes 1)
            std::uint8_t  pad09[7];
            void*         ref;         // +0x10
            std::uint8_t* outScanned;  // +0x18
            std::uint8_t* outPercent;  // +0x20
            std::uint8_t  flag28;      // +0x28  (ID_52157 passes param_5; UpdatePlanetProgress=0)
            std::uint8_t  pad29[7];
        } ctx {planetId, species, 1, {}, ref, &outScanned, &outPercent, 0, {}};
        std::uintptr_t dbPtr = db;
        PlanetProgressInner(&ctx, &dbPtr);
    }

    // One species TYPE fully greened on one target planet: the tree write (ID_52161) + the count
    // completion (ID_52158), both with the planet handed in explicitly. This is the per-(planet,
    // species) unit of the atomic galaxy green — the same pair that greened in test #7, but aimed
    // at an arbitrary planet instead of "the one you're standing on".
    void GreenAndCompleteTypeForPlanet(RE::TESObjectREFR* ref, std::uint32_t planetId, std::uint32_t species)
    {
        GreenTypeForPlanet(ref, planetId);
        CompleteTypeForPlanet(planetId, species, ref);
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
    int MarkEverythingForPlanet(std::uint32_t planetId, std::uint8_t delta)
    {
        if (!planetId)
            return 0;

        int marked = 0;
        int seen   = 0;
        ForEachAggregatedFormId(planetId, [&](std::uint32_t fid)
        {
            ++seen;
            auto* form = RE::TESForm::LookupByID(fid);
            if (form && form->GetFormType() == RE::FormType::kKYWD)
                return;  // trait keyword — handled by the trait path, not as a species
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
    int WritePlanetSurveyState(std::uint32_t planetId, std::uint8_t delta = kDefaultScanDelta)
    {
        SetPlanetAttributeBits(planetId);
        return MarkEverythingForPlanet(planetId, delta);
    }

    // Fully complete one planet's survey ref-free: write the state, then fire the
    // survey-complete event so the "<Planet> Survey Data" slate drops. This is the
    // single shared "complete one planet" entry point used by BOTH the on-planet
    // path (MarkResourcesForPlanet) and the galaxy sweep's per-planet finalize
    // (FinalizeSweptPlanet). Returns the marked-form count. Idempotent.
    int CompletePlanetSurveyState(std::uint32_t planetId, std::uint8_t delta = kDefaultScanDelta)
    {
        const int marked = WritePlanetSurveyState(planetId, delta);
        NotifySurveyProgress(planetId);
        return marked;
    }

    // Mark a planet's canonical flora/fauna (authored in Starfield.esm's PNDT PPBD data,
    // see EsmReader) as scanned. This is the ONE thing the runtime can't give us for a
    // never-visited planet — the engine materialises its biome species only on landing —
    // so we read them from the ESM and pre-write their scan flags. When the player later
    // lands, those species spawn already-scanned (green) and the survey reads a true 100%.
    // planetId == the planet's FormID == the key in the ESM map. Returns species marked.
    int MarkEsmSpeciesForPlanet(std::uint32_t planetId)
    {
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(planetId);
        if (it == m.end())
            return 0;  // barren / resource-only body — no authored flora/fauna
        int marked = 0;
        for (const auto speciesFormId : it->second)
            // Survey scan flag (+0x21/+0x20) — drives the % count + "scanned" readout.
            // NOTE: the GREEN outline is NOT set here — it reads a per-species tree
            // (subobj+0x80) only the real scan populates, which is why the galaxy sweep
            // completes the data but greening needs the on-planet spawn-and-scan pass.
            if (MarkSpeciesScannedForPlanet(planetId, speciesFormId, kDefaultScanDelta) == 1)
                ++marked;
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
    int CompleteAllPlanetsSurveyData_Phase1(std::uint8_t /*delta*/)
    {
        const auto                 t0 = std::chrono::steady_clock::now();
        std::vector<std::uint32_t> sweptForms;
        int                        total       = 0;
        int                        completed   = 0;
        int                        skipped     = 0;
        int                        markedTotal = 0;  // species/resource scan flags set
        int                        esmTotal    = 0;  // ESM-authored flora/fauna marked

        // Parse Starfield.esm up front (cached) so the per-planet flora/fauna lookups
        // below are cheap. This is the only source of a never-visited planet's species.
        Esm::GetPlanetSpecies();

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
            // 1) Engine discover (ID_102650): create the per-planet knowledge entry
            //    if missing + mark it discovered. The create is asynchronous, so a few
            //    entries aren't ready this frame — the Papyrus finalize pass mops those
            //    up (and fires each planet's completion event) a few frames later.
            ScanCompletePlanet(0, planetId, 1);
            // 2) Write the ref-free survey state (attribute bits + species/resource
            //    scan flags). Slate dispatch is deferred to the finalize pass.
            markedTotal += WritePlanetSurveyState(planetId, kDefaultScanDelta);
            // 3) Mark the planet's authored flora/fauna (from Starfield.esm) as scanned —
            //    the species the runtime can't give us for an unvisited planet. The finalize
            //    pass re-applies this once async entries flush, so stragglers are covered.
            esmTotal += MarkEsmSpeciesForPlanet(planetId);
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
        spdlog::info("CompleteAllPlanetsSurveyData: Phase 1 swept {} PNDT forms, {} processed, {} species/resource flags set, {} ESM flora/fauna marked, {} over cap (skipped) in {} ms",
                     total, completed, markedTotal, esmTotal, skipped, phase1Ms);
        return completed;
    }

    // Walk a cell's references directly (bypassing CommonLibSF's ForEachReference
    // which uses a lock at cell+0x120 that isn't a BSReadWriteLock on 1.16.236.0–1.16.244.0
    // — memory-probe confirmed that offset holds a 64-bit pointer, not lock state).
    // The BSTArray header at cell+0x080 IS correct, so iterate raw.
    //
    // Unlocked iteration is safe because the poller only fires when menusVisible
    // == false AND after a 30-frame grace period — cell is quiescent.
    //
    // Calls ScanRefNative (ID_83008) on each flora/fauna ref whose biome component
    // exists (IsBiomeRef != 0). Flips the per-ref scanned outline blue → green.
    // Guard generously: procgen cells can have stale/large ref arrays.
    constexpr std::uint32_t kMaxCellRefsToScan = 8192;

    int ScanAllRefsInCell(RE::TESObjectCELL* cell)
    {
        if (!cell || !cell->IsAttached())
            return 0;

        const auto* cellBytes = reinterpret_cast<const std::uint8_t*>(cell);
        const auto  size      = *reinterpret_cast<const std::uint32_t*>(cellBytes + kCellRefArraySize);
        const auto  capacity  = *reinterpret_cast<const std::uint32_t*>(cellBytes + kCellRefArrayCapacity);
        auto* const data      = *reinterpret_cast<RE::TESObjectREFR** const*>(cellBytes + kCellRefArrayData);
        if (!data || size == 0 || size > capacity || size > kMaxCellRefsToScan)
            return 0;

        int scanned = 0;
        for (std::uint32_t i = 0; i < size; ++i)
        {
            auto* ref = data[i];
            if (!ref)
                continue;
            auto* base = ref->GetBaseObject().get();
            if (!base)
                continue;
            const auto ft = base->GetFormType();
            if (ft != RE::FormType::kFLOR && ft != RE::FormType::kNPC_)
                continue;
            if (IsBiomeRef(ref) == 0)
                continue;
            ScanRefNative(ref, 1, kBiomeScanCategory, 0);
            ++scanned;
        }
        return scanned;
    }

    // Pending-sweep flag: set by Papyrus's ScanNearbyRefs, consumed by
    // Hook::InstallScanSweepPoller's per-frame task. Atomic so Papyrus dispatch
    // (worker thread) can set it without a lock; the poller runs on main thread.
    inline std::atomic<bool> g_pendingOutlineSweep {false};

    // Pending CompleteSurvey dispatch. Set by the scan hook via Papyrus's
    // CompleteSurveyIfEnabled; consumed by the poller when scanner UI is closed.
    // Deferring CompleteSurvey out of the active-scanner state avoids a race
    // between PlaceAtMe and the scanner UI's ref-list rendering.
    inline std::atomic<bool> g_pendingCompleteSurvey {false};

    // Countdowns owned by the poller (main-thread-only writes). Grace periods
    // from flag-set to actually running the dispatch, so the scanner UI has time
    // to dismiss and its rendering pipeline to quiesce.
    inline int g_scanSweepCountdown {0};
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

    // Bypass ID_83038's per-ref component check by calling the per-planet progress
    // updater (ID_52157) directly. Required after SetScanned on a PlaceAtMe'd flora
    // ref — the spawned ref lacks the (939118, ref_formID) component so ID_83038
    // no-ops and ID_52157 never fires unless we call it here.
    bool UpdatePlanetProgressForSpecies(std::monostate, RE::TESObjectREFR* ref, RE::TESForm* speciesForm)
    {
        if (!ref || !speciesForm)
            return false;
        Engine::UpdatePlanetProgress(ref, speciesForm->GetFormID());
        return true;
    }

    // Cache for EnumeratePlanetSpecies / GetPlanetSpeciesAt. Papyrus calls the
    // enumerate native once, then iterates with index-based accessor. Mutex guards
    // against concurrent Papyrus script access (unlikely but possible).
    static std::vector<std::uint32_t> g_planetSpeciesCache;
    static std::mutex                 g_speciesCacheMtx;

    // Enumerate every flora + fauna species for the planet, for spawn-and-scan.
    //
    // PRIMARY source is the authored Starfield.esm PPBD list (Esm::GetPlanetSpecies):
    // it is COMPLETE even for a planet the player has never visited. The engine
    // aggregator (ID_1016657) only returns species the player has already discovered,
    // so on a fresh planet it is empty — which is why the old spawn-and-scan greened
    // nothing on never-visited worlds. We union the aggregator in too, in case it
    // tracks something the PPBD parse missed. Cache the FLOR + NPC_ form IDs; Papyrus
    // fetches them via GetPlanetSpeciesAt(index) and PlaceAtMe's each one.
    std::int32_t EnumeratePlanetSpecies(std::monostate, RE::TESForm* planetForm)
    {
        std::lock_guard lock(g_speciesCacheMtx);
        g_planetSpeciesCache.clear();
        if (!planetForm) return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId) return 0;

        int esm = 0, agg = 0, noform = 0, other = 0;

        // Add a species FormID if it resolves to a spawnable FLOR/NPC_ and isn't
        // already cached. Lists are small (tens per planet), so a linear dedup is
        // fine and avoids pulling in a set container.
        const auto consider = [&](std::uint32_t fid, int& tally) {
            if (!fid)
                return;
            for (const auto have : g_planetSpeciesCache)
                if (have == fid)
                    return;
            auto* form = RE::TESForm::LookupByID(fid);
            if (!form) { ++noform; return; }
            const auto ft = form->GetFormType();
            if (ft == RE::FormType::kFLOR || ft == RE::FormType::kNPC_) {
                g_planetSpeciesCache.push_back(fid);
                ++tally;
            } else {
                ++other;
            }
        };

        const auto& esmMap = Esm::GetPlanetSpecies();
        if (const auto it = esmMap.find(planetId); it != esmMap.end())
            for (const auto fid : it->second)
                consider(fid, esm);

        Engine::ForEachAggregatedFormId(planetId, [&](std::uint32_t fid) {
            consider(fid, agg);
        }, "EnumeratePlanetSpecies");

        spdlog::debug("EnumeratePlanetSpecies: planet=0x{:08X} esm={} agg(extra)={} other={} noform={} kept={}",
                     planetForm->GetFormID(), esm, agg, other, noform, g_planetSpeciesCache.size());
        return static_cast<std::int32_t>(g_planetSpeciesCache.size());
    }

    // Returns the form ID (as int) at the cached index. Papyrus converts via
    // Game.GetForm(formID). Returning TESForm* from a native triggered a
    // CommonLibSF ID-0 crash on DLL init (marshalling template had an unmapped
    // REL::ID for 1.16.236.0–1.16.244.0), so we return a plain int instead.
    std::int32_t GetPlanetSpeciesAt(std::monostate, std::int32_t index)
    {
        std::lock_guard lock(g_speciesCacheMtx);
        if (index < 0 || static_cast<std::size_t>(index) >= g_planetSpeciesCache.size()) return 0;
        return static_cast<std::int32_t>(g_planetSpeciesCache[index]);
    }

    // Queue a deferred CompleteSurvey dispatch. The scan-hook path calls this
    // instead of running CompleteSurvey immediately, so PlaceAtMe doesn't race
    // with the active scanner UI. The poller picks up the flag, waits until
    // menusVisible == false + a grace period, then dispatches Papyrus CompleteSurvey.
    void QueueCompleteSurvey(std::monostate)
    {
        Engine::g_pendingCompleteSurvey.store(true, std::memory_order_release);
    }

    // Set a flag for the per-frame poller to run an outline-refresh sweep on
    // nearby refs once menus are closed. Running the sweep directly from Papyrus
    // races with the scanner UI and crashes on procgen cells.
    std::int32_t ScanNearbyRefs(std::monostate)
    {
        Engine::g_pendingOutlineSweep.store(true, std::memory_order_release);
        spdlog::info("ScanNearbyRefs: queued pending sweep (fires on next menu close)");
        return 0;
    }

    // Covers the category Papyrus can't enumerate directly (no GetBiomeResources API):
    // runs the engine's per-planet aggregator (ID_1016657) which returns every tracked
    // form ID for the planet, then marks each by bumping its scan-flag byte. Flora, fauna,
    // and traits are also swept as a by-product, but those are already marked explicitly
    // via their per-category paths in Papyrus — this call's unique contribution is resources.
    std::int32_t MarkResourcesForPlanet(std::monostate, RE::TESForm* planetForm, std::int32_t delta)
    {
        if (!planetForm)
            return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto d        = static_cast<std::uint8_t>(delta <= 0 ? Engine::kDefaultScanDelta : (delta > Engine::kMaxScanDelta ? Engine::kMaxScanDelta : delta));
        // Shared single-planet completion: attribute bits + species/resource scan
        // flags + the survey-complete event (drops the "<Planet> Survey Data" slate).
        // Same core the galaxy sweep's per-planet finalize uses.
        const auto n = Engine::CompletePlanetSurveyState(planetId, d);
        spdlog::info("MarkResourcesForPlanet: planet=0x{:08X} planetId=0x{:08X} delta={} -> marked={}",
                     planetForm->GetFormID(), planetId, d, n);
        return n;
    }

    // Sweep every planet/moon in the galaxy and complete its survey ref-free (no
    // teleport, no spawn): discover it (creating the knowledge entry), then write the
    // attribute bits + species/resource scan flags. The Papyrus finalize pass mops up
    // the few async-create stragglers and fires each planet's completion event (slate).
    // Returns the number of planets processed. Console:
    //   cgf "CompletePlanetSurveyQuest.CompleteAllPlanetsSurveyData"
    std::int32_t CompleteAllPlanetsSurveyData(std::monostate)
    {
        return Engine::CompleteAllPlanetsSurveyData_Phase1(Engine::kDefaultScanDelta);
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
        const auto marked = Engine::CompletePlanetSurveyState(planetId);
        // Re-apply the ESM-authored flora/fauna now the entry is guaranteed ready (catches
        // the async-create stragglers the C++ sweep couldn't write).
        Engine::MarkEsmSpeciesForPlanet(planetId);
        return marked;
    }

    // Cache of every UNIQUE flora/fauna species across all planets (the keys of the species->
    // planets inversion). The atomic galaxy green spawns ONE live instance per entry, then calls
    // GreenSpeciesEverywhere to green it on every planet that hosts it.
    static std::vector<std::uint32_t> g_allSpeciesCache;
    static std::mutex                 g_allSpeciesMtx;

    std::int32_t EnumerateAllSpecies(std::monostate)
    {
        std::lock_guard lock(g_allSpeciesMtx);
        g_allSpeciesCache.clear();
        const auto& inv = Engine::GetSpeciesToPlanets();
        g_allSpeciesCache.reserve(inv.size());
        for (const auto& [fid, planets] : inv)
        {
            // Only spawnable FLOR/NPC_ forms — the live handle is a PlaceAtMe of this form.
            auto* form = RE::TESForm::LookupByID(fid);
            if (!form)
                continue;
            const auto ft = form->GetFormType();
            if (ft == RE::FormType::kFLOR || ft == RE::FormType::kNPC_)
                g_allSpeciesCache.push_back(fid);
        }
        spdlog::info("EnumerateAllSpecies: {} unique flora/fauna species across all planets",
                     g_allSpeciesCache.size());
        return static_cast<std::int32_t>(g_allSpeciesCache.size());
    }

    std::int32_t GetAllSpeciesFormIdAt(std::monostate, std::int32_t index)
    {
        std::lock_guard lock(g_allSpeciesMtx);
        if (index < 0 || static_cast<std::size_t>(index) >= g_allSpeciesCache.size())
            return 0;
        return static_cast<std::int32_t>(g_allSpeciesCache[index]);
    }

    // Green ONE species type on EVERY planet that hosts it, using `ref` (a live spawned instance
    // of that species) as the handle. Per planet it drives the tree write (ID_52161) + the count
    // completion (ID_52158), both with the planet explicit — so the whole galaxy's green for this
    // species is written from one spot, no visiting. Returns the number of planets greened.
    std::int32_t GreenSpeciesEverywhere(std::monostate, RE::TESObjectREFR* ref, std::int32_t speciesFid)
    {
        if (!ref || speciesFid == 0)
            return 0;
        const auto  fid = static_cast<std::uint32_t>(speciesFid);
        const auto& inv = Engine::GetSpeciesToPlanets();
        const auto  it  = inv.find(fid);
        if (it == inv.end())
            return 0;
        for (const auto planetId : it->second)
            Engine::GreenAndCompleteTypeForPlanet(ref, planetId, fid);
        return static_cast<std::int32_t>(it->second.size());
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
            "CompletePlanetSurveyNative"sv, "MarkResourcesForPlanet"sv, CPS_GUARDED(MarkResourcesForPlanet),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "EnumeratePlanetSpecies"sv, CPS_GUARDED(EnumeratePlanetSpecies),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetPlanetSpeciesFormIdAt"sv, CPS_GUARDED(GetPlanetSpeciesAt),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "UpdatePlanetProgressForSpecies"sv, CPS_GUARDED(UpdatePlanetProgressForSpecies),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "EnumerateAllSpecies"sv, CPS_GUARDED(EnumerateAllSpecies),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetAllSpeciesFormIdAt"sv, CPS_GUARDED(GetAllSpeciesFormIdAt),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GreenSpeciesEverywhere"sv, CPS_GUARDED(GreenSpeciesEverywhere),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "ScanNearbyRefs"sv, CPS_GUARDED(ScanNearbyRefs), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "QueueCompleteSurvey"sv, CPS_GUARDED(QueueCompleteSurvey), std::optional<bool> {true}, false);

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

        spdlog::info("Bound Papyrus natives: DebugLog, MarkTraitKnownForPlanet, MarkResourcesForPlanet, "
                     "EnumeratePlanetSpecies, GetPlanetSpeciesFormIdAt, UpdatePlanetProgressForSpecies, "
                     "EnumerateAllSpecies, GetAllSpeciesFormIdAt, "
                     "GreenSpeciesEverywhere, ScanNearbyRefs, QueueCompleteSurvey, CompleteAllPlanetsSurveyData, "
                     "GetSweepPlanetCount, GetSweepPlanetFormIdAt, FinalizeSweptPlanet");
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

    // Per-frame poll: waits for the pending-sweep flag + scanner menu closed,
    // then fires ScanAllRefsInCell on the player's current cell.
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
                        DispatchPapyrusStatic("CompleteSurvey");
                        spdlog::info("Poller: dispatched CompleteSurvey (scanner closed)");
                    }
                }
            }
            else {
                Engine::g_completeSurveyCountdown = 0;
            }

            // === Pending outline sweep ===
            if (!Engine::g_pendingOutlineSweep.load(std::memory_order_acquire)) {
                Engine::g_scanSweepCountdown = 0;
                return;
            }
            if (!readyToFire(Engine::g_scanSweepCountdown)) {
                return;
            }

            // Claim the sweep — clear flag first so a concurrent Papyrus set
            // during the sweep requeues cleanly next iteration.
            if (!Engine::g_pendingOutlineSweep.exchange(false, std::memory_order_acq_rel)) {
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->parentCell) {
                spdlog::warn("ScanSweep poller: no player or parent cell");
                return;
            }
            const int total = Engine::ScanAllRefsInCell(player->parentCell);
            spdlog::info("ScanSweep poller: fired after menus closed, scanned {} refs", total);
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
    spdlog::info("{} v{} loading", Plugin::Name, Plugin::Version.string());

    const auto* messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageCallback))
    {
        spdlog::critical("Failed to register messaging listener");
        return false;
    }
    return true;
}
