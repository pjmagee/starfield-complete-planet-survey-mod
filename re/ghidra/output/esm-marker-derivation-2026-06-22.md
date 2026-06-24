# Pure-ESM slot+0x08 Marker Derivation — DEFINITIVE PLAN (2026-06-22)

Synthesis of 4 RE maps. Goal: compute every species' exact `slot+0x08` marker set
**purely from Starfield.esm** (no game, no visiting), so the mod can write markers +
scan flag to remote planets under the `+0x54` key (the write is already proven to
persist + complete survey % in-game).

---

## 0. TL;DR VERDICT

| Kingdom | Marker | Pure-ESM derivable? | Source |
|---|---|---|---|
| Fauna | **X (temperament)** | **YES — proven 100% on ground truth, 99.4% of 946 PNDT fauna** | NPC_ → OBTS → temperament OMOD → NKEY → FLST `0x00160C97` |
| Fauna | AnyResource `0x0023E90D` | YES (constant) | FLST unconditional entry |
| Fauna | AnyBiomes `0x002634BE` | YES (constant) | FLST unconditional entry |
| Fauna | ActorHealth `0x002634C2` | YES (constant) | FLST unconditional entry |
| Flora | AnyResource `0x0023E90D` | YES (constant) | FLST `0x00160C96` unconditional entry |
| Flora | AnyBiomes `0x002634BE` | YES (constant) | FLST `0x00160C96` unconditional entry |
| Flora | **Reproduction `PlantReproduction0N`** | **YES — ref-free per-species** | FLOR `PRPS` AVIF `0x0023E905` value N |
| Flora | **Genetics `Genetics_*`** (28 variants) | **PARTIAL** — only if OBTS-OMOD authored; planet-trait-gated cases are per-(species,planet) | FLOR OBTS → resource/DNA OMOD → NKEY → FLST (parallel to fauna), else runtime |
| Flora | Resource specificity (Nutrient/Fiber vs Toxin/Metabolic split) | **PARTIAL** — same OBTS-OMOD path; some gated on planet traits | FLOR OBTS resource OMOD, else runtime |

**Bottom line:** Fauna is **fully ref-free** (Map 4's empirical proof overrides the
earlier "materialization-bound" verdict — see §6.1). Flora is **mostly ref-free**:
the 4-marker skeleton + correct per-species reproduction is derivable now; genetics
and the resource blue/green split are derivable for OBTS-authored flora and otherwise
fall back to a documented approximation. Full universal correctness is blocked only on
the small set of **planet-trait-conditioned** flora markers (genuinely per-(species,planet),
not encodable in a per-species table).

---

## 1. THE COMPLETE RECIPE (given a species form id → its slot+0x08 set)

### Common ESM machinery (already in `EsmReader.cpp`)
- Record header 24 B: `sig[4] | dataSize u32@4 | flags u32@8 | formid u32@12 | (8 more)`.
- Compressed flag `0x00040000` → data = `u32 decompSize` + zlib stream (use existing
  `uncompress` + `kMaxDecompSize` guard).
- Top groups: `GRUP` 24 B header, `gsize@4`, `label@8`, `gtype@12`; `gtype==0` ⇒ label
  is the record sig of the group. Marker KYWDs live in the `KYWD` group; FLSTs in `FLST`;
  OMODs in `OMOD`; NPC_ in `NPC_`; FLOR in `FLOR`.
- Subrecords: `sig[4] | size u16@4 | payload`; `XXXX` overrides next size as u32
  (reuse existing `ForEachSubrecord`).

### One-time init parse (build the lookup tables once, cached like `g_map`)
1. **FLST `0x00160C97`** (HandScannerActorKeywords) → `KW2X_fauna` map.
2. **FLST `0x00160C96`** (HandScannerPlantKeywords) → flora condition table.
3. **OMOD group**: for every OMOD whose EDID starts `mod_CCT_Temperament_`, record
   `omodFormId → NKEY keyword id` ⇒ `TEMP_OMOD` set/map. (Same pass also captures
   `mod_CCT_Resource_*` and any DNA/genetics OMODs for the flora extension.)

