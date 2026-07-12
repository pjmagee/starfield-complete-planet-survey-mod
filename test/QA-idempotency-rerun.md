# QA: XP + slate idempotency on re-run (issue #14)

Audits whether re-running a completion command against an already-completed save
re-fires XP/slate/statistics side effects. Issue #14 was filed 2026-06-21 against
an earlier `src/Main.cpp` (all its line numbers are stale — the file has grown
past 2000 lines since). This doc re-audits the **current** code and lays out the
in-game test the issue itself says can't be settled from code.

## TL;DR verdict

The mod-side guard the issue asks for **already exists** on every completion path
that fires the engine's survey-complete event (`ID_97853`) or the ref-free
discover/scan-complete call (`ID_102650`). Every one of them is gated on
`Engine::IsPlanetFullyMarked()` (or, for traits, on the engine's own
`Planet.IsTraitKnown()`), so a second run against an already-complete planet is a
mod-side no-op — it should not re-grant XP, drop a duplicate Survey Data slate, or
double-count the "Planets Fully Surveyed" character-Statistics counter. This
matches the fix that resolved the "observed 1780 → 3746" stat-inflation bug
described in `src/Main.cpp:538-541`, which predates this issue being filed.

No source change was made. This PR is docs-only. What remains is exactly what the
issue itself says: an in-game re-run to prove the guard holds under real engine
behavior (the guard's correctness rests on `IsPlanetFullyMarked()` correctly
mirroring the engine's own "is this planet done" state — see "What remains
unprovable from code" below).

## Guard inventory

All line numbers are `src/Main.cpp` on this branch, current as of this audit.

| # | Guard | Location | What it protects | Enforcement |
|---|---|---|---|---|
| 1 | `IsPlanetFullyMarked(planetId)` check before `ScanCompletePlanet` (`ID_102650`) | `DiscoverPlanetEntry`, L1198-1199 | Skips the ref-free discover/scan-complete call — which itself fires the survey-complete event — for a planet already fully marked. Called by every category path that needs a knowledge entry (`resources`, `fauna`, `flora`) via `_CompletePlanetForm`/`CompleteLifePlanets` in the `.psc`. | Mod-side, code-enforced |
| 2 | `IsPlanetFullyMarked(planetId)` check before `ScanCompletePlanet` in the barren galaxy sweep | `CompleteAllPlanetsSurveyData_Phase1`, L745-746 | Skips barren planets already complete from a prior run — they're never even added to `g_sweepPlanetForms`, so `FinalizeSweptPlanet` (which unconditionally re-fires the complete event, see guard #6) never runs on them on a second run. | Mod-side, code-enforced |
| 3 | `wasComplete = IsPlanetFullyMarked(planetId)` gates `NotifySurveyProgress` (`ID_97853`) | `MarkResourcesForPlanet`, L1221-1224 | The resources category's own explicit fire of the complete/progress event only happens on first completion. | Mod-side, code-enforced |
| 4 | `wasScanned` (per-species, captured before write) gates the Statistics tally, and `curCount == markers.size()` + non-zero scan flag skips re-writing an already-complete species slot | `MarkEsmSpeciesForPlanet` L595-611 (stats tally); `TestBuildArray` L1100-1115 (`alreadyComplete` skip) | Species (flora/fauna) green path: re-running never re-counts an already-scanned species toward Flora/Fauna Fully Scanned or Unique Creatures Scanned, and never re-touches an already-correct `+0x08` marker slot (which the comment notes is itself a clobber risk, not just a stat-inflation one). | Mod-side, code-enforced |
| 5 | `if (built > 0) NotifySurveyProgress(planetId)` | `TestBuildArray`, L1133-1134 | The species/green path's own explicit re-fire of the complete/progress event only happens when this call actually built/repaired at least one species slot — an all-already-green re-run fires nothing. | Mod-side, code-enforced |
| 6 | `Planet.IsTraitKnown(...)` check before calling the trait native at all | `MarkTraits`, `CompletePlanetSurveyQuest.psc` L437-443 | Traits are never re-marked (and the underlying `SetTraitKnownNative`/`ID_52155` trait-progress-event fire never re-runs) for a trait already known — checked per-trait via the engine's own query, not our own bookkeeping. | Mod-side, code-enforced (delegates the "already known" check to the engine) |
| 7 | `Bool wasComplete = p.GetSurveyPercent() >= 1.0` (captured before any writes) gates only the **display counters** ("N greened" / "N worlds"), not the writes themselves | `CompleteLifePlanets`, `.psc` L251, L275-286 | Cosmetic/reporting idempotency: a re-run's result popup reports `0` newly-completed worlds instead of re-claiming the whole galaxy. Does not by itself prevent event re-fires — guards #1 and #5 do that; this just keeps the *reported numbers* honest. | Mod-side, code-enforced (reporting only) |
| 8 | `Game.QueryStat("Planets Scanned") < Game.QueryStat("Planets Fully Surveyed")` before `Game.IncrementStat` | `_ReconcilePlanetsScanned`, `.psc` L459-464 | The "Planets Scanned" catch-up is a monotonic `>=` reconciliation — a no-op once already caught up, so re-running never inflates it. Called at the end of every command. | Mod-side, code-enforced |
| 9 | `FinalizeSweptPlanet` unconditionally calls `CompletePlanetSurveyState` → `NotifySurveyProgress` for every planet in `g_sweepPlanetForms` | `FinalizeSweptPlanet`, L1265-1285, called from `CompleteBarrenPlanets` `.psc` L174 | **Not** re-run-unsafe on its own: the list it iterates is populated *only* by guard #2, i.e. only planets that were incomplete at the start of *this* run. The double-fire this causes (once from `ScanCompletePlanet` at discover time, once here) is a **within-one-run** intentional second fire — see explanation below — not a cross-run duplication. | Mod-side, code-enforced *indirectly* via guard #2 gating the list this iterates |
| 10 | "Planets Fully Surveyed" itself | n/a — this stat is written entirely inside the engine's native event handler, never by mod code (`grep` confirms no `IncrementStat`/`IncrementMiscStat` call for that name in `src/` or `.psc`) | The mod cannot double-write this counter directly; it can only cause the engine to double-write it by re-firing the complete event without cause. Guards #1/#2/#3/#5 are what stand between a re-run and that outcome. | **Engine-assumed** for the write itself; mod-side guards protect the trigger |

### Why guard #9's double-fire-per-run isn't the bug the issue worried about

`FinalizeSweptPlanet`'s own comment (L1276-1279) explains it deliberately re-fires
`ID_97853` *after* `WritePlanetSurveyState` finishes, because the earlier
`ScanCompletePlanet` (`ID_102650`) call at discover time fires prematurely —
before resources/attribute bits are written — so the planet isn't actually at
100% yet at that point and (per `ID_97853`'s own documented "fires the complete
event **if** the survey is now 100%" behavior, L83-85) the reward doesn't land.
The second, later fire is what the comment says is required for the slate to
"drop" at all. This is consistent with the repo's own history: the previously
*observed* stat-inflation bug was "1780 → 3746" from re-running the **command**
a second time (a ~2x on an already-~1798-planet galaxy), not a ~2x already
present on the very first run — which is what you'd see if this within-run
double-fire were itself inflating the counter. In other words: the counter
apparently only increments on a genuine 0%→100% transition read at fire time,
and `ScanCompletePlanet`'s premature fire doesn't yet see 100%, so it doesn't
increment; only `FinalizeSweptPlanet`'s later fire (the true transition) does.
This inference is **not directly provable from the decompile in this repo** — it
is inferred from the documented historical inflation ratio — which is exactly
why the in-game protocol below re-checks it explicitly (Phase 3).

## What remains UNPROVABLE from code (needs the in-game test)

1. **Whether `IsPlanetFullyMarked()` truly mirrors the engine's own completion
   state.** It checks the attribute-bits bitmask (`& 0x7 == 0x7`) plus every
   authored species' `+0x21` scan flag. If the engine's internal "is this planet
   surveyed" check (whatever `ID_97853`/`ID_102651` actually read) uses a
   *different* bit or a percent computed some other way, our guard could report
   "already complete" when the engine would still fire — or vice versa. This is
   a structural assumption that can only be confirmed by watching real XP/slate/
   stat behavior across two runs.
2. **Whether XP is genuinely a side effect only of `ID_97853`/`ID_102650`**, with
   no separate engine-side XP grant elsewhere in the survey pipeline the mod
   doesn't hook. `grep` for XP-granting code in `src/Main.cpp` found only a code
   comment (no XP-granting call), so the mod does not add XP itself — but that
   doesn't prove the engine's own dedup (or lack of it) for XP specifically, only
   for the "Planets Fully Surveyed" misc stat we have prior inflation evidence
   for.
3. **Whether the "Survey Data" slate reward is engine-deduped** the way the
   `FinalizeSweptPlanet` comment assumes ("the engine awards a planet's survey
   reward once, so re-firing is idempotent"). This is stated as an assumption in
   the comment itself, carried over from the original (pre-guard) design; it has
   apparently held in practice (no slate-flood reports since the guards landed),
   but "no reports" isn't the same as "verified."
4. **Whether the Statistics counters read correctly in the character sheet /
   `GetPCMiscStat` after a genuine re-run**, since `IncrementMiscStat` (L489-513)
   walks a raw engine table by pointer arithmetic — a code-correct guard against
   *calling* it twice doesn't rule out an off-by-something in the table walk
   itself surfacing only at real scale (~1798 planets).

## In-game test protocol

Uses the **current** command surface (`docs/COMPLETION-COMMANDS.md` /
`Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc`) — the issue's original
steps referenced `CompleteAllPlanetsSurveyData`, which is a **native**, not a
console-invokable Papyrus global; the equivalent player-facing command today is
`CompleteAllPlanets "all"`.

### Setup

1. Use a fresh-ish save (or one where you know the current counters) — **back up
   the save first** (`Documents\My Games\Starfield\Saves`).
2. Open the console (`~`) and record the baseline:
   ```
   GetPCMiscStat "Planets Scanned"
   GetPCMiscStat "Planets Fully Surveyed"
   GetPCMiscStat "Flora Fully Scanned"
   GetPCMiscStat "Fauna Fully Scanned"
   GetPCMiscStat "Unique Creatures Scanned"
   ```
3. Note the player's **level** and **XP-to-next-level** from the character menu
   (Statistics / Skills screen), and the current inventory count of `<Planet>
   Survey Data` items (search inventory for "Survey Data").

### Run 1

4. Run the full galaxy completion:
   ```
   cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"
   ```
5. Wait for both sweep phases to finish (the barren sweep shows a modal intro
   message, then both sweeps complete before the combined result `MessageBox`
   appears — expect this to take real time at ~1798 planets; see the per-phase
   timing lines in the log, `…\My Games\Starfield\SFSE\Logs\CompletePlanetSurvey.log`).
6. Record: player level, XP, Survey Data slate count (let the slate-award queue
   drain 1-2 minutes before counting — per `project_galaxy_census` memory, slates
   drain over 1-2 min and a mid-drain count is not a drop), and the same five
   `GetPCMiscStat` queries as step 2.

### Run 2 (the actual idempotency test)

7. On the **same save**, run the identical command again:
   ```
   cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"
   ```
8. Expect the combined result `MessageBox` to report **0 barren + 0 living**
   worlds catalogued ("Galaxy already fully surveyed — nothing new to catalogue"),
   per the `.psc`'s `barren + life == 0` branch (`CompleteAllPlanets`,
   `.psc` L353-354) — if it instead reports non-zero counts, that alone is a
   signal something in the audit above is wrong (`IsPlanetFullyMarked` disagreeing
   with the engine, or a planet skipped by both sweeps in run 1).
9. Record the same measurements as step 6.

### Result table (fill in during the test)

| Measurement | Baseline (before run 1) | After run 1 | After run 2 |
|---|---|---|---|
| Player level | | | |
| XP (current / to next level) | | | |
| Survey Data slate count (inventory) | | | |
| `GetPCMiscStat "Planets Scanned"` | | | |
| `GetPCMiscStat "Planets Fully Surveyed"` | | | |
| `GetPCMiscStat "Flora Fully Scanned"` | | | |
| `GetPCMiscStat "Fauna Fully Scanned"` | | | |
| `GetPCMiscStat "Unique Creatures Scanned"` | | | |
| `CompleteAllPlanets` result popup (N barren / N life) | n/a | | |

### Pass/fail

- **PASS** — all counters and level/XP are IDENTICAL between "after run 1" and
  "after run 2" (i.e., run 2 changed nothing), and run 2's result popup reports
  0/0. No new Survey Data slates appear after run 2.
- **FAIL** — any counter increases, level/XP jumps again, or new Survey Data
  slates appear after run 2. If this happens, capture the
  `CompletePlanetSurvey.log` from both runs (per-phase `spdlog::info` timing +
  count lines) — the mismatch between the log's own "N barren completed" count
  and the actual counter deltas will localize which guard in the inventory above
  is not doing what its comment claims.

## Notes for whoever runs this

- This is real-time-costly: `CompleteAllPlanets "all"` sweeps ~1798 planets each
  run, and the Survey Data slate award queue drains over 1-2 minutes per the
  existing galaxy-census notes — budget accordingly for two full runs plus
  drain time.
- If FAIL, this issue reopens as a real bug and the fix belongs in one of the
  ten guard sites above — most likely guard #1's `IsPlanetFullyMarked` if the
  mismatch is with the engine's own definition of "complete" (see "what remains
  unprovable" #1).
