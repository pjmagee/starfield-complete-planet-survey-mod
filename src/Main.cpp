#include "PCH.h"

#include "EsmReader.h"

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>  // GetModuleFileNameW, MAX_PATH (for ConfigureEsmSources)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
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

    // === Character "Statistics" (Data menu) counters (StatEntryCount .. StatNameUniqueCreatures) ===
    // The game keeps Flora/Fauna Fully Scanned + Unique Creatures Scanned in a global
    // "misc stats" table. A natural scan bumps them via the ID_100393 scan-event handler;
    // our ref-free green bypasses that event, so we replicate the increment ourselves.
    // Table layout (RE: re/ghidra/output/misc-stats-increment-2026-07-01.md): base =
    // *StatTableBase, count = *StatEntryCount, stride 0x20, entry+0x00 = interned stat-name
    // ptr, entry+0x10 = int32 value (exactly what the Stats menu ID_88202 displays and the
    // save stores). The Stat*Name globals each hold the interned pointer used as the key.

    // ID_1016657: per-planet survey aggregator constructor.
    //   (buffer, planet_id) — populates buffer with all tracked form IDs for the planet
    //   across four arrays (two uint-arrays for flora/trait ids, two ptr-arrays for resource/other).
    //   Buffer size seen in callers: >= 0x250 bytes. We allocate 0x400 to be safe.
    using fn_aggregator_t = void (*)(void* buffer, std::uint32_t planet_id);
    // ID_65318: cleanup for the aggregator buffer.
    using fn_buffer_free_t = void (*)(void* buffer);

    // ID_97853: survey check-and-dispatch. Called by SetTraitKnown/SetScanned flows after a write.
    //   Signature: (struct*) where the struct starts with { uint32 planet_id, float prev_pct, u8 flag, u8 skip }.
    //   Fires PlayerPlanetSurveyProgressEvent (conditional) and PlayerPlanetSurveyCompleteEvent
    //   if the planet's survey is now 100%. The Complete event is what generates the in-world
    //   "<Planet> Survey Data" slate in the player's inventory.
    using fn_survey_notify_t = void (*)(void* ctx);

    // ID_102650: the engine's ref-free "scan & fully survey a planet" entry point —
    // what a starmap/orbital scan ultimately drives. It resolves the knowledge DB
    // itself, then (via ID_102651) sets the surveyed bit, CREATES the entry if
    // missing (ID_52204), fires the survey-complete event (→ the Survey Data slate
    // reward), and recurses over the planet's moons. Self-contained: no spawn, no
    // teleport, no async two-phase. Args: (unused-context, planetId, fullFlag=1).
    using fn_scan_complete_t = void (*)(std::int64_t context, std::uint32_t planetId, std::uint8_t fullFlag);

    // ID_124901: the engine's species-slot hash (FNV-1a of the 4-byte species id) -> slot index in a
    // subobj's species hashmap. Used to dump the RAW per-species slot bytes so we can DIFF a full
    // scan (green+info+XP) vs a +0x21 byte-poke (half) and find the missing "species catalogued/known"
    // field the real scan writes and we don't.
    using fn_species_slot_hash_t = std::uint64_t (*)(std::uintptr_t hashmap, const void* key4);

    // ID_35755: BSTArray<u32>::push_back grow path — (header{begin,end,cap}, pos, &value). Allocates
    // via the engine allocator and updates the header + frees the old buffer, so the array is
    // engine-OWNED and safe to free on teardown. This is how the real scan fills slot+0x08; we use
    // it to build that array ref-free — the GREEN fix.
    using fn_bstarray_grow_t = std::uint32_t* (*)(std::int64_t* header, std::uint32_t* pos, const std::uint32_t* value);

    // ID_83006: resolve a species base FORM to its CANONICAL form (detailed rationale at
    // CanonicalFormId below — the green outline keys on this canonical id, not the raw ESM id).
    using fn_resolve_canonical_form_t = std::uintptr_t (*)(void* form);

    // ID_883341 (AllFormsMapHolder): the engine's global form registry — a BSTScatterTable<FormID,
    // TESForm*>. It's what TESForm::LookupByID (ID_47401) reads. Starfield does NOT keep planets in
    // TESDataHandler::formArrays (those are empty for galaxy types like PNDT), so this registry is
    // the only place to enumerate all planet forms (see ForEachFormOfType below).

    // ID_93988 (RefreshStarMapPanelData): repopulate the StarMap selected-planet info panel from the
    // knowledge DB — (panel controller, planet id). Full RE notes at the StarMap repaint logic below.
    using fn_refresh_starmap_t = void (*)(void* controller, std::uint32_t planetId);

    // === Address-library ids (single source of truth) ===
    // ONE X-macro table names every address-library id the plugin depends on. The load-time
    // self-check (CheckOffsets), the Relocation-global DECLARATIONS, the resolver (ResolveOffsets)
    // and the hook installers all derive from it, so the probe list, the declarations, the
    // assignments and the logged counts can never drift (static_assert'd below) — and a new global
    // CANNOT be added without appearing in the probe.
    //
    // Why this machinery exists: an unresolved REL::ID routes through CommonLibSF's IDDB → REX::FAIL
    // → MessageBox + TerminateProcess — a hard crash that does NOT throw and cannot be caught. So:
    //  (a) the Relocation globals (generated below) are DEFAULT-constructed (address 0), never
    //      resolved at DLL-load static-init;
    //  (b) CheckOffsets() parses the versionlib file ITSELF and records each id's RVA;
    //  (c) ResolveOffsets() assigns the globals directly from moduleBase + parsedRva — OUR
    //      Relocation globals and hook sites never go through REL::ID / the IDDB.
    // CommonLibSF-INTERNAL calls we make on the enabled path (VM/UI/data-handler singletons,
    // BSFixedString's string pool, TESForm::LookupByID, GameSettingCollection) DO still resolve
    // through the IDDB inside CommonLibSF — those ids are listed as PROBE-ONLY entries so a
    // versionlib that lacks any of them disables the mod up front instead of REX::FAILing at first
    // use. (Real case: BSStringPool::GetEntry id 1186742 exists in the 1.16.244 versionlib but is
    // beyond the END of the 1.16.236/242 tables — a green probe of only our own ids would still
    // have died in the first BSFixedString there.)
    //
    // CPS_RELOC_IDS: ids with a same-named, table-GENERATED REL::Relocation<type> global.
    // CPS_HOOKSITE_IDS: ids resolved ad hoc inside the Hook::Install* trampoline patchers.
    // CPS_PROBEONLY_IDS: CommonLibSF-internal ids — verified present, resolved by CommonLibSF itself.
#define CPS_RELOC_IDS(X)                                            \
    X(GetKnowledgeManager, 126578, fn_get_manager_t)                \
    X(SetTraitKnownNative, 52155, fn_set_trait_known_t)             \
    X(DbLookup, 126806, fn_db_lookup_t)                             \
    X(IncrementScanFlag, 124898, fn_incr_flag_t)                    \
    X(SetPercentByte, 124899, fn_set_percent_t)                     \
    X(TraitDiscriminator, 938333, std::uint16_t*)                   \
    X(StatEntryCount, 889375, std::uint32_t*)                       \
    X(StatTableBase, 889377, std::uintptr_t*)                       \
    X(StatTrackingEnabled, 894532, std::uint8_t*)                   \
    X(StatNameFloraFullyScanned, 923219, std::uintptr_t*)           \
    X(StatNameFaunaFullyScanned, 923220, std::uintptr_t*)           \
    X(StatNameUniqueCreatures, 923223, std::uintptr_t*)             \
    X(SurveyAggregator, 1016657, fn_aggregator_t)                   \
    X(SurveyBufferFree, 65318, fn_buffer_free_t)                    \
    X(SurveyCheckNotify, 97853, fn_survey_notify_t)                 \
    X(ScanCompletePlanet, 102650, fn_scan_complete_t)               \
    X(SpeciesSlotHash, 124901, fn_species_slot_hash_t)              \
    X(BSTArrayU32Grow, 35755, fn_bstarray_grow_t)                   \
    X(ResolveCanonicalForm, 83006, fn_resolve_canonical_form_t)     \
    X(AllFormsMapHolder, 883341, std::uintptr_t*)                   \
    X(RefreshStarMapPanelData, 93988, fn_refresh_starmap_t)

#define CPS_HOOKSITE_IDS(X)               \
    X(ScanHookOuter, 52157)               \
    X(StarMapScanHookOuter, 52173)        \
    X(StarMapRefreshHookOuter, 94011)

    // CommonLibSF-internal ids reachable from OUR enabled-path calls (enumerated from the CommonLibSF
    // source at the pinned submodule commit; re-audit when bumping the submodule):
    //   VirtualMachine::GetSingleton → RE::ID::GameVM::Singleton   (Papyrus::Register, DispatchPapyrusStatic)
    //   RE::UI::GetSingleton / IsMenuOpen                          (both pollers, RefreshStarMapPanelIfOpen)
    //   TESDataHandler::GetSingleton                               (ConfigureEsmSources)
    //   GameSettingCollection::GetSingleton / GetSetting           (ApplyInstantScanGameSettings)
    //   TESForm::LookupByID                                        (native completion paths)
    //   BSStringPool GetEntry / Entry::Release / BucketTable       (every RE::BSFixedString ctor/dtor)
#define CPS_PROBEONLY_IDS(X)                     \
    X(ClSF_GameVM_Singleton, 937585)             \
    X(ClSF_UI_Singleton, 937580)                 \
    X(ClSF_UI_IsMenuOpen, 130475)                \
    X(ClSF_TESDataHandler_Singleton, 937572)     \
    X(ClSF_GameSettings_Singleton, 938225)       \
    X(ClSF_GameSettings_GetSetting, 49324)       \
    X(ClSF_TESForm_LookupByID, 47401)            \
    X(ClSF_StringPool_GetEntry, 1186742)         \
    X(ClSF_StringPool_Release, 139340)           \
    X(ClSF_StringPool_BucketTable, 139337)

#define CPS_CRITICAL_IDS(X3, X2) CPS_RELOC_IDS(X3) CPS_HOOKSITE_IDS(X2) CPS_PROBEONLY_IDS(X2)

    namespace Ids
    {
        // Index of each id inside kCriticalOffsetIds / g_criticalRva (same order as the table).
        enum class Idx : std::size_t
        {
#define CPS_X3(name, id, type) name,
#define CPS_X2(name, id) name,
            CPS_CRITICAL_IDS(CPS_X3, CPS_X2)
#undef CPS_X3
#undef CPS_X2
                kCount
        };
    }  // namespace Ids

    // Every critical id, in table order, for the non-fatal load-time probe (CheckOffsets).
    inline constexpr std::uint64_t kCriticalOffsetIds[] = {
#define CPS_X3(name, id, type) id##ull,
#define CPS_X2(name, id) id##ull,
        CPS_CRITICAL_IDS(CPS_X3, CPS_X2)
#undef CPS_X3
#undef CPS_X2
    };
    inline constexpr std::size_t kCriticalIdCount = std::size(kCriticalOffsetIds);
    inline constexpr std::size_t kRelocIdCount    = [] {
        std::size_t n = 0;
#define CPS_X3(name, id, type) ++n;
        CPS_RELOC_IDS(CPS_X3)
#undef CPS_X3
        return n;
    }();
    static_assert(kCriticalIdCount == static_cast<std::size_t>(Ids::Idx::kCount),
                  "critical-id array out of sync with the Idx enum");

    // The engine-binding Relocation globals, GENERATED from the table (declared lazy / address 0;
    // assigned by ResolveOffsets once the probe has verified + parsed every RVA). Their per-id
    // documentation lives with the type aliases above and at the usage sites.
#define CPS_X3(name, id, type) inline REL::Relocation<type> name;
    CPS_RELOC_IDS(CPS_X3)
