# ESM Trait Scan-Target Authoring — Raw Findings (2026-06-23)

Source: `Starfield.esm` (1,457,098,709 bytes). Pure offline binary parse.
Tools (all in `re/tools/`): `esm_trait_scan_authoring.py` (index pass),
`esm_trait_master.py`, `esm_lcsr_count.py`, `esm_tasks_345.py`,
`esm_overlay_link.py`, `esm_acti_and_summary.py`, `esm_verify_refr.py`.
Index: 3,829,246 records, 51,892 KWDA-bearing, 9,369 FLST/COBJ/GBFM/QUST, 34,061 placement bases.

---

## HEADLINE ANSWERS

- **(a) Traits with a scan-target: 27 distinct numbered traits (NN 00–26)**, realized as
  **31 scan-target ACTIs** (NN 00/07/17/22 each have `01`+`02` sub-variants).
- **(b) The "Required" count M = the number of LCSR entries tagged with LocRefType
  `PlanetTraitScanTargetLocRef` (0x0027A567) inside the specific `OverlayTrait…Location`
  LCTN that got placed on the planet.** It is authored in the **LCTN's LCSR** subrecord, NOT
  in the QUST/GLOB/AVIF. For trait 20, all three overlay LCTNs contain **exactly 2** such
  entries → M = 2 (matches in-game "1/2 SCANNED"). M is **per-overlay-instance, not a fixed
  per-trait constant** (e.g. AeriformLife overlays carry 1, 1, 5).
- **(c) Derivation recipe:** PNDT.KWDA → PlanetTrait* keywords (the traits). For each trait
  keyword, reverse-KWDA → OverlayTrait*Location LCTNs → count LocRef=0x0027A567 entries in
  each LCTN's LCSR = candidate M values. (Which overlay variant actually lands is a runtime
  biome/overlay roll; offline you get the per-variant M set, not the single chosen instance.)
- **(d) Placements: the prompt's premise is REFUTED.** Every scan-target ACTI base IS
  statically placed by REFRs (6× for ACTI 20). Those REFRs live in the overlay LCTN's host
  cells and are referenced by the LCTN's LocRef LCSR entries. They are authored static refs;
  the *overlay cell* is what the biome system instantiates onto a planet surface at runtime.

---

## TASK 1 — FULL TRAIT TABLE (27 numbered traits / 31 scan ACTIs)

All trait KYWDs carry **TNAM = 44 (0x2C)** (except aux `_Close`/`_Possible` = TNAM 0).
PRPS/FTYP/KWDA pattern is identical on every scan ACTI (verified on ACTI 20, see Task-1 detail).

