# Changelog

All notable changes to **Complete Planet Survey** are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
versioning follows [Semantic Versioning 2.0.0](https://semver.org/) — the
de-facto pair across the OSS ecosystem. Versions are git tags (`vX.Y.Z`).
Entries describe what *actually* changed for players or the runtime —
recompiles, refactors, and tooling/docs churn are called out as such rather
than dressed up as features.

## Versioning policy

Semantic Versioning, interpreted for a save-game mod whose only hard
dependency is SFSE / the game build:

- **MAJOR** — a change a player can't just drop in: breaks existing saves,
  changes or renumbers the ESM / Settings toggle (GPOG/GPOF FormID), or
  otherwise requires manual action beyond replacing files.
- **MINOR** — new backward-compatible player-facing capability.
- **PATCH** — bug fixes, crash guards, and **compatibility recompiles**
  (rebuild against a new SFSE / game build with behavior unchanged).

**SFSE dependency rule.** SFSE is hard-locked to one exact game build, and a
game patch forces a new SFSE and a fresh DLL recompile. A compatibility
recompile is a **PATCH** (the mod's own contract didn't change) — *not* a
MAJOR bump — but it is a hard runtime gate that SemVer cannot express: a DLL
built for one game build will refuse to load on another. Therefore every
release **must** record the exact game + SFSE build it was built and tested
against, and a release that changes that target must say so prominently so
players know to match their game/SFSE to it.

Tags `v1.0.0`–`v1.0.7` predate this written policy; their increments are
historical and are not renumbered (they are published on Nexus). Address
Library decouples the native IDs from runtime offsets, so a release often
keeps working across minor game patches even when the tested build below is
older than the player's game — but the recorded build is the only one
verified.

## [Unreleased]

*No unreleased changes.*

## [1.5.0] — 2026-07-12

Same target as v1.4.0 — **Starfield 1.16.244.0 / SFSE 0.2.21** (CommonLibSF `998b6cd`, unchanged); a
drop-in upgrade, no game or SFSE change required. **DLL-only update** — the ESM, its FormIDs and the
Papyrus scripts are byte-for-byte unchanged, so existing saves and toggle settings are unaffected.

Surveys now work on worlds added by **DLC and Creations** — Shattered Space's Va'ruun'kai (Dazra)
first among them. Previously the mod only read `Starfield.esm`, so planets authored in any other
master were invisible to it.

### Added

- **DLC / Creations planet support.** The mod's offline planet reader now parses **every plugin in
  your load order** (base game, Shattered Space, other Bethesda content files, and third-party
  masters alike), not just `Starfield.esm`. Each file's FormIDs are remapped to their runtime values
  across all three plugin classes (full, medium, and small/light masters), later plugins override
  earlier ones — exactly the game's own rules — and cross-file references (a DLC creature built from
  base-game parts, base-game flora placed on a DLC world) resolve correctly. The load order is taken
  from the running game itself, so it is always in sync with what's actually loaded; if that read
  ever fails, the reader falls back to the previous `Starfield.esm`-only behaviour rather than
  guessing.

### Fixed

- **Surveys on Shattered Space (and any other DLC) worlds now complete** — the reported
  "not working on Dazra" bug. Two symptoms, one cause: on-surface `CompletePlanet` (and the Hand
  Scanner toggle) left DLC flora/fauna blue because their species were unknown to the reader, and
  the Orbital Scanner / galaxy-map completion couldn't finish DLC bodies either. Both paths verified
  in-game on Va'ruun'kai.
- **The whole-galaxy sweep no longer mis-treats DLC life worlds as barren.** Worlds like Va'ruun'kai
  weren't in the reader's species map, so `CompleteBarrenPlanets` / `CompleteAllPlanets` "completed"
  them ref-free — claiming a full survey while their flora/fauna were never scanned. With the
  species map now covering all masters, living DLC worlds are correctly held for the on-planet /
  orbital paths like base-game life worlds.

### Internal (no player-facing change)

- Offline validation extended to multi-master mode (`CPS_ESM_PATHS` load-order override for the
  test harness): the 17/17 Jemison + 7/7 actor-marker ground truth passes in both single-file and
  full-load-order modes; Va'ruun'kai derives green-marker sets for all 20 of its species, including
  planet-trait-gated flora genetics on DLC worlds.
- Repo tooling/docs only: GitHub Actions bumped to Node 24-native majors; CLAUDE.md reader notes.

## [1.4.0] — 2026-07-11

