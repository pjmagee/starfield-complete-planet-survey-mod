<!-- grok-build (xhigh, read-only) dead-code audit — 2026-06-25. Independent call-graph analysis; NOT yet acted on. Verify each item before removing. -->

**## Safe to remove**

**Table: Symbol | Kind | file:line | Why dead (no caller, or only-dead callers — NAME the would-be callers) | Confidence**

| Symbol | Kind | file:line | Why dead | Confidence |
|--------|------|-----------|----------|------------|
| `GetRenderPlanetId` | C++ helper | src/Main.cpp:223 | Defined; body calls only `ResolvePlanetFromRef`. Never invoked by any live path (no call from Papyrus natives, Hook poller, Phase1 sweep, green/resource paths, `CanonicalFormId`, `MarkEsm*`, `ForEachAggregated*`, `ScanAllRefsInCell`, flag pollers, or `DispatchPapyrusStatic`). | high |
| `ReadRenderGreen` | C++ helper | src/Main.cpp:243 | Defined; uses `RenderGreenRead`. No callers anywhere in live graph. | high |
| `ReadLiveMemberMarkers` | C++ helper | src/Main.cpp:311 | Defined; uses `Singleton937609` + `FindBiomeMember`. Never called. | high |
| `UpdatePlanetProgress` | C++ helper | src/Main.cpp:358 | Defined; uses `PlanetProgressNative`. Only called from nowhere (comment at 716 references the signature but no call site). | high |
| `GetCanonicalSpeciesId` | C++ helper | src/Main.cpp:377 | Defined; uses `GetCanonicalSpeciesNative`. Callers: only `RevealScanTargetIdentity:437` and `ProbeScanTargetKnownSet:461` (both dead). | high |
| `ProbeKnownMember` | C++ helper (diag) | src/Main.cpp:398 | Defined + internal `FindBiomeMember`. Only called from `ProbeScanTargetKnownSet:464` (dead). | high |
| `RevealScanTargetIdentity` | C++ helper | src/Main.cpp:427 | Defined; uses `Singleton937609`, `GetCanonicalSpeciesId`, `RevealKnownNative`. No callers in live code. | high |
| `ProbeScanTargetKnownSet` | C++ helper (diag) | src/Main.cpp:451 | Defined; uses dead `GetCanonicalSpeciesId` + `ProbeKnownMember`. No callers. | high |
| `ForEachScannableInRegistry` | C++ helper (template) | src/Main.cpp:479 | Defined; uses `Scannable*` RELs + `kReg*` consts + `GetKnowledgeDB`. No callers from any live entry (papyrus, hooks, poller, sweep). | high |
| `GreenTypeForPlanet` | C++ helper | src/Main.cpp:672 | Defined; uses `TypeScanInner`. Only caller: `GreenAndCompleteTypeForPlanet:729` (dead). | high |
| `CompleteTypeForPlanet` | C++ helper | src/Main.cpp:698 | Defined; uses `PlanetProgressInner`. Only caller: `GreenAndCompleteTypeForPlanet:730` (dead). | high |
| `GreenAndCompleteTypeForPlanet` | C++ helper | src/Main.cpp:727 | Defined; calls the two above. No external callers (no Papyrus binding, no call from `Mark*`, `Complete*`, `Test*`, sweep, poller, or hook paths). | high |
| `PlanetProgressNative` (ID_52157) | REL::Relocation | src/Main.cpp:53 | Only used in dead `UpdatePlanetProgress:362`. | high |
| `ResolvePlanetFromRef` (ID_52188) | REL::Relocation | src/Main.cpp:221 | Only used in dead `GetRenderPlanetId:229`. | high |
| `RenderGreenRead` (ID_52159) | REL::Relocation | src/Main.cpp:241 | Only used in dead `ReadRenderGreen:247`. | high |
| `ScannableDiscriminator` (ID_939118) | REL::Relocation | src/Main.cpp:70 | Only used inside dead `ForEachScannableInRegistry:488`. | high |
| `LocRefDiscriminator` (ID_938083) | REL::Relocation | src/Main.cpp:76 | Declared; **zero** references (`.get()` or otherwise) in any code. | high |
| `ScannableRegistryBegin` (ID_126805) | REL::Relocation | src/Main.cpp:84 | Only used inside dead `ForEachScannableInRegistry:497`. | high |
| `ScannableRegistryAdvance` (ID_39372) | REL::Relocation | src/Main.cpp:89 | Only used inside dead `ForEachScannableInRegistry:550`. | high |
| `TypeScanInner` (ID_52161) | REL::Relocation | src/Main.cpp:109 | Only used in dead `GreenTypeForPlanet:689`. | high |
| `PlanetProgressInner` (ID_52158) | REL::Relocation | src/Main.cpp:121 | Only used in dead `CompleteTypeForPlanet:720`. | high |
| `Singleton937609` (ID_937609) | REL::Relocation | src/Main.cpp:301 | Used only inside dead fns: `ReadLiveMemberMarkers:316`, `RevealScanTargetIdentity:431`, `ProbeScanTargetKnownSet:455`. | high |
| `FindBiomeMember` (ID_56887) | REL::Relocation | src/Main.cpp:307 | Used only inside dead fns (see above + `ProbeKnownMember:404`). | high |
| `GetCanonicalSpeciesNative` (ID_83009) | REL::Relocation | src/Main.cpp:375 | Only used in dead `GetCanonicalSpeciesId:381`. | high |
| `RevealKnownNative` (ID_83025) | REL::Relocation | src/Main.cpp:392 | Only used in dead `RevealScanTargetIdentity:444`. | high |
| `EngineScalarAlloc` (ID_35770) | REL::Relocation | src/Main.cpp:268 | Declared; zero uses (`.get()` or call). (Contrast: `BSTArrayU32Grow` is live.) | high |
| `EngineScalarFree` (ID_35771) | REL::Relocation | src/Main.cpp:273 | Declared; zero uses. | high |
| `kRegOffsetTableBase` / `kRegEntryKeyOffset` / `kRegEntryFormId` / `kRegEntryStateByte` / `kRegEndIndex` / `kRegKeyLowMask` / `kRegScratchBytes` / `kRegMaxIterations` | file-static consts | src/Main.cpp:94-101 | All only referenced inside dead `ForEachScannableInRegistry` body and its comments. | high |
| `kHandscannerHighlightRangeKw` | file-static const | src/Main.cpp:102 | Declared; zero references (value appears in Papyrus via `Game.GetForm` but C++ symbol is unused). | high |
| `GetSpeciesBiomeCount` (decl + impl) | C++ API + fn | include/EsmReader.h:30; src/EsmReader.cpp:1039 | Declared and defined. Never called from any .cpp (live paths or otherwise) in src/. Only consumer of `g_biomeCounts`. | high |
| `g_biomeCounts` + population sites | file-static global + code | src/EsmReader.cpp:1007 (decl), 834-835 (`if (counts[k] < 0xFF) ++counts[k];`), 919 (BuildMap param), 1019 (call) | Populated in live-called `ParsePndtGroup`/`BuildMap`, but data is **only read** by the dead `GetSpeciesBiomeCount`. The count tracking and map serve no live purpose. | med — verify (population is side-effect of a live parse path; removing requires excising the counting lines + map + using alias) |
| `SpeciesBiomeCount` using alias (comment claims "green build needs") | type alias | include/EsmReader.h:23 | Only used for the dead biome-count API + internal dead data flow. | high (once getter removed) |

