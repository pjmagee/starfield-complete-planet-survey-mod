<!-- grok-build (medium, read-only) review: traits - 2026-06-25 -->

**TRAITS completion deep review (read-only).**

Focus: `MarkTraits` (psc) → `MarkTraitKnownForPlanet` → `Engine::MarkTraitKnown` (ID_52155 / 938333 trait member-array) and `_CompleteTraitScanObjects` (SQ_Parent.DiscoverMatchingPlanetTraits + PlanetTraitLocationScanCount AV path).

Sources read: `.grok-context.txt`, `re/FINDINGS.md`, `src/Main.cpp` (full), `Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc`, `CompletePlanetSurveyNative.psc`, `re/ghidra/output/astro-trait-known-synthesis-2026-06-24.md`, `re/save/trait_complete_findings_2026-06-24.py`, `re/save/decode_pk_record.py`, `re/save/diff_trait_sections_2026-06-24.py`, `re/esm/pex/sq_parentscript.psc`, `docs/isolation-review.md`, `docs/COMPLETION-COMMANDS.md`, targeted greps.

All claims cite `file:line` + RE source. No edits, no builds, no in-game assertions.

### 1. Is the trait-known write (938333 / ID_52155) durable + byte-correct vs. a real scan? Malformed/transient record risk?

**No — concrete evidence of malformed record on the shipped path.**

- The traits DATA path is `CompletePlanetSurveyQuest.psc:347` (`MarkTraits`):
  ```papyrus
  traitCount = MarkTraits(currentPlanet, currentPlanet.GetKeywordTypeList(44))
  ...
  If !akPlanet.IsTraitKnown(...) then MarkTraitKnownForPlanet(planetForm, kw)
  ```
- Native: `CompletePlanetSurveyNative.psc:10` → `src/Main.cpp:1209` (`MarkTraitKnownForPlanet`):
  ```cpp
  const auto planetId = Engine::ReadPlanetId(planetForm);
  return Engine::MarkTraitKnown(planetId, keyword);  // 1214
  ```
- `src/Main.cpp:345` (`MarkTraitKnown`):
  ```cpp
  SetTraitKnownNative(planetId, reinterpret_cast<std::uintptr_t>(keyword), true);  // REL::ID(52155)
  ```
- Per `re/ghidra/output/astro-trait-known-synthesis-2026-06-24.md:34,96-110` (decompile-verified): ID_52155 → ID_52205 gate (CTDA) → ID_52156 (write `key=(938333<<48)|(planetId<<16)` into the trait member-array via ID_52204 insert or in-place) + `PlanetTraitKnownEvent` + unconditional `ID_97853`.

**Byte mismatch vs. real scan (save-verified in the dedicated diff):**
- `re/save/trait_complete_findings_2026-06-24.py:1-140` (Save28 baseline → Save30 REAL 2/2 vs Save31 MOD via `CompletePlanet "traits"` on same planet/trait):
  - Real delta (1/2→2/2): `ARRAY_A` (pooled keyword member array = the trait "slot+0x08" analogue) appends exactly one entry `[0x00225588]`; `ARRAY_B` (per-trait canonical-slot array) count stays 1, slot[0].flag 1→2 (pct stays 100).
  - Mod: `ARRAY_A=[0x225590,0x22558D,0x225588]` ("2 bogus + 1 real"); `ARRAY_B` count=6 with "misaligned ids" → 152-byte malformed record (real target 58 bytes).
  - Script verdict: "the mod writes a MALFORMED 152-byte structure ... That corruption is why the in-world object reloads as 'UNKNOWN FEATURE 0/2'."
- Record grammar (decode-verified): `+0x10 ARRAY_A count + u32[] members (KWIDs ^ 0x00400000)`; `ARRAY_B count` then 10-byte slots `<id ^ TAG, flag (slot+0x21), pct (slot+0x20), pad u32>`. Lives in GlobalData R1, not ChangeForms. See `re/save/decode_pk_record.py:2-100` and `re/save/diff_trait_sections_2026-06-24.py`.

