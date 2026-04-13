# Starfield Planet Survey Internals (1.16.236.0)

Reverse-engineered notes on how the planet-survey system stores state and
computes the UI percentage. Written from Ghidra analysis of Starfield.exe
1.16.236.0 during the Complete Planet Survey mod build; applies until
Bethesda reshuffles offsets.

## Architecture summary

Survey state is a **BSComponentDB2** component (`BSGalaxy::PlayerKnowledge`)
attached per body. It is **not** on `BGSPlanet::PlanetData` (that struct is
shared immutable data). Two discriminator globals are in play:

- `ID_938333` (uint16, `.bss`) — trait / per-planet-progress discriminator
- `ID_939118` (uint16, `.bss`) — per-reference scan-state discriminator

Both are populated at runtime during component-type registration (the
`BSComponentDB2::Detail::ComponentFactoryImpl_*` symbols at RTTI IDs 867581
and friends). Reading them before registration yields `0`.

The knowledge manager singleton: `ID_126578()` returns a manager pointer.
`manager + 0x8B0` is the DB pointer. `db + 0x268` is the `BSTHashMap`
keyed by the 64-bit composite `(disc << 48) | (lower_id << 16)`.

Lookup: `ID_126806(container, out[4], &key)`
- `out[3]` is the sentinel on miss (`0xfe0`) or an index on hit
- `out[2]` is the base (chunk pointer)
- Entry pointer = `out[2] + *(uint16*)(out[2] + 0x12 + out[3] * 4)`

## Per-planet component value layout (key = (938333, planet_id))

Given `value` = entry pointer computed above:

- `value + 0x20`: "subobj" — the planet-scoped knowledge bitfield + species
  table. Passed around as `lVar26` in the engine and used as `param_1` to
  the direct setters `ID_124898` / `ID_124899`.
- `subobj + 0x18`: embedded BSTHashMap (species id -> slot index)
- `subobj + 0x20`: uint32 bitfield of planet-level boolean flags (bits
  toggled by `ID_52153`, enum values 0,1,2,3,6 map to bits 1,2,3,4,14)
- `subobj + 0x24`: uint32 count (unclear; referenced in ID_83038 output)
- `subobj + 0x28`: per-species flag byte (the byte the scanner writes)
- `subobj + 0x40`: slot array base (entries stride 0x30)
- `subobj + 0x48`: slot array end sentinel
- Each slot: `+0x00` species_id (uint32), `+0x21` scan-flag byte.
  A known species has this byte >= a threshold from ID_69506 / ID_69507.

`value + 0x28` is a **separate** `BSTArray<uint32>` of known form IDs used by
`SetTraitKnown` (ID_52155 / ID_52156). Traits land here via the event
dispatcher path. Writing a flora/fauna formID here does **not** advance the
flora/fauna counts — those come from the `subobj` slot array.

## Per-reference component value layout (key = (939118, ref_formID))

Only exists for refs the engine procedurally populated in a biome. Writing
at this key no-ops for `PlaceAtMe`'d refs (no component entry). Fields at
`value + 0x20` (flag byte) and `value + 0x24` (count). ID_83038 is the
writer — called by ID_83008 which is called by the Papyrus `SetScanned`
native.

## Key functions (Address Library IDs, 1.16.236.0 offsets)

