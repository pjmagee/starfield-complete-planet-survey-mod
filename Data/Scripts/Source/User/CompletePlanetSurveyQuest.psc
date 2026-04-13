ScriptName CompletePlanetSurveyQuest

; Complete the planet survey for the biome the player is currently in.
; Invoke via console:  cgf "CompletePlanetSurveyQuest.CompleteSurvey"
;
; Writes the scan-flag byte directly in the per-planet knowledge DB entry
; for each flora/fauna species in the biome — the exact byte GetSurveyPercent's
; aggregator reads. No spawning, no refs, no harvest.

Function CompleteSurvey() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()

    If currentPlanet == None
        Debug.Notification("Survey: not on a planet")
        Return
    EndIf

    float surveyBefore = currentPlanet.GetSurveyPercent()

    ObjectReference playerRef_OR = playerRef as ObjectReference
    Flora[]     biomeFlora  = playerRef_OR.GetBiomeFlora(1.0)
    ActorBase[] biomeActors = playerRef_OR.GetBiomeActors(1.0)
    Keyword[]   traitKw     = currentPlanet.GetKeywordTypeList(44)
    Form        planetForm  = currentPlanet as Form

    CompletePlanetSurveyNative.DebugLog("biome flora=" + biomeFlora.Length + " fauna=" + biomeActors.Length + " traits=" + traitKw.Length)

    int floraCount = MarkSpecies(planetForm, biomeFlora as Form[])
    int faunaCount = MarkSpecies(planetForm, biomeActors as Form[])
    int traitCount = MarkTraits(currentPlanet, traitKw)
    ; Sweep everything the planet tracks (resources + anything missed): one-shot.
    int everything = CompletePlanetSurveyNative.MarkEverythingForPlanet(planetForm, 100)
    ; ScanNearbyRefs is disabled until the procgen-cell iteration crash is fixed.
    int refsScanned = 0
    ; Mark child locations (e.g. "Frozen Mountains") as explored so the planet map
    ; stops showing partial exploration.
    int locations = CompletePlanetSurveyNative.MarkLocationsExplored()

    float surveyAfter = currentPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("marked flora=" + floraCount + " fauna=" + faunaCount + " traits=" + traitCount + " all=" + everything + " refs=" + refsScanned + " survey=" + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
    Debug.Notification("Survey: " + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%) all=" + everything + " refs=" + refsScanned)
EndFunction

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
