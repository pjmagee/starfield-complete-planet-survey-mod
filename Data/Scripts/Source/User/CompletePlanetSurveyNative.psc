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

; The form id of the planet/moon the player last scanned on the STAR MAP, captured by the galaxy-map
; scan hook. The poller dispatches _GalaxyMapScanComplete, which reads this to know which body to
; complete. Returns 0 if nothing is pending / the scanned target wasn't a planet.
int Function GetGalaxyScanPlanetFormId() global native

; Queue a repaint of the star-map selected-planet info panel. Called by _GalaxyMapScanComplete after
; it completes a galaxy-map-scanned body; the poller does the actual repaint next frame on the main
; thread (re-invokes the engine's own panel populate on the live StarMap menu), so the panel shows
; 100% in place without a manual deselect/reselect. No-op if the star map isn't open.
Function QueueStarMapRefresh() global native

; Sweep the barren worlds and record them for the finalize pass. abWriteResources: TRUE writes each
; world's attribute bits + resource scan flags (the resources/all path); FALSE only enumerates them, so a
; traits-only run can mark trait-known without writing resources. Returns the number of planets swept.
int Function CompleteAllPlanetsSurveyData(bool abWriteResources) global native

; Accessors over the planets the last CompleteAllPlanetsSurveyData sweep touched, so the finalize pass
; can re-resolve each as a Planet (and mark its traits via GetKeywordTypeList(44) -> MarkTraitKnownForPlanet).
int Function GetSweepPlanetCount() global native
int Function GetSweepPlanetFormIdAt(int aiIndex) global native

; Fully complete one swept planet (by form ID): write its survey state (attribute bits + species/resource
; flags) and fire the survey-complete event so its Survey Data slate drops. Called from the finalize pass,
; which runs across later frames — by then the sweep's asynchronous knowledge-entry creates have flushed,
; so this also catches the few planets whose entry wasn't ready during the C++ pass. The slate award is
; idempotent (the engine awards a planet's survey reward once). Returns the number of forms marked.
int Function FinalizeSweptPlanet(int aiFormId) global native

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
