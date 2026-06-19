---
name: starfield-modding
description: |
  End-to-end guidance for modding and reverse engineering Bethesda's Starfield: SFSE plugin development with CommonLibSF, Papyrus scripting and decompilation (Champollion), binary analysis in Ghidra (GUI and `analyzeHeadless` CLI for scripted/agent-driven RE), address-library offset discovery across game versions, and packaging/distribution via FOMOD, Wrye Bash, and Nexus Mods.

  USE WHEN: user works on Starfield mods (SFSE plugins, Papyrus scripts, ESM/ESP plugins), reverse engineers Starfield.exe, hunts for function offsets after a game update, decompiles .pex scripts, drives Ghidra in headless mode for automated analysis, or packages/distributes a mod.

  COVERS: SFSE plugin anatomy, address library IDs, CommonLibSF RE/ types, Papyrus <-> native boundaries, Ghidra signature hunting (interactive and headless `analyzeHeadless` CLI workflows), AOB/sig updating workflow, FOMOD structure, Wrye Bash tagging, community-patch conventions.

---

# Starfield Modding & Reverse Engineering

Bethesda officially supports Starfield modding — Creation Kit is first-party, SFSE and CommonLibSF are mature community projects, and Nexus Mods is the primary distribution channel. This skill is the playbook for shipping real mods.

---

## Landscape at a Glance

