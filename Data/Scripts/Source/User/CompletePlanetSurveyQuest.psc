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

; ISOLATION PROBE — run it while STANDING on a blue planet:
;   cgf "CompletePlanetSurveyQuest.TestDirectGreen"
; Writes +0x21/+0x20 directly (esm key, no spawn/scan) for the planet you're on, whose
; PlayerKnowledge entry is already loaded. If flora/fauna turn GREEN -> the direct write +
; esm key is correct and the galaxy blue is purely a remote-entry-lifecycle problem. If they
; stay BLUE -> a direct write is insufficient and we must drive the engine writer (ID_52158).
Function TestDirectGreen() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.MessageBox("TestDirectGreen DID NOTHING — GetCurrentPlanet() is None. You're at a settlement / landing pad / ship / interior. Walk OUT into open wilderness (where the scanner shows biome flora & fauna), then run it again. The blue you see now is just 'nothing was written'.")
        Return
    EndIf
    int n = CompletePlanetSurveyNative.TestDirectGreen(currentPlanet as Form)
    CompletePlanetSurveyNative.DebugLog("TestDirectGreen: wrote " + n + " species (canonical/esm key) on current planet")
    If n == 0
        Debug.MessageBox("TestDirectGreen wrote 0 species — this body has no authored flora/fauna, or its knowledge entry didn't resolve. Move to a different LIVING planet and retry.")
    Else
        Debug.MessageBox("TestDirectGreen WROTE +0x21 for " + n + " species (valid run). The live colour will NOT change — that's expected. The real test: SAVE -> fully quit to desktop -> relaunch -> reload -> check these flora/fauna on the fresh load.")
    EndIf
EndFunction

; THE DECISIVE PROBE — stand on a living planet, then:
;   cgf "CompletePlanetSurveyQuest.ProbeScanKeys"
; For each of the first few flora AND fauna species, it spawns one instance, drives the REAL
; create path (SetScanned drives ID_118472 -> ID_83005, stamping the ScannableComponent +0x24),
; waits for the deferred create to flush, then logs authored vs base vs canonical(+0x24) off the
; SAME ref. Grep the log for "ProbeScanKeys:" — it tells us whether the canonical differs from the
; authored ESM id (the whole question) and whether the create path produced a real component.
Function ProbeScanKeys() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.Notification("ProbeScanKeys: stand on a planet surface first")
        Return
    EndIf
    ObjectReference playerRef_OR = playerRef as ObjectReference
    int total = CompletePlanetSurveyNative.EnumeratePlanetSpecies(currentPlanet as Form)
    int probed = 0
    int i = 0
    While i < total && probed < 8
        int  fid = CompletePlanetSurveyNative.GetPlanetSpeciesFormIdAt(i)
        Form sf  = Game.GetForm(fid)
        If sf != None
            ObjectReference r = playerRef_OR.PlaceAtMe(sf, 1, false, true, true, None, None, true)
            If r != None
                r.SetScanned(true)      ; REAL create path (ID_118472 -> ID_83005 stamps +0x24)
                Utility.Wait(0.15)      ; let the deferred component-create flush before we read it
                CompletePlanetSurveyNative.ProbeScanKeys(r, fid)
                r.Disable(false)
                r.Delete()
                probed += 1
            EndIf
        EndIf
        i += 1
    EndWhile
    Debug.MessageBox("ProbeScanKeys: probed " + probed + " species. Check CompletePlanetSurvey.log for 'ProbeScanKeys:' lines (authored vs base vs canonical).")
EndFunction

; PLANET-KEY FIX TEST — run standing in OPEN WILDERNESS on a living planet:
;   cgf "CompletePlanetSurveyQuest.TestRenderKeyGreen"
; Writes +0x21 under the RENDER planet id (ID_52188(player)) instead of the form +0x54 id the data
; sweep uses. The diagnosis is that the outline reads (938333|ID_52188), not (938333|+0x54). If a
; SAVE -> full quit -> reload then shows these flora/fauna GREEN (where TestDirectGreen was blue),
; the planet-key domain was the entire bug, and remote green becomes a matter of mapping +0x54 -> render id.
Function TestRenderKeyGreen() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.MessageBox("TestRenderKeyGreen DID NOTHING — GetCurrentPlanet() is None. Walk OUT into open wilderness and retry.")
        Return
    EndIf
    int n = CompletePlanetSurveyNative.TestRenderKeyGreen(playerRef as ObjectReference, currentPlanet as Form)
    If n == 0
        Debug.MessageBox("TestRenderKeyGreen wrote 0 — see log (ID_52188 returned 0, or the render-keyed entry didn't resolve). Try open terrain on a living planet.")
    Else
        Debug.MessageBox("TestRenderKeyGreen WROTE +0x21 under the RENDER planet key for " + n + " species. Now SAVE -> fully quit -> relaunch -> reload -> check. GREEN = planet-key was the whole bug.")
    EndIf
EndFunction

