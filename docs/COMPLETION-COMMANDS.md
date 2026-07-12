# Complete Planet Survey: completion commands

Run from the console:

```
cgf "CompletePlanetSurveyQuest.<Command>" "<categories>"
```

`<categories>` is a comma-separated list of any of `resources`, `traits`, `fauna`, `flora`, or the wildcard `all`.

| Command | Which worlds | Example |
|---|---|---|
| `CompletePlanet` | the one you're standing on | `cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits,fauna,flora"` |
| `CompleteSystem` | every planet and moon in the current star system | `cgf "CompletePlanetSurveyQuest.CompleteSystem" "all"` |
| `CompleteBarrenPlanets` | every lifeless world (no flora or fauna) | `cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"` |
| `CompleteLifePlanets` | every world with life | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "resources,traits,fauna,flora"` |
| `CompleteAllPlanets` | the whole galaxy (barren + life) | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"` |

The surface is a scope x category grid: every command applies the same per-planet completion core to a different set of worlds (current planet, current system, barren galaxy, life galaxy, whole galaxy), each filtered by the same category strings.

`CompleteAllPlanets` runs the barren and life sweeps as one operation. It shows the immersive intro message once and presents a single combined result. The galaxy commands accept an internal flag to suppress per-sweep popups.

Pick exactly what to survey: `"traits"` alone, `"resources,traits"`, `"fauna,flora"`, `"all"`, etc.

## What each category does

- `resources`: surveys the planet's resources and writes its attribute bits plus resource scan flags. Pure: does not touch flora, fauna, or traits.
- `traits`: marks the planet's traits known (the engine's off-planet data path). Updates the survey percentage, the survey panel, and the star map. Pure data only.
- `fauna`: surveys the creatures. Completes only fauna scan state.
- `flora`: surveys the plants. Completes only flora scan state.
- `all`: all of the above.

Each category is strict. "resources" never touches species. "fauna" completes creatures only. "flora" completes plants only. "traits" only marks trait-known data.

## Behaviour notes

- Survey data (percentage, star map, survey panel, and `<Planet> Survey Data` rewards) is written instantly and durably on any selected world, including never-visited ones. It persists across reload.
- Visuals update on the next load only. Creatures and plants read as scanned and the in-world "0 of N SCANNED" trait pillars refresh when you re-enter a world or arrive at one completed remotely. There is no in-place or on-foot repaint.
- Galaxy commands are ref-free. `CompleteBarrenPlanets`, `CompleteLifePlanets`, and `CompleteAllPlanets` work from orbit, another system, or on foot. `CompletePlanet` requires you on the surface of the target world (exit your ship; not in an interior).
- `CompleteSystem` completes every body orbiting the current star — barren and life-bearing alike — for the chosen categories, ref-free (on foot or from orbit anywhere in the system). The system is resolved from the planet you are on or orbiting; in deep space (no current planet) the command refuses with a notification. DLC systems (for example Va'ruun'kai) work. Never-visited bodies are discovered as needed, which drops their Survey Data slates.
- Strict category purity is enforced. Each command checks the category string. An unknown or empty category is a clean no-op with a notification. `"resources"` on a life world leaves fauna and flora untouched. Pure `"fauna"` or `"flora"` does only that kind.
- `CompleteLifePlanets` discovers never-visited worlds as needed so that resources, fauna, and flora writes land. Discovery also drops the Survey Data slate for that world. `"traits"` alone is self-sufficient and does not trigger discovery.
- Barren worlds (no flora or fauna) are handled by `CompleteBarrenPlanets`. A lifeless rock earns its full survey from the environmental survey and still produces Survey Data even with no resources on it. Pure species categories (`fauna`/`flora`) on barren return early with nothing to do.
- `fauna` and `flora` are separate. `"fauna"` surveys creatures only. `"flora"` surveys plants only. Use `"fauna,flora"` or `"all"` for both.
- `CompletePlanet` applies the requested categories to the current world only. It cancels any pending auto-complete from the scan hook.

## Natives backing this

- `CategoryEnabled(csv, token)`: case-insensitive check whether the list contains the token (or `all`).
- `CategoriesValid(csv)`: returns true only for recognized tokens with at least one present. Used to reject typos cleanly.
- `EnumerateLifePlanets()` / `GetLifePlanetFormIdAt(i)`: list of unique life-bearing worlds for the life sweep.
- `EnumerateBarrenPlanets()` / `GetBarrenPlanetFormIdAt(i)`: standalone list of lifeless worlds (the same classification the resources sweep uses). This is what a traits-only barren run iterates, so it no longer depends on the resources sweep populating state.
- `EnumerateSystemPlanets(planet)` / `GetSystemPlanetFormIdAt(i)`: every body sharing the planet's parent star, from the plugin files' authored galaxy data (no engine offsets; validated offline).
- `DiscoverPlanetEntry(planet)`: ref-free discover (ID_102650) so resources and species state can be written on never-visited worlds.
- `MarkResourcesForPlanet`, `MarkTraitKnownForPlanet`, `TestDirectGreen` / `TestBuildArray` (the production species path), `CompleteAllPlanetsSurveyData`, and the sweep accessors.

## Status

Shipped in **v1.2.0**; `CompleteSystem` and the decoupled traits-only enumeration added by issue #16 (unreleased). Commands write survey data instantly and durably for the chosen categories and worlds. On-surface scanned outlines and trait pillars update on the next area load. The five commands plus category strings are the public surface. Internal RE probe names remain on the native surface for the species path; the old scaffolding commands were removed from the scripts.
