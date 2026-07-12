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
;
; Issue #13 — re-entrancy gate: CompletePlanet/CompleteBarrenPlanets/CompleteLifePlanets/
; CompleteAllPlanets and the two auto-complete dispatch targets (_AutoCompleteCurrentPlanet,
; _GalaxyMapScanComplete) all acquire CompletePlanetSurveyNative.TryBeginRun(...) before doing any
; work and release it via EndRun(...) on every exit, so a second invocation (manual re-run, or an
; auto-scan firing mid-sweep) is CLEANLY REJECTED instead of interleaving with an in-flight run and
; corrupting the native-side caches the chunked sweeps walk by index across frames. Public commands
; reject with a player-visible Debug.Notification + a log line; the two auto-complete paths only log
; (they can fire on every routine scan and would otherwise be noisy). CompleteAllPlanets acquires ONE
; gate for both sweeps and drives them via the ungated _CompleteBarrenPlanetsCore/
; _CompleteLifePlanetsCore functions — calling the gated public wrappers from inside an
; already-gated run would immediately reject itself (the gate is one process-wide flag, not
; per-caller-reentrant).

;=================================================================================================
; PINNED ESM FORMIDS (issue #12) — every literal FormID this mod reads from Data/CompletePlanetSurvey.esm
; is centralized HERE, in ONE place, as named accessor functions (Papyrus has no const/global cross-
; script constant — a plain "int Property X = 0x807 AutoReadOnly" isn't usable here either, since
; every caller is a `global` static function with no bound object instance to hold it). Every call
; site below goes through these + ResolveEsmForm() instead of a bare Game.GetFormFromFile(0x80C, ...)
; literal, so there is exactly one place to update if the ESM is ever regenerated.
;
; Creation Kit REASSIGNS FormIDs on every save of a master, and CK can't edit a master in place — so
; a CK regen of CompletePlanetSurvey.esm can silently SHIFT every one of these ids. See
; docs/ESM-BINARY-EDIT-CHECKLIST.md for the safe (non-CK, FormID-preserving) way to add a NEW record
; to the master, and verify all THREE ids below in xEdit/SF1Edit after any CK regen regardless.
;
; FAILURE POLICY when a pinned form can't be resolved — either NOT FOUND (ResolveEsmForm returns
; None) or WRONG TYPE (the form resolved but the cast failed: the classic renumbered-FormID case,
; where the id now points at some OTHER record — worse than missing, because nothing else notices):
;   - Settings TOGGLES (GPOF) are FAIL-CLOSED: the caller treats both failures the same as "off",
;     since we cannot safely assume the player's intended value. The toggle becomes an inert no-op —
;     never a crash, never a guess — until the ESM is fixed. (ResolveEsmToggle wraps both checks.)
;   - The cosmetic intro MESSAGE is FAIL-OPEN: the underlying sweep/completion still runs; only the
;     immersive popup is skipped. A player never loses functionality over a missing flavor popup.
;   Both failure modes LOG a clear, named ERROR (DebugLogError -> spdlog error) — the old asymmetry
;   (the message form failed silently while the toggles logged) is gone; every pinned form logs the
;   same way, for missing AND wrong-type alike.
;=================================================================================================
int Function _FormId_RecallMessage() global
    Return 0x807   ; MESG CPSRecallMessage — CompleteBarrenPlanets' immersive intro popup (cosmetic)
EndFunction

int Function _FormId_HandScannerToggle() global
    Return 0x80C   ; GPOF CPSScanAutoComplete — Settings > Gameplay "Hand Scanner" toggle
EndFunction

int Function _FormId_OrbitalScannerToggle() global
    Return 0x80D   ; GPOF CPSGalaxyMapScan — Settings > Gameplay "Orbital Scanner" toggle
EndFunction

