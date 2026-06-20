ScriptName CompletePlanetSurveyQuest

; Complete the planet survey for the planet the player is currently on (all biomes).
; Invoke via console:  cgf "CompletePlanetSurveyQuest.CompleteSurvey"
;
; Also auto-fires on any in-game scan when the Settings > Gameplay toggle is on
; (see CompleteSurveyIfEnabled and the C++ scan hook).
;
; Flow:
;   1. Mark traits (MarkTraits -> MarkTraitKnownForPlanet).
;   2. MarkResourcesForPlanet — the shared single-planet completion core: sets the
;      attribute "known" bits + every species/resource scan flag, then fires the
;      survey-complete event so the Survey Data slate drops.
;   3. SpawnAndScanAllPlanetSpecies — PlaceAtMe + scan each flora/fauna species so the
;      in-world refs register as scanned and BIOME COMPLETE fires. This needs the player
;      present, so it's the one step the ref-free galaxy sweep can't reproduce.
;   4. ScanNearbyRefs — per-ref outline refresh (flips blue -> scanned colour).

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

    If surveyAfter < 1.0
        Debug.Notification("Survey: completed " + (surveyAfter * 100) as int + "% (some items may need manual scan)")
    EndIf
EndFunction

; Complete the survey for EVERY planet/moon in the galaxy in one pass — ref-free
; (no teleport, no spawn). Console:
;   cgf "CompletePlanetSurveyQuest.CompleteAllPlanetsSurveyData"
Function CompleteAllPlanetsSurveyData() global
    ; 1) C++ sweep: discover every planet + write its survey state (attribute bits +
    ;    species/resource flags), and record the planets it touched.
    int n = CompletePlanetSurveyNative.CompleteAllPlanetsSurveyData()

    ; 2) Finalize + trait pass. This loop runs across later frames, by which point the
    ;    sweep's async knowledge-entry creates have flushed. Per planet it re-applies the
    ;    survey state, fires the completion event (drops the Survey Data slate), then
    ;    marks traits (GetKeywordTypeList(44) -> MarkTraitKnownForPlanet). Spreading the
    ;    loop across frames keeps the game from freezing.
    int count = CompletePlanetSurveyNative.GetSweepPlanetCount()
    int traitsMarked = 0
    int fullyComplete = 0
    int i = 0
    While i < count
        int fid = CompletePlanetSurveyNative.GetSweepPlanetFormIdAt(i)
        CompletePlanetSurveyNative.FinalizeSweptPlanet(fid)   ; state + slate (entry ready now)
        Planet p = Game.GetForm(fid) as Planet
        If p != None
            traitsMarked += MarkTraits(p, p.GetKeywordTypeList(44))
            If p.GetSurveyPercent() >= 1.0
                fullyComplete += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    CompletePlanetSurveyNative.DebugLog("Sweep result: " + n + " scanned, " + fullyComplete + " / " + count + " at 100%, " + traitsMarked + " traits marked")
    Debug.Notification("Survey DATA complete: " + fullyComplete + " / " + count + " planets at 100%")

    ; 3) Green pass: every planet now has its survey entry, so paint all flora/fauna green for the
    ;    whole galaxy from here — one live handle per species, tree + count completion per planet.
    Debug.Notification("Greening flora/fauna across the galaxy...")
    GreenAllPlanets()
EndFunction

; Atomically green every planet's flora/fauna from one spot — no visiting. For each UNIQUE species
; across the galaxy, spawn ONE live instance as a handle, drive the tree write (ID_52161) + the
; count completion (ID_52158) for every planet that hosts it (planet passed explicitly), then
; delete the instance. Only ONE ref is live at a time (spawn -> green-everywhere -> delete), so
; there's no mass-spawn accumulation. Throttled so the engine can flush PlaceAtMe/Delete.
;   cgf "CompletePlanetSurveyQuest.GreenAllPlanets"
Function GreenAllPlanets() global
    Actor player = Game.GetPlayer()
    If player.IsInInterior() || player.GetCurrentPlanet() == None
        Debug.Notification("Green all: stand on any planet surface first")
        Return
    EndIf
    ObjectReference playerRef = player as ObjectReference

    int speciesCount = CompletePlanetSurveyNative.EnumerateAllSpecies()
    int greenedPlanets = 0
    int spawned = 0
    int i = 0
    While i < speciesCount
        int  fid = CompletePlanetSurveyNative.GetAllSpeciesFormIdAt(i)
        Form sf  = Game.GetForm(fid)
        If sf != None
            ObjectReference r = playerRef.PlaceAtMe(sf, 1, false, true, true, None, None, true)
            If r != None
                greenedPlanets += CompletePlanetSurveyNative.GreenSpeciesEverywhere(r, fid)
                r.Disable(false)
                r.Delete()
                spawned += 1
                ; yield periodically so the engine flushes the spawn/delete churn
                If spawned % 16 == 0
                    Utility.Wait(0.05)
                EndIf
            EndIf
        EndIf
        i += 1
    EndWhile

    CompletePlanetSurveyNative.DebugLog("GreenAllPlanets: " + spawned + " species spawned, " + greenedPlanets + " planet-types greened")
    Debug.Notification("Green all: " + spawned + " species across " + greenedPlanets + " planet-types")
EndFunction

; Called by the C++ scan hook on every species/resource scan. Reads the
; Settings > Gameplay toggle, short-circuits if disabled or planet already
; complete, then QUEUES CompleteSurvey. C++ poller dispatches it once the
; scanner UI has closed — avoids PlaceAtMe racing with live scanner state,
; which is what crashed the direct-dispatch path.
Function CompleteSurveyIfEnabled() global
    ; FormID 0x80C assigned by Creation Kit to GPOF CPSScanAutoComplete.
    ; Verify in xEdit if the ESM is ever regenerated — CK reassigns IDs.
    Form gpofForm = Game.GetFormFromFile(0x80C, "CompletePlanetSurvey.esm")
    GameplayOption gpofOption = gpofForm as GameplayOption
    If gpofOption == None
        CompletePlanetSurveyNative.DebugLog("CompleteSurveyIfEnabled: GPOF 0x80C not found — ESM missing or FormID changed")
        Return
    EndIf
    If gpofOption.GetValue() < 0.5
        Return
    EndIf

    Planet currentPlanet = Game.GetPlayer().GetCurrentPlanet()
    If currentPlanet != None && currentPlanet.GetSurveyPercent() >= 1.0
        Return
    EndIf

    CompletePlanetSurveyNative.QueueCompleteSurvey()
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
    int noFormCount = 0
    int placeFailCount = 0
    int i = 0
    While i < total && spawnCount < 128
        int  speciesFid  = CompletePlanetSurveyNative.GetPlanetSpeciesFormIdAt(i)
        Form speciesForm = Game.GetForm(speciesFid)
        If speciesForm == None
            noFormCount += 1
        Else
            ObjectReference ref = playerRef_OR.PlaceAtMe(speciesForm, 1, false, true, true, None, None, true)
            If ref != None
                spawned[spawnCount] = ref
                spawnCount += 1
            Else
                placeFailCount += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    ; Diagnostic: spawned < total means some species could not be placed (PlaceAtMe
    ; returned None) or resolved (Game.GetForm None) — those biomes won't complete.
    CompletePlanetSurveyNative.DebugLog("SpawnAndScan: total=" + total + " spawned=" + spawnCount + " noForm=" + noFormCount + " placeFail=" + placeFailCount)

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
