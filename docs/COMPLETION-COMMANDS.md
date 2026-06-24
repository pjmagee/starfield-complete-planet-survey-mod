# Complete Planet Survey — completion commands

Run from the console: `` cgf "CompletePlanetSurveyQuest.<Command>" "<categories>" ``

Three commands, each taking a **category string** — a comma list of any of
`resources`, `traits`, `fauna`, `flora` (or the wildcard `all`):

| Command | Which worlds | Example |
|---|---|---|
| `CompletePlanet` | the one you're standing on | `cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits,fauna,flora"` |
| `CompleteBarrenPlanets` | every world with **no** life | `cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"` |
| `CompleteLifePlanets` | every world **with** life | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "resources,traits,fauna,flora"` |

Pick exactly what gets marked: `"traits"` alone, `"resources,traits"`, `"all"`, etc.

## What each category does

- **resources** → the planet's resource + attribute survey data. **Pure** — never touches species.
- **traits** → marks the planet's traits *known* (`938333`, the engine's off-planet path). This is
  the durable DATA (survey %, TRAITS panel, galaxy map). It does **not** poke the in-world "Unknown
  Feature" scan-target objects — writing their transient `939118+0x28` byte jams the hand-scanner
  and doesn't move the 0/N count anyway, so those objects stay normally scannable in play.
- **fauna / flora** → greens the planet's creatures/plants ref-free: writes the `+0x21` scan flag
  and builds the ESM-derived `+0x08` marker catalogue (genetics / reproduction / temperament /
  abilities). No spawning.

## Behaviour notes (read these)

- **Everything is ref-free now.** `CompletePlanet` works on the world you're on; `CompleteBarrenPlanets`
  and `CompleteLifePlanets` work **from anywhere** (orbit, another system, on foot) — no spawning,
  no surface requirement.
- **`resources` is pure** — it marks resources + attribute bits only. On a life world, `"resources"`
  alone leaves the creatures untouched (blue), which is correct; add `fauna,flora` (or `all`) to green
  them.
- **`CompleteLifePlanets` discovers each world** before writing, so resources/green land even on
  never-visited worlds (it creates the knowledge entry via `ID_102650`). That discovery drops the
  world's "Survey Data" slate. `"traits"` alone skips the discover (and its slate) — the trait path
  is self-sufficient.
- **On `CompleteBarrenPlanets`, resources are always written** — the galaxy sweep writes them as it
  discovers each barren world, so they can't be separated from discovery. `"traits"` is the real
  toggle there.
- **`fauna` and `flora` are greened together** (the engine derives both from the same ESM pass).
  Asking for either does both. Splitting them is a possible follow-up (filter by FLOR vs NPC_).

## Natives backing this (read-only)

- `CategoryEnabled(csv, token)` — case-insensitive "does the list contain this token (or `all`)",
  because base Papyrus can't split a string.
- `EnumerateLifePlanets()` / `GetLifePlanetFormIdAt(i)` — the unique life-bearing worlds (the galaxy
  sweep skips them, so `CompleteLifePlanets` enumerates them to reach their traits/resources/green).
- `DiscoverPlanetEntry(planet)` — ref-free `ID_102650` discover; creates the knowledge entry so the
  ref-free resource/green writes land on a never-visited world.

## Status

Compiles (DLL + both scripts). **In-game verification pending** — in particular `CompleteLifePlanets`
now greens life worlds **ref-free and remotely** (the incorporated `GreenPlanetProper` method applied
galaxy-wide). That remote green is decompile-proven but should be confirmed live.

## Cleanup before release

`CompletePlanetSurveyQuest.psc` still carries RE-scaffolding commands (`TestDirectGreen`,
`ProbeScanKeys`, `TestRenderKeyGreen`, `ProbeRenderRead`, `DumpSpeciesSlots`, `TestBuildArray`,
`TestScanTraitTargets`, `TestMarkScanTargetKB`, `TestTraitOnPlanet`, `TestTraitRegistryWalk`).
Note `TestDirectGreen` + `TestBuildArray` are now also the production green path (`_GreenPlanet`),
so if these natives are renamed for release, update `_GreenPlanet` too.