| ID       | Offset       | Purpose                                                   |
|----------|--------------|-----------------------------------------------------------|
| 126578   | 142401c50    | GetKnowledgeManagerSingleton (returns manager*)           |
| 126806   | 142412a30    | BSTHashMap lookup: `(container*, out[4], &key)`           |
| 124898   | 142348ad0    | "IncrementSpeciesFlag" on a subobj: `(subobj, species_id, delta, 0)` — saturates at 0xFF. Creates entry if missing. |
| 124899   | 142348c20    | Sibling setter for a different field at entry+0x20        |
| 124901   | 142348da0    | BSTHashMap find-by-u32-key (ID_52154 / ID_97851 use it)   |
| 52154    | 1407b7730    | `IsTraitKnown` inner (reads known-forms list at +0x28)    |
| 52155    | 1407b78d0    | `SetTraitKnown` inner (writes + fires event)              |
| 52156    | 1407b7b90    | "AddOrRemoveKnownFormID" on the +0x28 list                |
| 52157    | 1407b7fa0    | Per-planet progress update wrapper (calls 52188 + 52158)  |
| 52158    | 1407b81c0    | Per-planet progress writer (updates slot array flag)      |
| 52188    | 1407bd600    | ResolveRefToPlanetIds: (ref, &out1, &out2) — uses ExtraData 0x81 on ref+0xC8 first, then parentCell fallback. Gate for 52157. |
| 56990    | 140910f20    | Fallback planet resolver (called by 52188)                |
| 83007    | 1413076d0    | IsRefScanned: checks base-form type (0x2a = actor) and per-ref component |
| 83008    | 141307910    | SetScanned inner: `(ref, flag, 0x0d, 0)` — dispatches to flora (83038 + 52157) or actor (52160) path |
| 83038    | 14130a600    | Flora per-ref scan writer: `(db, {flag, out_count}, &ref_formID)` |
| 114885   | 141f1ad60    | Planet::GetKeywordTypeList Papyrus impl                   |
| 114887   | 141f1b750    | Planet::GetSurveyPercent Papyrus impl (2-liner; wraps 1016657) |
| 114890   | 141f1b9a0    | Planet::IsTraitKnown Papyrus impl                         |
| 114891   | 141f1b9f0    | Planet::SetTraitKnown Papyrus impl                        |
| 114893   | 141f1ba70    | Planet Papyrus-native registrar (binds all Planet methods) |
| 118472   | 1420845e0    | ObjectReference::SetScanned Papyrus impl                  |
| 118497   | 1420881f0    | ObjectReference Papyrus-native registrar                  |
| 1016657  | 1417d8e80    | SurveyAggregator ctor: fills buffer with planet's tracked form IDs. Buffer needs >=0x250 bytes; 0x400 is safe. |
| 65318    | *(various)*  | Aggregator buffer free (pair with 1016657)                |
| 97850    | 1417d9ea0    | Survey percent compute: numerator/denominator sums        |
| 97851    | 1417da810    | Survey reader: populates the aggregator buffer's four form-id arrays from the DB |
| 97853    | 1417daca0    | Survey check-and-notify: rebuilds buffer, compares to prev, fires progress / completion events. Required to generate the "Survey Data" slate in inventory. |
| 938333   | 1461e9b94    | Trait / per-planet-progress discriminator (uint16)        |
| 939118   | 1461f68c8    | Per-reference scan-state discriminator (uint16)           |

## Aggregator buffer layout (output of ID_1016657)

Fields relevant for enumeration after calling `ID_1016657(buf, planet_id)`:

- `buf + 0x1c8`: planet_id (uint32; you wrote it in via param_2)
- `buf + 0x1d0 / 0x1d8`: `uint64*` begin/end — some list
- `buf + 0x1b0` (byte): planet-level knowledge flag (gates +0x1d0 loop in 97851)
- `buf + 0x1b1` (byte): another planet-level flag
- `buf + 0x1b4` (int32): planet-level int
- `buf + 0x1e8 / 0x1f0`: `TESForm**` begin/end — pointer array; form IDs at `*ptr + 0x28`
- `buf + 0x200 / 0x208`: `TESForm**` begin/end — pointer array
- `buf + 0x218 / 0x220`: `uint32*` begin/end — inline form IDs (uses ID_69507 threshold)
- `buf + 0x230 / 0x238`: `uint32*` begin/end — inline form IDs (uses ID_69506 threshold)

These four arrays cover traits + per-category species (flora / fauna / resources)
for the planet, though the exact mapping per category is not fully decoded.
Marking every form-id from all four arrays is idempotent and covers every
UI category.

## Advancing the survey %% without a real scan

The byte at `subobj + slot*0x30 + 0x21` (per-species scan flag) is what
ID_97851 reads to compute completion. Two stable ways to write it:

1. **Direct**: call `ID_124898(subobj, species_id, delta, 0)`. Saturating
   increment. Requires computing `subobj` from the DB lookup above.
2. **Via ID_52158**: needs a full 48-byte ctx struct including a ref pointer
   at offset +0x10 (it dereferences). More fragile; prefer (1).

## Generating the "<Planet> Survey Data" slate on completion

`ID_97853(ctx)` is the check-and-dispatch. Minimum ctx layout:

```
struct { uint32 planet_id; float prev_pct; uint8 typeFlag; uint8 skipFlag; };
```

