ScriptName CompletePlanetSurveyNative Hidden Native

; Native functions provided by CompletePlanetSurvey.dll (SFSE). All are ref-free (no spawning,
; no teleport) unless noted, and crash-guarded at the Papyrus boundary (GuardedNative in Main.cpp).

Function DebugLog(string asMsg) global native

; Mark a trait keyword as known for the planet (938333 PlayerKnowledge — the engine's own off-planet
; path). Drives survey %, the TRAITS panel and the galaxy map. Fires the trait progress event.
bool Function MarkTraitKnownForPlanet(Form akPlanet, Keyword akKeyword) global native

; GREEN a planet's flora/fauna (step 1 of 2): write the +0x21 scan flag / survey % for every authored
; species (read from Starfield.esm), keyed by the canonical id. No spawn, no scan. Returns species
; flagged (0 = no entry resolved — discover the planet first). Pair with BuildSpeciesMarkers for the
; full green. aiKind: 0 = both, 1 = flora (FLOR only), 2 = fauna (NPC_ only).
int Function TestDirectGreen(Form akPlanet, int aiKind) global native

; GREEN a planet's flora/fauna (step 2 of 2): build the slot+0x08 ESM-derived attribute-marker
; catalogue (genetics / reproduction / temperament / abilities) for each species, so the outline
; renders PROPERLY green (with info) after a reload — not the half-green a bare flag leaves. Returns
; species built. aiKind: 0 = both, 1 = flora (FLOR only), 2 = fauna (NPC_ only).
int Function TestBuildArray(Form akPlanet, int aiKind) global native

; RESOURCES category (pure): mark the planet's attribute bits + resource scan flags via the
; ID_1016657 aggregator, EXCLUDING flora/fauna (species are greened only by the green path) and
; trait keywords. Fires the survey-complete event that drops the Survey Data slate at 100%.
int  Function MarkResourcesForPlanet(Form akPlanet, int aiDelta) global native

; Ensure a planet's knowledge entry exists (ref-free) via the engine discover ID_102650, so the
; subsequent ref-free writes (resources / species green) actually land on a NEVER-VISITED planet.
; ResolvePlanetSubobj is a pure lookup, so resources + green silently no-op until this runs. Also
; sets the surveyed bit, fires the Survey Data slate, and recurses moons. Returns 1 on success.
int  Function DiscoverPlanetEntry(Form akPlanet) global native

; Queue a deferred CompleteSurvey dispatch. The scan hook calls this instead of invoking CompleteSurvey
; directly so PlaceAtMe doesn't race with the active scanner UI. C++ polls the flag, waits until the
; scanner is closed + grace period, then dispatches Papyrus CompleteSurvey from a clean state.
Function QueueCompleteSurvey() global native

; Cancel a pending auto-complete-on-scan dispatch. A manual completion command calls this first so an
; explicit category command (e.g. CompletePlanet "traits") is not overridden by a queued
; _AutoCompleteCurrentPlanet -> CompletePlanet("all") from an earlier real scan.
Function CancelPendingAutoComplete() global native

; Issue #12 — whether the native call-site hook that is SUPPOSED to invoke CompleteSurveyIfEnabled
; (the Hand Scanner path, ID_52157 -> ID_97853) is actually armed this session. False means a
; sig-scan miss (or an install-time fault) left that hook unpatched on this game build — the SFSE
; log names it at ERROR and the player already saw a one-time notice at load. The Settings toggle
; still reads/writes fine; it just has nothing to drive without the hook, so this exists so the
; toggle handler can no-op sanely rather than silently doing nothing with no signal anywhere.
bool Function IsHandScannerHookInstalled() global native

; Same as IsHandScannerHookInstalled, for the Orbital Scanner / galaxy-map scan hook
; (ID_52173 -> ID_97853) that drives _GalaxyMapScanComplete.
bool Function IsOrbitalScannerHookInstalled() global native

; The form id of the planet/moon the player last scanned on the STAR MAP, captured by the galaxy-map
; scan hook. The poller dispatches _GalaxyMapScanComplete, which reads this to know which body to
; complete. Returns 0 if nothing is pending / the scanned target wasn't a planet.
int Function GetGalaxyScanPlanetFormId() global native