#undef CPS_X3

    // Filled by CheckOffsets() on success: the running exe's base address and each critical id's
    // RVA parsed straight from the versionlib (bounds-checked against the module's SizeOfImage).
    // ResolveOffsets()/Hook::Install* read addresses from HERE — never from REL::ID/IDDB — so OUR
    // resolution cannot hit REX::FAIL (no TOCTOU either: the probe's parse IS the resolution source).
    inline std::uintptr_t                              g_moduleBase {0};
    inline std::array<std::uint32_t, kCriticalIdCount> g_criticalRva {};

    // Absolute address of a probed id. Only meaningful after CheckOffsets() returned true.
    inline std::uintptr_t CriticalAddress(Ids::Idx idx)
    {
        return g_moduleBase + g_criticalRva[static_cast<std::size_t>(idx)];
    }

    // Offsets within knowledge-manager / DB structs (Starfield 1.16.236.0–1.16.244.0, Ghidra-derived).
    constexpr std::size_t  kPlanetIdOffset       = 0x54;   // uint32 knowledge key at planetForm+0x54
    constexpr std::size_t  kManagerDbOffset      = 0x8B0;  // knowledge DB ptr at manager+0x8B0 (ID_126578 result)
    constexpr std::size_t  kDbContainerOffset    = 0x268;  // BSTHashMap<> start within the DB object
    constexpr std::size_t  kBucketOffsetTableOff = 0x12;   // uint16[] offset table start within a bucket base
    constexpr std::size_t  kEntrySubobjOffset    = 0x20;   // species subobj relative to the resolved entry ptr
    constexpr std::size_t  kFormPtrFormIdOffset  = 0x28;   // formID field in a TESForm* (aggregator ptr-arrays)

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

    // push_back one u32 onto a species slot's +0x08 BSTArray, matching the engine's inline push_back
    // (BSTArrayU32Grow = ID_35755, the engine grow path — see the type alias docs above).
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

    // Mark one species/resource form scanned on a planet. `cachedSubobj` is the planet's knowledge
    // subobj when the caller already resolved it once (issue #9 — avoid a DbLookup per species);
    // pass nullptr to resolve here (null-checked fallback for callers without a cache).
    int MarkSpeciesScannedForPlanet(std::uint32_t planetId, std::uint32_t speciesFormId, std::uint8_t delta,
                                    std::uint8_t* cachedSubobj = nullptr)
    {
        if (!planetId || !speciesFormId)
            return -1;

        std::uint8_t* subobj = cachedSubobj;
        if (!subobj)
        {
            const auto db = GetKnowledgeDB();
            if (!db)
                return -1;
            subobj = ResolvePlanetSubobj(db, planetId);
        }
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

        // Resolve the planet subobj ONCE for every species/resource write (issue #9). Fallback inside
        // MarkSpeciesScannedForPlanet re-resolves only when this is null (no knowledge entry yet).
        const auto    db           = GetKnowledgeDB();
        std::uint8_t* cachedSubobj = db ? ResolvePlanetSubobj(db, planetId) : nullptr;

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
            if (MarkSpeciesScannedForPlanet(planetId, fid, delta, cachedSubobj) == 1)
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

    // Increment a "Miscellaneous Statistics" counter (the Data-menu Statistics list) by
    // delta, exactly as the scan-event handler (ID_100393) does: find the table entry whose
    // name matches `internedName`, then entry+0x10 += delta. `internedName` is the value of
    // one of the Stat*Name globals. Bounded scan + in-place int add on an already-allocated
    // entry — NOT a BSTArray grow/allocator poke, so the heap-corruption rule doesn't apply.
    // Fully guarded: a null/absurd table just no-ops (never faults the player's game).
    void IncrementMiscStat(std::uintptr_t internedName, int delta)
    {
        if (delta == 0 || internedName == 0)
            return;
        const auto* enabled = StatTrackingEnabled.get();
        if (!enabled || *enabled == 0)  // respect the engine's own "stats enabled" gate
            return;
        const auto* countPtr = StatEntryCount.get();
        const auto* basePtr  = StatTableBase.get();
        if (!countPtr || !basePtr)
            return;
        const std::uint32_t  count = *countPtr;
        const std::uintptr_t base  = *basePtr;
        if (base == 0 || count == 0 || count > 0x10000)  // sane upper bound on the table
            return;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto entry = base + static_cast<std::uintptr_t>(i) * 0x20;
            if (*reinterpret_cast<std::uintptr_t*>(entry) == internedName)
            {
                *reinterpret_cast<std::int32_t*>(entry + 0x10) += delta;  // the displayed/saved value
                return;  // stat names are unique in the table
            }
        }
    }

    // True if `key`'s species slot on this planet's knowledge subobj is ALREADY flagged
    // scanned (slot+0x21 != 0) — i.e. it was fully scanned before we touch it. Lets the
    // caller count ONLY newly-scanned species toward the Statistics counters (a natural
    // scan increments once, on the 0->100% transition). Pure read; guards a missing slot.
    bool IsSpeciesScanned(void* subobj, std::uint32_t key)
    {
        if (!subobj)
            return false;
        const auto base    = reinterpret_cast<std::uintptr_t>(subobj);
        const auto hashmap = base + 0x18;
        const auto hashEnd = *reinterpret_cast<std::uint64_t*>(base + 0x48);
        const auto slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);
        if (!slots)
            return false;
        const auto idx = SpeciesSlotHash(hashmap, &key);
        if (idx == hashEnd)
            return false;  // no slot yet -> not previously scanned
        const auto slotAddr = slots + idx * 0x30;
        return *reinterpret_cast<std::uint8_t*>(slotAddr + 0x21) != 0;
    }

    // True if this planet is ALREADY fully marked complete BY US: its attribute "known" bits are all
    // set AND every authored species is scan-flagged. This mirrors the exact state our completion
    // writes, so it's the idempotency signal for the survey-COMPLETE event. We must NOT re-fire that
    // event for a planet we already completed: the engine's "Planets Fully Surveyed" statistic is NOT
    // deduped — every complete-event fire increments it — so re-running a completion would inflate it
    // without bound (observed 1780 -> 3746). A barren body has no species, so this reduces to the
    // attribute-bit check. Pure reads, all guarded (bounded by the planet's authored species list).
    bool IsPlanetFullyMarked(std::uint32_t planetId)
    {
        const auto db     = GetKnowledgeDB();
        const auto subobj = db ? ResolvePlanetSubobj(db, planetId) : nullptr;
        if (!subobj)
            return false;  // no entry -> never completed
        if ((*reinterpret_cast<std::uint32_t*>(subobj) & 0x7u) != 0x7u)
            return false;  // attribute "known" bits not fully set -> not complete
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(planetId);
        if (it != m.end())
        {
            for (const auto sf : it->second)
            {
                std::uint32_t key = CanonicalFormId(RE::TESForm::LookupByID(sf));
                if (key == 0)
                    key = sf;
                if (!IsSpeciesScanned(subobj, key))
                    return false;  // a species still unscanned -> not complete
            }
        }
        return true;
    }

    int MarkEsmSpeciesForPlanet(std::uint32_t planetId, int kind = 0)
    {
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(planetId);
        if (it == m.end())
            return 0;  // barren / resource-only body — no authored flora/fauna

        // Prior scan-state source for the character Statistics: count ONLY species that
        // transition to fully-scanned now (like a natural scan), so repeated completions
        // don't double-count. Resolve the knowledge subobj ONCE (issue #9) for both the
        // pre-write IsSpeciesScanned reads and the MarkSpeciesScannedForPlanet writes below.
        std::uint8_t* const subobj = [&]() -> std::uint8_t* {
            const auto db = GetKnowledgeDB();
            return db ? ResolvePlanetSubobj(db, planetId) : nullptr;
        }();

        int marked = 0, floraNew = 0, faunaNew = 0;
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
            const bool wasScanned = IsSpeciesScanned(subobj, key);  // capture BEFORE the write
            if (MarkSpeciesScannedForPlanet(planetId, key, kDefaultScanDelta, subobj) == 1)
            {
                ++marked;
                // Newly fully-scanned -> tally by kind for the Statistics counters, matching a
                // natural scan (the ID_100393 handler classifies flora vs fauna by form type).
                if (!wasScanned)
                {
                    if (auto* f = RE::TESForm::LookupByID(speciesFormId))
                    {
                        const auto ft = f->GetFormType();
                        if (ft == RE::FormType::kFLOR)
                            ++floraNew;
                        else if (ft == RE::FormType::kNPC_)
                            ++faunaNew;
                    }
                }
            }
        }

        // Replicate the per-species Data-menu Statistics increments a natural scan makes.
        // Flora/Fauna Fully Scanned by newly-completed species of each kind; Unique Creatures
        // Scanned tracks distinct fauna species (one unique creature per species), so += faunaNew.
        if (floraNew)
            IncrementMiscStat(*StatNameFloraFullyScanned.get(), floraNew);
        if (faunaNew)
        {
            IncrementMiscStat(*StatNameFaunaFullyScanned.get(), faunaNew);
            IncrementMiscStat(*StatNameUniqueCreatures.get(), faunaNew);
        }
        if (floraNew || faunaNew)
            spdlog::debug("MarkEsmSpeciesForPlanet: planet 0x{:08X} stats += flora {}, fauna {} (unique {})",
                          planetId, floraNew, faunaNew, faunaNew);
        return marked;
    }

    // The engine's global form registry — a BSTScatterTable<FormID, TESForm*>.
    // ID_883341 is the global that holds the map pointer; it's what
    // TESForm::LookupByID (ID_47401) reads. Starfield does NOT keep planets in
    // TESDataHandler::formArrays (those are empty for galaxy types like PNDT), so
    // this registry is the only place to enumerate all planet forms. (AllFormsMapHolder = ID_883341.)

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

    // Systemic-failure detector for the sweep's PER-PLANET fault isolation (issue #6): a single
    // faulting planet is caught, logged, skipped and queued as a straggler — it must NOT abort the
    // remaining ~1798 planets. But this many ATTEMPTED planets faulting IN A ROW is the signature
    // of a bad offset (every planet faults identically), so at the cap we abort THIS SWEEP only:
    // hard ERROR, remaining barren bodies counted as not-attempted (surfaced in the log AND the
    // player popup), and a re-run of the command stays viable. We deliberately do NOT latch
    // g_degraded here — that would no-op the very finalize/retry/report natives that surface the
    // failure (the popup would lie with failedCount=0) and make recovery-by-re-run impossible.
    // g_degraded stays reserved for uncaught faults that reach GuardedNative.
    constexpr int kMaxConsecutiveSweepFaults = 5;

    // Per-planet fault lines logged at ERROR before collapsing into one end-of-sweep summary line
    // that lists the remaining formIds (a galaxy run must not dump thousands of lines at release
    // level — but release level is INFO, so the lines must not be DEBUG either or they'd vanish).
    constexpr int kMaxPerPlanetFaultLines = 10;
    // FormIds listed explicitly in that summary line before truncating with "+K more".
    constexpr std::size_t kMaxFaultSummaryIds = 40;

    // Latched true when a bound native catches a fault (a C++ exception, or — because src/ is
    // built /EHa — an access violation), almost always a wrong/garbage offset deref. Once set,
    // the guarded natives short-circuit to safe defaults so the feature disables cleanly instead
    // of re-faulting on the same bad offset every call. Cleared only by a game restart.
    inline std::atomic<bool> g_degraded {false};

    // Form IDs of the planets the last sweep scan-completed. Consumed by the
    // Papyrus trait pass (Planet.GetKeywordTypeList(44) -> MarkTraitKnownForPlanet)
    // — the original mod's proven trait path, which ID_102650 alone doesn't apply
    // to every planet (it skips already-discovered ones).
    inline std::vector<std::uint32_t> g_sweepPlanetForms;
    // Form IDs of the sweep's STRAGGLERS: planets whose survey state could NOT be fully written
    // in-frame (the async ID_102650 entry create hadn't flushed, so the write no-op'd/partially
    // landed — or the per-planet body faulted). This is the EXACT set the Papyrus finalize/retry
    // passes must mop up; planets Phase 1 completed in-frame never appear here, so the mop-up no
    // longer restamps the whole sweep (the issue #6 perf win). FinalizeSweptPlanet REMOVES a
    // planet once its post-write state reads fully marked, so after the bounded retry passes the
    // residue is exactly the never-resolved failures (ReportSweepFailures logs + counts those).
    // Guarded by g_sweepMtx alongside the sweep list.
    inline std::vector<std::uint32_t> g_stragglerPlanetForms;
    // Barren PNDT form IDs collected by the cheap Phase 1 enumerate step (issue #9). The Papyrus
    // drive loop walks this list in chunks via SweepBarrenChunk — NOT the full ~1798 in one frame.
    // Guarded by g_sweepMtx with the other sweep vectors.
    inline std::vector<std::uint32_t> g_barrenWorkForms;
    inline std::mutex                 g_sweepMtx;
    // Barren planets the last sweep did NOT attempt because the consecutive-fault cap aborted it
    // (0 on a healthy run). They are in neither the straggler list nor the failure report — this
    // count is how the abort is surfaced to Papyrus (GetSweepNotAttemptedCount) so the popup can
    // say "sweep aborted early — N worlds not attempted" instead of under-reporting.
    inline std::atomic<int> g_sweepNotAttempted {0};

    // === Issue #13: re-entrant-invocation gate ===
    // g_sweepPlanetForms/g_stragglerPlanetForms/g_barrenWorkForms (above) and g_lifePlanetCache
    // (defined further below, alongside EnumerateLifePlanets) are native-side vectors CONSUMED BY
    // INDEX from Papyrus across MANY frames — the chunked barren sweep yields via Utility.Wait
    // between chunks, and both the barren finalize pass and the life-planet finalize pass yield
    // again. g_sweepMtx keeps each vector's own internal state consistent under concurrent access,
    // but it does NOT stop a second console invocation (or an auto-scan firing mid-run) from
    // CLEARING/REFILLING a vector out from under an active Papyrus loop that is still walking it by
    // index — that reads out-of-range or stale data, a race the mutex alone cannot prevent. Fix: a
    // single "a completion run is active" gate that every long-running/chunked entry point acquires
    // before touching these caches and releases on exit (TryBeginRun/EndRun below; Papyrus-side
    // wrappers in CompletePlanetSurveyQuest.psc).
    //
    // GENERATION TOKEN (PR #25 review — the ABA-steal race): a bare bool gate is unsafe to STEAL.
    // If TryBeginRun presumes a long-held gate abandoned and takes it, but the old run is actually
    // still ALIVE (realistic: the gate is acquired BEFORE CompleteBarrenPlanets' modal intro
    // Message.Show, so a player AFK on that popup for >= the timeout parks a healthy run on the gate
    // indefinitely), the old run's late EndRun would release the NEW owner's gate — and a third
    // invocation could then start and interleave with the new run on the shared index caches:
    // exactly the corruption this gate exists to prevent. Fix: every successful acquire/steal bumps
    // a monotonic GENERATION and returns it to the caller; EndRun(generation) only clears the gate
    // when the caller's generation matches the CURRENT one. A stolen run's late EndRun mismatches
    // (its generation was superseded) and becomes a logged no-op — a steal can never be undone by
    // its victim. This is also what makes acquiring before the modal intro acceptable: the worst
    // case for an AFK-held gate is a WARN-logged steal whose victim self-neutralizes on wake.
    //
    // All gate state below is guarded by g_runGateMtx (plain fields, not atomics): acquire, steal,
    // release, validate and the session-boundary clear each read+write several fields as one unit
    // (active/generation/startTicks/owner), and none of these calls is hot-path (a handful per run,
    // not per frame), so one small mutex is simpler and airtight versus juggling atomic orderings.
    inline std::mutex   g_runGateMtx;
    inline bool         g_runActive = false;   // a completion run currently holds the gate
    inline std::int32_t g_runGeneration = 0;   // monotonic; bumped on EVERY successful acquire/steal
    inline std::int64_t g_runStartTicks = 0;   // steady_clock ticks at the holder's acquire (steal aging)
    inline std::string  g_runOwner;            // holder's run name — reject/steal/mismatch diagnostics
    // Run names come from our own .psc call sites (short literals); bound the copy anyway
    // (crash-safety: never trust an unbounded string on a diagnostics path).
    constexpr std::size_t kMaxRunNameLen = 64;

    // STUCK-GATE FAILSAFE (issue #13): Papyrus has no try/finally, and — worse than a normal early
    // Return, which the *Core-function pattern already handles (control always returns to the gated
    // wrapper's next statement, so EndRun still runs) — it can die mid-run in ways that never
    // execute ANY more of its own code at all: an uncaught VM error, a save-load mid-run, or
    // quit-to-menu (the exact reason InstallSessionReRegisterPoller exists below — a session
    // teardown drops the formless script's VM state outright). If any of those happen while the
    // gate is held, a naive gate latches TRUE forever and bricks every completion command for the
    // rest of the process — "requires a game restart" is explicitly not acceptable here. Two
    // independent, deliberately-redundant failsafes:
    //   1) TIME-BASED THEFT (TryBeginRun, below): a gate held longer than kStuckRunTimeoutSec is
    //      presumed abandoned and stolen, with a WARN so a genuinely-stuck gate is still visible in
    //      the log even though it self-heals. The bound is deliberately generous (minutes, not
    //      seconds): a HEALTHY galaxy sweep can legitimately run for a couple of minutes (Phase 1
    //      alone is ~12 Wait(0.1) hops over ~1798 planets, plus up to 3 finalize Wait(1.0) passes on
    //      top) — a short timeout would steal a healthy run's gate mid-flight. The generation token
    //      above makes even a WRONG steal (victim still alive, e.g. AFK on the intro modal) safe:
    //      the victim's late EndRun mismatches and cannot free the new owner's gate, and its Core
    //      body refuses to proceed past its generation check (see IsRunActive + the fail-closed
    //      checks in the .psc Core functions).
    //   2) SESSION-BOUNDARY CLEAR (Engine::ResetPendingCompletionState, called on the Main-Menu-
    //      opened rising edge): a brand-new game session cannot possibly have a run in flight — the
    //      entire Papyrus VM state that would have been running one (including any in-progress
    //      CompleteBarrenPlanets loop) was just torn down — so clearing the gate there is always
    //      correct, and it fires immediately (no multi-minute wait) for the most common real-world
    //      stuck case: quit-to-menu mid-run.
    //
    // STEAL/CACHE-HYGIENE INVARIANT (PR #25 review): a steal (or any fresh acquire) never consumes a
    // previous run's stale cache indices, because EVERY gated run re-enumerates its work list at run
    // start, BEFORE consuming any index: the barren path's CompleteAllPlanetsSurveyData rebuilds
    // g_barrenWorkForms and resets g_sweepPlanetForms/g_stragglerPlanetForms/fault state; the life
    // path's EnumerateLifePlanets clears and refills g_lifePlanetCache; the CompletePlanet path
    // consumes no index cache at all (per-planet natives only). Any FUTURE gated flow must keep this
    // invariant: enumerate-first, then consume — never consume a list a previous run built.
    constexpr std::int64_t kStuckRunTimeoutSec = 300;  // 5 min — generous vs. a healthy multi-minute galaxy sweep

    // Cross-chunk Phase 1 accumulators (issue #9). The fault streak MUST span chunks — a systemic
    // bad-offset still trips the cap even when faults land in adjacent chunks. Papyrus drive is
    // single-threaded on the main thread; g_sweepMtx still guards vector merges.
    struct SweepPhase1Accum
    {
        int                                      completed         = 0;  // attempted AND succeeded
        int                                      markedTotal       = 0;
        int                                      firedInFrame      = 0;
        int                                      leftForFinalize   = 0;
        int                                      writeNoop         = 0;
        int                                      faulted           = 0;
        int                                      consecutiveFaults = 0;  // spans chunks
        int                                      skipped           = 0;  // over kMaxScansPerRun
        int                                      skippedLiving     = 0;
        int                                      totalPndt         = 0;
        int                                      chunkCount        = 0;
        bool                                     aborted           = false;
        bool                                     summaryLogged     = false;
        std::chrono::steady_clock::time_point    t0 {};
        std::vector<std::uint32_t>               faultedForms;
        // Finalize-pass timing (issue #9): wall ms spent inside FinalizeSweptPlanet across retries.
        std::int64_t                             finalizeMs    = 0;
        int                                      finalizeCalls = 0;
        bool                                     finalizeLogged = false;
    };
    inline SweepPhase1Accum g_phase1;

    // Emit the Phase 1 aggregate INFO line once (end of last chunk or abort). Call with g_sweepMtx held.
    void LogPhase1SummaryLocked()
    {
        if (g_phase1.summaryLogged)
            return;
        g_phase1.summaryLogged = true;

        // Collapse the per-planet fault lines beyond the cap into one bounded summary.
        if (g_phase1.faultedForms.size() > static_cast<std::size_t>(kMaxPerPlanetFaultLines))
        {
            std::string ids;
            const auto  begin = static_cast<std::size_t>(kMaxPerPlanetFaultLines);
            const auto  end   = std::min(g_phase1.faultedForms.size(), begin + kMaxFaultSummaryIds);
            for (std::size_t k = begin; k < end; ++k)
                ids += std::format("{}0x{:08X}", k == begin ? "" : ", ", g_phase1.faultedForms[k]);
            if (end < g_phase1.faultedForms.size())
                ids += std::format(", +{} more", g_phase1.faultedForms.size() - end);
            spdlog::error("CompleteAllPlanetsSurveyData: ...and {} more faulted planets: formIds=[{}]",
                          g_phase1.faultedForms.size() - begin, ids);
        }
        const int notAttempted = g_sweepNotAttempted.load(std::memory_order_relaxed);
        if (g_phase1.aborted)
            spdlog::error("CompleteAllPlanetsSurveyData: sweep ABORTED after {} faults; {} barren planets not attempted — re-run the command to retry them",
                          g_phase1.faulted, notAttempted);

        const auto phase1Ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - g_phase1.t0)
                                  .count();
        spdlog::info("CompleteAllPlanetsSurveyData: Phase 1 swept {} PNDT forms, {} attempted+succeeded ({} fired in-frame, {} left for finalize, {} write no-op), {} living skipped (flora/fauna left for on-planet), {} resource/attribute flags set, {} per-planet faults, {} not attempted{}, {} over cap — {} chunks, {} ms total",
                     g_phase1.totalPndt, g_phase1.completed, g_phase1.firedInFrame, g_phase1.leftForFinalize,
                     g_phase1.writeNoop, g_phase1.skippedLiving, g_phase1.markedTotal, g_phase1.faulted,
                     notAttempted, g_phase1.aborted ? " (SWEEP ABORTED: consecutive-fault cap)" : "",
                     g_phase1.skipped, g_phase1.chunkCount, phase1Ms);
    }

    // Cheap Phase 1 enumerate (issue #9): collect every barren PNDT formId into g_barrenWorkForms,
    // reset sweep/straggler/fault state, start the Phase 1 timer. Does NOT discover/write any planet
    // — that is SweepBarrenChunk's job across frames. Returns the barren work-list size.
    int EnumerateBarrenPlanetsForSweep()
    {
        const auto                 t0 = std::chrono::steady_clock::now();
        std::vector<std::uint32_t> work;
        int                        total         = 0;
        int                        skippedLiving = 0;

        // Parse ESM species map up front (cached). Living worlds are excluded from the work list
        // so chunk processing never has to re-check them.
        const auto& planetSpecies = Esm::GetPlanetSpecies();

        // Enumerate every planet (PNDT) form from the global form registry —
        // TESDataHandler::formArrays[kPNDT] is empty in Starfield (galaxy forms
        // live in the registry / galaxy DB, not the per-type arrays).
        ForEachFormOfType(RE::FormType::kPNDT, [&](RE::TESForm* form)
        {
            ++total;
            if (!form)
                return;
            const auto planetId = ReadPlanetId(form);
            if (!planetId)
                return;
            // Skip any planet that HAS authored flora/fauna. Completing it ref-free would mark its
            // flora/fauna "scanned" while the outline stays BLUE — the engine keys the green on a
            // per-(planet,species) CANONICAL id that only exists once the biome materializes the
            // creature on-planet, which we cannot write from here. That is an invalid state. Living
            // worlds are left for CompleteLifePlanets / on-planet green.
            if (planetSpecies.count(planetId))
            {
                ++skippedLiving;
                return;
            }
            work.push_back(form->GetFormID());
        });

        const int workCount = static_cast<int>(work.size());
        {
            std::lock_guard lock(g_sweepMtx);
            g_barrenWorkForms      = std::move(work);
            g_sweepPlanetForms     = {};
            g_stragglerPlanetForms = {};
            g_phase1               = SweepPhase1Accum {};
            g_phase1.t0            = t0;
            g_phase1.totalPndt     = total;
            g_phase1.skippedLiving = skippedLiving;
            // Empty work list: log the Phase 1 end summary immediately (no chunks will run).
            if (workCount == 0)
                LogPhase1SummaryLocked();
        }
        g_sweepNotAttempted.store(0, std::memory_order_release);

        spdlog::info("CompleteAllPlanetsSurveyData: Phase 1 enumerate: {} barren of {} PNDT ({} living skipped) — work will be chunked across frames",
                     workCount, total, skippedLiving);
        return workCount;
    }

    // Process one chunk of the barren work list (issue #9). Per-planet semantics are EXACTLY the
    // former monolithic Phase 1 body: pre-write guard → discover → write → post-check → conditional
    // event → straggler/fault/notAttempted accounting, with /EHa per-planet fault isolation.
    //
    // Returns:
    //   >= 0  work-list slots advanced this call (Papyrus: startIndex += ret). 0 = nothing left /
    //         empty range / already finished.
    //   -1    consecutive-fault cap aborted the sweep; remaining barren bodies are counted as
    //         notAttempted. Papyrus must break its chunk loop (do not keep calling).
    //
    // Fault-streak semantics: g_phase1.consecutiveFaults is NOT reset between chunks — only a
    // successful attempt (or already-complete clean predicate read) clears it. Abort mid-chunks
    // freezes further writes; remaining work-list entries become notAttempted; Phase 1 summary logs.
    int SweepBarrenChunk(int startIndex, int count, bool writeState)
    {
        if (startIndex < 0 || count <= 0)
            return 0;

        // Local accumulators merged under the lock at the end (and on abort) so engine calls never
        // hold g_sweepMtx. consecutiveFaults / completed / aborted are snapshotted then written back.
        std::vector<std::uint32_t> chunkFids;
        int                        completedBase     = 0;
        int                        faultedBase       = 0;  // so per-planet ERROR numbering stays sweep-global
        int                        consecutiveFaults = 0;
        int                        completed         = 0;
        int                        markedTotal       = 0;
        int                        firedInFrame      = 0;
        int                        leftForFinalize   = 0;
        int                        writeNoop         = 0;
        int                        faulted           = 0;
        int                        skipped           = 0;
        bool                       aborted           = false;
        std::vector<std::uint32_t> sweptForms;
        std::vector<std::uint32_t> stragglerForms;
        std::vector<std::uint32_t> faultedForms;
        {
            std::lock_guard lock(g_sweepMtx);
            if (g_phase1.aborted)
                return -1;
            const int workSize = static_cast<int>(g_barrenWorkForms.size());
            if (startIndex >= workSize)
            {
                LogPhase1SummaryLocked();
                return 0;
            }
            const int end = (std::min)(startIndex + count, workSize);
            chunkFids.assign(g_barrenWorkForms.begin() + startIndex,
                             g_barrenWorkForms.begin() + end);
            ++g_phase1.chunkCount;
            completedBase     = g_phase1.completed;
            consecutiveFaults = g_phase1.consecutiveFaults;
            faultedBase       = g_phase1.faulted;
        }

        const int chunkLen = static_cast<int>(chunkFids.size());
        int       advanced = 0;

        for (int ci = 0; ci < chunkLen; ++ci)
        {
            if (aborted)
                break;
            if (completedBase + completed >= kMaxScansPerRun)
            {
                ++skipped;
                ++advanced;
                continue;
            }

            const auto fid = chunkFids[static_cast<std::size_t>(ci)];
            auto*      form = RE::TESForm::LookupByID(fid);
            const auto planetId = ReadPlanetId(form);
            if (!form || !planetId)
            {
                // Unresolvable form mid-sweep: treat as a soft skip (not a fault streak — no engine
                // offset touch). Still advance the cursor so the list cannot stall.
                ++advanced;
                continue;
            }

            // PER-PLANET fault isolation (issue #6): one faulting planet must not abort the rest of
            // the sweep, and must never latch the global degraded flag on its own (GuardedNative
            // wraps the WHOLE native, so before this a single bad form killed every remaining planet
            // AND every later native this session). A caught fault skips just that planet and queues
            // it as a straggler — the Papyrus finalize/retry passes get another (fault-isolated) shot
            // and report it if it never resolves. Only kMaxConsecutiveSweepFaults ATTEMPTS faulting in
            // a row — the systemic bad-offset signature — abort the sweep (sweep-local; NOT g_degraded,
            // see the constant's comment). The streak SPANS chunks (issue #9).
            try
            {
                // writeState=false (the traits-only path): just RECORD the barren world — do NOT discover or
                // write resources. The Papyrus traits pass marks trait-known (self-sufficient; needs no entry),
                // so "traits" never writes a single resource flag.
                if (writeState)
                {
                    // Idempotency guard (unchanged): skip any planet already fully completed on a PRIOR run,
                    // so we never re-fire the completion event (the engine's "Planets Fully Surveyed" stat is
                    // NOT deduped — every fire increments it). Evaluated ONCE here on the PRE-write state, so
                    // writing the state below does NOT make the guard see this planet as "already complete":
                    // a planet that passes the guard ALWAYS reaches step 3's fire decision. This successful
                    // engine read also ends any fault streak — the streak measures consecutive FAILING
                    // engine access, and this planet's predicate just evaluated cleanly.
                    if (IsPlanetFullyMarked(planetId))
                    {
                        consecutiveFaults = 0;
                        ++advanced;
                        continue;
                    }
                    // ORDERING GUARANTEE (issue #8): the completion event fires LAST, only after this planet's
                    // survey state is fully written. Discover/create must still come FIRST (the writes need the
                    // entry to exist), so an abort between steps 1 and 3 can leave a planet discovered-but-
                    // partially-written — that partial state is exactly what the Papyrus FinalizeSweptPlanet
                    // pass (and any re-run: the guard above sees it as incomplete) mops up. What can NOT happen
                    // any more is "completion event fired / slate dropped but data unwritten".
                    //  1) ScanCompletePlanet (ID_102650) CREATES the per-planet knowledge entry (ID_52204).
                    //     REQUIRED first: ResolvePlanetSubobj is a pure lookup, so the writes at step 2 silently
                    //     no-op until the entry exists. Its internal survey-notify (ID_97853) fires here at the
                    //     PRE-write ~0% survey, so it does NOT complete/reward. The create and that internal
                    //     notify are welded inside one opaque engine call and cannot be separated (decompile:
                    //     re/ghidra/output/scan-complete.txt, ID_102651 — ID_52204 @ +0x108, ID_97853 @ +0x241).
                    ScanCompletePlanet(0, planetId, 1);
                    //  2) WRITE the ref-free survey state (attribute bits + resource scan flags) BEFORE the
                    //     completion event. With no flora/fauna on these bodies this reaches a genuine 100%.
                    const int written = WritePlanetSurveyState(planetId, kDefaultScanDelta);
                    markedTotal += written;
                    //  3) FIRE the completion event LAST — and ONLY if the write actually landed in full
                    //     (post-write re-check). When the entry create was async and step 2 no-op'd, firing
                    //     ID_97853 on a <100% planet would be pointless engine-call volume at galaxy scale, so
                    //     the planet is RECORDED as a straggler (the exact set, not just a count — issue #6)
                    //     for the Papyrus FinalizeSweptPlanet retry passes, which complete it a beat later
                    //     once the deferred create has flushed.
                    if (IsPlanetFullyMarked(planetId))
                    {
                        NotifySurveyProgress(planetId);
                        ++firedInFrame;
                    }
                    else
                    {
                        ++leftForFinalize;
                        stragglerForms.push_back(fid);
                        if (written == 0)
                            ++writeNoop;
                    }
                }
                sweptForms.push_back(fid);
                ++completed;
                consecutiveFaults = 0;
                ++advanced;
            }
            catch (...)
            {
                ++faulted;
                ++consecutiveFaults;
                // Degrade-and-continue: queue the planet as BOTH swept (the trait pass still covers
                // it) and straggler (the finalize retry re-attempts the write — fault-isolated there
                // too — and ReportSweepFailures names it if it never lands). Log volume is bounded:
                // the first kMaxPerPlanetFaultLines fault at ERROR, the rest collapse into ONE
                // summary ERROR after the sweep (release level is INFO, so DEBUG would hide them).
                stragglerForms.push_back(fid);
                sweptForms.push_back(fid);
                faultedForms.push_back(fid);
                const int faultNum = faultedBase + faulted;
                if (faultNum <= kMaxPerPlanetFaultLines)
                    spdlog::error("CompleteAllPlanetsSurveyData: caught fault completing planetId=0x{:08X} — planet skipped, queued as straggler (fault #{} this sweep)",
                                  planetId, faultNum);
                ++advanced;
                if (consecutiveFaults >= kMaxConsecutiveSweepFaults)
                {
                    aborted = true;
                    spdlog::error("CompleteAllPlanetsSurveyData: {} consecutive per-planet faults — systemic failure (bad offset?); aborting THIS sweep (mod stays active; re-run the command to retry)",
                                  consecutiveFaults);
                }
            }
        }

        // Merge chunk results into global sweep state. On abort, every work-list entry AFTER the
        // last advanced index becomes notAttempted (including later chunks never started).
        int notAttemptedAdd = 0;
        {
            std::lock_guard lock(g_sweepMtx);
            g_sweepPlanetForms.insert(g_sweepPlanetForms.end(), sweptForms.begin(), sweptForms.end());
            g_stragglerPlanetForms.insert(g_stragglerPlanetForms.end(), stragglerForms.begin(), stragglerForms.end());
            g_phase1.completed += completed;
            g_phase1.markedTotal += markedTotal;
            g_phase1.firedInFrame += firedInFrame;
            g_phase1.leftForFinalize += leftForFinalize;
            g_phase1.writeNoop += writeNoop;
            g_phase1.faulted += faulted;
            g_phase1.consecutiveFaults = consecutiveFaults;
            g_phase1.skipped += skipped;
            g_phase1.faultedForms.insert(g_phase1.faultedForms.end(), faultedForms.begin(), faultedForms.end());

            if (aborted)
            {
                g_phase1.aborted = true;
                // startIndex + advanced is the first NOT-yet-consumed index; everything from there
                // to the end of the work list was never attempted.
                const int firstNotAttempted = startIndex + advanced;
                const int ws                = static_cast<int>(g_barrenWorkForms.size());
                if (firstNotAttempted < ws)
                    notAttemptedAdd = ws - firstNotAttempted;
                g_sweepNotAttempted.store(notAttemptedAdd, std::memory_order_release);
                LogPhase1SummaryLocked();
            }
            else if (startIndex + advanced >= static_cast<int>(g_barrenWorkForms.size()))
            {
                LogPhase1SummaryLocked();
            }
            else
            {
                spdlog::debug("SweepBarrenChunk: advanced {} (start={} count={}) completed+={} faults+={} streak={}",
                              advanced, startIndex, count, completed, faulted, consecutiveFaults);
            }
        }

        if (aborted)
            return -1;
        return advanced;
    }

    // Attempted+succeeded count from the last (possibly still-running) Phase 1 — for Papyrus logs
    // that previously used CompleteAllPlanetsSurveyData's return value as "scanned".
    int GetSweepCompletedCount()
    {
        std::lock_guard lock(g_sweepMtx);
        return g_phase1.completed;
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

    // === Galaxy-map ("star map") scan → complete-that-planet ===
    // Set by the star-map scan hook (Hook::StarMapScanHook) when the player scans a body on the
    // galaxy map. g_galaxyScanPlanetFormId holds that body's PNDT FormID; g_pendingGalaxyScan
    // flags a dispatch. The poller picks it up and calls Papyrus _GalaxyMapScanComplete, which
    // reads the id via GetGalaxyScanPlanetFormId and completes that planet (if the toggle is on).
    // Unlike the on-surface path this is fully ref-free (no PlaceAtMe), so it does NOT wait for a
    // menu to close — the star map updates live — just a short grace out of the hook's call frame.
    inline std::atomic<bool>          g_pendingGalaxyScan {false};
    inline std::atomic<std::uint32_t> g_galaxyScanPlanetFormId {0};
    inline int                        g_galaxyScanCountdown {0};

    // === StarMap info-panel repaint after a galaxy-map completion ===
    // The star map caches the selected-planet panel (SURVEY %/RESOURCES/TRAITS) when it repaints
    // right after the scan — which is BEFORE our ~0.5s-later completion — so it only re-reads on a
    // manual deselect/reselect. ID_93988 is the engine's own "repopulate that panel from the
    // knowledge DB" routine (StarMapMenu*). We capture the live StarMap menu pointer from the scan
    // handler's own ID_93988 call (StarMapRefreshCaptureHook) and re-invoke ID_93988 on it once our
    // completion has finished, so the panel repaints to 100% in place.
    //   g_starMapMenu        — the panel CONTROLLER (param_1 of ID_94011→ID_93988) seen at the last
    //                          natural refresh. NOT the IMenu — see RefreshStarMapPanelIfOpen.
    //   g_starMapPanelPlanet — the planet id (2nd arg of ID_93988) shown in that panel.
    //   g_pendingStarMapRefresh — set by _GalaxyMapScanComplete when done; poller repaints next frame.
    inline std::atomic<void*>         g_starMapMenu {nullptr};
    inline std::atomic<std::uint32_t> g_starMapPanelPlanet {0};
    inline std::atomic<bool>          g_pendingStarMapRefresh {false};

    // Clear all our process-global "pending" state. Called on the exit-to-menu boundary: these flags
    // and captured pointers are process-resident (they outlive a game session), so a scan captured
    // just before quitting to the Main Menu must NOT dispatch a completion into the NEXT game — that
    // would write a stale planet's survey state into the new session. Also drops the captured StarMap
    // menu pointer (a prior session's menu is freed). Countdowns are poller-owned and self-reset once
    // their flags are false. (Does NOT touch the engine's own knowledge DB — that's the game's to
    // reset; we only clear OUR queue.)
    //
    // Issue #13 stuck-gate failsafe #2 (see the g_runActive comment): also unconditionally clears
    // the re-entrancy gate here. A brand-new session cannot have a live completion run — the entire
    // Papyrus VM state that would have been running one was just torn down by the session boundary —
    // so this is always safe, and it is the FAST path for the most common real-world stuck-gate cause
    // (quit-to-menu mid-run): it fires the moment the Main Menu opens, no multi-minute timeout wait.
    // The generation is bumped too, so a late EndRun/IsRunActive from the dead run (should be
    // impossible after teardown — belt and braces) mismatches instead of touching the next session.
    inline void ResetPendingCompletionState()
    {
        {
            std::lock_guard lock(g_runGateMtx);
            if (g_runActive)
            {
                spdlog::warn("Session boundary (Main Menu): completion run '{}' (generation {}) still held the "
                             "gate — cleared (the prior session's Papyrus run was torn down mid-run, e.g. "
                             "quit-to-menu)",
                             g_runOwner, g_runGeneration);
                g_runActive = false;
                ++g_runGeneration;
                g_runOwner.clear();
            }
        }
        g_pendingCompleteSurvey.store(false, std::memory_order_release);
        g_pendingGalaxyScan.store(false, std::memory_order_release);
        g_galaxyScanPlanetFormId.store(0, std::memory_order_release);
        g_pendingStarMapRefresh.store(false, std::memory_order_release);
        g_starMapMenu.store(nullptr, std::memory_order_release);
        g_starMapPanelPlanet.store(0, std::memory_order_release);
    }

    // ID_93988: repopulate the StarMap selected-planet info panel from the knowledge DB. It takes TWO
    // args — the panel CONTROLLER and the PLANET ID (proven by ID_93960 calling `ID_93988(controller,
    // planetId)`, and internally `ID_94888(buf, planetId)`→`ID_94906(db, buf)` reads the DB for that
    // planet). Passing the wrong planetId (or omitting it) populates the panel for a garbage planet →
    // it renders EMPTY. (RE 2026-07-11: re/ghidra/output/starmap-{refresh,select-refresh}-decomp.)
    // (RefreshStarMapPanelData = ID_93988, declared via the id table above.)

    // Repaint the StarMap selected-planet panel (ID_93988) after our completion, so it shows 100% in
    // place. ID_93988's arg is the star map's internal panel CONTROLLER (param_1 of ID_94011), NOT the
    // IMenu — so we must call it on the pointer StarMapRefreshCaptureHook stashed (the exact object
    // the game itself passes), never on a menu found by name (an IMenu has a different layout — doing
    // that reads controller offsets off the wrong object and corrupts memory → crash). The captured
    // controller lives exactly as long as the star-map IMenu is open, so we GATE on that: only repaint
    // if a live menu whose menuName contains "starmap" is present in RE::UI's menuArray/menuStack. If
    // the map has closed, the controller may be freed → skip. Fault-guarded (the poller caller is NOT
    // a GuardedNative). MUST run on the main thread. Returns true if it repainted.
    bool RefreshStarMapPanelIfOpen()
    {
        try
        {
            void* const         controller = g_starMapMenu.exchange(nullptr, std::memory_order_acq_rel);
            const std::uint32_t planetId   = g_starMapPanelPlanet.load(std::memory_order_acquire);
            if (!controller || !planetId)
                return false;  // capture hook never fired (scan routed via ID_94004) — nothing to repaint
            auto* ui = RE::UI::GetSingleton();
            if (!ui)
                return false;

            // Liveness gate: is the star-map menu still open? (Only touch the controller if so.)
            bool starMapOpen = false;
            auto check       = [&](const auto& container) {
                for (std::uint32_t i = 0; i < container.size() && !starMapOpen; ++i)
                {
                    auto* m = container[i].get();
                    if (!m)
                        continue;
                    const char* nm = m->menuName.c_str();
                    if (!nm)
                        continue;
                    std::string low = nm;
                    for (auto& ch : low)
                        if (ch >= 'A' && ch <= 'Z')
                            ch = static_cast<char>(ch + 32);
                    if (low.find("starmap") != std::string::npos)
                        starMapOpen = true;
                }
            };
            check(ui->menuArray);
            if (!starMapOpen)
                check(ui->menuStack);
            if (!starMapOpen)
                return false;  // star map closed — controller may be gone; do not deref it

            // ID_93988(controller, planetId): the exact call the game makes to populate this panel.
            RefreshStarMapPanelData(controller, planetId);
            spdlog::info("RefreshStarMapPanel: repainted panel for planetId=0x{:08X}", planetId);
            return true;
        }
        catch (...)
        {
            spdlog::error("RefreshStarMapPanelIfOpen: caught fault — skipping panel repaint");
            return false;
        }
    }

    // Set true ONCE at kPostDataLoad, only after CheckOffsets() has proven every critical id
    // resolves against the on-disk address library. While false (address library missing / stale for
    // the running runtime — the routine post-patch "SFSE ships before versionlib" case) the feature
    // is disabled: NOTHING is bound or installed (natives, hooks, pollers, ESM, GMSTs — even
    // Papyrus::Register is skipped, because CommonLibSF's VM/UI singletons resolve via REL::ID and
    // would REX::FAIL on the failed versionlib), and no address is ever resolved. The gate in
    // GuardedNative and the hook thunks is defense-in-depth on top of that. Cleared only by restart.
    inline std::atomic<bool> g_offsetsValid {false};

    // One-shot flag for the "native call ignored" WARN in GuardedNative — shared across ALL natives
    // so the disabled/degraded state logs exactly once per process, not once per native.
    inline std::once_flag g_disabledNoticeOnce;

    // Per-HOOK install state (issue #12) — distinct from g_offsetsValid, which gates the WHOLE
    // plugin. A sig-scan miss (FindCallSite finds no matching CALL — e.g. a game patch reordered the
    // compiler's output) only takes out ONE feature path; everything else (console commands, the
    // other hook, natives) keeps working. Set true ONLY by a successful Hook::Install*() trampoline
    // write; Papyrus reads these via IsHandScannerHookInstalled/IsOrbitalScannerHookInstalled so the
    // Settings-toggle handlers (CompleteSurveyIfEnabled / _GalaxyMapScanComplete) can no-op sanely
    // instead of assuming the native hook that is SUPPOSED to invoke them is actually armed.
    inline std::atomic<bool> g_handScannerHookInstalled {false};    // ScanHook       (0x80C toggle)
    inline std::atomic<bool> g_orbitalScannerHookInstalled {false}; // StarMapScanHook (0x80D toggle)

    // === Load-time offset self-check (non-fatal address-library probe) ==========================
    //
    // CommonLibSF resolves a REL::ID through IDDB, which calls REX::FAIL — a MessageBox +
    // TerminateProcess — the instant an id is missing or the versionlib's game version doesn't match
    // the running exe. REX::FAIL does NOT throw, so it can't be caught; the game just dies. After a
    // Starfield patch this is routine: SFSE updates first and the Address Library lags a few days, so
    // the versionlib .bin for the new runtime is absent and every player crashes on launch.
    //
    // To degrade gracefully we validate every id OURSELVES, reading the same versionlib file IDDB
    // would load but never going through REX::FAIL. Starfield's versionlib is format 5 (verified
    // against 1.16.236/242/244): a fixed 96-byte header followed by a little-endian u32[] indexed by
    // address-library id; entry 0 means "no offset for this id in this build".
    //   byte 0  : u32 format (== 5)
    //   byte 4  : u32 gameVersion[4]   (major, minor, patch, build)
    //   byte 20 : char name[64]        ("Starfield.exe")
    //   byte 84 : i32 pointerSize
    //   byte 88 : i32 dataFormat
    //   byte 92 : i32 offsetCount
    //   byte 96 : u32 offset[offsetCount]
    // The header's gameVersion[4] sits at byte 4 for formats 1/2 as well, so the version check is
    // format-independent; only the per-id offset table is format-5-specific.

    // Directory of THIS plugin DLL (…/Data/SFSE/Plugins) — where the versionlib lives, exactly as
    // CommonLibSF's IDDB locates it. Returns empty on failure (→ feature disabled). No engine derefs.
    inline std::filesystem::path GetPluginDirectory()
    {
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&GetPluginDirectory), &self) ||
            !self)
            return {};
        wchar_t buf[MAX_PATH] {};
        const auto n = GetModuleFileNameW(self, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return std::filesystem::path {buf}.parent_path();
    }

    // Why the probe failed — enumerated so the single ERROR line says exactly what is wrong
    // (a wrong-version file renamed to the runtime name reads very differently from a missing one).
    enum class ProbeResult
    {
        kOk,
        kFileMissing,        // no versionlib-<runtime>.bin next to the plugin (the patch-lag case)
        kBadHeader,          // unreadable / truncated / implausible header
        kVersionMismatch,    // header's game version != the running exe's version
        kUnsupportedFormat,  // header format != 5 — we can't verify ids, so FAIL CLOSED
        kIdMissing,          // an id is past the table or has a zero offset entry
    };

    inline const char* ProbeResultName(ProbeResult r)
    {
        switch (r)
        {
            case ProbeResult::kOk:                return "ok";
            case ProbeResult::kFileMissing:       return "version library file missing";
            case ProbeResult::kBadHeader:         return "version library header unreadable";
            case ProbeResult::kVersionMismatch:   return "version library is for a different game version";
            case ProbeResult::kUnsupportedFormat: return "version library format unsupported";
            case ProbeResult::kIdMissing:         return "id missing from the version library";
        }
        return "unknown";
    }

    // Parse the versionlib at `path`, verify it matches `exeVer`, and read the RVA of every critical
    // id into `rvasOut` (same order as kCriticalOffsetIds). Pure file I/O — never touches the engine,
    // never calls REX::FAIL. ANY problem fails closed: format != 5 is a failure too (we cannot verify
    // ids in a format we can't decode; trusting it would reintroduce the exact REX::FAIL crash this
    // probe exists to prevent). On kIdMissing, `missingId` receives the offending id. The parsed RVAs
    // double as the RESOLUTION source (ResolveOffsets), so there is no probe-then-resolve TOCTOU.
    inline ProbeResult ProbeVersionLib(const std::filesystem::path& path, const REL::Version& exeVer,
                                       std::array<std::uint32_t, kCriticalIdCount>& rvasOut,
                                       std::uint64_t& missingId, std::uint32_t& formatOut)
    {
        missingId = 0;
        formatOut = 0;
        rvasOut.fill(0);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return ProbeResult::kFileMissing;

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return ProbeResult::kBadHeader;

        std::uint32_t format  = 0;
        std::uint32_t gv[4]   = {0, 0, 0, 0};
        in.read(reinterpret_cast<char*>(&format), sizeof format);
        in.read(reinterpret_cast<char*>(gv), sizeof gv);
        if (!in)
            return ProbeResult::kBadHeader;
        formatOut = format;

        // The gameVersion[4] dwords sit at byte 4 for formats 1/2/5 alike, so check version first —
        // a mismatch is the more precise diagnosis even when the format is also unexpected.
        if (gv[0] != exeVer[0] || gv[1] != exeVer[1] || gv[2] != exeVer[2] || gv[3] != exeVer[3])
            return ProbeResult::kVersionMismatch;

        // FAIL CLOSED on any format we can't decode. Every shipped Starfield versionlib is format 5;
        // if that ever changes, this branch disables the mod (one clear ERROR) instead of letting an
        // unverified REL::ID resolve later and REX::FAIL-kill the game.
        if (format != 5)
            return ProbeResult::kUnsupportedFormat;

        char         name[64] {};
        std::int32_t ptrSize = 0, dataFormat = 0, count = 0;
        in.read(name, sizeof name);
        in.read(reinterpret_cast<char*>(&ptrSize), sizeof ptrSize);
        in.read(reinterpret_cast<char*>(&dataFormat), sizeof dataFormat);
        in.read(reinterpret_cast<char*>(&count), sizeof count);
        if (!in || count <= 0 || static_cast<std::uint64_t>(count) > 0x0800'0000ull)  // sane upper bound
            return ProbeResult::kBadHeader;

        const auto               entryCount = static_cast<std::uint64_t>(count);
        constexpr std::streamoff kDataStart = 96;  // == sizeof(HEADER_V5)
        for (std::size_t k = 0; k < kCriticalIdCount; ++k)
        {
            const auto id = kCriticalOffsetIds[k];
            if (id >= entryCount)
            {
                missingId = id;
                return ProbeResult::kIdMissing;
            }
            in.seekg(kDataStart + static_cast<std::streamoff>(id) * 4, std::ios::beg);
            std::uint32_t offset = 0;
            in.read(reinterpret_cast<char*>(&offset), sizeof offset);
            if (!in || offset == 0)
            {
                missingId = id;
                return ProbeResult::kIdMissing;
            }
            rvasOut[k] = offset;
        }
        return ProbeResult::kOk;
    }

    // Show a non-fatal, NON-BLOCKING notice so a player who never reads the SFSE log still learns why
    // the mod is inert (detached thread: the game's load thread is not held up; the game keeps
    // running). This is the only honest player-visible channel when disabled — the Papyrus/UI path
    // needs engine singletons that resolve via REL::ID, which is exactly what we can't touch.
    inline void ShowDisabledNotice(const std::string& text)
    {
        try
        {
            std::thread([text] {
                MessageBoxA(nullptr, text.c_str(), "Complete Planet Survey", MB_OK | MB_ICONWARNING);
            }).detach();
        }
        catch (const std::exception& e)
        {
            // Worst case the notice doesn't show; the ERROR log line still exists.
            spdlog::warn("ShowDisabledNotice: could not spawn the notice thread ({})", e.what());
        }
        catch (...)
        {
            spdlog::warn("ShowDisabledNotice: could not spawn the notice thread (unknown error)");
        }
    }

    // Log the single disable ERROR line AND show the player notice with the SAME reason text, then
    // return false — used by EVERY disable path in CheckOffsets so no failure is log-only.
    inline bool DisableWithReason(const std::string& runtime, const std::string& reason)
    {
        spdlog::error("CompletePlanetSurvey disabled: {} (runtime {})", reason, runtime);
        ShowDisabledNotice(std::format(
            "Complete Planet Survey is DISABLED for this game version ({}).\n\n"
            "Reason: {}.\n\n"
            "The game will run normally; the mod does nothing until an updated "
            "Address Library (version library) for this runtime is installed.",
            runtime.empty() ? "unknown" : runtime, reason));
        return false;
    }

    // Sig-scan-miss notice (issue #12) — SCOPED to one hook, unlike DisableWithReason above which
    // disables the whole plugin. FindCallSite missing its target CALL means a game/compiler build
    // reordered the outer function's code so the byte scan no longer finds it (e.g. after a Starfield
    // patch); everything else the plugin does (console commands, natives, the other hook) is
    // unaffected, so we do NOT touch g_offsetsValid/g_degraded here — only the caller marks its own
    // g_*HookInstalled flag false (the default) and the affected Settings toggle becomes an inert
    // no-op until a future build's byte pattern matches again. The caller's ERROR line names each
    // miss individually; the player-visible notice shows AT MOST ONCE per process (the once_flag) —
    // if BOTH scan hooks miss (the likely case when the game rebuilds, since they share the same
    // notify target), one MessageBox naming the first miss and pointing at the log beats stacking
    // two — so a player who never opens the SFSE log still learns why the toggles do nothing.
    inline std::once_flag g_hookMissingNoticeOnce;

    inline void NotifyHookMissing(const std::string& featureName)
    {
        std::call_once(g_hookMissingNoticeOnce, [&featureName] {
            ShowDisabledNotice(std::format(
                "Complete Planet Survey: the {} scan-hook could not be installed on this game build "
                "(signature scan miss).\n\n"
                "Auto-complete-on-scan for that feature is disabled until an updated build restores it "
                "(any other scan-hook failures are listed in the SFSE log). "
                "Everything else — including the console completion commands — still works normally.",
                featureName));
        });
    }

    // SizeOfImage of the module at `base`, read from its in-memory PE headers (all guarded).
    // Returns 0 on any inconsistency.
    inline std::uint32_t ModuleSizeOfImage(std::uintptr_t base)
    {
        if (!base)
            return 0;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
            return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        return nt->OptionalHeader.SizeOfImage;
    }

    // Non-fatal load-time gate. True IFF every critical id resolves against the on-disk versionlib
    // for the running runtime; on success it also stashes moduleBase + each id's RVA (the resolution
    // source for ResolveOffsets / Hook::Install*). On failure it logs ONE clear ERROR line naming the
    // cause and returns false; the caller then binds nothing and never resolves an id, so REX::FAIL
    // can never fire. The exe version is read via REL::GetFileVersion (returns nullopt on failure) —
    // NOT REX::FModule::GetFileVersion(), which dereferences that optional unchecked (UB, uncatchable).
    inline bool CheckOffsets()
    {
        std::string runtime;  // best-known runtime version string, for the disable messages
        try
        {
            // Running exe path + version, all failure-checked (no REX helpers that FAIL or deref
            // an empty optional).
            wchar_t exeBuf[MAX_PATH] {};
            const auto exeLen = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
            if (exeLen == 0 || exeLen >= MAX_PATH)
                return DisableWithReason(runtime, "could not determine the game executable path");
            const auto exeVerOpt = REL::GetFileVersion(std::wstring_view {exeBuf, exeLen});
            if (!exeVerOpt)
                return DisableWithReason(runtime, "could not read the game executable's version info");
            const auto exeVer = *exeVerOpt;
            runtime           = exeVer.string();

            const auto dir = GetPluginDirectory();
            if (dir.empty())
                return DisableWithReason(runtime, "could not locate the plugin directory to verify the address library");
            const auto file = dir / (L"versionlib-" + exeVer.wstring(L"-") + L".bin");

            std::array<std::uint32_t, kCriticalIdCount> rvas {};
            std::uint64_t missingId = 0;
            std::uint32_t format    = 0;
            const auto    result    = ProbeVersionLib(file, exeVer, rvas, missingId, format);
            if (result != ProbeResult::kOk)
            {
                std::string detail = ProbeResultName(result);
                if (result == ProbeResult::kIdMissing)
                    detail += std::format(" (id {})", missingId);
                else if (result == ProbeResult::kUnsupportedFormat)
                    detail += std::format(" (format {})", format);
                return DisableWithReason(runtime,
                                         std::format("address library has no offsets for this runtime — waiting for an "
                                                     "updated version library [{}: {}]",
                                                     file.filename().string(), detail));
            }

            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!base)
                return DisableWithReason(runtime, "could not determine the game module base address");

            // Sanity: every parsed RVA must fall inside the loaded module image. A corrupt/mismatched
            // versionlib could otherwise hand us addresses outside Starfield.exe entirely.
            const auto imageSize = ModuleSizeOfImage(base);
            if (imageSize == 0)
                return DisableWithReason(runtime, "could not read the game module's PE headers");
            for (std::size_t k = 0; k < kCriticalIdCount; ++k)
            {
                if (rvas[k] >= imageSize)
                    return DisableWithReason(runtime,
                                             std::format("version library entry for id {} points outside the game "
                                                         "module (rva 0x{:X} >= image size 0x{:X}) — corrupt file?",
                                                         kCriticalOffsetIds[k], rvas[k], imageSize));
            }

            g_moduleBase  = base;
            g_criticalRva = rvas;
            spdlog::info("CheckOffsets: all {} critical address-library ids resolve for runtime {} ({})",
                         kCriticalIdCount, runtime, file.filename().string());
            return true;
        }
        catch (const std::exception& e)
        {
            return DisableWithReason(runtime, std::format("address-library self-check failed ({})", e.what()));
        }
        catch (...)
        {
            return DisableWithReason(runtime, "address-library self-check failed (unknown)");
        }
    }

    // Bind every Relocation global directly from moduleBase + the RVA the probe parsed. Called ONCE
    // at kPostDataLoad, ONLY after CheckOffsets() returned true. OUR globals never go through
    // REL::ID / the IDDB, so THIS resolution cannot reach REX::FAIL (no TOCTOU: the probe's parse is
    // the source). CommonLibSF-internal ids still resolve via IDDB inside CommonLibSF — that set is
    // covered by the CPS_PROBEONLY_IDS probe entries. The assignment list is GENERATED from the same
    // CPS_RELOC_IDS table as the declarations and the probe list, so it cannot drift.
    inline void ResolveOffsets()
    {
#define CPS_X3(name, id, type) name = CriticalAddress(Ids::Idx::name);
        CPS_RELOC_IDS(CPS_X3)
#undef CPS_X3
        spdlog::info("ResolveOffsets: bound {} address-library relocations (direct RVA, no IDDB)", kRelocIdCount);
    }

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
    // Every bound native is registered through GuardedNative<>::call (see the CPS_GUARDED /
    // CPS_GUARDED_PURE macros used in Register) rather than bound directly. This traps any C++
    // exception OR — because src/ is compiled /EHa — any structured fault such as an access
    // violation, logs it, and returns a safe default. An uncaught fault crossing into the Papyrus
    // VM's C call frame is undefined behaviour / a silent CTD with no log line. After the first
    // fault we latch Engine::g_degraded so later natives bail to defaults instead of re-faulting
    // on a bad offset.
    //
    // RequiresEngine=false (CPS_GUARDED_PURE) marks natives that are PURE — string parsing /
    // logging only, no engine pointers, no offsets — so they stay live in a DEGRADED session
    // (g_degraded latched after a caught fault). Gating those would make the console UX lie
    // (e.g. CategoriesValid returning false for a valid "all" reads as "unknown category").
    // Note the offsets-invalid state can't reach any native in practice: when CheckOffsets fails,
    // Papyrus::Register is never called, so nothing is bound — the g_offsetsValid check below is
    // defense-in-depth only.
    template <class T, T fn, bool RequiresEngine = true>
    struct GuardedNative;

    template <class Ret, class... Args, Ret (*fn)(std::monostate, Args...), bool RequiresEngine>
    struct GuardedNative<Ret (*)(std::monostate, Args...), fn, RequiresEngine>
    {
        static Ret call(std::monostate self, Args... args)
        {
            // Feature-disabled (no valid offsets for this runtime) OR latched-degraded (a prior
            // fault): no-op to a neutral default without touching a single engine pointer. Logged
            // ONCE per process (not per call — a galaxy sweep would spam thousands of lines).
            if constexpr (RequiresEngine)
            {
                if (!Engine::g_offsetsValid.load(std::memory_order_acquire) ||
                    Engine::g_degraded.load(std::memory_order_acquire))
                {
                    std::call_once(Engine::g_disabledNoticeOnce, [] {
                        spdlog::warn("[native] call ignored — CompletePlanetSurvey is {} (further ignored calls are not logged)",
                                     Engine::g_offsetsValid.load(std::memory_order_acquire)
                                         ? "degraded after a caught fault"
                                         : "disabled (address library offsets unavailable)");
                    });
                    if constexpr (!std::is_void_v<Ret>)
                        return Ret {};
                    else
                        return;
                }
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
#define CPS_GUARDED_PURE(FN) (&GuardedNative<decltype(&FN), &FN, false>::call)

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

    // ERROR-level sibling of DebugLog (issue #12 review): Papyrus has no log-level primitive of its
    // own, so genuine script-side failures (e.g. ResolveEsmForm's missing / wrong-type pinned form)
    // route here and land at spdlog ERROR — greppable as [E] and never dropped by a level cap —
    // instead of INFO lines that merely contain the word "ERROR". PURE like DebugLog: it must stay
    // live in a degraded session precisely because it reports failures.
    void DebugLogError(std::monostate, RE::BSFixedString msg)
    {
        spdlog::error("[papyrus] {}", msg.c_str());
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
        // Starfield.esm (no game, no visiting, no live instance), so fauna greens remotely. Fauna resolves
        // its temperament X via NPC_->OBTS->temperament OMOD->NKEY->FLST 0x00160C97; flora gets a 4-marker
        // skeleton with per-species reproduction (PRPS) + the >=3-biome 5th marker. Unknown forms come back
        // empty -> left blue. A species is recovered (not left silently blue) when its slot doesn't resolve
        // or the derivation returns nothing; each per-species outcome logs at INFO.
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
                Engine::MarkSpeciesScannedForPlanet(planetId, key, Engine::kDefaultScanDelta, subobj);
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
        // Fire the survey recompute (ID_97853) now the planet's +0x21/+0x08 state is written, so its
        // survey %, the star map and the Survey Data slate update — but ONLY if we actually (re)built a
        // slot this call. When nothing changed (all species already green, built == 0), re-firing the
        // complete event would inflate the game's Planets Fully Surveyed stat (it is not deduped).
        if (built > 0)
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

    // Issue #13 — re-entrancy gate acquire. Every long-running/chunked completion entry point (the
    // Papyrus wrappers around CompletePlanet/CompleteBarrenPlanets/CompleteLifePlanets/
    // CompleteAllPlanets, and the two scan-hook auto-complete dispatch targets) must acquire this
    // BEFORE touching the cross-frame sweep/life caches, and release it via EndRun on every exit.
    // asRunName names the command/path for diagnostics AND is recorded as the gate OWNER, so
    // reject/steal/mismatch logs can say who holds it. The gate is a single process-wide flag, not
    // per-name reentrant (a nested acquire under the SAME logical run must go through an ungated
    // *Core function instead — see CompleteAllPlanets calling _CompleteBarrenPlanetsCore/
    // _CompleteLifePlanetsCore directly).
    // Returns the run's GENERATION token (> 0) on success — the caller must hold it and pass it to
    // EndRun/IsRunActive — or 0 = REJECTED (another run is active and not stale; the caller must
    // bail out WITHOUT touching any cache). See the Engine::g_runActive block for why the
    // generation exists (the ABA-steal race) and the steal/cache-hygiene invariants.
    std::int32_t TryBeginRun(std::monostate, RE::BSFixedString runName)
    {
        const auto nowTicks = std::chrono::steady_clock::now().time_since_epoch().count();
        const char* nameRaw = runName.c_str();
        std::string name(nameRaw ? nameRaw : "?");
        if (name.size() > Engine::kMaxRunNameLen)
            name.resize(Engine::kMaxRunNameLen);

        std::lock_guard lock(Engine::g_runGateMtx);

        // Fresh acquire — bump the generation (never hand out 0/negative: 0 is the reject value).
        auto acquire = [&](const char* how) -> std::int32_t {
            if (++Engine::g_runGeneration <= 0)
                Engine::g_runGeneration = 1;  // int32 wrap paranoia — unreachable in practice
            Engine::g_runActive     = true;
            Engine::g_runStartTicks = nowTicks;
            Engine::g_runOwner      = name;
            spdlog::info("TryBeginRun: '{}' {} the completion-run gate (generation {})",
                         name, how, Engine::g_runGeneration);
            return Engine::g_runGeneration;
        };

        if (!Engine::g_runActive)
            return acquire("acquired");

        const auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::duration(nowTicks - Engine::g_runStartTicks))
                                     .count();

        // Already held — stuck-gate failsafe #1 (see the Engine::g_runActive block): a gate held
        // longer than kStuckRunTimeoutSec is presumed abandoned (a Papyrus run that died without
        // reaching its EndRun) rather than a genuinely long-running galaxy sweep, and we steal it
        // rather than bricking every completion command for the rest of the session. The generation
        // bump inside acquire() is what makes this safe even when the presumption is WRONG (the old
        // run was alive, e.g. AFK on the intro modal): its late EndRun/IsRunActive now mismatch.
        if (elapsedSec >= Engine::kStuckRunTimeoutSec)
        {
            spdlog::warn("TryBeginRun: '{}' STEALING the gate from '{}' (held {}s, older than the {}s "
                         "stuck-run bound) — presumed abandoned by a dead Papyrus run; if that run is "
                         "actually alive, its EndRun will generation-mismatch and be ignored",
                         name, Engine::g_runOwner, elapsedSec, Engine::kStuckRunTimeoutSec);
            return acquire("STOLE");
        }

        spdlog::info("TryBeginRun: '{}' REJECTED — '{}' has held the completion-run gate for {}s",
                     name, Engine::g_runOwner, elapsedSec);
        return 0;
    }

    // Release the re-entrancy gate — but ONLY if the caller's generation is still the current one
    // (PR #25 review: without this, a stolen-from run waking up late would release the NEW owner's
    // gate — the ABA race). A mismatch (gate stolen, or cleared by the session boundary) is a
    // logged WARN no-op naming owner vs caller. MUST be CPS_GUARDED_PURE (see Register): if a
    // native fault degrades the session mid-run, a RequiresEngine=true gate would no-op THIS call
    // too and leave the gate stuck for the rest of the session — defeating the whole point of a
    // release-on-every-exit-path design.
    void EndRun(std::monostate, std::int32_t generation, RE::BSFixedString runName)
    {
        const char* nameRaw = runName.c_str();
        std::string name(nameRaw ? nameRaw : "?");
        if (name.size() > Engine::kMaxRunNameLen)
            name.resize(Engine::kMaxRunNameLen);

        std::lock_guard lock(Engine::g_runGateMtx);
        if (!Engine::g_runActive)
        {
            spdlog::warn("EndRun: '{}' (generation {}) called but no run is active — gate already cleared "
                         "(session boundary, or this run's gate was stolen and the thief finished)",
                         name, generation);
            return;
        }
        if (generation != Engine::g_runGeneration)
        {
            spdlog::warn("EndRun: '{}' (generation {}) does NOT match the current holder '{}' (generation "
                         "{}) — this run's gate was stolen; NOT releasing the new owner's gate",
                         name, generation, Engine::g_runOwner, Engine::g_runGeneration);
            return;
        }
        Engine::g_runActive = false;
        Engine::g_runOwner.clear();
        spdlog::info("EndRun: '{}' released the completion-run gate (generation {})", name, generation);
    }

    // True while the gate is held by exactly this generation. The ungated *Core functions call this
    // at entry as a FAIL-CLOSED guard (PR #25 review): the Cores are global Papyrus functions and
    // therefore console-reachable via cgf, which would bypass the wrappers' gate — a Core must
    // refuse to run unless its caller actually holds the CURRENT gate. Also turns a stolen-from run
    // that wakes mid-body into a no-op if it re-checks. PURE — reads only our own gate state.
    bool IsRunActive(std::monostate, std::int32_t generation)
    {
        std::lock_guard lock(Engine::g_runGateMtx);
        return Engine::g_runActive && generation == Engine::g_runGeneration;
    }

    // Issue #12: whether the on-surface hand-scanner call-site hook (ScanHook, ID_52157 → ID_97853)
    // is actually armed this session. False when Hook::Install()'s sig-scan missed its target CALL (a
    // future game build reordered the outer function) or faulted — in which case the native hook that
    // is SUPPOSED to invoke Papyrus CompleteSurveyIfEnabled never fires at all, so this exists purely
    // as defense-in-depth / an honest status query (e.g. a future direct invocation of
    // CompleteSurveyIfEnabled without the hook armed) rather than something the normal play loop needs.
    bool IsHandScannerHookInstalled(std::monostate)
    {
        return Engine::g_handScannerHookInstalled.load(std::memory_order_acquire);
    }

    // Same as above for the star-map ("Orbital Scanner") call-site hook (StarMapScanHook, ID_52173 →
    // ID_97853), which drives Papyrus _GalaxyMapScanComplete.
    bool IsOrbitalScannerHookInstalled(std::monostate)
    {
        return Engine::g_orbitalScannerHookInstalled.load(std::memory_order_acquire);
    }

    // Return the FormID of the planet/moon the player last scanned on the star map (captured by the
    // galaxy-map scan hook). Papyrus _GalaxyMapScanComplete reads this to know which body to complete.
    // Returns 0 when nothing is pending or the scanned target wasn't a planet.
    std::int32_t GetGalaxyScanPlanetFormId(std::monostate)
    {
        return static_cast<std::int32_t>(Engine::g_galaxyScanPlanetFormId.load(std::memory_order_acquire));
    }

    // Queue a StarMap info-panel repaint. _GalaxyMapScanComplete calls this once it has completed the
    // scanned planet; the poller does the actual ID_93988 repaint next frame on the MAIN thread (UI
    // calls must not run on the Papyrus VM thread) so the panel shows 100% without a manual reselect.
    void QueueStarMapRefresh(std::monostate)
    {
        Engine::g_pendingStarMapRefresh.store(true, std::memory_order_release);
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
        // A planet with no prior knowledge entry is one we're scanning for the first time — bump the
        // "Planets Scanned" Statistic once (completing anything on a planet implies it was scanned).
        // Probe BEFORE the discover creates the entry; idempotent (the entry persists on re-run).
        // Skip the engine discover (which fires the survey-complete event) if we've already fully
        // completed this planet — re-firing would inflate the game's Planets Fully Surveyed stat.
        // Planets Scanned is NOT counted here: it's reconciled to >= Planets Fully Surveyed at command
        // end in Papyrus (Game.IncrementStat, see _ReconcilePlanetsScanned). Per-planet counting here
        // mis-counted moons (ScanCompletePlanet pre-creates their entries) and never covered the current
        // world, which CompletePlanet surveys without ever discovering.
        if (!Engine::IsPlanetFullyMarked(planetId))
            Engine::ScanCompletePlanet(0, planetId, 1);
        spdlog::debug("DiscoverPlanetEntry: planet=0x{:08X} planetId=0x{:08X}",
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
        // touched species when it should not"). Write is idempotent; the survey-complete event fires
        // ONLY on first completion — re-firing on an already-complete planet inflates the game's
        // Planets Fully Surveyed stat (it is not deduped).
        const bool wasComplete = Engine::IsPlanetFullyMarked(planetId);
        const auto n           = Engine::WritePlanetSurveyState(planetId, d, /*includeSpecies=*/false);
        if (!wasComplete)
            Engine::NotifySurveyProgress(planetId);
        spdlog::debug("MarkResourcesForPlanet: planet=0x{:08X} planetId=0x{:08X} delta={} -> marked={}{}",
                     planetForm->GetFormID(), planetId, d, n, wasComplete ? " (already complete, no re-fire)" : "");
        return n;
    }

    // Phase 1 ENUMERATE (issue #9): collect every barren PNDT formId into the native work list and
    // reset sweep/straggler/fault state. Does NOT discover or write any planet — the Papyrus drive
    // loop then calls SweepBarrenChunk across frames so the ~1798-planet work does not hitch one
    // frame. abWriteResources is accepted for call-site compatibility (chunks receive the flag).
    // Returns the barren work-list size (0 = nothing to do).
    std::int32_t CompleteAllPlanetsSurveyData(std::monostate, bool /*writeResources*/)
    {
        return Engine::EnumerateBarrenPlanetsForSweep();
    }

    // Phase 1 CHUNK (issue #9): process [aiStartIndex, aiStartIndex+aiCount) of the barren work list
    // with the same per-planet semantics as the former monolithic sweep (guard → discover → write →
    // post-check → conditional event → straggler/fault accounting). Fault streak SPANS chunks.
    // Returns slots advanced (>=0) for the Papyrus cursor, or -1 if the consecutive-fault cap
    // aborted (remaining planets already counted as notAttempted — break the loop).
    std::int32_t SweepBarrenChunk(std::monostate, std::int32_t startIndex, std::int32_t count, bool writeResources)
    {
        return Engine::SweepBarrenChunk(startIndex, count, writeResources);
    }

    // Attempted+succeeded count from the last Phase 1 (post-chunk). Replaces the old
    // CompleteAllPlanetsSurveyData return value for Papyrus "scanned" log lines.
    std::int32_t GetSweepCompletedCount(std::monostate)
    {
        return Engine::GetSweepCompletedCount();
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

    // Re-apply the attribute "known" bits + resource/species scan flags for one STRAGGLER planet
    // (the exact set Phase 1 recorded — issue #6), and fire its completion event iff the write just
    // brought it to fully-marked. The C++ sweep creates each planet's knowledge entry via ID_102650
    // ASYNCHRONOUSLY, so some entries aren't ready when Phase 1 writes in the same frame
    // (ResolvePlanetSubobj returns null -> the write no-op'd). The Papyrus retry passes call this per
    // straggler across later frames/seconds, by which point the deferred creates have flushed.
    // Transaction ordering (issue #8): write-before-event here too — the event fires ONLY on the
    // not-complete -> complete transition this call ("Planets Fully Surveyed" is NOT deduped, and
    // firing while the entry still isn't ready would be a pointless engine call; the retry pass that
    // finally lands the write fires it). Only barren bodies (no flora/fauna) reach this pass — the
    // sweep skips living worlds — so the default includeSpecies restamp marks no flora/fauna here.
    // Returns 1 when the planet reads fully marked after this call (also REMOVES it from the native
    // straggler list, so later retry passes skip it), 0 when it is still unresolved (entry not ready
    // — retry) or the form/planet didn't resolve. Fault-isolated: a caught fault returns 0 (the
    // planet stays queued for retry/report) instead of latching the global degraded flag — the
    // bounded Papyrus passes + ReportSweepFailures are the failure path; GuardedNative stays the
    // outer boundary for everything else.
    //
    // On the dropped "restamp every swept planet" safety net (review question): no net is lost.
    // The pre-#21 sweep had the SAME already-complete early-return before sweptForms.push_back, so
    // previously-complete planets were never in the finalize list either; and for planets Phase 1
    // completed in-frame the old restamp re-ran the identical WritePlanetSurveyState that had just
    // succeeded (their entry provably resolved — the post-write predicate requires it), so it could
    // never write anything new. A planet whose engine-% could disagree is exactly one failing
    // IsPlanetFullyMarked — which is in the straggler set and still gets restamped here.
    std::int32_t FinalizeSweptPlanet(std::monostate, std::int32_t formId)
    {
        const auto t0 = std::chrono::steady_clock::now();
        try
        {
            auto* form = RE::TESForm::LookupByID(static_cast<std::uint32_t>(formId));
            if (!form)
                return 0;
            const auto planetId = Engine::ReadPlanetId(form);
            if (!planetId)
                return 0;
            // `wasComplete` is captured BEFORE the write so the restamp can't hide a straggler from
            // its own completion event; `nowComplete` is the post-write re-check (same predicate
            // Phase 1 gates its in-frame fire on).
            const bool wasComplete = Engine::IsPlanetFullyMarked(planetId);
            // A straggler whose Phase 1 body faulted BEFORE its ScanCompletePlanet call has NO
            // knowledge entry at all — the restamp below would silently no-op through every retry
            // pass (unrecoverable this run). If the entry is still missing, run the same engine
            // discover/create Phase 1 uses (idempotent; its internal survey-notify fires at the
            // pre-write ~0% state so it cannot complete/reward — see the Phase 1 step-1 comment).
            // Write-before-event ordering is preserved: create -> write -> (maybe) event.
            if (!wasComplete)
            {
                const auto db = Engine::GetKnowledgeDB();
                if (!db || !Engine::ResolvePlanetSubobj(db, planetId))
                    Engine::ScanCompletePlanet(0, planetId, 1);
            }
            const auto marked      = Engine::WritePlanetSurveyState(planetId);
            const bool nowComplete = wasComplete || Engine::IsPlanetFullyMarked(planetId);
            // Bookkeeping BEFORE the notify: NotifySurveyProgress is the faultable engine call, and
            // if it threw after the state write we'd leave an already-COMPLETED planet queued as a
            // straggler (harmless restamps, but the failure report would name a completed planet).
            if (nowComplete)
            {
                std::lock_guard lock(Engine::g_sweepMtx);
                auto&           v = Engine::g_stragglerPlanetForms;
                v.erase(std::remove(v.begin(), v.end(), static_cast<std::uint32_t>(formId)), v.end());
            }
            if (!wasComplete && nowComplete)
                Engine::NotifySurveyProgress(planetId);
            {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
                std::lock_guard lock(Engine::g_sweepMtx);
                Engine::g_phase1.finalizeMs += ms;
                ++Engine::g_phase1.finalizeCalls;
            }
            spdlog::debug("FinalizeSweptPlanet: planetId=0x{:08X} wasComplete={} marked={} nowComplete={} fired={}",
                          planetId, wasComplete, marked, nowComplete, !wasComplete && nowComplete);
            return nowComplete ? 1 : 0;
        }
        catch (...)
        {
            {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
                std::lock_guard lock(Engine::g_sweepMtx);
                Engine::g_phase1.finalizeMs += ms;
                ++Engine::g_phase1.finalizeCalls;
            }
            spdlog::error("FinalizeSweptPlanet: caught fault finalizing formId=0x{:08X} — left unresolved for retry/report",
                          static_cast<std::uint32_t>(formId));
            return 0;
        }
    }

    // Accessors over the sweep's STRAGGLER set — the exact planets Phase 1 could not fully write
    // in-frame (async entry create pending, or the per-planet body faulted). Mirrors the
    // GetSweepPlanetCount/GetSweepPlanetFormIdAt pattern (same mutex). FinalizeSweptPlanet removes
    // a planet once it resolves, so the list SHRINKS across the Papyrus retry passes — iterate it
    // backwards (removal keeps earlier indices valid).
    std::int32_t GetStragglerCount(std::monostate)
    {
        std::lock_guard lock(Engine::g_sweepMtx);
        return static_cast<std::int32_t>(Engine::g_stragglerPlanetForms.size());
    }

    std::int32_t GetStragglerFormIdAt(std::monostate, std::int32_t index)
    {
        std::lock_guard lock(Engine::g_sweepMtx);
        if (index < 0 || static_cast<std::size_t>(index) >= Engine::g_stragglerPlanetForms.size())
            return 0;
        return static_cast<std::int32_t>(Engine::g_stragglerPlanetForms[index]);
    }

    // Barren planets the last sweep never attempted because the consecutive-fault cap aborted it
    // (0 on a healthy run). Surfaced separately from the straggler/failure counts — these planets
    // are in neither — so the popup can say "sweep aborted early: N worlds not attempted" instead
    // of under-reporting exactly when things went worst.
    std::int32_t GetSweepNotAttemptedCount(std::monostate)
    {
        return Engine::g_sweepNotAttempted.load(std::memory_order_acquire);
    }

    // Post-retry failure report (issue #6): walk the RESIDUAL straggler set (everything the bounded
    // finalize passes could not resolve) and count planets whose survey state still reads incomplete.
    // abLogErrors=true logs each at ERROR — formId (hex), planetId and the editor id when the engine
    // still has one (display names aren't reliably reachable for PNDT via CommonLibSF, and this is a
    // log line, not UI) — so "which planets failed" never hides behind an aggregate count. The caller
    // surfaces the returned count in the player-facing result popup. Pass abLogErrors=false to
    // re-read the count without duplicating the ERROR lines (the combined CompleteAllPlanets popup).
    // Bounded by the straggler list; every per-planet probe is fault-isolated.
    std::int32_t ReportSweepFailures(std::monostate, bool logErrors)
    {
        std::vector<std::uint32_t> residue;
        {
            std::lock_guard lock(Engine::g_sweepMtx);
            residue = Engine::g_stragglerPlanetForms;
        }
        int failures = 0;
        for (const auto fid : residue)
        {
            try
            {
                auto*      form     = RE::TESForm::LookupByID(fid);
                const auto planetId = Engine::ReadPlanetId(form);
                if (planetId && Engine::IsPlanetFullyMarked(planetId))
                    continue;  // resolved between the last retry pass and this report
                ++failures;
                if (logErrors)
                {
                    // Name the failure MODE, not just the planet — the four cases point at very
                    // different problems (missing form vs id read vs create-never-flushed vs a
                    // partial write with the entry present).
                    const char* reason = "knowledge entry exists but its survey state is still incomplete (partial write)";
                    if (!form)
                        reason = "form did not resolve (LookupByID returned null)";
                    else if (!planetId)
                        reason = "planet id did not resolve (planetForm+0x54 read 0)";
                    else
                    {
                        const auto db = Engine::GetKnowledgeDB();
                        if (!db || !Engine::ResolvePlanetSubobj(db, planetId))
                            reason = "knowledge entry was never created (async create never flushed)";
                    }
                    const char* edid = form ? form->GetFormEditorID() : nullptr;
                    spdlog::error("Sweep straggler UNRESOLVED after retries: formId=0x{:08X} planetId=0x{:08X} name='{}' — {}; this planet's survey stays incomplete this run (re-run the command, or scan it in-game)",
                                  fid, planetId, (edid && *edid) ? edid : "<unknown>", reason);
                }
            }
            catch (...)
            {
                ++failures;
                if (logErrors)
                    spdlog::error("Sweep straggler UNRESOLVED after retries: formId=0x{:08X} — faulted while probing its state", fid);
            }
        }
        // Finalize-pass timing (issue #9): once per resources-path ReportSweepFailures(true) so the
        // SFSE log shows mop-up compute cost separate from Phase 1 chunk total (excludes Papyrus Wait).
        if (logErrors)
        {
            std::lock_guard lock(Engine::g_sweepMtx);
            if (!Engine::g_phase1.finalizeLogged)
            {
                Engine::g_phase1.finalizeLogged = true;
                spdlog::info("CompleteAllPlanetsSurveyData: finalize pass {} calls, {} ms (FinalizeSweptPlanet compute; excludes Utility.Wait)",
                             Engine::g_phase1.finalizeCalls, Engine::g_phase1.finalizeMs);
            }
        }
        return failures;
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

        // DebugLog / DebugLogError / CategoryEnabled / CategoriesValid are PURE (string parse /
        // logging, no engine pointers) — bound un-gated so a DEGRADED session (fault latch) still
        // parses categories and reports failures honestly. (When offsets are invalid, Register is
        // never called at all — nothing is bound.)
        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "DebugLog"sv, CPS_GUARDED_PURE(DebugLog), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "DebugLogError"sv, CPS_GUARDED_PURE(DebugLogError), std::optional<bool> {true}, false);

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
            "CompletePlanetSurveyNative"sv, "CategoryEnabled"sv, CPS_GUARDED_PURE(CategoryEnabled),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CategoriesValid"sv, CPS_GUARDED_PURE(CategoriesValid),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "QueueCompleteSurvey"sv, CPS_GUARDED(QueueCompleteSurvey), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CancelPendingAutoComplete"sv, CPS_GUARDED(CancelPendingAutoComplete), std::optional<bool> {true}, false);

        // PURE (issue #13): TryBeginRun/EndRun/IsRunActive only touch our own gate state — no engine
        // pointers — and EndRun in particular MUST stay callable in a DEGRADED session (see its
        // comment) or a fault mid-run would leave the re-entrancy gate stuck for the whole session.
        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "TryBeginRun"sv, CPS_GUARDED_PURE(TryBeginRun), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "EndRun"sv, CPS_GUARDED_PURE(EndRun), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "IsRunActive"sv, CPS_GUARDED_PURE(IsRunActive), std::optional<bool> {true}, false);

        // PURE (issue #12 review): these only read our own atomics — no engine pointers — and they
        // report hook status, so they must answer honestly even in a DEGRADED session. CPS_GUARDED
        // would return false when g_degraded latches, which Papyrus would mis-attribute to a
        // sig-scan miss.
        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "IsHandScannerHookInstalled"sv, CPS_GUARDED_PURE(IsHandScannerHookInstalled), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "IsOrbitalScannerHookInstalled"sv, CPS_GUARDED_PURE(IsOrbitalScannerHookInstalled), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetGalaxyScanPlanetFormId"sv, CPS_GUARDED(GetGalaxyScanPlanetFormId), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "QueueStarMapRefresh"sv, CPS_GUARDED(QueueStarMapRefresh), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CompleteAllPlanetsSurveyData"sv, CPS_GUARDED(CompleteAllPlanetsSurveyData),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "SweepBarrenChunk"sv, CPS_GUARDED(SweepBarrenChunk),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetSweepCompletedCount"sv, CPS_GUARDED(GetSweepCompletedCount),
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

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetStragglerCount"sv, CPS_GUARDED(GetStragglerCount),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetStragglerFormIdAt"sv, CPS_GUARDED(GetStragglerFormIdAt),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "ReportSweepFailures"sv, CPS_GUARDED(ReportSweepFailures),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetSweepNotAttemptedCount"sv, CPS_GUARDED(GetSweepNotAttemptedCount),
            std::optional<bool> {true}, false);

        spdlog::info("Bound Papyrus natives: DebugLog, DebugLogError, MarkTraitKnownForPlanet, TestDirectGreen, TestBuildArray, "
                     "MarkResourcesForPlanet, DiscoverPlanetEntry, EnumerateLifePlanets, GetLifePlanetFormIdAt, "
                     "CategoryEnabled, CategoriesValid, QueueCompleteSurvey, CancelPendingAutoComplete, "
                     "TryBeginRun, EndRun, IsRunActive, "
                     "IsHandScannerHookInstalled, IsOrbitalScannerHookInstalled, "
                     "GetGalaxyScanPlanetFormId, QueueStarMapRefresh, "
                     "CompleteAllPlanetsSurveyData, SweepBarrenChunk, GetSweepCompletedCount, "
                     "GetSweepPlanetCount, GetSweepPlanetFormIdAt, FinalizeSweptPlanet, "
                     "GetStragglerCount, GetStragglerFormIdAt, ReportSweepFailures, GetSweepNotAttemptedCount");
    }

#undef CPS_GUARDED
#undef CPS_GUARDED_PURE
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
            if (!Engine::g_offsetsValid.load(std::memory_order_acquire))
                return;  // feature disabled (bad offsets) — pass through, queue nothing
            DispatchPapyrusStatic("CompleteSurveyIfEnabled");
        }

        static inline fn_t func = nullptr;
    };

    // Star-map ("galaxy map") planet scan → complete-that-planet.
    //
    // Every survey mutation in the game converges on ID_97853 (survey check/notify). Pressing SCAN
    // on the star map / in-space scanner runs ID_94004 / ID_94011, which call ID_52173(planetId,
    // scanLevel, 0) — the "scan level increased" survey writer. ID_52173 stamps SurveyChangeReason
    // == 0xc (ScanLevelChanged) when its char arg is 0 (0xf only on the debug arg==1 path), sets
    // ctx+0x00 = planetId, then calls ID_97853(&ctx). The ctx is {u32 planetId@0x00, f32 pct@0x04,
    // u8 SurveyChangeReason@0x08, u8 flag@0x09}; planetId == *(u32)(planetForm+0x54) == PNDT FormID
    // (Game.GetForm resolves it). So hooking the ID_97853 CALL SITE inside ID_52173 and filtering
    // reason==0xc catches exactly a deliberate space scan and gives us the scanned body's id.
    // (Verified offline in the local Ghidra project 2026-07-11: xrefs → ID_94004/ID_94011 call
    // ID_52173; ID_52173 decomp shows reason 0xc + planetId@ctx+0. NOT ID_52153/InitialScan — that
    // one is driven only by ID_52152/ID_102651, never the star-map Scan. See
    // re/ghidra/output/starmap-scan-{decomp,xrefs}-2026-07-11.txt.)
    constexpr std::size_t  kSurveyCtxReasonOffset       = 0x08;  // SurveyChangeReason byte in the ID_97853 ctx
    constexpr std::uint8_t kSurveyReasonScanLevelChanged = 0xc;  // SurveyChangeReason::ScanLevelChanged (the space scan)
    // ID_52173 → ID_97853 call is at outer+0x144 — inside the default 0x400 window, but name it.
    constexpr std::size_t  kStarMapScanSearchWindow      = 0x200;

    // Intercept the survey-notify (ID_97853) inside the scan-level writer (ID_52173) — the engine's
    // "player scanned a planet from space" path (the star-map Scan button / in-space scanner). On a
    // ScanLevelChanged we capture the scanned body's planet id from the ctx and flag a deferred
    // galaxy-map completion (dispatched by the poller → Papyrus _GalaxyMapScanComplete, which honours
    // the "Enable Galaxy Map Scan" toggle). The thunk only touches the ctx pointer + two atomics —
    // no engine derefs beyond ctx — so it's safe on the UI/engine thread that drives the scan.
    struct StarMapScanHook
    {
        using fn_t = void (*)(void*);  // ID_97853: void(u32* ctx)

        static void thunk(void* ctx)
        {
            func(ctx);  // call original SurveyCheckNotify (ID_97853) first
            if (!ctx || !Engine::g_offsetsValid.load(std::memory_order_acquire))
                return;  // no ctx, or feature disabled (bad offsets) — pass through, capture nothing
            const auto* c        = reinterpret_cast<const std::uint8_t*>(ctx);
            const auto  planetId = *reinterpret_cast<const std::uint32_t*>(c);
            const auto  reason   = *(c + kSurveyCtxReasonOffset);
            spdlog::debug("StarMapScanHook: reason={} planetId=0x{:08X}", reason, planetId);
            if (reason == kSurveyReasonScanLevelChanged && planetId != 0)
            {
                Engine::g_galaxyScanPlanetFormId.store(planetId, std::memory_order_release);
                Engine::g_pendingGalaxyScan.store(true, std::memory_order_release);
                // INFO (not debug): a scan-level change is an infrequent, deliberate player action,
                // and this line confirms the galaxy-map hook fired in a release build. Not per-frame spam.
                spdlog::info("StarMapScanHook: captured space scan (ScanLevelChanged) planetId=0x{:08X} — galaxy-map completion queued", planetId);
            }
        }

        static inline fn_t func = nullptr;
    };

    // Capture the live StarMap panel state whenever the info panel naturally repaints — i.e. the scan
    // handler ID_94011's own call to ID_93988(controller, planetId) (the panel-populate routine). We
    // stash BOTH args (the panel controller and the displayed planet id) so RefreshStarMapPanelIfOpen
    // can re-issue the identical call after our deferred completion, repainting the panel to 100% in
    // place. Pure capture + pass-through — touches only the args, so it's safe on the UI thread.
    struct StarMapRefreshCaptureHook
    {
        using fn_t = void (*)(void*, std::uint32_t);  // ID_93988(controller, planetId)

        static void thunk(void* controller, std::uint32_t planetId)
        {
            Engine::g_starMapMenu.store(controller, std::memory_order_release);
            Engine::g_starMapPanelPlanet.store(planetId, std::memory_order_release);
            func(controller, planetId);  // call original panel-populate (ID_93988)
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
        // Whole body fault-guarded (/EHa): a bad probe RVA would make the FindCallSite byte scan read
        // unmapped memory, and the caller (MessageCallback) is noexcept — catch, log, and skip the
        // hook (auto-complete-on-scan degrades; nothing is half-armed because ScanHook::func is only
        // set by a successful write_call).
        try
        {
            // Addresses come from the verified probe table (CriticalAddress), NOT REL::ID — a REL::ID
            // on a stale versionlib would REX::FAIL (TerminateProcess). Same for the installers below.
            const auto outer = Engine::CriticalAddress(Engine::Ids::Idx::ScanHookOuter);      // ID_52157 planet-progress updater
            const auto inner = Engine::CriticalAddress(Engine::Ids::Idx::SurveyCheckNotify);  // ID_97853 survey check/notify

            const auto call_site = FindCallSite(outer, inner);
            if (!call_site)
            {
                // Sig-scan miss (issue #12): log + a player-visible notice, SCOPED to this one hook —
                // g_handScannerHookInstalled stays false (its default), CompleteSurveyIfEnabled reads
                // that and no-ops sanely instead of silently doing nothing with no signal anywhere.
                spdlog::error("ScanHook: CALL to ID_97853 not found inside ID_52157 — hook skipped, hand-scanner auto-complete disabled");
                Engine::NotifyHookMissing("Hand Scanner");
                return;
            }

            ScanHook::func = reinterpret_cast<ScanHook::fn_t>(
                REL::GetTrampoline().write_call<5>(call_site, reinterpret_cast<std::uintptr_t>(ScanHook::thunk)));
            Engine::g_handScannerHookInstalled.store(true, std::memory_order_release);

            spdlog::info("ScanHook: installed at call-site 0x{:016X} (ID_52157 → ID_97853)", call_site);
        }
        catch (...)
        {
            spdlog::error("ScanHook: caught fault during install — hand-scanner auto-complete disabled");
            Engine::NotifyHookMissing("Hand Scanner");
        }
    }

    // Install the star-map scan hook: patch the ID_97853 CALL inside ID_52173 (the scan-level
    // writer that ID_94004/ID_94011 drive when you press Scan on the star map) so scanning a planet
    // from the galaxy map triggers our completion. Separate call site from ScanHook's (which lives in
    // ID_52157, the on-surface path), so the two coexist.
    void InstallStarMapScanHook()
    {
        try  // fault-guarded like Install() — log + skip, never fault the noexcept message callback
        {
            const auto outer = Engine::CriticalAddress(Engine::Ids::Idx::StarMapScanHookOuter);  // ID_52173 scan-level survey writer (space scan)
            const auto inner = Engine::CriticalAddress(Engine::Ids::Idx::SurveyCheckNotify);     // ID_97853 survey check/notify

            const auto call_site = FindCallSite(outer, inner, kStarMapScanSearchWindow);
            if (!call_site)
            {
                // Sig-scan miss (issue #12): scoped notice, same shape as ScanHook's above.
                // g_orbitalScannerHookInstalled stays false so _GalaxyMapScanComplete can no-op sanely.
                spdlog::error("StarMapScanHook: CALL to ID_97853 not found inside ID_52173 — galaxy-map scan hook skipped, orbital-scanner auto-complete disabled");
                Engine::NotifyHookMissing("Orbital Scanner");
                return;
            }

            StarMapScanHook::func = reinterpret_cast<StarMapScanHook::fn_t>(
                REL::GetTrampoline().write_call<5>(call_site, reinterpret_cast<std::uintptr_t>(StarMapScanHook::thunk)));
            Engine::g_orbitalScannerHookInstalled.store(true, std::memory_order_release);

            spdlog::info("StarMapScanHook: installed at call-site 0x{:016X} (ID_52173 → ID_97853)", call_site);
        }
        catch (...)
        {
            spdlog::error("StarMapScanHook: caught fault during install — galaxy-map scan auto-complete disabled");
            Engine::NotifyHookMissing("Orbital Scanner");
        }
    }

    // Install the panel-refresh capture hook: patch ID_94011's CALL to ID_93988 so we stash the live
    // StarMap menu pointer on every natural repaint. Lets us re-invoke ID_93988 after our completion
    // (via RefreshStarMapPanelIfOpen) to repaint the info panel to 100% without a manual reselect.
    //
    // Sig-scan-miss policy (issue #12): COSMETIC and FAIL-OPEN, unlike ScanHook/StarMapScanHook above.
    // A miss here does not disable the Orbital Scanner feature itself — the completion still happens
    // (StarMapScanHook is independent); only the in-place info-panel repaint is lost, and the panel
    // still shows the correct 100% after a manual deselect/reselect or a menu reopen. So this stays
    // log-only (ERROR, no player popup) — a player-visible notice for a missed repaint would overstate
    // the impact and needlessly worry them about a feature that still works.
    void InstallStarMapRefreshHook()
    {
        try  // fault-guarded like Install() — log + skip, never fault the noexcept message callback
        {
            const auto outer = Engine::CriticalAddress(Engine::Ids::Idx::StarMapRefreshHookOuter);  // ID_94011 star-map scan handler
            const auto inner = Engine::CriticalAddress(Engine::Ids::Idx::RefreshStarMapPanelData);  // ID_93988 selected-planet panel populate

            const auto call_site = FindCallSite(outer, inner, kScanHookSearchWindow);
            if (!call_site)
            {
                spdlog::error("StarMapRefreshCaptureHook: CALL to ID_93988 not found inside ID_94011 — panel refresh disabled (cosmetic only; completion itself is unaffected)");
                return;
            }

            StarMapRefreshCaptureHook::func = reinterpret_cast<StarMapRefreshCaptureHook::fn_t>(
                REL::GetTrampoline().write_call<5>(call_site, reinterpret_cast<std::uintptr_t>(StarMapRefreshCaptureHook::thunk)));

            spdlog::info("StarMapRefreshCaptureHook: installed at call-site 0x{:016X} (ID_94011 → ID_93988)", call_site);
        }
        catch (...)
        {
            spdlog::error("StarMapRefreshCaptureHook: caught fault during install — star-map panel repaint disabled");
        }
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

            // === Pending galaxy-map scan dispatch ===
            // Star-map scan hook sets this when the player scans a body on the galaxy map. The
            // completion is fully ref-free, so we do NOT gate on the menu closing (the star map
            // updates live) — just a short frame grace to leave the hook's call frame, then dispatch
            // Papyrus _GalaxyMapScanComplete (which honours the "Enable Galaxy Map Scan" toggle).
            if (Engine::g_pendingGalaxyScan.load(std::memory_order_acquire)) {
                if (Engine::g_galaxyScanCountdown == 0) {
                    Engine::g_galaxyScanCountdown = kScannerDismissGraceFrames;
                }
                else if (--Engine::g_galaxyScanCountdown <= 0) {
                    if (Engine::g_pendingGalaxyScan.exchange(false, std::memory_order_acq_rel)) {
                        DispatchPapyrusStatic("_GalaxyMapScanComplete");
                        spdlog::info("Poller: dispatched _GalaxyMapScanComplete (planetFormId=0x{:08X})",
                                     Engine::g_galaxyScanPlanetFormId.load(std::memory_order_acquire));
                    }
                }
            }
            else {
                Engine::g_galaxyScanCountdown = 0;
            }

            // === Pending StarMap panel repaint ===
            // _GalaxyMapScanComplete sets this (via QueueStarMapRefresh) once it has completed a
            // galaxy-map-scanned planet. We do the ID_93988 repaint HERE — on the main thread, where
            // UI calls are safe — so the open info panel jumps to 100% without a manual reselect. The
            // captured menu is validated against the live menuArray first (never derefs a closed menu).
            if (Engine::g_pendingStarMapRefresh.exchange(false, std::memory_order_acq_rel)) {
                if (Engine::RefreshStarMapPanelIfOpen())
                    spdlog::info("Poller: repainted StarMap panel after galaxy-map completion");
            }
        });
        spdlog::info("InstallScanSweepPoller: per-frame poller registered");
    }

    // Re-bind the Papyrus natives whenever a new game SESSION becomes active.
    //
    // Why this is needed: our scripts (CompletePlanetSurveyQuest /
    // CompletePlanetSurveyNative) are FORMLESS — nothing in the ESM attaches them
    // to a quest, so the VM never PINS their script types. When the player quits to
    // the Main Menu the VM tears the session down and unloads any unpinned script
    // type (VirtualMachine::typesToUnload / DropAllRunningData). Our C++ native
    // bindings live inside the dropped CompletePlanetSurveyNative ObjectTypeInfo, so
    // they die with it. We register in kPostDataLoad, which fires ONLY ONCE per
    // process and never again, and this SFSE build exposes no PapyrusInterface for
    // per-VM re-registration. Result: after a main-menu -> load cycle the console
    // commands call into unbound natives and the "script is no longer recognised".
    //
    // Fix: watch the session boundary (Main Menu opening = a session ended) and
    // re-run Papyrus::Register() once the player is back in gameplay. BindNativeMethod
    // is idempotent — it overwrites the type's function-table entry — so a redundant
    // re-bind (e.g. the first load after boot) is harmless. We do NOT re-run
    // Hook::Install here: that is a code-memory trampoline patch that persists for the
    // whole process; re-applying it would double-patch the call site.
    //
    // We POLL from a permanent task rather than sink MenuOpenCloseEvent because the
    // event-sink path crashes DLL init on 1.16.236–244 here (see InstallScanSweepPoller).
    // If the menu-name strings ever drift, IsMenuOpen simply returns false and this
    // no-ops (no crash) — the mod degrades to the old once-only behaviour.
    void InstallSessionReRegisterPoller()
    {
        auto* task = SFSE::GetTaskInterface();
        if (!task) {
            spdlog::error("InstallSessionReRegisterPoller: no task interface");
            return;
        }
        constexpr int kSessionSettleFrames = 60;  // ~1s after the loading screen closes

        task->AddPermanentTask([]() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui)
                return;

            static const RE::BSFixedString kMainMenu {"MainMenu"};
            static const RE::BSFixedString kLoadingMenu {"LoadingMenu"};

            // Main-thread task, so plain statics are fine (no cross-thread access).
            static bool s_prevMainMenuOpen = false;
            static bool s_armed            = true;  // arm on first boot too (one redundant, harmless re-bind)
            static int  s_settle           = 0;

            const bool mainMenuOpen = ui->IsMenuOpen(kMainMenu);

            // Rising edge: the Main Menu just opened => the previous game session
            // ended (its formless script types, and our natives, were unloaded).
            // Arm a re-bind for the next time we reach gameplay, and drop any queued
            // scan/refresh so it can't fire a completion into the NEXT game session.
            if (mainMenuOpen && !s_prevMainMenuOpen)
            {
                s_armed = true;
                Engine::ResetPendingCompletionState();
                spdlog::info("Session boundary (Main Menu): cleared pending completion/refresh state");
            }
            s_prevMainMenuOpen = mainMenuOpen;

            if (!s_armed)
                return;

            // Only re-bind once we're truly back in gameplay: Main Menu closed AND
            // the loading screen gone, then settle a beat so VM re-init has finished.
            if (mainMenuOpen || ui->IsMenuOpen(kLoadingMenu)) {
                s_settle = 0;
                return;
            }
            if (++s_settle < kSessionSettleFrames)
                return;

            Papyrus::Register();  // re-attach ALL the natives onto the fresh session's VM (one shared bind path)
            s_armed  = false;
            s_settle = 0;
            spdlog::info("Re-bound Papyrus natives for new game session (formless-script relink)");
        });
        spdlog::info("InstallSessionReRegisterPoller: per-frame session-relink poller registered");
    }
}  // namespace Hook

