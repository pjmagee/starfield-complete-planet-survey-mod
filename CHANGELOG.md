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

[Unreleased]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.7...HEAD
[1.0.7]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.6...v1.0.7
[1.0.6]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.5...v1.0.6
[1.0.5]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.4...v1.0.5
[1.0.4]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/pjmagee/starfield-complete-planet-survey-mod/releases/tag/v1.0.0
