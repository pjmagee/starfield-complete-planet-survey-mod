# The marker catalog's ESM SOURCE — FLST + CTDA + CNDF (2026-06-22)

**Question (focus):** what ESM record TYPE(s) does the init that populates the runtime catalog
singletons `REL::ID(909810)`/`REL::ID(909812)` read, and where are those records in Starfield.esm?

**ANSWER: the catalogs are built from two FLST (FormList) records, each carrying inline CTDA
membership conditions per marker. The marker forms themselves are KYWD (Keyword) records.**

Parsed directly from `E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm`
(tools in `re/tools/esm_*.py`). All offsets/ids load-order 00.

---

## 1. The marker forms are KYWD records (`HandScanner*`)

Every slot+0x08 marker form-id is a **KYWD** record whose EDID starts `HandScanner`:

| form-id | EDID | role |
|---|---|---|
| 0x0023E90D | HandScannerAnyResource | flora slot0 / fauna shared "Resource" |
| 0x002634BE | HandScannerAnyBiomes | "Biomes" |
| 0x0023E90C | HandScannerPlantGenetics_Common_Standard_Carbon | flora "Genetics" |
| 0x00171867 | HandScannerPlantReproduction06 | flora "Reproduction" |
| 0x00171869 | HandScannerPlantReproduction04 | flora 5th |
| 0x002634C2 | HandScannerActorHealth | fauna "Reproduction"/Health |
| 0x001699B2 | HandScannerActorTemperament_Territorial | fauna X |
| 0x002634AD | HandScannerActorTemperament_Peaceful | fauna X |
| 0x002634AE | HandScannerActorTemperament_Skittish | fauna X |
| 0x00280172 | HandScannerActorTemperament_Wary | fauna X |
| 0x00280178 | HandScannerActorTemperament_Defensive | fauna X |

KYWD layout (uncompressed): `EDID` (cstr) · `CNAM`(u32 color 0xffffff00) · `TNAM`(u32=0x32 type) ·
`FNAM`(u32=0) · `FULL`(u32 lstring). The marker id IS the keyword form-id (runtime `*(form+0x28)`).

There are **87 `HandScanner*` KYWD records** total (full list dumped by `esm_kywd_species.py`):
PlantGenetics×28, ActorTemperament×7 (+ the Any*/Actor* category roots, PlantReproduction01-07,
TraitInfo00-26).

## 2. The catalog SOURCE = two FLST records (the registry)

| FLST form-id | EDID | → catalog | LNAM markers | conditioned entries |
|---|---|---|---|---|
| **0x00160C96** | **HandScannerPlantKeywords** | `REL::ID(909810/812)` flora | 39 | 37 |
| **0x00160C97** | **HandScannerActorKeywords** | fauna | 16 | 15 |

FLST subrecord layout (matches the runtime catalog struct read by `ID_83024`):
```
EDID                       (cstr)
LNAM × N    (u32 form-id)  -- the marker keyword ids, IN CATALOG ORDER == catalog+0x40 entry forms
[per conditioned entry:]
  INAM  (u32)              -- index INTO the LNAM array (which marker this block gates)
  CITC  (u32)              -- condition item count
  CTDA  × CITC (32 bytes)  -- membership condition(s)  == catalog+0x60 entry condition object
ANAM  (u32=0)              -- terminator
```
This is a 1:1 byte-for-byte image of the runtime catalog: `catalog+0x38`=entry count, `+0x40`=array
of marker forms (= LNAM order), `+0x60`=per-entry condition objects (= the CTDA blocks). The engine
init parses these FLSTs into 909810/909812. A marker with **no** INAM entry (e.g. plant Resource
0x0023E90D, plant Biomes 0x002634BE) is **unconditional** (always appended). A marker WITH an INAM
block is appended only if its CTDA passes for the species.

## 3. CTDA decode (32-byte Starfield condition) and the leaf functions

`+0x00 u8 op/flags · +0x04 f32 comp · +0x08 u16 funcIndex · +0x0C u32 param1 · +0x10 u32 param2 ·
+0x14 u32 p3 · +0x18 u32 p4 · +0x1C u32 runOn(0xffffffff=subject)`.

Function indices used by the marker conditions:
- **func=837** = EvaluateConditionForm(param1 = a **CNDF** record). Recurses into a named reusable
  condition (e.g. `HandScannerCND_DNAType_Standard`, `CND_ReproductionRhizomes`).
