# Complete Planet Survey — completion commands

Run from the console: `` cgf "CompletePlanetSurveyQuest.<Command>" "<categories>" ``

Three commands, each taking a **category string** — a comma list of any of
`resources`, `traits`, `fauna`, `flora` (or the wildcard `all`):

| Command | Which worlds | Example |
|---|---|---|
| `CompletePlanet` | the one you're standing on | `cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits,fauna,flora"` |
| `CompleteBarrenPlanets` | every world with **no** life | `cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"` |
| `CompleteLifePlanets` | every world **with** life | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "resources,traits,fauna,flora"` |
| `CompleteAllPlanets` | the **whole galaxy** (barren + life) | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"` |

`CompleteAllPlanets` runs both galaxy sweeps as one cohesive operation: it shows the immersive intro (the `CPSRecallMessage` modal) once, suppresses the two sweeps' individual result popups, and shows a **single** combined result. The two underlying commands (`CompleteBarrenPlanets`/`CompleteLifePlanets`) gained an optional `abShowResult` arg (default `true`) and now return their world count — the console `cgf` calls are unaffected.

Pick exactly what gets marked: `"traits"` alone, `"resources,traits"`, `"all"`, etc.

## What each category does

- **resources** → the planet's resource + attribute survey data. **Pure** — never touches species.
- **traits** → two parts: (1) marks the planet's traits *known* (`938333`, the engine's off-planet
  path) — the durable DATA (survey %, TRAITS panel, galaxy map); (2) completes the in-world "Unknown
  Feature / 0-of-N SCANNED" objects by driving the game's OWN survey quest
  (`SQ_Parent.DiscoverMatchingPlanetTraits`), exactly like a real hand-scan — no engine pokes. The
  objects reload complete + named (no `0/N` corruption, hand scanner stays usable). On-planet the
  loaded objects finish immediately; on worlds completed remotely they auto-resolve on arrival
  (the game's own `CheckForScanTargetUpdate`, since the trait is now known).
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
- **`fauna` and `flora` are split** — `"fauna"` greens only creatures (NPC_), `"flora"` only plants
  (FLOR). Use `"fauna,flora"` (or `all`) for both.

## Natives backing this (read-only)

- `CategoryEnabled(csv, token)` — case-insensitive "does the list contain this token (or `all`)",
  because base Papyrus can't split a string.
- `EnumerateLifePlanets()` / `GetLifePlanetFormIdAt(i)` — the unique life-bearing worlds (the galaxy
  sweep skips them, so `CompleteLifePlanets` enumerates them to reach their traits/resources/green).
- `DiscoverPlanetEntry(planet)` — ref-free `ID_102650` discover; creates the knowledge entry so the
  ref-free resource/green writes land on a never-visited world.

## Status

Shipped in **v1.2.0**, in-game verified: galaxy-wide ref-free resources, persistent flora/fauna green
(outline + info), the flora/fauna split, in-world trait-object completion, and the full ~1,700-world
run. The RE-scaffolding console commands have been removed from the shipped scripts; `_GreenPlanet`
still drives the `TestDirectGreen` + `TestBuildArray` natives (the names are historical — they were the
RE probes that became the production green path; a rename is the only remaining internal cleanup).