### FAUNA (NPC_) — fully ref-free, 4 markers `[X, 0x0023E90D, 0x002634BE, 0x002634C2]`
Per NPC_ form id:
1. Read its **OBTS** subrecord. Layout (verified on ground truth):
   `off0 u32 entryCount` ; `off4..0x11` fixed 18-byte prefix (`8×00, FF FF 01 00, 00 00`) ;
   `off0x12` then `entryCount × 7-byte entries` = `u32 OMOD formid` + `3 bytes 00 01 01`.
   Validate `entryCount*7 == len-0x12`; skip/degrade otherwise.
2. For each entry OMOD formid, test membership in `TEMP_OMOD`. Exactly one matches
   (1022/1276 NPC_ have exactly one; 0 ambiguous).
3. That OMOD's NKEY keyword → `X = KW2X_fauna[keyword]`.
4. Emit `[X, 0x0023E90D, 0x002634BE, 0x002634C2]`.
5. If no temperament OMOD (6 Lvl bosses: Terrormorph, Siren, 2× Grylloba, 2× Cataxi),
   emit `[0x0023E90D, 0x002634BE, 0x002634C2]` (X=0) — leaves those blue, acceptable.

### FLORA (FLOR) — skeleton + reproduction ref-free now; genetics/resource via OBTS-OMOD
Per FLOR form id:
1. **Always emit** (FLST `0x00160C96` INAM-free / unconditional entries):
   `0x0023E90D HandScannerAnyResource`, `0x002634BE HandScannerAnyBiomes`.
2. **Reproduction (ref-free, per-species):** read FLOR **PRPS** triple
   `(u32 AVIF, f32 value, u32)`; the AVIF == `0x0023E905 HandScannerPlantReproduction`,
   value N∈1..7 → `PlantReproduction0N` keyword (the 7 ids sorted by EDID suffix;
   observed: N=6→`0x00171867`, the 5th-marker `0x00171869` is `PlantReproduction04`).
3. **Genetics + resource split (OBTS-OMOD path, mirrors fauna — see §6.4):** if the
   FLOR carries an **OBTS** with a `mod_CCT_Resource_*` / DNA-genetics OMOD whose NKEY
   keyword maps through FLST `0x00160C96` func-560 conditions, emit that
   `Genetics_*` / resource marker. Otherwise fall back to
   `0x0023E90C HandScannerPlantGenetics_Common_Standard_Carbon` (the Common case).
4. **Biome-count 5th marker:** existing `GetSpeciesBiomeCount` ≥ 3 ⇒ also emit
   `0x00171869` (already wired; keep).

---

## 2. ATTRIBUTE → MARKER TABLES / RULES

### 2a. The catalog source (the single WRITE source the brief asked for)
Two FLST records hold inline CTDA membership conditions; markers are KYWD form-ids:
- **FLST `0x00160C96` "HandScannerPlantKeywords"** → FLORA catalog (39 LNAM markers, 37 conditioned).
- **FLST `0x00160C97` "HandScannerActorKeywords"** → FAUNA catalog (16 LNAM markers, 15 conditioned).

FLST layout:
```
EDID (cstr)
LNAM (u32 form-id) × N            -- marker keyword ids, catalog order
{ INAM (u32 = index into LNAM) ; CITC (u32 condCount) ; CTDA(32B) × CITC } per conditioned marker
ANAM (u32 = 0)                    -- terminator
```
CTDA 32B: `+0x00 u8 op/flags | +0x04 f32 comp | +0x08 u16 funcIndex | +0x0C u32 param1 | … | +0x1C u32 runOn`.
- **func 560 = HasKeyword(subject, param1=KYWD)** — the fauna-X selector and the flora
  content-keyword tests. **This is the membership leaf we replicate.**
- func 837 = EvaluateConditionForm(param1=CNDF) — recurse into a named CNDF (OR of sub-keywords).
- func 858 = HasKeyword(param1=PLANET-TRAIT KYWD) — **planet context, not species** (the per-planet split).
- func 448 = HasPerk (player Botany/Zoology) — gates *display* of NonLethalHarvest markers only; ignore for green.
- INAM-free markers (flora Resource `0x0023E90D`, Biomes `0x002634BE`) = **unconditional** = always emit.

### 2b. Fauna temperament map `KW2X_fauna` (from FLST `0x00160C97` func-560, NOT by name)
FLST `0x00160C97` LNAM[0..6] temperament markers:
`0x002634AF, 0x00280172, 0x00280173, 0x002634AE, 0x001699B2, 0x00280178, 0x002634AD`.
Build `{param1_keyword → LNAM[INAM]}` over func-560 entries. Resulting CCT-keyword → X:

