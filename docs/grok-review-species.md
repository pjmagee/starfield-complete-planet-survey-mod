<!-- grok-build (medium, read-only) review: species - 2026-06-25 -->

**Report: Deep review of SPECIES completion write path (correctness, robustness, idempotency)**

Read-only analysis of the current tree (`.grok-context.txt`, `src/Main.cpp`, `src/EsmReader.cpp`, `include/EsmReader.h`, `re/FINDINGS.md`, `re/ghidra/output/species-scan-complete-model-2026-06-23.md`, `re/ghidra/output/slot-0x08-catalogue-writer-2026-06-22.md`, `test/ValidateMarkers.cpp`, `docs/isolation-review.md`, and supporting RE/docs). All citations are `file:line`.

Focus areas per query: `TestDirectGreen`, `MarkEsmSpeciesForPlanet`, `MarkSpeciesScannedForPlanet` (via `IncrementScanFlag`/`SetPercentByte`), `SpeciesMatchesKind`, `CanonicalFormId`, `TestBuildArray` (idempotent skip + slot-recover + universal fallback), `PushSpeciesAttr`/`BSTArrayU32Grow`, and `Esm::GetSpeciesMarkers` / `GetSpeciesActorMarkers` / `EvalCatalog` / `AbilityMaskToMarkers` (src/EsmReader.cpp).

### 1. Do +0x21 (flag) and +0x08 (marker array) writes match the RE green model (slot+0x08 COMPLETENESS)? Is a species ever left with +0x21 but empty/incomplete +0x08 (→ blue)?

**Partial match, with a documented split and a concrete path to " +0x21 but incomplete +0x08".**

- RE model (FINDINGS 1.10): Green outline boolean is `slot+0x21 != 0` alone (`ID_52159` reads only the raw byte at the FNV slot; `ID_90491`/`ID_90548` use it). `slot+0x08` is **not** read for the outline color decision.  
- Practical rule (FINDINGS 1.11 + slot-0x08-catalogue-writer-2026-06-22.md:170): Write **BOTH**. `+0x21` gives the color boolean; a **COMPLETE** `slot+0x08` (full live-scan set, incl. func-699) is required for the detail panel splice (`ID_124900`→`ID_37875`), on-screen repaint, and (for ability creatures) the outline state machine. A bare `+0x21` poke leaves the panel/XP/catalogue path incomplete and can leave ability creatures blue despite the flag.

Write paths (src/Main.cpp):
- `+0x21`/`+0x20`: `TestDirectGreen:1259` → `MarkEsmSpeciesForPlanet:919` (the ESM species list) → per-species `CanonicalFormId` fallback → `MarkSpeciesScannedForPlanet:647` (calls `IncrementScanFlag` + `SetPercentByte` on the resolved subobj slot). This is the engine writer path; it creates the slot if missing and writes the flag/percent.
- `+0x08`: `TestBuildArray:1270` only. It resolves (or recovers) the slot by the *same* key, derives the marker vector, does the idempotent check, **zeros** the three BSTArray header words, then `PushSpeciesAttr` (which may call `BSTArrayU32Grow`).

**Split / incomplete cases (concrete bugs):**
- `TestDirectGreen` (or any path that reaches `MarkEsmSpeciesForPlanet`) writes `+0x21` for every authored species (via the engine `Mark...` call). If `TestBuildArray` is never called (single-category "fauna"/"flora" that skips the array step, or a prior partial run), the species has `+0x21` + empty `+0x08` (born NULL because `MarkSpeciesScannedForPlanet` stages nothing into subobj+0x08). This is the classic "100% data but blue" state the model describes.
- Inside `TestBuildArray:1318` (src/Main.cpp:1318): after the species `SpeciesSlotHash`, on miss it does a recover `MarkSpeciesScannedForPlanet(planetId, key, ...)` (this **writes +0x21/+0x20**), re-reads `base+0x40`/`+0x48`, re-hashes. If the re-hash still misses (`idx == hashEnd || !slots`), it logs `SLOT-MISS`, `continue`s — **no +0x08 write for this key**. Result: `+0x21` was just written by the recover, `+0x08` remains empty/incomplete → blue (especially ability creatures).
- The initial `TestDirectGreen` pass and the recover `Mark` inside the green pass both use the engine creation path, which (per slot-0x08-catalogue-writer) moves a zeroed staging array into the new slot. No code ever fills `+0x08` except the explicit build path.