| NN | Name | scan-target ACTI | trait KYWD | HandScannerTraitInfo KYWD | *Name* ACTI | MB_SurveyTrait QUST |
|----|------|------------------|-----------|---------------------------|-------------|---------------------|
| 00 | AeriformLife01 | 0x0021B297 | 0x00225597 | 0x0021B294 | 0x0021B296 | 0x0021C203 |
| 00 | AeriformLife02 | 0x001AC573 | 0x00225597 | 0x001AC56C | 0x001AC582 | 0x0021C203 |
| 01 | AmphibiousFoothold | 0x0021B288 | 0x00225596 | — | 0x0021B28A | 0x0021FD37 |
| 02 | BoiledSeas01 | 0x00245AC2 | 0x0029081C | 0x00249200 | 0x0026D991 | 0x0021C202 |
| 03 | BolideBombardment | 0x0021B286 | 0x00225595 | — | 0x0021B284 | 0x00195408 |
| 04 | CharredEcosystem | 0x0021B282 | 0x00225594 | — | 0x0021B280 | 0x00195407 |
| 05 | ContinualConductor | 0x0021B27C | 0x00225593 | — | 0x0021B27E | 0x00195406 |
| 06 | CorallineLandmass | 0x0021B278 | 0x00225592 | — | 0x0021B27A | 0x00195405 |
| 07 | CrystallineCrust01 | 0x0023CAD0 | 0x00246C66 | 0x0023563C | 0x0023CACD | 0x00195404 |
| 07 | CrystallineCrust02 | 0x00239D8C | 0x00246C66 | 0x003D1161 | 0x0023CACC | 0x00195404 |
| 08 | DiseasedBiosphere | 0x0021B274 | 0x00225591 | — | 0x0021B276 | 0x00195403 |
| 09 | EcologicalConsortium | 0x0021B272 | 0x00225590 | — | 0x0021B270 | 0x00195402 |
| 10 | EmergingTectonics | 0x0021B26C | 0x0022558F | — | 0x0021B26E | 0x00195401 |
| 11 | EnergeticRifting01 | 0x0023875B | 0x00290819 | 0x0023563A | 0x0026D98F | 0x00195400 |
| 12 | FrozenEcosystem | 0x0021B268 | 0x0022558E | — | 0x0021B26A | 0x001953FF |
| 13 | GaseousFont01 | 0x00238751 | 0x0029081B | 0x0023563B | 0x0026D990 | 0x0021C204 |
| 14 | GlobalGlacialRecession | 0x0021B264 | 0x0022558D | — | 0x0021B266 | 0x001953FE |
| 15 | PeltedFields | 0x0021B260 | 0x0022558C | — | 0x0021B262 | 0x001953FD |
| 16 | PrimedForLife | 0x0021B25C | 0x0022558B | — | 0x0021B25E | 0x001953FC |
| 17 | PrimordialNetwork01 | 0x0023CAD1 | 0x0029081A | 0x0023563E | 0x0023CACF | 0x001953FB |
| 17 | PrimordialNetwork02 | 0x00239D8B | 0x0029081A | 0x0023563D | 0x0023CACE | 0x001953FB |
| 18 | PrismaticPlumes | 0x0021B258 | 0x0022558A | — | 0x0021B25A | 0x001953FA |
| 19 | PsychotropicBiota | 0x0021B254 | 0x00225589 | — | 0x0021B256 | 0x001953F9 |
| 20 | SentientMicrobialColonies | 0x0021B250 | 0x00225588 | — (00 form is the kywd) | 0x0021B252 | 0x001953F8 |
| 21 | SlushySubsurfaceSeas | 0x0021B24C | 0x00225587 | — | 0x0021B24E | 0x001953F7 |
| 22 | SolarStormSeasons01 | 0x0021B248 | 0x00225586 | 0x0021B249 | 0x0021B24A | 0x001953F6 |
| 22 | SolarStormSeasons02 | 0x001677C3 | 0x00225586 | 0x001677C1 | 0x001677C2 | 0x001953F6 |
| 23 | SonorousLithosphere | 0x0021B244 | 0x00225585 | — | 0x0021B246 | 0x001953F5 |
| 24 | TurbulentLithosphere | 0x0021B240 | 0x00225584 | — | 0x0021B242 | 0x001953F4 |
| 25 | ExtinctionEvent01 | 0x0021B23C | 0x00221980 | 0x0021B23D | 0x0021B23E | 0x001953F3 |
| 26 | GravitationalAnomaly01 | 0x00111F5A | 0x00111F62 | 0x00111F5B | 0x00111F5C | — (no MB quest) |

