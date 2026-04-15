#include "PCH.h"
#include <windows.h>

// Address Library IDs for Starfield 1.16.236.0 — discovered via Ghidra.
// See memory/re_progress.md for the derivation and the knowledge-DB architecture.
//
// ID_126578: getter for the per-save knowledge-manager singleton.
//            The knowledge DB pointer lives at manager+0x8B0.
// ID_52155 : SetTraitKnown inner impl. (uint32 planet_id, BGSKeyword*, bool known).
//            Internally calls ID_52156 + fires a progress event. Safe for trait forms.
// ID_52156 : Generic AddOrRemoveKnownFormID. (uint32_t* args, longlong* db)
//            where args = { planet_id, form_id, insert_bool, ... }.
//            Appends/removes form_id in the per-planet known-forms list.
//
// Planet form's knowledge key is a uint32 at offset 0x54 on the planet form.
namespace Engine
{
    using fn_get_manager_t     = std::uintptr_t(*)();
    using fn_set_trait_known_t = void(*)(std::uint32_t planetId, std::uintptr_t keyword, bool known);
    using fn_add_known_form_t  = void(*)(void* args, std::uintptr_t* db);
    // ID_83038: flora scan writer.
    //   (db, struct{ char* flagPtr; int* outCountPtr; }, uint32_t* formId)
    //   Flips per-species "scanned" byte at component+0x28 and writes count from +0x24 into *outCountPtr.
    //   No-op if component for formId doesn't exist yet.
    using fn_flora_scan_t      = void(*)(std::uintptr_t db, void* args, const std::uint32_t* formId);
    // ID_83008: SetScanned inner, dispatches to flora (ID_83038+52157) or actor (ID_52160).
    //   (ObjectReference* ref, bool scannedFlag, byte=0xd, byte=0)
    //   The ref must be a real in-world reference placed in the current biome so its
    //   (ID_939118, ref->formID) component exists.
    using fn_scan_ref_t        = void(*)(void* ref, char scannedFlag, std::uint8_t a, std::uint8_t b);
    // ID_52157: per-planet progress updater. Called by ID_83008 only when ID_83038 found
    //   the ref-component. Signature: (ref, int count, byte=0xd, byte, byte).
    //   Calls ID_52188(ref, &out1, &out2) to derive planet id, then ID_52158 to write.
    using fn_planet_progress_t = void(*)(void* ref, std::int32_t count, std::uint8_t b3, std::uint8_t b4, std::uint8_t b5);
    // ID_126806: BSTHashMap lookup. Signature (container, out_buf[4 ulongs], &key_u64)
    //   out_buf layout on success: out[2] = entry base ptr, out[3] = entry index.
    //   Failure sentinel: out[3] == 0xfe0 and out[2] == 0.
    using fn_db_lookup_t       = void*(*)(std::uintptr_t* container, std::uintptr_t out[4], const std::uint64_t* key);
    // ID_124898: per-species flag increment on a "subobj" (value + 0x20).
    //   Signature (subobj*, species_id, delta_byte, ?).
    //   Finds/creates entry for species_id and increments the scan-flag byte at entry+0x21.
    using fn_incr_flag_t       = void(*)(void* subobj, std::uint32_t species_id, std::uint8_t delta, std::uint64_t zero);

    inline REL::Relocation<fn_get_manager_t>     GetKnowledgeManager{ REL::ID(126578) };
    inline REL::Relocation<fn_set_trait_known_t> SetTraitKnownNative{ REL::ID(52155)  };
    inline REL::Relocation<fn_add_known_form_t>  AddKnownFormNative { REL::ID(52156)  };
    inline REL::Relocation<fn_flora_scan_t>      FloraScanNative    { REL::ID(83038)  };
    inline REL::Relocation<fn_scan_ref_t>        ScanRefNative      { REL::ID(83008)  };
    inline REL::Relocation<fn_planet_progress_t> PlanetProgressNative{ REL::ID(52157) };
    inline REL::Relocation<fn_db_lookup_t>       DbLookup           { REL::ID(126806) };
    inline REL::Relocation<fn_incr_flag_t>       IncrementScanFlag  { REL::ID(124898) };
    inline REL::Relocation<std::uint16_t*>       TraitDiscriminator { REL::ID(938333) };

    // ID_83010 / ID_83012: biome-scoped species enumeration dispatchers.
    // Called by Papyrus GetBiomeActors / GetBiomeFlora after ID_52188 resolves
    // (ref -> planet_id, biome_id). The decompiled call shape:
    //   ID_83010(planet_id, biome_id, &out_array, CONCAT44(hi, int(weight * ID_500678)))
    // Array layout in out_array: [0..3]=count (uint32), [8..15]=uint32* (form IDs).
    using fn_biome_enum_t = void(*)(std::uint32_t planet_id,
                                    std::uint32_t biome_id,
                                    void*         out_array,
                                    std::uint64_t weight);

    inline REL::Relocation<fn_biome_enum_t> BiomeFaunaEnum { REL::ID(83010) };
    inline REL::Relocation<fn_biome_enum_t> BiomeFloraEnum { REL::ID(83012) };
    inline REL::Relocation<float*>          BiomeWeightScale { REL::ID(500678) };

    // Surface tree offsets (empirically verified 2026-04-15 — see re/findings-surfacetree.md).
    constexpr std::size_t kPlanetSurfaceTreeOffset = 0x38;   // BGSPlanet::PlanetData.surfaceTree
    constexpr std::size_t kTreeBiomeCountOffset    = 0x44;   // uint32 biome count in BGSSurface::Tree

