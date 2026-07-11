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
    ; Invalid category (typo like "res"/"creature", or empty) -> clean no-op, not a half-run.
    If !CompletePlanetSurveyNative.CategoriesValid(asCategories)
        Debug.Notification("Survey: unknown category in '" + asCategories + "' — use resources, traits, fauna, flora, or all")
        Return
    EndIf
    ; A manual command wins over a queued auto-complete-on-scan: cancel any pending
    ; _AutoCompleteCurrentPlanet -> CompletePlanet("all") so an explicit category isn't overridden.
    CompletePlanetSurveyNative.CancelPendingAutoComplete()

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

    ; abDiscover=false: we're standing on this world, so its knowledge entry already exists and the
    ; async re-discover (ID_102650) would evict freshly written species markers (green -> blue).
    float surveyAfter = _CompletePlanetForm(currentPlanet, asCategories, false)
    Debug.Notification("Planet survey: " + (surveyAfter * 100) as int + "% (" + asCategories + ")")
EndFunction

; Complete ONE planet (by Planet object) for the given categories — the shared core of BOTH the
; on-surface CompletePlanet (current planet) and the galaxy-map scan hook (the scanned body). All
; ref-free (no spawn, no teleport), so it works on a remote/never-visited world by form id.
; abDiscover: ensure the planet's knowledge entry exists first (ref-free engine discover) — REQUIRED
; for a never-visited / remotely-scanned body so the resource + species writes land; pass FALSE for
; the planet you're standing on (its entry exists, and the async re-discover can evict fresh markers).
; Returns the planet's survey percent (0..1) after completion.
float Function _CompletePlanetForm(Planet akPlanet, string asCategories, bool abDiscover) global
    bool doResources = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
    bool doTraits    = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
    bool doSpecies   = _WantsSpecies(asCategories)
    Form planetForm  = akPlanet as Form

    ; Resources + species need the knowledge entry to exist (ResolvePlanetSubobj is a pure lookup) —
    ; discover it ref-free first so those writes land on a never-visited world. Traits are self-sufficient.
    If abDiscover && (doResources || doSpecies)
        CompletePlanetSurveyNative.DiscoverPlanetEntry(planetForm)
    EndIf

    ; Resources before species (so a following green wins — matches the proven completion order).
    int resourceCount = 0
    int traitCount    = 0
    int speciesCount  = 0
    If doResources
        resourceCount = CompletePlanetSurveyNative.MarkResourcesForPlanet(planetForm, 100)
    EndIf
    If doTraits
        ; Trait-known DATA only (survey %, TRAITS panel, galaxy map). Ref-free — the in-world "0/N SCANNED"
        ; pillars resolve on arrival/re-entry via the game's own CheckForScanTargetUpdate (the trait is known).
        traitCount = MarkTraits(akPlanet, akPlanet.GetKeywordTypeList(44))
    EndIf
    If doSpecies
        ; Ref-free green: +0x21 scan flag + the +0x08 ESM-derived marker catalogue. No spawning, no
        ; scanner churn. _SpeciesKind keeps "fauna" = creatures only and "flora" = plants only.
        speciesCount = _GreenPlanet(planetForm, _SpeciesKind(asCategories))
    EndIf
    float surveyAfter = akPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("_CompletePlanetForm[" + asCategories + "]: traits=" + traitCount + " resources=" + resourceCount + " species=" + speciesCount + " survey=" + (surveyAfter * 100) as int + "%")
    _ReconcilePlanetsScanned()   ; surveying this world implies it was scanned -> keep Planets Scanned >= Fully Surveyed
    Return surveyAfter
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
    If !CompletePlanetSurveyNative.CategoriesValid(asCategories)   ; typo/empty -> clean no-op
        Debug.Notification("Survey: unknown category in '" + asCategories + "' — use resources, traits, fauna, flora, or all")
        Return 0
    EndIf
    CompletePlanetSurveyNative.CancelPendingAutoComplete()   ; manual command wins over queued auto-complete
    bool doTraits    = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
    bool doResources = CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
    ; Barren worlds have ONLY resources + traits (no flora/fauna). A pure species run
    ; ("fauna"/"flora"/"species"/"creatures", no resources/traits) has nothing to complete here —
    ; return early so the category is RESPECTED: CompleteAllPlanets "fauna" must green fauna on the
    ; LIFE worlds only and NOT run the barren resource/survey sweep (the "completing entire planets" bug).
    If !doResources && !doTraits
        Return 0
    EndIf
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

    ; 1) C++ sweep over the barren worlds. abWriteResources = doResources: TRUE writes each world's
    ;    attribute bits + resource scan flags (the resources/all path); FALSE just enumerates the barren
    ;    worlds, so the traits-only path can mark trait-known WITHOUT writing any resources.
    int n = CompletePlanetSurveyNative.CompleteAllPlanetsSurveyData(doResources)
    float tAfterSweep = Utility.GetCurrentRealTime()

    ; 2) Per-planet finalize + trait pass across later frames (the sweep's async knowledge-entry creates
    ;    have flushed by now). RESOURCES path: FinalizeSweptPlanet re-applies the survey state and fires
    ;    the completion event — the Survey Data slate drops ONLY at a true 100%. It writes resources, so
    ;    it is GATED on doResources: a traits-only run must never touch resources. TRAITS: mark trait-known.
    ;    The slate is a 100% side-effect either way, so a traits-only run on a resource-incomplete world
    ;    correctly drops none.
    int count = CompletePlanetSurveyNative.GetSweepPlanetCount()
    int traitsMarked = 0
    int fullyComplete = 0
    int i = 0
    While i < count
        int fid = CompletePlanetSurveyNative.GetSweepPlanetFormIdAt(i)
        If doResources
            CompletePlanetSurveyNative.FinalizeSweptPlanet(fid)   ; resource state + slate-at-100%
        EndIf
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
    _ReconcilePlanetsScanned()   ; keep Planets Scanned >= Planets Fully Surveyed after the barren sweep
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
    If !CompletePlanetSurveyNative.CategoriesValid(asCategories)   ; typo/empty -> clean no-op
        Debug.Notification("Survey: unknown category in '" + asCategories + "' — use resources, traits, fauna, flora, or all")
        Return 0
    EndIf
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
    Planet curPlanet = Game.GetPlayer().GetCurrentPlanet()   ; skip the async re-discover for the LIVE planet (below)
    int n = CompletePlanetSurveyNative.EnumerateLifePlanets()
    int i = 0
    While i < n
        int pid = CompletePlanetSurveyNative.GetLifePlanetFormIdAt(i)
        Planet p = Game.GetForm(pid) as Planet
        If p != None
            Bool wasComplete = p.GetSurveyPercent() >= 1.0   ; already fully surveyed BEFORE this run?
            Form pf = p as Form
            ; Resources + species need the knowledge entry to exist (ResolvePlanetSubobj is a pure
            ; lookup) — discover it ref-free first so those writes land on never-visited worlds.
            ; (Trait-KNOWN via MarkTraits is self-sufficient.) EXCEPT the planet you're standing on: its
            ; entry already exists, and DiscoverPlanetEntry's ASYNC re-discover (ID_102650) reconciles the
            ; LIVE creatures back to the engine's view, evicting freshly written markers (e.g. Abilities)
            ; -> green flips to blue. Skip it for the current planet; the on-foot green handles it cleanly.
            If (doResources || doSpecies) && p != curPlanet
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
                ; Always (re)green — idempotent, keeps the outline green — but only COUNT worlds this run
                ; newly greened, so the result popup's "N greened" is honest on re-runs (matches worlds).
                Int gr = _GreenPlanet(pf, _SpeciesKind(asCategories))
                If gr > 0 && !wasComplete
                    greened += 1
                EndIf
            EndIf
            ; Count only worlds this run NEWLY brought forward (were < 100% before), so the result popup
            ; reflects actual work — re-running an already-complete galaxy reports 0, matching the barren
            ; sweep. (The writes above are idempotent no-ops on an already-surveyed world; only the count
            ; changes.) A category that can't reach 100% on its own (e.g. "fauna,flora", no resources) stays
            ; < 100%, so those worlds keep counting each run — expected, they never fully "complete".
            If !wasComplete
                worlds += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    ; FINALIZE PASS. DiscoverPlanetEntry -> ID_102650 creates a never-visited world's knowledge entry
    ; ASYNCHRONOUSLY, so a handful of worlds' entries aren't ready when the same-pass resource/species
    ; writes ran above -> those worlds sit just under 100% after one loop. Utility.Wait yields frames so
    ; the deferred creates flush, then we re-run resources/traits/species for any world STILL < 100%.
    ; We deliberately do NOT re-run DiscoverPlanetEntry here: the entry now exists, and re-discovering
    ; would re-count Planets Scanned (each world is discovered/counted exactly once, in the loop above).
    ; This makes ONE command complete the whole set — the same reason the barren command has a finalize
    ; pass. Only needed when we created entries (doResources/doSpecies); a traits-only run is self-
    ; sufficient (MarkTraits needs no entry) and completes in the first loop.
    If doResources || doSpecies
        Utility.Wait(1.0)
        i = 0
        While i < n
            int fpid = CompletePlanetSurveyNative.GetLifePlanetFormIdAt(i)
            Planet fp = Game.GetForm(fpid) as Planet
            If fp != None && fp.GetSurveyPercent() < 1.0
                Form fpf = fp as Form
                If doResources
                    CompletePlanetSurveyNative.MarkResourcesForPlanet(fpf, 100)
                EndIf
                If doTraits
                    MarkTraits(fp, fp.GetKeywordTypeList(44))
                EndIf
                If doSpecies
                    _GreenPlanet(fpf, _SpeciesKind(asCategories))
                EndIf
            EndIf
            i += 1
        EndWhile
    EndIf

    ; Every life world's traits are marked KNOWN (data) in the loop above; the in-world "0/N SCANNED"
    ; pillars resolve on arrival/re-entry via the game's own CheckForScanTargetUpdate (the trait is known).
    float secs = Utility.GetCurrentRealTime() - t0
    CompletePlanetSurveyNative.DebugLog("CompleteLifePlanets[" + asCategories + "]: " + worlds + " life worlds, " + greened + " greened in " + secs + "s")
    _ReconcilePlanetsScanned()   ; keep Planets Scanned >= Planets Fully Surveyed after the life sweep + finalize
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
    If !CompletePlanetSurveyNative.CategoriesValid(asCategories)   ; typo/empty -> clean no-op (subs not run)
        Debug.Notification("Survey: unknown category in '" + asCategories + "' — use resources, traits, fauna, flora, or all")
        Return
    EndIf
    CompletePlanetSurveyNative.CancelPendingAutoComplete()
    ; CompleteBarrenPlanets shows the immersive CPSRecallMessage intro; both run with abShowResult=false
    ; so neither pops its own box — we present ONE cohesive combined result for the whole galaxy.
    int barren = CompleteBarrenPlanets(asCategories, false)
    int life   = CompleteLifePlanets(asCategories, false)
    ; barren/life are NEWLY-completed counts (0 when everything was already done), so a re-run reads
    ; honestly as "already surveyed" instead of always re-claiming the whole galaxy.
    If barren + life == 0
        Debug.MessageBox("Galaxy already fully surveyed — nothing new to catalogue (" + asCategories + ").")
    Else
        Debug.MessageBox("Galaxy survey complete.  " + barren + " lifeless and " + life + " living worlds catalogued (" + asCategories + ").")
    EndIf
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

