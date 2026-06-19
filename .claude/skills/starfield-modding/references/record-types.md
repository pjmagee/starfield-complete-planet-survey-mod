# Starfield Record / Form Types

The ESM/ESP mod file format. At the **top level** it's structured like Skyrim
(a `TES4` header record, then groups of records); the **record and especially
subrecord** layouts are Starfield-specific. Source of truth for the exhaustive
field-level breakdown is the [Starfield Wiki Mod File Format](https://starfieldwiki.net/wiki/Starfield_Mod:Mod_File_Format)
page (note: starfieldwiki.net 403s automated fetchers — open it in a browser, or
inspect records directly in SF1Edit/xEdit).

Use this when you need to know which record type holds a given kind of data,
what a 4-char signature means, or how a plugin's groups are ordered.

## Signature ↔ engine type (the ones that matter for this repo / RE)

Each record type is a 4-character signature. The Starfield-distinctive ones —
the galaxy/ship records that don't exist in prior Bethesda games — are where RE
and CK work overlap:

| Sig | Engine type (CommonLibSF) | What it is |
|---|---|---|
| `PNDT` | `BGSPlanet::PlanetData` | **Planet / moon** — the per-body data record (this repo's survey work keys off these) |
| `STDT` | `BSGalaxy::BGSStar` | **Star / star system** |
| `BIOM` | `BGSBiome` | **Biome** — surface biome definition (flora/fauna sets) |
| `ATMO` | atmosphere | Planet atmosphere |
| `SUNP` | `BGSSunPresetForm` | Sun preset |
| `GBFM` / `GBFT` | global form / template | Generic base form + template (Starfield's data-driven object system) |
| `OMOD` | object modification | Weapon/armor/ship mods |
| `LVLB` / `BMOD` / ship records | spaceship modules | Starfield ship-building data |
| `FLOR` / `NPC_` | flora / actor | The biome species our scan-completion enumerates |
| `LCTN` | `BGSLocation` | Location (planets/cells hang off the location hierarchy) |
| `KYWD` (`KYWD` group) | `BGSKeyword` | Keywords — planet **traits** are keywords (`GetKeywordTypeList(44)`) |

Common "vanilla" types still present: `WEAP AMMO ARMO ALCH BOOK MISC CONT
ACTI FURN CELL WRLD QUST DIAL INFO PERK MGEF SPEL ENCH FACT RACE GMST GLOB
FLST PACK SCEN TERM` (terminals) etc.

## Top-level group order

`Starfield.esm` lays its groups out in a fixed order (assumed load-significant,
as in Skyrim): it opens with `TES4` then `GMST KYWD FFKW LCRT AACT TRNS TXST
GLOB …` and the galaxy/ship records cluster late — `… BIOM … ATMO LVSC SPCH …
PNDT CNDF PCBN PCCN STDT …`. The full ~180-group ordering is on the wiki's
[Mod File Format](https://starfieldwiki.net/wiki/Starfield_Mod:Mod_File_Format)
page. You rarely need the whole order; you need it when hand-building or
diagnosing a malformed plugin (CK/SF1Edit write the correct order for you).

## Subrecords

Records are built from 4-char **subrecords**. A widely-shared one is `CTDA`
(condition data — the same condition system used across quests, perks, magic
effects, packages). Record-specific subrecords (e.g. `WEAP`'s `DATA`) are
documented per-record on the wiki.

## How to actually inspect records

- **SF1Edit / xEdit** — the practical tool: open the plugin, browse records by
  type, see decoded subrecords, spot conflicts. This is how you confirm a
  FormID or field rather than guessing.
- **Creation Kit** — authoritative authoring view (see [creation-kit.md](creation-kit.md)).
- **Ghidra** — when you need the *runtime* struct behind a record type
  (e.g. `PNDT` → `BGSPlanet::PlanetData`, used in [planet-survey-internals.md](planet-survey-internals.md)).

Project tie-in: this mod's Settings toggle is a `GPOG`/`GPOF`
(GameplayOptionGroup / GameplayOption) pair authored in the CK — see
[data-plugins.md](data-plugins.md) for FormID conventions and load order, and
[papyrus.md](papyrus.md) for the `GameplayOption` script that reads it.
