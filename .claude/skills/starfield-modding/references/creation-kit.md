# Creation Kit (the official authoring path)

The **Creation Kit (CK)** is Bethesda's first-party tool for viewing and editing
Starfield's data files — records, quests, dialogue, cells, and Papyrus scripts.
It's the "standard" modding path, as opposed to this repo's RE/SFSE-native path.
Most of the rest of this skill is about the hard path; reach for the CK when the
task is **authoring game data** (records/ESMs, quests, scripts) rather than
hooking the binary.

Canonical docs: [Starfield Wiki — Creation Kit](https://starfieldwiki.net/wiki/Starfield_Mod:Creation_Kit)
and the [Mod main page](https://starfieldwiki.net/wiki/Starfield_Mod:Main_Page).
(starfieldwiki.net 403s automated fetchers — open in a browser.)

## Getting it

- Released **2024-06-09** alongside a game patch.
- Free on **Steam → Library → Tools → "Starfield: Creation Kit"** (~823 MB).
  Only visible to accounts that own Starfield. **PC only.**
- Runs as a separate program from the game.

## What it's for

Nearly any non-binary aspect of the game: create new records, edit existing
ones, build quests/dialogue/scenes, place references in cells/worlds, author and
compile Papyrus, and retexture. It writes correctly-ordered, valid ESM/ESP
plugins (the [record/group order](record-types.md) you'd otherwise hand-maintain).

## Where it fits this repo

This mod is a hybrid: the **DLL is RE/SFSE**, but the **Settings → Gameplay
toggle is a CK-authored ESM** (a `GPOG` group header + a `GPOF` GameplayOption).
Key consequences already learned the hard way:

- The CK can only **save into the game's `Data/` folder**; `import-esm.bat`
  copies the saved ESM back into the repo. Don't hand-edit the ESM by script —
  re-author in the CK and re-import.
- The CK's default save is an **`.esp`**; this mod ships an **`.esm`** (MASTER
  flag set). Having both an `.esp` and `.esm` of the same name in `Data/` is a
  load crash. The `0x80` (Bethesda-DLC) flag must **not** be set on hand/CK
  plugins — it crashes on load.
- Editing a record in the CK can **reassign FormIDs**, which breaks the
  `Game.GetFormFromFile(0x80C, …)` lookup in Papyrus — verify FormIDs in
  SF1Edit after any CK edit. See [data-plugins.md](data-plugins.md).

## Papyrus in the CK

The CK ships the **Papyrus compiler** (`PapyrusCompiler.exe` under the CK's
`Tools/Papyrus Compiler/`) and the base-game script sources. This repo's
`deploy.bat` invokes that compiler against `temp_scripts/` (base sources +
`Starfield_Papyrus_Flags.flg`, both pulled from the CK install). See
[papyrus.md](papyrus.md) for authoring/compiling and the base-script reference.

## When NOT to use the CK

If the data you need to change isn't exposed as an editable record — e.g. the
per-planet survey knowledge lives in a runtime `BSComponentDB2`, not in any
PNDT field — the CK can't help. That's when you drop to the SFSE/RE path
([sfse.md](sfse.md), [reverse-engineering.md](reverse-engineering.md)).