    // ID_1016657: per-planet survey aggregator constructor.
    //   (buffer, planet_id) — populates buffer with all tracked form IDs for the planet
    //   across four arrays (two uint-arrays for flora/trait ids, two ptr-arrays for resource/other).
    //   Buffer size seen in callers: >= 0x250 bytes. We allocate 0x400 to be safe.
    using fn_aggregator_t   = void(*)(void* buffer, std::uint32_t planet_id);
    // ID_65318: cleanup for the aggregator buffer.
    using fn_buffer_free_t  = void(*)(void* buffer);

    inline REL::Relocation<fn_aggregator_t>  SurveyAggregator { REL::ID(1016657) };
    inline REL::Relocation<fn_buffer_free_t> SurveyBufferFree { REL::ID(65318)   };

    // ID_83007: returns 0 = not a biome species, 1 = biome species unscanned, 2 = already scanned.
    // Reads (939118, ref_formID) component — used as our "is this ref safe to pass to ID_83008" gate.
    using fn_is_biome_ref_t = char(*)(void* ref);
    inline REL::Relocation<fn_is_biome_ref_t> IsBiomeRef { REL::ID(83007) };

    // ID_97853: survey check-and-dispatch. Called by SetTraitKnown/SetScanned flows after a write.
    //   Signature: (struct*) where the struct starts with { uint32 planet_id, float prev_pct, u8 flag, u8 skip }.
    //   Fires PlayerPlanetSurveyProgressEvent (conditional) and PlayerPlanetSurveyCompleteEvent
    //   if the planet's survey is now 100%. The Complete event is what generates the in-world
    //   "<Planet> Survey Data" slate in the player's inventory.
    using fn_survey_notify_t = void(*)(void* ctx);
    inline REL::Relocation<fn_survey_notify_t> SurveyCheckNotify { REL::ID(97853) };

    // Offsets within knowledge-manager / DB structs (Starfield 1.16.236.0, Ghidra-derived).
    constexpr std::size_t  kPlanetIdOffset       = 0x54;   // uint32 knowledge key at planetForm+0x54
    constexpr std::size_t  kManagerDbOffset      = 0x8B0;  // knowledge DB ptr at manager+0x8B0 (ID_126578 result)
    constexpr std::size_t  kDbContainerOffset    = 0x268;  // BSTHashMap<> start within the DB object
    constexpr std::size_t  kBucketOffsetTableOff = 0x12;   // uint16[] offset table start within a bucket base
    constexpr std::size_t  kEntrySubobjOffset    = 0x20;   // species subobj relative to the resolved entry ptr
    constexpr std::size_t  kFormPtrFormIdOffset  = 0x28;   // formID field in a TESForm* (aggregator ptr-arrays)
    constexpr std::uint8_t kBiomeScanCategory    = 0x0d;   // category byte for ScanRefNative / PlanetProgressNative

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

    struct AddKnownArgs
    {
        std::uint32_t planetId;
        std::uint32_t formId;
        std::uint8_t  insert;
        std::uint8_t  pad[3]{};
    };

    std::uintptr_t GetKnowledgeDB()
    {
        const auto manager = GetKnowledgeManager();
        if (!manager) return 0;
        return *reinterpret_cast<std::uintptr_t*>(manager + kManagerDbOffset);
    }

