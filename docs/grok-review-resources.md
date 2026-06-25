<!-- grok-build (xhigh, read-only) review: resources - 2026-06-25 -->

**Read-only deep review complete.** All analysis from direct reads of the working tree (`.grok-context.txt`, `src/Main.cpp`, `src/EsmReader.cpp`, `include/EsmReader.h`, Papyrus sources, `docs/isolation-review.md`, `re/FINDINGS.md`, and key Ghidra outputs). No edits, builds, or in-game runs.

---

## 1. KEY SYMPTOM — `resources=17` for a 17-species planet (9 fauna + 8 flora)

**Call chain:**
- Papyrus `CompletePlanetSurveyQuest.psc:80` → `MarkResourcesForPlanet(planetForm, 100)`
- `src/Main.cpp:1446` → `Engine::CompletePlanetSurveyState(planetId, d, /*includeSpecies=*/false)`
- `src/Main.cpp:889` → `WritePlanetSurveyState` → `MarkEverythingForPlanet(planetId, delta, false)`
- `src/Main.cpp:756` → `ForEachAggregatedFormId` (runs `SurveyAggregator` ID_1016657, walks all 4 spans, calls `fn(fid)` per valid entry)
- `src/Main.cpp:809` (MarkEverythingForPlanet lambda): `LookupByID` → type checks → (possibly) `MarkSpeciesScannedForPlanet` → `IncrementScanFlag` + `SetPercentByte` (writes `slot+0x21`/`+0x20` via ID_124898/ID_124899)
- Return value logged as `resources=N` at `psc:100`

**What `marked` counts (src/Main.cpp:847):** the number of aggregator fids for which `MarkSpeciesScannedForPlanet(planetId, fid, delta) == 1`.

**Aggregator layout (src/Main.cpp:186-195, cross-checked vs ID_97851 decompile in `re/ghidra/output/survey-percent.txt:35-110` and `astro-starmap-handler-2026-06-24.txt:2399-2460`):**
- `kAggUintSpan0` (0x218-0x220) and `kAggUintSpan1` (0x230-0x238): inline `u32` keys (traits/flora per comments).
- `kAggPtrSpan0` (0x1e8-0x1f0) and `kAggPtrSpan1` (0x200-0x208): `TESForm*[]`; formID read at `+0x28`.
- All 4 spans feed the **same** per-planet slot hashmap (subobj+0x18 via `ID_124901`, slots at subobj+0x40, 0x30 stride). ID_97851 walks exactly these 4 arrays and counts `+0x21` bytes (above ID_69506/69507 thresholds) into its 4 category counters.

**Species keys live in this table.** ID_97851 uses the aggregator fids as keys into the `+0x21` slots to compute survey % categories. Writing `+0x21`/`+0x20` for an aggregator fid makes that fid "count" for survey % (and the green outline reads `+0x21` via ID_52159 on the same table).

**Filter in MarkEverythingForPlanet (src/Main.cpp:820-840):**
```cpp
auto* form = RE::TESForm::LookupByID(fid);
if (form) {
    if (ft == kKYWD) return;
    if (!includeSpecies && (ft == kFLOR || ft == kNPC_)) return;
} else if (!includeSpecies) {
    return;  // null-form guard
}
if (MarkSpeciesScannedForPlanet(...) == 1) ++marked;
```

**Leak analysis for the symptom:**
- If the aggregator returns the **raw ESM FLOR/NPC_ fids** authored in PPBD: `LookupByID` yields `kFLOR`/`kNPC_`; the `!includeSpecies` type check skips. No leak via this path.
- If the aggregator returns **species keys under different (canonical/remapped) ids** (the long-standing ID_83006/83009 concern; see `Main.cpp:367-369` and context about leveled/template fauna), then:
  - `LookupByID(fid)` may succeed but return a form whose `GetFormType()` is **not** `kFLOR`/`kNPC_` (e.g., `LVLN` leveled actor, or an intermediate form). The kingdom filter does not fire → `MarkSpeciesScannedForPlanet` is called → leak.
  - `LookupByID(fid)` may return `nullptr`. The `else if (!includeSpecies) return;` guard then skips. Guard protects against unresolved fids.
