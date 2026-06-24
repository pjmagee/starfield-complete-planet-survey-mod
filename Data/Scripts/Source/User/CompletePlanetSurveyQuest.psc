ScriptName CompletePlanetSurveyQuest

; Console completion commands (category string: comma-separated resources, traits, fauna, flora — or "all"):
;   cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits,fauna,flora"
;   cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "all"
;
; CompletePlanet — current planet only (player must be on-surface). CompleteBarrenPlanets — ref-free
; galaxy sweep for lifeless worlds. CompleteLifePlanets — life-bearing worlds (see each function's notes).
;
; Auto-complete on scan: Settings > Gameplay toggle (CompleteSurveyIfEnabled queues native dispatch;
; C++ poller runs _AutoCompleteCurrentPlanet after the scanner closes).

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

; TEST — directly SetScanned the STATIC placed trait scan-target REFRs for trait-20 (Sentient Microbial
; Colonies). Run standing near a "Microbial Community". Resolves whether those static FormIDs are the
; actual placed instances (greenable directly, no spawn) vs templates, and whether SetScanned greens them.
;   cgf "CompletePlanetSurveyQuest.TestScanTraitTargets"
Function TestScanTraitTargets() global
    ; DIAGNOSTIC — find scan-targets via the keyword (0x001CBEA3, on all 31 PlanetTraitScanTarget ACTIs)
    ; AND report the CLOSEST one's distance / base form id / scanned-state, then SetScanned it. This
    ; tells us whether the search returns the actual Unknown Feature you're aiming at (close + unscanned)
    ; or only far/already-scanned decoys. Aim at the Unknown Feature, then run it.
    Actor playerRef = Game.GetPlayer()
    Form scanTargetKw = Game.GetForm(0x001CBEA3)
    ObjectReference[] refs = playerRef.FindAllReferencesWithKeyword(scanTargetKw, 200000.0)
    int found = 0
    ObjectReference closest = None
    float closestDist = 9999999.0
    If refs
        found = refs.Length
        int i = 0
        While i < found
            If refs[i]
                float d = playerRef.GetDistance(refs[i])
                If d < closestDist
                    closestDist = d
                    closest = refs[i]
                EndIf
            EndIf
            i += 1
        EndWhile
    EndIf
    string msg = "DIAG keyword-find: " + found + " scan-targets within range. "
    If closest
        int baseId = 0
        Form b = closest.GetBaseObject()
        If b
            baseId = b.GetFormID()
        EndIf
        bool was = closest.IsScanned()
        closest.SetScanned(true)
        msg += "CLOSEST is " + (closestDist as int) + " units away, baseFormId(dec)=" + baseId + ", wasScanned=" + was + ", now SetScanned. >> Is that closest object the Unknown Feature you're aiming at, and did it change? Tell me: found, closest distance, baseFormId, wasScanned, and what the Unknown Feature shows now."
    Else
        msg += "NONE found in range — FindAllReferencesWithKeyword returns nothing for the scan-target keyword. That alone is the bug to fix."
    EndIf
    Debug.MessageBox(msg)
EndFunction

; TEST the durable-KB hypothesis: write the species-style 938333 knowledge entry (+0x21 scanned) for this
; planet's trait scan-target ACTIs, keyed (planet, ACTI) exactly like species. Then SAVE -> reload -> check
; if the scan-target objects go green / show scanned. Settles whether the KB write greens traits like species.
;   cgf "CompletePlanetSurveyQuest.TestMarkScanTargetKB"
Function TestMarkScanTargetKB() global
    Actor playerRef = Game.GetPlayer()
    Planet p = playerRef.GetCurrentPlanet()
    If p == None
        Debug.MessageBox("TestMarkScanTargetKB: not on a planet — stand on a planet with traits.")
        Return
    EndIf
    Form pf = p as Form
    Keyword[] traits = p.GetKeywordTypeList(44)
    int written = 0
    int t = 0
    While t < traits.Length
        int slot = 0
        While slot < 2
            int actiId = CompletePlanetSurveyNative.GetTraitScanTargetActi(traits[t], slot)
            If actiId != 0
                int r = CompletePlanetSurveyNative.MarkScanTargetScannedForPlanet(pf, actiId)
                If r > 0
                    written += 1
                EndIf
            EndIf
            slot += 1
        EndWhile
        t += 1
    EndWhile
    Debug.MessageBox("TestMarkScanTargetKB: wrote the species-style 938333 knowledge-DB entry for " + written + " trait scan-target ACTIs on this planet (same write that greens species). Now SAVE -> reload -> look at the scan-target objects: did any go GREEN / show scanned? Tell me yes or no.")
EndFunction

; Complete the planet's TRAIT scan-target surface objects (the "Unknown Feature" -> e.g. "Microbial
; Community"). For each PlanetTrait keyword on the planet, resolve its scan-target ACTI base form(s) and
; FindAllReferencesOfType + SetScanned the LOADED instances -> green outline + N/M count + identity reveal.
; ON-PLANET (refs must be loaded near the player); returns the number of scan-target refs scanned.
int Function CompleteTraitScanTargets(Planet akPlanet, ObjectReference playerRef_OR) global
    If playerRef_OR == None
        Return 0
    EndIf
    ; All 31 PlanetTraitScanTarget ACTIs carry Handscanner_AllowScanAtHighlightRange (0x001CBEA3) and
    ; nothing else does (ESM-verified 31/31) — so one keyword search finds every loaded trait scan-target
    ; on the planet, regardless of which trait, and SetScanned greens it (ID_83008 flora branch ->
    ; 939118 +0x28) + ticks N/M + reveals the identity. On-planet only (refs must be loaded near player).
    Form scanTargetKw = Game.GetForm(0x001CBEA3)
    ObjectReference[] refs = playerRef_OR.FindAllReferencesWithKeyword(scanTargetKw, 100000.0)
    int scanned = 0
    If refs
        int r = 0
        While r < refs.Length
            If refs[r]
                ; CORRECT recipe (replaces bare SetScanned): ID_83008(ref,1,8,0) green+count+durable
                ; + ID_83025 identity reveal, guarded by a live 939118 component. st>0 => had component.
                int st = CompletePlanetSurveyNative.CompleteTraitScanTargetRef(refs[r])
                If st > 0
                    scanned += 1
                EndIf
            EndIf
            r += 1
        EndWhile
    EndIf
    Return scanned
EndFunction

; ON-PLANET trait completion test — the CORRECT decompile-verified recipe (ID_83008+ID_83025),
; replacing the failed SetScanned. Stand near a trait scan-target ("Unknown Feature"). Run it, then
; LOOK AWAY AND BACK at the object to force the monocle repaint. Reports found / live-component /
; newly-scanned counts; the SFSE log has per-ref FormID/state/canon so we can see which refs the find
; returns (the crux of why SetScanned hit the wrong instances).
;   cgf "CompletePlanetSurveyQuest.TestTraitOnPlanet"
Function TestTraitOnPlanet() global
    Actor playerRef = Game.GetPlayer()
    Form scanTargetKw = Game.GetForm(0x001CBEA3)
    ObjectReference[] refs = playerRef.FindAllReferencesWithKeyword(scanTargetKw, 200000.0)
    int found = 0
    int live = 0
    int completed = 0
    float closestDist = 9999999.0
    If refs
        found = refs.Length
        int i = 0
        While i < found
            If refs[i]
                float d = playerRef.GetDistance(refs[i])
                If d < closestDist
                    closestDist = d
                EndIf
                int st = CompletePlanetSurveyNative.CompleteTraitScanTargetRef(refs[i])
                If st > 0
                    live += 1
                    If st == 1
                        completed += 1
                    EndIf
                EndIf
            EndIf
            i += 1
        EndWhile
    EndIf
    Debug.MessageBox("TestTraitOnPlanet: found " + found + " scan-target refs (closest " + (closestDist as int) + " units), " + live + " had a live scan component, " + completed + " were unscanned->now scanned via ID_83008+ID_83025. >> NOW LOOK AWAY AND BACK at the Unknown Feature to force the monocle to repaint. Did it go GREEN, show its real name, and move the N/M count? Also check the SFSE log for the per-ref FormID/state/canon lines.")
EndFunction

; REGISTRY-WALK trait completion test — completes EVERY loaded trait scan-target by walking the engine's
; GLOBAL 939118 ScannableComponent registry directly (the same store the outline + N/M count read), so
; it CANNOT miss the rendered overlay instances the keyword ref-find can. Stand on a planet with trait
; scan-targets, run it, then LOOK AWAY AND BACK to repaint. Compare the count it reports against
; TestTraitOnPlanet's: the registry walk should catch any instances the keyword find missed.
;   cgf "CompletePlanetSurveyQuest.TestTraitRegistryWalk"
Function TestTraitRegistryWalk() global
    Actor playerRef = Game.GetPlayer()
    ; Large radius so we don't accidentally exclude in-range targets; the registry only holds LOADED
    ; scannables anyway, so this is effectively "every loaded trait scan-target".
    int completed = CompletePlanetSurveyNative.CompleteTraitScanTargetsInRange(playerRef as ObjectReference, 200000.0)
    Debug.MessageBox("TestTraitRegistryWalk: completed " + completed + " trait scan-target(s) by walking the GLOBAL 939118 registry (no keyword find -> no missed instances). >> NOW LOOK AWAY AND BACK at the Unknown Feature(s) to force the monocle to repaint. Did they go GREEN, show their real names, and move the N/M count? Check the SFSE log for the [trait-walk] per-ref formID/state/dist lines.")
EndFunction

; CURRENT planet — pick categories (comma list of resources/traits/fauna/flora, or "all"):
;   cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits,fauna,flora"
;   cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits"
Function CompletePlanet(string asCategories) global
    bool doResources = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
    bool doTraits    = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
    bool doSpecies   = _WantsSpecies(asCategories)

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

    Form planetForm = currentPlanet as Form
    ObjectReference playerRef_OR = playerRef as ObjectReference

    ; Resources before species (so a following green wins — matches the proven CompleteSurvey order).
    int resourceCount = 0
    int traitCount    = 0
    int speciesCount  = 0
    If doResources
        resourceCount = CompletePlanetSurveyNative.MarkResourcesForPlanet(planetForm, 100)
    EndIf
    If doTraits
        traitCount = MarkTraits(currentPlanet, currentPlanet.GetKeywordTypeList(44))
        CompleteTraitScanTargets(currentPlanet, playerRef_OR)
    EndIf
    If doSpecies
        speciesCount = SpawnAndScanAllPlanetSpecies(planetForm, playerRef_OR)
    EndIf
    CompletePlanetSurveyNative.ScanNearbyRefs()

    float surveyAfter = currentPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("CompletePlanet[" + asCategories + "]: traits=" + traitCount + " resources=" + resourceCount + " species=" + speciesCount + " survey=" + (surveyAfter * 100) as int + "%")
    Debug.Notification("Planet survey: " + (surveyAfter * 100) as int + "% (" + asCategories + ")")
EndFunction

; Complete the survey for every UNINHABITED planet/moon in the galaxy (no flora/fauna) in one pass —
; ref-free (no teleport, no spawn). Worlds WITH species are deliberately skipped (their flora/fauna
; can't be greened by this command — that's the separate CompleteAllPlanets command). Console:
;   cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"
; Barren = worlds with no flora/fauna. Resources are written by the sweep as it discovers each
; world (always applied); "traits" additionally marks each world's traits known.
Function CompleteBarrenPlanets(string asCategories) global
    bool doTraits = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
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
            If doTraits
                traitsMarked += MarkTraits(p, p.GetKeywordTypeList(44))
            EndIf
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
; INTERNAL helper — used by CompleteLifePlanets for the fauna/flora category.
Function _GreenAllLifeWorlds() global
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

; ============================================================================
;  COMPLETION MENU — cohesive command set. See docs/COMPLETION-COMMANDS.md.
;  Two families:  CompletePlanet*  (the planet you are standing on)  and
;                 CompleteGalaxy*  (every planet, in one pass).
;  Filter suffixes: (none) = everything, NoCreatures, ResourcesOnly, CreaturesOnly.
;  Building blocks (proven): native sweep CompleteAllPlanetsSurveyData (resources +
;  attribute bits), MarkTraits (938333 PlayerKnowledge trait-known, the engine's own
;  off-planet path), and CompleteGalaxyCreaturesOnly (flora/fauna green — the only
;  part that needs you stood on a surface). Legacy aliases are at the bottom.
; ============================================================================

; Shared galaxy core (NO UI): discover every planet, write its survey state
; (resources + attribute bits), finalize it (fires the "Survey Data" slate),
; and optionally mark its traits known. Returns the count that reached 100%.
; Ref-free — no teleport, no spawn. One place for the sweep so every galaxy
; command behaves identically.
int Function _GalaxySweepCompleteData(bool doTraits) global
    int n     = CompletePlanetSurveyNative.CompleteAllPlanetsSurveyData()
    int count = CompletePlanetSurveyNative.GetSweepPlanetCount()
    int traitsMarked  = 0
    int fullyComplete = 0
    int i = 0
    While i < count
        int fid = CompletePlanetSurveyNative.GetSweepPlanetFormIdAt(i)
        CompletePlanetSurveyNative.FinalizeSweptPlanet(fid)
        Planet p = Game.GetForm(fid) as Planet
        If p != None
            If doTraits
                traitsMarked += MarkTraits(p, p.GetKeywordTypeList(44))
            EndIf
            If p.GetSurveyPercent() >= 1.0
                fullyComplete += 1
            EndIf
        EndIf
        i += 1
    EndWhile
    CompletePlanetSurveyNative.DebugLog("_GalaxySweepCompleteData: " + n + " scanned, " + fullyComplete + " / " + count + " at 100%, traits=" + traitsMarked + " (doTraits=" + doTraits + ")")
    Return fullyComplete
EndFunction

; Every world with LIFE (flora/fauna) — pick categories. resources/traits are ref-free; the
; flora/fauna green needs you stood on a surface. Note: on a life world, marking "resources"
; without greening leaves its creatures shown as scanned-but-blue, so "resources" auto-includes
; the green here.
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "resources,traits,fauna,flora"
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "traits"
Function CompleteLifePlanets(string asCategories) global
    bool doResources = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
    bool doTraits    = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
    bool doSpecies   = _WantsSpecies(asCategories)
    If doResources
        doSpecies = true   ; resources writes species flags; the green must follow or they show blue
    EndIf

    float t0 = Utility.GetCurrentRealTime()
    ; 1) resources + traits per life-bearing world (ref-free), BEFORE the green so the green wins.
    int worlds = 0
    If doResources || doTraits
        int n = CompletePlanetSurveyNative.EnumerateLifePlanets()
        int i = 0
        While i < n
            int pid = CompletePlanetSurveyNative.GetLifePlanetFormIdAt(i)
            Planet p = Game.GetForm(pid) as Planet
            If p != None
                If doResources
                    CompletePlanetSurveyNative.MarkResourcesForPlanet(p as Form, 100)
                EndIf
                If doTraits
                    MarkTraits(p, p.GetKeywordTypeList(44))
                EndIf
                worlds += 1
            EndIf
            i += 1
        EndWhile
    EndIf
    ; 2) flora/fauna green LAST (galaxy-wide) — needs you stood on a surface.
    If doSpecies
        Actor player = Game.GetPlayer()
        If player.IsInInterior() || player.GetCurrentPlanet() == None
            Debug.Notification("CompleteLifePlanets: stand on a planet surface to green flora/fauna")
        Else
            _GreenAllLifeWorlds()
        EndIf
    EndIf
    float secs = Utility.GetCurrentRealTime() - t0
    Debug.MessageBox("Life-bearing worlds processed: " + worlds + " (" + asCategories + "). Done in " + (secs as int) + "s.")
EndFunction

; True if the category list asks for creatures (any of fauna/flora/species/creatures, or "all").
bool Function _WantsSpecies(string asCategories) global
    Return CompletePlanetSurveyNative.CategoryEnabled(asCategories, "fauna") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "flora") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "species") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "creatures")
EndFunction

; INTERNAL — the on-scan auto-complete dispatch target. The C++ poller calls this by name (no args)
; when the "auto-complete on scan" setting is on. Not a player command.
Function _AutoCompleteCurrentPlanet() global
    CompletePlanet("all")
EndFunction
