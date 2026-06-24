# Complete Planet Survey

**Finish planet surveys from the console — one world, or the entire galaxy — exactly the way your
character would actually explore.** Resources, flora, fauna and planetary traits, written as proper,
persistent survey data: the TRAITS panel fills in, the galaxy map updates, flora & fauna render green
in the scanner, and the "*\<Planet\> Survey Data*" slates drop into your inventory — even for worlds
you have never set foot on.

No fast-travel grind, no spawning tricks. You choose **which worlds** and **which categories** get
completed, so the mod fits how *you* like to play instead of just flipping everything to 100%.

---

## Requirements

- **Starfield 1.16.244** (built and tested against this build)
- **[SFSE](https://www.nexusmods.com/starfield/mods/106) 0.2.21** (must match the game build)
- **[Address Library for SFSE Plugins](https://www.nexusmods.com/starfield/mods/3256)**

> SFSE is locked to one exact game build. If your game updates, wait for a matching SFSE + a mod
> build for that version before updating. The mod refuses to load on a mismatched build rather than
> read wrong data.

## Install

Install the archive with **Mod Organizer 2** or **Vortex** ("Install from file"). It is a flat
`Data/` layout — no FOMOD wizard. Make sure `CompletePlanetSurvey.esm` is enabled.

---

## How to use — the console

Open the console with the tilde key (**`~`**) and call a command with `cgf`:

```
cgf "CompletePlanetSurveyQuest.<Command>" "<categories>"
```

`<categories>` is a comma-separated list of any of **`resources`**, **`traits`**, **`fauna`**,
**`flora`** — or the wildcard **`all`**.

### The four commands

| Command | Which worlds it completes |
|---|---|
| `CompletePlanet` | the single world you are **standing on** |
| `CompleteBarrenPlanets` | every **lifeless** world in the galaxy (no flora/fauna) |
| `CompleteLifePlanets` | every **life-bearing** world in the galaxy |
| `CompleteAllPlanets` | the **whole galaxy** — both of the above, in one command |

The galaxy commands are **ref-free**: they work from anywhere — on foot, in orbit, or sitting in
another star system. No travel required.

### The categories

| Category | What it completes |
|---|---|
| `resources` | the world's resource + attribute survey data (never touches species) |
| `traits` | the planet's traits — the TRAITS panel, galaxy map, and the in-world "Unknown Feature" scan objects |
| `flora` | greens the world's **plants** (persistent scanner outline + species info) |
| `fauna` | greens the world's **creatures** |
| `all` | everything above |

---

## Play your way — pick a role

The same commands tell very different stories depending on how you point them. Pick the playstyle that
matches your character and use its command — or mix and match as your story goes.

| Role | Fantasy | Command |
|---|---|---|
| 🧭 **Explorer** | "I survey each world myself, as I land on it." | `cgf "CompletePlanetSurveyQuest.CompletePlanet" "all"` |
| 🚀 **Frontier** | "I don't care — chart the whole galaxy, all of it." | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"` |
| 🏔️ **Adventurer** | "Catalogue the dead rocks for me; I'll walk every world that has *life* myself." | `cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "all"` |
| 🌿 **Botanist** | "Every plant in the galaxy, cleanly — nothing else." | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "flora"` |
| ⛏️ **Miner** | "I only care about what I can dig up — resources everywhere." | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "resources"` |
| 🐾 **Beast Master** | "Every creature in the galaxy catalogued." | `cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "fauna"` |
| 🔭 **Astrophysicist** | "I read worlds from afar — every planetary trait, no boots needed." | `cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "traits"` |

Notes on the roles:

- **Explorer** is the purest playstyle: it completes only the world under your feet, so your survey
  log fills in at your own pace as you actually explore.
- **Adventurer** leans on the fact that lifeless worlds are quick, repetitive busywork while
  life-bearing worlds are the interesting hand-exploration — so it does the former and leaves the
  latter to you.
- **Botanist** and **Beast Master** use `CompleteLifePlanets` on purpose: plants and creatures only
  exist on life-bearing worlds, so this reaches every one of them without touching the lifeless rocks.
- **Astrophysicist** mirrors the in-game **Astrophysics** skill, which lets you discover a world's
  traits from orbit — so completing `traits` galaxy-wide is exactly in character.

---

## Good to know

- **The survey slates arrive over a minute or two.** Completing many worlds at once queues a lot of
  "*\<Planet\> Survey Data*" rewards; they drain into your inventory gradually, so the count climbs
  for a while after the command finishes. That is the game's award queue, not a hang.
- **Stars don't count.** The galaxy has ~1,800 bodies but only ~1,700 are surveyable planets and
  moons — system stars don't produce survey data, so a full `all` run lands around ~1,700 slates.
- **Traits on worlds you complete remotely finish themselves when you arrive.** Marking a world's
  traits known galaxy-wide means its in-world "Unknown Feature" scan objects auto-resolve the moment
  you land there — no manual scan, no leftover "0 of N" objects.
- **Green outline shy to appear?** If the scanner outline hasn't repainted, look away and back through
  the scanner to force it. The state is saved either way.
- **`resources` is pure.** On a life world, `"resources"` alone leaves the creatures uncatalogued
  (correct for a miner) — add `fauna,flora` or use `all` when you want the life, too.

## Optional setting

Under **Settings → Gameplay** the mod adds an **auto-complete on scan** toggle: with it on, scanning
any single flora/fauna on a world completes that world's whole survey automatically. Leave it off to
drive everything by command.