namespace
{
    // Hand the ESM reader the ENGINE's own load order: every compiled plugin's path, master type
    // (full/medium/small) and runtime compile index, sequenced by the data handler's file list so
    // later files override earlier ones exactly like they do in-game. This is what lets DLC /
    // Creations planets (ShatteredSpace.esm, SFBGS00D.esm, SFBGS050.esm, …) resolve to their real
    // runtime FormIDs. Crash-safe: null-checked and bounded throughout; on ANY failure we simply
    // don't configure sources and the reader falls back to Starfield.esm alone (v1.4.0 behaviour).
    void ConfigureEsmSources() noexcept
    {
        try
        {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh)
            {
                spdlog::warn("EsmSources: TESDataHandler unavailable; reader falls back to Starfield.esm");
                return;
            }

            wchar_t buf[MAX_PATH] {};
            const auto n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return;
            const auto dataDir = std::filesystem::path {buf}.parent_path() / L"Data";

            // The compiled arrays give (type, runtime index) per file; array position IS the
            // compile index within that type's FormID space.
            struct Entry
            {
                const RE::TESFile* file;
                Esm::SourceFile    source;
                bool               consumed {false};
            };
            std::vector<Entry> entries;
            auto collect = [&](const RE::BSTArray<RE::TESFile*>& arr, Esm::MasterType type) {
                const auto count = std::min<std::uint32_t>(arr.size(), 0x1000);  // bounded (paranoia)
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto* file = arr[i];
                    if (!file)
                        continue;
                    const std::string name(file->fileName, strnlen(file->fileName, sizeof file->fileName));
                    if (name.empty())
                        continue;
                    entries.push_back({file, {dataDir / name, type, static_cast<std::uint16_t>(i)}});
                }
            };
            const auto& cc = dh->compiledFileCollection;
            collect(cc.files, Esm::MasterType::kFull);
            collect(cc.mediumFiles, Esm::MasterType::kMedium);
            collect(cc.smallFiles, Esm::MasterType::kSmall);
            if (entries.empty())
            {
                spdlog::warn("EsmSources: compiled file collection empty; reader falls back to Starfield.esm");
                return;
            }

