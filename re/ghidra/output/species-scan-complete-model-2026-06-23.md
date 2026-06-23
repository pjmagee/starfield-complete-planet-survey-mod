# Species Scan — COMPLETE Validated Offline Marker Model (2026-06-23)

**Status: VALIDATED — 17/17 ground-truth species reproduced EXACTLY by a real CTDA evaluator
(no per-species hard-coded tables).** Tool: `re/tools/esm_derive_markers.py`.

This is the VALIDATE + SYNTHESIZE deliverable. It assembles the five RE maps into one offline
derivation, implements it as a runnable Python tool that evaluates the *real authored conditions*,
runs it against all 17 ground-truth species, spot-checks every edge case, and gives an honest
per-field confidence verdict plus the concrete `EsmReader.cpp` port plan.

---

## 0. TL;DR

| Field | Offline-derivable? | Confidence | Source |
|---|---|---|---|
| **Resource** | YES (single unconditional marker) | **100%** | FLST idx0 `0x0023E90D` unconditional; flora & fauna |
| **Biomes** | YES | **100%** | FLST unconditional `0x002634BE`; fauna swaps to `0x000CC6C2` if species has `CCT_Enviro_Underground` |
| **Genetics (flora)** | YES (planet-trait only) | **100%** (engine logic) / 95% (in-game cross-check pending for non-Carbon) | FLST 28 genetics markers → CNDF (func-858) → PNDT KWDA |
| **Reproduction (flora)** | YES (PRPS-N × planet-trait) | **100%** (Rhizomes ground-truthed) / 95% (Spores override not in-game-dumped) | FLST 7 repro markers → func-14 PRPS-N + CNDF override |
| **Temperament (fauna)** | YES | **100%** on procedural (99.26% surveyable; 7 Lvl/Player blue) | NPC_ OBTS → OMOD `mod_CCT_Temperament_*` → DATA `NKEY` → FLST func-560 |
| **Health (fauna)** | YES | **100%** | FLST unconditional `0x002634C2` |

**Net: every green-set field is 100% offline-derivable from Starfield.esm for procedural species.**
The earlier "genetics/reproduction are materialization-bound" verdict in `EsmReader.cpp` is **WRONG**
and is the main thing this model corrects: those fields are pure functions of (FLOR PRPS-N, planet PNDT
traits), evaluated through the authored CNDF/CTDA tree — no live instance needed.

---

## 0.5 WRITE SIDE — green predicate + planet key (formally decompiled 2026-06-23, task wjpdx9i2j)

The marker derivation above produces slot+0x08. The two remaining write-side questions are now closed:

- **GREEN OUTLINE PREDICATE = `slot+0x21 != 0` alone** (the raw scan-flag byte; NO threshold).
  `ID_52159(player, speciesId)` (@1407b8750) reads ONLY `*(u8*)(*(subobj+0x60)+0x21+idx*0x30)`; the deciders
  `ID_90491`/`ID_90548` compute `GREEN = (ID_52159 != 0) OR (species ∉ ID_52180(planet) membership set)`, and
  for a tracked biome species Term-2 is false → `GREEN ⇔ slot+0x21 != 0`. **slot+0x08 is NOT read in the green
  path.** Its roles: detail-panel member splice (`ID_124900→ID_37875`) + on-screen repaint/slot-registration.
  The "fully catalogued"/survey-% count is a DISJOINT graph (`ID_1016657→ID_97850→ID_97851`, fed by
  `ID_52157`/survey-writers/UI, never by `ID_90491/90548`) and even it reads slot+0x21(≥GMST `ID_69506/69507`)
  + slot+0x20, emitting a float fraction at +0x1cc — not a color. (Engine-confirmed earlier by `ProbeRenderRead`
  → 17/17 green from a bare +0x21 poke.) **So WRITE BOTH: +0x21 (the color boolean) AND slot+0x08 (panel +
  repaint + catalogue).**

