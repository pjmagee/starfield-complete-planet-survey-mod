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