; Resolve-or-log helper (issue #12): the ONE place every pinned FormID above is actually looked up.
; asDebugName should name what the form IS, not just repeat the hex, so the log line is self-
; explaining without cross-referencing this file (e.g. "GPOF CPSScanAutoComplete (Hand Scanner
; toggle, 0x80C)"). Failures log at real ERROR level via the DebugLogError native (spdlog::error),
; so they read as [E] in the SFSE log and survive any level filtering.
Form Function ResolveEsmForm(int aiFormId, string asDebugName) global
    Form f = Game.GetFormFromFile(aiFormId, "CompletePlanetSurvey.esm")
    If f == None
        CompletePlanetSurveyNative.DebugLogError("ResolveEsmForm: '" + asDebugName + "' not found in CompletePlanetSurvey.esm — ESM missing, not enabled, or FormID reassigned by a CK regen (verify in xEdit; see docs/ESM-BINARY-EDIT-CHECKLIST.md)")
    EndIf
    Return f
EndFunction

; Toggle-flavored wrapper: resolve AND cast to GameplayOption, logging a named error on EITHER
; failure. The wrong-type branch matters (issue #12 review): a renumbered FormID that now points at
; some other record resolves non-None but casts to None — without this log that's a silent Return,
; exactly the failure mode the checklist warns about. Returns None on any failure (fail-closed).
GameplayOption Function ResolveEsmToggle(int aiFormId, string asDebugName) global
    Form f = ResolveEsmForm(aiFormId, asDebugName)   ; logs the not-found case itself
    If f == None
        Return None
    EndIf
    GameplayOption opt = f as GameplayOption
    If opt == None
        CompletePlanetSurveyNative.DebugLogError("ResolveEsmToggle: '" + asDebugName + "' resolved but is NOT a GameplayOption — FormID renumbered to a different record type? (verify in xEdit; see docs/ESM-BINARY-EDIT-CHECKLIST.md)")
    EndIf
    Return opt
EndFunction

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
    ; Issue #13 re-entrancy gate: see the file-header note. Rejected cleanly (no cache touched) if a
    ; run — including an auto-complete dispatch — is already active.
    If !CompletePlanetSurveyNative.TryBeginRun("CompletePlanet")
        Debug.Notification("Survey: a completion run is already in progress — wait for it to finish")
        CompletePlanetSurveyNative.DebugLog("CompletePlanet: rejected — a completion run is already in progress")
        Return
    EndIf
    _CompletePlanetCore(asCategories)
    CompletePlanetSurveyNative.EndRun("CompletePlanet")
EndFunction

; The actual on-surface completion body (issue #13: extracted so _AutoCompleteCurrentPlanet — the
; hand-scanner auto-complete dispatch target — can run it under ITS OWN gate acquisition, without a
; nested TryBeginRun that would immediately reject itself; the gate is one process-wide flag, not
; per-caller-reentrant). Reachable only through CompletePlanet or _AutoCompleteCurrentPlanet — never
; call this directly from the console (it does no category validation of its own).
Function _CompletePlanetCore(string asCategories) global
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
    ; Issue #13 re-entrancy gate: acquired BEFORE any work (including the intro popup below) so a
    ; second invocation can't even start the chunked Phase 1 loop while ours is mid-sweep. See the
    ; file-header note + TryBeginRun's doc comment (CompletePlanetSurveyNative.psc) for the
    ; stuck-gate failsafe that keeps a dead run from bricking the command for the rest of the session.
    If !CompletePlanetSurveyNative.TryBeginRun("CompleteBarrenPlanets")
        Debug.Notification("Survey: a completion run is already in progress — wait for it to finish")
        CompletePlanetSurveyNative.DebugLog("CompleteBarrenPlanets: rejected — a completion run is already in progress")
        Return 0
    EndIf
    int result = _CompleteBarrenPlanetsCore(asCategories, abShowResult)
    CompletePlanetSurveyNative.EndRun("CompleteBarrenPlanets")
    Return result
EndFunction

; The actual barren-galaxy sweep body (issue #13: extracted so CompleteAllPlanets can run it under
; ITS OWN single gate acquisition — see CompleteAllPlanets — without a nested TryBeginRun, which
; would immediately reject itself; the gate is one process-wide flag, not per-caller-reentrant).
; Reachable only through CompleteBarrenPlanets or CompleteAllPlanets — never call this directly.
int Function _CompleteBarrenPlanetsCore(string asCategories, bool abShowResult) global
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
    ; FAIL-OPEN (see the PINNED ESM FORMIDS policy block above): a missing OR wrong-type form only
    ; skips the popup (both logged, not silent) — the sweep below runs regardless.
    Form recallForm = ResolveEsmForm(_FormId_RecallMessage(), "MESG CPSRecallMessage (intro popup, 0x807)")
    Message recallMsg = recallForm as Message
    If recallForm != None && recallMsg == None
        ; Resolved but wrong type = renumbered FormID pointing at another record (issue #12 review).
        CompletePlanetSurveyNative.DebugLogError("CompleteBarrenPlanets: 'MESG CPSRecallMessage (intro popup, 0x807)' resolved but is NOT a Message — FormID renumbered to a different record type? (verify in xEdit; see docs/ESM-BINARY-EDIT-CHECKLIST.md)")
    EndIf
    If recallMsg != None
        recallMsg.Show()
    EndIf

    ; Phase timings via Utility.GetCurrentRealTime (real seconds) so the log shows how long each
    ; phase actually takes. tStart is captured AFTER the popup closes, so it excludes the player's
    ; reading time and measures pure compute. Phase 1 also logs precise ms on the C++ side.
    float tStart = Utility.GetCurrentRealTime()

    ; Issue #13: the re-entrancy gate is already held here — acquired by the CompleteBarrenPlanets
    ; wrapper (or CompleteAllPlanets) BEFORE this Core function was even called — so the chunked
    ; Phase 1 loop below, which widens the mid-run window across frames (Utility.Wait between
    ; chunks), cannot be interleaved by a second invocation.

    ; 1) Phase 1 chunked across frames (issue #9). ENUMERATE all barren PNDT formIds (cheap), then
    ;    process them in chunks of kBarrenChunkSize with a short Wait between so the ~1798-planet
    ;    work does not hitch a single frame. Per-planet semantics (guard → discover → write →
    ;    post-check → conditional event → straggler/fault) are unchanged; the consecutive-fault
    ;    streak spans chunks; on abort (-1) remaining worlds are notAttempted and we break.
    ;    Chunk size 150: ballpark 100–200 keeps a chunk under a frame if per-planet work is
    ;    ~0.5–1 ms (discover + write + two IsPlanetFullyMarked checks), while limiting Wait hops
    ;    to ~12 for a full barren galaxy. abWriteResources = doResources (same as before).
    int kBarrenChunkSize = 150
    int workCount = CompletePlanetSurveyNative.CompleteAllPlanetsSurveyData(doResources)
    int startIdx = 0
    While startIdx < workCount
        int advanced = CompletePlanetSurveyNative.SweepBarrenChunk(startIdx, kBarrenChunkSize, doResources)
        If advanced < 0
            ; Consecutive-fault cap aborted the sweep — remaining notAttempted already recorded.
            startIdx = workCount
        ElseIf advanced == 0
            startIdx = workCount
        Else
            startIdx += advanced
            ; Yield so the engine can run a frame (slate cascade / UI / async entry creates).
            If startIdx < workCount
                Utility.Wait(0.1)
            EndIf
        EndIf
    EndWhile
    int n = CompletePlanetSurveyNative.GetSweepCompletedCount()
    float tAfterSweep = Utility.GetCurrentRealTime()

    ; 2) STRAGGLER-ONLY finalize with bounded retry (issue #6). Phase 1 already writes each planet's
    ;    state and — once fully written — fires its completion event in the same frame (write-before-
    ;    event, issue #8), AND records exactly which planets it could NOT fully write (the async
    ;    ID_102650 knowledge-entry create hadn't flushed, so the write no-op'd). Only THOSE need the
    ;    mop-up — planets completed in-frame need no finalize call at all, so the historical restamp
    ;    of the whole sweep is gone (the perf win). Bounded: at most 3 passes with Utility.Wait(1.0)
    ;    before each (entry creates flush within moments — far faster than the minutes-long award-
    ;    queue drain — so 1s per pass is generous), NEVER unbounded. FinalizeSweptPlanet removes a
    ;    resolved planet from the native list, so later passes only re-try what is still pending
    ;    (iterate BACKWARDS: removal keeps the remaining indices valid). Resources-path only — the
    ;    finalize writes resources, so a traits-only run must never invoke it (its stragglers, if
    ;    any, are fault-path records with nothing to finalize). After the final pass,
    ;    ReportSweepFailures(true) logs every never-resolved planet at ERROR (formId + name +
    ;    failure mode) and returns the count for the result popup below.
    int failedCount = 0
    If doResources
        int pass = 0
        int remaining = CompletePlanetSurveyNative.GetStragglerCount()
        While pass < 3 && remaining > 0
            Utility.Wait(1.0)   ; let the deferred entry creates flush before (re)trying
            int beforePass = remaining
            int si = CompletePlanetSurveyNative.GetStragglerCount() - 1
            While si >= 0
                int sfid = CompletePlanetSurveyNative.GetStragglerFormIdAt(si)
                If sfid != 0
                    CompletePlanetSurveyNative.FinalizeSweptPlanet(sfid)   ; 1 = complete (self-removes), 0 = still not ready
                EndIf
                si -= 1
            EndWhile
            remaining = CompletePlanetSurveyNative.GetStragglerCount()
            pass += 1
            CompletePlanetSurveyNative.DebugLog("Finalize pass " + pass + "/3: stragglers " + beforePass + " -> " + remaining)
        EndWhile
        failedCount = CompletePlanetSurveyNative.ReportSweepFailures(true)
    EndIf
    ; Sweep-abort surfacing: worlds the C++ sweep never even ATTEMPTED (its consecutive-fault cap
    ; tripped and aborted the sweep). Separate from failedCount — those planets are in neither the
    ; straggler list nor the failure report — so the popup below never under-reports an aborted sweep.
    int notAttempted = CompletePlanetSurveyNative.GetSweepNotAttemptedCount()

    ; 3) Trait pass + completion tally across ALL swept planets (unchanged behaviour): TRAITS marks
    ;    each world's trait keywords known; the 100% count feeds the result popup. The slate is a
    ;    100%-side-effect of the writes above, so a traits-only run on a resource-incomplete world
    ;    correctly drops none.
    int count = CompletePlanetSurveyNative.GetSweepPlanetCount()
    int traitsMarked = 0
    int fullyComplete = 0
    int i = 0
    While i < count
        int fid = CompletePlanetSurveyNative.GetSweepPlanetFormIdAt(i)
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

    CompletePlanetSurveyNative.DebugLog("Sweep result: " + n + " scanned, " + fullyComplete + " / " + count + " at 100%, " + traitsMarked + " traits marked, " + failedCount + " stragglers unresolved, " + notAttempted + " not attempted")

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
        string resultMsg = "Survey data catalogued across the galaxy.  " + fullyComplete + " lifeless worlds fully surveyed; worlds with flora & fauna are mapped and ready — run CompleteLifePlanets (or land on one) to catalogue their life.  Done in " + (secTotal as int) + "s."
        If failedCount > 0
            ; Straggler-failure surfacing (issue #6): a non-zero count is visible in the popup, not
            ; just the log. The SFSE log names each planet (formId + name + failure mode) at ERROR.
            resultMsg += "  WARNING: " + failedCount + " worlds could not be finalized — see the SFSE log for the list (re-run the command to retry)."
        EndIf
        If notAttempted > 0
            resultMsg += "  WARNING: the sweep aborted early after repeated faults — " + notAttempted + " worlds were not attempted (see the SFSE log; re-run the command to retry)."
        EndIf
        Debug.MessageBox(resultMsg)
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
    ; Issue #13 re-entrancy gate: see the file-header note + TryBeginRun's doc comment
    ; (CompletePlanetSurveyNative.psc) for the stuck-gate failsafe.
    If !CompletePlanetSurveyNative.TryBeginRun("CompleteLifePlanets")
        Debug.Notification("Survey: a completion run is already in progress — wait for it to finish")
        CompletePlanetSurveyNative.DebugLog("CompleteLifePlanets: rejected — a completion run is already in progress")
        Return 0
    EndIf
    int result = _CompleteLifePlanetsCore(asCategories, abShowResult)
    CompletePlanetSurveyNative.EndRun("CompleteLifePlanets")
    Return result
EndFunction

; The actual life-galaxy body (issue #13: extracted so CompleteAllPlanets can run it under ITS OWN
; single gate acquisition — see CompleteAllPlanets — without a nested TryBeginRun, which would
; immediately reject itself; the gate is one process-wide flag, not per-caller-reentrant). Reachable
; only through CompleteLifePlanets or CompleteAllPlanets — never call this directly.
int Function _CompleteLifePlanetsCore(string asCategories, bool abShowResult) global
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
    ; Issue #13 re-entrancy gate: ONE acquisition covers BOTH sweeps below, which is why they are
    ; driven via the ungated _CompleteBarrenPlanetsCore/_CompleteLifePlanetsCore functions instead of
    ; the gated public CompleteBarrenPlanets/CompleteLifePlanets — calling those here would nest a
    ; second TryBeginRun under this one and immediately reject itself (the gate is one process-wide
    ; flag, not per-caller-reentrant). See the file-header note + TryBeginRun's doc comment
    ; (CompletePlanetSurveyNative.psc) for the stuck-gate failsafe.
    If !CompletePlanetSurveyNative.TryBeginRun("CompleteAllPlanets")
        Debug.Notification("Survey: a completion run is already in progress — wait for it to finish")
        CompletePlanetSurveyNative.DebugLog("CompleteAllPlanets: rejected — a completion run is already in progress")
        Return
    EndIf
    ; CompleteBarrenPlanets shows the immersive CPSRecallMessage intro; both run with abShowResult=false
    ; so neither pops its own box — we present ONE cohesive combined result for the whole galaxy.
    int barren = _CompleteBarrenPlanetsCore(asCategories, false)
    int life   = _CompleteLifePlanetsCore(asCategories, false)
    ; Straggler-failure surfacing (issue #6): CompleteBarrenPlanets ran with its popup suppressed, so
    ; re-read the residual failure + not-attempted counts for the combined popup. abLogErrors=false —
    ; the barren sweep already logged each failed planet at ERROR; this only fetches the count (no
    ; duplicate lines). Gated on the categories that actually ran the sweep — a species-only run never
    ; swept, and reading the lists then would report a PREVIOUS run's stale residue: "resources" is
    ; the only path that runs the finalize passes; the sweep itself (and so a fault-cap abort) runs
    ; for resources OR traits.
    int failed = 0
    int notAttempted = 0
    If CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources")
        failed = CompletePlanetSurveyNative.ReportSweepFailures(false)
    EndIf
    If CompletePlanetSurveyNative.CategoryEnabled(asCategories, "resources") || CompletePlanetSurveyNative.CategoryEnabled(asCategories, "traits")
        notAttempted = CompletePlanetSurveyNative.GetSweepNotAttemptedCount()
    EndIf
    string failNote = ""
    If failed > 0
        failNote = "  WARNING: " + failed + " worlds could not be finalized — see the SFSE log for the list (re-run the command to retry)."
    EndIf
    If notAttempted > 0
        failNote += "  WARNING: the sweep aborted early after repeated faults — " + notAttempted + " worlds were not attempted (see the SFSE log; re-run the command to retry)."
    EndIf
    ; barren/life are NEWLY-completed counts (0 when everything was already done), so a re-run reads
    ; honestly as "already surveyed" instead of always re-claiming the whole galaxy. FAILURE-FIRST
    ; precedence: any failure/abort makes the headline "finished with problems" — never the
    ; contradictory "already fully surveyed ... WARNING: N worlds could not be finalized".
    If failed + notAttempted > 0
        Debug.MessageBox("Galaxy survey finished with problems.  " + barren + " lifeless and " + life + " living worlds catalogued (" + asCategories + ")." + failNote)
    ElseIf barren + life == 0
        Debug.MessageBox("Galaxy already fully surveyed — nothing new to catalogue (" + asCategories + ").")
    Else
        Debug.MessageBox("Galaxy survey complete.  " + barren + " lifeless and " + life + " living worlds catalogued (" + asCategories + ").")
    EndIf
    CompletePlanetSurveyNative.EndRun("CompleteAllPlanets")
EndFunction

; Called by the C++ scan hook on every species/resource scan. Reads the
; Settings > Gameplay toggle, short-circuits if disabled or planet already
; complete, then QUEUES CompleteSurvey. C++ poller dispatches it once the
; scanner UI has closed — avoids PlaceAtMe racing with live scanner state,
; which is what crashed the direct-dispatch path.
Function CompleteSurveyIfEnabled() global
    ; FAIL-CLOSED (see the PINNED ESM FORMIDS policy block near the top of this file): a missing
    ; toggle form is treated as "off" — logged by ResolveEsmForm, not silent.
    GameplayOption gpofOption = ResolveEsmToggle(_FormId_HandScannerToggle(), "GPOF CPSScanAutoComplete (Hand Scanner toggle, 0x80C)")
    If gpofOption == None
        Return
    EndIf
    If gpofOption.GetValue() < 0.5
        Return
    EndIf

    ; The toggle only MEANS something if the native hook that is SUPPOSED to invoke this function is
    ; actually armed (issue #12). NOTE: on a real sig-scan miss this guard normally never executes —
    ; a missing hook simply never dispatches here, so flipping the toggle produces NO Papyrus log
    ; line; the only signals are Hook::Install's load-time ERROR + one-time player notice. This is
    ; defense-in-depth for any future DIRECT invocation of this function without the hook armed.
    If !CompletePlanetSurveyNative.IsHandScannerHookInstalled()
        CompletePlanetSurveyNative.DebugLog("CompleteSurveyIfEnabled: Hand Scanner hook not installed this session (sig-scan miss at load — see the SFSE log) — ignoring")
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
    ; FAIL-CLOSED (see the PINNED ESM FORMIDS policy block near the top of this file): a missing
    ; toggle form is treated as "off" — logged by ResolveEsmForm, not silent.
    GameplayOption gpofOption = ResolveEsmToggle(_FormId_OrbitalScannerToggle(), "GPOF CPSGalaxyMapScan (Orbital Scanner toggle, 0x80D)")
    If gpofOption == None
        Return
    EndIf
    If gpofOption.GetValue() < 0.5
        Return   ; setting off -> vanilla galaxy-map scan
    EndIf

    ; Same hook-armed guard as CompleteSurveyIfEnabled (issue #12): normally never executes on a real
    ; sig-scan miss (a missing hook never dispatches here — no Papyrus log line results; the signals
    ; are the load-time ERROR + notice). Defense-in-depth against a future direct invocation.
    If !CompletePlanetSurveyNative.IsOrbitalScannerHookInstalled()
        CompletePlanetSurveyNative.DebugLog("_GalaxyMapScanComplete: Orbital Scanner hook not installed this session (sig-scan miss at load — see the SFSE log) — ignoring")
        Return
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

    ; Issue #13 re-entrancy gate: this auto-complete dispatch touches the same _CompletePlanetForm
    ; machinery a manual command uses, so it must not fire while a chunked galaxy sweep is mid-run.
    ; Log-only on rejection (no player-facing toast) — this can trigger on any routine star-map scan,
    ; and a manual command already wins over any auto-complete via CancelPendingAutoComplete.
    If !CompletePlanetSurveyNative.TryBeginRun("AutoComplete (Orbital Scanner)")
        CompletePlanetSurveyNative.DebugLog("_GalaxyMapScanComplete: auto-complete deferred — a completion run is already in progress")
        Return
    EndIf

    ; abDiscover=true: a map-scanned body may be never-visited, so ensure its knowledge entry exists
    ; first — UNLESS it's the planet we're physically on (then skip discover to avoid evicting fresh
    ; markers, exactly as CompletePlanet does).
    Bool onIt = (Game.GetPlayer().GetCurrentPlanet() == p)
    float surveyAfter = _CompletePlanetForm(p, "all", !onIt)
    CompletePlanetSurveyNative.EndRun("AutoComplete (Orbital Scanner)")
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
; Issue #13: gated with its OWN TryBeginRun/EndRun (calls _CompletePlanetCore directly, NOT the
; gated CompletePlanet wrapper — nesting would immediately reject itself). Log-only on rejection (no
; player-facing toast) — this can fire on every routine hand-scanner scan, and a manual command
; already wins over any queued auto-complete via CancelPendingAutoComplete.
Function _AutoCompleteCurrentPlanet() global
    If !CompletePlanetSurveyNative.TryBeginRun("AutoComplete (Hand Scanner)")
        CompletePlanetSurveyNative.DebugLog("_AutoCompleteCurrentPlanet: auto-complete deferred — a completion run is already in progress")
        Return
    EndIf
    _CompletePlanetCore("all")
    CompletePlanetSurveyNative.EndRun("AutoComplete (Hand Scanner)")
EndFunction