| CCT temperament keyword (granted by OMOD NKEY) | X marker | name |
|---|---|---|
| `0x001699AB` Aggressive | `0x002634AF` | Temperament_Aggressive |
| `0x00280174` Wary | `0x00280172` | Temperament_Wary |
| `0x00280175` Foolhardy | `0x00280173` | Temperament_Fearless |
| `0x00169995` AlwaysFlee | `0x002634AE` | Temperament_Skittish |
| `0x001699A3` Territorial | `0x001699B2` | Temperament_Territorial |
| `0x00280177` Defensive | `0x00280178` | Temperament_Defensive |
| `0x001699A1` CuriousPeaceful | `0x002634AD` | Temperament_Peaceful |

Names are **not** 1:1 (Foolhardy→Fearless, AlwaysFlee→Skittish, CuriousPeaceful→Peaceful) —
**use the FLST func-560 map, never name-matching.** Distribution sane (Wary 313, Peaceful
235, Defensive 173, Territorial 104, Aggressive 74, Fearless 69, Skittish 54).

### 2c. Fauna constants (all NPC_)
`AnyResource 0x0023E90D`, `AnyBiomes 0x002634BE`, `ActorHealth 0x002634C2` (the fauna
"reproduction" slot is actually Health). Full fauna set = `[X, 0x0023E90D, 0x002634BE, 0x002634C2]`.

### 2d. Flora reproduction map (AVIF value → keyword) — ref-free per-species
FLOR **PRPS** = `(u32 AVIF=0x0023E905, f32 N, u32)`, stride 12. N∈1..7 → `PlantReproduction0N`.
The N→keyword enum lives in the FLST CTDA, **not** in the AVIF (`0x0023E905` NLDT is a
description string only). Resolve the 7 `HandScannerPlantReproduction0N` KYWD ids by EDID
suffix at init. Confirmed ids: N=6→`0x00171867`. (Cross-check N→id against a
`DumpSpeciesSlots` flora sample before shipping — see §6.3.)

### 2e. Marker KYWD raw image (for the writer; the marker id used at slot+0x08 IS the KYWD form-id)
```
KYWD 0x0023E90D HandScannerAnyResource (len 69):
  EDID "HandScannerAnyResource\0" | CNAM 00FFFFFF | TNAM 0x32 | FNAM 0 | FULL lstring 0x8552
KYWD 0x002634C2 HandScannerActorHealth (len 69): EDID "...\0" | CNAM 00FFFFFF | TNAM 0x32 | FNAM 0 | FULL 0x848f
```
KYWDs carry NO condition/membership data — the link is entirely the FLST CTDA. A parallel
DFOB layer names them (`HandScannerAnyResourceDO 0x00237EB2 → 0x0023E90D`) but is not needed
for derivation.

---

## 3. EsmReader EXTENSION + Main.cpp WRITE PLAN

### 3.1 New `EsmReader.h` API
```cpp
namespace Esm {
    // Full slot+0x08 marker set for one species (FLOR or NPC_), ref-free from Starfield.esm.
    // Empty vector if the form is unknown / unparseable. planetId optional: enables the
    // multi-biome 5th-marker (>=3 biomes) and is reserved for future planet-trait handling.
    std::vector<std::uint32_t> GetSpeciesMarkers(std::uint32_t speciesFormId,
                                                 std::uint32_t planetFormId = 0);

    std::uint32_t GetFaunaX(std::uint32_t npcFormId);   // X marker (0 if no temperament OMOD)
}
```

### 3.2 New one-time tables (built in `BuildMap`, same `std::call_once`, same try/catch degrade)
- `g_tempOmod : unordered_map<u32 omodId, u32 cctKeyword>` — from OMOD group, EDID prefix
  `mod_CCT_Temperament_`, NKEY at `find("NKEY")+4`. Filter NKEYs to ids present in `g_kw2x`.
- `g_kw2x : unordered_map<u32 cctKeyword, u32 xMarker>` — from FLST `0x00160C97` func-560 CTDA.
- `g_floraRepro : array<u32,8>` — index N → `PlantReproduction0N` KYWD id (resolve by EDID at init).
- (Flora extension) `g_resOmod`/`g_dnaOmod` + flora FLST `0x00160C96` func-560 table — same pattern.

