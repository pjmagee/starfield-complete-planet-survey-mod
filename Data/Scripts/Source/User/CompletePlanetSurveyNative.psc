ScriptName CompletePlanetSurveyNative Hidden Native

; Native functions provided by CompletePlanetSurvey.dll (SFSE).

Function DebugLog(string asMsg) global native

; Mark a trait keyword as known for the planet. Fires the trait progress event.
bool Function MarkTraitKnownForPlanet(Form akPlanet, Keyword akKeyword) global native

; PROBE: write +0x21/+0x20 directly (esm species key, no spawn/scan) for the planet.
; Run on the planet you're standing on to test if a direct write greens an existing entry.
; Returns species written (0 = no entry resolved).
int Function TestDirectGreen(Form akPlanet) global native

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

; Mark every form the engine tracks for the planet (flora/fauna/resources/traits)
; as scanned via the ID_1016657 aggregator. Also fires the survey-complete event
; that drops the Survey Data slate when the planet hits 100%.
int  Function MarkResourcesForPlanet(Form akPlanet, int aiDelta) global native

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
