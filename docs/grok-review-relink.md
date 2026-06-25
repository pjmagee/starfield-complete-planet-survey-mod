<!-- grok-build (xhigh, read-only) review: relink - 2026-06-25 -->

**Audit Report: REL::ID / Relocation, struct offsets, and magic constants linkage to RE findings (read-only)**

Scope: `src/Main.cpp`, `src/EsmReader.cpp`, `include/*.h` (headers contain almost no numeric constants/RELs beyond the version block in Plugin.h). All analysis is against the current working tree. Citations use `file:line` for code and explicit `re/...` paths (or FINDINGS section) for RE docs.

### 1. Traceability Table (REL::ID / Relocation + key offsets + selected magic constants)

**REL::IDs (all in src/Main.cpp):**

| Symbol / ID | Code location | RE-doc source |
|-------------|---------------|---------------|
| GetKnowledgeManager {REL::ID(126578)} | Main.cpp:50 | FINDINGS.md:6b (GetKnowledgeManager) |
| SetTraitKnownNative {REL::ID(52155)} | Main.cpp:51 | FINDINGS.md:6b (ID_52155) + 2.1 |
| ScanRefNative {REL::ID(83008)} | Main.cpp:52 | FINDINGS.md:6b (ID_83008) |
| PlanetProgressNative {REL::ID(52157)} | Main.cpp:53 | FINDINGS.md:6b (ID_52157) |
| DbLookup {REL::ID(126806)} | Main.cpp:54 | FINDINGS.md:6b + q4-126806-confirm.txt |
| IncrementScanFlag {REL::ID(124898)} | Main.cpp:55 | FINDINGS.md:6b (ID_124898) + slot-0x08-catalogue-writer |
| SetPercentByte {REL::ID(124899)} | Main.cpp:56 | FINDINGS.md:6b (ID_124899) |
| TraitDiscriminator {REL::ID(938333)} | Main.cpp:57 | FINDINGS.md:6a (938333) + species-scan-complete-model |
| ScannableDiscriminator {REL::ID(939118)} | Main.cpp:70 | FINDINGS.md:6a (939118) + onp-resolver-2026-06-23.txt |
| LocRefDiscriminator {REL::ID(938083)} | Main.cpp:76 | FINDINGS.md:6a + loc-reflist / locref-* |
| ScannableRegistryBegin {REL::ID(126805)} | Main.cpp:84 | FINDINGS.md:6b + onp-resolver:2073 |
| ScannableRegistryAdvance {REL::ID(39372)} | Main.cpp:89 | onp-resolver-2026-06-23.txt:2163 (cited in comment) |
| TypeScanInner {REL::ID(52161)} | Main.cpp:109 | species-scan-complete-model + type-scan-inner.txt |
| PlanetProgressInner {REL::ID(52158)} | Main.cpp:121 | FINDINGS.md:6b (ID_52158) + real-scan-chain |
| SurveyAggregator {REL::ID(1016657)} | Main.cpp:131 | FINDINGS.md:6b (ID_1016657) + survey-aggregator.txt |
| SurveyBufferFree {REL::ID(65318)} | Main.cpp:132 | (paired with 1016657 in aggregator docs) |
| IsBiomeRef {REL::ID(83007)} | Main.cpp:137 | FINDINGS.md:6b (ID_83007) + trait-onplanet-completion |
| SurveyCheckNotify {REL::ID(97853)} | Main.cpp:145 | FINDINGS.md:6b (ID_97853) |
| ScanCompletePlanet {REL::ID(102650)} | Main.cpp:154 | FINDINGS.md:6b (ID_102650) + scan-complete.txt |
| ResolvePlanetFromRef {REL::ID(52188)} | Main.cpp:221 | FINDINGS.md:6b + planet-id-stamp-site-2026-06-23.md |
| RenderGreenRead {REL::ID(52159)} | Main.cpp:241 | FINDINGS.md:6b (ID_52159) + species-scanned-check.txt |
| SpeciesSlotHash {REL::ID(124901)} | Main.cpp:255 | FINDINGS.md:6b (ID_124901) |
| BSTArrayU32Grow {REL::ID(35755)} | Main.cpp:262 | slot-0x08-* + species-scan-complete-model |
| EngineScalarAlloc {REL::ID(35770)} | Main.cpp:268 | slot-0x08-catalogue-writer (allocator paths) |
| EngineScalarFree {REL::ID(35771)} | Main.cpp:273 | same |
| Singleton937609 {REL::ID(937609)} | Main.cpp:301 | species-scan-complete-model (live biome) + stamp-xrefs |
| FindBiomeMember {REL::ID(56887)} | Main.cpp:307 | slot-0x08-catalogue-writer + real-scan-chain |
| GetCanonicalSpeciesNative {REL::ID(83009)} | Main.cpp:375 | FINDINGS.md:6b (ID_83009) + scan-component-lifecycle |
| RevealKnownNative {REL::ID(83025)} | Main.cpp:392 | trait-onplanet-completion-2026-06-23.md |
| ResolveCanonicalForm {REL::ID(83006)} | Main.cpp:575 | scan-inner.txt + canonical-chain |
| AllFormsMapHolder {REL::ID(883341)} | Main.cpp:948 | (form registry walk; indirect in native-impls / form-lookup) |
| Hook sites (outer 52157, inner 97853) | Main.cpp:1789-1790 | FINDINGS 6b + Hook comments cite 52157/97853 |

