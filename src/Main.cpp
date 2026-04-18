#include "PCH.h"

// Address Library IDs for Starfield 1.16.236.0 — discovered via Ghidra.
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

    inline REL::Relocation<fn_get_manager_t>     GetKnowledgeManager {REL::ID(126578)};
    inline REL::Relocation<fn_set_trait_known_t> SetTraitKnownNative {REL::ID(52155)};
    inline REL::Relocation<fn_scan_ref_t>        ScanRefNative {REL::ID(83008)};
    inline REL::Relocation<fn_planet_progress_t> PlanetProgressNative {REL::ID(52157)};
    inline REL::Relocation<fn_db_lookup_t>       DbLookup {REL::ID(126806)};
    inline REL::Relocation<fn_incr_flag_t>       IncrementScanFlag {REL::ID(124898)};
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

    // Offsets within knowledge-manager / DB structs (Starfield 1.16.236.0, Ghidra-derived).
    constexpr std::size_t  kPlanetIdOffset       = 0x54;   // uint32 knowledge key at planetForm+0x54
    constexpr std::size_t  kManagerDbOffset      = 0x8B0;  // knowledge DB ptr at manager+0x8B0 (ID_126578 result)
    constexpr std::size_t  kDbContainerOffset    = 0x268;  // BSTHashMap<> start within the DB object
    constexpr std::size_t  kBucketOffsetTableOff = 0x12;   // uint16[] offset table start within a bucket base
    constexpr std::size_t  kEntrySubobjOffset    = 0x20;   // species subobj relative to the resolved entry ptr
    constexpr std::size_t  kFormPtrFormIdOffset  = 0x28;   // formID field in a TESForm* (aggregator ptr-arrays)
    constexpr std::uint8_t kBiomeScanCategory    = 0x0d;   // category byte for ScanRefNative / PlanetProgressNative

    // BSTHashMap lookup sentinel: out[3] value when the key is not found.
    constexpr std::uintptr_t kDbLookupNotFound     = 0xfe0;
    // Invalid/sentinel form ID used in aggregator arrays for empty slots.
    constexpr std::uint32_t  kInvalidFormId         = 0xFFFFFFFFu;
    // Default delta for scan-flag increment (marks species fully scanned in one pass).
    constexpr std::uint8_t   kDefaultScanDelta      = 100;
    // Maximum delta value (uint8 ceiling).
    constexpr std::uint8_t   kMaxScanDelta           = 255;

    // BSTArray header offsets within TESObjectCELL (Starfield 1.16.236.0).
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
    int MarkSpeciesScannedForPlanet(std::uint32_t planetId, std::uint32_t speciesFormId, std::uint8_t delta)
    {
        if (!planetId || !speciesFormId)
            return -1;
        const auto db = GetKnowledgeDB();
        if (!db)
            return -1;

        // Build 64-bit key: (trait_discriminator << 48) | (planet_id << 16) | 0.
        const std::uint16_t disc = *TraitDiscriminator.get();
        const std::uint64_t key =
            (static_cast<std::uint64_t>(disc) << 48) | (static_cast<std::uint64_t>(planetId) << 16);

        // Look up per-planet entry.
        std::uintptr_t out[4]    = {0, 0, 0, kDbLookupNotFound};
        auto           container = reinterpret_cast<std::uintptr_t*>(db + kDbContainerOffset);
        DbLookup(container, out, &key);
        if (out[3] == kDbLookupNotFound && out[2] == 0)
            return 0;

        // Compute subobj pointer: base = out[2] + *(uint16*)(out[2] + 0x12 + out[3] * 4); subobj = base + 0x20.
        const auto base            = reinterpret_cast<std::uint8_t*>(out[2]);
        const auto ushortOffsetPtr = reinterpret_cast<std::uint16_t*>(base + kBucketOffsetTableOff + out[3] * 4);
        const auto entryPtr        = base + *ushortOffsetPtr;
        auto       subobj          = entryPtr + kEntrySubobjOffset;

        IncrementScanFlag(subobj, speciesFormId, delta, 0);
        return 1;
    }

    // Iterate every form ID the engine's aggregator tracks for a planet.
    // Runs the aggregator, walks the four span pairs (two uint32[], two TESForm*[]),
    // calls `fn(formId)` for each valid entry, then frees the buffer.
    template <typename Fn>
    void ForEachAggregatedFormId(std::uint32_t planetId, Fn&& fn)
    {
        alignas(16) std::uint8_t buf[0x400] {};
        SurveyAggregator(buf, planetId);

        auto scanUint = [&](std::size_t beginOff, std::size_t endOff)
        {
            const auto* begin = *reinterpret_cast<std::uint32_t* const*>(buf + beginOff);
            const auto* end   = *reinterpret_cast<std::uint32_t* const*>(buf + endOff);
            for (auto p = begin; p && p != end; ++p)
            {
                if (*p && *p != kInvalidFormId)
                    fn(*p);
            }
        };
        auto scanPtr = [&](std::size_t beginOff, std::size_t endOff)
        {
            const auto* begin = *reinterpret_cast<std::uintptr_t* const*>(buf + beginOff);
            const auto* end   = *reinterpret_cast<std::uintptr_t* const*>(buf + endOff);
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

        SurveyBufferFree(buf);
    }

    // Mark every species the game tracks for this planet as scanned by bumping each flag.
    // Runs the game's own aggregator to enumerate form IDs — guaranteed to cover whatever
    // categories the UI displays (flora / fauna / resources / traits) for this planet.
    //
    // Returns the number of form IDs marked.
    int MarkEverythingForPlanet(std::uint32_t planetId, std::uint8_t delta)
    {
        if (!planetId)
            return 0;

        int marked = 0;
        ForEachAggregatedFormId(planetId, [&](std::uint32_t fid)
        {
            if (MarkSpeciesScannedForPlanet(planetId, fid, delta) == 1)
                ++marked;
        });
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

    // Walk a cell's references directly (bypassing CommonLibSF's ForEachReference
    // which uses a lock at cell+0x120 that isn't a BSReadWriteLock on 1.16.236.0
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

    // Enumerate all flora + fauna species tracked for the planet. Uses the engine
    // aggregator (ID_1016657) which returns form IDs across all biomes — broader
    // than Papyrus's GetBiomeFlora/GetBiomeActors which only return the player's
    // current biome. Cache the FLOR + NPC_ form IDs; Papyrus fetches them via
    // GetPlanetSpeciesAt(index).
    std::int32_t EnumeratePlanetSpecies(std::monostate, RE::TESForm* planetForm)
    {
        std::lock_guard lock(g_speciesCacheMtx);
        g_planetSpeciesCache.clear();
        if (!planetForm) return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId) return 0;

        Engine::ForEachAggregatedFormId(planetId, [&](std::uint32_t fid) {
            auto* form = RE::TESForm::LookupByID(fid);
            if (!form) return;
            const auto ft = form->GetFormType();
            if (ft == RE::FormType::kFLOR || ft == RE::FormType::kNPC_)
                g_planetSpeciesCache.push_back(fid);
        });

        spdlog::info("EnumeratePlanetSpecies: planet=0x{:08X} count={}",
                     planetForm->GetFormID(), g_planetSpeciesCache.size());
        return static_cast<std::int32_t>(g_planetSpeciesCache.size());
    }

    // Returns the form ID (as int) at the cached index. Papyrus converts via
    // Game.GetForm(formID). Returning TESForm* from a native triggered a
    // CommonLibSF ID-0 crash on DLL init (marshalling template had an unmapped
    // REL::ID for 1.16.236.0), so we return a plain int instead.
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
        const auto n        = Engine::MarkEverythingForPlanet(planetId, d);
        // Fire the completion check so the engine dispatches the survey-complete event
        // (generates the "Survey Data" slate, updates UI, etc.).
        Engine::NotifySurveyProgress(planetId);
        spdlog::info("MarkResourcesForPlanet: planet=0x{:08X} planetId=0x{:08X} delta={} -> marked={}",
                     planetForm->GetFormID(),
                     planetId,
                     d,
                     n);
        return n;
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
            "CompletePlanetSurveyNative"sv, "DebugLog"sv, &DebugLog, std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "MarkTraitKnownForPlanet"sv, &MarkTraitKnownForPlanet,
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "MarkResourcesForPlanet"sv, &MarkResourcesForPlanet,
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "EnumeratePlanetSpecies"sv, &EnumeratePlanetSpecies,
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetPlanetSpeciesFormIdAt"sv, &GetPlanetSpeciesAt,
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "UpdatePlanetProgressForSpecies"sv, &UpdatePlanetProgressForSpecies,
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "ScanNearbyRefs"sv, &ScanNearbyRefs, std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "QueueCompleteSurvey"sv, &QueueCompleteSurvey, std::optional<bool> {true}, false);

        spdlog::info("Bound Papyrus natives: DebugLog, MarkTraitKnownForPlanet, MarkResourcesForPlanet, "
                     "EnumeratePlanetSpecies, GetPlanetSpeciesFormIdAt, UpdatePlanetProgressForSpecies, "
                     "ScanNearbyRefs, QueueCompleteSurvey");
    }
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
    // doesn't line up with the MenuOpenCloseEvent specialization on 1.16.236.0
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
    spdlog::info("{} v{} loading", Plugin::Name, Plugin::Version.string());

    const auto* messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageCallback))
    {
        spdlog::critical("Failed to register messaging listener");
        return false;
    }
    return true;
}