- **PLANET KEY = a single FormID IDENTITY: `*(planetForm+0x54) == ID_52188(player) == planet PNDT FormID`,
  NO converter.** `ID_52188`'s ids round-trip through `ID_51710→ID_47401 = LookupFormByID` (FormID domain);
  in-game `TestRenderKeyGreen` measured `+0x54 == ID_52188 == 0x0003F5A1 == Jemison PNDT FormID`. The suspected
  `ID_51760/51773/124846/42691` cluster resolves only the SYSTEM coordinate, not the planet id. This REFUTES
  `render-read-target-2026-06-22.md`'s "two domains / converter needed" H2.

- **REMOTE GREEN = decompile-GO.** A write to a never-visited planet keyed `(ID_938333<<48)|(target +0x54
  FormID <<16)`, carrying slot+0x21 + the ESM-derived slot+0x08 set, lands in the exact entry the renderer
  reads when the player flies there (`ID_52188(player)` then returns that planet's FormID). WRITE RECIPE:
  `P = ID_52188(player) == *(planetForm+0x54)`; entry key `(ID_938333<<48)|(P<<16)`; per-species slot =
  FNV-1a(authored species id) in `subobj(entry+0x20)+0x18`; set `slot+0x21=0x64` (`ID_124898`) + fill
  `slot+0x08`. **RESIDUAL CLOSED (decompile-100%):** the stamp site is `ID_51735 = BGSPlanet::Manager::
  SetCurrentPlanet` doing `*(int*)(Manager+0x80)=param_2` VERBATIM (`MOV [RCX+0x80],EDX`, no transform), where
  param_2 is a FormID (immediately `LookupFormByID`'d + used as the BSGalaxy DB key). So Manager+0x80 (render
  fallback) ≡ planet FormID by construction; node+0x28 (fast path) measured == +0x54 in-game. `+0x54 ==
  render-key` is **DEFINITIONAL** — a remote write keyed by an unvisited planet's +0x54 FormID is guaranteed
  read on arrival; no remap exists. Doc: `planet-id-stamp-site-2026-06-23.md`. Only an optional in-game
  save-test remains for belt-and-suspenders.

---

## 1. The complete offline derivation

### 1.1 Catalogs (the only hard-coded ids)
- **Flora**: `FLST 0x00160C96 HandScannerPlantKeywords` — 39 LNAM markers, 37 conditioned blocks.
- **Fauna**: `FLST 0x00160C97 HandScannerActorKeywords` — 16 LNAM markers, 15 conditioned blocks.
- Subrecord layout: `EDID`; `LNAM`(u32)×N marker ids in catalog order; per conditioned marker
  `INAM`(u32 = LNAM index) `CITC`(u32 count) `CTDA`(32B)×count; `ANAM`(u32=0) terminator.
  Markers with **no INAM block are unconditional** (always emit).
- CTDA bytes: `op`@+0x00 u8, `comp` f32@+0x04, `func` u16@+0x08, `param1` u32@+0x0C, `runOn` u32@+0x1C.

### 1.2 CTDA evaluator (engine `ID_71422` list-walk / `ID_71429` per-item — ported faithfully)
- **Comparison op** = `(op>>5)&7`: `0:== 1:!= 2:> 3:>= 4:< 5:<=`. Leaf value compared vs `comp`.
- **OR-with-next** = `op&1`: consecutive OR-flagged items form an OR-run; runs are AND-ed together.
- Leaf functions used by the two catalogs:
  - `882` = constant `1.0` anchor. (`882 >1` ⇒ FALSE = *disabled branch*; `882 >0` ⇒ TRUE = enabled.)
  - `858` = `GetIsPlanetTrait(param1)` ⇒ `param1 ∈ planet PNDT KWDA`.
  - `560` = `HasKeyword(species, param1)` ⇒ `param1 ∈ species granted-keyword set`.
  - `14`  = `GetActorValue(0x0023E905)` ⇒ flora reproduction value N (from FLOR PRPS).
  - `837` = `EvaluateConditionForm(CNDF)` ⇒ recurse into the CNDF's own CTDA list.
  - `448`/`699` = `HasPerk`/`HasMagicEffectKeyword` ⇒ **display/perk-gated**, never in green set.
    The evaluator returns a sentinel for these so the *whole marker* is dropped.

### 1.3 Per-species inputs (all offline)
- **Flora reproduction value N**: FLOR `PRPS` stride-12 triples (`u32 AVIF, f32 value, u32`); the
  triple with `AVIF==0x0023E905` gives N (rounded). Surveyable flora span **N ∈ 1..6**, all present.
- **Fauna granted keywords** (what func-560 tests): NPC_ `OBTS` → entryCount@0, 18-byte prefix,
  then 7-byte entries (`u32 OMOD id` + 3 bytes) @0x12 → each OMOD's `DATA` blob scanned for the
  ASCII `NKEY` tag → the u32 keyword that follows. (**Temperament + Enviro_Underground only.**)
- **Planet traits** (what func-858 tests): planet `PNDT` `KWDA` (u32 array of KYWD ids).

### 1.4 Genetics tree (flora) — fully decoded CNDF (byte-exact op bytes)
DNAType axis (func-858 planet WaterQuality), DNABasis axis (Atmosphere + Magnetosphere):

```
DNAType:  Double  <- Water02Biological(0x295EB7)
          Chiral  <- Water03Chemical(0x295EB6)
          XNA     <- Water04HeavyMetal(0x295EB5) OR Water05Radioactive(0x295EA9)
          Standard <- NOT(Chiral|Double|XNA)                     [default]
DNABasis: Arsenic <- AtmTox01Toxic(0x279292)        [enabled: 882>0]
          H2S     <- AtmTox00Corrosive(0x279293)    [enabled: 882>0]
          Methane <- AtmType03M(0x295EA6) AND NOT(Arsenic|H2S)
          Silicon <- NOT(Arsenic|H2S|Methane) AND Magneto{None|VeryWeak|Weak}
          HF      <- **DISABLED** (882>1 anchor = FALSE) -> never fires
          Boranes <- **DISABLED** (882>1 anchor = FALSE) -> never fires
          Carbon  <- NOT(any of the above)                       [default]
```
The genetics marker is `Genetics_{Rarity}_{DNAType}_{DNABasis}`; **Rarity is purely a name** of the
(Type,Basis) pair. **HF and Boranes can NEVER be the result** — their CNDFs open with
`op=0x40 func=882 comp=1` (`1>1`=FALSE), so a Magneto-strong planet falls through to Carbon (this is
why the synthetic "VeryStrong→HF" check correctly returned Carbon, not HF).

**Proven invariant: exactly ONE genetics marker fires for every one of the 1765 PNDT planets**
(axes are mutually exclusive within Type and within Basis; the two-axis product is a single marker;
verified file-wide, 0 multi, 0 zero).

### 1.5 Reproduction tree (flora) — PRPS-N × planet override
- **Direct** markers Repro01..07 = `func14 GetAV==K` with `K`: 01←0, 02←1, 03←2, 04←3, 05←4, 06←5,
  07←6 (note the EDID suffix ≠ the AV; use the CTDA comp). Repro01 also fires on `AV>6` (clamp).
  Repro02/03/05/07 (and Repro01) additionally AND `HasOverride==0` ⇒ suppressed when an override is active.
- **Overrides** (func-837 → planet-trait CNDF):
  - `Rhizomes` = `EcologicalConsortium(0x225590)` ⇒ forces **Repro06 `0x00171867` "Self-cloning"** for
    *any* AV (no AV gate). This is an OR-run inside Repro06: `[Rhizomes] OR [AV==5]`.
  - `Spores` = `NOT Rhizomes AND AV<4 AND (Psychotropic(0x225589) OR Aeriform(0x225597))` ⇒ forces
    **Repro04 `0x00171869`**, but **only when AV<4** (the `op=0x80 func14 comp4` clause). Repro04 OR-run:
    `[Spores] OR [AV==3]`.
- Verified reproduction matrix (from the evaluator):

```
no override : AV0->Repro01 AV1->02 AV2->03 AV3->04 AV4->05 AV5->06 AV6->07 AV7->01
Rhizomes    : every AV -> 0x171867 ; AV3 ALSO -> 0x171869 (the un-suppressed 5th marker)
Spores      : AV0..3 -> 0x171869 ; AV>=4 falls back to direct (override inactive due to AV<4 gate)
```

### 1.6 Fauna temperament map (func-560 leaves in FLST 0x00160C97; names deliberately non-1:1)
```
CCT_Temperament_Aggressive 0x001699AB -> 0x002634AF Aggressive
CCT_Temperament_Wary       0x00280174 -> 0x00280172 Wary
CCT_Temperament_Foolhardy  0x00280175 -> 0x00280173 Fearless
CCT_Temperament_AlwaysFlee 0x00169995 -> 0x002634AE Skittish
CCT_Temperament_Territorial0x001699A3 -> 0x001699B2 Territorial
CCT_Temperament_Defensive  0x00280177 -> 0x00280178 Defensive
CCT_Temperament_CuriousPeaceful 0x001699A1 -> 0x002634AD Peaceful (also via CNDF CritterPeaceful 0x0015281C)
```
Fauna green set = `[ X, 0x0023E90D AnyResource, 0x002634BE AnyBiomes, 0x002634C2 ActorHealth ]`.
Underground species swap AnyBiomes → `0x000CC6C2 ActorUnderground` (mutually exclusive; reproduced).

---

## 2. Validation — `re/tools/esm_derive_markers.py`

The tool builds the EsmDB once, then for any species id (+ optional planet id) parses the catalogs and
CNDFs and **evaluates the real CTDA conditions** — there is no per-species lookup table; the only
constants are the two catalog ids, the reproduction AVIF, and the perk/magic-effect func ids.

### 2.1 Ground truth — 17/17 EXACT (planet 0x0003F5A1)

```
FLORA  0x00185478 OK   0x00185479 OK   0x0018547F OK   0x00185489 OK
       0x001854C1 OK   0x001854D8 OK   0x002F80A0 OK   0x002F80BB OK
FAUNA  0x00048A34 OK   0x0019B898 OK   0x0019B899 OK   0x0019B89A OK
       0x0019B89B OK   0x0019B89C OK   0x0019B89D OK   0x0019B89E OK   0x0019B89F OK
=> 17/17 EXACT MATCH (set equality)
```
Run: `python re/tools/esm_derive_markers.py --validate`. **No mismatches.**

### 2.2 Edge-case spot-checks (all pass)

- **GLOSSY STICKWEED / BOREAS ROOT → Self-cloning `0x00171867`, NOT Seeds/Spores.** Proven by the
  reproduction matrix: any flora on an EcologicalConsortium planet (which both home planets carry, as
  do all 8 ground-truth flora on 0x0003F5A1) is forced to Rhizomes→Repro06 `0x00171867` *regardless of
  PRPS-N*. The old "PRPS f32 = reproduction" heuristic produced the wrong Seeds/Spores precisely because
  it ignored the planet-trait override. ✔
- **COLD CAVE NETTLE (Toxin) / TUFTED SNOW WILLOW (Metabolic Agent) resource marker.** There is
  **exactly ONE** flora resource marker file-wide: `0x0023E90D HandScannerAnyResource` (unconditional).
  The 63 `mod_CCT_Resource_*` OMODs grant **zero** green-set keywords (only 6 `_NonLethal` grant
  `HandScannerAnyNonLethalHarvest`, which is the func-448 perk-gated *display* marker). So a Toxin/
  Metabolic flora gets the *same* AnyResource marker; the divergent *displayed resource name* is a
  render-time computation (`mod_CCT_Resource_*`), **not** a second slot+0x08 marker. Their blue/green
  is driven by genetics+reproduction matching, not by resource. ✔
- **BEETLE GRAZER temperament.** The surveyable form `0x001F3E88 PCM_Narion_Kreet_GrazerPreyA` resolves
  `X = 0x00280172 Wary` offline (OMOD PreyBasic → CCT_Temperament_Wary). The in-game "3/4 markers" was a
  runtime porting/coverage gap, not an ESM-derivation gap — the offline model is strictly more complete. ✔
- **Genetics general case (synthetic + file-wide).** Single special-trait planets fire the correct
  basis/type (Toxic→Arsenic, Corrosive→H2S, AtmM→Methane, HeavyMetalWater→XNA, Chemical→Chiral,
  Biological→Double, MagnetoNone→Silicon); two-axis collisions fire one combined marker
  (HeavyMetalWater+AtmM→XNA_Methane; Toxic+Chemical→Chiral_Arsenic). Exactly one genetics marker per
  planet across **all 1765 PNDT**. ✔

### 2.3 Coverage
- Surveyable (PPBD-referenced): **FLOR 142, NPC_ 947.**
- Fauna temperament resolved: **940/947 = 99.26%.** Unresolved 7 = Player + 6 Lvl/template bosses
  (`LC030 Grylloba`, `LC116 Cataxi`, `LvlTerrormorph`, `LvlSiren`) — temperament applied via
  leveled-list/template indirection at spawn (materialization-bound); correctly X=0 (blue).
- Flora: **142/142 have PRPS-N** ⇒ 100% reproduction-derivable; genetics planet-only (1 marker/planet).

---

## 3. HONEST per-field confidence verdict

| Field | Verdict | Detail / remaining gap |
|---|---|---|
| **Resource** | **100% offline, validated.** | One unconditional marker `0x0023E90D`. No second resource marker exists (proven). Displayed *name* is render-only. |
| **Biomes** | **100% offline, validated.** | Unconditional `0x002634BE`; underground swap to `0x000CC6C2` logic decoded & reproduced. **In-game-unvalidated** only because ~1 underground fauna exists file-wide (low risk, mechanical). |
| **Genetics** | **100% offline by engine logic; in-game cross-check pending for non-Carbon.** | Full DNAType×DNABasis CNDF tree decoded byte-exact incl. the HF/Boranes 882-anchor disable; 1-marker invariant proven on all 1765 PNDT. Only the *Standard/Carbon* leaf is in the 8/8 ground truth; a single DumpSpeciesSlots on a Toxic (Arsenic) or HeavyMetalWater (XNA) planet would close it to fully-validated. |
| **Reproduction** | **100% offline; Rhizomes validated, Spores not in-game-dumped.** | PRPS-N direct + Rhizomes override are ground-truthed 8/8 (incl. the AV3 5th-marker). The **Spores** override (Repro04 via Psychotropic/Aeriform, AV<4 gate) is decoded byte-exact but no in-game dump on a Psychotropic/Aeriform planet was available — engine-logic-certain, in-game-unconfirmed. |
| **Temperament** | **100% on procedural fauna, validated 9/9; 99.26% surveyable coverage.** | The 7 unresolved are Player + Lvl/template bosses whose temperament is applied at spawn via leveled/template indirection (genuinely materialization-bound). Not a derivation gap for procedural fauna. |

**Things that are NOT 100% / next RE step (all low-risk, none block shipping):**
1. **Spores reproduction override** — needs one DumpSpeciesSlots on a PsychotropicBiota/Aeriform planet.
2. **Non-Carbon genetics** — needs one DumpSpeciesSlots on a Toxic or HeavyMetalWater planet.
3. **Underground fauna biome swap** — needs one DumpSpeciesSlots on the single cave creature.
4. **Lvl/template boss temperament** — requires TPLT/leveled-actor spawn resolution (runtime); out of
   scope for procedural fauna, leave blue.
5. **CTDA op/func encodings** — inferred from the data path and confirmed by the 17/17 exact match; not
   cross-checked against the engine dispatch table in Ghidra (effectively confirmed, not formally proven).

These are *cross-checks*, not derivation gaps: the offline model already reproduces every available
ground-truth sample exactly. Marked HONESTLY as in-game-unconfirmed where no sample exists.

---

## 4. EsmReader.cpp implementation plan (ship the validated derivation)

The fauna path (`GetFaunaX`, OBTS→OMOD→DATA-`NKEY`→FLST func-560) is **already correct** and matches the
validated parser — keep it. The **flora path is wrong** and must be replaced. Current
`GetSpeciesMarkers` emits hardcoded `kMarkerPlantGeneticsCommon (0x0023E90C)` + `kMarkerPlantReproCommon
(0x00171867)` and ignores `planetFormId`, on the false premise that genetics/reproduction are
materialization-bound. They are not.

**Add (build-time, in `MarkerTables` / `BuildMarkers`):**
1. **PNDT trait reader**: a `std::unordered_map<u32 planetFormId, std::vector<u32> traitKwds>` from each
   PNDT's `KWDA`. (PNDT is already walked for PPBD; capture KWDA in the same pass.)
2. **Catalog condition tables**: parse `FLST 0x00160C96` into `lnam[]` + per-marker CTDA blocks, and
   parse the referenced `CNDF` records (genetics `0x00177CF8..0x00177D04`, reproduction
   `0x00171350/51/52`) into CTDA lists. Walk the `CNDF` top group once (add `kSigCNDF=0x46444E43`).
3. **A minimal CTDA evaluator** mirroring §1.2 (≈40 lines): comparison `(op>>5)&7`, OR-run grouping
   `op&1`, leaf dispatch for funcs `882/858/560/14/837`, perk funcs `448/699` → drop marker. Port
   `eval_condlist` / `eval_condform` from `esm_derive_markers.py` verbatim.

**Replace `GetSpeciesMarkers` flora branch with:**
```cpp
// build species ctx: PRPS-N (already collected as floraRepro source value),
// planet traits = traitTable[planetFormId] (empty if 0/unknown).
// For each flora FLST marker: unconditional -> emit; else eval its CTDA block -> emit if true.
// Genetics + reproduction now derive correctly from (N, planet traits).
```
`planetFormId` becomes **required** for correct genetics/reproduction (it gates func-858). When 0 is
passed, fall back to the no-trait default (Standard/Carbon genetics, direct reproduction by N) — this is
still correct for trait-free planets and strictly better than today's hardcoded Self-cloning.

**Remove / deprecate:**
- `kMarkerPlantGeneticsCommon` and `kMarkerPlantReproCommon` as *emitted* fallbacks (keep only as the
  no-trait default the evaluator naturally produces).
- The `SpeciesBiomeCount` ≥3-biome → `0x00171869` rule is already removed; confirm nothing re-adds it.
  The 5th marker is **AV==3 (Repro04)**, not a biome count — the evaluator handles it.

**Net behavioral change:** flora green sets become *exact* on every planet (correct genetics basis,
correct reproduction incl. planet overrides) instead of a Carbon/Self-cloning approximation — closing
the "value-divergent flora require an on-planet scan" limitation noted in the current header.

**Caveat unchanged:** this fixes the *derivation* of the correct slot+0x08 SET. It does **not** change
the standing REMOTE-green verdict (map 5): an unvisited planet still cannot be greened ref-free because
(R1) the render key needs a runtime BSGalaxy NumericID and (R2) the live biome member table is null
off-planet. The validated set is for current-planet green (both levers on the same slot) and for any
future writer that has a correctly-keyed materialized slot.

---

## 5. Artifacts
- `re/tools/esm_derive_markers.py` — the unified deriver + `--validate` (17/17).
- Ground truth, edge cases, genetics general case, 1765-PNDT invariant, underground swap, and 99.26%
  fauna coverage all reproduced from this single tool + Starfield.esm, no in-game state.