**No dead Papyrus**:
- All reachable roots/helpers in `CompletePlanetSurveyQuest.psc` (CompletePlanet:49, CompleteBarrenPlanets:111, CompleteLifePlanets:205, CompleteAllPlanets:279, CompleteSurveyIfEnabled:293, _AutoCompleteCurrentPlanet:397, _GreenPlanet:24, _SpeciesKind:35, MarkTraits:318, _CompleteTraitScanObjects:346, _WantsSpecies:391) are live per the spec.
- All 16 native decls in `CompletePlanetSurveyNative.psc` (DebugLog through CategoryEnabled) satisfy the three conditions: bound in `Papyrus::Register` (Main.cpp:1540-1608) + declared + called from the reachable Papyrus fns above (via `CompletePlanetSurveyNative.XXX`).

**No dead bound natives** (all satisfy the CRITICAL binding rule; Papyrus name vs C++ fn name differences already resolved correctly, e.g. "GetLifePlanetFormIdAt" ↔ `GetLifePlanetAt`).

**No dead code in `src/EsmReader.cpp` internals** beyond the biome-count items above — `GetPlanetSpecies`, `GetSpeciesMarkers`, `GetSpeciesActorMarkers`, `BuildMarkers`/`Parse*`/`Eval*` etc. are all reached from live green + sweep paths.

**Transitive chains** (exact reachability proof — only dead callers):

- `GreenTypeForPlanet` (672) ← `GreenAndCompleteTypeForPlanet` (729) ← (nothing)
- `CompleteTypeForPlanet` (698) ← `GreenAndCompleteTypeForPlanet` (730) ← (nothing)
- `GreenAndCompleteTypeForPlanet` (727) ← (nothing live)
- `ForEachScannableInRegistry` (479) + `Scannable*` RELs + all `kReg*` (94-101) ← `ProbeScanTargetKnownSet` (451) + `Reveal...` comments only ← (nothing)
- `RevealScanTargetIdentity` (427) + `GetCanonicalSpeciesId` (377) + `ProbeKnownMember` (398) + `ReadLiveMemberMarkers` (311) + `Singleton937609`/`FindBiomeMember`/`GetCanonicalSpeciesNative`/`RevealKnownNative` ← (nothing live)
- `GetRenderPlanetId` (223) + `ResolvePlanetFromRef` (221) ← (nothing)
- `ReadRenderGreen` (243) + `RenderGreenRead` ← (nothing)
- `UpdatePlanetProgress` (358) + `PlanetProgressNative` (53) ← (nothing; 716 is a comment)
- `EngineScalarAlloc/Free` ← (nothing)
- `LocRefDiscriminator` ← (nothing)
- `GetSpeciesBiomeCount` (EsmReader.cpp:1039) + `g_biomeCounts` + increments (834) ← (nothing)

