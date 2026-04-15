# Design: Auto-Complete on Remote Scan

**Branch:** `investigate/astrodynamics-rank4`
**Goal:** When a player scans a planet **from orbit or the starmap** (not on its surface), auto-complete that planet's survey to 100% — bypassing the Astrodynamics Rank 4 perk entirely (range + 50% trait chance), because the mod fully completes fauna/flora/resources/traits anyway.

---

## 1. Settings

Single new GPOF under the existing GPOG `CPSGroup`:

| Field            | Value                                              |
|------------------|----------------------------------------------------|
| EditorID         | `CPSRemoteScanAutoComplete`                        |
| Display name     | `Auto-Complete Survey on Remote Scan`              |
| Description      | `When you scan a planet from orbit or the starmap, instantly complete its full survey` |
| Type             | Bool (GPOF)                                        |
| Default          | ON                                                 |
| Parent GPOG      | `CPSGroup` (existing)                              |

Independent of the existing `CPSScanAutoComplete` toggle — a player can enable either, both, or neither.

Authored in Creation Kit alongside the existing records, then `import-esm.bat` copies the ESM back into the repo. Papyrus reads the FormID via `Game.GetFormFromFile(<new_id>, "CompletePlanetSurvey.esm")` — ID captured post-CK-save, same fragile pattern as `0x80C`.

---

## 2. Event signal

From decompiled `actor.psc`:

- `Event OnPlayerPlanetSurveyComplete(Planet akPlanet)` — fires at 100%, too late.
- `Event OnPlayerScannedObject(ObjectReference akScannedRef)` — fires for ref scans. **Unknown** whether the starmap "Scan" action triggers it on a planet body ref. First empirical test.

No dedicated `OnPlayerPlanetSurveyProgress` event exists in the decompiled scripts — trait-reveals are silent to Papyrus.

### Phase 1 — Papyrus-only (try first)

Register a quest-scoped remote event: `Actor.OnPlayerScannedObject`. Handler:

1. Guard on `CPSRemoteScanAutoComplete` toggle.
2. Resolve the `akScannedRef` to a `Planet`. Possible paths (test empirically):
   - Cast `akScannedRef` to a planet body / check a `PlanetKeyword` on its base form.
   - Fall back to `Game.GetPlayer().GetCurrentPlanet()` only if it's an orbit-context scan (player in ship, planet selected).
3. If the resolved planet is non-null **and player is NOT on that planet's surface**, call `CompleteSurveyForPlanet(planet)`.
4. Short-circuit if already 100%.

This needs a new helper `CompleteSurveyForPlanet(Planet akPlanet)` — a refactor of the existing `CompleteSurvey()` that takes the planet as a parameter instead of reading `GetCurrentPlanet()`. The existing console-invocable `CompleteSurvey()` becomes a thin wrapper: resolve current planet, call `CompleteSurveyForPlanet`.

**Open question for Phase 1:** does `OnPlayerScannedObject` fire for starmap planet scans? If no, Phase 1 produces zero observable behavior and we advance to Phase 2.

### Phase 2 — Engine hook fallback (only if Phase 1 is insufficient)

Per `re/ghidra/output/knowledge-api.txt:181`, `ID_52153` (per-planet knowledge writer, the path `SetTraitKnown` ultimately hits) calls `ID_97853` (survey check-and-dispatch). Remote planet scans reveal traits → flow through `ID_52153`.

Add a second call-site hook in `src/Main.cpp::Hook`, analogous to the existing `ID_52157 → ID_97853` hook, using `write_call<5>` and the same `FindCallSite` helper. Thunk dispatches to Papyrus `CompleteSurveyIfRemoteScanEnabled(planetForm)`, which resolves the planet from the write context.

**Risks:**
- `ID_52153` is also hit by ground-scan trait reveals — handler must distinguish remote vs. landed (player not on planet surface ⇒ remote).
- Extracting the planet form from the `ID_52153` args requires confirming its signature from the dump.

---

## 3. Papyrus API shape

`CompletePlanetSurveyQuest.psc` gains:

```papyrus
; Refactor: planet becomes a parameter. Existing per-biome + trait + aggregator
; passes unchanged.
Function CompleteSurveyForPlanet(Planet akPlanet) global

; Thin wrapper, preserves console command cgf "CompletePlanetSurveyQuest.CompleteSurvey"
Function CompleteSurvey() global

; Event handler entry for Phase 1.
Function HandleRemoteScan(ObjectReference akScannedRef) global
```

A small bootstrap quest-scoped script (probably attached to the CK-authored quest that already owns the mod) registers `Actor.OnPlayerScannedObject` on the player in its `OnQuestInit` and forwards to `HandleRemoteScan`.

---

## 4. Testing plan

1. Fresh save, toggle `CPSRemoteScanAutoComplete` ON, `CPSScanAutoComplete` OFF.
2. Travel to a system with an unsurveyed planet. Stand in the ship (not landed).
3. Open starmap → select planet → click Scan.
4. **Expected (Phase 1 works):** planet jumps to 100%, Survey Data slate available, `Debug.Notification` fires.
5. **If nothing happens:** confirm via `sfse.log` that `OnPlayerScannedObject` did not fire for the planet-body ref. Advance to Phase 2.
6. Regression: E-scan a flora on a landed planet with the existing `CPSScanAutoComplete` toggle ON. Must behave as before (no interference from the new remote path).
7. Edge: scan a planet the player IS currently on (shouldn't happen via orbit, but keep the guard).

---

## 5. Out of scope

- Hooking the starmap UI input handler directly (requires more Ghidra work — dumps don't cover UI/menu code yet).
- Any change to the existing landed-scan hook (`ID_52157 → ID_97853`).
- Perk-rank or distance checks — deliberately omitted; the toggle is the sole gate.

---

## 6. Risks / unknowns

- **Phase 1 viability** is unverified — if `OnPlayerScannedObject` doesn't fire for starmap scans, we bear the Phase 2 C++ cost.
- `ID_52153` signature for Phase 2 must be confirmed (planet form extraction from the call args).
- CK will reassign `CPSRemoteScanAutoComplete`'s FormID on edits; the `Game.GetFormFromFile(...)` call is fragile exactly like the existing `0x80C` lookup.
