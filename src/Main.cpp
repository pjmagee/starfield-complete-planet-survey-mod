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

    // ---- GLOBAL 939118 ScannableComponent registry walk -----------------------------------------
    // The count walker ID_90522 (re/ghidra/output/onp-resolver-2026-06-23.txt:2030) and the outline
    // reader (ID_83007 via ID_90548) both read the SAME global 939118 ScannableComponent registry —
    // the BSTHashMap at managerBase+0x268, where managerBase = *(GetKnowledgeManager()+0x8b0) =
    // GetKnowledgeDB(). Walking that registry yields EXACTLY the loaded scannable refs the count and
    // outline see, with no "wrong instance" risk (FindAllReferencesWithKeyword can miss the rendered
    // overlay copies; the registry cannot — every loaded 939118 component is one entry).
    //
    // ID_939118: NOT a function — a uint16 domain discriminator (used as `(u64)ID_939118 << 0x30`
    //   and `CONCAT24(ID_939118, x)`, i.e. concatenated as a 2-byte value: onp-resolver:2070,
    //   discriminators.txt:3665). Read it as a uint16* global, exactly like TraitDiscriminator(938333).
    inline REL::Relocation<std::uint16_t*> ScannableDiscriminator {REL::ID(939118)};

    // ID_938083: the ref-keyed LocationManager "encountered/seen" component in db+0x268 — the durable
    // store a real trait scan writes that the planet-keyed 938333 does NOT (so 938333 alone leaves the
    // in-world object stuck at 0/N on reload). Same disc-global pattern (CONCAT24(ID_938083, refId)<<0x10,
    // discriminators.txt:1409, loc-reflist:25). Read as uint16* like the other two.
    inline REL::Relocation<std::uint16_t*> LocRefDiscriminator {REL::ID(938083)};

    // ID_126805: the registry BEGIN/lower-bound iterator over the same BSTHashMap ID_126806 point-looks
    //   up. Signature mirrors ID_126806 (q4-126806-confirm.txt): (container=db+0x268, out[4]u64 scratch,
    //   &keyLow) -> returns the out buffer (ptr to 4 u64s {b8,b0,a8,a0}). The caller passes an 80-byte
    //   scratch (`undefined1 local_90[80]`), so we give it >= 80 bytes. a8 (out[2]) = entry-bucket base,
    //   a0 (out[3]) = index; sentinel a0==0xfe0 && a8==0 means end. (onp-resolver:2073.)
    using fn_registry_begin_t = std::uint64_t* (*)(std::uintptr_t* container, void* out80, const std::uint64_t* keyLow);
    inline REL::Relocation<fn_registry_begin_t> ScannableRegistryBegin {REL::ID(126805)};

    // ID_39372: the iterator ADVANCE. Takes &iterState (the 4 u64s {b8,b0,a8,a0} laid out contiguously,
    //   as `&local_b8`) and updates them in place to the next entry. (onp-resolver:2163.)
    using fn_registry_advance_t = void (*)(void* iterState4);
    inline REL::Relocation<fn_registry_advance_t> ScannableRegistryAdvance {REL::ID(39372)};

    // Registry-entry / sentinel constants, read straight from the ID_90522 decompile (onp-resolver:2078-2121):
    //   entryOff = *(u16*)(a8 + 0x12 + a0*4);  formID = *(u32*)(entryOff + 0x20 + a8);
    //   byte     = *(u8*) (entryOff + 0x28 + a8);  keyHigh = (u64)disc<<0x30 | 0xffffffffffff.
    constexpr std::size_t  kRegOffsetTableBase = 0x12;   // a8 + 0x12 + a0*4 -> u16 entry offset
    constexpr std::size_t  kRegEntryKeyOffset  = 0x10;   // entryOff + 0x10 + a8 -> u64 key (vs keyHigh)
    constexpr std::size_t  kRegEntryFormId     = 0x20;   // entryOff + 0x20 + a8 -> u32 FormID
    constexpr std::size_t  kRegEntryStateByte  = 0x28;   // entryOff + 0x28 + a8 -> u8 scanned state
    constexpr std::uintptr_t kRegEndIndex      = 0xfe0;  // a0 sentinel for "end"
    constexpr std::uint64_t  kRegKeyLowMask    = 0xffffffffffffULL;  // OR'd into keyHigh
    constexpr std::size_t  kRegScratchBytes    = 96;     // >= the caller's 80-byte scratch, rounded up
    constexpr std::uint32_t kRegMaxIterations  = 4096;   // infinite-loop backstop (registry is small)
    constexpr std::uint32_t kHandscannerHighlightRangeKw = 0x001CBEA3;  // Handscanner_AllowScanAtHighlightRange
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

    // ID_52188: resolve the RENDER-domain planet id for a ref — the BSGalaxy NumericID the outline
    // renderer keys the green PlayerKnowledge entry on (from the ref's ExtraLocation 0x81 / parentCell
    // / current-planet global). This is a DIFFERENT id domain than ReadPlanetId's form +0x54 FormID
    // (which the data sweep + survey-% use). THE "100% but blue" BUG: the mod wrote +0x21 under the
    // +0x54 id, but the outline reads (938333|ID_52188(player)) — a different entry. A real scan
    // writes via ID_52188(ref), same domain it reads, which is why CompleteSurvey greens. See
    // render-read-target-2026-06-22.md. Out params are two int* (planetId + a small secondary).
    using fn_resolve_planet_t = std::uint64_t (*)(void* ref, std::int32_t* outPlanetId, std::int32_t* outSecondary);
    inline REL::Relocation<fn_resolve_planet_t> ResolvePlanetFromRef {REL::ID(52188)};

    std::uint32_t GetRenderPlanetId(RE::TESObjectREFR* ref)
    {
        if (!ref)
            return 0;
        std::int32_t planetId  = 0;
        std::int32_t secondary = 0;
        ResolvePlanetFromRef(ref, &planetId, &secondary);
        return static_cast<std::uint32_t>(planetId);
    }

    // ID_52159: the OUTLINE renderer's OWN green read — `ID_52159(playerRef, speciesId)` returns the
    // +0x21 byte for (player's planet via ID_52188, species via FNV hash) — the exact value
    // ID_90491/ID_90548 use to decide green-vs-blue (species-scanned-check.txt:86-136). Calling it
    // directly lets us ASK THE ENGINE what the render sees for a species, instead of trusting the
    // decompile (wrong 4×). It reads the SAME hashmap+slot the mod's ID_124898 writes — so if it
    // returns 0 for a species we just wrote +0x21 for, the render keys on a DIFFERENT species id
    // than the authored one (e.g. the live wild creature's id), which is the real discrepancy.
    using fn_render_green_read_t = char (*)(void* playerRef, std::uint32_t species);
    inline REL::Relocation<fn_render_green_read_t> RenderGreenRead {REL::ID(52159)};

    std::uint8_t ReadRenderGreen(RE::TESObjectREFR* playerRef, std::uint32_t species)
    {
        if (!playerRef || !species)
            return 0;
        return static_cast<std::uint8_t>(RenderGreenRead(playerRef, species));
    }

    // ID_124901: the engine's species-slot hash (FNV-1a of the 4-byte species id) -> slot index in a
    // subobj's species hashmap. Used to dump the RAW per-species slot bytes (mirrors ID_52159's
    // lookup) so we can DIFF a full scan (green+info+XP) vs a +0x21 byte-poke (half) and find the
    // missing "species catalogued/known" field the real scan writes and we don't.
    using fn_species_slot_hash_t = std::uint64_t (*)(std::uintptr_t hashmap, const void* key4);
    inline REL::Relocation<fn_species_slot_hash_t> SpeciesSlotHash {REL::ID(124901)};

    // ID_35755: BSTArray<u32>::push_back grow path — (header{begin,end,cap}, pos, &value). Allocates
    // via the ENGINE allocator (ID_35770) and updates the header + frees the old buffer (ID_35757),
    // so the array is engine-OWNED and safe to free on teardown (ID_35771). This is how the real scan
    // fills slot+0x08; we use it to build that array ref-free — the GREEN fix.
    using fn_bstarray_grow_t = std::uint32_t* (*)(std::int64_t* header, std::uint32_t* pos, const std::uint32_t* value);
    inline REL::Relocation<fn_bstarray_grow_t> BSTArrayU32Grow {REL::ID(35755)};

    // ID_35770: the engine's BSTArray heap allocator — void* alloc(size_bytes, alignment). Same allocator
    // ID_35755 uses to grow slot+0x08, and the same the StoredComponent serializer (ID_45726) walks +
    // teardown (ID_35771) frees. Used to build the 16-byte pooled BSTArray at subobj+0x08 (below).
    using fn_engine_alloc_t = void* (*)(std::uint64_t sizeBytes, std::uint64_t alignment);
    inline REL::Relocation<fn_engine_alloc_t> EngineScalarAlloc {REL::ID(35770)};

    // ID_35771: the matching BSTArray deallocator — void free(void* ptr, size_t byteCount). Frees a buffer
    // allocated by ID_35770/ID_35755 (the engine calls it on slot teardown / rehash with 4-byte stride).
    using fn_engine_free_t = void (*)(void* ptr, std::uint64_t byteCount);
    inline REL::Relocation<fn_engine_free_t> EngineScalarFree {REL::ID(35771)};

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

    // The LIVE biome member table holds the engine's EXACT per-species marker set on the CURRENT planet —
    // it is the source array that slot+0x08 is copied from (ID_52158). Reading it yields the correct
    // value-specific markers for BOTH kingdoms (flora's resource/genetics/reproduction markers AND fauna's
    // X) with NO derivation, NO condition VM, NO fault risk (pure loads) — strictly more correct than any
    // hardcode, and it sidesteps the materialization-bound ID_83024 (which faults ref-free) and the
    // value-specific flora markers (which a fixed list can't capture). Off-planet the live biome is null
    // (the table is materialization-bound), so this returns false there. (complete-scan-green-model §3.2a.)
    inline REL::Relocation<std::uintptr_t*> Singleton937609 {REL::ID(937609)};  // *+0x160 = live biome

    // ID_56887: FNV-1a member find over the biome member hashmap. (table=biome+0x20, out[2], key4) ->
    // out[0]=table base (==biome+0x20), out[1]=matched index (== bucketCount on MISS). The member entry
    // is bucketBase + out[1]*0x28; its uint[] marker array is [member+0x08 .. member+0x10).
    using fn_find_member_t = std::int64_t* (*)(std::int64_t table, std::int64_t out[2], unsigned char* key4);
    inline REL::Relocation<fn_find_member_t> FindBiomeMember {REL::ID(56887)};

    // Read a (current-planet, species) marker set from the live biome member table. Returns false
    // off-planet / on miss / on fault (caller leaves the species blue). LOCALLY guarded (/EHa); pure loads.
    bool ReadLiveMemberMarkers(std::uint32_t speciesFormId, std::vector<std::uint32_t>& out)
    {
        out.clear();
        try
        {
            auto* const s = Singleton937609.get();
            if (!s || !*s)
                return false;
            const auto biome = *reinterpret_cast<std::uintptr_t*>(*s + 0x160);
            if (!biome)  // off-planet / no live biome materialized
                return false;
            std::int64_t res[2] = {0, 0};
            FindBiomeMember(static_cast<std::int64_t>(biome) + 0x20, res, reinterpret_cast<unsigned char*>(&speciesFormId));
            const auto table       = static_cast<std::uintptr_t>(res[0]);  // == biome+0x20
            const auto bucketBase  = *reinterpret_cast<std::uintptr_t*>(table + 0x20);
            const auto bucketCount = *reinterpret_cast<std::uint64_t*>(table + 0x28);
            const auto idx         = static_cast<std::uint64_t>(res[1]);
            if (bucketCount == 0 || idx == bucketCount || !bucketBase)  // empty / MISS
                return false;
            const auto  member = bucketBase + idx * 0x28;
            auto* const begin  = *reinterpret_cast<std::uint32_t**>(member + 0x08);
            auto* const end    = *reinterpret_cast<std::uint32_t**>(member + 0x10);
            if (!begin || end < begin || (end - begin) > 0x40)  // sanity bound
                return false;
            for (auto* p = begin; p != end; ++p)
                out.push_back(*p);
            return !out.empty();
        }
        catch (...)
        {
            return false;
        }
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

    // ID_83009: resolves the CANONICAL species id (ScannableComponent +0x24) that the outline
    // renderer (ID_52159, via ID_90491/ID_90548) hashes for a live instance. CONFIRMED by full
    // decompile trace: the engine's REAL scan keys the persistent green +0x21 byte under THIS
    // canonical id, NOT the ESM/PPBD authored form id. For leveled/template fauna the two differ,
    // so our ref-free sweep — which keyed +0x21 by the AUTHORED id — landed in the wrong slot:
    // GetSurveyPercent (walks the authored array) reads 100%, but the outline (canonical FNV-1a
    // hash) reads 0 -> blue. That is exactly the "survey complete but flora/fauna blue" signature.
    // ID_83009 returns entry+0x24 when the instance HAS a scan component, else ref->formID (the
    // identity case where authored == canonical). Only the ref arg is consumed; rest is spill.
    using fn_get_canonical_species_t = std::uint32_t (*)(void* ref, void*, void*, void*);
    inline REL::Relocation<fn_get_canonical_species_t> GetCanonicalSpeciesNative {REL::ID(83009)};

    std::uint32_t GetCanonicalSpeciesId(RE::TESObjectREFR* ref)
    {
        if (!ref)
            return 0;
        return GetCanonicalSpeciesNative(ref, nullptr, nullptr, nullptr);
    }

    // ID_83025: the identity-reveal known-set writer (Unknown -> named). Marks the ref's canonical id
    // "discovered/known" in the live biome's known-set (biome+0x20) so the scanner panel's ID_64337
    // gate shows the real name instead of "Unknown Feature". SAFE by construction: it does an ID_56887
    // member-find over biome+0x20 keyed by canonId and ONLY writes if the id is a member (no-ops
    // otherwise); the inner ID_83024 (which faults ref-free) is reached only after a valid slot lookup.
    // param_1 = *(937609+0x160) = the live biome (the SAME deref as ReadLiveMemberMarkers); param_2 =
    // ref; param_3 = canonId (ID_83009). (trait-onplanet-completion-2026-06-23.md §2 step D / §5.)
    using fn_reveal_known_t = void (*)(std::uintptr_t panelDB, void* ref, std::uint32_t canonId);
    inline REL::Relocation<fn_reveal_known_t> RevealKnownNative {REL::ID(83025)};

    // DIAGNOSTIC: probe the known-set member's marker array (the inferred source of the serialized rec+0x06
    // "known" byte). Logs whether the canonId member is found and how many marker ids it holds, so we can see
    // — before vs after ID_83025 — whether the mark actually FILLS the marker array (member+0x08..+0x10) or
    // no-ops. (Save21 reloaded 0/2 even though ID_83025 already ran, so we must learn why empirically.)
    void ProbeKnownMember(std::uintptr_t biome, std::uint32_t canon, const char* tag)
    {
        try
        {
            std::int64_t res[2] = {0, 0};
            auto         c      = canon;
            FindBiomeMember(static_cast<std::int64_t>(biome) + 0x20, res, reinterpret_cast<unsigned char*>(&c));
            const auto table       = static_cast<std::uintptr_t>(res[0]);
            const auto bucketBase  = *reinterpret_cast<std::uintptr_t*>(table + 0x20);
            const auto bucketCount = *reinterpret_cast<std::uint64_t*>(table + 0x28);
            const auto idx         = static_cast<std::uint64_t>(res[1]);
            const bool found       = (bucketCount != 0 && idx != bucketCount && bucketBase != 0);
            long long  count       = -1;
            if (found)
            {
                const auto  member = bucketBase + idx * 0x28;
                auto* const begin  = *reinterpret_cast<std::uint32_t**>(member + 0x08);
                auto* const end    = *reinterpret_cast<std::uint32_t**>(member + 0x10);
                count = (begin && end >= begin) ? static_cast<long long>(end - begin) : -2;
            }
            spdlog::info("[known-probe] {} canon=0x{:08X} found={} markerCount={} (bucketCount={} idx={})",
                         tag, canon, found, count, bucketCount, idx);
        }
        catch (...)
        {
            spdlog::info("[known-probe] {} canon=0x{:08X} FAULT", tag, canon);
        }
    }

    void RevealScanTargetIdentity(RE::TESObjectREFR* ref)
    {
        if (!ref)
            return;
        auto* const s = Singleton937609.get();
        if (!s || !*s)
            return;
        const auto biome = *reinterpret_cast<std::uintptr_t*>(*s + 0x160);
        if (!biome)  // off-planet / no live biome materialized -> nothing to reveal into
            return;
        std::uint32_t canon = GetCanonicalSpeciesId(ref);
        if (!canon)
            return;
        // ID_83025 finds the canonId member and (via ID_83024) appends runtime scan-markers into its marker
        // array (member+0x08..+0x10); the serializer projects "marker array non-empty" -> the durable rec+0x06
        // "known" byte the panel reveal reads. The members PRE-EXIST (save-verified: 31 in every save), so no
        // create is needed.
        RevealKnownNative(biome, ref, canon);  // ID_83025: fill the member's marker array (Unknown -> named)
    }

    // READ-ONLY: resolve a loaded trait scan-target's known-set member and log its state — NO writes.
    // Mirrors RevealScanTargetIdentity's resolution but only probes, so the diagnostic registry walk can
    // run without setting the on-planet "scanned" byte (which jams manual re-scanning until the planet
    // re-materializes) or the durable 938333 record. Used by TestTraitRegistryWalk (now non-destructive).
    void ProbeScanTargetKnownSet(RE::TESObjectREFR* ref)
    {
        if (!ref)
            return;
        auto* const s = Singleton937609.get();
        if (!s || !*s)
            return;
        const auto biome = *reinterpret_cast<std::uintptr_t*>(*s + 0x160);
        if (!biome)
            return;
        const std::uint32_t canon = GetCanonicalSpeciesId(ref);
        if (!canon)
            return;
        ProbeKnownMember(biome, canon, "walk");
    }

    // ★ COMPLETE one LOADED trait scan-target REFR, robust to the byte-already-set case.
    //
    // ID_83008(ref,1,8,0) sets 939118+0x28 (green outline + count store), but its durable/identity
    // fan-out (-> ID_52157) only fires INSIDE ID_83038's `if (byte changed 0->1)` block. If an earlier
    // pass already set the byte, ID_83008 no-ops the credit -> count stays 0/M, name stays "Unknown".
    // So we ALSO call PlanetProgressNative (ID_52157) DIRECTLY, which is UNCONDITIONAL:
    //   ID_52157(ref, canonId, mode=8, 0, fireEvent=1)  (decompile ID_52157 @1407b7fa0)
    //     -> resolves planetId from the ref via ID_52188 (must be on-surface)
    //     -> ID_52158: writes durable 938333 +0x21 (scanned) / +0x20 (pct) keyed by the ID_83009
    //        canonical id (ID_124898/ID_124899), fires ID_101322 "fully surveyed" on threshold, and
    //        writes the ID_83025 known-set (Unknown->named) — all with NO byte-transition requirement.
    //     -> ID_83019 survey-event sink (because param_5==1) + ID_97853 survey recompute.
    // Then RevealScanTargetIdentity belt-and-braces (the same ID_83025 write ID_52158 already does).
    //
    // `canon` is ID_83009(ref); when 0 (no resolvable canonical) we skip the direct credit (ID_52157's
    // 938333 key would be degenerate) but still set the green/count byte via ID_83008. Caller MUST have
    // checked ID_83007(ref) != 0 (live component) before calling — see CompleteTraitScanTargetRef §7.
    // Trait scan-target table: trait keyword <-> its scan-target base ACTI(s). Shared by
    // GetTraitScanTargetActi (kw->ACTI) and TraitKeywordForCanonical (ACTI->kw = the +0x08 member id a
    // real scan appends to the durable 938333 slot; decoded byte-exact, scan-count-store-2026-06-23.md).
    struct TraitScanTargetEntry { std::uint32_t traitKw; std::uint32_t acti[2]; };
    inline constexpr TraitScanTargetEntry kTraitScanTargets[] = {
        {0x00225597, {0x001AC573, 0x0021B297}}, {0x00225596, {0x0021B288, 0}},
        {0x0029081C, {0x00245AC2, 0}},          {0x00225595, {0x0021B286, 0}},
        {0x00225594, {0x0021B282, 0}},          {0x00225593, {0x0021B27C, 0}},
        {0x00225592, {0x0021B278, 0}},          {0x00246C66, {0x00239D8C, 0x0023CAD0}},
        {0x00225591, {0x0021B274, 0}},          {0x00225590, {0x0021B272, 0}},
        {0x0022558F, {0x0021B26C, 0}},          {0x00290819, {0x0023875B, 0}},
        {0x0022558E, {0x0021B268, 0}},          {0x0029081B, {0x00238751, 0}},
        {0x0022558D, {0x0021B264, 0}},          {0x0022558C, {0x0021B260, 0}},
        {0x0022558B, {0x0021B25C, 0}},          {0x0029081A, {0x00239D8B, 0x0023CAD1}},
        {0x0022558A, {0x0021B258, 0}},          {0x00225589, {0x0021B254, 0}},
        {0x00225588, {0x0021B250, 0}},          {0x00225587, {0x0021B24C, 0}},
        {0x00225586, {0x001677C3, 0x0021B248}}, {0x00225585, {0x0021B244, 0}},
        {0x00225584, {0x0021B240, 0}},          {0x00221980, {0x0021B23C, 0}},
        {0x0028515F, {0x00111F5A, 0}},
    };
    std::uint32_t TraitKeywordForCanonical(std::uint32_t canonicalActi);  // fwd (ACTI -> trait kwd; below)
    bool CompleteTraitSlot(std::uint32_t planetId, std::uint32_t canonicalActi, std::uint32_t traitKeyword);  // fwd (below)
    void CompleteScanTargetCredit(RE::TESObjectREFR* ref, std::uint32_t canon)
    {
        if (!ref)
            return;
        const auto before = IsBiomeRef(ref);
        // A: green outline + N/M count store (939118+0x28). Drives the durable fan-out too, but ONLY
        // if the byte transitions 0->1 (so this alone is insufficient when an earlier pass set it).
        ScanRefNative(ref, 1, 8, 0);
        const auto afterByte = IsBiomeRef(ref);  // DIAGNOSTIC: did ID_83008 actually persist +0x28?
        // B/C/D: the DURABLE "100% SCANNED" + named completion = the planet-keyed 938333 PlayerKnowledge
        // record, decoded byte-exact vs a real scan (re/save/scan-count-store-2026-06-23.md): per-canonical
        // slot = +0x21 flag(==2), +0x20 pct(==100), AND +0x08 member BSTArray holding the TRAIT KEYWORD.
        // Routing through ID_52158 set flag/pct but left +0x08 EMPTY (its biome-cluster pass maps SPECIES,
        // not the trait keyword) — verified absent in the user's Save17. CompleteTraitSlot hand-appends the
        // keyword to +0x08 directly (engine BSTArray grow-path ID_35755) so the saved record is byte-equal
        // to a real scan -> renders "100% SCANNED" + named on reload, ref-free + all-planets. (The transient
        // outline / live N-M digits are a SEPARATE 939118 store; the ScanRefNative above covers the on-planet
        // live outline.) planetId = RENDER domain (ID_52188) the durable is keyed by, resolved via the player.
        if (canon)
        {
            auto* const player   = RE::PlayerCharacter::GetSingleton();
            const auto  planetId = GetRenderPlanetId(player ? static_cast<RE::TESObjectREFR*>(player) : ref);
            const auto  keyword  = TraitKeywordForCanonical(canon);
            if (planetId && keyword)
                CompleteTraitSlot(planetId, canon, keyword);
        }
        const auto afterCredit = IsBiomeRef(ref);
        // Belt-and-braces identity reveal (no-ops if the canonical isn't a live biome member).
        RevealScanTargetIdentity(ref);
        spdlog::info("[trait-credit] byte-state before={} afterScanRef={} afterCredit={} canon=0x{:08X} "
                     "(1=unscanned,2=scanned; if afterScanRef stays 1 the +0x28 write was gated)",
                     static_cast<int>(before), static_cast<int>(afterByte), static_cast<int>(afterCredit), canon);
    }

    // Walk the GLOBAL 939118 ScannableComponent registry exactly as the count walker ID_90522 does
    // (re/ghidra/output/onp-resolver-2026-06-23.txt:2070-2167), invoking `fn(formID, stateByte, REFR*)`
    // for each loaded scannable. This is the EXACT store the outline reader (ID_83007/ID_90548) and the
    // N/M count (ID_90522) both read, so it cannot miss the rendered overlay instances the way
    // FindAllReferencesWithKeyword can. Returns the number of entries visited (capped).
    //
    // SAFETY: the whole walk is wrapped in try/catch — src/ is built /EHa, so a wrong offset deref
    // (access violation) is caught as a C++ exception rather than hard-crashing the process. Every
    // pointer is null-checked before deref and the loop is hard-capped at kRegMaxIterations as an
    // infinite-loop backstop. fn itself is invoked inside the try, so a fault inside a callback also
    // unwinds cleanly. Caller must still hold a stable game state (on-surface, menus quiescent).
    template <typename Fn>
    std::uint32_t ForEachScannableInRegistry(Fn&& fn)
    {
        std::uint32_t visited = 0;
        try
        {
            const auto db = GetKnowledgeDB();  // == *(GetKnowledgeManager()+0x8b0) == ID_90521's managerBase
            if (!db)
                return 0;

            const std::uint16_t disc    = *ScannableDiscriminator.get();  // ID_939118 (uint16 domain prefix)
            const std::uint64_t keyLow  = static_cast<std::uint64_t>(disc) << 0x30;
            const std::uint64_t keyHigh = keyLow | kRegKeyLowMask;

            // Scratch for the iterator (caller uses an 80-byte local; give it 96 to be safe), plus the
            // 4-u64 iterator state {b8,b0,a8,a0} laid out contiguously (ID_39372 takes &b8).
            alignas(16) std::uint8_t scratch[kRegScratchBytes] {};
            auto container = reinterpret_cast<std::uintptr_t*>(db + kDbContainerOffset);  // db+0x268

            std::uint64_t* it = ScannableRegistryBegin(container, scratch, &keyLow);
            if (!it)
                return 0;

            // iterState = {b8, b0, a8(entry-bucket base), a0(index)} — ID_39372 advances in place.
            std::uint64_t iterState[4] = {it[0], it[1], it[2], it[3]};

            for (std::uint32_t guard = 0; guard < kRegMaxIterations; ++guard)
            {
                const std::uintptr_t a8 = static_cast<std::uintptr_t>(iterState[2]);
                const std::uintptr_t a0 = static_cast<std::uintptr_t>(iterState[3]);

                // End sentinel: (a0 == 0xfe0 && a8 == 0). Match the decompile's exact condition.
                if (a0 == kRegEndIndex && a8 == 0)
                    break;
                if (!a8)
                    break;

                // entryOff = *(u16*)(a8 + 0x12 + a0*4). Bounds the read within the bucket.
                const auto* offTablePtr =
                    reinterpret_cast<const std::uint16_t*>(a8 + kRegOffsetTableBase + a0 * 4);
                const std::uintptr_t entryOff = *offTablePtr;
                const std::uintptr_t entry    = a8 + entryOff;

                // keyHigh guard: stop once the entry key exceeds the 939118 domain ceiling (the
                // walker's `*(u64*)(entryOff+0x10+a8) <= keyHigh` loop condition).
                const std::uint64_t entryKey = *reinterpret_cast<const std::uint64_t*>(entry + kRegEntryKeyOffset);
                if (entryKey > keyHigh)
                    break;

                ++visited;

                const std::uint32_t formID    = *reinterpret_cast<const std::uint32_t*>(entry + kRegEntryFormId);
                const std::uint8_t  stateByte = *reinterpret_cast<const std::uint8_t*>(entry + kRegEntryStateByte);

                RE::TESObjectREFR* refr = nullptr;
                if (formID)
                {
                    auto* form = RE::TESForm::LookupByID(formID);  // == ID_47401
                    if (form)
                    {
                        // The walker resolves the live REFR via the form's vtable slot +0x228. In
                        // CommonLibSF the loaded scannable entries ARE references, so a direct cast is
                        // sufficient and avoids a hand-rolled vtable call. Guard the form type so a
                        // stray non-REFR form can't be reinterpreted.
                        if (form->Is(RE::FormType::kREFR) || form->GetFormType() == RE::FormType::kACHR)
                            refr = static_cast<RE::TESObjectREFR*>(form);
                    }
                }

                fn(formID, stateByte, refr);

                // Advance the iterator in place (ID_39372 updates iterState[0..3]).
                ScannableRegistryAdvance(iterState);
            }

            if (visited >= kRegMaxIterations)
                spdlog::warn("[trait-walk] hit iteration cap {} — registry larger than expected or loop stuck",
                             kRegMaxIterations);
        }
        catch (...)
        {
            spdlog::error("[trait-walk] caught fault during registry walk after {} entries — aborting walk", visited);
        }
        return visited;
    }

    // ID_83006: resolve a species base FORM to its CANONICAL form. This is the missing piece.
    // The outline reader ID_52159 hashes the canonical id stamped into a scanned instance's
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

    // Mark a LOADED scan-target ref "encountered/seen" in the ref-keyed ID_938083 LocationManager
    // component — the durable store a real scan writes that the mod's planet-keyed 938333 does NOT,
    // and the missing piece that leaves the in-world trait object stuck at 0/N on reload even with a
    // byte-perfect 938333 record (in-game confirmed). THE WRITE is decompile-verified from ID_57033
    // @140914700:103 — after a DbLookup under (disc_938083<<48)|(refId<<16), set the single byte at
    //   entry+0xb9 = 1   (NOT the +0x20 subobj; a plain u8, not a list-append).
    // REF-keyed -> only works for a ref whose 938083 entry already exists (the engine creates it on
    // cell-load/materialization). DbLookup is a pure point-lookup: a miss returns 0 (no create, no
    // crash). Returns 1 written, 0 no-entry (not materialized), -1 bad input/db.
    int MarkScanTargetLocationDurable(std::uint32_t refFormId)
    {
        if (!refFormId)
            return -1;
        const auto db = GetKnowledgeDB();
        if (!db)
            return -1;
        const std::uint16_t disc = *LocRefDiscriminator.get();  // ID_938083
        const std::uint64_t key  =
            (static_cast<std::uint64_t>(disc) << 48) | (static_cast<std::uint64_t>(refFormId) << 16);

        std::uintptr_t out[4]    = {0, 0, 0, kDbLookupNotFound};
        auto           container = reinterpret_cast<std::uintptr_t*>(db + kDbContainerOffset);
        DbLookup(container, out, &key);
        if (out[3] == kDbLookupNotFound && out[2] == 0)
            return 0;  // no 938083 entry for this ref (not materialized) — nothing to write, no create

        const auto base     = reinterpret_cast<std::uint8_t*>(out[2]);
        const auto entryOff = *reinterpret_cast<std::uint16_t*>(base + kBucketOffsetTableOff + out[3] * 4);
        // entry = base + entryOff; the "seen" byte is at entry+0xb9 (ID_57033:103). Engine writes 1.
        *(base + entryOff + 0xb9) = 1;
        spdlog::info("[locref-seen] ref=0x{:08X} -> 938083 entry+0xb9 = 1 (durable seen)", refFormId);
        return 1;
    }

    // ACTI scan-target canonical -> its trait keyword (the +0x08 member a real scan appends). Inverse of
    // kTraitScanTargets (kw->ACTI). The canon arg comes from ID_83009(ref) and == the scan-target base ACTI.
    std::uint32_t TraitKeywordForCanonical(std::uint32_t canonicalActi)
    {
        for (const auto& e : kTraitScanTargets)
            if (e.acti[0] == canonicalActi || e.acti[1] == canonicalActi)
                return e.traitKw;
        return 0;
    }

    // Reproduce a real trait-scan's DURABLE 938333 PlayerKnowledge record for one scan-target, so it
    // renders "100% SCANNED" + the named feature on reload (ref-free, all-planets, persists). Decoded
    // BYTE-EXACT from a real scan vs the mod's own write (re/save/scan-count-store-2026-06-23.md, the
    // Save12/13/14 vs Save18 three-state diff). The per-planet 938333 subobject serializes as:
    //   ver(u32=3) | u16(0)
    //   POOLED member BSTArray<u32>  (in memory at subobj+0x08, a 16-byte BSTArray {data, size, cap})
    //   SLOT hashmap { count(u32), pad(u32), [ slot{ id(u32), pct(u8 @slot+0x20), flag(u8 @slot+0x21),
    //                                               inline 24-byte BSTArray @slot+0x08 } ] }
    //
    // A REAL trait scan (Save14) puts the trait KEYWORD in the POOLED array (subobj+0x08) and leaves the
    // canonical SLOT's inline +0x08 EMPTY, with slot flag == 2, pct == 100. The mod's PRIOR write put the
    // keyword in the SLOT's inline +0x08 (wrong array — serializes AFTER the slot, not before) and let the
    // flag accumulate to 10 (8 stamped by the earlier ScanRefNative engine path + 2 from two IncrementScanFlag
    // calls). Both errors made the saved record NON-byte-identical -> the panel reveal rejected it on reload.
    //
    // FIX (produces a record byte-identical to Save14):
    //   1. flag = EXACTLY 2 and pct = 100 written DIRECTLY to slot+0x21 / slot+0x20 (idempotent absolute
    //      writes — NOT the saturating IncrementScanFlag accumulate, which inherits the ScanRefNative-8).
    //   2. the canonical slot's inline +0x08 24-byte array is CLEARED (a real scan leaves it empty here).
    //   3. the trait keyword is appended to the POOLED 16-byte BSTArray at subobj+0x08 via the engine
    //      allocator (ID_35770) — round-trips through save (ID_45726/ID_52193) + load (ID_51523).
    // Returns false if the planet's knowledge entry / canonical slot is missing (discover / scan first).
    bool CompleteTraitSlot(std::uint32_t planetId, std::uint32_t canonicalActi, std::uint32_t traitKeyword)
    {
        if (!planetId || !canonicalActi || !traitKeyword)
            return false;
        const auto db = GetKnowledgeDB();
        if (!db)
            return false;
        const auto subobj = ResolvePlanetSubobj(db, planetId);
        if (!subobj)
            return false;
        const auto base = reinterpret_cast<std::uintptr_t>(subobj);

        // CLEAN per-trait write (save-diff 2026-06-24: real 2/2 record = 58 B, the mod's old write = 152 B
        // MALFORMED — the engine reloads the malformed record as "0/2 / UNKNOWN FEATURE"). The malformation
        // came from HAND-MANAGING the slot's inline +0x08 array (the manual zero) and HAND-PUSHING the keyword
        // into the pooled subobj+0x08 BSTArray. Both are removed. We now only:
        //   1. let the ENGINE create the slot + set pct (ID_124899 SetPercentByte) — engine-sized, +0x08 empty,
        //   2. set the scan-flag byte to EXACTLY 2 directly (IncrementScanFlag would ACCUMULATE past 2).
        // The trait KEYWORD goes into the pooled array via MarkTraits (ID_52155, the engine's own trait-known
        // writer) which the caller runs FIRST — so the pooled array is engine-grown, never hand-built. This
        // reproduces the clean Save30 slot byte-for-byte: <id> <flag=2> <pct=100> <empty +0x08>.
        SetPercentByte(subobj, canonicalActi, static_cast<std::uint8_t>(100), 0);  // ID_124899: create slot + pct

        const auto hashmap = base + 0x18;
        const auto hashEnd = *reinterpret_cast<std::uint64_t*>(base + 0x48);
        const auto slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);
        const auto idx     = SpeciesSlotHash(hashmap, &canonicalActi);
        if (idx == hashEnd || !slots)
            return false;
        const auto slotAddr = slots + idx * 0x30;
        *reinterpret_cast<std::uint8_t*>(slotAddr + 0x21) = 2;  // scan-flag = 2 (complete); pct=100 set above

        spdlog::info("[trait-slot] planet=0x{:08X} canon=0x{:08X} kwd=0x{:08X} -> slot flag=2 pct=100 "
                     "(clean; +0x08 left empty, keyword owned by MarkTraits)", planetId, canonicalActi, traitKeyword);
        return true;
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
    int CompleteAllPlanetsSurveyData_Phase1(std::uint8_t /*delta*/)
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
            // Engine discover (ID_102650): create the per-planet knowledge entry if missing + mark
            // it discovered (async create; the Papyrus finalize pass mops up stragglers + fires the
            // slate). Then write the ref-free survey state (attribute bits + resources). With no
            // flora/fauna on these bodies, this reaches a genuine 100%.
            ScanCompletePlanet(0, planetId, 1);
            markedTotal += WritePlanetSurveyState(planetId, kDefaultScanDelta);
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

    // Map a PlanetTrait keyword -> its PlanetTraitScanTarget ACTI base form id(s). The surface
    // "scan target" objects (the "Unknown Feature" / e.g. "Microbial Community") whose LOADED instances
    // Papyrus FindAllReferencesOfType + SetScanned to green a planet's trait scan-targets (per-ref
    // 939118 +0x28; on-planet). Static catalog table (27 traits; 4 carry a 2nd target). slot 0|1; 0=none.
    // Built from the ESM (re/tools/trait_scan_target_map.json): trait KYWD -> PlanetTraitScanTarget ACTI.
    std::int32_t GetTraitScanTargetActi(std::monostate, RE::BGSKeyword* traitKw, std::int32_t slot)
    {
        if (!traitKw || slot < 0 || slot > 1)
            return 0;
        struct Entry
        {
            std::uint32_t traitKw;
            std::uint32_t acti[2];
        };
        static constexpr Entry kTable[] = {
            {0x00225597, {0x001AC573, 0x0021B297}},  // 00 AeriformLife
            {0x00225596, {0x0021B288, 0}},           // 01 AmphibiousFoothold
            {0x0029081C, {0x00245AC2, 0}},           // 02 BoiledSeas
            {0x00225595, {0x0021B286, 0}},           // 03 BolideBombardment
            {0x00225594, {0x0021B282, 0}},           // 04 CharredEcosystem
            {0x00225593, {0x0021B27C, 0}},           // 05 ContinualConductor
            {0x00225592, {0x0021B278, 0}},           // 06 CorallineLandmass
            {0x00246C66, {0x00239D8C, 0x0023CAD0}},  // 07 CrystallineCrust
            {0x00225591, {0x0021B274, 0}},           // 08 DiseasedBiosphere
            {0x00225590, {0x0021B272, 0}},           // 09 EcologicalConsortium
            {0x0022558F, {0x0021B26C, 0}},           // 10 EmergingTectonics
            {0x00290819, {0x0023875B, 0}},           // 11 EnergeticRifting
            {0x0022558E, {0x0021B268, 0}},           // 12 FrozenEcosystem
            {0x0029081B, {0x00238751, 0}},           // 13 GaseousFont
            {0x0022558D, {0x0021B264, 0}},           // 14 GlobalGlacialRecession
            {0x0022558C, {0x0021B260, 0}},           // 15 PeltedFields
            {0x0022558B, {0x0021B25C, 0}},           // 16 PrimedForLife
            {0x0029081A, {0x00239D8B, 0x0023CAD1}},  // 17 PrimordialNetwork
            {0x0022558A, {0x0021B258, 0}},           // 18 PrismaticPlumes
            {0x00225589, {0x0021B254, 0}},           // 19 PsychotropicBiota
            {0x00225588, {0x0021B250, 0}},           // 20 SentientMicrobialColonies
            {0x00225587, {0x0021B24C, 0}},           // 21 SlushySubsurfaceSeas
            {0x00225586, {0x001677C3, 0x0021B248}},  // 22 SolarStormSeasons
            {0x00225585, {0x0021B244, 0}},           // 23 SonorousLithosphere
            {0x00225584, {0x0021B240, 0}},           // 24 TurbulentLithosphere
            {0x00221980, {0x0021B23C, 0}},           // 25 ExtinctionEvent
            {0x0028515F, {0x00111F5A, 0}},           // 26 GravitationalAnomaly
        };
        const auto kwId = traitKw->GetFormID();
        for (const auto& e : kTable)
            if (e.traitKw == kwId)
                return static_cast<std::int32_t>(e.acti[slot]);
        return 0;
    }

    // TEST the user's hypothesis: write the SPECIES-STYLE 938333 PlayerKnowledge entry (+0x21 scanned /
    // +0x20 pct) for a trait scan-target's base ACTI, keyed by (planet, ACTI) — identical to the species
    // write, and the ACTI is the scan-target's canonical key (ID_64338 falls to base for ACTIs). If this
    // greens/completes the surface scan-targets like it greens species, the durable-KB path works for
    // traits; if not, it confirms the visual reads the transient 939118, not 938333.
    std::int32_t MarkScanTargetScannedForPlanet(std::monostate, RE::TESForm* planetForm, std::int32_t actiFormId)
    {
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId || !actiFormId)
            return -1;
        return Engine::MarkSpeciesScannedForPlanet(planetId, static_cast<std::uint32_t>(actiFormId), 100);
    }

    // ON-PLANET trait scan-target completion — the decompile-verified ID_90506 recipe
    // (re/ghidra/output/trait-onplanet-completion-2026-06-23.md + trait-true-completion-2026-06-23.md).
    // For a LOADED scan-target REFR:
    //   green outline + N/M count  : ID_83008(ref,1,8,0) on the FormID-correct loaded ref. Both the
    //                                outline (ID_90548[0xf2c]=ID_83007) and the count walker (ID_90522)
    //                                read 939118+0x28, keyed by REFR FormID, so this is the load-bearing call.
    //   durable %/"fully surveyed" : the ID_52157->ID_52158 fan-out (938333 +0x21/+0x20 via ID_124898/9,
    //                                ID_101322 "fully surveyed" event, and the ID_83025 identity write).
    //   Unknown -> named identity  : ID_83025(*(937609+0x160), ref, ID_83009(ref)) — fired INSIDE ID_52158
    //                                (onp-helpers:201) AND, belt-and-braces, directly via RevealScanTargetIdentity.
    //
    // ★ THE BYTE-ALREADY-SET BUG (HARD EMPIRICAL FACT 2026-06-23). When an EARLIER pass already set
    // 939118+0x28 (the player can no longer hand-scan; IsScanned()==TRUE), a bare ScanRefNative(ref,1,8,0)
    // does NOT credit the count or reveal the name: ID_83008->ID_83038 copies the canonical id out and
    // forwards to ID_52157 ONLY inside `if (*newByte != *oldByte)` (onp-resolver/scanned-state:2658-2670,
    // the `if (*pcVar6 != *(...0x28...))` block). An idempotent re-set is no change -> ID_52157 is SKIPPED
    // -> no durable 938333 write, no ID_83025 identity, no ID_101322 event -> "0/2 SCANNED" + "UNKNOWN
    // FEATURE" persist exactly as observed. FIX: call PlanetProgressNative (ID_52157) DIRECTLY after
    // ScanRefNative so the credit fan-out runs REGARDLESS of a byte transition. ID_52157 unconditionally
    // builds the ID_52158 context and calls it (decompile ID_52157 @1407b7fa0:2789-2811) + always runs the
    // survey recompute ID_97853 (:2837); ID_52158 writes 938333+0x21/+0x20 keyed by the ID_83009 canonical
    // (:123/:150), fires ID_101322 on threshold (:185-187), and writes the ID_83025 known-set (:190-201).
    // param_2 = canonical id (ID_83009); param_3 = mode 8 (matches the real scan); param_5 = 1 so the
    // survey-event sink ID_83019 also runs. The leading guard (ID_52157:2775) passes for a normal ACTI id.
    //
    // MANDATORY crash guard (§7): only write if ID_83007(ref) != 0 (a live 939118 component exists) —
    // otherwise there is no +0x28 to write and no canonical id, and the durable fan-out faults. This is
    // exactly why the prior ref-free ID_83024 path crashed. Logs per-ref FormID/state/canon so we can
    // confirm WHICH refs the find returns (the prior SetScanned hit wrong-FormID template handles).
    // Returns pre-write state: -1 null, 0 skipped (no live component), 1 was-unscanned (now scanned),
    // 2 already-scanned (the byte-already-set case the direct ID_52157 credit now fixes). Visual refresh
    // still needs a monocle repaint (look away/back) after the batch.
    std::int32_t CompleteTraitScanTargetRef(std::monostate, RE::TESObjectREFR* ref)
    {
        if (!ref)
            return -1;
        const auto refId = ref->GetFormID();
        const auto state = Engine::IsBiomeRef(ref);            // ID_83007: 0/1/2
        const auto canon = Engine::GetCanonicalSpeciesId(ref); // ID_83009 (ACTI canonical -> base)
        spdlog::info("[trait-scan] ref=0x{:08X} state={} canon=0x{:08X}", refId, static_cast<int>(state), canon);
        if (state == 0)  // no live 939118 component — do NOT proceed (crash guard)
            return 0;
        Engine::CompleteScanTargetCredit(ref, canon);          // 83008 + DIRECT 52157 credit + 83025 identity
        return static_cast<std::int32_t>(state);
    }

    // True if a loaded REFR is a TRAIT scan-target (vs flora/fauna/resource). The 31 PlanetTraitScanTarget
    // base forms are ACTIs carrying Handscanner_AllowScanAtHighlightRange (0x001CBEA3), and NOTHING else
    // does (ESM-verified 31/31 — see CompleteTraitScanTargets in the Quest script). Flora are FLOR, fauna
    // are NPC_, so the base-form-type screen alone separates them; the keyword check then confirms it is a
    // scan-target ACTI specifically and not some other ACTI. All offset-clean (CommonLibSF typed access).
    bool RefIsTraitScanTarget(RE::TESObjectREFR* ref)
    {
        if (!ref)
            return false;
        auto base = ref->GetBaseObject();  // NiPointer<TESBoundObject>
        if (!base)
            return false;
        if (base->GetFormType() != RE::FormType::kACTI)
            return false;  // flora (kFLOR) / fauna (kNPC_) / resources are excluded here
        // ACTI derives from BGSKeywordForm — confirm the scan-target keyword. Fail closed if the keyword
        // form cannot be resolved (do not treat arbitrary ACTIs as trait scan-targets).
        auto* acti = static_cast<RE::TESObjectACTI*>(base.get());  // ACTI -> BGSKeywordForm sub-object
        auto* kw   = RE::TESForm::LookupByID<RE::BGSKeyword>(Engine::kHandscannerHighlightRangeKw);
        if (!kw)
            return false;
        return static_cast<RE::BGSKeywordForm*>(acti)->HasKeyword(kw);
    }

    // READ-ONLY diagnostic: walk the engine's GLOBAL 939118 ScannableComponent registry (the EXACT store
    // the outline reader ID_83007 and the count walker ID_90522 both read) and PROBE every loaded trait
    // scan-target in range. Because we enumerate the registry itself — not a keyword ref-find — there is
    // ZERO "wrong instance" risk: FindAllReferencesWithKeyword can miss the rendered overlay instances,
    // but the registry holds exactly one entry per loaded 939118 component, which is what both the count
    // and the outline key on.
    //
    // For each resolved REFR: guard IsBiomeRef(ref)!=0 (a live 939118 component must exist — mandatory
    // crash guard, §7 of trait-onplanet-completion-2026-06-23.md), filter to TRAIT scan-targets only
    // (RefIsTraitScanTarget: base is a PlanetTraitScanTarget ACTI carrying kw 0x001CBEA3), distance-gate
    // against `player` when non-null, then ProbeScanTargetKnownSet(ref) only (known-set member state log;
    // NO ScanRefNative / ID_83008 / RevealScanTargetIdentity writes). The whole walk is fault-guarded
    // (/EHa try/catch in ForEachScannableInRegistry, plus the per-native CPS_GUARDED wrapper). Returns
    // the number of trait scan-targets probed.
    std::int32_t CompleteTraitScanTargetsInRange(std::monostate, RE::TESObjectREFR* player, float radiusUnits)
    {
        const bool  haveRadius = (player != nullptr) && (radiusUnits > 0.0f);
        const auto  playerPos  = player ? player->GetPosition() : RE::NiPoint3 {};
        const float radiusSq   = radiusUnits * radiusUnits;

        std::int32_t probed  = 0;
        std::int32_t scanned = 0;  // registry entries that were still unscanned (state was 1)

        const auto visited = Engine::ForEachScannableInRegistry(
            [&](std::uint32_t formID, std::uint8_t stateByte, RE::TESObjectREFR* ref)
            {
                if (!ref)
                    return;

                // Mandatory crash guard: a live 939118 component must exist (ID_83007 != 0). This is the
                // SAME gate that prevents the ref-free ID_83024 fault; without it the durable fan-out and
                // canonical read dereference a missing component.
                const auto biomeState = Engine::IsBiomeRef(ref);  // 0 = none, 1 = unscanned, 2 = scanned
                if (biomeState == 0)
                    return;

                // TRAIT filter: skip flora/fauna/resources — only probe trait scan-targets.
                if (!RefIsTraitScanTarget(ref))
                    return;

                // Distance gate (only when a player + positive radius were supplied).
                float distSq = 0.0f;
                if (haveRadius)
                {
                    const auto p  = ref->GetPosition();
                    const float dx = p.x - playerPos.x;
                    const float dy = p.y - playerPos.y;
                    const float dz = p.z - playerPos.z;
                    distSq = dx * dx + dy * dy + dz * dz;
                    if (distSq > radiusSq)
                    {
                        spdlog::info("[trait-walk] formID=0x{:08X} state={} dist={} (out of range, skipped)",
                                     formID, static_cast<int>(biomeState), static_cast<int>(std::sqrt(distSq)));
                        return;
                    }
                }

                spdlog::info("[trait-walk] formID=0x{:08X} state={} rawByte={} dist={}",
                             formID, static_cast<int>(biomeState), static_cast<int>(stateByte),
                             haveRadius ? static_cast<int>(std::sqrt(distSq)) : -1);

                // THE FIX: write the ref-keyed ID_938083 "seen" byte (entry+0xb9 = 1) — the durable
                // LocationManager store a real scan writes that the planet-keyed 938333 does NOT, the
                // piece that leaves the in-world object stuck at 0/N on reload (decompile ID_57033:103,
                // in-game: 938333-complete colony still 0/2). NO 939118 jam byte, so the hand-scanner is
                // never bricked. The registry gives the rendered ref's correct FormID (the 938083 key),
                // so no wrong-instance miss. A ref with no 938083 entry (not materialized) no-ops.
                ++probed;
                if (Engine::MarkScanTargetLocationDurable(formID) == 1)
                    ++scanned;  // had a 938083 entry and we set its seen byte
            });

        spdlog::info("[trait-walk] registry walk visited {} entries, {} trait scan-targets, {} marked seen (938083)",
                     visited, probed, scanned);
        return scanned;
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

    // THE DECISIVE PROBE (build-skew already ruled out 1.16.244). Call AFTER the Papyrus side has
    // PlaceAtMe'd a species instance AND driven the REAL create path (ObjectReference.SetScanned,
    // = ID_118472 -> ID_83005, which stamps the ScannableComponent +0x24 canonical) AND waited a
    // beat for the deferred create to flush. We read three keys off the SAME live instance:
    //   authored  = the ESM/PPBD species formId we placed
    //   base      = ref->GetBaseObject()->GetFormID() (what the renderer is claimed to hash, base+0x28)
    //   canonical = ID_83009(ref) -> ScannableComponent +0x24 (what the real scan keys +0x21 on)
    // Outcomes:
    //   canonical == INSTANCE (0xFF...) -> the create path did NOT flush (no component) -> need a
    //       longer wait or a different create trigger; the canonical is unobtainable this way.
    //   canonical == authored (== base) for all -> ID_83006 is IDENTITY -> the off-planet AUTHORED
    //       write was the right key, so the blue is NOT a key-domain bug (-> planet-binding /
    //       entry-rematerialize / ID_52180 membership). Stop chasing the canonical.
    //   canonical != authored -> a REAL remap exists; THIS canonical is the key to cache + write
    //       ref-free per host planet (the probe-once-cache-replay lead).
    void ProbeScanKeys(std::monostate, RE::TESObjectREFR* ref, std::int32_t authoredFid)
    {
        if (!ref)
            return;
        const std::uint32_t refId    = static_cast<std::uint32_t>(ref->GetFormID());
        const auto          base     = ref->GetBaseObject();  // NiPointer<TESBoundObject>
        const std::uint32_t baseId   = base ? static_cast<std::uint32_t>(base->GetFormID()) : 0u;
        const std::uint32_t canon    = Engine::GetCanonicalSpeciesId(ref);  // ID_83009 -> +0x24
        const std::uint32_t authored = static_cast<std::uint32_t>(authoredFid);
        const char* baseTag  = (baseId == authored) ? "==authored" : "!=authored";
        const char* canonTag = (canon == 0)        ? "NULL"
                             : (canon == refId)     ? "==INSTANCE(no-component!)"
                             : (canon == authored)  ? "==authored(IDENTITY)"
                             : (canon == baseId)    ? "==base"
                                                    : "REMAP(distinct)";
        spdlog::info("ProbeScanKeys: authored=0x{:08X} base=0x{:08X}({}) canonical=0x{:08X}({}) instance=0x{:08X}",
                     authored, baseId, baseTag, canon, canonTag, refId);
    }

    // THE PLANET-KEY FIX TEST. The diagnosis: the outline reads the green entry keyed by
    // (938333 | ID_52188(player)) — the BSGalaxy NumericID — NOT (938333 | planetForm+0x54) the data
    // path uses. This writes +0x21 under the RENDER planet id for the current planet's species, and
    // logs both ids so we see the domain split. If, after this, a SAVE->reload renders GREEN where
    // TestDirectGreen (+0x54) was blue, the planet key was the entire bug. Species are still looked up
    // by the +0x54 form id (the ESM map domain), but WRITTEN under the render id.
    std::int32_t TestRenderKeyGreen(std::monostate, RE::TESObjectREFR* playerRef, RE::TESForm* planetForm)
    {
        if (!playerRef || !planetForm)
            return -1;
        const auto formId   = Engine::ReadPlanetId(planetForm);       // +0x54 (data / survey-% domain)
        const auto renderId = Engine::GetRenderPlanetId(playerRef);   // ID_52188 (outline domain)
        spdlog::info("TestRenderKeyGreen: formId(+0x54)=0x{:08X} renderId(ID_52188)=0x{:08X}{}",
                     formId, renderId, (formId != renderId) ? " (DIFFERENT -> confirms the 2 domains)" : " (SAME)");
        if (!renderId)
        {
            spdlog::warn("TestRenderKeyGreen: ID_52188 returned 0 — not on a resolved planet");
            return 0;
        }
        const auto& m  = Esm::GetPlanetSpecies();
        const auto  it = m.find(formId);
        if (it == m.end())
            return 0;
        int n = 0;
        for (const auto sf : it->second)
            if (Engine::MarkSpeciesScannedForPlanet(renderId, sf, Engine::kDefaultScanDelta) == 1)
                ++n;
        spdlog::info("TestRenderKeyGreen: wrote +0x21 under renderId=0x{:08X} for {} species", renderId, n);
        return n;
    }

    // PROBE (read-only, the definitive one): ask the engine's OWN outline-green reader (ID_52159)
    // what it returns for each authored species of the current planet, for THIS player. Cuts through
    // the unreliable decompile by calling the actual render-decision function.
    //   - Run AFTER CompleteSurvey (a GREEN planet) -> expect "GREEN for N/N". Confirms ID_52159 IS
    //     the read and that it returns nonzero where the outline is green.
    //   - Run AFTER TestDirectGreen (a BLUE byte-poke planet) -> if "GREEN for 0/N", the render reads
    //     a DIFFERENT slot/species-id than our authored write (e.g. the wild creature's render id),
    //     pinpointing exactly where the green really lives.
    std::int32_t ProbeRenderRead(std::monostate, RE::TESObjectREFR* playerRef, RE::TESForm* planetForm)
    {
        if (!playerRef || !planetForm)
            return -1;
        const auto  formId = Engine::ReadPlanetId(planetForm);
        const auto& m      = Esm::GetPlanetSpecies();
        const auto  it     = m.find(formId);
        if (it == m.end())
            return 0;
        int green = 0;
        for (const auto sf : it->second)
        {
            const auto r = Engine::ReadRenderGreen(playerRef, sf);
            if (r != 0)
                ++green;
            spdlog::info("ProbeRenderRead: species=0x{:08X} ID_52159=0x{:02X}{}", sf, r,
                         (r != 0) ? " (GREEN)" : " (blue)");
        }
        spdlog::info("ProbeRenderRead: ID_52159 returns GREEN for {}/{} authored species on planet 0x{:08X}",
                     green, static_cast<int>(it->second.size()), formId);
        return green;
    }

    // DUMP the raw species DB state for diffing. Run it (a) right after TestDirectGreen (half-scan)
    // and (b) right after CompleteSurvey (full scan: green + info + XP) on the SAME planet — the byte
    // difference in the per-species slot (and the subobj header / +0x60 tree region) is the missing
    // "species catalogued/known" record the real scan writes. Read-only; mirrors ID_52159's lookup.
    std::int32_t DumpSpeciesSlots(std::monostate, RE::TESForm* planetForm)
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
            spdlog::info("DumpSpeciesSlots: no knowledge entry for planet 0x{:08X}", planetId);
            return 0;
        }
        const auto         base = reinterpret_cast<std::uintptr_t>(subobj);
        static const char* H    = "0123456789ABCDEF";
        auto               hexdump = [&](std::uintptr_t addr, int len) {
            std::string s;
            s.reserve(static_cast<std::size_t>(len) * 3);
            const auto* p = reinterpret_cast<const std::uint8_t*>(addr);
            for (int i = 0; i < len; ++i)
            {
                s += H[p[i] >> 4];
                s += H[p[i] & 0xF];
                s += ' ';
            }
            return s;
        };

        // subobj header incl. the subobj+0x60 scanned-species tree region — catches subobj-level state.
        spdlog::info("DumpSpeciesSlots: planet=0x{:08X} subobj[0x00..0x70]=[ {}]", planetId, hexdump(base, 0x70));

        const auto  hashmap = base + 0x18;
        const auto  end     = *reinterpret_cast<std::uint64_t*>(base + 0x48);
        const auto  slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);
        const auto& m       = Esm::GetPlanetSpecies();
        const auto  it      = m.find(planetId);
        if (it == m.end())
            return 0;
        int dumped = 0;
        for (const auto sf : it->second)
        {
            // Resolve the slot under the CANONICAL key — the same key TestDirectGreen/TestBuildArray
            // write +0x21/+0x08 under. Keying by the raw ESM id mis-reads (or misses) the slot for any
            // remapped species, so the dump would lie about what the writers actually built.
            auto* const  form = RE::TESForm::LookupByID(sf);
            std::uint32_t key = form ? Engine::CanonicalFormId(form) : 0;
            if (key == 0)
                key = sf;
            const auto idx = Engine::SpeciesSlotHash(hashmap, &key);
            if (idx == end || !slots)
            {
                spdlog::info("DumpSpeciesSlots: species=0x{:08X} key=0x{:08X} NO-SLOT", sf, key);
                continue;
            }
            const auto slotAddr = slots + idx * 0x30;
            spdlog::info("DumpSpeciesSlots: species=0x{:08X} slot[0x00..0x30]=[ {}]", sf, hexdump(slotAddr, 0x30));
            // Deref the +0x08 BSTArray {begin,end,cap} and dump its u32 contents — the species'
            // catalogue ids the full scan builds and our poke leaves NULL.
            const auto arrBegin = *reinterpret_cast<const std::uintptr_t*>(slotAddr + 0x08);
            const auto arrEnd   = *reinterpret_cast<const std::uintptr_t*>(slotAddr + 0x10);
            if (arrBegin != 0 && arrEnd > arrBegin && (arrEnd - arrBegin) <= 0x200 && ((arrEnd - arrBegin) % 4) == 0)
            {
                const auto  count = (arrEnd - arrBegin) / 4;
                const auto* ids   = reinterpret_cast<const std::uint32_t*>(arrBegin);
                std::string s;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto v = ids[i];
                    for (int n = 28; n >= 0; n -= 4)
                        s += H[(v >> n) & 0xF];
                    s += ' ';
                }
                spdlog::info("DumpSpeciesSlots:   species=0x{:08X} +0x08 array[{}] u32s=[ {}]", sf, count, s);
            }
            ++dumped;
        }
        spdlog::info("DumpSpeciesSlots: dumped {} slots for planet 0x{:08X}", dumped, planetId);
        return dumped;
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
        const auto hashEnd = *reinterpret_cast<std::uint64_t*>(base + 0x48);
        const auto slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);
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
        int built = 0;
        for (const auto sf : it->second)
        {
            if (!Engine::SpeciesMatchesKind(sf, kind))
                continue;  // kind filter: flora-only / fauna-only
            auto* const form = RE::TESForm::LookupByID(sf);
            if (!form)
                continue;
            // Slot keyed by the SAME key TestDirectGreen wrote +0x21 under (green needs +0x21 AND +0x08
            // on ONE slot). CanonicalFormId is 0/NO-CANON for bare forms, so both fall back to the raw
            // ESM id identically — the slots line up.
            std::uint32_t key = Engine::CanonicalFormId(form);
            if (key == 0)
                key = sf;
            const auto idx = Engine::SpeciesSlotHash(hashmap, &key);
            if (idx == hashEnd || !slots)
                continue;
            const auto slotAddr = slots + idx * 0x30;
            // Clear any existing +0x08 array (leak the old engine buffer — safe, no double-free) so we
            // rebuild cleanly (no duplicate markers on re-run).
            *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x08) = 0;
            *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x10) = 0;
            *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x18) = 0;

            std::vector<std::uint32_t> markers = Esm::GetSpeciesMarkers(sf, planetId);
            if (markers.empty())
            {
                spdlog::debug("TestBuildArray: 0x{:08X} no esm-derived markers -> skip (left blue)", sf);
                continue;
            }
            // Append the func-699 actor-scan markers (Abilities/Resistances/Weaknesses) so a creature
            // with an ability greens with its FULL set, matching a real in-game scan (the slot+0x08
            // species set alone leaves ability-creatures one marker short -> blue).
            const auto actorMarkers = Esm::GetSpeciesActorMarkers(sf);
            markers.insert(markers.end(), actorMarkers.begin(), actorMarkers.end());
            for (const auto id : markers)
                Engine::PushSpeciesAttr(slotAddr, id);
            spdlog::debug("TestBuildArray: 0x{:08X} esm-derived {} markers ({} actor) (first=0x{:08X})",
                         sf, markers.size(), actorMarkers.size(), markers[0]);
            ++built;
        }
        spdlog::debug("TestBuildArray: wrote direct +0x08 marker set for {} species on planet 0x{:08X}",
                     built, planetId);
        return built;
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

    // Set a flag for the per-frame poller to run an outline-refresh sweep on
    // nearby refs once menus are closed. Running the sweep directly from Papyrus
    // races with the scanner UI and crashes on procgen cells.
    std::int32_t ScanNearbyRefs(std::monostate)
    {
        Engine::g_pendingOutlineSweep.store(true, std::memory_order_release);
        spdlog::info("ScanNearbyRefs: queued pending sweep (fires on next menu close)");
        return 0;
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

    // DURABLE trait scan-target completion — the write a REAL scan makes to the knowledge DB
    // (938333), WITHOUT the transient 939118+0x28 byte (the jammer the old path also wrote). For
    // ONE scan-target ACTI on a planet: per-canonical slot +0x21=2 / +0x20=100 + the trait keyword
    // appended to the pooled subobj+0x08 BSTArray. This record is byte-equal to a real 2/2 scan
    // (re/save/compare_save21.py: mod == real Save14). Ref-free, all-planets — the planet's knowledge
    // entry must already exist (current planet, or DiscoverPlanetEntry first). The point (per the
    // user, in-game): a CORRECT durable write removes the "0/N scan required" state on reload — no
    // jam byte, so the hand-scanner is never bricked. Returns 1 on write, 0 if the slot didn't resolve.
    std::int32_t CompleteTraitObjectSlot(std::monostate, RE::TESForm* planetForm, std::int32_t actiFormId, RE::BGSKeyword* traitKw)
    {
        if (!planetForm || actiFormId == 0 || !traitKw)
            return 0;
        const auto planetId = Engine::ReadPlanetId(planetForm);
        if (!planetId)
            return 0;
        const bool ok = Engine::CompleteTraitSlot(planetId, static_cast<std::uint32_t>(actiFormId),
                                                  static_cast<std::uint32_t>(traitKw->GetFormID()));
        return ok ? 1 : 0;
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
        // Only barren bodies (no flora/fauna) reach the finalize pass now — the sweep skips living
        // worlds — so we deliberately do NOT mark any flora/fauna here. That would write the
        // invalid "scanned but blue" state. Living worlds are greened on-planet via CompleteSurvey.
        const auto marked = Engine::CompletePlanetSurveyState(planetId);
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

    // Green ONE species type on EVERY planet that hosts it, using `ref` (a live spawned instance
    // of that species) as the canonical-id source. The persistent green is the +0x21 scan-flag in
    // BSGalaxy::PlayerKnowledge, keyed by (planet, CANONICAL species id) — and the outline renderer
    // (ID_52159) reads it for the player's CURRENT planet using the rendered instance's canonical
    // id. So to green a TARGET planet ref-free we write +0x21 DIRECTLY for (targetPlanet, canonical)
    // — no spoofing "the planet you're on", no driving ID_52158 (which re-reads the current biome
    // and fired the same wrong key). The ONLY fix vs the old sweep is using the canonical id, not
    // the ESM id. Returns the number of planets actually written. See COMPLETE-scan-to-green-trace.
    std::int32_t GreenSpeciesEverywhere(std::monostate, RE::TESObjectREFR* ref, std::int32_t speciesFid)
    {
        if (!ref || speciesFid == 0)
            return 0;
        const auto  esmFid = static_cast<std::uint32_t>(speciesFid);
        const auto& inv    = Engine::GetSpeciesToPlanets();
        const auto  it     = inv.find(esmFid);
        if (it == inv.end())
            return 0;

        // Register the live instance the way CompleteSurvey (the proven-working per-planet green)
        // does, so its ScannableComponent — and the canonical id at +0x24 — is populated for
        // ID_83009 to read. Side effect: greens the CURRENT planet for this species (harmless).
        Engine::ScanRefNative(ref, 1, Engine::kBiomeScanCategory, 0);
        Engine::UpdatePlanetProgress(ref, esmFid);

        // The canonical id the renderer will hash for fresh instances. Key the +0x21 write by THIS,
        // not the ESM id, or leveled/template fauna stay blue. Fall back to ESM if unresolved.
        std::uint32_t canonicalId = Engine::GetCanonicalSpeciesId(ref);
        if (canonicalId == 0)
            canonicalId = esmFid;

        // One diagnostic line per unique species. "(REMAPPED)" => canonical != ESM, i.e. this
        // species was the kind silently breaking the green. If a retest is still blue, this log
        // tells us definitively whether the key remapped — no more guessing.
        spdlog::info("GreenSpeciesEverywhere: esmFid=0x{:08X} canonicalId=0x{:08X}{} -> {} host planets",
                     esmFid, canonicalId, (canonicalId != esmFid) ? " (REMAPPED)" : "",
                     it->second.size());

        std::int32_t greened = 0;
        for (const auto planetId : it->second)
            if (Engine::MarkSpeciesScannedForPlanet(planetId, canonicalId, Engine::kDefaultScanDelta) == 1)
                ++greened;
        return greened;
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
            "CompletePlanetSurveyNative"sv, "GetTraitScanTargetActi"sv, CPS_GUARDED(GetTraitScanTargetActi),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "MarkScanTargetScannedForPlanet"sv,
            CPS_GUARDED(MarkScanTargetScannedForPlanet), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CompleteTraitScanTargetRef"sv,
            CPS_GUARDED(CompleteTraitScanTargetRef), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CompleteTraitScanTargetsInRange"sv,
            CPS_GUARDED(CompleteTraitScanTargetsInRange), std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "TestDirectGreen"sv, CPS_GUARDED(TestDirectGreen),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "ProbeScanKeys"sv, CPS_GUARDED(ProbeScanKeys),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "TestRenderKeyGreen"sv, CPS_GUARDED(TestRenderKeyGreen),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "ProbeRenderRead"sv, CPS_GUARDED(ProbeRenderRead),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "DumpSpeciesSlots"sv, CPS_GUARDED(DumpSpeciesSlots),
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
            "CompletePlanetSurveyNative"sv, "CompleteTraitObjectSlot"sv, CPS_GUARDED(CompleteTraitObjectSlot),
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
            "CompletePlanetSurveyNative"sv, "EnumerateLifePlanets"sv, CPS_GUARDED(EnumerateLifePlanets),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GetLifePlanetFormIdAt"sv, CPS_GUARDED(GetLifePlanetAt),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "CategoryEnabled"sv, CPS_GUARDED(CategoryEnabled),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "GreenSpeciesEverywhere"sv, CPS_GUARDED(GreenSpeciesEverywhere),
            std::optional<bool> {true}, false);

        ivm->BindNativeMethod(
            "CompletePlanetSurveyNative"sv, "ScanNearbyRefs"sv, CPS_GUARDED(ScanNearbyRefs), std::optional<bool> {true}, false);

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
                        DispatchPapyrusStatic("_AutoCompleteCurrentPlanet");
                        spdlog::info("Poller: dispatched _AutoCompleteCurrentPlanet (scanner closed)");
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