### 3.3 New parsers (mechanical, reuse `Cursor`/`ForEachSubrecord`, pure file reads → no fault risk)
- **NPC_ OBTS parser**: header validate (`entryCount*7 == len-0x12`), iterate 7-byte entries,
  match against `g_tempOmod`. Add NPC_ group walk to `BuildMap` (mirror `ParsePndtGroup`).
- **OMOD DATA parser**: `find NKEY → u32 at +4`. (Robust to NREA/NAID/NRDS; for the
  PreyCuriousFoolhardy double-NKEY case, filter to keywords in `g_kw2x`.)
- **FLST CTDA parser**: walk LNAM array, then INAM/CITC/CTDA blocks; for func==560 record
  `{param1 → LNAM[INAM]}`.
- **FLOR PRPS parser**: stride-12 triples, AVIF==`0x0023E905` → value N. (FLOR group walk.)

### 3.4 Main.cpp write (replace the `kFloraAttrs` universal-4 hardcode in `TestBuildArray` /
the production green path)
The write mechanism is **unchanged and already proven**:
- Resolve slot via `SpeciesSlotHash(hashmap, &key)` where `key = CanonicalFormId(form)` (fallback raw id).
- Clear `slotAddr+0x08/+0x10/+0x18` (leak old buffer — safe), then
  `Engine::PushSpeciesAttr(slotAddr, id)` for each id.
- Slot is keyed under the same key TestDirectGreen writes `+0x21`; species looked up by the
  `+0x54` form-id (ESM domain), written under the **render id (ID_52188)** — the proven remote key.

New selection logic (replaces live-member read for remote planets; keep live-member as an
optional same-planet validator):
```cpp
std::vector<std::uint32_t> markers = Esm::GetSpeciesMarkers(sf, planetId);
if (markers.empty()) continue;            // unknown form: leave blue
for (auto id : markers) Engine::PushSpeciesAttr(slotAddr, id);
```
`GetSpeciesMarkers` internally branches FLOR vs NPC_ via the form type already known to the
ESM parse (or `form->GetFormType()`), so fauna now greens remotely too — the previous
"fauna → blue (materialization-bound)" fallback is **deleted**.

---

## 4. HONEST FEASIBILITY VERDICT

**Full pure-ESM derivation is ACHIEVABLE for fauna (100% on resolvable ground truth) and
for the flora skeleton + per-species reproduction. It is PARTIAL for flora genetics and the
flora resource blue/green split.**

### What is solid (ship it)
- **Fauna X**: proven in Python against the real `Starfield.esm` — 100% of resolvable
  ground-truth fauna, 99.4% of 946 PNDT fauna, 0 ambiguous. Two indirections (OBTS→OMOD→NKEY)
  + FLST map. Mechanical parse, no engine call, no Ghidra, no fault risk. This is the headline
  result and it **overturns** the earlier "fauna-X materialization-bound" verdict (§6.1).
- **Fauna constants + flora skeleton (Resource, Biomes, Health)**: unconditional FLST entries.
- **Flora reproduction**: genuinely ref-free per-species from FLOR PRPS — fixes the wrong
  universal `reproduction=06` hardcode.