    std::uint32_t ReadPlanetId(const RE::TESForm* planetForm)
    {
        if (!planetForm) return 0;
        return *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(planetForm) + kPlanetIdOffset);
    }

    bool MarkFormKnown(std::uintptr_t db, std::uint32_t planetId, std::uint32_t formId)
    {
        if (!db || !planetId || !formId) return false;
        AddKnownArgs args{ planetId, formId, 1, {} };
        AddKnownFormNative(&args, &db);
        return true;
    }

    bool MarkTraitKnown(std::uint32_t planetId, RE::BGSKeyword* keyword)
    {
        if (!planetId || !keyword) return false;
        SetTraitKnownNative(planetId, reinterpret_cast<std::uintptr_t>(keyword), true);
        return true;
    }

    // Mark a species (flora base form) as scanned via the ID_83038 writer.
    // Returns the resulting count (0 if component for this form did not exist / no change).
    int RegisterSpeciesScan(std::uint32_t formId)
    {
        const auto db = GetKnowledgeDB();
        if (!db || !formId) return 0;
        char flag = 1;
        int  outCount = 0;
        struct { char* flagPtr; int* outCountPtr; } args{ &flag, &outCount };
        // Pass a 2-element array for the formId arg (engine reads *param_3 = first elem).
        std::uint32_t formIdArr[2] = { formId, 0 };
        FloraScanNative(db, &args, formIdArr);
        return outCount;
    }

    // Read the species-component discriminator (set by the engine during component registration).
    std::uint16_t ReadSpeciesDiscriminator()
    {
        static REL::Relocation<std::uint16_t*> g{ REL::ID(939118) };
        return *g.get();
    }

    // Call the engine's inner SetScanned directly on a real ObjectReference,
    // bypassing the harvest fallback of the Papyrus SetScanned native.
    // The ref must be a biome-initialized instance (PlaceAtMe'd enabled) so its
    // (939118, ref->formID) component exists.
    void ScanRef(void* ref)
    {
        if (!ref) return;
        ScanRefNative(ref, 1 /*scanned*/, kBiomeScanCategory, 0);
    }

    // Skip ID_83038 (which no-ops for un-registered refs) and go straight to the
    // per-planet progress updater. count = the base form's formID (the "known species id").
    void UpdatePlanetProgress(void* ref, std::uint32_t speciesFormId)
    {
        if (!ref || !speciesFormId) return;
        PlanetProgressNative(ref, static_cast<std::int32_t>(speciesFormId), kBiomeScanCategory, 0, 0);
    }

    // Directly increment the scan-flag byte at per-planet component value's
    // species array entry for `speciesFormId`. This is the byte that ID_97851
    // reads to compute GetSurveyPercent — the only thing that matters.
    //
    // Returns 1 on success, 0 if planet not found in DB, -1 on null/invalid inputs.
    int MarkSpeciesScannedForPlanet(std::uint32_t planetId, std::uint32_t speciesFormId, std::uint8_t delta)
    {
        if (!planetId || !speciesFormId) return -1;
        const auto db = GetKnowledgeDB();
        if (!db) return -1;

        // Build 64-bit key: (trait_discriminator << 48) | (planet_id << 16) | 0.
        const std::uint16_t disc = *TraitDiscriminator.get();
        const std::uint64_t key  = (static_cast<std::uint64_t>(disc) << 48)
                                 | (static_cast<std::uint64_t>(planetId) << 16);

        // Look up per-planet entry.
        std::uintptr_t out[4] = { 0, 0, 0, 0xfe0 };
        auto container = reinterpret_cast<std::uintptr_t*>(db + kDbContainerOffset);
        DbLookup(container, out, &key);
        if (out[3] == 0xfe0 && out[2] == 0) return 0;

        // Compute subobj pointer: base = out[2] + *(uint16*)(out[2] + 0x12 + out[3] * 4); subobj = base + 0x20.
        const auto base = reinterpret_cast<std::uint8_t*>(out[2]);
        const auto ushortOffsetPtr = reinterpret_cast<std::uint16_t*>(base + kBucketOffsetTableOff + out[3] * 4);
        const auto entryPtr        = base + *ushortOffsetPtr;
        auto subobj                = entryPtr + kEntrySubobjOffset;

        IncrementScanFlag(subobj, speciesFormId, delta, 0);
        return 1;
    }

    // Mark every species the game tracks for this planet as scanned by bumping each flag.
    // Runs the game's own aggregator to enumerate form IDs — guaranteed to cover whatever
    // categories the UI displays (flora / fauna / resources / traits) for this planet.
    //
    // Returns the number of form IDs marked.
    int MarkEverythingForPlanet(std::uint32_t planetId, std::uint8_t delta)
    {
        if (!planetId) return 0;

        // Allocate aggregator buffer on the heap to stay conservative with stack size.
        alignas(16) std::uint8_t buf[0x400]{};
        SurveyAggregator(buf, planetId);

        int marked = 0;

        // Helper to mark a single form id.
        auto mark = [&](std::uint32_t fid) {
            if (!fid || fid == 0xFFFFFFFFu) return;
            if (MarkSpeciesScannedForPlanet(planetId, fid, delta) == 1) ++marked;
        };

        // Four arrays inside the aggregator buffer (offsets observed in ID_97851):
        //   +0x218..+0x220 : uint32[]  (inline form ids, e.g. traits)
        //   +0x230..+0x238 : uint32[]  (inline form ids)
        //   +0x1e8..+0x1f0 : TESForm*[] (form ids at *ptr+0x28)
        //   +0x200..+0x208 : TESForm*[] (form ids at *ptr+0x28)

        auto scanUintRange = [&](std::size_t beginOff, std::size_t endOff) {
            const auto* begin = *reinterpret_cast<std::uint32_t* const*>(buf + beginOff);
            const auto* end   = *reinterpret_cast<std::uint32_t* const*>(buf + endOff);
            for (auto p = begin; p && p != end; ++p) mark(*p);
        };
        auto scanPtrRange = [&](std::size_t beginOff, std::size_t endOff) {
            const auto* begin = *reinterpret_cast<std::uintptr_t* const*>(buf + beginOff);
            const auto* end   = *reinterpret_cast<std::uintptr_t* const*>(buf + endOff);
            for (auto p = begin; p && p != end; ++p) {
                if (*p == 0) continue;
                mark(*reinterpret_cast<const std::uint32_t*>(*p + kFormPtrFormIdOffset));
            }
        };

        scanUintRange(kAggUintSpan0Begin, kAggUintSpan0End);
        scanUintRange(kAggUintSpan1Begin, kAggUintSpan1End);
        scanPtrRange (kAggPtrSpan0Begin,  kAggPtrSpan0End);
        scanPtrRange (kAggPtrSpan1Begin,  kAggPtrSpan1End);

        SurveyBufferFree(buf);
        return marked;
    }

    // Forward declaration (defined lower down with the diagnostic helpers).
    bool IsReadable(std::uintptr_t p, std::size_t bytes);

    // Enumerate every biome on a planet (via tree+0x44 biome count) and call
    // the engine's BiomeFaunaEnum/BiomeFloraEnum dispatchers per biome_id, then
    // pipe each returned form id through MarkSpeciesScannedForPlanet — which
    // uses ID_124898's find-or-create path to materialize the slot even if it
    // wasn't in the per-planet knowledge DB yet.
    //
    // This is the fix for the 97% cap on remote-scanned biome-heavy planets.
    //
    // Returns the number of species/forms marked. Logs detailed branch/loop state.
    int MarkAllBiomeSpeciesForPlanet(std::uint32_t planet_id,
                                     const RE::TESForm* planetForm,
                                     std::uint8_t delta)
    {
        if (!planet_id || !planetForm) {
            spdlog::info("MarkAllBiomeSpecies: invalid input (planet_id={} form={})",
                         planet_id, static_cast<const void*>(planetForm));
            return 0;
        }
        const auto* base = reinterpret_cast<const std::uint8_t*>(planetForm);
        const auto  tree = *reinterpret_cast<const std::uint8_t* const*>(base + kPlanetSurfaceTreeOffset);
        if (!tree) {
            spdlog::info("MarkAllBiomeSpecies: planet 0x{:08X} has null surfaceTree — skipping",
                         planet_id);
            return 0;
        }
        const auto biomeCount = *reinterpret_cast<const std::uint32_t*>(tree + kTreeBiomeCountOffset);
        spdlog::info("MarkAllBiomeSpecies: planet=0x{:08X} tree=0x{:016X} biomeCount={}",
                     planet_id, reinterpret_cast<std::uintptr_t>(tree), biomeCount);
        if (biomeCount == 0 || biomeCount > 64) {
            spdlog::info("MarkAllBiomeSpecies: biomeCount out of sane range, aborting");
            return 0;
        }

        // Build the weight parameter. Papyrus GetBiomeActors(1.0) passes
        //   CONCAT44(uVar19, (int)(1.0 * ID_500678))
        // uVar19 is almost certainly the float's raw bits (param_5 passed
        // through), so the full 64-bit packs {float_bits, scaled_int}.
        const float fPct = 1.0f;
        const float scale = *BiomeWeightScale.get();
        const auto  weight_lo = static_cast<std::uint32_t>(static_cast<std::int32_t>(fPct * scale));
        std::uint32_t weight_hi;
        std::memcpy(&weight_hi, &fPct, sizeof(weight_hi));
        const auto weight = (static_cast<std::uint64_t>(weight_hi) << 32) | weight_lo;
        spdlog::info("MarkAllBiomeSpecies: scale={} weight_lo=0x{:08X} weight_hi=0x{:08X}",
                     scale, weight_lo, weight_hi);

        int totalMarked = 0;
        for (std::uint32_t biome_id = 0; biome_id < biomeCount; ++biome_id) {
            alignas(16) std::uint8_t faunaBuf[0x40]{};
            alignas(16) std::uint8_t floraBuf[0x40]{};

            BiomeFaunaEnum(planet_id, biome_id, faunaBuf, weight);
            BiomeFloraEnum(planet_id, biome_id, floraBuf, weight);

            auto walk = [&](const char* label, const std::uint8_t* buf) {
                const auto count = *reinterpret_cast<const std::uint32_t*>(buf);
                const auto* data = *reinterpret_cast<const std::uint32_t* const*>(buf + 8);
                spdlog::info("  biome={} {} count={} data=0x{:016X}",
                             biome_id, label, count,
                             reinterpret_cast<std::uintptr_t>(data));
                if (!data || count == 0 || count > 1024) return;
                if (!IsReadable(reinterpret_cast<std::uintptr_t>(data), count * sizeof(std::uint32_t))) {
                    spdlog::info("    data unreadable, skipping");
                    return;
                }
                for (std::uint32_t i = 0; i < count; ++i) {
                    const auto fid = data[i];
                    if (!fid || fid == 0xFFFFFFFFu) continue;
                    const auto rc = MarkSpeciesScannedForPlanet(planet_id, fid, delta);
                    if (rc == 1) ++totalMarked;
                }
            };

            walk("fauna", faunaBuf);
            walk("flora", floraBuf);
        }

        spdlog::info("MarkAllBiomeSpecies: totalMarked={} across {} biomes",
                     totalMarked, biomeCount);
        return totalMarked;
    }

    // Diagnostic: hex-dump a range of bytes starting at `base` to the log.
    // Used to probe unknown struct layouts without committing to specific offsets.
    void HexDump(const char* label, const std::uint8_t* base, std::size_t bytes)
    {
        if (!base) {
            spdlog::info("{}: null", label);
            return;
        }
        spdlog::info("{} @ 0x{:016X} ({} bytes):", label, reinterpret_cast<std::uintptr_t>(base), bytes);
        for (std::size_t i = 0; i < bytes; i += 16) {
            std::string line;
            for (std::size_t j = 0; j < 16 && (i + j) < bytes; ++j) {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02X ", base[i + j]);
                line += buf;
            }
            spdlog::info("  +0x{:03X}: {}", i, line);
        }
    }

    // Diagnostic: probe the planet's static structure. Dumps:
    //   - First 0x100 bytes of the planet form (TESForm + BGSPlanet::PlanetData)
    //   - The surfaceTree* pointer at +0x30
    //   - First 0x100 bytes of the surface tree, if non-null
    // The user runs this on a known planet (e.g. Skink with 7/8 resources) and we read
    // the log offline to figure out where the biome list lives.
    // Check whether `p` points to at least `bytes` of readable committed memory.
    // Uses VirtualQuery so we never dereference an invalid pointer (which crashes).
    bool IsReadable(std::uintptr_t p, std::size_t bytes)
    {
        if (p == 0) return false;
        if (p & 0x3) return false;
        if ((p >> 48) != 0) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE
                                  | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                  | PAGE_WRITECOPY   | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & kReadable)) return false;
        if (mbi.Protect & PAGE_GUARD) return false;
        // Ensure full span fits within this page region.
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (p + bytes > regionEnd) return false;
        return true;
    }

    // Backwards-compat alias used below; now wraps the real check with a small default.
    bool LooksLikeHeapPtr(std::uintptr_t p) { return IsReadable(p, 8); }

    void ProbePointer(const char* label, std::uintptr_t candidate, std::size_t bytes)
    {
        spdlog::info("{} = 0x{:016X}", label, candidate);
        if (IsReadable(candidate, bytes)) {
            HexDump(label, reinterpret_cast<const std::uint8_t*>(candidate), bytes);
        } else {
            spdlog::info("{}: unreadable — skipping dump", label);
        }
    }

    void DumpPlanetLayout(const RE::TESForm* planetForm)
    {
        if (!planetForm) {
            spdlog::info("DumpPlanetLayout: null planetForm");
            return;
        }
        const auto* base = reinterpret_cast<const std::uint8_t*>(planetForm);
        spdlog::info("=== DumpPlanetLayout: planet=0x{:08X} formType={} ===",
                     planetForm->GetFormID(), static_cast<int>(planetForm->GetFormType()));

        // Empirically verified for 1.16.236.0:
        //   PlanetData (kPNDT, 0xBA)
        //     +0x38: BGSSurface::Tree* surfaceTree  (kSFTR, 0xB6) — NOT +0x30 as CommonLibSF claims
        //     +0x58..+0xA8: BSTArray<BGSKeyword*> (keywords — traits live here)
        const auto surfaceTree = *reinterpret_cast<const std::uint8_t* const*>(base + 0x38);
        spdlog::info("surfaceTree* @ (form+0x38) = 0x{:016X}", reinterpret_cast<std::uintptr_t>(surfaceTree));
        if (!LooksLikeHeapPtr(reinterpret_cast<std::uintptr_t>(surfaceTree))) {
            spdlog::info("surfaceTree: invalid — aborting tree dump");
            return;
        }

        if (!IsReadable(reinterpret_cast<std::uintptr_t>(surfaceTree), 0x200)) {
            spdlog::info("surfaceTree: pointer set but region not fully readable");
            return;
        }
        HexDump("SurfaceTree", surfaceTree, 0x200);

        // Safely probe 8-byte-aligned slots in the tree for readable sub-pointers.
        // IsReadable uses VirtualQuery, so garbage values won't crash us.
        for (std::size_t off = 0x38; off < 0x200; off += 8) {
            const auto p = *reinterpret_cast<const std::uintptr_t*>(surfaceTree + off);
            if (p == 0) continue;
            if (!IsReadable(p, 0x80)) continue;
            char buf[40];
            std::snprintf(buf, sizeof(buf), "tree+0x%02zX->", off);
            HexDump(buf, reinterpret_cast<const std::uint8_t*>(p), 0x80);
        }
    }

    // Diagnostic: walk the aggregator for a planet and log every form id + span label.
    // Returns total count of form ids found across all four spans. Safe to call from
    // orbit or landed context (aggregator is planet-scoped, not player-scoped).
    int DumpAggregator(std::uint32_t planetId)
    {
        if (!planetId) return 0;
        alignas(16) std::uint8_t buf[0x400]{};
        SurveyAggregator(buf, planetId);

        int total = 0;
        auto dumpUint = [&](const char* label, std::size_t beginOff, std::size_t endOff) {
            const auto* begin = *reinterpret_cast<std::uint32_t* const*>(buf + beginOff);
            const auto* end   = *reinterpret_cast<std::uint32_t* const*>(buf + endOff);
            int n = 0;
            for (auto p = begin; p && p != end; ++p) {
                spdlog::info("  [{}] fid=0x{:08X}", label, *p);
                ++n; ++total;
            }
            spdlog::info("  {}: count={}", label, n);
        };
        auto dumpPtr = [&](const char* label, std::size_t beginOff, std::size_t endOff) {
            const auto* begin = *reinterpret_cast<std::uintptr_t* const*>(buf + beginOff);
            const auto* end   = *reinterpret_cast<std::uintptr_t* const*>(buf + endOff);
            int n = 0;
            for (auto p = begin; p && p != end; ++p) {
                if (*p == 0) continue;
                const auto fid = *reinterpret_cast<const std::uint32_t*>(*p + kFormPtrFormIdOffset);
                spdlog::info("  [{}] ptr=0x{:016X} fid=0x{:08X}", label, *p, fid);
                ++n; ++total;
            }
            spdlog::info("  {}: count={}", label, n);
        };

        spdlog::info("=== Aggregator dump for planetId=0x{:08X} ===", planetId);
        dumpUint("uint0@0x218", kAggUintSpan0Begin, kAggUintSpan0End);
        dumpUint("uint1@0x230", kAggUintSpan1Begin, kAggUintSpan1End);
        dumpPtr ("ptr0@0x1E8",  kAggPtrSpan0Begin,  kAggPtrSpan0End);
        dumpPtr ("ptr1@0x200",  kAggPtrSpan1Begin,  kAggPtrSpan1End);
        spdlog::info("=== Aggregator total={} ===", total);

        SurveyBufferFree(buf);
        return total;
    }

    // Fire the survey check/notify routine. Triggers the completion event that
    // generates the "<Planet> Survey Data" slate when the survey hits 100%.
    //
    // The ctx struct matches ID_97853's expectations: planet_id at +0, float prev-pct
    // at +4, byte flag at +8 (0 = skip progress event dispatch), byte skip at +9 (0 = run).
    void NotifySurveyProgress(std::uint32_t planetId)
    {
        if (!planetId) return;
        struct Ctx {
            std::uint32_t planetId;
            float         prevPct;
            std::uint8_t  flag;
            std::uint8_t  skip;
            std::uint16_t pad{};
        } ctx{ planetId, 0.0f, 0, 0, 0 };
        SurveyCheckNotify(&ctx);
    }

    // Walk a cell's references, calling ID_83008 on each flora/fauna ref.
    // Unlike PlaceAtMe'd refs, these are real engine-placed instances whose
    // (939118, ref_formID) knowledge component exists, so ID_83008 flips
    // the per-ref scanned flag — making the scanner UI treat them as scanned.
    int ScanAllRefsInCell(RE::TESObjectCELL* cell)
    {
        if (!cell || !cell->IsAttached()) {
            spdlog::info("ScanAllRefsInCell: cell null or not attached");
            return 0;
        }
        spdlog::info("ScanAllRefsInCell: entering cell 0x{:08X}", cell->GetFormID());
        int scanned = 0;
        int inspected = 0;
        cell->ForEachReference([&](const RE::NiPointer<RE::TESObjectREFR>& ref) -> RE::BSContainer::ForEachResult {
            ++inspected;
            if (!ref) return RE::BSContainer::ForEachResult::kContinue;
            auto base = ref->GetBaseObject();
            if (!base) return RE::BSContainer::ForEachResult::kContinue;
            const auto ft = base->GetFormType();
            if (ft != RE::FormType::kFLOR && ft != RE::FormType::kNPC_) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            spdlog::info("  candidate ref=0x{:08X} base=0x{:08X} ft={}",
                         ref->GetFormID(), base->GetFormID(), static_cast<int>(ft));
            const char biome = IsBiomeRef(ref.get());
            spdlog::info("    IsBiomeRef -> {}", static_cast<int>(biome));
            if (biome == 0) return RE::BSContainer::ForEachResult::kContinue;
            spdlog::info("    calling ScanRefNative");
            ScanRefNative(ref.get(), 1, kBiomeScanCategory, 0);
            ++scanned;
            spdlog::info("    done");
            return RE::BSContainer::ForEachResult::kContinue;
        });
        spdlog::info("ScanAllRefsInCell: inspected={} scanned={}", inspected, scanned);
        return scanned;
    }
}

