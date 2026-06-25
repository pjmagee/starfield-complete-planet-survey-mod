<!-- grok-build (xhigh, read-only) review: papyrus - 2026-06-25 -->

**Read-only review complete.** Files read: [.grok-context.txt](.grok-context.txt), [Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc](/D:/Projects/pjmagee/starfield-complete-planet-survey-mod/Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc), [Data/Scripts/Source/User/CompletePlanetSurveyNative.psc](/D:/Projects/pjmagee/starfield-complete-planet-survey-mod/Data/Scripts/Source/User/CompletePlanetSurveyNative.psc), [src/Main.cpp](/D:/Projects/pjmagee/starfield-complete-planet-survey-mod/src/Main.cpp) (full).

---

## 1. Four command entry points — order of operations per category (discover / resources / traits / green)

### CompletePlanet (lines 50-101)
- Guard: `CategoriesValid` (51), `CancelPendingAutoComplete` (57).
- Guards: `IsInInterior` (63-65), `GetCurrentPlanet()==None` (67-71).
- **Order** (current planet only):
  1. `doResources`: `MarkResourcesForPlanet` (80) — no discover (entry already exists on-surface).
  2. `doTraits`: `MarkTraits` (84) then `_CompleteTraitScanObjects` (89) — DATA then in-world objects.
  3. `doSpecies`: `_GreenPlanet` (94) → `TestDirectGreen` then `TestBuildArray`.
- Always: `ScanNearbyRefs` (96), log + toast (98-100).

**Correct** per documented order ("resources before species so a following green wins").

### CompleteBarrenPlanets (lines 111-198)
- Guard: `CategoriesValid` (112), `CancelPendingAutoComplete` (116).
- Shows `CPSRecallMessage` (121-125).
- **Order**:
  1. Phase 1: `CompleteAllPlanetsSurveyData` (134) — C++ does `ScanCompletePlanet` (discover) + `WritePlanetSurveyState` (resources/attributes) for barren worlds only. Records forms.
  2. Phase 2 loop (142-159): `FinalizeSweptPlanet` (re-applies state + fires notify/slate) then (if `doTraits`) `MarkTraits`.
- No green path (by design — living worlds skipped).
- If `doTraits` + on-planet: `_CompleteTraitScanObjects` (169-171).
- `abShowResult` (190-192) controls the final `MessageBox`.

**Correct**: discover+resources fused in C++ sweep; traits added in Papyrus finalize pass; no species.

### CompleteLifePlanets (lines 210-279)
- Guard: `CategoriesValid` (210), `CancelPendingAutoComplete` (214).
- Extra early-out: `!doR && !doT && !doS` (218-221).
- `curPlanet = GetCurrentPlanet()` captured once (226).
- Enumerate + loop (229-259):
  1. **Discover (conditional)**: `If (doResources || doSpecies) && p != curPlanet` → `DiscoverPlanetEntry` (240-241). Comment explains: avoid async ID_102650 re-discover on the live planet (evicts ability markers).
  2. `doResources`: `MarkResourcesForPlanet` (244).
  3. `doTraits`: `MarkTraits` only (DATA) (250). No remote `_CompleteTraitScanObjects`.
  4. `doSpecies`: `_GreenPlanet` (253).
- Post-loop: if `doTraits` + on-planet: `_CompleteTraitScanObjects` (268-270).
- `abShowResult` (275-277) controls `MessageBox`.

**Correct** (with the documented recent guard). Discover is deliberately skipped for the current planet before resources/green.

### CompleteAllPlanets (lines 289-300)
- Guard: `CategoriesValid` (290).
- `CancelPendingAutoComplete`.
- Calls `CompleteBarrenPlanets(asCategories, false)` (293).
- Calls `CompleteLifePlanets(asCategories, false)` (294).
- Combined `MessageBox` (299).

**Correct** composition: both sweeps, single intro + single result. `abShowResult=false` plumbing works.

---

## 2. Recent guards

**CategoriesValid (all four entry points)**

- Checked first in CompletePlanet (51), CompleteBarrenPlanets (112), CompleteLifePlanets (210), CompleteAllPlanets (290).
- On invalid: notification + early return (no work). Consistent message text.
- C++ impl (Main.cpp:1589-1629): lowercases + trims; rejects any token not in {"all","resources","traits","fauna","flora","species","creatures"}; requires >=1 non-empty token after split. Empty/whitespace → `any=false` → false. "resource"/"creature"/"res" correctly rejected.
- No valid input wrongly rejected; no invalid accepted.
- **Double-notification?** CompleteAllPlanets checks first (290) and returns before calling subs. Subs never see an invalid string from it. No double toast path.