- **func=560** = HasKeyword(subject, param1 = a **KYWD**). Tests a content keyword (fauna temperament
  `CCT_Temperament_*`, `CCT_Enviro_Underground`, `ActorTypeCritter`).
- **func=858** = HasKeyword-style test where param1 = a **PLANET trait KYWD** (`PlanetWaterQuality*`,
  `PlanetAtmosphereToxicity01Toxic`, `PlanetTrait09EcologicalConsortium`). ← PLANET-CONTEXT, not species.
- **func=448** = HasPerk (param1 = PERK `Skill_Botany`/`Skill_Zoology`; p3/p4 = rank). Player-state →
  gates the *display* (NonLethalHarvest) markers only, irrelevant to a species' authored set.
- **func=699** = HasMagicEffectKeyword (param1 = HandScannerActor*EffectKeyword). For ability/
  weakness/resistance markers.
- **func=882** = unconditional/GetIsID true (op 0x40/0x60). The plain "always" marker.

CNDFs bottom out the same way: e.g. `CND_DNAType_Standard` = OR of `CND_DNABasis_*`, which are
`func=837`→`func=858 HasKeyword(species, PlantGenetics-basis KYWD)`. `CND_CritterPeaceful` =
`func=560 HasKeyword(CCT_Temperament_* / ActorTypeCritter)`.

## 4. DFOB named-default-object layer (the `ID_909814/816/...` singletons)

Each category marker also has a **DFOB** (Default Object) record `HandScanner*DO` whose `DATA`(u32)
points back to the KYWD: e.g. `HandScannerAnyResourceDO`(0x00237EB2)→0x0023E90D,
`HandScannerActorHealthDO`(0x002491FE)→0x002634C2, `HandScannerAnyBiomesDO`(0x002491FB)→0x002634BE.
These are the named singletons `ID_83057` switches against (the info-panel category formatter).

## 5. OFFLINE-DERIVATION FEASIBILITY (honest)

The catalog itself is **fully ref-free** (two FLSTs + their CTDA/CNDF tree, all static). The blocker is
the SUBJECT the conditions test:

- **Flora — TRACTABLE-ish.** The leaf tests are `HasKeyword(species, <PlantGenetics_* / DNA-basis /
  reproduction-method KYWD>)` and `GetActorValue(species, HandScannerPlantReproduction)`. BUT the
  FLOR leaf KWDA does **NOT** carry these content keywords (0/300 FLOR records carry any temperament/
  enviro leaf keyword; the genetics keywords are likewise not on the FLOR base). The flora's
  genetics/reproduction values are authored on the species' **component/keyword container**
  (`species[0x19]`, the runtime-resolved keyword set) and via the planet biome — not the flat KWDA.
  Some conditions are also **PLANET-trait** tests (func=858 PlanetWaterQuality/AtmosphereToxicity) →
  the SAME flora species gets a different Resource/Genetics marker on different planets. That is
  exactly the in-game "value-specific, same species splits green/blue" result, and it means a marker
  set is **(species × planet)**, not per-species.
- **Fauna — NOT TRACTABLE offline.** Fauna-X = `HandScannerActorTemperament_*`, gated by
  `HasKeyword(CCT_Temperament_*)`. Those temperament keywords are referenced in **zero** static
  RACE/NPC_ KWDA (1 of 7131 NPC_; the rest get tagged at runtime via CCT object-mods / CSTY / spawn).
  Temperament is a runtime classification, confirming the in-game "fauna-X materialization-bound"
  verdict. No pure-ESM read exists.

**Net:** the *registry* (which markers exist, their order, their conditions) is now fully recovered
from the ESM (FLST 0x00160C96 / 0x00160C97 + CTDA/CNDF). The *per-species evaluation* is NOT purely
ref-free because (a) the tested keywords live on the runtime keyword/component container, not the flat
KWDA, and (b) some conditions test PLANET traits, making the correct set per-(species,planet). The
closest tractable offline approximation: hardcode the unconditional markers (flora Resource+Biomes;
the always-on set), then for the conditioned genetics/reproduction/temperament markers fall back to
the live biome member-read on the current planet (the engine's own evaluated array) — which is what
`complete-scan-green-model-2026-06-22.md` already recommends.
