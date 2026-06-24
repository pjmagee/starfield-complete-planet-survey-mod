ScriptName CompletePlanetSurveyNative Hidden Native

; Native functions provided by CompletePlanetSurvey.dll (SFSE).

Function DebugLog(string asMsg) global native

; Mark a trait keyword as known for the planet. Fires the trait progress event.
bool Function MarkTraitKnownForPlanet(Form akPlanet, Keyword akKeyword) global native

; Map a PlanetTrait keyword -> its PlanetTraitScanTarget ACTI base form id (slot 0 or 1; 0 = none).
; The surface "scan target" objects whose loaded instances we FindAllReferencesOfType + SetScanned to
; green a planet's trait scan-targets on the surface.
int Function GetTraitScanTargetActi(Keyword akTraitKeyword, int aiSlot) global native

; TEST: write the species-style 938333 knowledge-DB entry (+0x21 scanned) for a trait scan-target's
; base ACTI, keyed (planet, ACTI). Tests whether the durable-KB write greens traits like species.
int Function MarkScanTargetScannedForPlanet(Form akPlanet, int aiActiFormId) global native

; ON-PLANET trait scan-target completion — the decompile-verified real-scan recipe (ID_90506):
;   ID_83008(ref,1,8,0) (green outline + N/M count + durable %/event) + ID_83025 identity reveal
;   (Unknown -> named). Mandatory guard ID_83007(ref)!=0 (a live 939118 component must exist).
; Pass a LOADED scan-target ref (found near the player). Returns pre-write state: -1 null, 0 skipped
; (no live component), 1 was-unscanned (now scanned), 2 already-scanned. Logs per-ref FormID/state/canon.
; Visual still needs a monocle repaint (look away/back) after the batch.
int Function CompleteTraitScanTargetRef(ObjectReference akRef) global native

; Complete ALL loaded trait scan-targets in range by walking the engine's GLOBAL 939118 registry (the
; EXACT store the outline/count read — ZERO "wrong instance" risk; FindAllReferencesWithKeyword can miss
; the rendered overlay instances, the registry cannot). For each registry REFR it guards ID_83007(ref)!=0,
; filters to trait scan-target ACTIs (keyword 0x001CBEA3), distance-gates against akPlayer when non-None,
; then writes the ref-keyed durable ID_938083 "seen" byte (entry+0xb9 = 1, decompile ID_57033) — the
; LocationManager store a real scan writes that the planet-keyed 938333 does NOT, so the in-world "0/N"
; clears on RELOAD. NO 939118 jam byte -> the hand-scanner is never bricked. ON-PLANET only (the ref must
; be materialized; a ref with no 938083 entry no-ops). Pass Game.GetPlayer() + radius (0 = all loaded).
; Returns the count whose 938083 seen-byte was set. Check the log for [trait-walk]/[locref-seen] lines.
int Function CompleteTraitScanTargetsInRange(ObjectReference akPlayer, float afRadiusUnits) global native

; PROBE: write +0x21/+0x20 directly (esm species key, no spawn/scan) for the planet.
; Run on the planet you're standing on to test if a direct write greens an existing entry.
; Returns species written (0 = no entry resolved).
; aiKind: 0 = both, 1 = flora (FLOR only), 2 = fauna (NPC_ only).
int Function TestDirectGreen(Form akPlanet, int aiKind) global native

; DECISIVE PROBE: call AFTER PlaceAtMe + SetScanned(true) + a short Wait on a live species
; instance. Logs authored vs base-form vs canonical(+0x24) id off the SAME ref, so we learn
; whether the canonical differs from the authored ESM id at all (and whether the create path
; stamped a real component). akAuthoredFid = the ESM formId originally placed.
Function ProbeScanKeys(ObjectReference akRef, int akAuthoredFid) global native

; PLANET-KEY FIX TEST: write +0x21 under the RENDER planet id (ID_52188(player)) instead of the
; form +0x54 id, for the current planet's species. If a save/reload then renders GREEN where
; TestDirectGreen was blue, the planet-key domain was the whole "100% but blue" bug. Returns
; species written.
int Function TestRenderKeyGreen(ObjectReference akPlayer, Form akPlanet) global native

; DEFINITIVE READ PROBE: calls the engine's OWN outline-green reader (ID_52159) for each authored
; species and returns how many it reports GREEN. Run on a CompleteSurvey'd (green) planet vs a
; TestDirectGreen'd (blue) one to see, from the engine itself, what the render actually reads.
int Function ProbeRenderRead(ObjectReference akPlayer, Form akPlanet) global native

; DUMP the raw per-species DB slot bytes (+ subobj header/tree region) for the planet. Run after
; TestDirectGreen (half) and after CompleteSurvey (full green+info) and diff the hex to find the
; "species catalogued/known" field the real scan writes that our byte-poke skips. Returns slots dumped.
int Function DumpSpeciesSlots(Form akPlanet) global native

; THE FIX (validation): engine-build the slot+0x08 attribute array for species whose +0x08 is empty
; (after a TestDirectGreen poke), pushing the 2 universal attribute ids. If they then render PROPERLY
; green after a reload, slot+0x08 is the gate and the build is sound. Returns slots built.
; aiKind: 0 = both, 1 = flora (FLOR only), 2 = fauna (NPC_ only).
int Function TestBuildArray(Form akPlanet, int aiKind) global native