**Core struct offsets / layout constants (Main.cpp unless noted):**

| Offset/constant | Code location | RE-doc source |
|-----------------|---------------|---------------|
| kPlanetIdOffset = 0x54 | Main.cpp:157 | FINDINGS.md:6c (planetForm+0x54) |
| kManagerDbOffset = 0x8B0 | Main.cpp:158 | FINDINGS.md:6c + knowledge-db |
| kDbContainerOffset = 0x268 | Main.cpp:159 | FINDINGS.md:6c (db+0x268) |
| kBucketOffsetTableOff = 0x12 | Main.cpp:160 | onp-resolver + q4-126806 (entryOff calc) |
| kEntrySubobjOffset = 0x20 | Main.cpp:161 | FINDINGS 6c (entry+0x20 == subobj) + ResolvePlanetSubobj comment |
| kFormPtrFormIdOffset = 0x28 | Main.cpp:162 | aggregator docs |
| subobj+0x00 (attribute bitmask, `\|= 0x7`) | Main.cpp:606,632,642 (SetPlanetAttributeBits) | FINDINGS 3.1 + survey-percent.txt |
| subobj+0x18 / +0x40 / +0x48 (species hashmap + slots base/end) + stride 0x30 | Main.cpp:605-606,1285-1287,1335 (`ResolvePlanetSubobj`, TestBuildArray) | species-scan-complete-model + slot-0x08-catalogue-writer (slot map) |
| slot+0x08 / +0x10 / +0x18 (BSTArray header) | Main.cpp:280-281,1369-1371 (PushSpeciesAttr + clear) | FINDINGS 6c + slot-0x08-* docs |
| slot+0x20 / +0x21 | Main.cpp:659-660,1357 (MarkSpeciesScannedForPlanet + idempotency) | FINDINGS 1.10/1.11/6c |
| kReg* (0x12, 0x10, 0x20, 0x28, 0xfe0, key masks) for 939118 walk | Main.cpp:94-99,509-522 | onp-resolver-2026-06-23.txt:2078-2121 (explicitly cited in comment) |
| kDbLookupNotFound = 0xfe0 | Main.cpp:166 | 126806 decomp (multiple) |
| Aggregator spans (0x1e8/0x1f0, 0x200/0x208, 0x218/0x220, 0x230/0x238) | Main.cpp:188-194 | survey-aggregator.txt + buffer-populator |
| Cell BSTArray (0x080/0x084/0x088) | Main.cpp:178-180 | ScanAllRefsInCell comment + cell layout notes |
| Form map slot stride 0x18, value@+0x08, next@+0x10 | Main.cpp:972-974 | form-lookup.txt + native-impls |
| Biome member stride 0x28, markers +0x08..+0x10 | Main.cpp:330-332 | slot-0x08-catalogue-writer + live biome docs |
| kX86Call* (0xE8, length 5) | Main.cpp:183-184 | Hook::FindCallSite (generic) |

