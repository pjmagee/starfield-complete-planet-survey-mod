# Complete Planet Survey — Starfield SFSE mod

SFSE C++ plugin (CommonLibSF) + Papyrus + an ESM that completes planet surveys (resources,
flora/fauna, traits) from the console. Game/SFSE build targets are recorded **per release in
CHANGELOG.md** — don't hardcode them in docs (they go stale; the README carries no pin by design).

## Build, test & deploy

- `build.bat` — builds the SFSE DLL (xmake/MSVC). Fails loud on compile error (older versions
  could ship a STALE DLL silently).
- **Build modes** — `build.bat` and CI build `releasedbg` (optimized **with** symbols — the ship
  config, the one Address Library + crash triage want). For a verbose dev build: `xmake f -m debug`
  then `xmake`. The DLL gates its spdlog level on `NDEBUG`: **release = INFO** (per-planet/per-species
  green & resource lines are DEBUG, so a galaxy completion doesn't dump ~20k lines), **debug = DEBUG**
  (full per-body trace). Player-facing Papyrus popups are NOT debug — they always show.
- `test\build_validate.bat` — the only automated test: compiles the REAL `src/EsmReader.cpp`
  (stub spdlog) and validates the 17 ground-truth Jemison species markers straight from
  `Starfield.esm` (`CPS_ESM_PATH` overrides the default Steam path). Run it after any
  EsmReader/marker change — it proves the derivation offline BEFORE any in-game test.
- `deploy.bat` — compiles Papyrus + copies DLL/ESM/.pex into the game. **Refuses while Starfield
  is running** (DLL/ESM are locked — close the game first).
- Compile one script only: `PapyrusCompiler.exe "<Script>" -i=<game>\Data\Scripts\Source;Data\Scripts\Source\User -f=<flags> -o=Data\Scripts`.
- **In-game behaviour can only be verified by the user** — the running game can't be automated.

## CI & release (`.github/workflows/`)

- `build.yml` — every push/PR: releasedbg build + `package.py` zip artifact. A `vX.Y.Z` tag also
  cuts the GitHub Release AND uploads to Nexus. Repo var `NEXUS_FILE_ID` must be the mod-file
  **GROUP id** (7302980), not a per-upload file id — a wrong id 404'd the v1.1.0 Nexus upload;
  the group id is validated (v1.2.0 + v1.3.0 tag runs green).
- `pages.yml` — renders the LikeC4 architecture model (`docs/*.c4`) to GitHub Pages;
  `docs/dist/` is CI output and gitignored.
- Release flow: update CHANGELOG.md (use the `changelog` skill) → tag → CI publishes → paste
  `docs/NEXUS-DESCRIPTION.bbcode.txt` (into the Nexus editor's **BBCode source view**, not the
  WYSIWYG view) and `docs/NEXUS-CHANGELOG.txt` onto mod page 16493.

## Layout

- `src/Main.cpp` — the plugin: REL::ID engine bindings, the native Papyrus functions, the
  survey/trait/species completion logic, character-Statistics writes, the scan-site hook.
- `src/EsmReader.cpp` + `include/EsmReader.h` — offline multi-master PNDT/PPBD reader (galaxy
  species lists + scan markers for never-visited worlds). Parses EVERY loaded master in load order
  (base + DLC/Creations, e.g. ShatteredSpace.esm — the Dazra/Va'ruun'kai fix) with per-file FormID
  remapping to runtime ids (full / FD-medium / FE-small index spaces, later file overrides earlier);
  Main.cpp hands it the engine load order from TESDataHandler at kPostDataLoad (any failure → the
  old Starfield.esm-only fallback). Offline: `CPS_ESM_PATHS` (';'-separated load order) or legacy
  single-file `CPS_ESM_PATH`.
- `Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc` — the console commands.
- `Data/Scripts/Source/User/CompletePlanetSurveyNative.psc` — `native` decls for the C++ functions.
- `Data/CompletePlanetSurvey.esm` — the Settings toggles (Hand Scanner `0x80C`, Orbital Scanner
  `0x80D`), authored in Creation Kit (CK can't edit a master in place). NEW GPOF toggles can instead
  be ADDED by Python binary surgery on the master: clone an existing GPOF record, append its FormID to
  the GPOG `GOGL` list, and fix every GRUP size + the HEDR record count (how the Orbital Scanner toggle
  was added). Papyrus keys on the FormID, so NNAM/DNAM (name/description) are safe display-only edits.
  Full checklist: `docs/ESM-BINARY-EDIT-CHECKLIST.md`. Every pinned FormID Papyrus reads from this
  master is centralized in one block at the top of `CompletePlanetSurveyQuest.psc`, resolved through
  a shared `ResolveEsmForm` helper that logs a named error on a miss (fail-closed for toggles,
  fail-open for the cosmetic intro popup — see that file's policy comment).
- `test/` — the offline marker-validation harness (above) + QA notes.
- `re/` — reverse engineering: `re/ghidra/output/` (decompiles), `re/save/*.py` (save parsing),
  `re/frida/` (live probes), `re/esm/` (ESM extraction). The AUTHORITATIVE RE source is the local
  queryable Ghidra project `ghidra-project/Starfield.gpr` (gitignored, ~3.4 GB) — the
  `re/ghidra/output/*.txt` dumps are point-in-time and can be STALE/incomplete (a subagent hooked the
  wrong function off them until the live project corrected it). Query it headless:
  `analyzeHeadless.bat ghidra-project Starfield -process Starfield.exe -noanalysis -scriptPath
  re/ghidra/scripts -postScript DecompileIds.java <out> <ids…>` (also `XrefsToId.java`,
  `FindStringXrefs.java`). Ghidra at `C:\Tools\ghidra_12.1.2_PUBLIC`; regenerate the project via the
  `starfield-modding` skill if missing.
- `docs/` — player docs (`COMPLETION-COMMANDS.md`, `how-it-works.md`, `MISC-STATS.md`), Nexus
  page sources (`MOD-DESCRIPTION.md` + the two `NEXUS-*` paste files), the LikeC4 model (`*.c4`),
  and design/review archives (`grok-review-*`, `dead-code-audit.md`, `feature-*.md`, …).

## Console commands  (`cgf "CompletePlanetSurveyQuest.<Cmd>" "<args>"`)

Completion menu — each takes a category string (`resources,traits,fauna,flora`, or `all`):
`CompletePlanet`, `CompleteBarrenPlanets`, `CompleteLifePlanets`, `CompleteAllPlanets` (whole
galaxy, both sweeps, one result). Completion also writes the character-sheet Statistics counters.
Two ESM Settings toggles auto-complete on a scan: **Hand Scanner** (`0x80C`, on-surface hand-scanner
→ `ID_52157`→`ID_97853` hook) and **Orbital Scanner** (`0x80D`, star-map scan → `ID_52173`→`ID_97853`
hook, ScanLevelChanged; also repaints the star-map panel in place). See `docs/COMPLETION-COMMANDS.md`.

## Gotchas (these burned us)

- **Offsets = decompile addr − 0x140000000**; `REL::ID` resolves via the address library → version-
  specific. After a game patch SFSE ships first, versionlib lags days.
- **Crash-safety is paramount** — a native fault crashes the player's game. Null-check before every
  engine-pointer deref; wrap faultable engine calls in `/EHa` try-catch (src is built `/EHa`); bound
  all loops/allocs; guard fault-prone calls (e.g. `ID_83007(ref)!=0` before `ID_83008`).
- **Heap-corruption history** — never hand-write BSTArray `cap`/`size` or do manual allocator pokes;
  use CommonLibSF containers / the engine's own grow paths.
- **Formless scripts unbind on quit-to-menu** — the VM drops their types on session teardown, which
  unbinds the natives; Main.cpp re-binds per session. Any NEW native must be registered in that same
  re-bind path or it dies after a Main-Menu reload.
- **Logging** — spdlog must `flush_on(info)` (Starfield exit eats unflushed lines); runtime log under
  `…\My Games\Starfield\SFSE\Logs\CompletePlanetSurvey.log`.
- **Deep RE notes** live in the session's local memory + `re/`, not all in git.

## Skills

- `/changelog` — honest tag-to-tag CHANGELOG + Nexus-notes maintenance (behaviour vs refactor split,
  versioning policy).
- `/starfield-modding` — SFSE / CommonLibSF / Papyrus / Ghidra / address-library workflow reference.
- `/grok` — xAI grok CLI as a separate-billing reviewer / background worker (`.claude/skills/grok/`).
