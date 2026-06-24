# Starfield planet-survey RE — CONFIRMED FINDINGS

Curated index of the **100%-confirmed** reverse-engineering results for the Complete Planet
Survey mod. This is the durable, in-repo record so every agent/contributor sees the same
ground truth instead of re-deriving it from the raw `re/` dumps.

**Scope rule.** A finding only appears here if it carries one of these confidence tags:

- `save-verified` — proven by a byte-diff of real Starfield `.sfs` saves (scripts in `re/save/`).
- `decompile-verified` — the engine function was disassembled/decompiled and the logic read directly.
- `tool-validated 17/17` — reproduced by an offline tool against all 17 ground-truth species (no hardcoded per-species tables).
- `in-game` — observed live by the user (the only one who can run the game).
- `shipped` — implemented in the mod and released.

Anything that is *not* confirmed lives under **★ Known dead-ends** or is omitted. The source
memory was full of self-corrections; claims that were disproven are recorded as dead-ends so
they are never re-pursued as fact.

> **Engine target:** Starfield 1.16.236 ≡ 1.16.244 (offsets byte-identical across both —
> `decompile-verified`, see `re/ghidra/output/offset-skew-236-vs-244.md`). SFSE 0.2.21.
> **Offset convention:** runtime offset = decompile address − `0x140000000`. `REL::ID(n)`
> resolves the address via the Address Library (version-specific; the field offsets below are not).

---

## 1. Green markers / species (flora + fauna) — SOLVED, offline-derivable

