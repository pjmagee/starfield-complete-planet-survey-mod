<!-- grok-build (xhigh, read-only) isolation review + iteration audit — 2026-06-25. Analysis only; not acted on. -->

**Adversarial READ-ONLY review of Starfield SFSE Complete Planet Survey mod (iterations in `.grok-iter-diff.txt` + current working tree).**

No files were edited, no builds executed, no in-game claims made. All analysis is from direct reads of the current tree (`src/Main.cpp`, `Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc`, `CompletePlanetSurveyNative.psc`, `src/EsmReader.cpp`, `include/EsmReader.h`, `CLAUDE.md`, `re/FINDINGS.md`, `docs/`, `re/ghidra/output/`, and `.grok-iter-diff.txt`) + grep cross-references. Citations use `file:line` (relative paths) throughout.

The four console commands (CompletePlanetSurveyQuest.psc) dispatch on a category CSV to:
- `resources` → `MarkResourcesForPlanet` → `CompletePlanetSurveyState(false)` → `SetPlanetAttributeBits` + `MarkEverythingForPlanet` + `NotifySurveyProgress`.
- `traits` → `MarkTraits` (pure `MarkTraitKnownForPlanet`) + `_CompleteTraitScanObjects` (Papyrus SQ_Parent + Location AV).
- `fauna`/`flora` → `_GreenPlanet` → `TestDirectGreen` (MarkEsmSpeciesForPlanet → `MarkSpeciesScannedForPlanet`) + `TestBuildArray` (slot +0x08 build + `NotifySurveyProgress` — the just-added call).

The diff (now reflected in the tree) addressed three "all-vs-single" failures: missing repaint after species green, "resources" leaking into species via unresolved fids, and typo'd categories half-running.

---

### PART 1 — Review of the iterations (`.grok-iter-diff.txt` hunks)

**Hunk 1 (Papyrus guards + CategoriesValid native declaration/implementation):**  
Correct and narrow. The four command entry points (`CompletePlanetSurveyQuest.psc:50`, `:110`, `:211`, `:289`) now early-return with a notification on `!CategoriesValid`. The native (`src/Main.cpp:1577` implementation, registered at `1660`) lowercases, trims, splits on `,`, requires `>=1` token, and rejects anything not in the exact set `{"all","resources","traits","fauna","flora","species","creatures"}`. This directly fixes the "typo'd category half-ran" class. No over-rejection of valid inputs (aliases are accepted); empty/blank and "res"/"creature" correctly false. Double-notification risk between `CompleteAllPlanets` and its subs is mitigated by the `abShowResult=false` passing (`psc:284` and callers) — the top-level guard fires once, subs are not re-entered on invalid.

**Hunk 2 (MarkEverythingForPlanet null-form guard):**  
```cpp
// src/Main.cpp:830 (post-diff)
else if (!includeSpecies)
{
    return;
}
```
Pragmatic but not the most precise guard. After `LookupByID` fails on an aggregator fid, the `!includeSpecies` (resources) path bails instead of calling `MarkSpeciesScannedForPlanet`. This stops the documented leak (unresolved species fids being treated as "resources"). However, any legitimate non-species resource fid that fails `LookupByID` (rare, but possible for edge forms or timing) would now be skipped under "resources". An ESM-species-set check (as the comment itself contemplates) would have been cleaner. No heap poke — safe. The prior path (includeSpecies) still marks unresolved fids, which is intentional for the full sweep.

**Hunk 3 (TestBuildArray slot-recover + re-read + fallback + Notify):**  
Core changes at `src/Main.cpp:1283` (non-const `auto hashEnd`/`slots`), `1319` (miss path: `MarkSpeciesScannedForPlanet` then re-read `base+0x48`/`base+0x40`, re-hash), `1349` (empty markers → `{0x0023E90Du, 0x002634BEu}` + log), `1369` (`NotifySurveyProgress(planetId)`).