Same target as v1.3.0 — **Starfield 1.16.244.0 / SFSE 0.2.21** (CommonLibSF `998b6cd`, unchanged); a
drop-in upgrade, no game or SFSE change required. The ESM gains a **new** Settings toggle; existing
toggle and record FormIDs are unchanged, so existing saves are unaffected.

Adds an opt-in way to complete a world's survey straight from the **galaxy map**: scan a planet or moon
on the star map and its entire survey completes for that body, with the info panel updating in place.

### Added

- **Orbital Scanner: galaxy-map scan → complete that world** (opt-in, default **off**). With the new
  **Orbital Scanner** toggle on, scanning a planet or moon on the star map completes its **entire**
  survey for that specific body — the same outcome as running `CompletePlanet "all"` while standing on
  it — resources, traits, and flora/fauna, all ref-free (no landing). Off by default, so the vanilla
  scan is unchanged unless you enable it.
- **New Settings → Gameplay toggle: "Orbital Scanner"** (GPOF `0x0100080D`). It is *added* to the
  ESM's existing option group; the prior hand-scanner toggle (`0x0100080C`) and all other record
  FormIDs are unchanged, so this remains a drop-in update.
- **The star-map info panel refreshes to 100% in place** after an Orbital Scanner scan completes — the
  survey %, resources and traits update without needing to deselect and reselect the planet.

### Changed

- **Both Settings toggles renamed for consistency** (display names/descriptions only — FormIDs and your
  saved on/off values are unchanged). "Auto-Complete Survey on Scan" → **"Hand Scanner"**; the new
  star-map toggle is **"Orbital Scanner"**. They now read as a matching pair: Hand Scanner (on-surface
  scan) and Orbital Scanner (star-map scan).

### Internal (no player-facing change)

- Extracted the per-planet completion core (`_CompletePlanetForm`) shared by the on-surface
  `CompletePlanet` command and the new galaxy-map path.
