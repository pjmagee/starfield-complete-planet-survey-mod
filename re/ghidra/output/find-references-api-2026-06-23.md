# FindAllReferencesWithKeyword / OfType — decompile verdict (2026-06-23)

Game: Starfield.exe 1.16.236 (byte-identical to 1.16.244). Ghidra project
`ghidra-project/Starfield`. All IDs below are Address Library `ID_<n>` (REL::ID).

## VERDICT: WILL-FIND

`ObjectReference.FindAllReferencesWithKeyword(kw, radius)` enumerates the LIVE
per-cell reference arrays of every loaded cell in the grid (plus the worldspace
persistent/sky cell), then matches each ref by its **base form's keyword set via
the ref's virtual keyword interface**, and appends every match to an unbounded
output array. A procedurally-instantiated surface scan-target REFR — whose own
FormID carries no keyword but whose base `PlanetTraitScanTarget` ACTI carries
`Handscanner_AllowScanAtHighlightRange` (0x001CBEA3) — IS in its cell's ref array
once 3D-loaded, so it IS matched and returned. There is no authored-vs-runtime
distinction in the enumeration, no result-count cap. The only practical gates are
(a) the ref must be loaded (have 3D / cell-attached), (b) the enabled flag, and
(c) the radius. Set radius generously (cylindrical/2D on the planet surface) and
be standing in the overlay area so the host cell is loaded.

## Native REL::IDs (proof anchors)

| ID | role |
|----|------|
| `ID_118279` @ 142070630 | **Papyrus impl** `TESObjectREFR::FindAllReferencesWithKeyword` (owns the error string "None or invalid form passed in to FindAllReferencesWithKeyword") |
| `ID_118278` @ 14206fa20 | **Papyrus impl** `FindAllReferencesOfType` |
| `ID_118497` @ 1420881f0 | ObjectReference Papyrus binding **registrar** (registers both names; that is why both literal strings xref here) |
| `ID_63482` @ 140b59470 | interior-vs-exterior dispatch (reads calling ref parent cell `+0xb0`) |
| `ID_46201` @ 14058ad30 | **exterior/worldspace grid enumerator** (planet surface path) — keyword variant |
| `ID_46199` @ 14058a9d0 | exterior grid enumerator — OfType variant |
| `ID_46103` @ 140586820 | interior-cell enumerator |
| `ID_46167` @ 1405894d0 | OfType cell-AABB walk entry |
| `ID_46100` @ 140586180 | grid + worldspace-cell walk (loaded-cell DB + persistent cell) |
| `ID_46231` @ 14058d2e0 | `FilterCellByAABB` — per-cell coord/AABB pre-filter, then ForEachReference |
| `ID_63054` @ 140b1e8e0 | **`TESObjectCELL::ForEachReference`** — walks the cell ref array `cell+0x80`/`cell+0x88` |
| `ID_64473` @ 140bb5c80 | get worldspace persistent/sky cell (`tes+0x188`) |
| `ID_46239` @ 14058d850 | `FilterRefsByRadius::operator()` thunk → ID_46104 |
| `ID_46104` @ 140586990 | **radius test** (2D or 3D distance vs radius²) → inner functor |
| `ID_46176` @ 140589b10 | **`FilterReferencesForAllKeywordsOfSet<CollectReferences>::operator()`** — the KEYWORD match + collect (keyword path) |
| `ID_46091` @ 140584f00 | `CollectReferences` (OfType path) — nearest-by-3D-distance collect |
| `ID_115108` @ 141f333a0 | native NiPointer array → Papyrus VMArray result builder |
| `ID_63342` @ 140b42e30 | `TESObjectREFR::AddKeyword` — uses the SAME `REFR+0x68` keyword virtual (corroborates base-delegation) |

Filter-functor vtables (resolved from ID_118279 asm + DumpVtable):
- `0x144b2aca8` = `FilterRefsByRadius<FilterReferencesForAllKeywordsOfSet<CollectReferences>>::vftable`; slot **+0x08 = ID_46176** (inner keyword functor).
- `0x144b2a860` = `LocalFunctionImpl<reference_wrapper<FilterRefsByRadius<...>>>::vftable`; slot **+0x10 = ID_46239** (the per-ref callback ID_63054 invokes).

## Enumeration mechanism — walks LIVE cell ref arrays (not a static/authored list)

