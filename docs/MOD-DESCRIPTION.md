# Complete Planet Survey

Finish planet surveys from the console. One world, or the whole galaxy.

Resources, traits, flora and fauna get marked surveyed. The survey hits 100%, the star map updates, and
the "*\<Planet\> Survey Data*" items land in your inventory. It works on worlds you've never even been to.

You pick which worlds and which categories to complete, so it fits how you play.

## Requirements

- Starfield 1.16.244
- [SFSE](https://www.nexusmods.com/starfield/mods/106) 0.2.21
- [Address Library for SFSE Plugins](https://www.nexusmods.com/starfield/mods/3256)

SFSE has to match your game build. If your game updates, wait for a matching SFSE and a new mod build
before you update. The mod won't load on the wrong build.

## Install

Install with Mod Organizer 2 or Vortex ("Install from file"). Make sure `CompletePlanetSurvey.esm` is
ticked.

## How to use

Open the console with the tilde key (`~`) and run:

```
cgf "CompletePlanetSurveyQuest.<Command>" "<categories>"
```

`<categories>` is any mix of `resources`, `traits`, `fauna`, `flora`, separated by commas. Or just `all`.

### The four commands

| Command | What it completes |
|---|---|
| `CompletePlanet` | the world you're standing on |
| `CompleteBarrenPlanets` | every lifeless world (no plants or creatures) |
| `CompleteLifePlanets` | every world with life |
| `CompleteAllPlanets` | the whole galaxy, both at once |

The galaxy commands run from anywhere. Orbit, another system, on foot, doesn't matter. `CompletePlanet`
is the one that needs you actually on a world.

### The categories

| Category | What it does |
|---|---|
| `resources` | surveys the world's resources |
| `traits` | surveys the planet's traits |
| `flora` | surveys the plants |
| `fauna` | surveys the creatures |
| `all` | all of the above |

Each one sticks to its own job. `"fauna"` won't touch plants. `"resources"` won't touch life. Combine
them if you want. `"resources,traits"` does both. `"fauna,flora"` does the life.

### What to expect

The survey finishes the moment the command runs. The survey panel and star map update straight away, the
Survey Data drops into your inventory, and it stays done after you reload.

The creatures and plants right next to you read as scanned once the area reloads. Land on a world you
finished from orbit and they're already scanned. On the world you're standing on, fly up to orbit and
back and they catch up. Either way the survey is already complete.

## Play your way

Same commands, different stories. Pick whatever fits your character.

| Role | The idea | Command |
|---|---|---|
| 🧭 Explorer | "I survey each world myself as I land." | `cgf "CompletePlanetSurveyQuest.CompletePlanet" "all"` |
| 🚀 Frontier | "Chart the whole galaxy. All of it." | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"` |
| 🏔️ Adventurer | "Do the dead rocks for me. I'll walk the living worlds." | `cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "all"` |
| 🌿 Botanist | "Every plant in the galaxy. Nothing else." | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "flora"` |
| ⛏️ Miner | "Just what I can dig up. Resources everywhere." | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "resources"` |
| 🐾 Beast Master | "Every creature catalogued." | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "fauna"` |
| 🔭 Astrophysicist | "Every planetary trait, read from orbit." | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "traits"` |

Botanist and Beast Master use CompleteLifePlanets because plants and creatures only show up on living
worlds, so it hits every one without bothering the dead rocks. Astrophysicist lines up with the
Astrophysics skill, which reads traits from orbit.

## Good to know

Survey Data takes a minute or two to all show up. Finish a lot of worlds at once and the rewards trickle
in, so the count keeps climbing after the command's done.

Stars don't have a survey, so a full `all` run lands around 1,700 items.

`"resources"` on its own leaves a living world's creatures untouched. That's the point, for a miner. Add
`fauna,flora` or use `all` when you want the life too.

A dead world counts as done once its environment is surveyed, so it still gives you Survey Data even with
no resources on it.

## Watch the XP

Surveying gives XP, and finishing the whole galaxy in one go gives a pile of it. On a fresh character a
single `all` run can shoot you up to around level 80, which makes the early game a walkover. Worth knowing
before you pull the trigger.

If you'd rather skip the levels:

- Do it on a character that's already established, or run a handful of worlds at a time instead of
  everything at once.
- Pair this with a "no XP" or lower-XP-curve mod if you want the survey data without the levels at all.
- Already racked them up? You can set your level in the console with `player.setlevel <number>` (for
  example `player.setlevel 5`). Heads up: any console command turns achievements off for that save unless
  you're running an Achievement Enabler mod.

## Optional setting

There's an "auto-complete on scan" toggle under Settings > Gameplay. Turn it on and scanning any single
plant or creature finishes that whole world for you. Leave it off to do everything by command.