namespace Papyrus
{
    // Bound as CompletePlanetSurveyQuest.MarkFormKnownForPlanet(planet, form) -> bool.
    // Papyrus iterates GetBiomeFlora / GetBiomeActors / resources and calls this per entry.
    bool MarkFormKnownForPlanet(std::monostate, RE::TESForm* planetForm, RE::TESForm* formToMark)
    {
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto db       = Engine::GetKnowledgeDB();
        const std::uint32_t planetFormId = planetForm ? planetForm->GetFormID() : 0;
        const std::uint32_t markFormId   = formToMark ? formToMark->GetFormID() : 0;
        spdlog::info("MarkFormKnownForPlanet: planet=0x{:08X} planetId=0x{:08X} form=0x{:08X} db=0x{:016X}",
                     planetFormId, planetId, markFormId, db);
        if (!planetId || !formToMark) {
            return false;
        }
        return Engine::MarkFormKnown(db, planetId, markFormId);
    }

    // Bound as CompletePlanetSurveyQuest.MarkTraitKnownForPlanet(planet, traitKeyword) -> bool.
    // Fires the "trait discovered" progress event so the UI/notifications behave normally.
    bool MarkTraitKnownForPlanet(std::monostate, RE::TESForm* planetForm, RE::BGSKeyword* keyword)
    {
        const auto planetId = Engine::ReadPlanetId(planetForm);
        spdlog::info("MarkTraitKnownForPlanet: planet=0x{:08X} planetId=0x{:08X} kw={}",
                     planetForm ? planetForm->GetFormID() : 0u, planetId,
                     static_cast<const void*>(keyword));
        if (!planetId || !keyword) return false;
        return Engine::MarkTraitKnown(planetId, keyword);
    }