Notes:
- `HandScannerTraitInfo` KYWD only exists for some (multi-variant) traits; the "—" ones don't
  have a separate HSTI keyword (the scan ACTI's KWDA still references the trait's primary HSTI
  where present; trait 20's ACTI KWDA = 0x0021B251 `HandScannerTraitInfo20…01`).
- Aux KYWDs `PlanetTrait26GravitationalAnomaly_Close` (0x0028515F) and `_Possible`
  (0x0007C6A8) exist with TNAM=0; not scan traits.
- Overlay LCTNs per trait listed in Task 2 table.

### Task-1 detail — scan-target ACTI 20 (0x0021B250) subrecords (all known facts confirmed)
```
EDID  PlanetTraitScanTarget20SentientMicrobialColonies
VMAD  planettraitscantargetscript
BFCB  "BGSScannable" … BFCE
KWDA  0x0021B251 HandScannerTraitInfo20SentientMicrobialColonies01,
      0x001CBEA3 Handscanner_AllowScanAtHighlightRange
PRPS  0x0022A2B6 (AVIF HandScannerTarget) value=1.0   (bytes b6a22200 0000803f 00000000)
FTYP  0x0027A567 (LCRT PlanetTraitScanTargetLocRef)
```

---

## TASK 2 — REQUIRED COUNT "M"

**Authored location: LCTN → LCSR → count of entries whose leading LocRefType formid ==
0x0027A567 (PlanetTraitScanTargetLocRef).**

### LCSR entry layout (decoded, stride = 20 bytes, exact)
`[LocRefType formid (4)] [ref formid (4)] [parent/worldspace formid (4)] [gridX i16] [gridY i16] [flags/uk (4) usually 0xFFFFFFFF]`
- Med01 LCSR = 860 B / 20 = 43 entries; Lg01 = 800 B / 20 = 40 entries — both divide exactly.
- Entry whose LocRefType==0x0027A567: `67a52700 | <refFormID> | <worldspace> | gridXY | ffffffff`.

### Trait-20 overlays — PlanetTraitScanTargetLocRef entry counts = **2 each**
| LCTN | EDID | LocRef entries (=M) | the REFRs (NAME base verified) |
|------|------|---------------------|--------------------------------|
| 0x0010A476 | …Med01Location | **2** | 0x00159EB2, 0x00159F10 → base 0x0021B250 ✓ |
| 0x0021D242 | …Med02Location | **2** | 0x0016776F, 0x00167770 → base 0x0021B250 ✓ |
| 0x003807B9 | …Lg01newLocation | **2** | 0x002EA231, 0x002EA0D1 → base 0x0021B250 ✓ |

→ **M = 2 for trait 20**, matching the in-game "1/2 SCANNED" (required 2). Verified via
`esm_verify_refr.py`: all six refs are REFR records whose NAME(base)=0x0021B250.

### Candidates RULED OUT for the count
- **QUST `MB_SurveyTrait20` (0x001953F8):** it is a *mission-board reward* quest. Its only
  numeric fields are reward GLOBs (`MissionBoardSurveyTraitRewardActual20` 0x0019540E,
  `…RewardBase` 0x0021FD33, `…XPRewardActual20` 0x0016AB9D, `GameHour` 0x00000038) and a QOBJ
  index. No scan-count field; aliases are mission targets (TargetPlanetLocation via ALKF=trait
  kywd 0x00225588), not a required count. **Not the source.**
- **No GLOB / AVIF / GMST** holds a per-trait required count.

### Per-trait M is variable (range across each trait's overlay variants)
M ranges per-overlay (the LCSR locref count differs by overlay instance):
```
00 AeriformLife 1,1,5 | 01 Amphibious 1,2,2 | 02 BoiledSeas 1,2,4 | 03 Bolide 1×6
04 Charred 1,1,2,2,2,2 | 05 Continual 1,1,2 | 06 Coralline 2,2,2 | 07 Crystalline 1,1,2
08 Diseased 1,2,3 | 09 Ecological 1,1,3 | 10 Emerging 1,1,1 | 11 Energetic 1,1,2
12 Frozen 1,2,2 | 13 Gaseous 1,2,3 | 14 GlobalGlacial 1,1,1 | 15 Pelted 1,1,4
16 PrimedForLife 1,1,3 | 17 Primordial 1,2,2 | 18 Prismatic 1,1,1 | 19 Psychotropic 1,1,2
20 Sentient 2,2,2 | 21 Slushy 1,2,2 | 22 SolarStorm 2,2,2,2,3,3 | 23 Sonorous 1,2,2
24 Turbulent 1,1,1 | 25 Extinction 2,2,3 | 26 GravAnomaly 1,1,1
```
Across all 93 OverlayTrait LCTNs the locref-count distribution is {0:3, 1:47, 2:33, 3:7, 4:2, 5:1}.
(The 3 zero-count overlays are `…WaterLocation` variants for PeltedField.) So **M is determined
by which overlay LCTN the runtime overlay/biome system instantiated**, not a fixed per-trait
authored constant.

---

## TASK 3 — PNDT → TRAIT LINK (Jemison)

**PNDT.KWDA contains the PlanetTrait* keywords = how a planet "has" a trait.**
`JemisonPlanetData` 0x0003F5A1, KWDA count 16:
```
PlanetAtmosphereType05O2, PlanetFaunaAbundance04Abundant, PlanetFaunaExists,
PlanetFaunaProbability02Possible, PlanetFloraAbundance04Abundant, PlanetFloraProbability02Possible,
PlanetGravity01Terrestrial, PlanetMagnetosphereType04Strong, PlanetPressure02Terrestrial,
PlanetTemperature03Temperate,
>>> 0x00225590 PlanetTrait09EcologicalConsortium
>>> 0x0022558D PlanetTrait14GlobalGlacialRecession
>>> 0x00225588 PlanetTrait20SentientMicrobialColonies
PlanetType07Rock, PlanetWaterAbundance02Terrestrial, PlanetWaterQuality01Safe
```
**Jemison has trait 20 = YES** (0x00225588 present). Trait keyword 0x00225588 is referenced by
**50 PNDT records** total. Filter rule: a planet's traits = PNDT.KWDA entries whose EDID starts
`PlanetTrait` and not `PlanetTraitScanTarget`.

---

## TASK 4 — OVERLAY / SPAWN LINK (the linking record the mod needs)

**The link is the OverlayTrait…Location LCTN itself, joined by the trait keyword in its KWDA.**
Overlay LCTN KWDA (trait 20, all three variants) =
```
LocTypeOverlay (0x002CA99D), LocTypeOE_Keyword (0x001A5468),
LocTypeOE_ThemeNaturalKeyword (0x000C6928),
PlanetTrait20SentientMicrobialColonies (0x00225588)  <-- the trait discriminator,
LocTypeTrait (0x0027D7D3)
```
So: **trait KYWD → (reverse KWDA) → overlay LCTNs → (LCSR LocRef=0x0027A567) → REFRs of the
scan-target ACTI.** No separate FLST/COBJ/GBFM ties trait kywd to scan ACTI.

FLST/COBJ/GBFM/QUST containing BOTH trait20 kywd (0x00225588) AND scan ACTI20 / locref:
- **Only `QUST SQ_Parent` 0x0007092C** — it contains the trait kywd AND the LocRefType
  0x0027A567 (in **VMAD@4173**, i.e. a script property), but **NOT** the scan ACTI formid.
  SQ_Parent is the global parent quest that runs the planet-survey/overlay scripts; it holds the
  LocRefType as a script property (this is the runtime "how many LocRef refs scanned" counter
  owner), confirming the count is driven off the LocRefType, not an authored integer.
- `MB_SurveyTrait20` also references trait20 kywd (alias target) but no locref/ACTI count.
- **No container references the scan ACTI 0x0021B250 at all** (it is only reached via REFR NAME).

---

## TASK 5 — STATIC vs RUNTIME PLACEMENT  (PROMPT PREMISE REFUTED)

The prompt asserted "NO static REFR references the scan-target ACTI base by NAME." **False.**
Every scan-target ACTI base has static REFR placements (NAME→base):
```
ACTI 20 0x0021B250 → 6 static REFRs   (incl. the 6 LocRef-tagged ones across its 3 overlays)
ACTI 04 0x0021B282 → 10 REFRs  | ACTI 02 0x00245AC2 → 9 | ACTI 13 0x00238751 → 8
ACTI 22a 0x0021B248 → 8 | ACTI 25 0x0021B23C → 7 | … (all 31 bases placed, 1–10× each)
```
Plus the 31 *Name*-variant ACTIs are each statically placed too (31 placements).

**Reconciliation:** the placements are authored **static REFRs inside the OverlayTrait…Location
LCTN's host cells**. They are NOT free-standing surface placements on each planet's worldspace —
they live in the overlay "kit" cell. At runtime the biome/overlay system **instantiates the
chosen overlay cell onto the planet surface tile**, which materializes those authored REFRs.
So: refs are *authored* (static within the overlay), but *which* overlay (and thus how many scan
targets / what M) appears on a given planet is a **runtime overlay-placement roll**. Both halves
of the earlier intuition are partly right: the scan-target geometry is authored; the
planet-specific instance is procedural.

---

## OFFLINE DERIVATION RECIPE (for the mod)

Given a planet PNDT formid:
1. Read PNDT.KWDA. Collect keywords whose EDID matches `^PlanetTrait\d+` (exclude
   `PlanetTraitScanTarget*` and the `_Close`/`_Possible` aux). These are the planet's traits.
2. For each trait keyword K (e.g. 0x00225588):
   - Map K → scan-target ACTI by EDID suffix `PlanetTrait<NN><Name>` ⇒
     `PlanetTraitScanTarget<NN><Name>[01/02]` (use the Task-1 table; multi-variant traits have
     1+ ACTIs).
   - Reverse-KWDA lookup: all LCTN whose KWDA contains K = the trait's OverlayTrait…Location set.
   - For each such LCTN, parse LCSR (stride 20) and count entries with LocRefType==0x0027A567.
     That count is the candidate **required M** for that overlay variant.
3. Result per planet: set of (trait K, scan-target ACTI(s), {M per overlay variant}).
   - Caveat: offline you cannot know *which* overlay variant the engine placed for this planet
     instance, so M is a small set (e.g. {2} for trait20, {1,2,2} for AmphibiousFoothold). If the
     mod needs the single live M it must read it at runtime (the SQ_Parent/LocRef scan counter),
     but for trait 20 every variant = 2 so M=2 is unambiguous.

### Key formids for the mod
- LocRefType `PlanetTraitScanTargetLocRef` = **0x0027A567** (FTYP on every scan ACTI; LCSR tag).
- Scan-target PRPS property AVIF `HandScannerTarget` = **0x0022A2B6** (value 1.0).
- Parent survey quest `SQ_Parent` = **0x0007092C** (holds LocRefType as script property).
- Trait keyword TNAM marker value = **44 (0x2C)** for all real scan traits.
