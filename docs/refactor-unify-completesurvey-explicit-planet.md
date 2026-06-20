# Planned refactor — unify CompleteSurvey onto the explicit-planet green

**Status:** planned, deferred (do *after* the v1.1.0 pre-release hardening + ship).
**Origin:** the `IsInInterior()` / "exit your ship first" guard is a vestige of the
spawn-and-scan mechanism, which the new explicit-planet green path no longer needs.

## Goal

Make the single-planet `CompleteSurvey` use the **same explicit-planet green** the galaxy
path uses, instead of spawn-and-scan-at-the-player. This removes the location dependency,
collapses two code paths into one, and lets the mod complete the current planet **from
inside the ship, or from orbit**.

## Why this is possible now

`CompleteSurvey` today calls `SpawnAndScanAllPlanetSpecies` → `UpdatePlanetProgressForSpecies`
→ `ID_52157` → **`ID_52188`, which resolves the planet from the *ref's location*** — so the
player must be standing on the surface, else the spawned refs credit the wrong cell.

The galaxy path proved we don't need that: `GreenSpeciesEverywhere` / `GreenAndCompleteTypeForPlanet`
drive `ID_52161` (tree) + `ID_52158` (count) with the planet passed **explicitly**. We greened
never-visited planets from Jemison — location of the player/handle is irrelevant to the writes.

## Approach

In `CompleteSurvey` (CompletePlanetSurveyQuest.psc), replace the spawn-and-scan +
`ScanNearbyRefs` block with a single-planet invocation of the explicit-planet green:
- Keep: `MarkTraits` + `MarkResourcesForPlanet` (data + traits + slate).
- Replace: `SpawnAndScanAllPlanetSpecies(planetForm, playerRef)` →
  green the current planet's species via the explicit-planet path. Simplest reuse: a
  helper that, for each of the current planet's species, spawns one handle and calls
  `GreenSpeciesEverywhere`-style `ID_52161`+`ID_52158` against `currentPlanet` only
  (or factor a `GreenSpeciesOnPlanet(ref, planet, species)` native that both
  `CompleteSurvey` and `GreenSpeciesEverywhere` share).
- Relax the guard: `IsInInterior()` → drop to just `GetCurrentPlanet() == None`
  (you still need a planet under you; you no longer need to be *outside*).

If this holds, `SpawnAndScanAllPlanetSpecies` / `UpdatePlanetProgressForSpecies` /
`ScanNearbyRefs` may become removable from the single-planet path (the galaxy path
never used them), shrinking the surface further. Verify before deleting.

## Risk / what to verify in-game

The one real unknown: `ID_52158`'s biome-cluster propagation reads a **current-world
global** (`ID_937609 + 0x160`). When the player is in orbit or a ship interior, that
global may be null/stale. We established the biome propagation is **not load-bearing for
the green** (the tree + count are), so the green should still apply — but it is untested
when that global is null, and a null deref there would crash.

Test matrix after the change:
1. On the surface, outside — current planet greens (regression check). ✅ baseline.
2. Inside the landed ship — current planet greens, no crash.
3. In orbit over a planet — does `GetCurrentPlanet()` return it? If yes, greens, no crash.
4. In deep space (no planet) — `GetCurrentPlanet() == None` guard fires cleanly.

If (2)/(3) crash on the null global, guard `ID_52158` against a null current-world
context (or keep a lighter interior guard) rather than reverting the whole change.

## Sequencing

After: (a) the pre-release C++ hardening batch, (b) the v1.1.0 ship. This is a behavior
change to the most-used path, so it deserves its own commit + its own in-game test pass,
not a rider on the release.
