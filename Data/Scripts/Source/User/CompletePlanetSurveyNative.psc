ScriptName CompletePlanetSurveyNative Hidden Native

; Native functions provided by CompletePlanetSurvey.dll (SFSE).

Function DebugLog(string asMsg) global native

; Mark a trait keyword as known for the planet. Fires the trait progress event.
bool Function MarkTraitKnownForPlanet(Form akPlanet, Keyword akKeyword) global native

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

; Green one species TYPE on an EXPLICIT target planet, using a live spawned instance as the
; handle (drives the engine's type-completion writer ID_52161 directly). The planet is an
; argument — so one spawned instance can green that species on ANY planet, not just the one the
; player is on. The target planet's survey entry must already exist (discover / MarkResources).
Function GreenTypeForPlanet(ObjectReference akRef, Form akPlanet) global native

; Drive the per-species COUNT completion (ID_52158) for an EXPLICIT target planet — the second
; half of the green (the tree write alone stayed blue; tree + count greened). `akRef` is a live
; spawned instance of the species. Used to validate the explicit-planet count before the galaxy
; loop, and is what GreenSpeciesEverywhere drives per planet internally.
Function CompleteTypeForPlanet(ObjectReference akRef, Form akPlanet, Form akSpecies) global native

; Enumerate every UNIQUE flora/fauna species across ALL planets (from the ESM PPBD data).
; Call once, then iterate 0..count-1 via GetAllSpeciesFormIdAt + Game.GetForm. The atomic galaxy
; green spawns one live instance per entry as the handle for GreenSpeciesEverywhere.
int Function EnumerateAllSpecies() global native
int Function GetAllSpeciesFormIdAt(int aiIndex) global native

; Green ONE species type on EVERY planet that hosts it, using `akRef` (a live spawned instance of
; that species) as the handle. Per planet it writes the tree (ID_52161) + drives the count
; completion (ID_52158), both with the planet explicit. Returns the number of planets greened.
int Function GreenSpeciesEverywhere(ObjectReference akRef, int aiSpeciesFormId) global native