; DEFINITIVE READ PROBE — ask the engine what the outline actually reads:
;   cgf "CompletePlanetSurveyQuest.ProbeRenderRead"
; Calls the renderer's OWN green reader (ID_52159) for each authored species. Run it (a) on a planet
; you just CompleteSurvey'd (green) — expect "GREEN for N/N"; then (b) on a planet where only
; TestDirectGreen ran (blue) — if "GREEN for 0/N", the render keys on a different species id than our
; authored write, which finally pins down where the green lives. Read-only, safe to spam.
Function ProbeRenderRead() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.MessageBox("ProbeRenderRead: GetCurrentPlanet None — walk into open wilderness on a living planet.")
        Return
    EndIf
    int green = CompletePlanetSurveyNative.ProbeRenderRead(playerRef as ObjectReference, currentPlanet as Form)
    Debug.MessageBox("ProbeRenderRead: the engine's outline reader (ID_52159) reports GREEN for " + green + " authored species (see log, per-species). Compare a CompleteSurvey'd planet vs a TestDirectGreen'd one.")
EndFunction

; DB-STATE DIFF PROBE — find what a full scan writes that our byte-poke doesn't:
;   cgf "CompletePlanetSurveyQuest.DumpSpeciesSlots"
; Dumps the raw per-species slot bytes + subobj header to the log. Run it (a) after TestDirectGreen
; (half-scan) and (b) after CompleteSurvey (full green+info) on the same planet — the byte difference
; is the missing "catalogued/known" record. Read-only.
Function DumpSpeciesSlots() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.MessageBox("DumpSpeciesSlots: GetCurrentPlanet None — walk into open wilderness on a living planet.")
        Return
    EndIf
    int n = CompletePlanetSurveyNative.DumpSpeciesSlots(currentPlanet as Form)
    Debug.MessageBox("DumpSpeciesSlots: dumped " + n + " species slots to the log. Run before/after CompleteSurvey and diff the hex.")
EndFunction

; THE FIX — validation step. On a fresh living planet, in order:
;   cgf "CompletePlanetSurveyQuest.TestDirectGreen"   (half-scan: creates slots, +0x08 empty)
;   cgf "CompletePlanetSurveyQuest.TestBuildArray"    (engine-build the +0x08 attribute array)
;   save -> quit -> reload -> check the flora/fauna
; If they render PROPERLY green (outline + info) after the reload, slot+0x08 IS the gate and the
; engine-allocated build works -> we then derive full per-species attribute ids from the ESM.
Function TestBuildArray() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Debug.MessageBox("TestBuildArray: GetCurrentPlanet None — walk into open wilderness on a living planet.")
        Return
    EndIf
    int n = CompletePlanetSurveyNative.TestBuildArray(currentPlanet as Form)
    If n == 0
        Debug.MessageBox("TestBuildArray built 0 arrays — run TestDirectGreen first (it creates the empty slots), on a living planet.")
    Else
        Debug.MessageBox("TestBuildArray engine-built the +0x08 attribute array for " + n + " species. SAVE -> quit -> reload -> check: do they render properly GREEN now (outline + info)?")
    EndIf
EndFunction

; PROPER GREEN — the incorporated solution, ref-free + no spawning. Marks species scanned (+0x21,
; survey %) AND builds the slot+0x08 attribute-marker catalogue (the green outline + info panel).
; One command for the planet you're on:  cgf "CompletePlanetSurveyQuest.GreenPlanetProper"
Function GreenPlanetProper() global
    Actor playerRef = Game.GetPlayer()
    Planet p = playerRef.GetCurrentPlanet()
    If p == None
        Debug.MessageBox("GreenPlanetProper: not on a planet — stand in open wilderness on a living planet.")
        Return
    EndIf
    Form pf = p as Form
    int flags = CompletePlanetSurveyNative.TestDirectGreen(pf)   ; +0x21 survey flags + create slots
    int cat   = CompletePlanetSurveyNative.TestBuildArray(pf)    ; slot+0x08 attribute catalogue
    Debug.MessageBox("GreenPlanetProper: " + flags + " species scanned, " + cat + " catalogues built. Flora AND fauna should now render GREEN with the full correct 4-marker info (genetics/reproduction/temperament now derived per-species from the ESM). If anything stays blue or shows a wrong/missing marker, tell me the species and which field.")
EndFunction

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