; Called by the C++ galaxy-map scan hook (via the per-frame poller) after the player scans a
; planet/moon on the STAR MAP. Reads the "Enable Galaxy Map Scan" toggle (GPOF 0x80D); if OFF the
; scan behaves vanilla (this returns). If ON, it completes the scanned body's ENTIRE survey — the
; same outcome as CompletePlanet "all", but for that specific remote planet (all ref-free, so no
; landing needed). The target form id is captured natively at scan time. Not a player command.
Function _GalaxyMapScanComplete() global
    ; FormID 0x80D = GPOF CPSGalaxyMapScan (the "Orbital Scanner" Settings toggle).
    ; Verify in xEdit if the ESM is ever regenerated — CK reassigns IDs.
    Form gpofForm = Game.GetFormFromFile(0x80D, "CompletePlanetSurvey.esm")
    GameplayOption gpofOption = gpofForm as GameplayOption
    If gpofOption == None
        CompletePlanetSurveyNative.DebugLog("_GalaxyMapScanComplete: GPOF 0x80D not found — ESM missing or FormID changed")
        Return
    EndIf
    If gpofOption.GetValue() < 0.5
        Return   ; setting off -> vanilla galaxy-map scan
    EndIf

    int fid = CompletePlanetSurveyNative.GetGalaxyScanPlanetFormId()
    If fid == 0
        Return   ; no body captured (scan target wasn't a planet, or already consumed)
    EndIf
    Planet p = Game.GetForm(fid) as Planet
    If p == None
        CompletePlanetSurveyNative.DebugLog("_GalaxyMapScanComplete: captured form is not a Planet")
        Return
    EndIf
    If p.GetSurveyPercent() >= 1.0
        Return   ; already fully surveyed — nothing to do
    EndIf

    ; abDiscover=true: a map-scanned body may be never-visited, so ensure its knowledge entry exists
    ; first — UNLESS it's the planet we're physically on (then skip discover to avoid evicting fresh
    ; markers, exactly as CompletePlanet does).
    Bool onIt = (Game.GetPlayer().GetCurrentPlanet() == p)
    float surveyAfter = _CompletePlanetForm(p, "all", !onIt)
    ; Repaint the star-map info panel in place (the panel cached its data when the scan first painted,
    ; BEFORE this completion, so without this it only updates on a manual deselect/reselect). The
    ; poller does the actual repaint next frame on the main thread.
    CompletePlanetSurveyNative.QueueStarMapRefresh()
    Debug.Notification("Planet survey: " + (surveyAfter * 100) as int + "% (galaxy map scan)")
EndFunction

; Mark every trait keyword on a planet KNOWN (the engine's own off-planet path: 938333 PlayerKnowledge).
; The durable, ref-free survey-data side of traits — survey %, the TRAITS panel, the galaxy map. The
; in-world "0/N SCANNED" pillars resolve on arrival/re-entry (the game's own CheckForScanTargetUpdate,
; since the trait is now known). Returns the count newly marked.
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

; True if the category list asks for creatures (any of fauna/flora/species/creatures, or "all").
bool Function _WantsSpecies(string asCategories) global
    Return CompletePlanetSurveyNative.CategoryEnabled(asCategories, "fauna") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "flora") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "species") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "creatures")
EndFunction

; Ensure Planets Scanned >= Planets Fully Surveyed. Surveying a world implies scanning it first, and
; once a world is surveyed you can no longer scan it — so a completion that drives Fully Surveyed up
; must top Planets Scanned up too, or the character sheet shows the impossible "surveyed > scanned"
; (e.g. CompletePlanet on New Atlantis: Fully Surveyed 1, Planets Scanned stuck at 0 forever). Uses the
; game's OWN misc-stat natives (the same system the ModPCMS console command edits). Only bumps UP — it
; never reduces a player's real orbital-scan tally — and is idempotent (a no-op once scanned >= surveyed).
Function _ReconcilePlanetsScanned() global
    int scanned  = Game.QueryStat("Planets Scanned")
    int surveyed = Game.QueryStat("Planets Fully Surveyed")
    If scanned < surveyed
        Game.IncrementStat("Planets Scanned", surveyed - scanned)
    EndIf
EndFunction

; INTERNAL — the on-scan auto-complete dispatch target. The C++ poller calls this by name (no args)
; when the "auto-complete on scan" setting is on. Not a player command.
Function _AutoCompleteCurrentPlanet() global
    CompletePlanet("all")
EndFunction