`ID_118279`:
- L67-72: validates the keyword arg is form-type `0x04` (KYWD) or `0x69`='i' (FLST);
  builds a scrap array of the keyword(s) (L81-133 — single KYWD, or iterate the
  FLST's array `param_5[8]`).
- L137 `ID_63482(param_4,0)` decides interior vs exterior from the *calling ref's*
  parent cell. L158: `cVar9=='\0'` (exterior, i.e. on a planet surface) →
  `ID_46201(ID_883589=TES, &functor, refpos=param_4+0x8c, radius=param_6)`.

`ID_46201` (exterior, the surface path):
- L36-46: `do { lVar5 = *(*(tes+0x30) + i*8); ... ID_46231(&aabb,...) } while`,
  bounded by `*(tes+0x44)` — this is the **loaded grid-cell array**. Every loaded
  cell in the grid is visited.
- L55-64: additionally walks the worldspace persistent/sky cell
  (`tes+0x188` → `ID_64473` → `ID_63054`).

`ID_46231` (`FilterCellByAABB`): if the cell's grid coords (`cell+0x58` → `+0x24/+0x28`)
overlap the query AABB, calls `ID_63054(cell, functor, 0)`.

`ID_63054` (`TESObjectCELL::ForEachReference`): iterates the cell's reference array
— count `*(cell+0x80)`, data `*(cell+0x88)` — backward, and for each entry invokes
the functor `(*functor+0x10)(functor,&ref)`. Per-ref gate (L345-347):
`ref != 0 && (includeDisabled || (ref+0x20 bit3)) && *(ref+0x98) != 0`. `ref+0x98`
is the ref's loaded-data / cell-attach pointer. **This is the live runtime ref
array of the cell — any REFR that has been instantiated into the loaded cell
(including procedurally-spawned overlay scan targets) is enumerated. There is no
test of FormID origin, no "authored-only" filter.** A template REFR that is NOT
3D-loaded fails the `ref+0x98 != 0` gate and is skipped — consistent with the
in-game probe where the authored static FormIDs (un-materialized templates) were
not found, while the materialized surface instances are.

## Filter mechanism — matches the BASE form's keyword set

`ID_46104` (radius): computes squared distance ref↔center (`ref+0x8c/0x90/0x94` =
ref position; 2D when the struct's 2D byte is set, else 3D), and if within
radius² calls `(*functor+0x08)(functor,&ref)` = **ID_46176**.

`ID_46176` (keyword match + collect), confirmed from asm `findrefs-asm46176-2026-06-23.txt`:
```
MOV  RCX,[R14]        ; RCX = the REFR (*param_2)
ADD  RCX,0x68         ; RCX = REFR + 0x68  (embedded keyword-form interface 'this')
MOV  RAX,[RCX]        ; vtable of that subobject
MOV  R9,[RAX+0x8]     ; vtable slot +8 = HasKeyword(keyword, TESForm*& out)
...
MOV  RDX,[kwArray + i*8] ; the i-th keyword of the set
LEA  R8,[out]
CALL R9                 ; virtual HasKeyword on the ref
```
For each keyword in the set it virtual-calls `HasKeyword` on the ref's keyword
interface at `REFR+0x68`; if all required keywords are present (`cVar7` AND-chain),
it appends the REFR pointer (refcounted) into the `CollectReferences` output
BSTArray (L58-70) via `ID_123859` (grow-array). Output is **unbounded** — no count
cap; initial reserve 0x80, grown as needed.

Why this resolves to the BASE form: `REFR+0x68` is the TESObjectREFR's embedded
`BGSKeywordForm`-interface subobject. The IDENTICAL virtual (`*(REFR+0x68)+8`) is
used by `TESObjectREFR::AddKeyword` (`ID_63342` L147) as its "already has this
keyword?" pre-check — and Bethesda's reference keyword query merges the base
object's keyword form with any instance-added keywords. So a runtime instance with
no own keywords but a base ACTI carrying 0x001CBEA3 returns true. (`OfType`,
`ID_118278`, instead matches via `(*form+0x1e8)` IsBoundObject and `ID_46091`
collects the nearest by 3D distance — it is a base-form-id/type match, not a
keyword match, and only keeps the single closest.)

## Caps / flags that limit results in practice

- **Loaded only**: `ID_63054` skips refs with `ref+0x98 == 0` (no 3D / not
  cell-attached). The host overlay cell + the scan-target ref must be materialized
  (player standing in/near the overlay area). Un-materialized authored templates
  are correctly excluded — this is the observed behavior, not a bug.
- **Enabled gate**: `ID_63054` skips disabled refs unless an "include disabled"
  TLS flag is set (cleared by default → disabled refs skipped). Surface scan
  targets are enabled while loaded, so unaffected.
- **Radius**: real squared-distance cutoff (2D on the surface). 100000 is ample;
  it costs only iteration over loaded refs, no hard cap. The center is the calling
  ref's position (`param_4+0x8c`), so call on `playerRef`.
- **Grid extent**: only cells currently in the loaded grid + the worldspace
  persistent cell are traversed. Refs in unloaded cells beyond the grid are not
  returned regardless of radius — irrelevant for surface targets the player is
  standing among.
- **No result-count cap** for the keyword path (unbounded BSScrapArray).
- `FindAllReferencesWithKeyword` accepts a single KYWD (form type 0x04) OR an FLST
  formlist (0x69); a null/wrong-type arg logs the error and returns an empty array.

## Practical recipe (unchanged from the mod's current approach)

`playerRef.FindAllReferencesWithKeyword(kw=0x001CBEA3, 100000)` while standing in
the materialized overlay area → iterate the returned array → `SetScanned(true)`
(SetScanned→green already decompile-proven: ID_83008→ID_83038→939118+0x28→ID_83007).
The FIND is confirmed to return the loaded procedural surface scan-target
instances.

## Output files generated (re/ghidra/output/)
- findrefs-strings-2026-06-23.txt   (string xrefs → ID_118279/118278/118497)
- findrefs-impls-2026-06-23.txt     (ID_118279, ID_118278, ID_118497 decompiles)
- findrefs-enum-2026-06-23.txt      (ID_46201/46103/46167/63482)
- findrefs-cellvisit-2026-06-23.txt (ID_46231/46100/46199/63054/64473)
- findrefs-predicate-2026-06-23.txt (ID_46239)
- findrefs-radiuskw-2026-06-23.txt  (ID_46104)
- findrefs-kwmatch-2026-06-23.txt   (ID_46176, ID_46175)
- findrefs-collect-2026-06-23.txt   (ID_46091 collect, ID_115108 VMArray builder)
- findrefs-functor-vt-2026-06-23.txt / findrefs-frbr2-vt-2026-06-23.txt (vtable dumps)
- findrefs-asm46176-2026-06-23.txt  (asm proving the REFR+0x68 keyword virtual call)
- findrefs-asm118279-2026-06-23.txt (asm: vtable LEAs + interior/exterior dispatch)
- findrefs-63342-2026-06-23.txt     (ID_63342 AddKeyword — same +0x68 keyword virtual)
