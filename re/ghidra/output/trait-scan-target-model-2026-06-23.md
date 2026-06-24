# Planet Trait Scan-Target model — scan state, green predicate, ref-free verdict (2026-06-23)

Sub-problem: planet TRAIT scan-targets (`PlanetTraitScanTarget<NN><Name>` ACTIs, shown in-game as
e.g. "MICROBIAL COMMUNITY"). In-game, the mod's `CompleteSurvey` marks the trait KNOWN (panel
"TRAITS 3/3") but the surface scan-target renders **BLUE / "1/2 SCANNED"** — its scan state is not set.

Confidence tags: `[decompile-verified]` read in the cited decompile; `[esm-verified]` read in the
real Starfield.esm; `[inferred]` reasoned from verified facts; `[in-game]` user ground truth.

Companion docs: `SCAN-TO-GREEN-KNOWLEDGE-BASE.md` (the species model this builds on),
`esm-trait-scan-target-authoring-2026-06-23.md` (full ESM authoring + 27-trait table).

---

## 0. TL;DR

There are **TWO independent systems**, and `CompleteSurvey` only touches the first:

1. **Trait-KNOWN** (panel "TRAITS N/N"): `ID_52155 SetTraitKnown(planetId, traitKwd, true)` → `ID_52205`
   writes the planet's trait set on the **planet proxy form** (resolved via discriminator
   **ID_937887 "ProxyFormPtr"**, `ID_124903`), and fires `StarMap::PlanetTraitKnownEvent` (`ID_52210`).
   The mod already calls this. `[decompile-verified]`

2. **Scan-target "N/M SCANNED" + green outline**: this is the **per-ref BGSScannable component**
   (discriminator **ID_939118**, keyed by the scan-target REFR's own FormID, scanned byte at
   component **+0x28**) — the SAME container/branch as biome flora. The Monocle scanner outline color
   (`_TargetScanned` blue vs `_TargetFullyScanned` green) is `ID_90548`'s byte `param_1[0xf2c] =
   ID_83007(ref)`. The "N/M" panel number is a UI-model aggregate (`uLocationTraitRefsScanned` /
   `uLocationTraitRefsRequired`) the scanner recomputes each frame by walking the player's Location
   refs tagged with LocRefType `PlanetTraitScanTargetLocRef` (0x0027A567). `[decompile-verified]`

**`CompleteSurvey`'s trait path is MISSING #2 entirely** — it sets trait-KNOWN but never sets the
per-ref scanned state on the placed scan-target REFRs.

**Ref-free off-planet completion of scan-targets is NOT achievable** — strictly worse than the
species green crux. The scan-target REFRs are materialization-bound (they only exist when the
planet's overlay cell is instantiated on the surface, which only happens on the planet you are
physically on), AND the count "M" is determined by *which* overlay variant the runtime placed
(not a fixed authored per-trait constant). The honest analog of the species solution is an
**on-planet** pass: find the loaded scan-target REFRs and `SetScanned(true)` each.

---

## 1. WHAT "1/2 SCANNED" COUNTS — the reader

### 1.1 The UI data model (the panel number)
`ID_90486` (@141597d40) is the **serializer** for the planet-survey / Monocle-scanner UI data
model. It binds named fields to struct offsets `[decompile-verified]` (trait-ref-count.txt:1-31):
```
model+0x28  "aPlanetTraits"               (panel TRAITS list / "3/3")
model+0xa0  "uLocationTraitRefsScanned"   <-- the N in "N/M SCANNED"
model+0xc0  "uLocationTraitRefsRequired"  <-- the M
model+0x260 "fSurveyPercentage"
... uResourcesCurrent/Max, uFloraCurrent/Max, bBiomeFloraComplete, ...
```
So "1/2 SCANNED" = `uLocationTraitRefsScanned=1`, `uLocationTraitRefsRequired=2`. These are
**aggregate counts over a Location**, not per-ref state. The model is built per-frame by the giant
Monocle updater **ID_90518** (@14159b890, reached via `ID_90661`/`ID_90676`; references
`$ScanMapMarker_Unscanned`, `MonocleMenuData`). `[decompile-verified]` (trait-ref-count.txt:35;
trait-menu-owners.txt; trait-ui-xrefs.txt)

These are a *derived* count — the durable per-ref scan state lives in the BGSScannable component (§2).

### 1.2 The green / outline predicate for a scan-target ACTI
The outline-color decider **ID_90548** (@1415a47c0) sets the Monocle outline byte directly from the
per-ref scanned state `[decompile-verified]` (trait-ref-count.txt:2316-2317, 2266-2276, and the
582-line body):
```
param_1[0xf2c] = ID_83007(ref);          // 0=not-scannable, 1=scannable/UNSCANNED(blue),
                                         //                   2=this ref SCANNED (green)