; Complete the survey for every UNINHABITED planet/moon in the galaxy (no flora/fauna) in one pass —
; ref-free (no teleport, no spawn). Worlds WITH species are deliberately skipped (their flora/fauna
; can't be greened by this command — that's the separate CompleteAllPlanets command). Console:
;   cgf "CompletePlanetSurveyQuest.CompleteUninhabitedPlanets"
Function CompleteUninhabitedPlanets() global
    ; Immersive framing as a MODAL popup — the CK-authored MESG record CPSRecallMessage (0x807),
    ; NOT a toast. A toast would be buried under the cascade of native "<Planet> Survey Data"
    ; notifications that drains over several minutes. Message.Show() is modal and blocks until the
    ; player presses OK, so the narrative lands first; the cascade that follows becomes the story.
    Message recallMsg = Game.GetFormFromFile(0x807, "CompletePlanetSurvey.esm") as Message
    If recallMsg != None
        recallMsg.Show()
    EndIf

    ; Phase timings via Utility.GetCurrentRealTime (real seconds) so the log shows how long each
    ; phase actually takes. tStart is captured AFTER the popup closes, so it excludes the player's
    ; reading time and measures pure compute. Phase 1 also logs precise ms on the C++ side.
    float tStart = Utility.GetCurrentRealTime()

    ; 1) C++ sweep: discover every planet + write its survey state (attribute bits +
    ;    species/resource flags), and record the planets it touched.
    int n = CompletePlanetSurveyNative.CompleteAllPlanetsSurveyData()
    float tAfterSweep = Utility.GetCurrentRealTime()

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
    float tAfterFinalize = Utility.GetCurrentRealTime()

    CompletePlanetSurveyNative.DebugLog("Sweep result: " + n + " scanned, " + fullyComplete + " / " + count + " at 100%, " + traitsMarked + " traits marked")

    ; NO galaxy-wide green pass. Greening a world's flora/fauna requires the per-(planet,species)
    ; CANONICAL id the engine only produces when the biome materializes the creature on-planet — it
    ; cannot be written ref-free without leaving the outline blue (an invalid state). So living
    ; worlds are completed on-foot via CompleteSurvey; this command completes the barren bodies and
    ; catalogues survey data + traits galaxy-wide.
    float tEnd = Utility.GetCurrentRealTime()

    ; Explicit per-phase durations (seconds) — so "how long did it take" is in the log directly.
    float secSweep    = tAfterSweep - tStart
    float secFinalize = tAfterFinalize - tAfterSweep
    float secTotal    = tEnd - tStart
    CompletePlanetSurveyNative.DebugLog("Timing(s): phase1(sweep)=" + secSweep + " phase2(finalize)=" + secFinalize + " total=" + secTotal)

    ; Unmissable completion signal — a MODAL Debug.MessageBox, NOT a toast (toasts queue behind the
    ; Survey Data slate cascade and surface buried, minutes later). Honest wording: barren worlds are
    ; fully done; living worlds need an on-planet scan.
    Debug.MessageBox("Survey data catalogued across the galaxy.  " + fullyComplete + " lifeless worlds fully surveyed; worlds with flora & fauna are mapped and ready — land on one and run CompleteSurvey to catalogue its life.  Done in " + (secTotal as int) + "s.")
EndFunction

; Green every planet's flora/fauna from one spot — no visiting. The GREEN is per-(planet,species) and
; bound to "the planet the scanned ref is on" (ID_52188) — proven in-game (a real scan greens only the
; planet you're physically on). So for each UNIQUE species we spawn ONE instance, then
; GreenSpeciesEverywhere loops its host planets and, per planet, SPOOFS the current-planet global to
; that planet and runs the genuine real scan (SetScanned/ID_83008 + ID_52157), writing the persistent
; green for that target planet. Then delete the instance. Only ONE ref live at a time. Throttled.
;   cgf "CompletePlanetSurveyQuest.GreenAllPlanets"
Function GreenAllPlanets() global
    Actor player = Game.GetPlayer()
    If player.IsInInterior() || player.GetCurrentPlanet() == None
        Debug.Notification("Green all: stand on any planet surface first")
        Return
    EndIf
    ObjectReference playerRef = player as ObjectReference

    float tGreenStart = Utility.GetCurrentRealTime()
    int speciesCount = CompletePlanetSurveyNative.EnumerateAllSpecies()
    int spawned = 0
    int i = 0
    While i < speciesCount
        int  fid = CompletePlanetSurveyNative.GetAllSpeciesFormIdAt(i)
        Form sf  = Game.GetForm(fid)
        If sf != None
            ObjectReference r = playerRef.PlaceAtMe(sf, 1, false, true, true, None, None, true)
            If r != None
                ; Per HOST PLANET of this species, GreenSpeciesEverywhere spoofs the "current planet"
                ; global to that planet then runs the genuine real scan (SetScanned/ID_83008 + ID_52157),
                ; writing the PERSISTENT per-(planet,species) green for the target — the same write
                ; CompleteSurvey does for the current planet, redirected to each host planet.
                CompletePlanetSurveyNative.GreenSpeciesEverywhere(r, fid)
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

    ; No toast here — it would queue behind the Survey Data slate cascade and surface minutes
    ; later, buried. The galaxy command shows a modal Debug.MessageBox when everything is done;
    ; a standalone GreenAllPlanets run reports via the log line below (with its duration).
    float secGreen = Utility.GetCurrentRealTime() - tGreenStart
    CompletePlanetSurveyNative.DebugLog("GreenAllPlanets: " + spawned + " unique species real-scanned (per-species green) in " + secGreen + "s")
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