; Queue a repaint of the star-map selected-planet info panel. Called by _GalaxyMapScanComplete after
; it completes a galaxy-map-scanned body; the poller does the actual repaint next frame on the main
; thread (re-invokes the engine's own panel populate on the live StarMap menu), so the panel shows
; 100% in place without a manual deselect/reselect. No-op if the star map isn't open.
Function QueueStarMapRefresh() global native

; Phase 1 ENUMERATE (issue #9): collect every barren PNDT formId into the native work list and reset
; sweep/straggler/fault state. Does NOT discover or write any planet — pair with SweepBarrenChunk.
; abWriteResources is accepted for call-site compatibility (pass the same flag to each chunk).
; Returns the barren work-list size (cursor upper bound for SweepBarrenChunk).
int Function CompleteAllPlanetsSurveyData(bool abWriteResources) global native

; Phase 1 CHUNK (issue #9): process [aiStartIndex, aiStartIndex+aiCount) of the barren work list with
; the same per-planet semantics as the former monolithic sweep (guard → discover → write → post-check
; → conditional event → straggler/fault accounting). Fault streak spans chunks. Returns slots advanced
; (>=0; Papyrus: start += ret), or -1 if the consecutive-fault cap aborted (remaining already counted
; as notAttempted — break the drive loop). abWriteResources: TRUE writes attribute bits + resource
; flags; FALSE only records the barren world for the traits pass (no resource writes).
int Function SweepBarrenChunk(int aiStartIndex, int aiCount, bool abWriteResources) global native

; Attempted+succeeded count from the last Phase 1 (after chunks). Replaces the old
; CompleteAllPlanetsSurveyData return value for "scanned" log lines.
int Function GetSweepCompletedCount() global native

; Accessors over the planets the last CompleteAllPlanetsSurveyData sweep touched, so the finalize pass
; can re-resolve each as a Planet (and mark its traits via GetKeywordTypeList(44) -> MarkTraitKnownForPlanet).
int Function GetSweepPlanetCount() global native
int Function GetSweepPlanetFormIdAt(int aiIndex) global native

; Mop-up for one STRAGGLER planet (by form ID): re-write its survey state (attribute bits +
; species/resource flags — an idempotent restamp), then fire the survey-complete event ONLY on the
; not-complete -> complete transition this call (the C++ sweep fires the event itself for every
; planet it fully wrote in-frame; re-firing would inflate the un-deduped "Planets Fully Surveyed"
; statistic). Called from the bounded finalize retry passes across later frames/seconds — by then
; the sweep's asynchronous knowledge-entry creates have flushed. Returns 1 when the planet reads
; fully marked after this call (it is then REMOVED from the native straggler list, so later passes
; skip it), 0 when it is still unresolved (entry not ready — retry it) or the form didn't resolve.
; Fault-isolated per call: a caught fault returns 0 and leaves the planet queued for retry/report.
int Function FinalizeSweptPlanet(int aiFormId) global native

; The sweep's STRAGGLER set (issue #6): the exact planets Phase 1 could NOT fully write in-frame
; (async knowledge-entry create pending, or the per-planet body faulted). Same access pattern as the
; sweep list. FinalizeSweptPlanet removes a planet once resolved, so the list SHRINKS across retry
; passes — iterate it BACKWARDS (count-1 .. 0) so removals keep the remaining indices valid.
int Function GetStragglerCount() global native
int Function GetStragglerFormIdAt(int aiIndex) global native

; Post-retry failure report: counts the straggler-set planets whose survey state STILL reads
; incomplete after the bounded finalize passes. abLogErrors=true logs each one at ERROR (formId hex,
; planetId, editor id when available, and the failure MODE — form/id unresolved vs entry never
; created vs partial write) so failures are named, not just counted; pass false to re-read the count
; without duplicating the ERROR lines (the combined CompleteAllPlanets popup does this).
int Function ReportSweepFailures(bool abLogErrors) global native

; Barren worlds the last sweep never ATTEMPTED because its consecutive-fault cap aborted it early
; (0 on a healthy run). These are in neither the straggler list nor the failure report — the result
; popups surface this count so an aborted sweep is never silently under-reported.
int Function GetSweepNotAttemptedCount() global native

; Enumerate the UNIQUE life-bearing planets (those with flora/fauna). Call once, then iterate
; 0..count-1 via GetLifePlanetFormIdAt + Game.GetForm(...) as Planet. The galaxy sweep skips living
; worlds, so CompleteLifePlanets uses this list to reach their traits / resources / green.
int Function EnumerateLifePlanets() global native
int Function GetLifePlanetFormIdAt(int aiIndex) global native

; Parse a "resources,traits,fauna,flora" category string (base Papyrus can't split a string):
; returns true if the list contains asToken (case-insensitive) or the wildcard "all".
bool Function CategoryEnabled(string asCategoryList, string asToken) global native

; Validate a category string: true ONLY if every comma-separated token is a recognized option
; (resources/traits/fauna/flora/species/creatures/all) and at least one is present. A typo ("res",
; "creature") or empty string returns false, so a command can no-op cleanly instead of half-running.
bool Function CategoriesValid(string asCategoryList) global native
