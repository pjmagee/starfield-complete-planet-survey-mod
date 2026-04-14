ScriptName CompletePlanetSurveyQuest

; Complete the planet survey for the WHOLE planet (all biomes).
; Invoke via console:  cgf "CompletePlanetSurveyQuest.CompleteSurvey"
;
; Two passes on purpose:
;   1. Per-biome flora/fauna marked via MarkSpeciesScannedForPlanet — flips the
;      per-species scan-flag byte at subobj+0x21 (engine ID_124898 path).
;   2. Planet-wide aggregator sweep via MarkEverythingForPlanet — enumerates
;      every tracked form (all biomes' flora/fauna, resources, misc) via
;      ID_1016657 and marks each "known" in the knowledge DB slot array.
; Both layers are needed to hit 100% + trigger the Survey Data slate.

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

    ObjectReference playerRef_OR = playerRef as ObjectReference
    Flora[]     biomeFlora  = playerRef_OR.GetBiomeFlora(1.0)
    ActorBase[] biomeActors = playerRef_OR.GetBiomeActors(1.0)
    Keyword[]   traitKw     = currentPlanet.GetKeywordTypeList(44)
    Form        planetForm  = currentPlanet as Form

    int floraCount = MarkSpecies(planetForm, biomeFlora as Form[])
    int faunaCount = MarkSpecies(planetForm, biomeActors as Form[])
    int traitCount = MarkTraits(currentPlanet, traitKw)
    int everything = CompletePlanetSurveyNative.MarkEverythingForPlanet(planetForm, 100)

    float surveyAfter = currentPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("marked flora=" + floraCount + " fauna=" + faunaCount + " traits=" + traitCount + " all=" + everything + " survey=" + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
    Debug.Notification("Survey: " + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
EndFunction

; Called by the C++ scan hook on every species/resource scan. Reads the
; Settings > Gameplay toggle, short-circuits early if disabled or the planet
; is already 100% surveyed, then delegates to CompleteSurvey() which does
; its own interior/planet guards.
Function CompleteSurveyIfEnabled() global
    ; FormID 0x80C assigned by Creation Kit to GPOF CPSScanAutoComplete.
    ; Verify in xEdit if the ESM is ever regenerated — CK reassigns IDs.
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
