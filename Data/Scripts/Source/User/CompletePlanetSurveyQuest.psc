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

; Green a planet's flora/fauna REF-FREE (no spawning) — the incorporated GreenPlanetProper method,
; now the standard species-completion path for every command. Writes the scan-flag (+0x21 / survey
; %) for every authored species, then builds the slot+0x08 attribute-marker catalogue (genetics /
; reproduction / temperament / abilities, all derived from Starfield.esm). Works on the current
; planet AND on a remote planet by form id, as long as the planet's knowledge entry is materialized
; (the caller marks resources/traits first, which resolves it). Returns species scan-flagged.
int Function _GreenPlanet(Form planetForm) global
    int flags = CompletePlanetSurveyNative.TestDirectGreen(planetForm)   ; +0x21 survey flags + create slots
    CompletePlanetSurveyNative.TestBuildArray(planetForm)                ; slot+0x08 attribute catalogue
    If flags < 0
        flags = 0
    EndIf
    Return flags
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
    ; A manual command wins over a queued auto-complete-on-scan: cancel any pending
    ; _AutoCompleteCurrentPlanet -> CompletePlanet("all") so an explicit category isn't overridden.
    CompletePlanetSurveyNative.CancelPendingAutoComplete()
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

    ; Resources before species (so a following green wins — matches the proven completion order).
    int resourceCount = 0
    int traitCount    = 0
    int speciesCount  = 0
    If doResources
        resourceCount = CompletePlanetSurveyNative.MarkResourcesForPlanet(planetForm, 100)
    EndIf
    If doTraits
        ; 1) Trait-known DATA (survey %, TRAITS panel, galaxy map) for the planet's traits.
        traitCount = MarkTraits(currentPlanet, currentPlanet.GetKeywordTypeList(44))
        ; 2) The in-world "Unknown Feature / 0-of-N SCANNED" objects — drive the GAME'S OWN survey-quest
        ; completion path (SQ_Parent.DiscoverMatchingPlanetTraits) on each loaded scan-target, exactly what
        ; a real hand-scan does: set the location PlanetTraitLocationScanCount AV to the required count,
        ; SetExplored, discover the trait. On-planet only (the objects must be loaded). No engine pokes.
        _CompleteTraitScanObjects(playerRef as ObjectReference)
    EndIf
    If doSpecies
        ; Ref-free green (the incorporated GreenPlanetProper method): +0x21 scan flag + the +0x08
        ; ESM-derived marker catalogue. No spawning, no scanner churn.
        speciesCount = _GreenPlanet(planetForm)
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
    CompletePlanetSurveyNative.CancelPendingAutoComplete()   ; manual command wins over queued auto-complete
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

; Complete the in-world TRAIT scan-target OBJECTS ("Unknown Feature / 0-of-N SCANNED") by driving the
; GAME'S OWN survey-quest path — exactly what a real hand-scan does, in plain Papyrus, no engine pokes.
;
; The mechanism (from the shipped game scripts PlanetTraitScanTargetScript.psc + SQ_ParentScript.psc):
;   - PlanetTraitScanTargetScript.OnScanned() -> SQ_Parent.DiscoverMatchingPlanetTraits(self)
;   - that sets the LOCATION actor value PlanetTraitLocationScanCount; when it reaches the alive
;     scan-target count GetRefTypeAliveCount(PlanetTraitScanTargetLocRef) (= the "N" of "0/N"), it
;     SetExplored()s the location and discovers the trait (SetTraitKnown + name + event).
; So the "0/N SCANNED" is just a Location actor value. We set it to the required count and let the
; game's own DiscoverMatchingPlanetTraits do the explore + discover + name. This produces the correct
; durable state a scan does (the location AV + explored + trait-known, all saved) -> the object reloads
; complete + named. ON-PLANET only: the scan-target objects (and their location) must be loaded.
; Returns the number of loaded scan-target objects completed.
int Function _CompleteTraitScanObjects(ObjectReference akPlayer) global
    SQ_ParentScript sqp = Game.GetForm(0x0007092C) as SQ_ParentScript   ; SQ_Parent (Starfield.esm)
    If sqp == None
        CompletePlanetSurveyNative.DebugLog("_CompleteTraitScanObjects: SQ_Parent (0x0007092C) not found")
        Return 0
    EndIf
    ; All 31 PlanetTraitScanTarget ACTIs carry Handscanner_AllowScanAtHighlightRange (0x001CBEA3); find
    ; the loaded instances near the player. (The location AV is keyed by LOCATION, not the specific ref,
    ; so the exact instance does not matter — any loaded scan-target in the location drives its count.)
    Form scanTargetKw = Game.GetForm(0x001CBEA3)
    ObjectReference[] refs = akPlayer.FindAllReferencesWithKeyword(scanTargetKw, 100000.0)
    int done = 0
    If refs
        int i = 0
        While i < refs.Length
            ObjectReference r = refs[i]
            If r
                Location loc = r.GetCurrentLocation()
                If loc
                    ; 1) Mark the REF itself scanned — this reveals the detail text (the "Tendrils..."
                    ; trait description) and blocks the player from re-scanning it (the re-scan is what
                    ; produced the 3/2 over-count). This is the scanned-state a real scan sets; together
                    ; with the location completion below the object is correctly DONE — NOT the old
                    ; "byte set but location still incomplete" jam (here the location IS complete).
                    r.SetScanned(true)
                    ; 2) Force N = required (the alive scan-target count for this location). SetValue is
                    ; ABSOLUTE, so it also CAPS any extra +1 that SetScanned's OnScanned may have added
                    ; (no 3/2). Then let the game explore + discover + name (incrementScanCount=false).
                    int needed = loc.GetRefTypeAliveCount(sqp.PlanetTraitScanTargetLocRef)
                    If needed < 1
                        needed = 1
                    EndIf
                    loc.SetValue(sqp.PlanetTraitLocationScanCount, needed as float)
                    sqp.DiscoverMatchingPlanetTraits(r, false)
                    done += 1
                EndIf
            EndIf
            i += 1
        EndWhile
    EndIf
    CompletePlanetSurveyNative.DebugLog("_CompleteTraitScanObjects: drove SQ_Parent completion on " + done + " loaded scan-target refs")
    Return done