### What is partial / not cleanly per-species (be honest)
- **Flora genetics (28 variants) + resource specificity.** Two failure modes:
  1. **OBTS-authored** flora resolve via the same NPC_-style OBTS→OMOD→NKEY→FLST path
     (`mod_CCT_Resource_*` OMODs confirmed present on fauna OBTS, strongly implying a
     parallel `HandScannerPlant` path) — derivable, **pending the §6.4 confirmation pass**.
  2. **Planet-trait-gated** flora (FLST func **858** = `PlanetWaterQuality04HeavyMetal`,
     `PlanetAtmosphereToxicity01Toxic`, `PlanetTrait09EcologicalConsortium`). The SAME flora
     species yields a DIFFERENT genetics/resource marker on different planets. This is the
     exact in-game "same species splits green/blue" observation. It is **fundamentally
     per-(species,planet)** — a per-species table cannot capture it. It IS still pure-ESM
     computable (the planet's traits are authored on the planet record), but the mod must
     evaluate `(species OBTS keyword set) × (planet trait keywords)` against the FLST CTDA
     tree, not a flat table.

### Best tractable approximation (if §6.4 confirms NO flora OBTS resource path)
Emit `[0x0023E90D, 0x002634BE, Genetics_Common_Standard_Carbon 0x0023E90C, PlantReproduction0N(PRPS)]`
+ the `>=3 biome` 5th marker. This greens flora whose genetics resolves to Common/Standard/Carbon
and the planet imposes no overriding trait; others stay blue. This is strictly better than the
current universal-4 (which wrongly pins reproduction=06 for all flora) because reproduction is
now correct per-species. Genetics-non-Common and toxin/heavy-metal-planet flora remain blue
until the planet-trait evaluator is built.

### The non-existent "Toxin/Metabolic resource marker"
There is **no** Toxin or Metabolic-Agent ResourceMarker KYWD in the file — `0x0023E90D AnyResource`
is the only resource keyword (Map 2, registry scan). The in-game blue-vs-green split the brief
attributed to "a different ResourceMarker" is actually a differing **genetics/reproduction**
marker or a planet-trait gate, **not** a second resource form. Do not hunt for one.

---

## 5. CONCRETE NEXT STEPS (ordered)
1. **Ship fauna now.** Implement OBTS/OMOD/FLST parse + `GetFaunaX`/`GetSpeciesMarkers(NPC_)`;
   replace the fauna-blue fallback. Highest value, fully proven.
2. **Ship flora reproduction fix.** PRPS parse → correct `PlantReproduction0N`; replace the
   `reproduction=06` element of the universal-4.
3. **§6.4 confirmation pass** (Python, one ESM read): do FLOR records carry OBTS with
   `mod_CCT_Resource_*`/DNA OMODs? If yes → flora genetics/resource becomes ref-free; wire it.
4. **(Optional, hardest)** Planet-trait evaluator for func-858 flora markers → full per-(species,planet)
   correctness.
5. **One in-game cross-check**: `DumpSpeciesSlots` on a planet with known PRPS-N flora + known
   temperament fauna, confirm derived sets == dumped `+0x08` arrays before release.

---

## 6. GAPS / UNPROVEN (carry forward)
1. **Catalog-build init fn** (writer of `REL::ID(909810)/909812`) NOT Ghidra-traced. FLST↔catalog
   is by STRUCTURAL match (count@+0x38, marker-forms@+0x40 in LNAM order, conditions@+0x60) +
   naming + Map 4's 100% empirical fauna match. High-confidence inferred; one Ghidra pass on the
   909810 WRITE site closes it. (Academic for the data goal given the empirical match.)
2. **Map conflict RESOLVED: fauna X.** Maps 1 & 2 said "materialization-bound, no pure-ESM read";
   Map 4 PROVED ref-free via OBTS→OMOD→NKEY. Maps 1/2 only tested the flat **KWDA** (correctly
   empty — the keyword is GRANTED by the OMOD at instance build, not authored on KWDA) and the
   runtime `form+0x260` walk (fails on un-materialized PlaceAtMe'd forms). Parsing OBTS in the
   FILE sidesteps both. **Map 4's verdict stands.**
3. **func-index→mnemonic** (560/837/858/448) inferred from param record types + behavior, not
   cross-checked against the engine dispatch table (`&ID_896671 + idx*0xb`). func-560=HasKeyword
   is validated by the 100% fauna match, so it is effectively confirmed for the data path.
4. **Flora OBTS resource/genetics path (§6.4)** — INFERRED from fauna OBTS carrying
   `mod_CCT_Resource_Nutrient/Toxin/MetabolicAgent_` OMODs; not yet confirmed that FLOR records
   carry the analogous OMODs. This is THE open item that decides flora genetics feasibility.
5. **OBTS header robustness** — 18-byte prefix verified on ground truth; defensive parser must
   validate `entryCount*7 == len-0x12` and skip otherwise. OMOD INCLUDE/sub-OMOD indirection
   unchecked (940/946 procedural fauna are direct; the 6 Lvl bosses may use leveled/template).
6. **PlantReproduction N→id** inferred from naming + AVIF description; confirm against a
   `DumpSpeciesSlots` flora sample before shipping the reproduction fix.

Reusable Python parsers (proven against the real ESM at
`E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm`):
`re/tools/esm_*.py`, `C:\Users\patri\AppData\Local\Temp\esmwork\{esmlib,verify,coverage}.py`.
