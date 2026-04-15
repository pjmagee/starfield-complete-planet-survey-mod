ScriptName CompletePlanetSurveyQuest

; The mod's Papyrus surface. Three contexts produce the same end state — the
; scanned planet's survey at 100%:
;
;   1. LANDED  — player E-scans flora/fauna/resource. The SFSE call-site hook
;                (ID_52157 -> ID_97853) fires CompleteSurveyIfEnabled, which
;                gates on CPSScanAutoComplete (GPOF 0x80C) and runs the full
;                per-biome + aggregator completion on the current planet.
;   2. ORBIT   — player opens starmap, picks the planet they're orbiting, R.
;   3. REMOTE  — player opens starmap, picks a planet in range (Astro perk),
;                R. No physical presence required.
;
; Cases 2 and 3 both fire Actor-alias OnShipScan in CPSRemoteScanQuestScript,
; which forwards the scanned Planet to HandleShipScan here. One code path,
; different entry points.

; ---------------------------------------------------------------------------
; Core completion routine. abLanded=true means the player is physically on
; akPlanet — we do the per-biome species pass via GetBiomeFlora/Actors.
; abLanded=false (orbit/remote) skips that pass because those accessors only
; return data when the player is in a biome cell; MarkEverythingForPlanet's
; aggregator sweep still marks every form the engine tracks.
;
; Known limitation: for remote scans the per-planet knowledge DB is sparse
; (engine populates biome-specific slots only on landing), so this can cap
; at ~97% on un-visited planets until a follow-up native is added that
; materializes the missing slots. Landed scans reach 100%.
; ---------------------------------------------------------------------------
Function CompleteSurveyForPlanet(Planet akPlanet, bool abLanded) global
    If akPlanet == None
        Return
    EndIf

    float surveyBefore = akPlanet.GetSurveyPercent()
    Form planetForm = akPlanet as Form

    int floraCount = 0
    int faunaCount = 0
    If abLanded
        ObjectReference playerRef_OR = Game.GetPlayer() as ObjectReference
        Flora[]     biomeFlora  = playerRef_OR.GetBiomeFlora(1.0)
        ActorBase[] biomeActors = playerRef_OR.GetBiomeActors(1.0)
        floraCount = MarkSpecies(planetForm, biomeFlora as Form[])
        faunaCount = MarkSpecies(planetForm, biomeActors as Form[])
    EndIf

    Keyword[] traitKw = akPlanet.GetKeywordTypeList(44)
    int traitCount = MarkTraits(akPlanet, traitKw)

    ; Materialize biome-specific species slots (flora/fauna per biome) BEFORE
    ; the aggregator sweep so MarkEverythingForPlanet sees them.
    int biomeCount = CompletePlanetSurveyNative.MarkAllBiomeSpecies(planetForm, 100)

    int everything = CompletePlanetSurveyNative.MarkEverythingForPlanet(planetForm, 100)

    float surveyAfter = akPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("marked flora=" + floraCount + " fauna=" + faunaCount + " traits=" + traitCount + " biome=" + biomeCount + " all=" + everything + " landed=" + abLanded + " survey=" + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
    Debug.Notification("Survey: " + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
EndFunction

; ---------------------------------------------------------------------------
; LANDED path — console + hook entry points.
; ---------------------------------------------------------------------------

; Manual trigger from console. Console:
;   cgf "CompletePlanetSurveyQuest.CompleteSurvey"
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
    CompleteSurveyForPlanet(currentPlanet, true)
EndFunction

; Called by the C++ scan hook on every flora/fauna E-scan. Gated on the
; CPSScanAutoComplete toggle (GPOF 0x80C).
Function CompleteSurveyIfEnabled() global
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

; ---------------------------------------------------------------------------
; ORBIT / REMOTE path — OnShipScan event handler entry.
; Called by CPSRemoteScanQuestScript when the player triggers a starmap scan.
; Gated on CPSRemoteScanAutoComplete toggle (GPOF 0x807).
; ---------------------------------------------------------------------------
Function HandleShipScan(Planet akPlanet) global
    int pid = 0
    If akPlanet != None
        pid = (akPlanet as Form).GetFormID()
    EndIf
    CompletePlanetSurveyNative.DebugLog("HandleShipScan: enter planet=0x" + pid)

    Form gpofForm = Game.GetFormFromFile(0x807, "CompletePlanetSurvey.esm")
    GameplayOption gpofOption = gpofForm as GameplayOption
    If gpofOption == None
        CompletePlanetSurveyNative.DebugLog("HandleShipScan: GPOF 0x807 not found")
        Return
    EndIf
    float toggleVal = gpofOption.GetValue()
    CompletePlanetSurveyNative.DebugLog("HandleShipScan: CPSRemoteScanAutoComplete=" + toggleVal)
    If toggleVal < 0.5
        CompletePlanetSurveyNative.DebugLog("HandleShipScan: toggle OFF, skipping")
        Return
    EndIf

    If akPlanet == None
        CompletePlanetSurveyNative.DebugLog("HandleShipScan: null planet")
        Return
    EndIf
    float pct = akPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("HandleShipScan: survey%=" + (pct * 100) as int)
    If pct >= 1.0
        CompletePlanetSurveyNative.DebugLog("HandleShipScan: already 100%, skipping")
        Return
    EndIf

    Actor playerRef = Game.GetPlayer()
    bool landedHere = (playerRef.GetCurrentPlanet() == akPlanet) && !playerRef.IsInInterior()
    CompletePlanetSurveyNative.DebugLog("HandleShipScan: landedHere=" + landedHere + ", calling CompleteSurveyForPlanet")
    CompleteSurveyForPlanet(akPlanet, landedHere)
EndFunction

; ---------------------------------------------------------------------------
; DEBUG / TESTING.
; ---------------------------------------------------------------------------

; Complete any planet by FormID. Console:
;   cgf "CompletePlanetSurveyQuest.CompletePlanetById" 0x5E2B5
Function CompletePlanetById(int aiFormId) global
    Form f = Game.GetForm(aiFormId)
    Planet p = f as Planet
    If p == None
        Debug.Notification("0x" + aiFormId + " is not a planet")
        Return
    EndIf
    CompleteSurveyForPlanet(p, false)
EndFunction

; RE diagnostic. Hex-dump the orbited planet's memory layout. Console:
;   cgf "CompletePlanetSurveyQuest.DumpPlanetLayout"
Function DumpPlanetLayout() global
    spaceshipreference ship = Game.GetPlayerHomeSpaceShip()
    Planet p = None
    If ship != None
        p = ship.GetCurrentPlanet()
    EndIf
    If p == None
        Debug.Notification("Ship not orbiting a planet")
        Return
    EndIf
    CompletePlanetSurveyNative.DumpPlanetLayout(p as Form)
    Debug.Notification("Planet layout dumped (see log)")
EndFunction

; RE diagnostic. Hex-dump a planet's memory layout by FormID. Console expects
; DECIMAL, not hex — cgf doesn't parse 0x prefix. Example: 0x5E2B5 = 385717.
;   cgf "CompletePlanetSurveyQuest.DumpPlanetLayoutById" 385717
Function DumpPlanetLayoutById(int aiFormId) global
    Form f = Game.GetForm(aiFormId)
    If f == None
        Debug.Notification("Form 0x" + aiFormId + " not found")
        Return
    EndIf
    CompletePlanetSurveyNative.DumpPlanetLayout(f)
    Debug.Notification("Planet layout dumped (see log)")
EndFunction

; ---------------------------------------------------------------------------
; Shared internals.
; ---------------------------------------------------------------------------

int Function MarkSpecies(Form planetForm, Form[] speciesList) global
    int marked = 0
    int i = 0
    While i < speciesList.Length
        If speciesList[i] != None
            If CompletePlanetSurveyNative.MarkSpeciesScannedForPlanet(planetForm, speciesList[i], 100)
                marked += 1
            EndIf
        EndIf
        i += 1
    EndWhile
    Return marked
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