A species can easily end up with `+0x21` set but an incomplete (or zero-length) `+0x08`.

### 2. Is the slot KEY consistent between TestDirectGreen (write) and TestBuildArray (resolve)? Both use CanonicalFormId-with-raw-fallback — can they ever diverge and write +0x21 to one slot but +0x08 to another?

**Source expression is identical, but the mechanism is not guaranteed stable.**

Write (src/Main.cpp:934):
```cpp
std::uint32_t key = CanonicalFormId(RE::TESForm::LookupByID(speciesFormId));
if (key == 0) key = speciesFormId;
...
MarkSpeciesScannedForPlanet(planetId, key, ...);
```

Resolve/build (src/Main.cpp:1314):
```cpp
std::uint32_t key = Engine::CanonicalFormId(form);  // form = LookupByID(sf)
if (key == 0) key = sf;
auto idx = Engine::SpeciesSlotHash(hashmap, &key);
...
```

`CanonicalFormId:577` (src/Main.cpp:577) does `ResolveCanonicalForm(form)` (ID_83006) + `*(u32*)(canonForm+0x28)`, with a local `/EHa` try/catch that returns 0 on any fault. `SpeciesMatchesKind` is only a type filter (FLOR/NPC_), not a key transform.

**Divergence vectors (edge cases):**
- Transient fault in `ResolveCanonicalForm` on one call (returns 0, falls back to raw `sf`) but success (or a different value) on the later call in the other function. Write lands under raw; resolve looks under canon (or vice versa) → `+0x21` and `+0x08` go to different slots.
- Form object state or vtable behavior changes between the `MarkEsm...` pass and the `TestBuildArray` pass (rare, but the try/catch exists precisely because ID_83006 has faulted before).
- `TestDirectGreen` logging (src/Main.cpp:1247) computes canon for diagnostics only; the actual write goes through `MarkEsmSpeciesForPlanet` which re-does the lookup. Two separate `LookupByID` + `CanonicalFormId` calls even within one logical operation.

FINDINGS 1.13 states that for all 17 probed species, canonical == authored (the remap theory was dead). The fallback path is defensive. In practice for those species the keys match, but the code structure still permits a per-call divergence via the caught-fault path. No cross-check that the key used for the flag write is the exact same numeric value later used for the array build.

### 3. Is the new idempotent completeness check (flag != 0 AND curCount == markers.size()) correct? Could it (a) skip a genuinely-incomplete species, or (b) needlessly rebuild a complete one? Is curCount computed safely (the <=0x400 bound, the begin/end read)?

**The intent is sound; the implementation has several ways to get the counts wrong, producing both (a) and (b) plus needless clobber.**

Check (src/Main.cpp:1362):
```cpp
const auto flagByte = *reinterpret_cast<std::uint8_t*>(slotAddr + 0x21);
const auto arrBegin = *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x08);
const auto arrEnd   = *reinterpret_cast<std::uintptr_t*>(slotAddr + 0x10);
const std::size_t curCount = (arrBegin != 0 && arrEnd > arrBegin && (arrEnd - arrBegin) <= 0x400)
                             ? (arrEnd - arrBegin) / 4 : 0;
if (flagByte != 0 && curCount == markers.size()) { alreadyComplete++; continue; }
```

Then unconditional clear + rebuild for non-matching cases (src/Main.cpp:1369).

