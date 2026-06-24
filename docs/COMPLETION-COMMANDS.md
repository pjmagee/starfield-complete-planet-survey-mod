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

- **resources** → the planet's resource + attribute survey data.
- **traits** → marks the planet's traits known (`938333`, the engine's off-planet path).
- **fauna / flora** → greens the planet's creatures/plants.

## Behaviour notes (read these)

- **The flora/fauna green needs you stood on a planet surface** (it spawns one hidden ref per
  species and redirects the engine's real scan to each host world). `resources` and `traits` are
  fully ref-free.
- **On `CompleteBarrenPlanets`, resources are always written** — the galaxy sweep writes them as it
  discovers each barren world, so they can't be separated from discovery. `"traits"` is the real
  toggle there.
- **On `CompleteLifePlanets`, asking for `resources` auto-includes the green.** The resource write
  also stamps the world's species flags; without greening they'd show "scanned but blue", so the
  green is forced to follow.
- **`fauna` and `flora` are currently greened together** (the engine greens both in one pass).
  Asking for either does both. Splitting them is a possible follow-up (filter by FLOR vs NPC_).

## Natives backing this (read-only)

- `CategoryEnabled(csv, token)` — case-insensitive "does the list contain this token (or `all`)",
  because base Papyrus can't split a string.
- `EnumerateLifePlanets()` / `GetLifePlanetFormIdAt(i)` — the unique life-bearing worlds (the galaxy
  sweep skips them, so `CompleteLifePlanets` enumerates them to reach their traits/resources).

## Status

Compiles (DLL + both scripts). **Not yet tested in-game** — in particular the `CompleteLifePlanets`
resources-then-green ordering on living worlds needs a verification pass.

## Cleanup before release

`CompletePlanetSurveyQuest.psc` still carries RE-scaffolding commands (`TestDirectGreen`,
`ProbeScanKeys`, `TestRenderKeyGreen`, `ProbeRenderRead`, `DumpSpeciesSlots`, `TestBuildArray`,
`TestScanTraitTargets`, `TestMarkScanTargetKB`, `TestTraitOnPlanet`, `TestTraitRegistryWalk`,
`GreenPlanetProper`). Strip these (and their native bindings) for a shipped build.
