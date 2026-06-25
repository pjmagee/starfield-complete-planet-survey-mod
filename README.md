# Complete Planet Survey

A Starfield mod that completes planet surveys from the console: one world, or the whole galaxy.

**Nexus:** <https://www.nexusmods.com/starfield/mods/16493>

Resources, traits, flora, and fauna get marked surveyed. The survey reads 100%, the star map updates,
and the `<Planet> Survey Data` rewards drop into your inventory, even for worlds you have never visited.

It is an SFSE plugin (DLL) plus a small ESM (one Settings toggle) and a Papyrus script. Native engine
IDs are resolved through Address Library, so the mod keeps working across game patches as long as those
IDs stay mapped. The exact game and SFSE build each release was tested against is recorded in
[CHANGELOG.md](CHANGELOG.md).

## What it does

Four console commands, each pointed at the categories you choose. Categories are any comma-separated mix
of `resources`, `traits`, `fauna`, `flora`, or the wildcard `all`:

```
cgf "CompletePlanetSurveyQuest.<Command>" "<categories>"
```

| Command | Completes |
|---|---|
| `CompletePlanet` | the world you are standing on |
| `CompleteBarrenPlanets` | every lifeless world |
| `CompleteLifePlanets` | every world with life |
| `CompleteAllPlanets` | the whole galaxy, both sweeps in one command |

Each category does only its own thing. The survey data (percentage, star map, survey panel, and the
Survey Data rewards) is written instantly and persists across saves, on any world including never-visited
ones. The galaxy commands run from anywhere: orbit, another system, or on foot. On-surface scanned
outlines and the in-world trait pillars refresh on the next area load.

There is also an optional **Settings > Gameplay > Auto-Complete Survey on Scan** toggle: with it on,
scanning any single plant or creature completes that whole world. (The plugin also sets the hand-scanner
to complete a species in one scan, the built-in equivalent of the Instant Scan mod.)

Full command reference: [docs/COMPLETION-COMMANDS.md](docs/COMPLETION-COMMANDS.md). Playstyle guide and
the Nexus page text: [docs/MOD-DESCRIPTION.md](docs/MOD-DESCRIPTION.md).

## Requirements and install

- [SFSE](https://www.nexusmods.com/starfield/mods/106) and
  [Address Library for SFSE Plugins](https://www.nexusmods.com/starfield/mods/3256), matching your game
  build. SFSE is locked to one exact build, so do not update the game ahead of a matching SFSE and mod
  build. See [CHANGELOG.md](CHANGELOG.md) for the versions each release targets.
- Install the release zip with Vortex or Mod Organizer 2 ("Install from file"). It is a flat `Data/`
  layout, no FOMOD. Enable `CompletePlanetSurvey.esm`, and launch through `sfse_loader.exe`.

## How it works

Three layers, each with one job:

```text
ESM (CompletePlanetSurvey.esm)        one CK-authored Settings toggle (GPOG + GPOF).
        |   Game.GetFormFromFile -> GameplayOption.GetValue()
Papyrus (CompletePlanetSurveyQuest)   the console commands: reads the toggle, validates and routes
        |                             the categories, calls the DLL natives.
DLL (CompletePlanetSurvey.dll)        the SFSE plugin: binds the Papyrus natives, writes survey state
                                      into the game's knowledge DB, reads authored species from
                                      Starfield.esm, and hooks the scan call site for the toggle.
```

Survey state is written directly into the engine's per-body knowledge database. No spawning, no
teleporting, no fast-travel. Species lists for never-visited worlds come from parsing Starfield.esm's
PNDT / Per Biome Data records at startup. The on-surface scanner outlines and trait pillars are the
game's own per-instance state, so they resolve when an area loads rather than being painted in place.

The reverse-engineering behind this (engine offsets, Address Library IDs, Ghidra decompiles, and every
approach that was tried) lives in [`re/`](re/). The details that matter for a given piece of code are
documented in comments right next to it.

## Repo layout

```text
src/Main.cpp                              SFSE plugin: natives, knowledge-DB writes, the scan hook
src/EsmReader.cpp, include/EsmReader.h    Starfield.esm PNDT/PPBD reader (galaxy species + markers)
Data/CompletePlanetSurvey.esm             CK-authored Settings toggle
Data/Scripts/Source/User/*.psc            Papyrus sources (the console commands)
Data/Scripts/*.pex                        compiled scripts
docs/                                     usage and design notes
re/                                       reverse-engineering trail (Ghidra output, save parsing, probes)
extern/CommonLibSF/                       SFSE / CommonLibSF (GPL-3.0)
build.bat / deploy.bat / import-esm.bat / package.py
```

## Build and development

Building the DLL needs little. Everything else is optional.

Build the DLL:

```bat
git submodule update --init --recursive   :: fetch CommonLibSF
build.bat                                  :: compile via xmake (releasedbg)
```

Needs xmake, MSVC (VS Build Tools), and Python 3. xmake fetches the rest (spdlog) on first build, the
same as CI.

Recompile Papyrus and deploy locally:

```bat
deploy.bat       :: compile Papyrus, copy DLL+ESM+PEX into the game, manage plugins.txt
import-esm.bat   :: copy the game ESM back into the repo (after editing in the Creation Kit)
```

`deploy.bat` compiles against the game's own base scripts and refuses to run while Starfield is open (a
locked partial copy would otherwise leave a stale DLL). Paths derive from `GAME_DIR` at the top of the
file; adjust that one line if your install is elsewhere.

Package the distributable:

```bat
python package.py --version X.Y.Z   :: -> Complete-Planet-Survey-X.Y.Z.zip
```

Run `build.bat` first. CI runs the same command on `v*` tags, which also cut the GitHub release and
upload to Nexus.

For reverse engineering you need Ghidra and a copy of `Starfield.exe`; the Address Library offsets file
(downloaded from Nexus, not checked in) only labels IDs in Ghidra and is not needed to build.
[CrashLoggerSF](https://www.nexusmods.com/starfield/mods/3273) is recommended for any hooking work. The
workflow notes are in the [starfield-modding skill](.claude/skills/starfield-modding/).

## License

GPL-3.0, see [LICENSE](LICENSE). This mod links against
[CommonLibSF](https://github.com/Starfield-Reverse-Engineering/CommonLibSF), which is GPL-3.0, so derived
works inherit GPL-3.0.

## Credits

- **SFSE** by ianpatt: the script extender
- **CommonLibSF**: Bethesda RE library (GPL-3.0)
- **Address Library for Starfield** by meh321 ([Nexus 3256](https://www.nexusmods.com/starfield/mods/3256))
- **Instant Scan** ([Nexus 759](https://www.nexusmods.com/starfield/mods/759)): reference for the hand-scanner setting
- **Ghidra** by the NSA: static analysis
- **Champollion**: Papyrus decompiler, used for stub generation