; RESOURCES category (pure): mark the planet's attribute bits + resource scan flags via the
; ID_1016657 aggregator, EXCLUDING flora/fauna (species are greened only by the green path) and
; trait keywords. Fires the survey-complete event that drops the Survey Data slate at 100%.
int  Function MarkResourcesForPlanet(Form akPlanet, int aiDelta) global native

; Ensure a planet's knowledge entry exists (ref-free) via the engine discover ID_102650, so the
; subsequent ref-free writes (resources / species green) actually land on a NEVER-VISITED planet.
; ResolvePlanetSubobj is a pure lookup, so resources + green silently no-op until this runs. Also
; sets the surveyed bit, fires the Survey Data slate, and recurses moons. Returns 1 on success.
int  Function DiscoverPlanetEntry(Form akPlanet) global native

; DURABLE trait scan-target completion for ONE scan-target ACTI — the 938333 write a real scan makes
; (slot +0x21=2/+0x20=100 + trait keyword in the pooled subobj+0x08), byte-equal to a real 2/2 scan,
; WITHOUT the transient 939118 byte (the jammer). Ref-free, all-planets (entry must exist). A correct
; durable write removes the in-world "0/N scan required" state on reload. Returns 1 on write.
int  Function CompleteTraitObjectSlot(Form akPlanet, int aiActiFormId, Keyword akTraitKw) global native

; Enumerate every flora + fauna species form for the planet (across all biomes).
; Call once, then iterate 0..count-1 via GetPlanetSpeciesFormIdAt + Game.GetForm.
int  Function EnumeratePlanetSpecies(Form akPlanet) global native
int  Function GetPlanetSpeciesFormIdAt(int aiIndex) global native

; Bypass the scanner's per-species component check by calling the per-planet
; progress updater (ID_52157) directly on a ref. Required for spawn-and-scan
; of flora — PlaceAtMe'd refs lack the (939118, ref_formID) component that the
; standard scan path (ID_83038) checks for.
bool Function UpdatePlanetProgressForSpecies(ObjectReference akRef, Form akSpecies) global native

; Queue per-ref visual outline refresh. C++ polls a flag and runs a sweep on
; parentCell's references when menus are closed — outside the scanner UI's
; active state, avoiding a cell-iteration race.
int  Function ScanNearbyRefs() global native

; Queue a deferred CompleteSurvey dispatch. The scan hook calls this instead of
; invoking CompleteSurvey directly so PlaceAtMe doesn't race with the active
; scanner UI. C++ polls the flag, waits until the scanner is closed + grace
; period, then dispatches Papyrus CompleteSurvey from a clean state.
Function QueueCompleteSurvey() global native

; Cancel a pending auto-complete-on-scan dispatch. A manual completion command calls this first so
; an explicit category command (e.g. CompletePlanet "traits") is not overridden by a queued
; _AutoCompleteCurrentPlanet -> CompletePlanet("all") from an earlier real scan.
Function CancelPendingAutoComplete() global native

; Sweep every planet/moon in the galaxy and complete its survey ref-free (no
; teleport, no spawn): discover each, then write its attribute bits + species/
; resource scan flags. Records the planets it touched for the finalize pass.
; Returns the number of planets processed.
int Function CompleteAllPlanetsSurveyData() global native

; Accessors over the planets the last CompleteAllPlanetsSurveyData sweep touched,
; so the finalize pass can re-resolve each as a Planet (and mark its traits via
; GetKeywordTypeList(44) -> MarkTraitKnownForPlanet).
int Function GetSweepPlanetCount() global native
int Function GetSweepPlanetFormIdAt(int aiIndex) global native

; Fully complete one swept planet (by form ID): write its survey state (attribute
; bits + species/resource flags) and fire the survey-complete event so its Survey
; Data slate drops. Called from the finalize pass, which runs across later frames —
; by then the sweep's asynchronous knowledge-entry creates have flushed, so this
; also catches the few planets whose entry wasn't ready during the C++ pass.
; Returns the number of species/resource forms marked.
int Function FinalizeSweptPlanet(int aiFormId) global native

; Enumerate every UNIQUE flora/fauna species across ALL planets (from the ESM PPBD data).
; Call once, then iterate 0..count-1 via GetAllSpeciesFormIdAt + Game.GetForm. The atomic galaxy
; green spawns one live instance per entry as the handle for GreenSpeciesEverywhere.
int Function EnumerateAllSpecies() global native
int Function GetAllSpeciesFormIdAt(int aiIndex) global native

; Green ONE species type on EVERY planet that hosts it, using `akRef` (a live spawned instance of
; that species) as the handle. Per planet it writes the tree (ID_52161) + drives the count
; completion (ID_52158), both with the planet explicit. Returns the number of planets greened.
int Function GreenSpeciesEverywhere(ObjectReference akRef, int aiSpeciesFormId) global native

; --- Parameterized completion menu (read-only helpers) ---
; Enumerate the UNIQUE life-bearing planets (those with flora/fauna). Call once, then iterate
; 0..count-1 via GetLifePlanetFormIdAt + Game.GetForm(...) as Planet. The galaxy sweep skips living
; worlds, so CompleteLifePlanets uses this list to reach their traits.
int Function EnumerateLifePlanets() global native
int Function GetLifePlanetFormIdAt(int aiIndex) global native

; Parse a "resources,traits,fauna,flora" category string (base Papyrus can't split a string):
; returns true if the list contains asToken (case-insensitive) or the wildcard "all".
bool Function CategoryEnabled(string asCategoryList, string asToken) global native