... then it reads the form type tag *(ref+0x2e) ('K','.', '%', ...) to pick the color set.
```
`ID_83007` (@1413076d0) for a scan-target ACTI takes the FLORA branch because the base form's bit
`0x2a` is clear (`ID_36022(base+0xc8,0x2a)==0`): it looks up `(ID_939118<<48) | (refFormID<<16)` in
`db+0x268`, returns `(component[+0x28] != 0) + 1`. `[decompile-verified]` (scanned-state.txt:26-68;
KNOWLEDGE-BASE §6.4). The string set confirms scan-targets have their own outline states:
`aHighlightScannableOutlineColorHigh_TargetScanned:Monocle` vs
`..._TargetFullyScanned:Monocle` (scanned-count-strings.txt:132-152).

**So the scan-target green flag is the per-ref BGSScannable `+0x28` byte — NOT the species `+0x21`
slot, NOT the trait-known set.** Same id-domain as biome flora (ID_939118), keyed by the ref's
own FormID, not per-(planet, trait).

### 1.3 Where "Required" (M) comes from
M = number of refs in the player's current Location tagged with **LocRefType
`PlanetTraitScanTargetLocRef` (0x0027A567)**. The parent survey quest `SQ_Parent` (0x0007092C) holds
that LocRefType as a VMAD script property — it is the runtime owner of the "how many trait refs
scanned" counter. `[esm-verified]` (authoring doc §2, §4). The scan-target base ACTI carries
`FTYP = 0x0027A567`, and each overlay LCTN's LCSR lists exactly the LocRef-tagged refs. So the
scanner counts, over the loaded Location: refs of LocRefType 0x0027A567 (M total) and how many have
their BGSScannable `+0x28` set (N).

---

## 2. WHAT A REAL SCAN OF A SCAN-TARGET WRITES vs. SetTraitKnown

### 2.1 A real scan-target scan (Papyrus `SetScanned` / hand-scanner)
`ID_118472 SetScanned(ref, flag)` (@1420845e0) `[decompile-verified]` (setscanned.txt:1-20):
- If `ID_83007(ref) != 0` (component already exists) → `ID_83008(ref, flag, 0xd, 0)` → writes the
  scanned byte via `ID_83038` (find-only on the 939118 component) + per-planet credit.
- **Else (no component yet — the normal case for a freshly-materialized scan-target ref)** →
  `ID_83006(ref)` (canonical form) then **`ID_83005(ref, canonical, flag)`**, which queues a
  `BSComponentDB2 CreateAndDeleteCommand<…InitialScanStatus>` (lambda `ID_83047`/`ID_83043`) that
  **BIRTHS the per-ref BGSScannable component (ID_939118) and stamps +0x24=canonical, +0x28=scanned**.
  `[decompile-verified]` (scan-target-persist.txt:457-515 `ID_83005`; trait-scan-persist2.txt:100-110
  `ID_83047`).

The net durable effect of scanning a scan-target ref: a **per-ref ScannableComponent (939118)** with
`+0x28 = scanned`. That is exactly what `ID_83007` reads to color the outline green and what the
Monocle counts toward N. The component is keyed by the **ref's own FormID** (`*(ref+0x28)`).

### 2.2 What `CompleteSurvey` does (`ID_52155 SetTraitKnown`)
`ID_52155(planetId, traitKwd, true)` (@1407b78d0) `[decompile-verified]` (knowledge-db.txt:92-244):
- Resolves the planet's trait/proxy form via `ID_58987`/`ID_124903` keyed `(ID_937887<<48)|(planetId
  <<16)` (discriminator **ID_937887 "ProxyFormPtr"**, NOT 939118, NOT the species 938333 slot).
- `ID_52205` writes the trait into the proxy form's trait-known set (ref-counted form write).
- Refreshes survey % (`ID_1016657`/`ID_65318`), and fires `StarMap::PlanetTraitKnownEvent`
  (`ID_52210`) + `ID_97853`.

It writes the planet's **trait-KNOWN** set only. It touches **NOTHING** in the per-ref BGSScannable
(939118) domain and nothing the Monocle's `uLocationTraitRefs*` counter reads.

### 2.3 The MISSING piece (exact)
`CompleteSurvey` makes the panel show "TRAITS 3/3" (trait-known set) but never sets the
BGSScannable `+0x28` byte on the placed `PlanetTraitScanTarget*` REFRs. The surface object's outline
is `ID_83007(ref)` = 1 (component exists from materialization but unscanned) → BLUE, and the Monocle
counts N=already-scanned, M=LocRef count → "1/2 SCANNED". **Trait-KNOWN and scan-target-SCANNED are
two unrelated stores; completing one does not complete the other.**

---

## 3. REF-FREE WRITE RECIPE — honest verdict

**VERDICT: there is NO ref-free off-planet write that fully-scans + greens a planet's trait
scan-targets.** It is materialization-bound, strictly worse than the species-green crux:

1. **The refs don't exist off-planet.** Scan-target REFRs are authored *inside* the overlay
   `OverlayTrait…Location` LCTN's host cells; they only materialize when the biome/overlay system
   instantiates the chosen overlay cell onto the surface of the planet the player is standing on.
   `[esm-verified]` (authoring doc §5). The BGSScannable component (939118) is keyed by the ref's
   FormID and only exists once the ref is loaded; like the species ScannableComponent it is the
   transient per-instance layer (KNOWLEDGE-BASE §5: "939118 rebuilt per load"). There is no
   persistent per-(planet, trait) scan-target slot to pre-write (unlike species' persistent
   `(938333|planet)` `+0x21`). `[decompile-verified]` / `[inferred]`

2. **"M" is not an authored per-trait constant.** It is the LocRef count of *whichever overlay
   variant the runtime rolled* for this planet instance (e.g. AmphibiousFoothold overlays carry
   M ∈ {1,2,2}; SentientMicrobialColonies happens to be uniformly {2}). Offline you can only derive
   the *set* of possible M, not the live one. `[esm-verified]` (authoring doc §2).

3. **No analog of the species `+0x21` pre-write exists.** Species green persists because it is keyed
   on the durable `(938333 | planetId)` + species-FormID slot. The scan-target green is keyed on the
   transient `(939118 | refFormID)` component; there is no durable per-trait scan-target byte in
   `db+0x268` to stamp. (`ID_52151`/`ID_52153` only set planet-level category BITS in the 938333
   `slot+0x20`, which drive % and "biome complete", not the per-scan-target outline.)
   `[decompile-verified]` (trait-scan-persist2.txt:52-72).

**The achievable, honest path (ON-PLANET only):** mirror the species solution's spawn-and-scan, but
for scan-targets you do not even need to spawn — the refs are already placed when you are on the
planet. On the surface:
- Enumerate loaded ObjectReferences whose base ACTI has FTYP `PlanetTraitScanTargetLocRef`
  (0x0027A567) **or** whose base carries PRPS AV `HandScannerTarget` (0x0022A2B6), **or** that are
  in the current Location tagged LocRefType 0x0027A567.
- Call Papyrus `ObjectReference.SetScanned(true)` (= `ID_118472`) on each. Because these refs lack a
  scanned component yet, this takes the `ID_83005` CREATE path that stamps `+0x28=scanned` → next
  `ID_83007` returns 2 → outline `_TargetFullyScanned` (green) and the Monocle counts them toward N
  until N==M. `[decompile-verified]` (setscanned.txt:1-20; trait-ref-count.txt:2316-2317).

  CAVEAT — persistence: the 939118 component is the transient per-instance layer. Whether a
  `SetScanned`-stamped scan-target survives a save/reload was NOT verified here (the species G2 case
  greens via the durable 938333 `+0x21`, a path the scan-target does not have). This needs an
  in-game save-test; if it does not persist, the on-planet pass must be re-run after each load, or a
  different durable record (the QUST/alias counter on SQ_Parent) must be driven instead. **OPEN.**

---

## 4. ESM-DERIVED DATA (offline, per planet)

Authored linkage `[esm-verified]` (full detail in `esm-trait-scan-target-authoring-2026-06-23.md`):

- **A planet's traits** = `PNDT.KWDA` entries whose EDID matches `^PlanetTrait\d+` (exclude
  `PlanetTraitScanTarget*` and `_Close`/`_Possible`). Verified: JemisonPlanetData (0x0003F5A1) KWDA
  holds traits 09, 14, 20. Trait keywords carry `TNAM = 44 (0x2C)`.
- **trait KYWD → scan-target ACTI**: match by EDID suffix `PlanetTrait<NN><Name>` ⇒
  `PlanetTraitScanTarget<NN><Name>[01/02]`. Full 27-trait / 31-ACTI table is in the authoring doc
  (e.g. trait 20 `0x00225588` → ACTI `0x0021B250`).
- **trait KYWD → overlay Locations**: reverse-KWDA lookup → `OverlayTrait<Name><size>Location` LCTNs
  (trait 20: 0x0010A476 Med01, 0x0021D242 Med02, 0x003807B9 Lg01).
- **Required count M (candidate set)**: for each overlay LCTN, parse `LCSR` (stride 20 bytes:
  `[LocRefType(4)][refFormid(4)][worldspace(4)][gridX i16][gridY i16][flags(4)]`) and count entries
  whose LocRefType == `0x0027A567`. Trait 20 = 2 in every overlay → M=2 (matches "1/2 SCANNED").
- **Key formids for the mod**: LocRefType `PlanetTraitScanTargetLocRef` = **0x0027A567**;
  PRPS AV `HandScannerTarget` = **0x0022A2B6** (value 1.0); parent survey quest `SQ_Parent` =
  **0x0007092C**; `IsTraitKnown`/`SetTraitKnown` natives = `ID_52154`/`ID_52155`, trait discriminator
  on the planet proxy = `ID_937887`.

Offline you can derive, per planet, the set of (trait, scan-target ACTI, {M per overlay variant}).
You CANNOT derive the single live M (which overlay landed) or pre-write the scan state without the
materialized refs.

---

## 5. PROOF — headless commands + citations

Ghidra (project `ghidra-project/Starfield`, Starfield.exe 1.16.236 ≡ .244 offsets per
offset-skew-236-vs-244.md), `analyzeHeadless … -noanalysis -scriptPath re/ghidra/scripts -postScript`:
- `FindStringsXref.java scanned-count-strings.txt "SCANNED" "ScanTarget" "Scan Target"` →
  `uLocationTraitRefsScanned`@144c7e9a0 ← ID_90486; `_TargetScanned`/`_TargetFullyScanned`,
  `$ScanMapMarker_Unscanned`.
- `DecompileIds.java trait-ref-count.txt 90486 90518 90491 90548` → the UI model serializer
  (uLocationTraitRefs* @ model+0xa0/+0xc0) and the outline decider (param_1[0xf2c]=ID_83007).
- `DecompileIds.java trait-known-writer.txt 52205 52156 52210 97853` → SetTraitKnown writer +
  PlanetTraitKnownEvent source.
- `DecompileIds.java scan-target-persist.txt 83010 94477 52211 124903 83005` → ID_124903 (planet
  proxy trait table, disc ID_937887); ID_83005 (BGSScannable create path).
- `DecompileIds.java trait-scan-persist2.txt 51413 52151 52210 83047 38418` → 938333 bit reader;
  create lambda.
- `XrefsToIds.java trait-ui-xrefs.txt 90486 90518 …` → UI model is a vtable class (DATA refs).

Existing dumps reused: `setscanned.txt`, `scanned-state.txt`, `knowledge-db.txt`, `hinge-58987.txt`,
`scan-component-lifecycle.txt`, `SCAN-TO-GREEN-KNOWLEDGE-BASE.md`.

ESM: `re/tools/esm_scan_target_probe.py`, `esm_resolve_forms.py`, and the sub-agent's
`esm_trait_scan_authoring.py` et al. (real CTDA/LCSR parse, no hardcoded tables).

---

## 6. KEY IDs / DISCRIMINATORS (this sub-problem)

| Thing | ID / FormID | Role |
|---|---|---|
| `ID_52155` SetTraitKnown | @1407b78d0 | writes trait-KNOWN set (panel 3/3); CompleteSurvey calls this |
| `ID_52154` IsTraitKnown | @1407b7730 | reads trait-KNOWN; uses disc ID_938333 + keyword+0x28 |
| `ID_937887` "ProxyFormPtr" | disc | per-planet proxy form holding the trait set (ID_124903/ID_58987) |
| `ID_52210` | @1407c0920 | `StarMap::PlanetTraitKnownEvent` source (fired by SetTraitKnown) |
| `ID_83007` is-scanned | @1413076d0 | reads BGSScannable +0x28 → outline byte (scan-target green) |
| `ID_118472` SetScanned | @1420845e0 | scan write; ID_83005 create path stamps 939118 +0x28 |
| `ID_83005` | @1413075a0 | births per-ref BGSScannable (939118) component |
| `ID_939118` | disc | per-ref BGSScannable (+0x24 canonical, +0x28 scanned) — the scan-target green store |
| `ID_90548` | @1415a47c0 | Monocle outline decider; `param_1[0xf2c]=ID_83007(ref)` |
| `ID_90486` | @141597d40 | UI model serializer: model+0xa0 scanned / +0xc0 required |
| `ID_90518` | @14159b890 | Monocle scanner per-frame updater (builds the model, counts LocRef refs) |
| LocRefType `PlanetTraitScanTargetLocRef` | 0x0027A567 | tags scan-target refs in the Location; M = count of these |
| AVIF `HandScannerTarget` | 0x0022A2B6 | PRPS on scan-target ACTI base; flags it scannable |
| QUST `SQ_Parent` | 0x0007092C | runtime owner of the LocRef scan counter (script property) |

**Superseded intuition (recorded):** the prompt's premise "no static REFR references the scan-target
ACTI base" is REFUTED — the refs ARE authored static placements inside overlay LCTN cells; they are
materialization-bound at the *cell-instantiation* level, not absent. (`esm-trait-scan-target-
authoring-2026-06-23.md` §5.)
