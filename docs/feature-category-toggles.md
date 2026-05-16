# Feature Plan: Per-Category Survey Toggles

## Goal

Replace the single "auto-complete on scan" setting with four independent
category toggles so players can opt in per category:

- Resources (Enabled / Disabled)
- Flora (Enabled / Disabled)
- Fauna (Enabled / Disabled)
- Traits (Enabled / Disabled)

All four default to **Enabled** so existing users see no behaviour change
after updating.

## Current State

One GPOF (`CPSScanAutoComplete`, FormID `0x80C` in `CompletePlanetSurvey.esm`)
gates the entire scan-hook auto-complete path. When enabled, the scan hook
queues `CompleteSurvey`, which unconditionally runs all four phases:

| Phase     | Driver                                                                                         |
| --------- | ---------------------------------------------------------------------------------------------- |
| Traits    | [CompletePlanetSurveyQuest.psc:37-38](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc#L37-L38) → `MarkTraits` → `MarkTraitKnownForPlanet` native |
| Resources | [CompletePlanetSurveyQuest.psc:39](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc#L39) → `MarkResourcesForPlanet` native                        |
| Species   | [CompletePlanetSurveyQuest.psc:40](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc#L40) → `SpawnAndScanAllPlanetSpecies` (flora + fauna combined) |
| Refresh   | [CompletePlanetSurveyQuest.psc:41](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc#L41) → `ScanNearbyRefs` native                                |

The species phase iterates a single cache of `kFLOR` + `kNPC_` form IDs
populated by [Main.cpp:383-402](src/Main.cpp#L383-L402).

## Proposed State

### ESM (Creation Kit)

Replace the single GPOF with four GPOFs under the same GPOG parent
(`CPSGameplayOptions`). Suggested editor IDs:

- `CPSScanCompleteResources`
- `CPSScanCompleteFlora`
- `CPSScanCompleteFauna`
- `CPSScanCompleteTraits`

Creation Kit reassigns FormIDs on save — each new GPOF's FormID must be
recorded (xEdit or CK log) and pinned in Papyrus the same way `0x80C` is
pinned today. Prefer keeping `0x80C` as one of the four (e.g. reuse it for
`CPSScanCompleteResources`) so the existing ID stays hot and only three new
IDs need discovery.

Decision: drop the legacy `CPSScanAutoComplete` GPOF entirely rather than
keep a fifth "master" toggle. Four defaults-on toggles is equivalent and
avoids two-level gating UX.

### Papyrus

In [CompletePlanetSurveyQuest.psc](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc):

1. Add a helper `GetCategoryOption(int whichFid) global` that wraps the
   `GetFormFromFile` → `as GameplayOption` → `GetValue()` pattern currently
   inlined in `CompleteSurveyIfEnabled`. Returns `True` if the option is
   enabled (or if the GPOF is missing — fail-open preserves shipping
   behaviour).
2. Change `CompleteSurvey()` so each phase is gated on its own toggle:
   - Traits phase wrapped in `If TraitsEnabled()`
   - Resources phase wrapped in `If ResourcesEnabled()`
   - Species phase split into two passes (see below) each gated separately
   - `ScanNearbyRefs` runs unconditionally (pure visual refresh, cheap)
3. Change `CompleteSurveyIfEnabled()` to short-circuit only if **all four**
   toggles are off. Otherwise queue `CompleteSurvey` and let the per-phase
   gates decide what runs.

### Splitting flora vs fauna

The native cache mixes FLOR and NPC_ form IDs. Two options:

- **Option A (Papyrus filter, preferred).** Keep `EnumeratePlanetSpecies`
  as-is. In `SpawnAndScanAllPlanetSpecies` cast each form — `speciesForm as
  Flora` non-None means flora, otherwise fauna — and skip the ones whose
  category toggle is off. Zero native changes.
- **Option B (split natives).** Add `EnumeratePlanetFlora` and
  `EnumeratePlanetFauna` that filter at the C++ layer. Cleaner, but doubles
  the cache surface and needs a DLL rebuild.

Go with **Option A** unless the filter causes a measurable Papyrus cost on
dense planets (it shouldn't — the cast is cheap and species counts are
capped at 128).

### Native (C++)

No required changes. `QueueCompleteSurvey` / `ScanNearbyRefs` stay as-is.

## Default Values

All four GPOFs default to 1.0 (Enabled) in the CK. Result: a player who
upgrades without touching settings gets identical behaviour to today.

## Migration

`CPSScanAutoComplete` (`0x80C`) is dropped. If it's reused as one of the
four new GPOFs (e.g. `CPSScanCompleteResources`) an existing save with the
toggle OFF will read as "resources disabled" after update — acceptable,
since the other three categories default on and the overall auto-complete
still functions. Call this out in the changelog.

If we choose to assign a fresh FormID instead and retire `0x80C`, existing
saves always read the new GPOFs as their default (Enabled). Simpler story,
one unused FormID in the ESM.

Pick the retire-and-reassign path: cleaner, no silent semantic change for
existing saves.

## Edge Cases

- **GPOF missing.** `GetCategoryOption` returns `True` (fail-open). Logs a
  single warning per missing FormID, not per scan, to avoid log spam.
- **All four disabled.** `CompleteSurveyIfEnabled` returns before queueing.
  Console `cgf "CompletePlanetSurveyQuest.CompleteSurvey"` still runs all
  phases unconditionally — console override is the escape hatch.
- **Traits-only run.** Cheap; no species spawn/scan/delete cycle.
- **Species-only run (flora OR fauna off).** The spawn loop still runs but
  skips the disabled kind before `PlaceAtMe`, so no wasted refs.
- **Survey % < 100% after partial run.** Existing `If surveyAfter < 1.0`
  notification already handles this. No new wording needed — the user
  chose the partial behaviour.

## Open Questions

1. Is a "Refresh nearby refs" toggle worth exposing too? Current plan
   leaves it unconditional because it's a visual-only sweep. Revisit if
   users report outline flicker on disabled categories.
2. Settings menu ordering: traits first (cheap, fast) or resources first
   (most requested category)? CK controls display order via GPOF record
   order under the GPOG.
3. Should the Papyrus helper cache the GPOF form lookups? Lookups are
   O(hash), called once per scan, so probably not worth the state.

## Out of Scope

- MCM / SkyUI-style in-game menu. Native Settings > Gameplay menu only.
- Per-biome toggles. Still whole-planet granularity.
- Persisting toggle state across different characters/saves beyond what
  the engine already does for GPOFs.
