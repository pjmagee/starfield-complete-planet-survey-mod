<!-- grok-build (medium, read-only) CONSOLIDATED SYNTHESIS of the 5 reviews - 2026-06-25 -->

**1. Verdict**

The uncommitted state (CategoriesValid guard on all four entry points, DiscoverPlanetEntry skip for the live planet in CompleteLifePlanets, null-form guard under `!includeSpecies`, idempotent green with slot-recover + Notify in TestBuildArray) is a symptom-level patch set on a fundamentally shared mutable structure. It is not yet safe to tag v1.2.0.

Two release-blocking issues remain unaddressed:
- Resources path still walks the aggregator and can mark species keys (via non-FLOR/NPC_ canonicals or LVLN forms) into the shared 938333 subobj slot table.
- Traits bulk-write via ID_52155 produces a provably malformed 938333 record (Save28-31 diffs) that contradicts FINDINGS.md §2.3.

Crash-safety surface is improved but the traits malformation and shared-table mutation are the exact class of defect that previously produced heap corruption and player-visible blue states.

**2. Findings table** (deduplicated, Critical→Low)

| Severity | Area | Finding (1 line) | file:line | Source report(s) |
|----------|------|------------------|-----------|------------------|
| Critical | Resources purity | Aggregator walk + type filter + null-guard still lets species keys reach MarkSpeciesScannedForPlanet under resources (observed: resources==17 on 17-spp life planet). | src/Main.cpp:756 (ForEach), 809-849 (MarkEverything), 829 (FLOR/NPC_ test), 834 (null guard), 1455 (call with false) | resources, isolation-review |
| Critical | Traits durability | Bulk MarkTraits + ID_52155 writes malformed 938333 record (ARRAY_A extras + ARRAY_B misaligned count vs real incremental scan); contradicts FINDINGS 2.3. | psc:347, Main.cpp:1214/345, re/save/trait_complete_findings_2026-06-24.py | traits |
| High | Species green completeness | +0x21 written by TestDirectGreen/MarkEsm/recover; +0x08 can be skipped (SLOT-MISS after recover) or under-filled (fallback, derivation gap) → blue or partial panel. | Main.cpp:919 (MarkEsm), 1322 (recover Mark), 1331 (SLOT-MISS continue), 1346 (fallback), 1369 (clear+push) | species |
| High | Shared mutable state | Resources and species both mutate the single 938333 subobj slot hashmap (subobj+0x18/0x40/0x48); resources can grow/rehash before or between species passes under "all". | Main.cpp:605 (Resolve), 647 (MarkSpecies), 1283-1325 (TestBuildArray reads), 809 (resources path) | resources, species, isolation-review |
| Med | Traits materialization | Pure remote "traits" (no resources/species) skips DiscoverPlanetEntry; ID_52205 gate may see missing 938333/937887/938336 components. | psc:240 (condition only on doR\|doS), Main.cpp:1428 | traits |
| Med | Green key stability | CanonicalFormId has local /EHa try/catch returning 0 (fallback to raw); two calls (write vs resolve) can diverge on transient fault. | Main.cpp:577 (Canonical), 934 (MarkEsm write), 1314 (TestBuildArray resolve) | species, relink |
| Med | BSTArray leak | TestBuildArray zeros +0x08/+0x10/+0x18 without EngineScalarFree on prior engine-owned buffer. | Main.cpp:1369-1371 (clear) vs 272/285 (free only via engine) | species |
| Med | Notify duplication | Both CompletePlanetSurveyState (resources) and TestBuildArray (species) call NotifySurveyProgress per planet under "all". | Main.cpp:890, 1369 | species, isolation-review, traits |
| Low | Papyrus robustness | MarkTraits has no internal None guard on akPlanet (callers currently guard). ScanNearbyRefs called unconditionally at CompletePlanet end. | psc:332-333, 96 | papyrus |
| Low | Comment/doc drift | Stale "+0x21 alone" comments and old planet-key-domain text remain; code behavior + later re/ docs say write BOTH. | Main.cpp:233-238, 216-218; FINDINGS 1.10 vs 1.11; species-scan-complete-model vs complete-scan-green-model | relink, species |
| Low | Native surface | All 17 decls match registrations; cosmetic double-spaces in Native.psc. | Native.psc:*, Main.cpp:1641-1703 | papyrus |

**3. Crash-safety / memory-safety call-out**