- The observed `marked == 17 == species count` on a life planet is **strong evidence of a genuine species-key leak**, not a coincidental count of 17 unrelated resource forms. The aggregator (ID_1016657) is the engine's survey-% source of truth; for life planets it enumerates the tracked species entries. The mod's walk is unfiltered by kingdom membership—only by post-hoc runtime `GetFormType()` on whatever the aggregator emitted.

**Concrete lines:** `src/Main.cpp:756` (ForEachAggregatedFormId calls fn for every span entry), `809-849` (MarkEverythingForPlanet), `829` (FLOR/NPC_ test), `834-839` (null-form guard), `647-662` (MarkSpeciesScannedForPlanet writes the shared slots).

---

## 2. Is the type check + null-form guard sufficient for purity?

**No, it is not sufficient.**

- The guard is a **negative heuristic** ("skip if it looks like a species form at lookup time"). It does not prove "this fid is not a species key in the slot table."
- Species can reach `MarkSpeciesScannedForPlanet` under `resources` (includeSpecies=false) when:
  1. Aggregator emits a fid for a species that resolves to a non-`FLOR`/`NPC_` form (LVLN, template wrapper, or a canonical that is a distinct form record). Type check misses.
  2. (Less likely under resources) a fid fails `LookupByID` but the `!includeSpecies` null branch is not taken (code structure prevents this today).
- An **explicit membership check against `Esm::GetPlanetSpecies()`** (already populated and used for the green path at `src/Main.cpp:919`, `EsmReader.cpp:840`) would be a positive allowlist: "if this fid (or its authored equivalent) is in the planet's ESM species set, treat it as species and exclude under resources." This is stronger and immune to runtime form-type remapping.
- The null-form guard **can wrongly drop legitimate non-species resource fids**: any resource fid that `LookupByID` fails to resolve (timing, unloaded plugin forms, edge authored entries) is silently skipped in the resources path. The prior path (`includeSpecies=true`) still marks unresolved fids (intentional for the full sweep). Comment at `src/Main.cpp:834-838` acknowledges this tradeoff.

**Concrete lines:** `src/Main.cpp:829` (type skip), `834` (`else if (!includeSpecies) return;`), `1446-1456` (MarkResourcesForPlanet hardcodes false), `919-939` (MarkEsmSpeciesForPlanet uses the ESM map directly).

---

## 3. Are the attribute bits (`subobj+0x00 |= 0x7`) correct + complete?

**Yes for the bits the survey-% gate reads; the write is narrowly scoped and matches decompile.**

- `SetPlanetAttributeBits` (`src/Main.cpp:633-644`): `*reinterpret_cast<uint32_t*>(subobj) |= 0x7u;`
- ID_97851 (`survey-percent.txt:2391-2397`, `astro-starmap-handler-2026-06-24.txt:2391-2397`):
  - `*(byte*)(... + 0x1b0) = (bit1 of entry+0x20)`
  - `*(byte*)(... + 0x1b1) = (bit2 of entry+0x20)`
  - bit0 controls DV selection (`+0x24` vs fallback 4 at `+0x1b4`).
- FINDINGS.md:3.1 explicitly states: set low 3 bits at `entry+0x20` (== `subobj+0x00`) to gate the attribute categories (magnetosphere/resources/atmosphere/gravity/temp/water). Barren bodies reach 100% with this + resources + traits.
- The mod does **not** need to populate the aggregator's transient category counters (param_1[1..4] in ID_97851); it writes the durable slot flags that those readers **count**. The bits + `MarkSpeciesScannedForPlanet` on non-species aggregator fids are the two levers.
- No evidence of a 4th "resource attribute" bit outside these 3 that the mod must also poke for survey % to consider resources "known."

**Caveat:** if future survey-% weighting adds categories gated on other bits of the same mask (or on separate records under 938336), `|= 0x7` would be incomplete. Today it matches the proven gate.

**Concrete lines:** `src/Main.cpp:642` (`|= 0x7`), `633` (ResolvePlanetSubobj), ID_97851 reads at `lVar9+0x20` (entry+0x20).