**Safety of curCount:**
- Only loads (no dereference of the array content at this point). The `<= 0x400` is a byte-difference guard (max 256 u32s) before `/4`.
- If `arrEnd < arrBegin` (corrupt or re-laid-out header), treated as 0.
- Pointer arithmetic is on raw `uintptr_t` values read from the slot; they are later used as addresses for the clear and for `PushSpeciesAttr`. If a rehash moved the slot table between the earlier hash and these reads (or between these reads and the stores), the numbers are stale offsets into freed or repurposed memory.
- The bound is generous for real species (typical 4–6 markers) but would truncate any species that legitimately had >256 markers (curCount would under-count → rebuild).

**Skip a genuinely-incomplete (false "already complete"):**
- If `markers.size()` under-counts reality (derivation gap or the fallback case) but the slot already holds the *real* larger set, `curCount == markers.size()` is false → we rebuild (see below). The opposite (curCount > our size) cannot cause a wrongful skip.
- A slot that has `+0x21` and a partial array whose byte-length happens to equal our (wrong) `markers.size()` would be skipped. Unlikely but possible if sizes coincide.

**Needlessly rebuild a complete one (or clobber it):**
- Derivation returns fewer markers than the engine actually wrote for that (planet,species) → `curCount != size` → clear + rewrite with the smaller set. This is exactly the "re-writing a complete species is the clobber source" scenario the comment at src/Main.cpp:1353 tries to avoid. The clear zeros the header (momentarily empty +0x08 visible to any concurrent reader), then we write a subset.
- Fallback path (src/Main.cpp:1346): when `GetSpeciesMarkers` + actor returns empty, we force `{0x0023E90D, 0x002634BE}` (size=2). A previously complete slot that had the real 4–6 will mismatch and be overwritten with 2 universals → under-complete +0x08.
- A just-recovered slot (the `Mark` inside the loop) has `+0x21` set by that call but `+0x08` still 0 → curCount=0 != size → we build (correct).
- After a real scan (or prior full build) the slot header may contain a live engine-owned buffer. Zeroing the three words without `EngineScalarFree` (see Q5) leaks it and changes observable state.

The check is performed with data loaded directly from the slot; there is no validation that the loaded `arrBegin`/`arrEnd` actually point to valid u32s of the expected content.

### 4. Does the marker derivation produce the FULL correct set per species (incl func-699 abilities)? Where could it UNDER-count (→ incomplete +0x08 → blue) or OVER-count? Is the 17/17 validation representative?

**Strong for the validated 17; structural gaps exist for other species classes. Under-count is the dominant risk. No obvious over-count path.**

Core (src/EsmReader.cpp):
- `GetSpeciesMarkers:1047`: looks up `SpeciesMarkerInfo` (populated in `BuildMarkers`), selects flora or fauna catalog, builds `EvalCtx`, calls `EvalCatalog`.
- `EvalCatalog:741`: walks `cat.lnam`; for entries with a CTDA block, runs `EvalCondList` (the ID_71422-style OR-run AND of comparisons). Only unconditional or true-evaluating markers are emitted. Perk/display funcs (448/699 at the leaf) → `nullopt` → whole marker dropped.
- `GetSpeciesActorMarkers:1081`: `AbilityMaskToMarkers` on the precomputed mask (only for NPC_ entries).
- `TestBuildArray:1338`: `markers = GetSpeciesMarkers(...)`; append `GetSpeciesActorMarkers`; fallback to the two universals if still empty; then write.

**func-699 path**: `AbilityMaskToMarkers:528` (src/EsmReader.cpp:528) emits the three scanner-kw markers (`0x002634BF/C1/C0`) when the corresponding bits are set in the mask built bottom-up (MGEF KWDA → SPEL EFID → PERK PRKE+1 DATA → OMOD NPRK → NPC_ OBTS). `ValidateMarkers.cpp:109` has explicit actor cases (including 1-bit and 2-bit creatures); they are asserted separately from the catalog 17/17.

