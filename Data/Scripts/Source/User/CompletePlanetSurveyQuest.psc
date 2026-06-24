ScriptName CompletePlanetSurveyQuest

; Console completion commands (category string: comma-separated resources, traits, fauna, flora — or "all"):
;   cgf "CompletePlanetSurveyQuest.CompletePlanet" "resources,traits,fauna,flora"
;   cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "all"
;   cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"   ; whole galaxy (barren + life)
;
; CompletePlanet — current planet only (player must be on-surface). CompleteBarrenPlanets — ref-free
; galaxy sweep for lifeless worlds. CompleteLifePlanets — life-bearing worlds. CompleteAllPlanets —
; both sweeps, the entire galaxy in one command. (See each function's notes.)
;
; Auto-complete on scan: Settings > Gameplay toggle (CompleteSurveyIfEnabled queues native dispatch;
; C++ poller runs _AutoCompleteCurrentPlanet after the scanner closes).

; Green a planet's flora/fauna REF-FREE (no spawning) — the standard species-completion path for every
; command. Writes the scan-flag (+0x21 / survey %) for every authored species, then builds the slot+0x08
; attribute-marker catalogue (genetics / reproduction / temperament / abilities, all derived from
; Starfield.esm). Works on the current planet AND on a remote planet by form id, as long as the planet's
; knowledge entry is materialized (the caller marks resources/traits first, which resolves it). Returns
; species scan-flagged. aiKind: 0 = both, 1 = flora only (FLOR), 2 = fauna only (NPC_) — so "fauna"/"flora"
; green only that kind. (The two native calls are the production green path; their TestDirectGreen/
; TestBuildArray names are historical — they were the RE probes that became the real method.)
int Function _GreenPlanet(Form planetForm, int aiKind) global
    int flags = CompletePlanetSurveyNative.TestDirectGreen(planetForm, aiKind)   ; +0x21 survey flags + create slots
    CompletePlanetSurveyNative.TestBuildArray(planetForm, aiKind)                ; slot+0x08 attribute catalogue
    If flags < 0
        flags = 0
    EndIf
    Return flags
EndFunction

; Map a category string to the green "kind": 1 = flora only, 2 = fauna only, 0 = both. "species"/
; "creatures"/"all" (or both flora+fauna) => both. This is what keeps "fauna" from greening flora too.
int Function _SpeciesKind(string asCategories) global
    bool doFlora = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "flora") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "species") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "creatures")
    bool doFauna = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "fauna") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "species") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "creatures")
    If doFlora && !doFauna
        Return 1
    ElseIf doFauna && !doFlora
        Return 2
    EndIf
    Return 0
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
        ; Ref-free green: +0x21 scan flag + the +0x08 ESM-derived marker catalogue. No spawning, no
        ; scanner churn. _SpeciesKind keeps "fauna" = creatures only and "flora" = plants only.
        speciesCount = _GreenPlanet(planetForm, _SpeciesKind(asCategories))
    EndIf
    CompletePlanetSurveyNative.ScanNearbyRefs()

    float surveyAfter = currentPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("CompletePlanet[" + asCategories + "]: traits=" + traitCount + " resources=" + resourceCount + " species=" + speciesCount + " survey=" + (surveyAfter * 100) as int + "%")
    Debug.Notification("Planet survey: " + (surveyAfter * 100) as int + "% (" + asCategories + ")")
EndFunction

