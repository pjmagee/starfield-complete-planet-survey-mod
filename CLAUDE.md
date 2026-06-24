# Complete Planet Survey — Starfield SFSE mod

SFSE C++ plugin (CommonLibSF) + Papyrus + an ESM that completes planet surveys (resources,
flora/fauna, traits) from the console. Game 1.16.236≡1.16.244, SFSE 0.2.21.

## Build & deploy

- `build.bat` — builds the SFSE DLL (xmake/MSVC). Fails loud on compile error (older versions
  could ship a STALE DLL silently).
- `deploy.bat` — compiles Papyrus + copies DLL/ESM/.pex into the game. **Refuses while Starfield
  is running** (DLL/ESM are locked — close the game first).
- Compile one script only: `PapyrusCompiler.exe "<Script>" -i=<game>\Data\Scripts\Source;Data\Scripts\Source\User -f=<flags> -o=Data\Scripts`.
- **In-game behaviour can only be verified by the user** — the running game can't be automated.

## Layout

- `src/Main.cpp` — the whole plugin: REL::ID engine bindings, the native Papyrus functions, the
  survey/trait/species completion logic.
- `Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc` — the console commands.
- `Data/Scripts/Source/User/CompletePlanetSurveyNative.psc` — `native` decls for the C++ functions.
- `Data/CompletePlanetSurvey.esm` — authored in Creation Kit (CK can't edit a master in place).
- `re/` — reverse engineering: `re/ghidra/output/` (decompiles), `re/save/*.py` (save parsing),
  `re/frida/` (live probes), `re/esm/` (ESM extraction). `docs/` — usage docs.

## Console commands  (`cgf "CompletePlanetSurveyQuest.<Cmd>" "<args>"`)

Completion menu — each takes a category string (`resources,traits,fauna,flora`, or `all`):
`CompletePlanet`, `CompleteBarrenPlanets`, `CompleteLifePlanets`. See `docs/COMPLETION-COMMANDS.md`.

## Gotchas (these burned us)

- **Offsets = decompile addr − 0x140000000**; `REL::ID` resolves via the address library → version-
  specific. After a game patch SFSE ships first, versionlib lags days.
- **Crash-safety is paramount** — a native fault crashes the player's game. Null-check before every
  engine-pointer deref; wrap faultable engine calls in `/EHa` try-catch (src is built `/EHa`); bound
  all loops/allocs; guard fault-prone calls (e.g. `ID_83007(ref)!=0` before `ID_83008`).
- **Heap-corruption history** — never hand-write BSTArray `cap`/`size` or do manual allocator pokes;
  use CommonLibSF containers / the engine's own grow paths.
- **Logging** — spdlog must `flush_on(info)` (Starfield exit eats unflushed lines); runtime log under
  `…\My Games\Starfield\SFSE\Logs\CompletePlanetSurvey.log`.
- **Deep RE notes** live in the session's local memory + `re/`, not all in git.

## Skills

- `/grok` — xAI grok CLI as a separate-billing reviewer / background worker (`.claude/skills/grok/`).