**EsmReader.cpp key magic constants (primarily ESM layout + catalog):**

- 4CC signatures (kSigPNDT etc.), kCompressedFlag=0x00040000, kSub* (OBTS/DATA/NKEY etc.) — derived from ESM dump tools (re/esm/* + extract_planet_species.py); not function RELs.
- kCatalogFlora=0x00160C96, kCatalogFauna=0x00160C97 — heavily documented in species-scan-complete-model-2026-06-23.md §1.1 + FINDINGS 1.2 + esm-marker-derivation.
- kAvifPlantReproduction=0x0023E905, kFunc* CTDA (14/560/837/858/882), kCtda* offsets, kObtsEntriesOff=0x12, kPrpsTripleStride=12 — directly from species-scan-complete-model + esm_derive_markers.py (tool-validated 17/17).
- Ability markers (0x002634BF/C0/C1) + kKw* scanner keywords — fauna-ability-markers-2026-06-23.md + FINDINGS 1.8.
- Universal markers 0x0023E90D / 0x002634BE — cited in complete-scan-green-model + fallback code path (Main.cpp:1346).

### 2. UNDOCUMENTED / Weakly Traced Items

- **Registry walk kReg* constants** (Main.cpp:94-101): Explicitly cite onp-resolver-2026-06-23.txt in comments, but the precise numeric values (0x12/0x10/0x20/0x28/0xfe0 + 96-byte scratch) are not restated in FINDINGS §6. They live only in the .txt decompile.
- **Aggregator buffer spans** (Main.cpp:188-194): Used in ForEachAggregatedFormId. Paired with ID_1016657 in survey-*/aggregator-*.txt, but exact byte offsets inside the 0x400 buffer are not enumerated in FINDINGS §6c.
- **Form-map slot layout** (0x18 stride, +0x08 value, +0x10 next; Main.cpp:972-974): Described in comment; no direct FINDINGS table entry (only general form-lookup references).
- **Cell BSTArray offsets** (0x080 etc.): Local comment only; not in FINDINGS §6.
- **Biome member stride 0x28 + marker window +0x08..+0x10**: Used in ReadLiveMemberMarkers / probes. Present in slot-0x08-catalogue-writer but not a top-level FINDINGS entry.
- **EsmReader internal CTDA/OBTS/PRPS strides and kCtda* / kObts* offsets**: Fully consistent with the Python reference (esm_derive_markers.py) and species-scan-complete-model, but the byte offsets themselves are not listed in FINDINGS §6 (they are ESM subrecord layout, not runtime REL/offsets).
- **kHandscannerHighlightRangeKw (0x001CBEA3)** and some trait scan-target consts: Lightly referenced (trait authoring docs) but not a core listed ID in FINDINGS.
- **ID_64338** (mentioned in user query and code comment at Main.cpp:569): Appears only as a parenthetical gate note for ResolveCanonicalForm; not enumerated in FINDINGS §6b table.

All the high-profile IDs the query listed (52155/57/58, 97853, 102650, 124898/899, 1016657, 124901, 83009/07, 938333, etc.) + the subobj/slot offsets + universal markers + catalog FLSTs + 0x0023E90D/0x002634BE **are** documented (directly or via the models that cite the decompiles).

### 3. Comment / Claim Consistency with RE Docs

**Green model (core focus of query):**
- Code at Main.cpp:1312: "green needs +0x21 AND +0x08 on ONE slot".
- Code at Main.cpp:1351-1361 (idempotency): checks `flagByte != 0 && curCount == markers.size()` before skipping; clears +0x08/+0x10/+0x18 and rebuilds via PushSpeciesAttr.
- Comments at 1264-1268, 1293-1295, 1380 correctly describe the current practical path (ESM-derived markers into +0x08 + +0x21) and the NotifySurveyProgress repaint.
- This matches the **later practical/in-game model** ("write BOTH", FINDINGS 1.11 + complete-scan-green-model-2026-06-22.md §2.1).
- However, some older-style comments remain:
  - Main.cpp:233-234 ("ID_52159: the OUTLINE renderer's OWN green read — returns the +0x21 byte... ID_90491/ID_90548 use to decide green-vs-blue") — accurate to the single-function decompile (species-scanned-check.txt / species-scan-complete-model) but incomplete per the in-game arbiter that requires +0x08 for proper outline + panel + ability creatures.
  - Similar phrasing around "ID_52159's +0x21" in 237-238 and 930.
- The "100% but blue" bug comments (Main.cpp:216-218) describe the old planet-key-domain hypothesis. That hypothesis was **superseded** (render-read-target-2026-06-22.md explicitly says "SUPERSEDED... planet key is identity"; FINDINGS 1.12). The comments are stale relative to later RE.

**Other:**
- ResolvePlanetSubobj comment (Main.cpp:604-606) accurately describes subobj layout and +0x20/+0x21 per species-scan-complete-model.
- CanonicalFormId / 83006/83009 comments correctly note the shift away from the "remap" theory (FINDINGS 1.13 + in-game ProbeScanKeys).
- No hand-written BSTArray cap/size pokes (good — aligns with "Heap-corruption history" in CLAUDE.md).
- EsmReader comments (marker derivation, 17/17, catalog FLSTs, func numbers, universal markers) are consistent with species-scan-complete-model and the Python reference.

**Stale/reverted-approach comments:** The planet-key-domain story and some "ID_52159 alone = green" phrasing are the clearest remnants. The code **behavior** (TestBuildArray always ensures both fields for the green path; idempotency guards on both) is aligned with the current practical rule.

### 4. Internal Consistency of the re/ Docs on the Green Model

**There is a documented evolution/contradiction that the docs themselves surface:**

- slot-0x08-catalogue-writer-2026-06-22.md (earlier): "GREEN = slot+0x21 alone... slot+0x08 is info panel / on-screen repaint. ID_52159 reads only +0x21."
- species-scan-complete-model-2026-06-23.md (★★ predicate): Still leads with "+0x21 != 0 alone" (via ID_52159) but adds the practical note to "WRITE BOTH".
- complete-scan-green-model-2026-06-22.md §2.1 explicitly calls out the contradiction: the "+0x21 alone" + "slot+0x08 = info only" claims from the catalogue-writer doc are **DISPROVEN in-game**. TestDirectGreen (+0x21 only) left blue; +0x08 build made proper green (outline + info). "ID_52159's +0x21 read is the half-scan / % signal, not the outline gate."
- FINDINGS.md 1.10 repeats the "slot+0x21 != 0 alone" phrasing (citing species-scan-complete-model). 1.11 immediately follows with "**Practical green rule = write BOTH**" (citing in-game + slot-0x08-catalogue-writer). This is an internal tension the curated doc acknowledges but does not fully collapse.

**Planet key:** render-read-target-2026-06-22.md opens with "SUPERSEDED 2026-06-23 — the H2 'WRONG PLANET KEY' VERDICT IS REFUTED". FINDINGS 1.12 and planet-id-stamp-site reflect the final identity result. Older text in some dumps still carries the prior hypothesis.

**Code vs. RE:** The current implementation (writes +0x21 via the ID_124898/124899 path for every species in MarkEsmSpeciesForPlanet, then builds the full +0x08 set in TestBuildArray using Esm::GetSpeciesMarkers + actor markers, with idempotency on both fields) follows the **later in-game/practical rule**. It does **not** treat +0x08 completeness as the *sole* runtime gate for the +0x21 write itself; it ensures both are present for the species green path.

No other major contradictions were found in the core surveyed constants (discriminators, subobj layout, stride, catalog FLSTs, universal markers, and the listed REL IDs are stable across FINDINGS §6 and the model docs).

**End of report.** All analysis used only reads (no edits, no builds, no in-game execution).