            // Sequence by the data handler's master file list (true load order). Files the list
            // doesn't surface are appended afterwards in array order (full, medium, small) — the
            // per-type indices stay correct either way; only cross-type override order could vary.
            std::vector<Esm::SourceFile> sources;
            sources.reserve(entries.size());
            std::size_t guard = 0;
            for (auto* file : dh->files)
            {
                if (++guard > 0x2000)  // bounded walk of an engine-owned linked list
                    break;
                if (!file)
                    continue;
                for (auto& e : entries)
                {
                    if (!e.consumed && e.file == file)
                    {
                        e.consumed = true;
                        sources.push_back(e.source);
                        break;
                    }
                }
            }
            std::size_t appended = 0;
            for (auto& e : entries)
            {
                if (!e.consumed)
                {
                    sources.push_back(e.source);
                    ++appended;
                }
            }

            spdlog::info("EsmSources: configured {} plugins from the engine load order ({} full, {} medium, "
                         "{} small; {} not in the file list, appended)",
                         sources.size(), cc.files.size(), cc.mediumFiles.size(), cc.smallFiles.size(),
                         appended);
            for (const auto& s : sources)
                spdlog::debug("EsmSources:   [{}:{}] {}", static_cast<int>(s.type), s.runtimeIndex,
                              s.path.filename().string());
            Esm::SetSources(std::move(sources));
        }
        catch (const std::exception& e)
        {
            spdlog::error("EsmSources: failed ({}); reader falls back to Starfield.esm", e.what());
        }
        catch (...)
        {
            spdlog::error("EsmSources: failed (unknown); reader falls back to Starfield.esm");
        }
    }

    void MessageCallback(SFSE::MessagingInterface::Message* a_msg) noexcept
    {
        if (a_msg->type == SFSE::MessagingInterface::kPostDataLoad)
        {
            // Load-time offset self-check FIRST. If the address library has no offsets for this
            // runtime (the routine post-patch "SFSE ships before versionlib" case), degrade gracefully
            // instead of letting the first address-library resolution route through REX::FAIL and
            // hard-kill the game.
            //
            // When disabled we must do NOTHING that touches the engine — including binding the
            // natives: Papyrus::Register needs VirtualMachine::GetSingleton, and the re-bind poller's
            // task calls RE::UI::GetSingleton, BOTH of which CommonLibSF resolves internally via
            // REL::ID (e.g. RE::ID::GameVM::Singleton = 937585) → IDDB → REX::FAIL on the very
            // versionlib that just failed the probe. So the disabled path binds no natives (console
            // commands will report the script as unrecognised — unavoidable without the VM), installs
            // no hooks/pollers, loads no ESM, writes no GMSTs. CheckOffsets() already logged the one
            // ERROR line naming the cause and showed the player a non-blocking notice.
            if (!Engine::CheckOffsets())
            {
                spdlog::info("CompletePlanetSurvey initialized DISABLED: no natives bound, no hooks installed; "
                             "the game runs normally and console commands are unavailable until an updated "
                             "address library is installed");
                return;
            }

            Engine::ResolveOffsets();  // direct RVA binding from the probe's parse — no IDDB, no REX::FAIL
            Engine::g_offsetsValid.store(true, std::memory_order_release);

            ConfigureEsmSources();  // must precede any Esm:: query (the parse is one-shot)
            Papyrus::Register();
            Hook::Install();
            Hook::InstallStarMapScanHook();     // galaxy-map planet scan → complete-that-planet
            Hook::InstallStarMapRefreshHook();  // capture the StarMap menu for the post-completion repaint
            Hook::InstallScanSweepPoller();
            Hook::InstallSessionReRegisterPoller();  // re-bind natives after main-menu -> load (formless scripts)
            Engine::ApplyInstantScanGameSettings();
            spdlog::info("CompletePlanetSurvey initialized");
        }
    }
}  // namespace

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    // trampolineSize covers BOTH call-site hooks (ScanHook + StarMapScanHook); each write_call<5>
    // consumes ~14 bytes of trampoline. 128 leaves ample headroom.
    SFSE::Init(a_sfse, {.trampoline = true, .trampolineSize = 128});
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