    void DebugLog(std::monostate, RE::BSFixedString msg)
    {
        spdlog::info("[papyrus] {}", msg.c_str());
    }

    // Bypass SetScanned's harvest fallback and call the engine's inner scan writer
    // directly on a real ObjectReference. The ref MUST be a PlaceAtMe'd enabled
    // instance (so the engine has registered its knowledge component).
    bool ScanRef(std::monostate, RE::TESObjectREFR* ref)
    {
        if (!ref) return false;
        spdlog::info("ScanRef: ref=0x{:08X}", ref->GetFormID());
        Engine::ScanRef(ref);
        return true;
    }

    // Takes a real (in-biome) ref and a species base form id. Bypasses the per-ref
    // component check and goes directly to the per-planet progress update.
    bool UpdatePlanetProgressForSpecies(std::monostate, RE::TESObjectREFR* ref, RE::TESForm* speciesForm)
    {
        if (!ref || !speciesForm) return false;
        const auto fid = speciesForm->GetFormID();
        spdlog::info("UpdatePlanetProgress: ref=0x{:08X} species=0x{:08X}", ref->GetFormID(), fid);
        Engine::UpdatePlanetProgress(ref, fid);
        return true;
    }

    // Mark every species the game tracks for this planet (flora/fauna/resources/traits) as scanned.
    // Uses the engine's own aggregator to enumerate the form IDs — covers every UI category.
    // Walk every currently-loaded cell in the player's worldspace and scan all flora/fauna refs,
    // so the scanner UI stops showing them as "unscanned". Returns the number of refs scanned.
    // Mark every BGSLocation whose parent chain leads to the player's current
    // worldspace as explored + everExplored. Moves the "Frozen Mountains (50%)"
    // style region-exploration meter. Independent from the survey DB.
    std::int32_t MarkLocationsExplored(std::monostate)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->parentCell) return 0;
        auto* ws = player->parentCell->cellWorldspace;
        if (!ws) return 0;
        auto* rootLoc = ws->location.get();
        if (!rootLoc) {
            spdlog::warn("MarkLocationsExplored: worldspace has no root location");
            return 0;
        }

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return 0;

        auto& locForms = dh->formArrays[std::to_underlying(RE::FormType::kLCTN)];
        const RE::BSAutoReadLock locker(locForms.lock);

        int marked = 0;

        // Mark the root explicitly.
        if (!rootLoc->explored) {
            rootLoc->explored = true;
            rootLoc->everExplored = true;
            ++marked;
        }

        // Starfield's deepest observed location hierarchy is ~6 levels
        // (galaxy → system → body → biome → region → area); 64 is a generous
        // cycle-safety bound, not a real depth limit.
        constexpr int kMaxLocationParentDepth = 64;

        for (auto& formPtr : locForms.formArray) {
            if (!formPtr) continue;
            auto* loc = formPtr->As<RE::BGSLocation>();
            if (!loc || loc->explored) continue;
            auto* parent = loc->parentLocation.get();
            int depth = 0;
            while (parent && depth < kMaxLocationParentDepth) {
                if (parent == rootLoc) {
                    loc->explored = true;
                    loc->everExplored = true;
                    ++marked;
                    break;
                }
                parent = parent->parentLocation.get();
                ++depth;
            }
        }
        spdlog::info("MarkLocationsExplored: marked {} locations under 0x{:08X}",
                     marked, rootLoc->GetFormID());
        return marked;
    }

    std::int32_t ScanNearbyRefs(std::monostate)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            spdlog::warn("ScanNearbyRefs: no player");
            return 0;
        }
        auto* parentCell = player->parentCell;
        if (!parentCell) {
            spdlog::warn("ScanNearbyRefs: no parent cell");
            return 0;
        }
        const int total = Engine::ScanAllRefsInCell(parentCell);
        spdlog::info("ScanNearbyRefs: scanned {} flora/fauna refs in parent cell", total);
        return total;
    }

    // Diagnostic: hex-dump the planet form + its surface tree to the log.
    void DumpPlanetLayout(std::monostate, RE::TESForm* planetForm)
    {
        Engine::DumpPlanetLayout(planetForm);
    }

    // Enumerate every biome on the planet via the engine's own biome-species
    // dispatchers and mark all returned flora/fauna as scanned. Closes the
    // 97% remote-scan cap by materializing biome slots that weren't in the
    // per-planet knowledge DB before landing.
    std::int32_t MarkAllBiomeSpecies(std::monostate, RE::TESForm* planetForm, std::int32_t delta)
    {
        if (!planetForm) return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto d        = static_cast<std::uint8_t>(delta <= 0 ? 100 : (delta > 255 ? 255 : delta));
        const auto n = Engine::MarkAllBiomeSpeciesForPlanet(planetId, planetForm, d);
        spdlog::info("MarkAllBiomeSpecies (Papyrus): planet=0x{:08X} planetId=0x{:08X} delta={} -> marked={}",
                     planetForm->GetFormID(), planetId, d, n);
        return n;
    }

    // Diagnostic: dump the aggregator contents for a planet to the SFSE log. Returns total count.
    std::int32_t DumpAggregator(std::monostate, RE::TESForm* planetForm)
    {
        if (!planetForm) return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        spdlog::info("DumpAggregator: planetForm=0x{:08X} planetId=0x{:08X}",
                     planetForm->GetFormID(), planetId);
        return Engine::DumpAggregator(planetId);
    }

    std::int32_t MarkEverythingForPlanet(std::monostate, RE::TESForm* planetForm, std::int32_t delta)
    {
        if (!planetForm) return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto d        = static_cast<std::uint8_t>(delta <= 0 ? 100 : (delta > 255 ? 255 : delta));
        const auto n        = Engine::MarkEverythingForPlanet(planetId, d);
        // Fire the completion check so the engine dispatches the survey-complete event
        // (generates the "Survey Data" slate, updates UI, etc.).
        Engine::NotifySurveyProgress(planetId);
        spdlog::info("MarkEverythingForPlanet: planet=0x{:08X} planetId=0x{:08X} delta={} -> marked={}",
                     planetForm->GetFormID(), planetId, d, n);
        return n;
    }

    // The definitive write: bump the scan-flag byte at the per-planet species array,
    // which is exactly the byte GetSurveyPercent's aggregator reads.
    bool MarkSpeciesScannedForPlanet(std::monostate, RE::TESForm* planetForm, RE::TESForm* speciesForm, std::int32_t delta)
    {
        if (!planetForm || !speciesForm) return false;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        const auto fid      = speciesForm->GetFormID();
        const auto d        = static_cast<std::uint8_t>(delta == 0 ? 1 : delta);
        const auto rc = Engine::MarkSpeciesScannedForPlanet(planetId, fid, d);
        spdlog::info("MarkSpeciesScanned: planet=0x{:08X} planetId=0x{:08X} species=0x{:08X} delta={} -> rc={}",
                     planetForm->GetFormID(), planetId, fid, d, rc);
        return rc == 1;
    }

    // Register a species scan. Returns post-call species count (0 on no-op).
    std::int32_t RegisterSpeciesScan(std::monostate, RE::TESForm* speciesForm)
    {
        if (!speciesForm) return 0;
        const auto formId = speciesForm->GetFormID();
        const auto disc   = Engine::ReadSpeciesDiscriminator();
        const auto count  = Engine::RegisterSpeciesScan(formId);
        spdlog::info("RegisterSpeciesScan: form=0x{:08X} disc=0x{:04X} -> count={}", formId, disc, count);
        return count;
    }

    void Register()
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            spdlog::error("Failed to get VM singleton");
            return;
        }
        auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "MarkFormKnownForPlanet"sv,
            &MarkFormKnownForPlanet,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "MarkTraitKnownForPlanet"sv,
            &MarkTraitKnownForPlanet,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "DebugLog"sv,
            &DebugLog,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "RegisterSpeciesScan"sv,
            &RegisterSpeciesScan,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "ScanRef"sv,
            &ScanRef,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "UpdatePlanetProgressForSpecies"sv,
            &UpdatePlanetProgressForSpecies,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "MarkSpeciesScannedForPlanet"sv,
            &MarkSpeciesScannedForPlanet,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "MarkEverythingForPlanet"sv,
            &MarkEverythingForPlanet,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "DumpAggregator"sv,
            &DumpAggregator,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "DumpPlanetLayout"sv,
            &DumpPlanetLayout,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "MarkAllBiomeSpecies"sv,
            &MarkAllBiomeSpecies,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "ScanNearbyRefs"sv,
            &ScanNearbyRefs,
            std::optional<bool>{ true },
            false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv,
            "MarkLocationsExplored"sv,
            &MarkLocationsExplored,
            std::optional<bool>{ true },
            false);

        spdlog::info("Bound Papyrus natives: MarkFormKnownForPlanet, MarkTraitKnownForPlanet, DebugLog, RegisterSpeciesScan, ScanRef, UpdatePlanetProgressForSpecies, MarkSpeciesScannedForPlanet");
    }
}

