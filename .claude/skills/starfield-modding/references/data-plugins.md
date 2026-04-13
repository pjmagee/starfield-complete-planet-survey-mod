# Data Plugins: ESM / ESP / ESL, Community Patch, Wrye Bash

Starfield ships content as **plugins**: record-oriented binary files that reference or override forms (quests, NPCs, items, worldspace cells). File extensions:

- `.esm` — master plugin (Starfield.esm, ShatteredSpace.esm)
- `.esp` — standard plugin
- `.esl` — "light" plugin; FormIDs prefixed `FE xxx` so thousands can coexist in the load order

## Load order

Controlled by `Plugins.txt` at `%LOCALAPPDATA%\Starfield\`. Order affects which mod "wins" conflicting records. Typical layering:

1. Starfield.esm + DLC
2. Unofficial/Community patches
3. Libraries/frameworks
4. Content mods
5. Tweaks and gameplay overrides
6. Patches (compatibility bridges between two mods)

## Tools

- **Creation Kit** — Bethesda's official editor; required for making quests, dialog, scripted forms.
- **xEdit / SF1Edit** — view/edit records across the entire load order; essential for conflict resolution and cleaning ITMs/ITPOs.
- **Wrye Bash** — https://github.com/wrye-bash/wrye-bash — load-order management, Bashed Patch generation, mod staging, BAIN installers. Understands Bash Tags embedded in plugin descriptions.

### Bash tags (relevant subset)

- `Names` — merge renamed forms
- `Stats` — merge stat changes
- `Relations` — faction changes
- `Delev` / `Relev` — leveled list removals/additions
- `NoMerge` — exclude from Bashed Patch

Tags go in the plugin description field so Wrye Bash picks them up automatically.

## Starfield Community Patch

Upstream: https://github.com/Starfield-Community-Patch/Starfield-Community-Patch

Community-maintained bug-fix baseline. Load high (right after official masters). Most mods should treat it as a soft dependency: don't duplicate its fixes, don't revert them. When patching a conflict, inspect in xEdit against the Community Patch first.

## FormID conventions

- Regular ESP/ESM: `XX000000` where `XX` is load-order slot (00 = first master).
- ESL-flagged: `FE xxx yyy` — `xxx` is the light slot, `yyy` is the local ID.
- Runtime-generated (scripts): above `0xFF000000`.
- Address-library-style plugin IDs — not related; those are code offsets, not FormIDs.

## Cleaning a plugin

In xEdit, for each plugin right-click -> "Apply Filter for Cleaning" -> remove ITM (identical to master) and UDR (undeleted/disabled references) records. Saves conflict pain downstream.

## Packaging data

- Loose files under `Data/` work but blow out the file count.
- Archive into `.ba2` with Creation Kit's archive tool or `Archive2.exe` for shipping.
- `MyMod - Main.ba2` and `MyMod - Textures.ba2` are auto-loaded if the plugin is enabled.