- `typeFlag = 0` skips the Progress event (fine for bulk writes).
- `skipFlag = 0` runs the check.

Must be called after bulk byte writes, otherwise the engine never transitions
"100%% on paper" -> "CompleteEvent fired" and the slate item is never awarded.

## Paths that **don't** work

- `Papyrus SetScanned` on a `PlaceAtMe`'d ref (initiallyDisabled = any):
  the engine has no registered `(939118, ref_formID)` component for new
  refs, so ID_83038 silently returns count=0 and ID_52157 skips the write.
- Writing to the `+0x28` known-forms list (ID_52156) for flora/fauna form
  IDs: they land in a list the UI does not aggregate.
- Calling ID_52157 with a PlaceAtMe'd ref: ID_52188 fails to resolve the
  ref to a planet (no ExtraData 0x81, cell chain doesn't lead to planet).

## Cell-side reference scan state

To stop the scanner UI from showing individual plants/creatures as
`[scannable]` after a bulk planet-complete: iterate the **currently-loaded
player cell** and call `ID_83008(ref, 1, 0x0d, 0)` on refs whose base form
is `FLOR` or `NPC_`. Those refs have registered components (real engine-
placed instances), so ID_83008's path through ID_83038 succeeds.

`cell->ForEachReference(lambda)` from CommonLibSF is the iteration API.
`TESWorldSpace::unk0D0 / unk1D0` are declared in CommonLibSF but their
types are incomplete — dereferencing their `.get()` was observed to crash.
Stick to `player->parentCell` for now.

## Named constexpr offsets in src/Main.cpp

All Ghidra-derived struct field offsets are promoted to named constants in the
`Engine` namespace so raw hex never appears in logic. Current set:

| Constant | Value | Meaning |
| --- | --- | --- |
| `kPlanetIdOffset` | `0x54` | uint32 knowledge key at `planetForm + 0x54` |
| `kManagerDbOffset` | `0x8B0` | DB ptr field within the manager singleton |
| `kDbContainerOffset` | `0x268` | `BSTHashMap<>` start inside the DB object |
| `kBucketOffsetTableOff` | `0x12` | `uint16[]` offset table start in a bucket base |
| `kEntrySubobjOffset` | `0x20` | Species subobj relative to the resolved entry ptr |
| `kFormPtrFormIdOffset` | `0x28` | `formID` field in a `TESForm*` (aggregator ptr arrays) |
| `kBiomeScanCategory` | `0x0d` | Category byte for `ScanRefNative` / `PlanetProgressNative` |
| `kAggUintSpan{0,1}{Begin,End}` | `0x218–0x238` | Aggregator buffer `uint32[]` span descriptors |
| `kAggPtrSpan{0,1}{Begin,End}` | `0x1e8–0x208` | Aggregator buffer `TESForm*[]` span descriptors |

If a future patch shifts any of these, update the constant and its comment — the
logic itself stays unchanged.

## Signatures worth locking in

```cpp
using fn_get_manager_t     = std::uintptr_t(*)();
using fn_db_lookup_t       = void*(*)(std::uintptr_t*, std::uintptr_t[4], const std::uint64_t*);
using fn_incr_flag_t       = void(*)(void* subobj, std::uint32_t species_id, std::uint8_t delta, std::uint64_t zero);
using fn_aggregator_t      = void(*)(void* buf, std::uint32_t planet_id);
using fn_survey_notify_t   = void(*)(void* ctx);
using fn_scan_ref_t        = void(*)(void* ref, char flag, std::uint8_t b3, std::uint8_t b4);
```

## Gotchas

- `REL::ID` numeric values in CommonLibSF's `IDs.h` comments (e.g. `// 92501`)
  are not the Address Library IDs we use. They're a separate (possibly PDB
  or older-version) numbering. Ignore the comments; work from the actual
  `REL::ID(N)` literal or from offset cross-references.
- Entry pointers inside the BSTHashMap are reached via a `uint16` offset at
  `out[2] + 0x12 + out[3] * 4` — not a simple multiply.
- `ID_1016657` allocates internal buffers; **must** pair with `ID_65318(buf)`
  or you leak.
- Direct writes without `ID_97853` leave the game thinking the survey is
  incomplete from a "has the completion event fired" standpoint, so the
  reward slate never appears until something else triggers the event.