namespace Hook
{
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
        using fn_t = void(*)(void*);   // ID_97853: void(undefined4* ctx)

        static void thunk(void* ctx)
        {
            func(ctx);   // call original SurveyCheckNotify (ID_97853)

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;
            auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);

            // All per-scan constants hoisted out of the hot path: firing on
            // every species scan, we don't want to reconstruct the lambda,
            // both BSFixedStrings (atomized), and the empty smart pointer each time.
            using VarArray = RE::BSScrapArray<RE::BSScript::Variable>;
            static const std::function<bool(VarArray&)> kNoArgs =
                [](VarArray&) -> bool { return true; };
            static const RE::BSFixedString kScriptName{ "CompletePlanetSurveyQuest" };
            static const RE::BSFixedString kFnName{ "CompleteSurveyIfEnabled" };
            static const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> kNoCallback;

            ivm->DispatchStaticCall(kScriptName, kFnName, kNoArgs, kNoCallback, 0);
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
        for (std::size_t i = 0; i < scan_limit; ++i) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(outer + i);
            if (*p == 0xE8) {
                const auto disp        = *reinterpret_cast<const std::int32_t*>(p + 1);
                const auto call_target = static_cast<std::uintptr_t>(
                    static_cast<std::int64_t>(outer + i + 5) + disp);
                if (call_target == inner) {
                    return outer + i;
                }
            }
        }
        return 0;
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> outer{ REL::ID(52157) };   // planet-progress updater
        REL::Relocation<std::uintptr_t> inner{ REL::ID(97853) };   // survey check/notify

        const auto call_site = FindCallSite(outer.address(), inner.address());
        if (!call_site) {
            spdlog::error("ScanHook: CALL to ID_97853 not found inside ID_52157 — hook skipped");
            return;
        }

        ScanHook::func = reinterpret_cast<ScanHook::fn_t>(
            REL::GetTrampoline().write_call<5>(
                call_site,
                reinterpret_cast<std::uintptr_t>(ScanHook::thunk)));

        spdlog::info("ScanHook: installed at call-site 0x{:016X} (ID_52157 → ID_97853)",
                     call_site);
    }

    // Diagnostic call-site hook: ID_52153 (trait writer / per-planet knowledge writer)
    // is the path that remote starmap scans flow through to dispatch survey progress
    // events. Hooking its CALL site to ID_97853 lets us log every trait-driven
    // survey update with the planet_id that was affected.
    //
    // This is a read-only diagnostic — the thunk calls the original ID_97853
    // unchanged, then logs the planet_id for RE.
    struct TraitTraceHook
    {
        using fn_t = void(*)(void*);

        static void thunk(void* ctx)
        {
            std::uint32_t planet_id = 0;
            if (ctx) {
                planet_id = *reinterpret_cast<std::uint32_t*>(ctx);
            }
            spdlog::info("TraitTrace: ID_52153 -> ID_97853 fired, planet=0x{:08X}", planet_id);
            func(ctx);
        }

        static inline fn_t func = nullptr;
    };

    void InstallTraitTrace()
    {
        REL::Relocation<std::uintptr_t> outer{ REL::ID(52153) };   // per-planet knowledge writer
        REL::Relocation<std::uintptr_t> inner{ REL::ID(97853) };   // survey check/notify

        // 0x800 window — ID_52153 may be longer than the 0x400 species path.
        const auto call_site = FindCallSite(outer.address(), inner.address(), 0x800);
        if (!call_site) {
            spdlog::error("TraitTrace: CALL to ID_97853 not found inside ID_52153 — hook skipped");
            return;
        }

        TraitTraceHook::func = reinterpret_cast<TraitTraceHook::fn_t>(
            REL::GetTrampoline().write_call<5>(
                call_site,
                reinterpret_cast<std::uintptr_t>(TraitTraceHook::thunk)));

        spdlog::info("TraitTrace: installed at call-site 0x{:016X} (ID_52153 -> ID_97853)", call_site);
    }
}

namespace
{
    void MessageCallback(SFSE::MessagingInterface::Message* a_msg) noexcept
    {
        if (a_msg->type == SFSE::MessagingInterface::kPostDataLoad) {
            Papyrus::Register();
            Hook::Install();
            Hook::InstallTraitTrace();
            spdlog::info("CompletePlanetSurvey initialized");
        }
    }
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    SFSE::Init(a_sfse, { .trampoline = true, .trampolineSize = 128 });
    spdlog::info("{} v{} loading", Plugin::Name, Plugin::Version.string());

    const auto* messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageCallback)) {
        spdlog::critical("Failed to register messaging listener");
        return false;
    }
    return true;
}
