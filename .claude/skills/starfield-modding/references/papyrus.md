# Papyrus: Scripting & Decompilation

Papyrus is Bethesda's in-game scripting language. Source is `.psc`, compiled bytecode is `.pex`. The Creation Kit (CK) compiler produces `.pex`; Champollion reverses it.

## Authoring

- Scripts live under `Data/Scripts/Source/User/*.psc` (or `Base/` for vanilla/DLC).
- Compile via the CK's Papyrus compiler (`Papyrus\Compiler\PapyrusCompiler.exe`) or the CK GUI.
- Flags live in `Data/Scripts/Source/Base/TESV_Papyrus_Flags.flg` (Skyrim legacy name; Starfield uses its own variant).
- Compiled `.pex` goes into `Data/Scripts/`.

Example binding to a native function:

```papyrus
Scriptname MyPlugin:Survey extends Quest

Int[] Function GetAllResourceActorValueIDs() native global
Int Function ApplyPlanetSurveyPercent(ObjectReference akPlanet, Float afPercent) native global
```

The `native global` signature must exactly match what the SFSE plugin registers with `BindNativeMethod`. Mismatches cause silent no-ops or crashes.

## Starfield-new language features

The CK's Papyrus adds capabilities over Skyrim/Fallout Papyrus
([wiki: Papyrus — New Features](https://starfieldwiki.net/wiki/Starfield_Mod:Papyrus_-_New_Features)):

- **Guards** — critical-path single-thread protection. A `Guard` marks a region
  so the VM serialises access to shared state, avoiding the races you'd hit when
  multiple script instances touch the same data. (This repo solves the same class
  of problem at the native layer with a per-frame poller + atomics; Papyrus
  Guards are the in-script equivalent.)
- **Structs** — near first-class user-defined data structures (named fields
  passed around as a value) instead of parallel arrays.
- **Imports** — import namespaces *and* attributes from other scripts, not just
  global functions — cuts `OtherScript.Function()` boilerplate.

These are CK-compiler features; a `.pex` using them won't run on older engines,
which is moot for Starfield-only mods.

## Base script reference (Object Scripts)

The base-game Papyrus API — what each script *type* exposes — is on the wiki's
[Papyrus category](https://starfieldwiki.net/wiki/Category:Starfield_Mod-Papyrus)
(the "Object Scripts" reference, ~32 types). Use it for a real signature on a
vanilla type instead of guessing; each page lists the `ScriptName … Extends …`
header and its members. (starfieldwiki.net 403s automated fetchers — open in a
browser.)

Notable types:
- **Starfield-new**: `SpaceshipBase`, `SpaceshipReference`,
  `LeveledSpaceshipBase`, `GameplayOption` (Settings-menu options),
  `Terminal` / `TerminalMenu`, `ConditionForm`, `WwiseEvent`.
- **Workhorses** (as before): `ObjectReference`, `Actor`, `Game`, `Form`,
  `Quest`, `Keyword`, `FormList`, `Utility`, `Debug`, `Math`,
  `ReferenceAlias` / `RefCollectionAlias`.

Example — the [`GameplayOption`](https://starfieldwiki.net/wiki/Starfield_Mod:Script-GameplayOption)
type this repo's Settings → Gameplay toggle uses:

```papyrus
ScriptName GameplayOption Extends Form Native Hidden

float Function GetValue()      ; the toggle / slider value
float Function GetRewardValue()
float Function GetXPTotal()
Function NotifyGameplayOptionUpdateFinished()
```

`CompletePlanetSurveyQuest.psc` does exactly this: resolve the GPOF via
`Game.GetFormFromFile(0x80C, "CompletePlanetSurvey.esm") as GameplayOption`, then
gate on `GetValue()`. See [record-types.md](record-types.md) for the `GPOF`/`GPOG`
record side and [creation-kit.md](creation-kit.md) for authoring it.

## Decompiling with Champollion

Upstream: https://github.com/Orvid/Champollion
Vendored here: [tools/champollion/](../../../tools/champollion/)

```
champollion.exe path\to\Script.pex -o output_dir
```

Produces a `.psc` that's close to the original but loses comments, local names, and some inlined constants. Treat decompiled output as read-only reference, not source of truth.

Use cases:
- Understanding a vanilla script you want to override
- Inspecting another mod's logic for conflict analysis
- Recovering a `.psc` you lost (last resort)

Do **not** decompile, modify, and redistribute someone else's mod without permission.

## Native <-> Papyrus boundary

- Primitives: `Int`, `Float`, `Bool`, `String` map to `std::int32_t`, `float`, `bool`, `RE::BSFixedString`.
- Arrays: `Int[]` in Papyrus <-> `std::vector<std::int32_t>` in native (CommonLibSF marshals).
- Forms: `ObjectReference`, `Actor`, `Quest` etc. <-> corresponding `RE::TES*` pointers.
- Nullable: Papyrus `None` <-> `nullptr`. Always guard.
- Blocking calls: native functions should be fast. Long-running work — spawn a thread, return immediately, notify via a registered event.

## Debugging

- Enable papyrus logging in `StarfieldCustom.ini`:
  ```ini
  [Papyrus]
  bEnableLogging=1
  bEnableTrace=1
  bLoadDebugInformation=1
  ```
- Logs: `Documents\My Games\Starfield\Logs\Script\Papyrus.0.log`
- Use `Debug.Trace("message")` in scripts; appears in the log.

## Project-local example

- Source: [Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc](../../../Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc)
- Compiled: [Data/Scripts/CompletePlanetSurveyQuest.pex](../../../Data/Scripts/CompletePlanetSurveyQuest.pex)
- Native side: [src/Main.cpp](../../../src/Main.cpp)
