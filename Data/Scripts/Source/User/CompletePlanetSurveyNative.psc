ScriptName CompletePlanetSurveyNative Hidden Native

; Native functions provided by CompletePlanetSurvey.dll (SFSE).

bool Function MarkFormKnownForPlanet(Form akPlanet, Form akForm) global native
bool Function MarkTraitKnownForPlanet(Form akPlanet, Keyword akKeyword) global native
Function DebugLog(string asMsg) global native
int Function RegisterSpeciesScan(Form akSpecies) global native
bool Function ScanRef(ObjectReference akRef) global native
bool Function UpdatePlanetProgressForSpecies(ObjectReference akRef, Form akSpecies) global native
bool Function MarkSpeciesScannedForPlanet(Form akPlanet, Form akSpecies, int aiDelta) global native
int  Function MarkEverythingForPlanet(Form akPlanet, int aiDelta) global native
Function DumpPlanetLayout(Form akPlanet) global native
int  Function MarkAllBiomeSpecies(Form akPlanet, int aiDelta) global native
int  Function ScanNearbyRefs() global native
int  Function MarkLocationsExplored() global native