**DiscoverPlanetEntry skip for current planet (CompleteLifePlanets)**

- Line 226: `Planet curPlanet = Game.GetPlayer().GetCurrentPlanet()`.
- Line 240: `If (doResources || doSpecies) && p != curPlanet`.
- **Is `p != curPlanet` valid Papyrus?** Yes. Both are `Planet` (Form). Form identity comparison and `Form != None` are well-defined.
- **GetCurrentPlanet None-safe when in space?**
  - If None: `p != None` is true for any enumerated `p`, so Discover runs for all life planets. Correct (no current planet to protect).
  - CompletePlanet guards explicitly (67-71).
  - Trait object paths use `GetCurrentPlanet() != None` guards (169, 268).
  - CompleteSurveyIfEnabled short-circuits (321).
  - All sites are None-safe.

**abShowResult plumbing**

- Default `true`; CompleteAllPlanets passes `false` (293, 294).
- Barren (190) and Life (275) both gate their `MessageBox` on it.
- CompleteAllPlanets shows one combined box (299). Correct.

---

## 3. Helpers — None-deref, wrong cast, off-by-one, logic errors

**_GreenPlanet** (24-30)
- Passes `planetForm` (can be None) to natives. C++ returns -1 for null; Papyrus clamps `<0 → 0` (28-29). Safe.
- Calls both TestDirectGreen then TestBuildArray unconditionally. Build logs "no entry" and returns 0 if missing — matches documented precondition (caller should discover first for remote planets).

**_SpeciesKind** (35-42) / **_WantsSpecies** (405-407)
- Pure CategoryEnabled calls. Correct mapping: flora/species/creatures → flora side; fauna/... → fauna side; overlap or "all" → 0 (both).
- No Form deref.

**MarkTraits** (332-346)
- `akPlanet.IsTraitKnown(...)` and cast to Form (333). No internal None guard.
- All call sites guard: CompletePlanet after `currentPlanet != None`; Barren/Life inside `If p != None`. Latent robustness gap (None would fault), but not hit today.

**_CompleteTraitScanObjects** (360-401)
- `akPlayer.FindAllReferencesWithKeyword` (372) — would None-deref if passed None.
- Callers:
  - CompletePlanet: `playerRef as ObjectReference` (playerRef from GetPlayer, after interior/planet guards). Practically safe.
  - Barren/Life: guarded by `!bp.IsInInterior() && bp.GetCurrentPlanet() != None` before cast. The guard itself derefs `bp` (GetPlayer), so assumes player exists.
- Inside: `refs[i]`, `r.GetCurrentLocation()`, `loc.GetRefTypeAliveCount`, `loc.SetValue`, `sqp.DiscoverMatchingPlanetTraits(r, false)`. All guarded by `If refs`, `If r`, `If loc`. OK.
- `needed < 1` fallback to 1 (389-390) prevents 0/negative AV. Correct per comment (caps SetScanned overcount).

**_AutoCompleteCurrentPlanet** (410-412)
- Calls `CompletePlanet("all")`. No args. Matches C++ poller dispatch. OK.

**CompleteSurveyIfEnabled** (305-323)
- GPOF 0x80C lookup with None guard + log (311-314).
- `currentPlanet != None && currentPlanet.GetSurveyPercent() >= 1.0` short-circuit (320-322). OK.
- Queues rather than direct dispatch (documented race avoidance). OK.

**Life-planet loop (CompleteLifePlanets)**
- `While i < n` with `GetLifePlanetFormIdAt(i)`, `Game.GetForm(pid) as Planet`, `If p != None`. Standard. No off-by-one.
- `worlds += 1` only inside `If p != None` (258); greened count only on `_GreenPlanet > 0`. OK.
- No species count returned (returns `worlds`); `greened` only for logging/MessageBox. Matches signature/doc.

**General**
- No obvious casts of wrong type.
- No manual array length math that can drift.
- Native calls are all under the GuardedNative wrapper in C++ (crash → degraded + safe default).

---

## 4. Native DECLS vs C++ BindNativeMethod registrations

Papyrus decls (CompletePlanetSurveyNative.psc) vs bindings (Main.cpp:1641-1703). 17 vs 17.