| Layer | What it is | Primary tool |
|---|---|---|
| Native code extension | DLL injected into Starfield.exe | [SFSE](https://github.com/ianpatt/sfse) + CommonLibSF |
| Native binary analysis | Reversing Starfield.exe to find offsets/types | [Ghidra](https://github.com/NationalSecurityAgency/ghidra) (GUI **or** `analyzeHeadless` CLI) (+ IDA) |
| Scripting | Papyrus `.psc` -> compiled `.pex` | Creation Kit compiler; [Champollion](https://github.com/Orvid/Champollion) to decompile |
| Data/records | ESM/ESP/ESL plugins (forms, quests, records) | Creation Kit, xEdit (SF1Edit) |
| Load order / patching | Merging, tagging, conflict resolution | [Wrye Bash](https://github.com/wrye-bash/wrye-bash) |
| Compat baseline | Bug fixes + cross-mod glue | [Starfield Community Patch](https://github.com/Starfield-Community-Patch/Starfield-Community-Patch) |
| Install format | Versioned, option-driven installer | FOMOD (ModuleConfig.xml) |
| Distribution | Nexus Mods (primary), GitHub for source | [Address Library (Nexus 3256)](https://www.nexusmods.com/starfield/mods/3256), [SFSE distribution (Nexus 7589)](https://www.nexusmods.com/starfield/mods/7589) |
| Reference docs | CK records, Papyrus base scripts, form/file formats | [Starfield Wiki](https://starfieldwiki.net/wiki/Starfield_Mod:Main_Page) (the canonical CK/Papyrus/data reference; 403s automated fetchers — open in a browser) |

---

## First Response Protocol

When the user poses a Starfield modding task, produce:

1. **Verify deployment state** — what's actually installed in `Data\`, `Data\SFSE\Plugins\`, `Plugins.txt`, `ContentCatalog.txt`, and the SFSE log dir. Filesystem is the truth; READMEs and prior session memory are hearsay until checked. See [references/deployment-state.md](references/deployment-state.md).
2. **Task classification** — plugin (native), script (Papyrus), data (ESM), or hybrid
3. **Game version target** — offsets and structs vary per update
4. **Tool plan** — which of the above layers are in scope
5. **Risk notes** — offset rot after patches, load-order hazards
6. **First concrete action** — one command or file to open

Do not speculate about offsets, function signatures, or what's currently deployed — verify with Address Library / Ghidra / the filesystem.

**Prefer Ghidra's headless CLI for agent-driven RE.** Ghidra ships `analyzeHeadless.bat` (Windows) / `analyzeHeadless` (Unix) under `<ghidra>/support/`. It runs import, analysis, and Java/Jython post-scripts non-interactively — ideal when Claude is driving the loop (batch decompile dumps, offset-label imports, pattern searches, cross-version diffs) instead of a human clicking in the GUI. Reach for headless mode by default for any automatable RE step; only open the GUI when you need to browse decompiler output or the graph view. See [references/reverse-engineering.md](references/reverse-engineering.md) for the commands and script-authoring rules (no `askFile`, positional args, etc.).

---

## Quick Reference by Task

- **Updating the mod after a Starfield game patch (versionlib lag, IDDB load crash, offset rot, shipping the recompile)** → [references/game-patch-update.md](references/game-patch-update.md)
- **Auditing what's actually deployed (start of session, before claiming "no conflict", post-pivot cleanup)** → [references/deployment-state.md](references/deployment-state.md)
- **Writing an SFSE plugin** → [references/sfse.md](references/sfse.md) and [references/commonlibsf.md](references/commonlibsf.md)
- **Finding or updating an offset after a game patch** → [references/reverse-engineering.md](references/reverse-engineering.md)
- **Working with Papyrus — authoring, decompiling, Starfield-new features (Guards/Structs/Imports), base-script API** → [references/papyrus.md](references/papyrus.md)
- **Record / form types (`PNDT`, `STDT`, `BIOM`, ship records, top-level group order)** → [references/record-types.md](references/record-types.md)
- **Authoring records / ESMs / quests in the Creation Kit (the official data path)** → [references/creation-kit.md](references/creation-kit.md)
- **Editing ESM/ESP or merging plugins** → [references/data-plugins.md](references/data-plugins.md)
- **Packaging an installer / shipping to Nexus** → [references/packaging.md](references/packaging.md)
- **Writing to the planet-survey knowledge DB** → [references/planet-survey-internals.md](references/planet-survey-internals.md) (1.16.236.0 offsets, ID_124898/97853, BSComponentDB2 keys)

---

## Core Invariants

- **Address Library is the ABI.** Never hardcode `0x1400XXXXX` — always look up by stable ID. See Address Library for Starfield (Nexus 3256). When the game patches, only the ID -> offset map changes; your plugin's IDs stay the same.
- **Version-check in plugin manifest.** SFSE plugins declare supported runtime versions; mismatches should fail to load loudly, not crash silently.
- **Papyrus calls native via registered functions.** The Papyrus <-> C++ boundary is where most SFSE plugin value lives; keep signatures and thread-safety in mind.
- **ESM/ESP ordering matters.** Community Patch sits near the top; gameplay overhauls later. Bash tags drive Wrye Bash's bashed patch merging.
- **Never edit `.pex` directly.** Decompile with Champollion for reading only; recompile from authored `.psc` with the Creation Kit compiler.

---

## Common Pitfalls

- **Trusting the README / project memory over the filesystem** when claiming what's deployed. Architecture pivots (ESM → SFSE) leave the old layer running unless it's actively removed — `Plugins.txt` and `ContentCatalog.txt` don't auto-clean. Run the [deployment-state checklist](references/deployment-state.md) at the start of every session and before any "X is the only thing doing Y" claim.
- Loading a plugin built against a different CommonLibSF commit than the runtime expects — linker will succeed, behavior will be undefined.
- Using Address Library IDs from one version's database against a different version.
- Shipping `.pex` without the matching `.psc` — makes future maintenance impossible.
- Forgetting to register FormIDs as `0xFE000000`-prefixed when shipping as ESL-flagged.
- Publishing without a FOMOD when the mod has optional features — users end up manually copying files.
- Running an SFSE-only mod without [CrashLoggerSF](https://www.nexusmods.com/starfield/mods/3273) — the raw `.dmp` Starfield drops on crash needs WinDbg + symbols. CrashLogger writes a readable stack trace to `SFSE\Logs\crash-*.log`. Install before any RE or hooking work.

---

## Project-Local Context

This repo (`starfield-complete-planet-survey-mod`) is a concrete example of the hybrid pattern:
- Native DLL in [src/Main.cpp](../../../src/Main.cpp) registering Papyrus functions via CommonLibSF
- Papyrus caller in [Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc](../../../Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc)
- Address library offsets file (`offsets-1-16-236-0.txt`) is **not checked in** (19 MB) — download from Address Library (Nexus 3256) and place at repo root
- Ghidra analysis artifacts in [re/](../../../re/) — scripts under `re/ghidra/scripts/`, decompile dumps under `re/ghidra/output/`, Python helpers under `re/tools/`
- Ghidra project DB (`ghidra-project/Starfield.gpr` + `Starfield.rep`) is local-only (gitignored). In Ghidra's Script Manager add `re/ghidra/scripts/` as a script directory so the canonical scripts run from there.
- Champollion decompiler binary in `tools/champollion/` (local-only, gitignored)
- Distributable is a **flat `Data/`-prefixed zip** built by [package.py](../../../package.py) (FOMOD was dropped in v1.0.5 for MO2/Vortex "Install From File" compatibility); CI runs it on `v*` tags and auto-uploads to Nexus

Use these as the first place to ground any concrete suggestion before looking elsewhere.