**UNDER-count risks (→ incomplete +0x08 → blue for outline state machine or panel):**
- Species absent from `g_markers.species` (BuildMarkers only populates from direct `kSigNPC_` / `kSigFLOR` record walks). Lvl/template creatures that have no concrete OBTS-bearing NPC_ record in the groups will be missing → `GetSpeciesMarkers` returns {} → fallback (2 universals). These may still appear in a planet's PNDT PPBD list.
- Temperament (func-560) or reproduction (func-14) parse misses an NKEY/PRPS → wrong grantedKw or reproAv → subset of catalog markers (or wrong override).
- CNDF recursion depth > `kMaxCondFormDepth` (16) short-circuits a branch to 0 (src/EsmReader.cpp:680).
- Underground biome marker (`0x000CC6C2` vs `0x002634BE`): data-driven via catalog CTDA + `HasKeyword` on grantedKw. If an underground creature's OBTS→NKEY chain does not surface `CCT_Enviro_Underground`, it gets the wrong biomes marker. (Model doc notes ~1 such creature file-wide; the constant is not mentioned in src because the logic is catalog-driven.)
- A planet's traits are looked up in `g_markers.planetTraits` (populated only from PNDT KWDA in `ParsePndtGroup`). Missing traits → genetics/repro conditions evaluate to the default path (possible wrong marker).
- The 7 Lvl/template bosses + Player correctly resolve to X=0 (materialization-bound); they are intentionally left blue.

**OVER-count risks**: Low. `EvalCatalog` only emits entries present in the catalog `lnam` whose conditions pass. Actor markers are exactly the three bits. No duplication logic except the explicit append in TestBuildArray (catalog + actor are disjoint sets).

**17/17 representativeness** (ValidateMarkers.cpp:31 + species-scan-complete-model:160):
- Jemison procedural flora (8) + fauna (9) with direct records. Exercises multiple temperaments, the Rhizomes 5th-marker case (AV3 + EcologicalConsortium), and several ability creatures.
- **Not covered** (per model §3): underground fauna biome swap, non-Carbon genetics (Arsenic/H2S/Methane/XNA/Silicon), Spores override (Psychotropic/Aeriform + AV<4), Lvl/template indirection species, any species whose catalog conditions depend on rare trait combinations.
- The offline Python reference (`esm_derive_markers.py`) and the C++ port are byte-for-byte equivalent on the ground truth, and the C++ is built from the same one-time ESM parse under try/catch.

Result: for "normal" direct-record surveyable species on typical planets, the derivation + append produces the full set the engine would write. For the classes above, under-count (via empty result → fallback or via a missed conditional marker) is possible, producing an incomplete `+0x08`.

### 5. Memory-safety of the +0x08 writes (clear of begin/end/cap then PushSpeciesAttr) — any leak/UAF/heap-corruption risk vs. the repo's history? Re-entrancy with the mid-loop slot-recover re-read of slots/hashEnd?

**Leak is present and structural. No demonstrated UAF or immediate corruption in the current pattern, but it deviates from the "engine owns the BSTArray" discipline stressed in the repo. The recover pattern has narrow but real re-entrancy assumptions.**

Clear + write (src/Main.cpp:1369):
```cpp
*reinterpret_cast<std::uintptr_t*>(slotAddr + 0x08) = 0;
*... + 0x10 = 0;
*... + 0x18 = 0;
for (...) Engine::PushSpeciesAttr(slotAddr, id);
```

`PushSpeciesAttr:278` reads the (now-zero) end/cap; on `end==cap` it calls `BSTArrayU32Grow` (ID_35755) which allocates via the engine allocator (ID_35770). The engine free (ID_35771, 4-byte stride) is only ever called by engine paths on whatever the header currently points to at teardown/rehash time.

**Leak**: The prior non-null `begin` buffer (if any) is never passed to `EngineScalarFree`. The allocation from a previous real scan or prior `TestBuildArray` run becomes unreachable. Small (typically 16–32 bytes per species per planet) but unbounded across many planets / repeated runs. This is a deviation from the repo rule ("never hand-write BSTArray ... use ... the engine's own grow paths").

