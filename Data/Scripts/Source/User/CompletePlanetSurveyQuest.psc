ScriptName CompletePlanetSurveyQuest

; Complete the planet survey for the WHOLE planet (all biomes).
; Invoke via console:  cgf "CompletePlanetSurveyQuest.CompleteSurvey"
;
; Also auto-fires on any in-game scan when the Settings > Gameplay toggle is on
; (see CompleteSurveyIfEnabled and the C++ scan hook at ID_52157 → ID_97853).
;
; Flow:
;   1. Mark traits via the DB-level trait native (ID_52155).
;   2. Mark resources + aggregator catch-all (ID_1016657 → ID_52156 per form).
;      Fires PlayerPlanetSurveyCompleteEvent so the Survey Data slate drops.
;   3. For each flora + fauna species across ALL biomes, PlaceAtMe a disabled ref,
;      SetScanned + UpdatePlanetProgressForSpecies (bypasses ID_83038 component
;      check that no-ops PlaceAtMe'd flora), then delete. Fires BIOME COMPLETE
;      for every biome on the planet.
;   4. Per-ref outline refresh for in-world refs (flips blue → scanned colour).

Function CompleteSurvey() global
    Actor playerRef = Game.GetPlayer()

    If playerRef.IsInInterior()
        Debug.Notification("Survey: exit your ship first")
        Return
    EndIf

    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.Notification("Survey: not on a planet")
        Return
    EndIf

    float surveyBefore = currentPlanet.GetSurveyPercent()
    Form  planetForm   = currentPlanet as Form
    ObjectReference playerRef_OR = playerRef as ObjectReference

    Keyword[] traitKw      = currentPlanet.GetKeywordTypeList(44)
    int       traitCount   = MarkTraits(currentPlanet, traitKw)
    int       resourceCount = CompletePlanetSurveyNative.MarkResourcesForPlanet(planetForm, 100)
    int       speciesCount = SpawnAndScanAllPlanetSpecies(planetForm, playerRef_OR)
    CompletePlanetSurveyNative.ScanNearbyRefs()

    float surveyAfter = currentPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("CompleteSurvey: traits=" + traitCount + " resources=" + resourceCount + " species=" + speciesCount + " survey=" + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
EndFunction

; Called by the C++ scan hook on every species/resource scan. Reads the
; Settings > Gameplay toggle, short-circuits if disabled or planet already
; complete, then delegates to CompleteSurvey.
Function CompleteSurveyIfEnabled() global
    ; FormID 0x80C assigned by Creation Kit to GPOF CPSScanAutoComplete.
    ; Verify in xEdit if the ESM is ever regenerated — CK reassigns IDs.
    Form gpofForm = Game.GetFormFromFile(0x80C, "CompletePlanetSurvey.esm")
    GameplayOption gpofOption = gpofForm as GameplayOption
    If gpofOption == None || gpofOption.GetValue() < 0.5
        Return
    EndIf

    Planet currentPlanet = Game.GetPlayer().GetCurrentPlanet()
    If currentPlanet != None && currentPlanet.GetSurveyPercent() >= 1.0
        Return
    EndIf

    CompleteSurvey()
EndFunction

; Spawn one disabled ref of every flora + fauna species the engine tracks for
; this planet (across all biomes, via the aggregator), scan each to register
; biome progress, then delete. Refs stay disabled so there's no visual flicker.
;
; Capped at 128 species to bound runtime on dense planets.
int Function SpawnAndScanAllPlanetSpecies(Form planetForm, ObjectReference playerRef_OR) global
    int total = CompletePlanetSurveyNative.EnumeratePlanetSpecies(planetForm)
    If total == 0
        Return 0
    EndIf

    ObjectReference[] spawned = new ObjectReference[128]
    int spawnCount = 0
    int i = 0
    While i < total && spawnCount < 128
        int  speciesFid  = CompletePlanetSurveyNative.GetPlanetSpeciesFormIdAt(i)
        Form speciesForm = Game.GetForm(speciesFid)
        If speciesForm != None
            ObjectReference ref = playerRef_OR.PlaceAtMe(speciesForm, 1, false, true, true, None, None, true)
            If ref != None
                spawned[spawnCount] = ref
                spawnCount += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    ; SetScanned drives ID_83008 (fauna works). UpdatePlanetProgressForSpecies hits
    ; ID_52157 directly — required for flora whose ID_83038 no-ops on PlaceAtMe'd refs.
    i = 0
    While i < spawnCount
        If spawned[i] != None
            spawned[i].SetScanned(true)
            Form baseForm = spawned[i].GetBaseObject()
            If baseForm != None
                CompletePlanetSurveyNative.UpdatePlanetProgressForSpecies(spawned[i], baseForm)
            EndIf
        EndIf
        i += 1
    EndWhile

    i = 0
    While i < spawnCount
        If spawned[i] != None
            spawned[i].Disable(false)
            spawned[i].Delete()
        EndIf
        i += 1
    EndWhile

    Return spawnCount
EndFunction

int Function MarkTraits(Planet akPlanet, Keyword[] traitKeywords) global
    Form planetForm = akPlanet as Form
    int marked = 0
    int i = 0
    While i < traitKeywords.Length
        If !akPlanet.IsTraitKnown(traitKeywords[i])
            If CompletePlanetSurveyNative.MarkTraitKnownForPlanet(planetForm, traitKeywords[i])
                marked += 1
            EndIf
        EndIf
        i += 1
    EndWhile
    Return marked
EndFunction