; Complete the survey for every UNINHABITED planet/moon in the galaxy (no flora/fauna) in one pass —
; ref-free (no teleport, no spawn). Worlds WITH species are deliberately skipped (their flora/fauna
; can't be greened by this command — that's the separate CompleteLifePlanets command). Console:
;   cgf "CompletePlanetSurveyQuest.CompleteBarrenPlanets" "resources,traits"
; Barren = worlds with no flora/fauna. Resources are written by the sweep as it discovers each
; world (always applied); "traits" additionally marks each world's traits known.
; abShowResult=false suppresses the result popup (CompleteAllPlanets shows ONE combined result instead);
; the immersive CPSRecallMessage intro always shows. Returns the count of barren worlds fully surveyed.
int Function CompleteBarrenPlanets(string asCategories, bool abShowResult = true) global
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

    ; If you ran this while standing on a (barren) planet, complete its loaded in-world trait objects
    ; explicitly — they won't auto-resolve on-load since they're already loaded. Remote worlds' objects
    ; auto-resolve on arrival (the game's CheckForScanTargetUpdate, since their traits are now known).
    If doTraits
        Actor bp = Game.GetPlayer()
        If !bp.IsInInterior() && bp.GetCurrentPlanet() != None
            _CompleteTraitScanObjects(bp as ObjectReference)
        EndIf
    EndIf

    ; NO galaxy-wide green pass here. Greening a world's flora/fauna requires the per-(planet,species)
    ; CANONICAL id the engine only produces when the biome materializes the creature on-planet — it
    ; cannot be written ref-free without leaving the outline blue (an invalid state). So living
    ; worlds are completed by CompleteLifePlanets; this command completes the barren bodies and
    ; catalogues survey data + traits galaxy-wide.
    float tEnd = Utility.GetCurrentRealTime()

    ; Explicit per-phase durations (seconds) — so "how long did it take" is in the log directly.
    float secSweep    = tAfterSweep - tStart
    float secFinalize = tAfterFinalize - tAfterSweep
    float secTotal    = tEnd - tStart
    CompletePlanetSurveyNative.DebugLog("Timing(s): phase1(sweep)=" + secSweep + " phase2(finalize)=" + secFinalize + " total=" + secTotal)

    ; Unmissable completion signal — a MODAL Debug.MessageBox, NOT a toast (toasts queue behind the
    ; Survey Data slate cascade and surface buried, minutes later). Honest wording: barren worlds are
    ; fully done; living worlds need their flora/fauna catalogued. Suppressed when CompleteAllPlanets
    ; runs us (it shows ONE combined result for the whole galaxy).
    If abShowResult
        Debug.MessageBox("Survey data catalogued across the galaxy.  " + fullyComplete + " lifeless worlds fully surveyed; worlds with flora & fauna are mapped and ready — run CompleteLifePlanets (or land on one) to catalogue their life.  Done in " + (secTotal as int) + "s.")
    EndIf
    Return fullyComplete
EndFunction

; Every world with LIFE (flora/fauna) — pick categories. ALL ref-free (no spawning, works from
; ANYWHERE — orbit, another system, on foot). Per world: discover the knowledge entry (so the
; ref-free writes land on never-visited worlds), then mark the requested categories. "resources"
; is pure (no species); "fauna,flora" greens via the +0x21 scan-flag + the ESM-derived +0x08
; marker catalogue. "traits" marks the trait known (DATA); the in-world "0/N" objects then finish
; on arrival (the game's own CheckForScanTargetUpdate, since the trait is now known) or on-foot via
; CompletePlanet "traits". Note: completing anything on a never-visited world discovers it (drops
; its Survey Data slate).
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "resources,traits,fauna,flora"
;   cgf "CompletePlanetSurveyQuest.CompleteLifePlanets" "traits"
; abShowResult=false suppresses the result popup (CompleteAllPlanets shows ONE combined result instead).
; Returns the count of life-bearing worlds processed.
int Function CompleteLifePlanets(string asCategories, bool abShowResult = true) global
    CompletePlanetSurveyNative.CancelPendingAutoComplete()   ; manual command wins over queued auto-complete
    bool doResources = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
    bool doTraits    = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
    bool doSpecies   = _WantsSpecies(asCategories)
    If !doResources && !doTraits && !doSpecies
        Debug.Notification("CompleteLifePlanets: nothing selected (resources,traits,fauna,flora,all)")
        Return 0
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
                If _GreenPlanet(pf, _SpeciesKind(asCategories)) > 0
                    greened += 1
                EndIf
            EndIf
            worlds += 1
        EndIf
        i += 1
    EndWhile

    ; The galaxy loop marks every life world's traits KNOWN (data); remote worlds' in-world objects then
    ; auto-resolve on arrival (the game's OnLoad CheckForScanTargetUpdate -> SetScanned, since the trait is
    ; known). But if you ran this while STANDING on a planet, that world's objects are already loaded and
    ; won't re-fire on-load — so complete the CURRENT planet's loaded scan-target objects explicitly here.
    If doTraits
        Actor lp = Game.GetPlayer()
        If !lp.IsInInterior() && lp.GetCurrentPlanet() != None
            _CompleteTraitScanObjects(lp as ObjectReference)
        EndIf
    EndIf

    float secs = Utility.GetCurrentRealTime() - t0
    CompletePlanetSurveyNative.DebugLog("CompleteLifePlanets[" + asCategories + "]: " + worlds + " life worlds, " + greened + " greened in " + secs + "s")
    If abShowResult
        Debug.MessageBox("Life-bearing worlds processed: " + worlds + " (" + asCategories + ").  " + greened + " greened.  Done in " + (secs as int) + "s.")
    EndIf
    Return worlds
EndFunction

; Complete EVERY planet/moon in the galaxy — barren AND life-bearing — for the given categories, in
; ONE cohesive command. Shows the immersive CPSRecallMessage intro (the "main MSG") ONCE, runs both
; galaxy sweeps with their individual result popups SUPPRESSED, then shows ONE combined result. Together
; the two sweeps cover the whole galaxy (the barren sweep deliberately skips living worlds, so both are
; needed). DATA + ref-free species green are written galaxy-wide; the in-world trait OBJECTS still finish
; on-foot / auto-resolve on arrival (see CompleteLifePlanets).
;   cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "all"
;   cgf "CompletePlanetSurveyQuest.CompleteAllPlanets" "resources,traits"
Function CompleteAllPlanets(string asCategories) global
    CompletePlanetSurveyNative.CancelPendingAutoComplete()
    ; CompleteBarrenPlanets shows the immersive CPSRecallMessage intro; both run with abShowResult=false
    ; so neither pops its own box — we present ONE cohesive combined result for the whole galaxy.
    int barren = CompleteBarrenPlanets(asCategories, false)
    int life   = CompleteLifePlanets(asCategories, false)
    Debug.MessageBox("Galaxy survey complete.  " + barren + " lifeless and " + life + " living worlds catalogued (" + asCategories + ").")
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

; Mark every trait keyword on a planet KNOWN (the engine's own off-planet path: 938333 PlayerKnowledge).
; This is the durable survey-data side of traits — survey %, the TRAITS panel, the galaxy map. The
; in-world "Unknown Feature" objects are handled separately by _CompleteTraitScanObjects. Returns the
; count newly marked.
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

; True if the category list asks for creatures (any of fauna/flora/species/creatures, or "all").
bool Function _WantsSpecies(string asCategories) global
    Return CompletePlanetSurveyNative.CategoryEnabled(asCategories, "fauna") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "flora") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "species") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "creatures")
EndFunction

; INTERNAL — the on-scan auto-complete dispatch target. The C++ poller calls this by name (no args)
; when the "auto-complete on scan" setting is on. Not a player command.
Function _AutoCompleteCurrentPlanet() global
    CompletePlanet("all")
EndFunction