**UAF / corruption**:
- Not from the write itself (we never free the old buffer). Readers that held a stale begin/end snapshot from before the clear can observe a suddenly-empty array (the "clobber window" the idempotent check was added to shrink).
- If a concurrent (or later same-frame) engine reader walks the slot header after we zeroed it but before Push finishes, it sees an empty array. The comment at src/Main.cpp:1353 acknowledges this as the reason for the skip.
- The header writes are raw stores through a `slotAddr` computed from a `SpeciesSlotHash` result on a snapshot of `slots`/`hashEnd`. A rehash of the species map between the hash and the stores would make `slotAddr` invalid.

**Re-entrancy / mid-loop recover (src/Main.cpp:1322)**:
- Pattern per species: hash → miss? → `MarkSpeciesScannedForPlanet` (engine create/grow of the species hashmap + flag write) → re-read `* (base+0x48)` and `* (base+0x40)` → re-hash → compute `slotAddr` → clear + pushes.
- The re-read + re-hash is only for *this* key and happens before we take `slotAddr` for the writes of this key. Prior iteration's writes used their own (earlier) snapshot of slots + their own resolved `slotAddr`; they have already completed.
- The only Mark inside the loop for a given key is the recover for that key. No other species' recover runs between a given iteration's resolve and its three stores + pushes.
- `BSTArrayU32Grow` (slot array growth) is a separate heap from the species hashmap; it should not induce a rehash of `subobj+0x18/0x40/0x48`.
- `IncrementScanFlag` (the recover) *can* grow/rehash the species map. The code relies on the engine preserving prior entries' data (including the u32 heap buffers pointed to by begin/end/cap) when it moves headers. This matches the comment "engine moves entries". It is an assumption, not a proven isolation.

Repo history (CLAUDE.md, isolation-review.md:91, docs on BSTArray): the bad pattern was hand-writing capacity/size or poking allocators. This code uses the grow path for appends and only hand-clears the header triple to force a clean rebuild. The missing free is the new hazard.

**Concrete species / scenarios that can be written incompletely**
- Any species that hits the `SLOT-MISS` path after recover (src/Main.cpp:1331) — +0x21 written by recover, +0x08 never written.
- Any species for which `GetSpeciesMarkers` + actor returns empty (not in the parsed species table, or all conditions gated) — written with only the 2-universal fallback (src/Main.cpp:1346). If the real expected set is larger, the slot is under-complete.
- Ability creatures (func-699 bits set) whose catalog derivation is missing one or more markers (underground, rare trait conditions, CNDF depth, missed OBTS/OMOD/NKEY) — the appended actor set is still incomplete relative to what the outline state machine expects.
- Lvl/template or non-direct-record creatures that appear in a planet's PPBD list but have no entry in `g_markers.species`.
- Re-run of the green path on a planet whose slots already hold the engine's larger set but our `markers.size()` is smaller (idempotent check fails → rebuild with subset).

### Summary of concrete bugs / edge cases (file:line)

- Split state: `+0x21` via `MarkEsm...` or recover Mark, `+0x08` never or incompletely written (Main.cpp:919 (MarkEsm), 1322 (recover), 1331 (SLOT-MISS continue), 1369 (clear path)).
- Key computation can diverge across calls via caught fault in `CanonicalFormId` (Main.cpp:577, 934, 1314).
- Idempotent check can cause rebuild of a complete slot when derivation under-counts or fallback is taken (Main.cpp:1360–1362, 1346).
- `curCount` arithmetic on raw uintptr_t with a hard byte bound; can under-count or be fooled by a moved header (Main.cpp:1360).
- Clear of BSTArray header without `EngineScalarFree` (leak of prior engine-owned u32[] buffer) (Main.cpp:1369; contrast with 272 (EngineScalarFree) and 285 (only grow path is used for append)).
- Recover re-read assumes engine rehash semantics for the species map (Main.cpp:1325); no protection against a Mark between resolve and the three header stores.
- Derivation coverage gaps for underground, non-Carbon genetics, Spores, Lvl/template (EsmReader.cpp:1047 (Get), 1081 (actor), BuildMarkers walk of direct records only; model doc §3).

All analysis above is derived from the files read; no runtime claims are made.