| Papyrus name (decl)              | Papyrus sig (decl)                          | Bound Papyrus name (C++)                    | C++ impl fn                  | Return/args match? |
|----------------------------------|---------------------------------------------|---------------------------------------------|------------------------------|--------------------|
| DebugLog                         | (string)                                    | "DebugLog"                                  | DebugLog                     | Yes (void) |
| MarkTraitKnownForPlanet          | (Form, Keyword) → bool                      | "MarkTraitKnownForPlanet"                   | MarkTraitKnownForPlanet      | Yes |
| TestDirectGreen                  | (Form, int) → int                           | "TestDirectGreen"                           | TestDirectGreen              | Yes |
| TestBuildArray                   | (Form, int) → int                           | "TestBuildArray"                            | TestBuildArray               | Yes |
| MarkResourcesForPlanet           | (Form, int) → int                           | "MarkResourcesForPlanet"                    | MarkResourcesForPlanet       | Yes |
| DiscoverPlanetEntry              | (Form) → int                                | "DiscoverPlanetEntry"                       | DiscoverPlanetEntry          | Yes |
| ScanNearbyRefs                   | () → int                                    | "ScanNearbyRefs"                            | ScanNearbyRefs               | Yes |
| QueueCompleteSurvey              | ()                                          | "QueueCompleteSurvey"                       | QueueCompleteSurvey          | Yes (void) |
| CancelPendingAutoComplete        | ()                                          | "CancelPendingAutoComplete"                 | CancelPendingAutoComplete    | Yes (void) |
| CompleteAllPlanetsSurveyData     | () → int                                    | "CompleteAllPlanetsSurveyData"              | CompleteAllPlanetsSurveyData | Yes |
| GetSweepPlanetCount              | () → int                                    | "GetSweepPlanetCount"                       | GetSweepPlanetCount          | Yes |
| GetSweepPlanetFormIdAt           | (int) → int                                 | "GetSweepPlanetFormIdAt"                    | GetSweepPlanetFormIdAt       | Yes |
| FinalizeSweptPlanet              | (int) → int                                 | "FinalizeSweptPlanet"                       | FinalizeSweptPlanet          | Yes |
| EnumerateLifePlanets             | () → int                                    | "EnumerateLifePlanets"                      | EnumerateLifePlanets         | Yes |
| GetLifePlanetFormIdAt            | (int) → int                                 | "GetLifePlanetFormIdAt"                     | GetLifePlanetAt              | Yes (name is Papyrus name) |
| CategoryEnabled                  | (string, string) → bool                     | "CategoryEnabled"                           | CategoryEnabled              | Yes |
| CategoriesValid                  | (string) → bool                             | "CategoriesValid"                           | CategoriesValid              | Yes |

**Findings**
- All decls have a binding; all bindings have a decl.
- `GetLifePlanetFormIdAt` (Papyrus name) binds to `GetLifePlanetAt` (C++ name) — correct (Papyrus-visible name is what matters).
- No orphan decls or unregistered bindings.
- `ScanNearbyRefs` is declared `int` and C++ returns 0; Papyrus callers ignore the result. Harmless.
- Argument/return types align (Form/Keyword/string/int/bool → TESForm*/BGSKeyword*/BSFixedString/int32_t/bool).

---

## Summary of issues (with line numbers)

**None (hard defects) found in the reviewed areas.** The following are minor/notes:

- **MarkTraits lacks internal None guard** (332-333): `akPlanet.IsTraitKnown` / cast. Callers currently guard, but a direct call with None would fault. (Papyrus side.)
- **"nothing selected" path in CompleteLifePlanets (218-221) is unreachable after a passing CategoriesValid**, because every token CategoriesValid accepts maps to at least one of doResources/doTraits/doSpecies. Defensive, not harmful.
- **ScanNearbyRefs called unconditionally at CompletePlanet end (96)** even for traits-only or resources-only. Harmless (queues a no-op refresh), but not strictly required for non-species categories.
- Cosmetic: double-space in some native decls (`int  Function`) in Native.psc — no semantic effect.
- No double-notification between CompleteAllPlanets and subs on invalid input (CompleteAllPlanets guards first).
- `p != curPlanet` (240) and GetCurrentPlanet None handling are valid and safe per Papyrus rules and surrounding guards.

All four command flows implement the documented discover→resources→traits→green ordering (with the explicit current-planet Discover skip for life planets). Guards are consistent. Native surface matches the C++ registrations.