- **Traits malformation (Critical)**: bulk ID_52155 on GetKeywordTypeList(44) produces 152-byte vs 58-byte records in GlobalData R1. This is exactly the heap/record-shape corruption class the repo history forbids.
- **Shared slot table + recover re-read (High)**: resources/species both drive IncrementScanFlag/BSTArrayU32Grow on the same per-planet subobj. Slot-recover re-reads base+0x40/0x48 after a Mark but holds no snapshot across categories. A rehash that relocates during/after a resources Mark followed by a species resolve is an unproven assumption.
- **BSTArray header clear without free (Med)**: prior engine-owned u32[] buffer becomes unreachable; small per-species leak but unbounded across planets.
- No hand-written cap/size pokes remain (good). All native paths are CPS_GUARDED + /EHa.

These three gate a release.

**4. Cross-review CONTRADICTIONS or consensus**

**Consensus (high confidence):**
- CategoriesValid + current-planet Discover skip + unconditional Notify after TestBuildArray are correct and match documented ordering.
- Traits write path is operationally isolated from species/resource slot writes (different ID_52155 member-array path inside the same 938333 container).
- +0x21 alone is insufficient for full green (panel + ability creatures); both fields are required in practice.

**Disagreement / open tension:**
- Green model: relink correctly notes internal re/ evolution (slot-0x08-catalogue-writer "+0x21 alone" vs complete-scan-green-model "write BOTH" + in-game evidence). Code now does both; FINDINGS.md still leads with 1.10 ("+0x21 alone") before the 1.11 practical rule.
- Resources isolation: resources report says type+null guard is insufficient (aggregator can emit non-FLOR/NPC_ species keys). Papyrus report sees the guard as recently added and effective for the tested cases. Isolation-review agrees the guard is a band-aid on a shared table.
- Traits byte-correctness: traits report cites fresh Save28-31 diffs showing malformation; FINDINGS §2.3 claims "byte-identical" from older saves. Contradiction is real and save-version specific.

**5. Likely FALSE POSITIVES**

- "curCount > markers.size() can never skip a complete slot" (species): the check is `==`, so a larger real array would fail the skip and rebuild (a false negative for the idempotent intent, not a wrongful skip). Low practical impact given fallback size=2.
- "Null-form guard can drop legitimate resources" (resources): correct observation, but the aggregator is the engine's own survey-% source of truth; unresolved fids under resources are rare and the prior behavior (marking them) was the leak vector. Acceptable tradeoff if the ESM-positive filter is added.
- "Double Notify under all is a correctness bug": it is observable noise only; ID_97853 is documented as check-and-dispatch and the slate award is idempotent. Not a blocker.
- Stale comments in Main.cpp are not behavioral defects.

**6. Prioritized action list before v1.2.0**

**Must-fix-before-commit**
- Add positive ESM species membership test in the resources path (use Esm::GetPlanetSpecies() as allowlist) so MarkSpeciesScannedForPlanet is never called for a species key under includeSpecies=false. Resolves Critical resources leak. (Main.cpp:820-849 + resources report + isolation-review Q5)
- Investigate + document (or fix) the traits malformation: either switch traits path to incremental single calls matching real scan shape, or accept and update FINDINGS.md + add a save-diff regression test. Do not ship claiming byte-identical state. Resolves Critical traits durability. (traits report + Save28-31 evidence)
- Close the SLOT-MISS path after recover in TestBuildArray (or guarantee the Mark always produces a resolvable slot for the key just written). Resolves High partial-green. (Main.cpp:1331)
- Add explicit EngineScalarFree (or avoid clobbering) for the prior BSTArray buffer on rebuild. Resolves Med leak. (Main.cpp:1369)

**Follow-up / nice-to-have**
- Unify NotifySurveyProgress to a single call after all per-planet categories (or at top-level command end) to eliminate N×2 traffic. (isolation-review Q3)
- Make CanonicalFormId key derivation stable or add cross-check that the key used for +0x21 write equals the key used for +0x08 build. (species + relink)
- Strengthen remote pure-traits path or document that traits on never-visited worlds may be a no-op until a discover touch. (traits report)
- Clean stale "+0x21 alone" / planet-key-domain comments in Main.cpp and collapse FINDINGS 1.10/1.11 into one coherent practical rule. (relink)
- Add None guard inside MarkTraits (defensive). (papyrus)

This synthesis is derived solely from the five provided reports + direct reads of cited sources. No edits were made.