- **Slot-recover re-read mid-loop:** The pattern is: compute idx via `SpeciesSlotHash`, on miss call the engine `Mark` (which creates via `IncrementScanFlag`), re-read the two pointers, re-resolve for *this* key only. Comments claim "engine moves entries" on rehash and "each species re-resolves its own slot". This is better than holding a stale `idx` or `slotAddr` across iterations. Re-reading the pointers (non-const `auto`) after a potential grow/rehash is the minimal defensive step. However: (a) `MarkSpeciesScannedForPlanet` itself is not proven re-entrant-safe against its own hashmap growth in the exact same frame; (b) if a rehash frees the old slots buffer while another in-flight pointer (from a prior iteration's `slotAddr` computation that was *not* re-resolved) were used, corruption would occur — but the code only computes `slotAddr` *after* its own re-resolve for the current key, so prior writes are assumed preserved by engine move. No manual BSTArray header mutation (uses `PushSpeciesAttr` → `BSTArrayU32Grow` or in-place via engine pointers) — respects the repo's heap-corruption history. Still: re-reading mid-loop is a symptom of fighting a shared mutable table rather than a clean isolation proof.
- **Marker FALLBACK:** Universals `0x0023E90D` (resource) + `0x002634BE` (biomes) per `re/FINDINGS.md:1.1` and model. Makes `+0x08` non-empty so species do not stay "blue despite +0x21". Adversarial risk: if the outline repaint or panel completeness gate requires the *full* ESM-derived set for some species (especially ability creatures via func-699), a fallback species will be green-in-outline but information-incomplete. Code now logs at INFO (`src/Main.cpp:1354`) and counts `fallbackUsed` — visible in release logs. Acceptable band-aid; not a correct full set.
- **NotifySurveyProgress at end of TestBuildArray:** Placed *after* the `+0x08` build (`1369`), once per `_GreenPlanet` invocation. In `CompleteLifePlanets` this fires ~182 times (one per life planet). Idempotent per comment and `ID_97853` design. Perf/re-entrancy: sequential single-threaded Papyrus dispatch; no re-entrancy inside one planet. Event-storm concern is real under "all" (resources path already does `CompletePlanetSurveyState` → Notify; species now adds another). See Question 3.
- Overall hunk: improves the "green but blue on screen" and "some species blue" symptoms without introducing manual allocator writes. Still operates on a shared map.

All changes are guarded by the existing `/EHa` + `CPS_GUARDED` + null checks. No direct violation of CLAUDE.md heap rules.

---

### PART 2 — ISOLATION MAP

#### 1. Table: Category | Engine structure/record | exact location | read-only or read-write

| Category   | Engine structure/record                          | Exact location (struct+offset / RECORD / fn + file:line) | R/W |
|------------|--------------------------------------------------|-----------------------------------------------------------|-----|
| resources | Per-planet survey subobj (938333 entry) — attribute bitmask | `subobj+0x00` (== entry+0x20); `SetPlanetAttributeBits` writes `\|= 0x7`; `src/Main.cpp:636`, `642`, called from `WritePlanetSurveyState:880` and `CompletePlanetSurveyState:889` (via `MarkResourcesForPlanet:1430`) | RW |
| resources | Species/resource slot hashmap + slots (same 938333 subobj) | `subobj+0x18` (hash), `subobj+0x40` (slots ptr), `+0x48` (hashEnd); per-slot `+0x20` pct / `+0x21` flag via `MarkSpeciesScannedForPlanet:647` (calls `IncrementScanFlag:661` + `SetPercentByte:662`); `MarkEverythingForPlanet:809` (aggregator walk, filtered) | RW |
| resources | Notify / survey recompute | `Engine::NotifySurveyProgress:857` (ID_97853 ctx) called from `CompletePlanetSurveyState:890` | RW (event) |
| fauna/flora | Same slot hashmap + slots as above | `MarkEsmSpeciesForPlanet:919` → `MarkSpeciesScannedForPlanet`; `TestBuildArray:1274` (hash lookup at `base+0x40/0x48`, `slotAddr = slots + idx*0x30`, clear +0x08 header at `1335`, `PushSpeciesAttr:278` into BSTArray, fallback at `1349`, re-create at `1322`) | RW |
| fauna/flora | Notify | `TestBuildArray:1369` (post +0x08) | RW (event) |
| traits    | 938333 PlayerKnowledge trait member-array | `MarkTraitKnownForPlanet:1210` → `Engine::MarkTraitKnown:340` → `SetTraitKnownNative` (ID_52155); key `(938333<<48)|(planet<<16)`; separate member array inside same disc record per `re/FINDINGS.md:2.1` | RW |
| traits    | In-world scan-target Location actor values + SQ_Parent | Pure Papyrus: `CompletePlanetSurveyQuest.psc:360` (`_CompleteTraitScanObjects`): `loc.SetValue(PlanetTraitLocationScanCount)`, `r.SetScanned(true)`, `sqp.DiscoverMatchingPlanetTraits`; no subobj/938333 slot writes | RW (Location AV + quest state) |
| (all)     | Discover entry | `DiscoverPlanetEntry:1410` → `ScanCompletePlanet` (ID_102650); prerequisite for subobj writes on never-visited | RW (creates 938333 entry) |

#### 2. SHARED-STRUCTURE matrix

**Primary shared structure: the per-planet 938333 subobj's species/resource slot table (subobj+0x18 hashmap, +0x40 slots, +0x48 end, 0x30-stride slots containing +0x20/+0x21 bytes + +0x08 BSTArray).**

- Touched by: **resources** (`MarkEverythingForPlanet` (include=false) → `MarkSpeciesScannedForPlanet` on non-FLOR/NPC_ aggregator fids) **and fauna/flora** (`MarkEsmSpeciesForPlanet` + `TestBuildArray` slot-recover + `PushSpeciesAttr`).
- Interaction: same hash function (`SpeciesSlotHash` ID_124901), same growth path inside `IncrementScanFlag` (ID_124898). A resources pass on a life planet first populates resource fids (and can cause allocation/growth/rehash of the table). A subsequent species green iterates ESM species keys; a miss now does a create + re-read. Rehash under the engine can relocate prior entries. Previous species writes (from an earlier category) survive only if the engine copy-on-rehash is correct.
- Resources fids and species fids are *different keys*, so no direct overwrite of a species slot's +0x21 by a resource fid. But the *table* (capacity, buckets, backing storage) is shared mutable state. Growth from one category can affect resolution of the other.
- Secondary sharing: the 938333 *record* itself contains both the species slot table *and* the trait member-array (written via separate ID_52155 path). Traits do not touch the slot hashmap.
- Notify (ID_97853): written/fired by resources *and* species paths.
- In-world trait objects (Location AVs): isolated to traits + on-planet Papyrus; no overlap with subobj.

**Other sharing:** `DiscoverPlanetEntry` (ID_102650) is a prerequisite for both resources and species on never-visited worlds. Aggregator (ID_1016657) is read by resources path only.

#### 3. ISOLATION VERDICT per category

- **resources**: Not fully isolated from species. It deliberately avoids FLOR/NPC_ forms (via type check + the new `!includeSpecies` null-guard at `src/Main.cpp:829`), but still writes the *exact same subobj slot hashmap* via `MarkSpeciesScannedForPlanet` on resource fids. Any growth/rehash from resources precedes or interleaves with species green under "all". The null-guard + ESM-species filter mitigate the "leaked into species scan flags" symptom but do not eliminate the shared-table mutation. Attribute bits (`+0x00`) are resources-only.
- **traits**: Mostly isolated. Uses a distinct writer (`SetTraitKnownNative` ID_52155) into a separate member-array inside the 938333 record (`re/FINDINGS.md:2.1`). Does *not* call `MarkSpeciesScannedForPlanet`, `SetPlanetAttributeBits`, or touch slot +0x08/+0x20/+0x21. In-world objects are separate (Location AV + SQ_Parent). However, it shares the overarching 938333 entry container and can be followed by Notify from other categories. No species/resource flag pollution.
- **fauna vs flora**: Real isolation via `_SpeciesKind` + `SpeciesMatchesKind` (`psc:38`, `src/Main.cpp:901` — kind==1 FLOR only, kind==2 NPC_ only). They write the *same slot table* but different keys. No cross-kind writes.
- **"all" as sequence**: Only incidentally isolated. The documented bugs (species green not repainting until resources fired Notify; resources leaking species fids) show that ordering and shared table matter. The fixes (Notify in TestBuildArray, guard, CategoriesValid) paper over symptoms without changing the fundamental shared mutable structure.

---

### QUESTIONS (exhaustive, file:line)

1. **List EVERY shared structure and the categories that touch it. Which sharings are benign vs. dangerous?**  
   - 938333 per-planet entry + subobj slot table (`subobj+0x40` etc., `src/Main.cpp:605`, `1283`): resources (resource fids), fauna, flora. Dangerous (growth/rehash can move slots under "all").  
   - 938333 trait member-array: traits only (via ID_52155). Benign vs. species slots.  
   - Attribute bitmask (`subobj+0x00`): resources only. Benign.  
   - Notify/ID_97853: resources + species (now). Benign for correctness but ordering/perf concern.  
   - Location AVs + SQ_Parent objects: traits only. Isolated.  
   - Aggregator spans: resources read only.  
   - Discover (ID_102650): resources + species (prereq). Benign.

2. **For the subobj species/resource hashmap specifically: can resources marking resource slots (growth/rehash) before/after species green corrupt or mis-resolve species slots under "all"? Does the just-added slot-recover fully neutralize that, or only mask it?**  
   Yes it can move the table. Resources path (`MarkResourcesForPlanet:1430` → `CompletePlanetSurveyState(false)` → `MarkEverythingForPlanet:809` → `MarkSpeciesScannedForPlanet:647`) calls the same `IncrementScanFlag` that creates/grows slots for whatever fids the aggregator returns. A later (or earlier) species green in the same planet's processing can see a rehashed table. The slot-recover (`TestBuildArray:1322`: Mark + re-read `* (base+0x48)` / `* (base+0x40)` + re-hash) only helps *inside* the species green pass for keys that miss on the first `SpeciesSlotHash`. It does not prevent resources from having already grown the map, nor does it protect if a rehash occurs *during* a `Mark` call itself. It masks the "slot not found after TestDirectGreen" symptom but does not prove isolation. No species-side re-resolve exists in the resources path.

3. **Does firing NotifySurveyProgress from BOTH resources and species (under "all") cause any double-recompute/ordering issue?**  
   Resources fire it via `CompletePlanetSurveyState:890` (after attribute + resource flags). Species fire it at the *end* of `TestBuildArray:1369` (after +0x08). In `CompleteLifePlanets` (and thus `CompleteAllPlanets`) for a life planet that requests both, you get two calls per planet (plus any from traits' internal event). `ID_97853` is described as check-and-dispatch (fires complete event + slate at 100%). Double call is likely idempotent for the slate (engine awards once) but can produce duplicate UI events or redundant recomputes. Ordering is "resources Notify then later species Notify" in the loop. Previously species-only left the outline stale until a later resources Notify; the new placement fixes the single-category case at the cost of duplication in "all".

4. **Is "traits" truly isolated (separate 938333 trait record + Location AVs), or does it touch the survey subobj/notify in a way that interacts with species/resources?**  
   Traits are the most isolated. `MarkTraitKnownForPlanet` uses only `SetTraitKnownNative` (no call to `ResolvePlanetSubobj`, no `MarkSpeciesScannedForPlanet`, no `SetPlanetAttributeBits`, no direct `NotifySurveyProgress`). The in-world path is pure Papyrus (`psc:360`). However, the 938333 *record* is the shared container (`re/FINDINGS.md:1.14`, `2.1`); survey % readers (aggregator) may see trait progress. Notify can arrive from sibling categories under "all". No slot +0x21 pollution. Traits alone do not create the knowledge entry (so remote trait marking on never-visited is self-sufficient per docs).

5. **Concrete recommendations to make each category provably ISOLATED + LOCKED.**  
   - Use **disjoint key domains** inside the 938333 subobj (or a separate discriminator for resources vs. species slots) so `MarkSpeciesScannedForPlanet` for resources never touches the species-keyed table.  
   - Or: make species green use its *own* table walk/create path that does not share the exact hashmap storage mutated by resources.  
   - Move the single `NotifySurveyProgress` to the *very end* of each top-level command (or to `CompleteAllPlanets` after both sweeps) instead of per-category inside the hot loops.  
   - Guard species slot writes with an explicit "this is a species key" check derived from the ESM map rather than post-hoc type + null-form.  
   - For the fallback, either always derive a full set or document that fallback species have incomplete panels.  
   - Add a read-only "validate isolation" diagnostic that, after a category, asserts that only expected keys in the subobj were mutated. Prioritize the disjoint-key or post-"all" single notify.

---

### Areas of concern (numbered, adversarial)

1. **The slot hashmap (`subobj+0x40/0x48`) is the recurring shared mutable state** (`src/Main.cpp:605`, `1283`, `1319`, `MarkSpeciesScannedForPlanet:647`, `MarkEverythingForPlanet:809`). Resources and species both mutate it under "all". Slot-recover only masks symptoms inside one green pass; it does not prevent rehash effects from a prior category's `Mark` calls. This is the root of the "all-vs-single" bugs that keep recurring.

2. **Re-reading `slots`/`hashEnd` after `MarkSpeciesScannedForPlanet` inside the loop (`src/Main.cpp:1325`) is sound only under strong assumptions** about the engine's rehash (copies prior data, no concurrent mutation, the create for this key never frees a still-referenced old buffer). The code correctly avoids holding an `idx` across the create, but the pattern is a red flag for a design that fights shared state.

3. **Marker fallback (0x0023E90D/0x002634BE) at `src/Main.cpp:1349`** guarantees a non-empty +0x08 (to trigger repaint) but is a *subset*. For species whose full panel/outline gate requires the complete ESM set (especially func-699 ability creatures), this produces a partially-correct green. Logged, but still a correctness gap vs. "provably isolated + correct".

4. **Notify firing from both resources (`CompletePlanetSurveyState:890`) and species (`TestBuildArray:1369`)** produces N×2 calls on life planets under "all". While the per-planet sequential nature limits damage, this is unnecessary event traffic and potential double slate/UI side effects. The placement "at end of green" was added to fix the single-category repaint bug but creates duplication in the composite case.

5. **Null-form guard (`src/Main.cpp:830`) is a band-aid, not a precise isolation boundary.** It correctly stops the species leak in the resources path but would silently drop any legitimate unresolved resource fid. An ESM-derived species set (already loaded via `Esm::GetPlanetSpecies()`) would be a stronger, positive guard.

6. **CategoriesValid + guards are good for the typo/empty case** but the validation is string-based and duplicated (C++ `1577` + Papyrus callers). Any future token addition requires synchronized changes in two places. No central constant list.

7. **"resources" still calls the species-named function `MarkSpeciesScannedForPlanet`** on non-species fids. Naming + implementation reuse across categories is itself an isolation smell, even with the `includeSpecies` filter.

8. **Traits share the 938333 container** even though their writer is separate. Survey % and some readers walk the same record; a future change that assumes "only species slots live here" could break trait data.

9. **CompleteLifePlanets loop (~182 iterations) does Discover + per-category writes + (now) per-green Notify sequentially.** No locking or snapshot of the subobj; each category's mutations are immediately visible to the next. Under "all" the order (resources before green in some paths) is the only thing preventing the old leak — incidental, not enforced isolation.

10. **No defensive re-resolve or snapshot in the resources path.** Only the species green gained the "recreate + re-read" logic. A resources-only or resources-then-species sequence on a never-visited life planet that grows the table can still leave later species green in a partially inconsistent view of the map.

11. **PushSpeciesAttr + BSTArray growth uses engine paths** (`BSTArrayU32Grow` + `EngineScalarAlloc/Free`) — compliant with history. However, the explicit header zeroing (`1335`) before rebuild assumes the slot was already allocated by a prior `Mark`/`TestDirectGreen`. A slot that was never created would fault on the `slotAddr` arithmetic after the miss recovery failed to re-create — the code does guard the post-recreate idx, but the zeroing path is downstream.

12. **"all" is only a sequence of steps, not a transaction.** There is no outer guard that the subobj state after resources is a valid prefix for species green (or vice versa). The fixes make the observed failures rarer; they do not make the categories *provably* composable without ordering side effects.

**Summary verdict:** The diff patches are locally correct and address the immediate symptoms without introducing new heap-corruption vectors. However, the fundamental design still has species/resources sharing a single mutable per-planet slot table under the 938333 discriminator. Isolation is partial and relies on filtering + ordering + recovery code rather than disjoint structures. Traits are the cleanest. Recommendations in Q5 are the concrete path to "provably ISOLATED + LOCKED". All claims are traceable to the cited lines and `re/FINDINGS.md` entries.