**Contradiction with FINDINGS.md:** §2.3 claims "save-verified" + "byte-identical to a real scan" (citing `compare_save19.py`/`compare_save21.py`). Those earlier saves may have covered single-trait or different conditions; the Save28-31 series specifically exercises the mod's "mark the planet's full GetKeywordTypeList(44) set" path and documents structural corruption.

**Root cause class:** Bulk calls to ID_52155 (for every trait on the planet) vs. the engine's natural incremental single-trait scan path produce different internal array layout that serializes incorrectly. No direct array poke in the mod (it uses the engine writer), but the call pattern + preconditions are not equivalent to a real scan. Also: `astro-trait-known-synthesis-2026-06-24.md:165` requires the planet's 938333/937887/938336 components to be materialized for the ID_52205 gate; a pure remote "traits" on a never-visited body may short-circuit silently.

**Durable?** The 938333 write itself persists (R1 GlobalData). The *contents* can be wrong.

### 2. Is "traits" genuinely isolated from species/resources? Does it touch the species/resource slot hashmap (+0x20/+0x21/+0x08) or SetPlanetAttributeBits?

**Write sites: yes, isolated. Container sharing exists but writers are disjoint.**

- `src/Main.cpp` (full grep): `ResolvePlanetSubobj`, `SetPlanetAttributeBits`, `IncrementScanFlag`, `MarkSpeciesScannedForPlanet`, `SpeciesSlotHash`, `PushSpeciesAttr` are **only** called from:
  - Resources: `MarkResourcesForPlanet:1450` → `CompletePlanetSurveyState:889` (includeSpecies=false) → `WritePlanetSurveyState:880` → `SetPlanetAttributeBits:636` + `MarkEverythingForPlanet:809` → `MarkSpeciesScannedForPlanet:647` (which does `IncrementScanFlag` + `SetPercentByte` on subobj slots at +0x20/+0x21).
  - Green/species: `MarkEsmSpeciesForPlanet:919` + `TestBuildArray:1274` (hashmap walk at base+0x40/0x48, slotAddr = slots+idx*0x30, clear +0x08 headers, `PushSpeciesAttr`).
- Trait path touches **none** of them:
  - `MarkTraitKnownForPlanet:1209` body only reads planetId and calls `Engine::MarkTraitKnown`.
  - `Engine::MarkTraitKnown:345` only does `SetTraitKnownNative(ID_52155)`.
  - In-world objects: pure Papyrus at `CompletePlanetSurveyQuest.psc:360` (`_CompleteTraitScanObjects`): `loc.SetValue`, `r.SetScanned(true)`, `sqp.DiscoverMatchingPlanetTraits(r, false)`. Zero C++ subobj or slot code.