EndFunction

; ============================================================================
;  COMPLETION MENU — parameterized command set. See docs/COMPLETION-COMMANDS.md.
;  Each command takes a category string: comma-separated resources, traits, fauna,
;  flora — or "all". Three commands:
;    CompletePlanet        — the planet you are standing on
;    CompleteBarrenPlanets — every lifeless world (galaxy sweep, ref-free)
;    CompleteLifePlanets   — every life-bearing world (ref-free, works anywhere)
;  Building blocks (proven, all ref-free): MarkResourcesForPlanet (resources +
;  attribute bits, species EXCLUDED), MarkTraits (938333 PlayerKnowledge trait-known,
;  the engine's own off-planet path), and _GreenPlanet (the +0x21 scan flag + the
;  ESM-derived +0x08 marker catalogue — the incorporated GreenPlanetProper method).
;  Traits write DATA only; the in-world "Unknown Feature" objects are NOT byte-poked
;  (that jams the hand-scanner) — they stay normally scannable in play.
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

; Every world with LIFE (flora/fauna) — pick categories. ALL ref-free (no spawning, works from
; ANYWHERE — orbit, another system, on foot). Per world: discover the knowledge entry (so the
; ref-free writes land on never-visited worlds), then mark the requested categories. "resources"
; is pure (no species); "fauna,flora" greens via the +0x21 scan-flag + the ESM-derived +0x08
; marker catalogue (the incorporated GreenPlanetProper method). "traits" marks the trait known AND
; writes the durable scan-target object completion (no jam byte) so the in-world "0/N" is gone on
; reload. Note: completing anything on a never-visited world discovers it (drops its Survey Data slate).
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "resources,traits,fauna,flora"
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "traits"
Function CompleteLifePlanets(string asCategories) global
    CompletePlanetSurveyNative.CancelPendingAutoComplete()   ; manual command wins over queued auto-complete
    bool doResources = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
    bool doTraits    = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
    bool doSpecies   = _WantsSpecies(asCategories)
    If !doResources && !doTraits && !doSpecies
        Debug.Notification("CompleteLifePlanets: nothing selected (resources,traits,fauna,flora,all)")
        Return
    EndIf

    float t0 = Utility.GetCurrentRealTime()
    int worlds  = 0
    int greened = 0
    int n = CompletePlanetSurveyNative.EnumerateLifePlanets()
    int i = 0
    While i < n
        int pid = CompletePlanetSurveyNative.GetLifePlanetFormIdAt(i)
        Planet p = Game.GetForm(pid) as Planet
        If p != None
            Form pf = p as Form
            ; Resources + species need the knowledge entry to exist (ResolvePlanetSubobj is a pure
            ; lookup) — discover it ref-free first so those writes land on never-visited worlds.
            ; (Trait-KNOWN via MarkTraits is self-sufficient.) Resources before the green so the green
            ; wins the final state.
            If doResources || doSpecies
                CompletePlanetSurveyNative.DiscoverPlanetEntry(pf)
            EndIf
            If doResources
                CompletePlanetSurveyNative.MarkResourcesForPlanet(pf, 100)
            EndIf
            If doTraits
                ; DATA only here (survey/panel/map). The in-world scan-target OBJECTS can't be completed
                ; remotely — they (and their Location) aren't loaded off-planet; the SQ_Parent path needs
                ; loaded refs. Those finish on-foot via CompletePlanet "traits" when you're on the world.
                MarkTraits(p, p.GetKeywordTypeList(44))
            EndIf
            If doSpecies
                If _GreenPlanet(pf) > 0
                    greened += 1
                EndIf
            EndIf
            worlds += 1
        EndIf
        i += 1
    EndWhile

    float secs = Utility.GetCurrentRealTime() - t0
    CompletePlanetSurveyNative.DebugLog("CompleteLifePlanets[" + asCategories + "]: " + worlds + " life worlds, " + greened + " greened in " + secs + "s")
    Debug.MessageBox("Life-bearing worlds processed: " + worlds + " (" + asCategories + ").  " + greened + " greened.  Done in " + (secs as int) + "s.")
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