- Native detection of the star-map scan via a crash-safe call-site hook on the engine's survey-notify
  (`ID_52173` → `ID_97853`, filtered to the *ScanLevelChanged* reason — the space scan), and an
  in-place panel repaint by re-issuing the engine's own panel-populate call (`ID_93988(controller,
  planetId)`, captured from `ID_94011`) once completion finishes, validated against the live UI menu
  list so a closed menu is never touched. Two new natives (`GetGalaxyScanPlanetFormId`,
  `QueueStarMapRefresh`); trampoline size raised 64 → 128 for the added hooks. Any new native is
  registered in the per-session re-bind path (survives a Main-Menu → load).
- Reverse-engineering dumps for the star-map scan/refresh path added under `re/ghidra/output/`.

## [1.3.0] — 2026-07-01

Built/tested against **Starfield 1.16.244.0 / SFSE 0.2.21** (same target as v1.2.0 — a drop-in
upgrade; no game or SFSE change required).

Completion now reflects itself on your **character's exploration Statistics**, and several
completion-flow bugs are fixed: console commands surviving a Main-Menu reload, life worlds finishing
in a single command, and re-runs being true no-ops instead of inflating counters.

### Added

- **Character exploration Statistics.** Completing worlds now writes the counters on the character
  sheet (Data > Statistics): **Flora Fully Scanned**, **Fauna Fully Scanned**, **Unique Creatures
  Scanned**, and **Planets Scanned**. Flora/fauna are counted once per species that newly reaches a
  full scan (idempotent — re-running a completion does not double-count). **Planets Scanned** is kept
  at or above **Planets Fully Surveyed** (surveying a world implies scanning it, and a surveyed world
  can no longer be scanned), reconciled through the game's own stat API. A new
  [`docs/MOD-DESCRIPTION.md`](docs/MOD-DESCRIPTION.md) section shows how to view or adjust these
  counters yourself with the vanilla `GetPCMiscStat` / `ModPCMiscStat` console commands.

### Fixed

- **Console commands stopped working after quitting to the Main Menu and loading a save.** The mod's
  formless scripts had their VM script types dropped on session teardown, which unbound the native
  functions; the plugin now re-binds them once gameplay resumes, so the `cgf` commands keep working
  across reloads.
- **`CompleteLifePlanets` / `CompleteAllPlanets` now finish in a single command.** The engine creates
  a never-visited world's knowledge entry asynchronously, so a handful of worlds were left just under
  100% on the first pass (you had to run the command twice). A finalize pass now waits for those
  deferred creates and completes the stragglers in the same run.
- **Re-running a completion no longer inflates *Planets Fully Surveyed*.** The game's survey-complete
  event is fired only when a world *newly* reaches 100%, so repeat runs are genuine no-ops instead of
  re-counting worlds that were already done.
- **Honest completion results.** `CompleteLifePlanets` / `CompleteAllPlanets` now report the worlds
  *newly* completed (0 on a re-run, matching the barren sweep), and `CompleteAllPlanets` says the
  galaxy is *already fully surveyed* when nothing was new — instead of always re-claiming all worlds.

### Internal (no player-facing change)

- Removed ~490 lines of unreachable dead code (orphaned probe / reveal / render chains), the
  `kBiomeScanCategory` constant, and the now-unused `EsmReader` multi-biome-count machinery
  (`SpeciesBiomeCount` / `GetSpeciesBiomeCount`); trimmed stale diary comments in `TestBuildArray`.
- Repo tooling/docs only: `README.md` rewritten as an evergreen project README;
  `docs/COMPLETION-COMMANDS.md` updated to shipped v1.2.0 behaviour; `docs/MISC-STATS.md` and the
  misc-stat reverse-engineering notes added; multi-agent grok review reports + dead-code audit added.

## [1.2.0] — 2026-06-25

Built/tested against **Starfield 1.16.244.0 / SFSE 0.2.21** (same target as v1.1.0 — a drop-in
upgrade; no game or SFSE change required).

Turns the single galaxy command into a **parameterized completion suite** you point at exactly the
worlds and categories you want, makes every category write strictly its own survey data (no
cross-category bleed), and settles the green model on the engine's own state: survey data is written
instantly and durably, and the on-surface scanned outline follows on the next fresh load of the world.

### Added

- **Parameterized completion commands.** Each takes a category string — any comma-separated mix of
  `resources`, `traits`, `fauna`, `flora`, or the wildcard `all`:
  - `CompletePlanet "<cats>"` — the world you are standing on.
  - `CompleteBarrenPlanets "<cats>"` — every lifeless world (ref-free galaxy sweep).
  - `CompleteLifePlanets "<cats>"` — every life-bearing world (ref-free; works from orbit, another
    system, or on foot).
  - `CompleteAllPlanets "<cats>"` — the whole galaxy (both sweeps) in one cohesive command: one
    immersive intro, one combined result.

  See [`docs/COMPLETION-COMMANDS.md`](docs/COMPLETION-COMMANDS.md) and the playstyle guide in
  [`docs/MOD-DESCRIPTION.md`](docs/MOD-DESCRIPTION.md).
- **Category validation.** An unrecognised or empty category string is a clean no-op (with a
  notification) instead of half-running a command.

### Changed

- **Strict category purity.** Each category writes only its own survey data: `"resources"` never
  touches species, `"fauna"`/`"flora"` complete only that kind, `"traits"` writes only trait-known
  data. Previously a completion could bleed across categories (e.g. `"fauna"` also catching nearby
  plants). `CompleteAllPlanets` now respects the category too: `"fauna"` completes the living worlds
  without running the barren resource sweep.
- **Survey data is instant and durable; the scanner outline follows on reload.** The survey %, star
  map, survey panel and `<Planet> Survey Data` rewards are written the moment a command finishes, on
  any world (including never-visited ones), and persist across save/reload. The scanned outline on
  already-loaded creatures/plants refreshes when those objects next load fresh: already done when you
  arrive at a world completed remotely, or after a re-entry on the world you are standing on. The
  ESM-derived species detail markers (genetics / reproduction / temperament / abilities) are written
  alongside the scan flag.
- **Flora and fauna are split.** `"fauna"` completes only creatures and `"flora"` only plants.

### Removed

- **In-place outline ref-scan and the on-foot trait scan-target driver.** The green outline and the
  in-world "0 of N SCANNED" feature pillars now resolve through the game's own state on reload/arrival
  (the outline from the saved scan flag; the pillars via `CheckForScanTargetUpdate`, since the trait is
  known), instead of driving the engine on the live cell. This removes the live-instance contention
  that could leave a just-completed creature blue until something else recomputed.

### Fixed

- **`"traits"` no longer writes resources** on the barren / `all` sweep: with only traits requested,
  the sweep enumerates the worlds without writing any resource or attribute data.

### Internal (no player-facing change)

- **Build-mode log gating.** A release / `releasedbg` build ships at log level INFO — the per-planet
  and per-species green & resource lines log at DEBUG, so a whole-galaxy completion no longer writes
  ~20k lines to `CompletePlanetSurvey.log`; only per-stage summaries, timings and faults remain. A
  `xmake f -m debug` build keeps the full per-body trace. CI builds the release symbol build (`releasedbg`).
- Removed the reverse-engineering scaffolding console commands (`TestDirectGreen`, `ProbeScanKeys`,
  `TestRenderKeyGreen`, `ProbeRenderRead`, `DumpSpeciesSlots`, `TestBuildArray`, `TestScanTraitTargets`,
  `TestMarkScanTargetKB`, `TestTraitOnPlanet`, `TestTraitRegistryWalk`) and dead helpers from the
  shipped scripts; trimmed the native decl file to the production surface.
- Crash-safety hardening at the native boundary and consolidation of the green / trait / survey-%
  reverse-engineering trail under `re/`.

## [1.1.0] — 2026-06-20

Built/tested against **Starfield 1.16.244.0 / SFSE 0.2.21** (same target as v1.0.8 —
a drop-in upgrade; no game or SFSE change required).

The first release that completes the **entire galaxy at once**, and the first to set the
**persistent green flora/fauna scanner outline** for planets — including ones you have
never visited. The original single-scan behaviour is unchanged; this adds a new console
command on top of it.

### Added

- **`cgf "CompletePlanetSurveyQuest.CompleteAllPlanetsSurveyData"`** — completes the
  survey for every planet and moon in the galaxy in one pass, with no fast-travel and no
  per-planet visiting:
  - **Survey data** for all ~1798 bodies — attribute bits, every species/resource scan
    flag, and the `<Planet> Survey Data` slate — written ref-free.
  - **Traits** marked on every planet.
  - **Persistent green flora/fauna.** The scanner "scanned" outline is saved per species
    per planet, so when you later land on a planet completed this way — even one you have
    never set foot on — its plants and creatures render green, including freshly-spawned
    instances, and across save/reload.
- Flora/fauna species are read directly from `Starfield.esm`'s PNDT *Per Biome Data*
  (zlib-inflated at runtime), so a planet can be completed without the game first having
  to materialise its biomes on landing.

### Internal (no change to existing features)

- New `EsmReader` (PNDT/PPBD parse + zlib inflate); `xmake.lua` adds the `zlib` package.
- The galaxy green is written by driving two engine routines with an **explicit target
  planet**: `ID_52161` (per-type scanned-species tree) + `ID_52158` (per-species count
  completion). The tree write alone stays blue; the pair greens. The full reverse-
  engineering trail — including every approach that failed and why — is in
  [`docs/green-outline-attempts.md`](docs/green-outline-attempts.md).
- Corrected the green-outline model in the RE/skill docs (it is persistent per-type
  state, not a per-loaded-instance paint); added decompile dumps + RE scripts under `re/`.

## [1.0.8] — 2026-06-17

Built/tested against **Starfield 1.16.244.0 / SFSE 0.2.21**.

> ⚠ **Target changed from v1.0.7.** This DLL is built for Starfield 1.16.244.0 +
> SFSE 0.2.21. The v1.0.7 build (1.16.242.0) **crashes at load** on 1.16.244 with
> `REL/IDDB.cpp: Failed to find offset for Address Library ID! Invalid ID: 139352`.
> Match your game and SFSE to this release; stay on v1.0.7 only if you remain on
> 1.16.242.0.

A compatibility recompile for Starfield 1.16.244 — no new survey capability over
v1.0.7.

### Changed

- Rebuilt against CommonLibSF `998b6cd` (was `186654e`). 1.16.244 relocated
  `BSStringPool::GetEntry` / `GetEntryW`, whose Address Library IDs changed
  (`139352 → 1186742`, `139354 → 1186743`); the old IDs are absent from Address
  Library v22, so the prior build aborted at load on the first `BSFixedString`
  construction. Upstream `RUNTIME_LATEST` is now 1.16.244.
- Native `REL::ID`s and the Ghidra-derived struct offsets are **unchanged** —
  re-verified correct on 1.16.244 across 5 planets and all four trigger paths
  (console command, resource / flora / fauna scan).

### Added

- Per-stage survey diagnostics in `CompletePlanetSurvey.log` (aggregator span
  sizes, DB `seen`/`marked`, form-type breakdown, spawn `placeFail`/`noForm`) so
  the next patch's triage is a single log read rather than an offset hunt.

### Internal (no behavior change)

- `build.bat` now runs the real xmake build (it referenced a non-existent
  CMake/Ninja setup); `deploy.bat` refuses to run while Starfield is open (a
  locked partial copy silently left a stale DLL loaded); removed the stale
  FOMOD-era `package.bat` (superseded by `package.py` since v1.0.5).
- Skill: added the game-patch update playbook + next-patch test checklist
  (`.claude/skills/starfield-modding/references/game-patch-update.md`).

## [1.0.7] — 2026-05-16

Built/tested against **Starfield 1.16.242.0 / SFSE 0.2.20**.

> ⚠ **Target changed from v1.0.6.** This DLL is built for Starfield
> 1.16.242.0 + SFSE 0.2.20 and will not load on the older 1.16.236.0 /
> SFSE 0.2.19 build. Match your game and SFSE to this release; stay on
> v1.0.6 if you are still on 1.16.236.0.

A recompile for the new game/SFSE plus a code cleanup. Not a feature release —
no new survey capability over 1.0.6.

### Changed

- Rebuilt against the updated `CommonLibSF` for Starfield 1.16.242.0 / SFSE
  0.2.20. Native REL::IDs are unchanged; this is a compatibility recompile.
- Survey now posts a notification when it completes below 100%
  (`"Survey: completed X% (some items may need manual scan)"`) instead of
  silently logging.
- A missing/renamed ESM is no longer a silent no-op: if GPOF `0x80C` can't be
  resolved, the Papyrus glue logs a clear diagnostic and returns, rather than
  behaving identically to "toggle off".

### Fixed

- Crash guard in `ScanAllRefsInCell`: cell ref arrays larger than 8192 are
  rejected. Procgen cells could hand back stale/oversized arrays that crashed
  the sweep.
- Defensive mutex around the species cache (`EnumeratePlanetSpecies` /
  `GetPlanetSpeciesAt`) against concurrent Papyrus access.

### Internal (no behavior change)

- Extracted `ForEachAggregatedFormId<Fn>` — deduped the aggregator walk that
  was copy-pasted across `MarkEverythingForPlanet` and
  `EnumeratePlanetSpecies`.
- Extracted `DispatchPapyrusStatic()` — deduped VM-dispatch boilerplate shared
  by the scan hook and the per-frame poller.
- Magic numbers promoted to named `constexpr` constants (DB sentinel, invalid
  form ID, scan deltas, cell ref-array offsets, x86 CALL opcode/length).
- Repo tooling/docs only: LikeC4 architecture diagrams (`docs/*.c4`), Claude
  skills/MCP config, GitHub Pages workflow, README rewrite.

## [1.0.6] — 2026-04-17

Built/tested against **Starfield 1.16.236.0 / SFSE 0.2.19** (unchanged since
v1.0.0; CommonLibSF `c3c7e12`). This is the last release on that target —
v1.0.7 moved to 1.16.242.0 / SFSE 0.2.20.

### Changed

- Spawn-and-scan per species so **BIOME COMPLETE** fires for every biome on
  the planet, not just the one the player is standing in.
- `CompleteSurvey` dispatch deferred out of the scan-hook call chain (via the
  per-frame poller) so `PlaceAtMe` no longer races the active scanner UI.

## [1.0.5] — 2026-04-16

### Changed

- Dropped the FOMOD installer; ship a flat `Data/`-prefixed archive for
  Mod Organizer 2 / Vortex "Install From File" compatibility.

## [1.0.4] — 2026-04-16

### Changed

- CI: set a pretty Nexus `display_name` while keeping `.zip` in the served
  filename.

## [1.0.3] — 2026-04-16

### Changed

- CI: reverted the `display_name` override to match the upstream Nexus-upload
  action's documented example.

## [1.0.2] — 2026-04-16

### Changed

- CI: pass `display_name` to the Nexus upload without the `.zip` suffix.

## [1.0.1] — 2026-04-14

### Changed

- Simplified the scan hot path; unified the CI and local package zip layout.

### Fixed

- Corrected a misleading biome-only docstring in the quest script.

### Added

- CI: auto-upload the release zip to Nexus on `v*` tags.

## [1.0.0] — 2026-04-14

Initial public release. Built/tested against **Starfield 1.16.236.0 /
SFSE 0.2.19** (CommonLibSF `c3c7e12`). This target held unchanged through
v1.0.6.

### Added

- SFSE plugin that completes a planet's survey — across every biome — the
  moment any single flora/fauna is scanned.
- Settings → Gameplay toggle via a CK-authored ESM (GPOG/GPOF).
- Direct knowledge-DB writes for full survey completion; Ghidra-derived
  struct offsets promoted to named constants.
- Built-in Instant-Scan-style per-species GMST patch.
- `cgf "CompletePlanetSurveyQuest.CompleteSurvey"` console entry point.
- CI builds and ships the DLL + ESM as the release artifact.

[Unreleased]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.5.0...HEAD
[1.5.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.4.0...v1.5.0
[1.4.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.8...v1.1.0
[1.0.8]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.7...v1.0.8
[1.0.7]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.6...v1.0.7
[1.0.6]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.5...v1.0.6
[1.0.5]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.4...v1.0.5
[1.0.4]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/releases/tag/v1.0.0