---

## 4. Shared subobj slot hashmap — can resources corrupt species slots?

**Yes, the structure is shared and growth is not isolated.**

- Single per-planet 938333 subobj: attribute bitmask at `+0x00`; slot hashmap at `+0x18` (keys), slots at `+0x40` (0x30-stride entries with `+0x20` pct, `+0x21` flag, `+0x08` BSTArray). Both resources and species write via `MarkSpeciesScannedForPlanet` → `IncrementScanFlag`/`SetPercentByte` (ID_124898/ID_124899) into this table.
- `ForEachAggregatedFormId` + resources path calls `MarkSpeciesScannedForPlanet` on whatever fids the aggregator yields (after filter). This creates/grows slots for resource keys.
- `IncrementScanFlag` (and the hash/rehash inside it) can relocate or reallocate backing storage for the slot table. The species green path (`TestBuildArray`, `src/Main.cpp:1319-1332`) added slot-recover (re-Mark + re-read `* (base+0x40)` / `* (base+0x48)` + re-hash). The resources path has no equivalent.
- Ordering in `CompleteLifePlanets`/`CompletePlanet` (`psc:79`): resources before species for a life planet. Resources can grow/rehash before `TestDirectGreen`/`TestBuildArray` run. Under "all" the table is mutated by both categories.
- `docs/isolation-review.md:92-95` already flags this: "resources path ... can cause allocation/growth/rehash ... A later (or earlier) species green ... can see a rehashed table." The slot-recover only helps inside one green pass for keys that miss on the first hash.

**No direct key collision** (different fids hash to different slots), but the **backing arrays/buckets** are the shared mutable state. Engine rehash semantics are assumed to preserve prior entries; the code does not snapshot or use disjoint storage.

**Concrete lines:** `src/Main.cpp:605` (ResolvePlanetSubobj), `647` (MarkSpeciesScannedForPlanet calls the two writers), `1283-1325` (TestBuildArray re-read after potential grow), `809` (MarkEverythingForPlanet for resources), `1446` (entry point), isolation-review.md:50,62,92.

---

## Summary — concrete bugs/leaks (file:line)

- **Species leak under resources (root symptom):** `src/Main.cpp:756` (ForEachAggregatedFormId) + `809-849` (MarkEverythingForPlanet) enumerate aggregator fids and call `MarkSpeciesScannedForPlanet` on any fid that is not filtered by runtime `GetFormType()`. No positive ESM species membership test. When aggregator emits species keys that do not resolve as `kFLOR`/`kNPC_` (canonicals, LVLN, etc.), they are marked. Observed as `resources == species count`.
- **Type filter insufficient:** `src/Main.cpp:829` only checks resolved form type. Misses remapped/non-FLOR/NPC_ representations of species.
- **Null-form guard is a band-aid with downside:** `src/Main.cpp:834-839` correctly stops unresolved species under resources but can silently drop legitimate unresolved resource fids.
- **Explicit ESM guard absent:** `Esm::GetPlanetSpecies()` (populated at `EsmReader.cpp:920+`) is the authoritative species set; it is not consulted in the resources filter path.
- **Shared mutable slot table (growth/rehash risk):** `src/Main.cpp:605` (subobj), `647` (writes), `1283` (species recover only). Resources mutates the table that species later reads/writes without isolation or snapshot. Ordering-dependent.
- **Attribute bits appear complete for known gate:** `src/Main.cpp:642` (`|= 0x7`) matches the 3 bits read by ID_97851 (bit0/1/2 → DV + 0x1b0/0x1b1). No missing bit write identified for the documented attribute categories.

**Stronger purity would require:** either (a) disjoint key domains inside the 938333 subobj for resources vs. species, or (b) an explicit "is this fid a species key for this planet?" check derived from `Esm::GetPlanetSpecies()` (or the aggregator's own species vs. resource classification) before any `MarkSpeciesScannedForPlanet` in the `!includeSpecies` path. The current filter + guard is a symptomatic patch on a shared table design.
