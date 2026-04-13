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
