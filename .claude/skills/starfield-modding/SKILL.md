---
name: starfield-modding
description: |
  End-to-end guidance for modding and reverse engineering Bethesda's Starfield: SFSE plugin development with CommonLibSF, Papyrus scripting and decompilation (Champollion), binary analysis in Ghidra, address-library offset discovery across game versions, and packaging/distribution via FOMOD, Wrye Bash, and Nexus Mods.

  USE WHEN: user works on Starfield mods (SFSE plugins, Papyrus scripts, ESM/ESP plugins), reverse engineers Starfield.exe, hunts for function offsets after a game update, decompiles .pex scripts, or packages/distributes a mod.

  COVERS: SFSE plugin anatomy, address library IDs, CommonLibSF RE/ types, Papyrus <-> native boundaries, Ghidra signature hunting, AOB/sig updating workflow, FOMOD structure, Wrye Bash tagging, community-patch conventions.

---

# Starfield Modding & Reverse Engineering

Bethesda officially supports Starfield modding — Creation Kit is first-party, SFSE and CommonLibSF are mature community projects, and Nexus Mods is the primary distribution channel. This skill is the playbook for shipping real mods.

---

## Landscape at a Glance

| Layer | What it is | Primary tool |
|---|---|---|
| Native code extension | DLL injected into Starfield.exe | [SFSE](https://github.com/ianpatt/sfse) + CommonLibSF |
| Native binary analysis | Reversing Starfield.exe to find offsets/types | [Ghidra](https://github.com/NationalSecurityAgency/ghidra) (+ IDA) |
| Scripting | Papyrus `.psc` -> compiled `.pex` | Creation Kit compiler; [Champollion](https://github.com/Orvid/Champollion) to decompile |
| Data/records | ESM/ESP/ESL plugins (forms, quests, records) | Creation Kit, xEdit (SF1Edit) |
| Load order / patching | Merging, tagging, conflict resolution | [Wrye Bash](https://github.com/wrye-bash/wrye-bash) |
| Compat baseline | Bug fixes + cross-mod glue | [Starfield Community Patch](https://github.com/Starfield-Community-Patch/Starfield-Community-Patch) |
| Install format | Versioned, option-driven installer | FOMOD (ModuleConfig.xml) |
| Distribution | Nexus Mods (primary), GitHub for source | [Address Library (Nexus 3256)](https://www.nexusmods.com/starfield/mods/3256), [SFSE distribution (Nexus 7589)](https://www.nexusmods.com/starfield/mods/7589) |

---

## First Response Protocol

When the user poses a Starfield modding task, produce:

1. **Task classification** — plugin (native), script (Papyrus), data (ESM), or hybrid
2. **Game version target** — offsets and structs vary per update
3. **Tool plan** — which of the above layers are in scope
4. **Risk notes** — offset rot after patches, load-order hazards
5. **First concrete action** — one command or file to open

Do not speculate about offsets or function signatures — verify with Address Library / Ghidra.

---

## Quick Reference by Task

- **Writing an SFSE plugin** → [references/sfse.md](references/sfse.md) and [references/commonlibsf.md](references/commonlibsf.md)
- **Finding or updating an offset after a game patch** → [references/reverse-engineering.md](references/reverse-engineering.md)
- **Working with Papyrus / decompiling .pex** → [references/papyrus.md](references/papyrus.md)
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

- Loading a plugin built against a different CommonLibSF commit than the runtime expects — linker will succeed, behavior will be undefined.
- Using Address Library IDs from one version's database against a different version.
- Shipping `.pex` without the matching `.psc` — makes future maintenance impossible.
- Forgetting to register FormIDs as `0xFE000000`-prefixed when shipping as ESL-flagged.
- Publishing without a FOMOD when the mod has optional features — users end up manually copying files.

---

## Project-Local Context

This repo (`starfield-complete-planet-survey-mod`) is a concrete example of the hybrid pattern:
- Native DLL in [src/Main.cpp](../../../src/Main.cpp) registering Papyrus functions via CommonLibSF
- Papyrus caller in [Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc](../../../Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc)
- Address library offsets file (`offsets-1-16-236-0.txt`) is **not checked in** (19 MB) — download from Address Library (Nexus 3256) and place at repo root
- Ghidra analysis artifacts in [re/](../../../re/) — scripts under `re/ghidra/scripts/`, decompile dumps under `re/ghidra/output/`, Python helpers under `re/tools/`
- Ghidra project DB (`ghidra-project/Starfield.gpr` + `Starfield.rep`) is local-only (gitignored). In Ghidra's Script Manager add `re/ghidra/scripts/` as a script directory so the canonical scripts run from there.
- Champollion decompiler binary in `tools/champollion/` (local-only, gitignored)
- FOMOD installer staged in [fomod/](../../../fomod/)

Use these as the first place to ground any concrete suggestion before looking elsewhere.