- Confirmed in `docs/isolation-review.md:55` table: traits → "938333 trait member-array" + "Location actor values + SQ_Parent" (RW column); no subobj slot row.
- `re/FINDINGS.md:2.1`: "written by `ID_52155` ... into the planet's `938333` trait member-array". Separate from species slots (which use FNV `ID_124901` into the same 938333 entry's subobj+0x40 area).

**Shared container caveat (not a write isolation failure):** The 938333 record contains both the trait member-array *and* the species slot table (per `re/FINDINGS.md:1.14,2.1`; `db+0x268` entry+0x20 subobj). Survey % readers (`ID_97851` via aggregator) and some outline paths walk the record. Traits fire `ID_97853` (astro doc L147). This is structural sharing, not mutation overlap. No evidence traits clobber species +0x21/+0x20/+0x08 bytes or attribute bits (`subobj+0x00`).

**Verdict:** Operationally isolated on the write paths. The command "traits" category does not invoke any species/resources machinery.

### 3. The in-world scan-target object path (`_CompleteTraitScanObjects`): PlanetTraitLocationScanCount logic correct? Over-count / corruption? Remote/auto-resolve claim?

**Logic is a best-effort mirror of the shipped game scripts; over-count mitigation is explicit but not bulletproof. Remote claim holds per decompile + game scripts.**

- `CompletePlanetSurveyQuest.psc:360` (`_CompleteTraitScanObjects`):
  ```papyrus
  refs = FindAllReferencesWithKeyword(0x001CBEA3, ...)
  For each r:
      loc = r.GetCurrentLocation()
      r.SetScanned(true)
      needed = loc.GetRefTypeAliveCount(sqp.PlanetTraitScanTargetLocRef)
      If needed < 1 then needed=1
      loc.SetValue(sqp.PlanetTraitLocationScanCount, needed as float)  ; absolute
      sqp.DiscoverMatchingPlanetTraits(r, false)  ; incrementScanCount=false
  ```
- Game script (`re/esm/pex/sq_parentscript.psc:483-510` `DiscoverMatchingPlanetTraits`):
  - `scanCountNeeded = GetRefTypeAliveCount(...)`
  - `scanCount = GetValue(...)`
  - If `incrementScanCount`: `scanCount += 1`; `SetValue(...)`
  - If `scanCount >= needed`: `SetExplored(True)` + `UpdatePlanetTraitDiscoveryPrivate` (eventual `DiscoverPlanetTrait` → `planet.SetTraitKnown`).
  - With `false`: it uses the AV we just forced, does not +=1, still triggers explore when >=.

- **Over-count (3/2 etc.):** `SetScanned(true)` can fire `OnScanned` → `Discover(..., true)` which does a +1 before our `SetValue`. The code sets *after* SetScanned and uses absolute `needed`, then a final `Discover(..., false)`. Comment at `psc:386`: "ABSOLUTE, so it also CAPS any extra +1 ... (no 3/2)". This matches the intent documented in `re/FINDINGS.md:87` (the "shipped" Papyrus-only fix).

- **needed source:** `GetRefTypeAliveCount(PlanetTraitScanTargetLocRef)` on the ref's current location. Per `re/FINDINGS.md:4.5` (tool-validated): scan-target REFRs are static, authored via LCTN overlays (LCSR entries with LocRefType 0x0027A567); typically M=2 per overlay, 26/27 traits have overlays. Risk if a ref's location is not the overlay location owning the count, or dead refs, or multiple traits sharing location state — but walking the loaded refs that carry the scan kw should bind to the correct loc.

- **On-planet only:** Correct. The objects + their Location are materialization-bound / loaded-ref only (`re/FINDINGS.md:4.4,4.1`: 939118 per-ref transient component with stub serializer; no durable per-REFR store; "ON-PLANET / loaded-ref ONLY").

- **Remote / auto-resolve on arrival claim:** Holds. `sq_parentscript.psc:561` (`CheckForScanTargetUpdate`, called from `PlanetTraitScanTargetScript.OnLoad`):
  ```papyrus
  If planetToCheck.IsTraitKnown(theData.PlanetTrait):
      Self.UpdateScanTarget(refToCheck)  ; does SetScanned(True)
      locationToCheck.SetExplored(True)
  ```
  This is the Astrophysics orbital-discover → land flow. Marking the trait via 938333 (`MarkTraitKnown`) makes `IsTraitKnown` true, so future OnLoad auto-finishes the objects. Explicit `_CompleteTraitScanObjects` is only for the "already on-planet when we mark" case (OnLoad already fired). Documented in `psc:166,202,263` and `re/FINDINGS.md:90,4.6`.

**Gaps:** No guard that the location we got from the ref is the one the game uses for the trait's overlay count. `needed < 1` fallback to 1 is a band-aid. `FindAllReferencesWithKeyword` can miss overlay copies (registry walk was explored for other reasons but not used here).

### 4. Does the traits path interact badly with species/resources writes or survey recompute under "all"?

**Data mutation: no. Side effects (recompute ordering, entry materialization, event volume): yes, low-severity.**

- No slot/attribute mutation (Q2).
- Recompute: ID_52155 path unconditionally calls `ID_97853` (astro synthesis L147). Resources does `NotifySurveyProgress` (ID_97853) at `src/Main.cpp:890`. Species now does it at `TestBuildArray:1369` (added for repaint). Under `CompletePlanet "all"` or `CompleteLifePlanets "all"` or galaxy "all" you get multiple per-planet calls. `ID_97853` is check-and-dispatch (slate awarded once, idempotent per comments), but produces extra UI events and recomputes.
- Ordering under remote sweeps (`psc:230` CompleteLifePlanets loop):
  - Discover only if `(doResources || doSpecies) && p != curPlanet` (`psc:240`). Pure "traits" skips it.
  - Then traits (`MarkTraits`), then species.
  - Comment at `psc:250`: "(Trait-KNOWN via MarkTraits is self-sufficient.)"
- Materialization gap (potential correctness bug for pure traits on remote never-visited): `astro-trait-known-synthesis-2026-06-24.md:165` and decompile: ID_52205 gate needs 938333/937887/938336 components present. A planet resolved only via `Game.GetForm` + `GetKeywordTypeList(44)` may not have them until a discover-like touch. Resources/species force `DiscoverPlanetEntry` (ID_102650); traits does not in the remote life-planet path. If the gate fails, the write is a silent no-op. (Earlier saves "worked" may have been on already-discovered bodies or after other categories.)

- Under barren sweep (`psc:140`): traits happen in the finalize Papyrus pass after the C++ sweep (which does discover + resources for barren). Safer ordering there.
- No evidence of traits clobbering species/resource state or vice-versa on the 938333 sub-structures.

**Concrete interaction under "all":** extra `ID_97853` traffic + reliance on "traits is self-sufficient" for entry materialization on never-visited pure-traits worlds.

### Summary of concrete bugs/gaps (with citations)

1. **Malformed 938333 record on traits completion** (`re/save/trait_complete_findings_2026-06-24.py:110` (Save31 mod vs Save30 real); ARRAY_A extras + ARRAY_B count=6 misaligned). Bulk `MarkTraits` + `GetKeywordTypeList(44)` + ID_52155 does not produce the same serialized layout as incremental real scan. Contradicts `re/FINDINGS.md:57`. (`src/Main.cpp:347` call site, `psc:347`, astro L96.)

2. **Remote pure-"traits" may not materialize the knowledge components** required by the ID_52205 gate. Discover is deliberately skipped for traits-only on remote life planets (`psc:240-250`). Astro doc requires them (`astro-trait-known-synthesis-2026-06-24.md:165`).

3. **Multiple unconditional survey recomputes** under any "all" path (traits ID_97853 + resources Notify + species Notify at `TestBuildArray:1369`). Not a data corruption but observable side-effect noise.

4. **_CompleteTraitScanObjects relies on FindAllReferencesWithKeyword + GetRefTypeAliveCount** without the registry walk used elsewhere for loaded scannables; can miss instances or use wrong location for the count. Over-count mitigation is post-hoc absolute SetValue (`psc:384-393` vs `sq_parentscript.psc:497`).

5. **Isolation is operational (writers disjoint) but not structural** — 938333 is a shared container for trait member-array and species slots. No cross-mutation today, but future changes assuming "this record only has species data" would be unsafe (`re/FINDINGS.md:1.14,2.1`; `src/Main.cpp:605` subobj layout comments).

No other direct touches (slot hashmap, attribute bits, species markers) from traits code. The in-world objects path is correctly Papyrus-only and the remote auto-resolve claim is backed by the game scripts' OnLoad path. The primary durability/byte-correctness problem is #1 above.