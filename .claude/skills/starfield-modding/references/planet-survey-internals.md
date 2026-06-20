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

## TWO LAYERS: survey DATA vs the green VISUAL (read this first)

The survey has **two independent state layers**, and conflating them is the
single biggest time-sink in this subsystem:

1. **DATA layer** — the survey %, the "<Planet> Survey Data" slate, the
   per-category counts ("Fauna 9/9"). Lives in the `(938333, planetId)`
   component's `subobj` slot table (`+0x21` scan-flag bytes) + the planet
   bitfield. **Fully writable ref-free** (see sections above). This is the
   actual survey completion and the reward.
2. **VISUAL layer** — the **green outline** + populated details panel the hand
   scanner draws on a *loaded* flora/fauna object. This is **per-type**,
   persisted in the save, and is **NOT** read from the `+0x21` scan flag. It
   reads a separate **red-black tree** keyed by a per-type canonical id.

Setting `+0x21` to 100 makes a planet read "100% scanned" in data and drops the
slate, but the objects on the ground **stay blue** — the outline never looks at
`+0x21`. They are different keys in different structures.

This maps exactly onto the **public gameplay model**, which is the sanity check
for the RE: you scan ~8 *separate instances* of a type, and once the type hits
100% **all members of that type render green** (in the scanner and from afar),
now and on reload; different biomes hold different species so you must visit each.
The mod forces the per-type scan requirement to **1** via game settings
(`ApplyInstantScanGameSettings`, the `instant scan` GMSTs for animals/plants).
Cross-reference:
[Bethesda support — Scanning/Surveying](https://help.bethesda.net/app/answers/detail/a_id/60950/~/scanning/surveying---system---starfield),
[GameRant 100%-scan guide](https://gamerant.com/starfield-how-to-100-scan-planet-flora-fauna-resources-traits/),
[Deltia's scanning guide](https://deltiasgaming.com/how-to-100-survey-planets-in-starfield-scanning-guide/).

## The green scanner outline (the VISUAL layer)

### Read path

`ID_83007(refr)` is the scanner's "is this scanned?" check. It branches on
`ID_36022(*(refr + 0xc8), 0x2a)` — a property-bit test on the base form:

- **branch A** (`cVar3 == 0`): per-OBJECT — reads a `(939118, *(refr+0x28))`
  component's `+0x28` byte. `*(refr+0x28)` is the **spawned-instance** FormID
  (`0xFF……`, verified in-game), so this branch is per-instance.
- **branch B** (`cVar3 != 0`): per-TYPE.
  - dynamic instance id (`0xFF……`) → **`ID_52162`**
  - static base id (`0x00……`) → `ID_52159` (reads the `+0x21` survey flag)

Loaded flora/fauna instances are dynamic, so they take **`ID_52162`**:

1. Look up `*(refr+0x28)` (the instance FormID) in the **catalog at `db + 0x3d8`**
   (a `BSComponentDB2 DisposableInstancedFormDB`: instance→type-key, auto-populated
   when the instance spawns, disposed on unload). Yields a **3-uint per-type key**
   (BSResource::ID-shaped `{file, ext, dir}`).
2. Resolve the `(938333, planetId)` survey component → walk a **red-black tree**
   at **`subobj + 0x60`** (== `entry + 0x80`) for that 3-uint key.
3. Found → green.

So the **persistent per-type green state is the tree at `subobj + 0x60`**, keyed
by the type's 3-uint canonical key. The catalog and the instance lookup are
transient, rebuilt by the engine on cell load.

> **CRITICAL — do not get this backwards.** This tree is **saved** and **per-type**.
> A planet completed in one session, after a full game restart + character load, has
> its flora/fauna spawn **GREEN** — including brand-new instances never individually
> scanned — because they resolve their type into this saved tree on load. So **you do
> NOT need the objects loaded to set the green**: write the persistent per-type state,
> and fresh instances pick it up whenever they next spawn. "You can't paint objects
> that don't exist yet" is **wrong** — the objects read a saved record, they aren't painted.
> Greening an unvisited planet is therefore possible *in principle*; the only hard part
> is writing the **correct key** (below) for an arbitrary target planet.

### The per-type canonical key (the thing that's hard to get)

- **Not on the base form.** The resource-id vfunc at TESForm vtable slot `0x15`
  (byte offset `0xa8`, the one `ID_52161` calls) returns `{0,0,0}` for `FLOR` /
  `NPC_` base forms. Confirmed in-game: even a live, *scanned* instance's
  vtable-`0x15` returns `{0,0,0}`.
- **Minted per type when an instance first goes live.** `ID_137052` mints one
  (timer + username hash → effectively random; flags it `0xa0000000`) when the
  form carries no authored id. So it **cannot be precomputed** — it only exists
  once an instance of the type is loaded.
- **Readable off a live instance.** The catalog entry at `db+0x3d8[instanceFormId]`
  holds it, present the instant the object spawns — no scan required. Read via
  `ID_126718(&db, out[4], instanceFormId)`: on hit (`out[1] != 0x7c`) the 3 uints
  are at `*out[3]`. (Observed: `out[2]` is adjacent/padding — use `out[3]`.)
- **The catalog key has a PER-INSTANCE component (proven).** Reading
  `db+0x3d8[instanceFormId]` off two spawns of the same species: the first two uints
  match (per-type), but the **third differs per spawn**, and scanning the first does
  NOT stabilise it. Since `ID_52162`'s tree search compares all three, a key harvested
  from one spawn never matches another instance. So you **cannot** hand-read a key and
  stamp it — the correct key resolution is internal to the engine's writer.

### Setting the tree — use the engine's writer, NOT a hand-written key

`ID_124902(&treeRoot, out, key3)` is the raw RB-tree insert (`treeRoot = subobj+0x60`;
node key at `+0x1c/+0x20/+0x24`; `ID_124900` is the matching find) — but it needs the
*correctly-resolved* key, which only **`ID_52161`** (the type-completion writer)
produces. So drive `ID_52161` via a REAL scan completing a type:

- **Per planet — CONFIRMED working.** Spawn one live instance of each type + scan it
  (`SetScanned` → `ID_83008` → `ID_52160` → `ID_52161`; 1 scan tips the type to 100%
  with the instant-scan setting) + `ScanNearbyRefs` to refresh already-loaded refs.
  Writes the planet's **saved** tree correctly; fresh instances — this session OR a
  future game launch — render green. This is `CompleteSurvey` in the repo.
- **All planets atomically — UNDER TEST.** `ID_52161` takes the **planet as an
  argument** (context: `planetId @0x00`, live-instance FormID `@0x10`). So drive it
  directly with an EXPLICIT target planet, using one live spawned instance per species
  as the handle, to write any planet's saved tree from one spot — no per-planet visit.
  (`Engine::GreenTypeForPlanet` + the `TestDirectGreen` validation in the repo.)

### What does NOT work (tried + removed from the repo)

- **Capture-and-stamp** (read the `db+0x3d8` key off a spawn, insert into trees with
  `ID_124902`): dead — the catalog key's 3rd component is per-instance, so a stamped
  key never matches a fresh instance. (`CaptureTypeKey`/`WriteAllPlanetGreenTrees` were
  deleted.)
- **Setting `+0x21`/`+0x20` alone** (the survey-% scan flag): completes the survey DATA
  (menus read 100%, slate drops) but never touches the green tree → ground stays blue.

### Per-instance ScannableComponent (the (939118) branch-A side)

`ID_83004` is the per-instance creator: builds `(939118, instanceFormId)` born
`+0x28 = 0` (unscanned), instance id at `entry+0x20`, base/species id at
`entry+0x24` — keyed per-instance, payload-carries the species. `ID_83043` is its
save-restore. `ID_38418` is the insert primitive (`ID_126806`/`ID_83038` are find-only).
This (939118) per-instance record is **transient** (rebuilt per load) and is **not**
what makes a completed planet green on a fresh launch — the persistent **per-type tree**
(`subobj+0x60`) is. So the visual green is set per-type via `ID_52161` (which writes that
saved tree), not by touching this per-instance component.

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
| **— green VISUAL layer —** | | (see "The green scanner outline" section) |
| 83007    | 1413076d0    | Outline "is scanned" check; branches on ID_36022(base+0xc8, 0x2a) |
| 36022    | 1402c62b0    | Base-form property-bit test; selects per-instance (A) vs per-type (B) branch |
| 52159    | 1407b8750    | Per-type check, static id → reads survey `+0x21` flag     |
| 52162    | 1407b8c50    | Per-type check, dynamic id → catalog(db+0x3d8) key → tree(subobj+0x60) search |
| 52160    | 1407b88a0    | Type-scan wrapper (calls 52188 + 52161)                   |
| 52161    | 1407b8a20    | Resolve type key + write catalog + write tree; **gated to dynamic ids** |
| 124900   | 142348d40    | Scanned-species tree **find** (sibling of 124902)         |
| 124902   | 142348e40    | Scanned-species tree **insert** (red-black) at subobj+0x60 |
| 126718   | 14240bf60    | Catalog (db+0x3d8) find by formID; type key at `*out[3]` on hit |
| 126719   | 14240c0e0    | Catalog insert (bidirectional db+0x3d8 / db+0x418)        |
| 126802   | 142412590    | Catalog (db+0x3d8) find used by 52162                     |
| 137052   | 1427d9dc0    | Mint a random canonical key (timer+username; flag 0xa0000000) |
| 46756    | 1405b32b0    | Catalog writer (CreateAndDeleteCommand, formID→BSResource::ID) |
| 83004    | 141307430    | Per-instance ScannableComponent **creator** (born +0x28=0; instance id @+0x20, species @+0x24) |
| 83043    | 14130ad90    | Per-instance ScannableComponent save-restore             |
| 38418    | *(via 83004)*| Component insert primitive (126806/83038 are find-only)   |

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

- Writing to the `+0x28` known-forms list (ID_52156) for flora/fauna form
  IDs: they land in a list the UI does not aggregate.
- Calling ID_52157 with a PlaceAtMe'd ref **to credit a remote planet**:
  ID_52188 resolves the planet from the ref's **location** (ExtraData 0x81 on
  `ref+0xc8`, else position via ID_56990) — i.e. the planet you're standing on,
  never the species' "home" planet. So spawning planet B's species under you on
  planet A and scanning credits **A**, not B.
- Greening via **hand-stamping** a key read from the `db+0x3d8` catalog: dead — that
  key's 3rd component is per-instance, so it never matches another instance. (NOTE: the
  green tree itself IS persistent + per-type + writable for any planet — the problem was
  only that we hand-wrote the *wrong key*; the engine's `ID_52161` writes the right one.)
- Reading the per-type key off the **base form** (vtable slot 0x15): returns `{0,0,0}`.
  The key is resolved internally by `ID_52161`; don't try to compute it yourself.
- Setting `+0x21`/`+0x20` alone: completes the survey % (menus 100%, slate) but not the
  green tree → ground stays blue.

## Cell-side reference scan state (per-instance branch only)

NOTE: this greens objects via the per-INSTANCE (939118) branch on the
currently-loaded cell — superseded for the bulk case by the per-TYPE tree write
(see "The green scanner outline"), which persists and covers fresh spawns. Kept
for reference / the live-cell case.

To flip already-loaded refs immediately (cosmetic refresh of what's on screen now):
iterate the **currently-loaded player cell** and call `ID_83008(ref, 1, 0x0d, 0)` on
refs whose base form is `FLOR` or `NPC_`. This is `ScanNearbyRefs` in the repo, run as a
finishing touch after the per-type tree write — the tree write is what makes *future*
spawns green; this just recolors the ones already loaded around you without waiting for a
reload.

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
| `kGreenTreeOffset` | `0x60` | scanned-species RB-tree root, relative to subobj (== entry+0x80) |
| `kResourceIdVtableIdx` | `0x15` | TESForm vtable slot for the resource-id vfunc (returns {0,0,0} for FLOR/NPC_ — key is runtime-minted, read from db+0x3d8 instead) |
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