| # | Finding (one line) | Confidence | Evidence |
|---|---|---|---|
| 1.1 | **Every** green-set marker (resource, biomes, genetics, reproduction, temperament) for flora AND fauna is 100% derivable offline from `Starfield.esm` — no live instance, no materialization. | `tool-validated 17/17` | `re/tools/esm_derive_markers.py` (real CTDA evaluator, reproduces all 17 ground-truth species exactly, generalizes to all 1765 PNDT planets) · `re/ghidra/output/species-scan-complete-model-2026-06-23.md` |
| 1.2 | The two marker catalogs are FLST `0x00160C96` HandScannerPlantKeywords (flora, 39 markers) and FLST `0x00160C97` HandScannerActorKeywords (fauna, 16). Layout: `LNAM`(u32)×N markers, then per conditioned marker `INAM`(u32 = LNAM idx)+`CITC`(u32)+`CTDA`(32B)×N; no INAM block ⇒ unconditional. | `decompile-verified` + `tool-validated` | `re/ghidra/output/esm-marker-derivation-2026-06-22.md` · `re/esm/handscanner_kywds.json` (full 87-marker registry) |
| 1.3 | CTDA evaluator: comparison = `(op>>5)&7` {0:== 1:!= 2:> 3:>= 4:< 5:<=}; OR-with-next = `op&1` (consecutive OR-flagged items OR'd into runs, runs AND'd). Leaf funcs: 882=const-1.0 anchor; 858=GetIsPlanetTrait (param1 ∈ planet PNDT KWDA); 560=HasKeyword (species granted-kw set); 14=GetActorValue(FLOR PRPS AVIF `0x0023E905`); 837=EvaluateConditionForm (recurse into the CNDF's CTDA). | `decompile-verified` + `tool-validated` | `re/tools/esm_derive_markers.py` (faithful `TESCondition::IsTrue` port) · `re/tools/esm_ctda_decode.py` |
| 1.4 | **Flora genetics = pure f(planet PNDT KWDA traits)** — no species input. STRUCTURE ⇐ planet WaterQuality (Standard default / Double ⇐ Water02Biological / Chiral ⇐ Water03Chemical / XNA ⇐ Water04HeavyMetal\|05Radioactive); BASIS ⇐ atmosphere+magnetosphere (Carbon default / Arsenic ⇐ AtmTox01Toxic / H2S ⇐ AtmTox00Corrosive / Methane ⇐ AtmType03M / Silicon ⇐ weak-Magnetosphere). Exactly 1 genetics marker/planet (proven on all 1765). | `tool-validated 17/17` | `re/tools/esm_derive_markers.py` · model doc §genetics |
| 1.5 | **Flora reproduction = f(static FLOR PRPS AV N, planet traits)**: direct Repro0N by AV gated by `HasOverride==0`; overrides Rhizomes (EcologicalConsortium `0x00225590` ⇒ Self-cloning `0x00171867`) and Spores (¬Rhizomes ∧ AV≥4 ∧ (PsychotropicBiota `0x00225589`\|AeriformLife `0x00225597`) ⇒ `0x00171869`). CNDFs `0x00171350/51/52`. | `tool-validated 17/17` | `re/tools/esm_derive_markers.py` · `re/tools/esm_repro_ctda_full.py` |
| 1.6 | **Fauna temperament X** is offline-derivable via NPC_ `OBTS` (7-byte entries = OMOD id @0x12) → OMOD EDID `mod_CCT_Temperament_*` → DATA `NKEY` keyword → FLST `0x00160C97` func-560 map. The earlier "temperament is materialization-bound" verdict is WRONG (it only checked the flat KWDA, which is empty). 99.26% coverage; 7 Lvl/template bosses + Player are spawn-bound (X=0, out of scope). | `tool-validated 17/17` | `re/tools/esm_derive_markers.py` · `re/tools/esm_obts_omod_link.py` · `re/tools/esm_temperament_source.py` |
| 1.7 | The OBTS subrecord 4CC is `0x5354424F` ("OBTS"). The long-standing "fauna 3/4 markers" bug was a one-char typo (`0x53544250` = "PBTS") in `EsmReader.cpp` that made OBTS never match → empty granted-kw set → every temperament marker dropped. Not a derivation gap. | `in-game` + `tool-validated` | Caught by `test/ValidateMarkers.cpp` (8/17 → 17/17 after fix); model doc "ROOT CAUSE FOUND + FIXED" |
| 1.8 | **Fauna Abilities/Resistances/Weaknesses** (`0x002634BF/C1/C0`) are gated by func-699 HasMagicEffectKeyword and ARE green-required for ability-creatures (refutes "448/699 = display-only" for func-699). Derivable offline via NPC_ OBTS→OMOD `NPRK`→PERK (PRKE byte0==1)→DATA spell→SPEL `EFID`→MGEF `KWDA` scanner kw. Written as a separate actor-marker set. | `decompile-verified` + `in-game` | `re/ghidra/output/fauna-ability-markers-2026-06-23.md` (in-game: REEFWALKER 5-marker green, APEX CROCODAUNT 6-marker green) |
| 1.9 | **Planet traits source** = the PNDT record's `KWDA` subrecord (u32 KYWD array). `GetIsPlanetTrait(t)` offline ≡ "t ∈ planet's PNDT KWDA". The trait-keyword ranges are `0x00225xxx / 0x00295Exx / 0x002792xx / 0x00290Fxx`. | `decompile-verified` + `tool-validated` | model doc · `re/tools/esm_planet_trait_check.py` |
| 1.10 | **Green outline color boolean = `slot+0x21 != 0` alone** (raw scan-flag byte, no threshold) for a tracked biome species, via `ID_52159` → `ID_90491/90548`. `slot+0x08` is NOT read in the color decision — it is the detail-panel splice + on-screen repaint. (See 1.11 for the practical refinement.) | `decompile-verified` + `in-game` | `re/ghidra/output/species-scan-complete-model-2026-06-23.md` (★★ predicate); `ProbeRenderRead` called `ID_52159` after a bare +0x21 poke → 17/17 green |
| 1.11 | **Practical green rule = write BOTH** `slot+0x21` (the color boolean) AND a COMPLETE `slot+0x08` marker set (the full live-scan set incl. func-699 abilities). On-screen repaint + the detail panel need the `slot+0x08` build; for ability-creatures the outline state machine additionally gates on `slot+0x08` completeness. | `in-game` | model doc ★★ ("writing func-699 markers to slot+0x08 GREENS ability-creatures"); `re/ghidra/output/slot-0x08-catalogue-writer-2026-06-22.md` (the `ID_52158` writer path) |
| 1.12 | **Planet key is a single FormID identity:** `*(planetForm+0x54) == ID_52188(player) == planet PNDT FormID` — no converter. The stamp site `ID_51735` BGSPlanet::Manager::SetCurrentPlanet writes `Manager+0x80 = planet FormID` VERBATIM (`MOV [RCX+0x80],EDX`), so the render key ≡ planet FormID by construction. | `decompile-verified` + `in-game` | `re/ghidra/output/planet-id-stamp-site-2026-06-23.md`; in-game `TestRenderKeyGreen` measured `+0x54 == renderId == 0x0003F5A1` |
| 1.13 | **canonical species id == authored ESM id** for all 17 probed species (the `ID_83006`/`ID_83009` canonical-remap theory is dead — authored id is the correct DB key). | `in-game` | `ProbeScanKeys` (8 Jemison species, `canonical == authored` ×8; 2 fauna had dynamic `0xFF` base but canonical still == authored) |
| 1.14 | The durable green record = BSGalaxy::PlayerKnowledge at `db+0x268`, entry key `(ID_938333<<48)|(planetId<<16)`, subobj = entry+0x20, species slot via FNV-1a (`ID_124901`) of the authored species id; percent`slot+0x20`, scan-flag`slot+0x21`. Ref-free writes persist (the`ID_124898` dirty bit is saved; no event sink commits state). | `decompile-verified` + `in-game` | `re/ghidra/output/species-scan-complete-model-2026-06-23.md`; on-planet CompleteSurvey greens + persists across quit-to-desktop (`in-game`) |

**Shipped status (`in-game`, `shipped`):** on-planet `CompleteSurvey` greens flora+fauna and
persists across a full restart. `EsmReader.cpp` now ports the unified CTDA evaluator and derives
every flora/fauna marker offline, matching the Python reference 17/17.

---

## 2. Traits — planet trait-known data (the all-planets path) — SOLVED

| # | Finding (one line) | Confidence | Evidence |
|---|---|---|---|
| 2.1 | **Trait-known = the `938333` PlayerKnowledge record**, written by `ID_52155` (MarkTraitKnown) → gate `ID_52205` → `ID_52156` into the planet's `938333` trait member-array (`key=(938333<<48)|(planetId<<16)`, DB-insert`ID_52204`) + fires`PlanetTraitKnownEvent` + `ID_97853` survey recompute. | `decompile-verified` | `re/ghidra/output/astro-trait-known-synthesis-2026-06-24.md` |
| 2.2 | The `MarkTraitKnown`/`MarkTraitKnownForPlanet` path is **ref-free, off-planet, all-planets** — it is the engine's own orbital (Astrophysics-skill) trait path. Driven galaxy-wide by the Papyrus sweep (`CompletePlanetSurveyQuest.psc` `MarkTraits(p, p.GetKeywordTypeList(44))`). Only requirement: the planet's components are materialized in `db+0x268` (the discover→finalize→MarkTraits order guarantees this). | `decompile-verified` + `shipped` | `astro-trait-known-synthesis-2026-06-24.md`; `CompletePlanetSurveyQuest.psc` |
| 2.3 | All trait readers hit `938333`: `ID_52154` IsTraitKnown (panel "TRAITS N/N"), `ID_52159` species green outline, `ID_97851` survey % (via the `ID_1016657` aggregator arrays). So the durable `938333` write is **byte-identical to a real scan** for the trait-known data. | `save-verified` + `decompile-verified` | `re/save/decode_pk_record.py`, `re/save/compare_save19.py`, `re/save/compare_save21.py` (mod Save19/21 == real Save14: slot flag=2, pct=100, pooled keyword `0x00625588` present) |
| 2.4 | In the `938333` record, IDs are stored as **DB-local indices = `FormID ^ 0x00400000`** (bit22 tag), which is why raw-FormID searches found 0 hits. The record lives in **GlobalData REGION 1** (not the ChangeForms section). | `save-verified` | `re/save/decode_pk_record.py` (`0x0061B250`→ACTI `0x0021B250`; `0x00625588`→KYWD `0x00225588`) |

---

## 3. Resources & attribute survey state — SOLVED (ref-free, all-planets)

| # | Finding (one line) | Confidence | Evidence |
|---|---|---|---|
| 3.1 | A planet reaches 100% only when THREE legs are satisfied: (a) per-species scan flags `slot+0x21`/`+0x20`; (b) the **attribute-known bitmask at `entry+0x20` (== subobj+0x00)** — set `*(u32*)subobj \|= 0x7` (bits 0/1/2 gate ~10 of the ~16 categories: magnetosphere/resources/atmosphere/gravity/temp/water); (c) traits via `ID_52155`. A barren body sits at ~50% until the bits are set. | `decompile-verified` + `in-game` | `re/ghidra/output/survey-percent.txt`, `survey-aggregator.txt`; in-game Toliman II-A 51%→100% |
| 3.2 | **Survey % is computed from the player's knowledge DB, not canonical planet data.** Chain `ID_1016657`→`ID_1016658`→`ID_97857`→`ID_97849` build a ~0x400 struct's species lists from knowledge, then `ID_97850`→`ID_97851` weight ~16 categories into `scanned_total/total_total`. | `decompile-verified` | `re/ghidra/output/survey-percent.txt`, `aggregator-and-planetdata.txt`, `populator.txt` |
| 3.3 | **Barren / resource-only bodies are fully completable ref-free, galaxy-wide** (bits + resources + traits + slate). Resource host = disc `ID_938336` via `ID_58987`; entry create/discover = `ID_102650` (also recurses moons); slate = `ID_97853`. | `decompile-verified` + `shipped` | `re/ghidra/output/knowledge-db.txt`, `hinge-58987.txt`; shipped galaxy sweep |
| 3.4 | **PPBD per-biome data is fully authored in `Starfield.esm`** and statically extractable: all 1765 PNDT records are zlib-compressed (record flag `0x00040000`); 182 planets have PPBD flora/fauna (947 fauna + 764 flora refs). Per-biome layout: `u32 biome \| f32 chance \| u32 unk \| u32 RSGD \| u32 nFauna+NPC_[] \| u32 nKw+KYWD[] \| u32 nFlora+u32 entrySize(=9)+{u32 FLOR,u32 MISC,u8 freq}[]`. File FormIDs == runtime FormIDs (load order 00). | `tool-validated` | `re/esm/extract_planet_species.py` (validated against the real ESM); loader `ID_51401` decompiled (`re/ghidra/output/ppbd-parser.txt`) |

---

## 4. In-world trait scan-target OBJECT ("Microbial Community" / "Unexplored Ecological Feature")

This is the **on-planet, materialization-bound** surface object — distinct from the trait-known
DATA in §2 (which is already all-planets-complete). The DATA is solved; only this cosmetic
on-surface visual is engine/scan-bound.

> ### ★ SOLVED 2026-06-24 (`in-game`) — drive the game's OWN Papyrus survey quest, not the engine.
> The whole byte-level hunt below (939118 / 938333-slot / 938083 / the read-hook) is SUPERSEDED. The
> completion is plain Papyrus in the SHIPPED game scripts (`…\Starfield\Data\Scripts\Source\PlanetTrait‑
> ScanTargetScript.psc` + `SQ_ParentScript.psc`): a scan fires `OnScanned()` → `SQ_Parent.DiscoverMatching‑
> PlanetTraits(ref)`, which sets the **Location actor value `PlanetTraitLocationScanCount`**; at the alive
> scan-target count (`loc.GetRefTypeAliveCount(PlanetTraitScanTargetLocRef)` = the "N") it `SetExplored()`s
> + discovers the trait. **"0/N SCANNED" is literally a Location actor value.** THE FIX (shipped, Papyrus-
> only, no DLL change): for each loaded scan-target ref — `r.SetScanned(true)` (reveals the detail text +
> blocks re-scan) + `loc.SetValue(PlanetTraitLocationScanCount, needed)` (absolute, caps any over-count) +
> `SQ_Parent.DiscoverMatchingPlanetTraits(r, false)`. Object now reads named + 100% + detail, persists on
> reload. The objects have no all-planets durable WRITE store, BUT they **auto-resolve on arrival** —
> `PlanetTraitScanTargetScript.OnLoad → CheckForScanTargetUpdate → UpdateScanTarget → SetScanned()` fires
> whenever the planet's trait is KNOWN (SQ_ParentScript:607-630). That is the **Astrophysics-skill flow**
> (orbital-discover a trait → land → the object is already done). The off-planet trait-known path is
> `MarkTraitKnown`/`ID_52155`→`938333` = **ref-free, all-planets** (§2.2; `astro-trait-known-synthesis-
> 2026-06-24.md`), which `MarkTraits` drives galaxy-wide. So `CompleteLifePlanets`/`CompleteBarrenPlanets
> "traits"` (mark known everywhere) make every planet's objects complete on the next visit; the explicit
> `_CompleteTraitScanObjects` is only for objects ALREADY LOADED when you mark the trait (their OnLoad
> already fired). Key forms: `SQ_Parent` QUST `0x0007092C`, `PlanetTraitScanTargetLocRef` `0x0027A567`,
> scan kw `0x001CBEA3`. The user's insight cracked it: the objects are SEPARATE entities LINKED to a
> trait, not the trait itself — and Astrophysics already proves off-planet trait completion exists.

| # | Finding (one line) | Confidence | Evidence |
|---|---|---|---|
| 4.1 | The surface scan-target's outline + N/M count read a **TRANSIENT per-ref `BGSScannable` component** (disc `ID_939118`, scanned byte `+0x28`), keyed by the placed REFR's own FormID. `939118` has an **empty save serializer** (`ID_38417`) and is **reset to 0 on every materialize** — it does not exist off-planet. **For traits, BLUE = scanned** (not green). | `decompile-verified` + `save-verified` | `re/ghidra/output/trait-scan-target-durable-store-2026-06-23.md`, `trait-true-completion-2026-06-23.md`; `re/save/analyze_scan_count.py` (REFRs/ACTI/kw absent from all 3 saves) |
| 4.2 | The **N/M digit** = `model+0xa0 ← *(u8*)(ID_938422+0x38)` (scanner-menu runtime global `@0x1461ea0c8`), rebuilt only by the aim-bound scan refresh — NOT a recount of the `+0x28` byte. A console/ref-free write cannot move it. | `decompile-verified` + `in-game` | `re/ghidra/output/panel-count-source-2026-06-23.md`; in-game (mod set `+0x28`=1 on both targets → stuck "0/2") |
| 4.3 | The **title (Unknown→named)** = durable `938333` "discovered" record read by `ID_124900(planetId, ID_83009-canonical)` in `ID_90518` case-5; the panel reveal `ID_124900`→`ID_37875` reads `slot+0x08`, so an empty `slot+0x08` shows "UNKNOWN FEATURE" even with `+0x21`/`+0x20` set. | `decompile-verified` + `save-verified` | `re/save/decode_pk_record.py` (member array `slot+0x08` ticks 0→1 with trait keyword on scan#2); `panel-count-source-2026-06-23.md` |
| 4.4 | **The on-screen object visual (green/count/identity) is ON-PLANET / loaded-ref ONLY — not all-planets writable.** Proven by three independent angles: decompile (transient per-ref `939118`), two in-game tests (SetScanned + `938333` KB-write, both failed incl. save+reload), and a save-file byte-scan (scan-target data absent from saves). Even the base game only greens these on a physical surface scan. | `decompile-verified` + `in-game` + `save-verified` | convergent verdict across `trait-scan-target-durable-store-2026-06-23.md`, `trait-true-completion-2026-06-23.md`, `re/save/save-file-write-feasibility-2026-06-23.md` |
| 4.5 | The scan-target REFRs are **static, authored, fixed FormIDs** (not procedural): trait KYWD `PlanetTrait<NN>` (in PNDT KWDA) → `OverlayTrait…Location` LCTN whose KWDA contains the trait kw → the LCTN's `LCSR` entries (stride 20 = 5×u32) with `[0] == 0x0027A567` (PlanetTraitScanTargetLocRef); `[1]` = placed REFR FormID. 26/27 traits have overlays, 87 overlays, 144 scan-target REFRs, M=2 per overlay. | `decompile-verified` + `tool-validated` | `re/tools/trait_scan_target_map.json`; `re/ghidra/output/esm-trait-scan-target-authoring-2026-06-23.md` |
| 4.6 | An on-planet completion path exists for LOADED refs (cosmetic only — the survey is already complete via §2): get the right ref (aimed `*(monocle+0xf18)` via `ID_139363`, OR FindAllReferencesWithKeyword(`0x001CBEA3`) filtered by `ID_83007(ref)!=0`) → `ID_83008(ref,1,8,0)` → `ID_83025` identity → monocle repaint. **Guard `ID_83007(ref)!=0` before any write** (no component = fault; this is what made `ID_83024` crash). | `decompile-verified` | `re/ghidra/output/trait-onplanet-completion-2026-06-23.md` |

> Note: a regression was observed — `ScanRefNative` (`939118+0x28`) **persists across a same-planet
> fast-load** (the engine doesn't re-materialize already-loaded scan-targets), which jams the
> hand-scanner until a full restart / leave+return. Do not ship a test command that writes this byte
> blind. (`in-game`)

---

## 5. Save format (.sfs)

| # | Finding (one line) | Confidence | Evidence |
|---|---|---|---|
| 5.1 | The `.sfs` container is byte-exact decoded: `BCPS` magic, LE header, zlib `ZIP`-tagged ~212 KB chunks; body = `SFS_SAVEGAME` + header + plugin list + FO4/SSE-shaped file-location table. | `save-verified` | `re/save/sfs_container.py` + `re/save/sfs_body.py` |
| 5.2 | The durable PlayerKnowledge `938333` record lives in **GlobalData Region 1** (`body[offsetA:offsetB]`), NOT ChangeForms — which is why earlier ChangeForms/formID-array diffs missed it. It grows +22 B on scan#1 (CREATE) and edits in place on scan#2. | `save-verified` | `re/save/decode_pk_record.py`, `re/save/scan-count-store-2026-06-23.md` |
| 5.3 | **Direct save-file authoring of trait scan-target green is BLOCKED**: never-visited overlay REFRs don't exist in the save (materialization-created), and the ChangeForms encoding is undocumented (community StarfieldSaveTool stops at header/plugins; Starfield ChangeForms/GlobalData diverges from FO4). | `save-verified` | `re/save/save-file-write-feasibility-2026-06-23.md` |
| 5.4 | The real formID array (start = section offset at `table_base−8`: `u32 count` then FormIDs) is **byte-identical across the 0/2, 1/2, 2/2 saves** (count 311117, 0 added/removed) → there is **no per-REFR ChangeForm** for a scanned trait target and no durable id-keyed N/M counter. `changeFormCount` 15362→15363 = scan#1 adds the planet's `938333` singleton, scan#2 edits in place. | `save-verified` | `re/save/analyze_scan_count.py` (and `diff_real_fidarray.py`, `diff_changeforms.py`, `diff_all_regions.py`) |

---

## 6. Confirmed engine offsets & REL::IDs

Only entries we are confident about (decompile + cross-verified, and stable across 1.16.236/244).
This table is the candidate set for an upstream CommonLibSF contribution.

> **All struct-field offsets below are byte-identical on 1.16.236 and 1.16.244** — only the
> function *addresses* moved (whole-image shift handled by the Address Library). Source:
> `re/ghidra/output/offset-skew-236-vs-244.md` (versionlib parser validated 0 mismatches across
> all 910,562 IDs; each function disassembled from the live 244 exe).

### 6a. Discriminators (BSGalaxy::PlayerKnowledge / ModuleState component types)

| Symbol | Disc value | What it is | Confidence |
|---|---|---|---|
| `ID_938333` | PlayerKnowledge | Per-planet survey scan-state record (`db+0x268`); species flags + trait member-array. Key `(938333<<48)\|(planetId<<16)`. | `decompile-verified` + `save-verified` |
| `ID_937887` | CTProxyFormPtr ("ProxyFormPtr") | Per-planet pointer to the planet's proxy form (engine-authored definition data). **NOT a writable knowledge/trait-known slot** (common misconception — see dead-ends). | `decompile-verified` |
| `ID_938336` | resource host | Resource-generation host (via `ID_58987`); yields resources only, not flora/fauna. | `decompile-verified` |
| `ID_938335` | CTPlanetOverlayData | Per-planet spatial biome overlay (`db+0x300`), key `(biomeId<<32)\|938335\|0x10000`; **empty until cell-load** (land-gated). | `decompile-verified` |
| `ID_938158` | CTPerBiomeData | Per-(planet,biome) authored fauna/flora/resource component; **lazy** — empty for un-landed planets. | `decompile-verified` |
| `ID_939118` | per-ref BGSScannable | Transient per-REFR scan component, scanned byte `+0x28`; empty serializer, reset on materialize. | `decompile-verified` + `save-verified` |
| `ID_938422` | scanner-menu runtime state | Global `@0x1461ea0c8`; `+0x38` = the N/M scanner tally byte (aim-bound, transient). | `decompile-verified` |

### 6b. Functions (REL::ID → role)

| REL::ID | 1.16.244 addr | Role | Confidence |
|---|---|---|---|
| `ID_126578` | 0x1423ff640 | GetKnowledgeManager (Meyer's singleton; `manager+0x8B0` = the DB, caller-side) | `decompile-verified` |
| `ID_126806` | 0x1424105d0 | DB bucket-table lookup; bucket = `base + 0x12 + idx*4`, `0xfe0` sentinel | `decompile-verified` |
| `ID_124898` | 0x1423464e0 | Writer: scan-flag byte `slot+0x21` (+ moves staging array into slot `+0x08`) | `decompile-verified` |
| `ID_124899` | 0x142346630 | Writer: percent byte `slot+0x20` | `decompile-verified` |
| `ID_124901` | 0x1423467b0 | FNV-1a slot hash of the species id (stride `0x30`, next-ptr `+0x28`) | `decompile-verified` |
| `ID_124900` | — | Detail-panel member splice (→ `ID_37875`); reads `slot+0x08` | `decompile-verified` |
| `ID_52159` | 0x1407b7d60 | Green/species-scanned reader; reads `*(u8*)(*(subobj+0x60)+0x21+idx*0x30)` | `decompile-verified` + `in-game` |
| `ID_90491` / `ID_90548` | 0x141597a50 / 0x1415a3db0 | Outline deciders (call `ID_52159`/`ID_52180`) | `decompile-verified` |
| `ID_52180` | 0x1407bb5b0 | Planet-membership set (2nd green lever; disc `ID_938158`, authored FormIDs) | `decompile-verified` |
| `ID_52188` | 0x1407bcbd0 | Planet-from-player resolver (returns current planet FormID == render key) | `decompile-verified` + `in-game` |
| `ID_52157` → `ID_52158` | 0x1407b75b0 → 0x1407b77d0 | Real per-species scan chain; `ID_52158` populates `slot+0x08` from the live biome | `decompile-verified` |
| `ID_52155` → `ID_52156` | — | SetTraitKnown → write `938333` trait member-array (ref-free, all-planets) | `decompile-verified` + `shipped` |
| `ID_52205` | — | CTDA condition EVALUATOR validating the trait belongs to the planet (read-only gate; **not** a writer) | `decompile-verified` |
| `ID_51735` | 0x14078aa30 | BGSPlanet::Manager::SetCurrentPlanet — writes `Manager+0x80 = planet FormID` verbatim | `decompile-verified` |
| `ID_102650` | — | Discover/create the `(938333\|planetId)` entry ref-free (recurses moons) | `decompile-verified` + `shipped` |
| `ID_97853` | — | Survey-progress notify / slate drop | `decompile-verified` + `shipped` |
| `ID_1016657` → `ID_97851` | — | Survey-% aggregator → weighted percent (`scanned_total/total_total`) | `decompile-verified` |
| `ID_58987` | — | Resource host accessor (disc `ID_938336`) | `decompile-verified` |
| `ID_51401` | 0x14076f4f0 | PNDT record loader (parses PPBD subrecords) | `decompile-verified` |
| `ID_83009` | 0x141307180 | ScannableComponent canonical id (`+0x24`); == authored id for all probed species | `decompile-verified` + `in-game` |
| `ID_83007` | — | Scan-target outline-read; **must be `!=0` before `ID_83008`** (fault guard) | `decompile-verified` |
| `ID_83008` | — | SetScanned (writes `939118+0x28`; credits `ID_52157` only on a 0→1 transition) | `decompile-verified` |
| `ID_90518` | 0x14159b890 | Scan-target panel painter (case-5 title reveal; writes the N/M digit from `ID_938422+0x38`) | `decompile-verified` |
| `ID_139363` | — | Global handle-table lookup (FormID → REFR, via `ID_883285`) | `decompile-verified` |

### 6c. Load-bearing struct field offsets

| Offset | Belongs to | Meaning | Confidence |
|---|---|---|---|
| `+0x8B0` | KnowledgeManager | → the knowledge DB | `decompile-verified` |
| `+0x268` | knowledge DB | survey-state component container (`938333`) | `decompile-verified` |
| `+0x300` | knowledge DB | spatial biome-overlay container (`938335`) | `decompile-verified` |
| `entry+0x20` | DB entry | subobj base; `*(u32*)` here = attribute-known bitmask (set `\|= 0x7`) | `decompile-verified` + `in-game` |
| `subobj+0x18` | subobj | species hashmap (keys) | `decompile-verified` |
| `subobj+0x40` | subobj | slots base (stride `0x30`) | `decompile-verified` |
| `subobj+0x48` | subobj | slots-end | `decompile-verified` |
| `subobj+0x60` | subobj | per-(planet,species) RB-tree region | `decompile-verified` |
| `slot+0x08` | species slot | `BSTArray<u32>` marker/catalogue array (panel + green completeness) | `decompile-verified` + `in-game` |
| `slot+0x20` | species slot | percent byte | `decompile-verified` |
| `slot+0x21` | species slot | scan-flag byte (the color boolean) | `decompile-verified` + `in-game` |
| `planetForm+0x54` | BGSPlanet | planet FormID == render key (identity, no converter) | `decompile-verified` + `in-game` |
| `Manager+0x80` | BGSPlanet::Manager | current-planet FormID (renderer fallback; `ID_937609` = the singleton) | `decompile-verified` |
| component `+0x28` | BGSScannable (`939118`) | scanned byte | `decompile-verified` + `save-verified` |
| component `+0x24` | BGSScannable | canonical id | `decompile-verified` |
| `ID_938422+0x38` | scanner-menu state | N/M tally byte (aim-bound) | `decompile-verified` |
| ScannableComponent vtable `[0x228]`/`[0x428]` | — | lifecycle/resolve slots | `decompile-verified` |

### 6d. Surface-tree / PlanetData offsets (1.16.236 live dump — CANDIDATE, lower confidence)

| Offset | Belongs to | Meaning | Confidence |
|---|---|---|---|
| `BGSPlanetData+0x38` | BGSPlanetData (PNDT, 0xBA) | `BGSSurface::Tree*` (**not** `+0x30` as CommonLibSF claims; `+0x30` is zero) | `in-game` (single-planet dump) |
| `Tree+0x54` | BGSSurface::Tree (SFTR, 0xB6) | back-reference to parent planet FormID | `in-game` (single-planet dump) |
| `Tree+0x68` | BGSSurface::Tree | opaque child/tile array (NOT BGSBiome pointers — see dead-ends) | `in-game` (single-planet dump) |

### 6e. Key authored FormIDs / keywords

| FormID | What |
|---|---|
| `0x00160C96` | FLST HandScannerPlantKeywords (flora marker catalog) |
| `0x00160C97` | FLST HandScannerActorKeywords (fauna marker catalog) |
| `0x0023E905` | AVIF HandScannerPlantReproduction (the func-14 PRPS input) |
| `0x0023E90D` | HandScannerAnyResource (universal flora+fauna marker) |
| `0x002634BE` | HandScannerAnyBiomes (universal marker) |
| `0x002634C2` | HandScannerActorHealth (fauna) |
| `0x0027A567` | LocRefType PlanetTraitScanTargetLocRef |
| `0x0022A2B6` | PRPS AV HandScannerTarget |
| `0x001CBEA3` | keyword for FindAllReferencesWithKeyword on scan-targets |
| `0x00225590` | PlanetTrait09EcologicalConsortium (forces Self-cloning) |
| `0x00225589` / `0x00225597` | PsychotropicBiota / AeriformLife (Spores drivers) |

---

## ★ Known dead-ends (do NOT re-pursue)

These were asserted at some point in the RE history and then **disproven**. Recorded so they are
not re-introduced as findings.

1. **`937887` is NOT a trait-known gate / store.** It is `CTProxyFormPtr` (proxy-form pointer), a
   sibling discriminator to `938333` at factory slot 0x2d vs 0x2f; neither gates the other. The
   "the mod must pre-write 937887 before MarkTraitKnown" lead was wrong — `ID_52155`→`938333` is
   already the complete, correct, ref-free trait path. (`decompile-verified`, `astro-trait-known-synthesis-2026-06-24.md`)

2. **The trait known-set is NOT `biome+0x20`** (the species known-set table). Live probe
   `ProbeKnownMember` reading `biome+0x20` (`*(937609+0x160)+0x20`) returned `found=false
   bucketCount=0` when aimed at a trait. `ID_83025`/`ID_83030`/`biome+0x20` are the wrong store for
   trait scan-targets. (`in-game`)

3. **`0x00013FE1` is NOT a discriminator** for a trait "known-set" record — it is a hash-value
   coincidence, not a real disc. (Ruled out 2026-06-24.)

4. **The trait scan-target N/M count does NOT read the `939118+0x28` byte we set.** Setting the byte
   leaves the count at 0/2. The count reads `ID_938422+0x38` (aim-bound) + the durable `938333`/known-set,
   not the transient byte. (`decompile-verified` + `in-game`, `panel-count-source-2026-06-23.md`)

5. **The in-world trait scan-target object is NOT all-planets / ref-free greenable.** Its render gate
   is the transient per-ref `938083`/`939118` (ref-keyed, materialization-bound), and the durable
   `938333` write only feeds the survey-% aggregator (gives "100% but BLUE / Unknown"), never the
   outline/count/identity. On-planet/loaded-ref only. (Triple-confirmed; this is the one piece that
   stays materialization-bound.)

6. **Off-planet/remote SPECIES green is NOT "impossible"** — that verdict was overturned. (Listed here
   only to neutralize the many stale "materialization-bound / two-domains / byte-poke-dead /
   off-planet-impossible" notes in the raw `re/` dumps for species. The species marker set is fully
   ESM-derived 17/17 and `+0x54` IS the render key. **Do not cite the old species "impossible" dumps
   as a live conclusion.**) Note the contrast: §4/dead-end-5 (the trait *object*) genuinely stays
   on-planet; species do not.

7. **`ID_83024` is NOT safely ref-free callable.** Calling `ID_83024(catalog, esmForm, &slot+0x08)`
   off a bare ESM form access-faults on the per-species condition (the runtime keyword/component data
   doesn't exist off-instance). Use the offline ESM derivation (§1) instead; if writing on-planet, use
   `ID_83025` (guarded by `ID_83007(ref)!=0`), never `ID_83024`. (`in-game`)

8. **The surface-tree `tree+0x58` "biome list" lead is dead** — the live dump shows `tree+0x58` is a
   value/form-id (`0x17C3A`), not a `BSTArray` data pointer. CommonLibSF's `BGSSurfaceTree
   BSTArray<uint32> @0x58` mapping is guessed/wrong; the biome→tile mapping is the opaque `tree+0x68`
   tile array. (`in-game` hex dump)

9. **No ref-free "planet → flora/fauna species list" runtime path exists.** Every enumerator
   (`ID_83010`/`ID_83012`/`ID_90531`) is land-gated (reads `db+0x300`, empty off-planet) or resolves
   the planet via `ID_52188(player)` (current-position-bound). The species list IS recoverable, but only
   by **statically parsing the PNDT/PPBD from the ESM** (§3.4), not from the live knowledge DB. (`in-game` + `decompile-verified`)

### Frida caveat

`re/frida/probe_scan.py` / `probe_durable_key.py` offsets are **mislabeled/unreliable** — re-derive
runtime addresses via REL::ID + the Address Library before any further Frida work, and never hook
per-frame functions (`ID_90513/90517/90523/90530` — hooking them crashed the game). The known
correct fix recorded: `ID_83025` true offset is `0x1309730` (not `0x309730`).

---

## How to regenerate the key evidence

- **Offline marker derivation / 17-species validation:** `python re/tools/esm_derive_markers.py --validate`
- **Compile + assert the real EsmReader 17/17:** `test/build_validate.bat` (compiles `test/ValidateMarkers.cpp` against the real `EsmReader.cpp`)
- **Save byte-diffs (need the user's Save12/13/14 etc.):** `python re/save/analyze_scan_count.py`, `re/save/compare_save21.py`, `re/save/verify_render_gate.py`
- **PNDT/PPBD extraction:** `python re/esm/extract_planet_species.py`
- **Offset skew across game versions:** `re/ghidra/scripts/offset_skew.py` + `offset_skew2.py`

See `re/README.md` for the full map of `re/`.