All other references to these symbols are definitions, comments, or the dead chains above.

## DO NOT REMOVE (explicit safety list)

- `TestDirectGreen` (Main.cpp:1218, bound at 1552) + `TestBuildArray` (1250, bound at 1555): **LIVE production green path**. Called by `_GreenPlanet` (Quest.psc:25-26) which is called by `CompletePlanet`, `CompleteLifePlanets`, etc. Names are historical (per spec).
- `CanonicalFormId` (Main.cpp:577): LIVE — called by `MarkEsmSpeciesForPlanet:925` (green path) and `TestBuildArray:1301`.
- `GetSpeciesToPlanets` (736): LIVE — called by `EnumerateLifePlanets:1476` (CompleteLifePlanets path).
- `ForEachAggregatedFormId` (753) + `kAgg*` consts (188-194): LIVE — reached via `MarkEverythingForPlanet:816` → resources + `CompletePlanetSurveyState` + barren sweep finalize.
- `MarkEsmSpeciesForPlanet` (910), `MarkSpeciesScannedForPlanet`, `WritePlanetSurveyState`, `CompletePlanetSurveyState`, `NotifySurveyProgress`, `SetPlanetAttributeBits`, `ResolvePlanetSubobj`, `SpeciesMatchesKind`, `PushSpeciesAttr`, `SpeciesSlotHash`, `BSTArrayU32Grow`: all shared by live green + resources paths.
- `ScanAllRefsInCell` (1075) + `IsBiomeRef` + `ScanRefNative`: reached from live poller.
- `CompleteAllPlanetsSurveyData_Phase1` (999) + `ForEachFormOfType` + `AllFormsMapHolder` + sweep globals: live barren path.
- All RELs and helpers used by the above (GetKnowledgeManager, DbLookup, ScanCompletePlanet, SurveyAggregator, etc.).
- `g_*` flags/caches, `g_degraded`, `ApplyInstantScanGameSettings`, `Hook::Install`/`InstallScanSweepPoller`, `Papyrus::Register`, `DispatchPapyrusStatic`, `MessageCallback`, `SFSE_PLUGIN_LOAD`: all root setup or live flag paths.
- `GetSpeciesMarkers` / `GetSpeciesActorMarkers` + entire marker derivation (EsmReader): live via TestBuildArray.
- `kBiomeScanCategory`, `kDefaultScanDelta`, `kX86*`, `kCell*`, etc.: used by live code.

## Suggested removal order (leaves-first)

1. Dead leaf fns + their immediate private helpers (no other live deps): `GetRenderPlanetId`/`ReadRenderGreen`/`ReadLiveMemberMarkers`/`UpdatePlanetProgress`/`GetCanonicalSpeciesId`/`ProbeKnownMember`/`RevealScanTargetIdentity`/`ProbeScanTargetKnownSet`/`ForEachScannableInRegistry` + `GreenTypeForPlanet`/`CompleteTypeForPlanet`/`GreenAndCompleteTypeForPlanet`.
2. Dead REL decls that become unreferenced after (1): `PlanetProgressNative`, `ResolvePlanetFromRef`, `RenderGreenRead`, `GetCanonicalSpeciesNative`, `RevealKnownNative`, `Scannable*` trio, `LocRefDiscriminator`, `Singleton937609`, `FindBiomeMember`, `EngineScalarAlloc`/`EngineScalarFree`, `TypeScanInner`, `PlanetProgressInner`.
3. Dead file-static consts: the entire `kReg*` block (94-101), `kHandscannerHighlightRangeKw` (102).
4. Esm biome dead API + data: remove `GetSpeciesBiomeCount` decl/impl, `SpeciesBiomeCount` alias (if unused after), `g_biomeCounts`, and the counting block inside `ParsePndtGroup` (834-835) + `BuildMap`/`EnsureParsed` sites that only exist for it. (Header comment at EsmReader.h:22 can be cleaned as drive-by.)
5. Any resulting dead local structs/ctx types inside the removed fns (TypeScanCtx, ProgressCtx usage, etc.) and unused includes (none obvious).
6. (Optional later) dead comments referencing the removed items.

After each step the remaining code still compiles because the removed items have no live incoming edges. All live entry points and the full Papyrus<->C++ binding graph remain untouched.

This audit was performed by exhaustive reading of src/Main.cpp (full 1-1834), src/EsmReader.cpp (full 1-1089), include/*.h, and both .psc sources, plus targeted call/REL/identifier searches. Reachability was walked from the exact live roots listed in the query. No file was edited.
