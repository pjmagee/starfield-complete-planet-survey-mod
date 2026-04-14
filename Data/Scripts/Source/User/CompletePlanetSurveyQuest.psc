ScriptName CompletePlanetSurveyQuest

; Complete the planet survey for the biome the player is currently in.
; Invoke via console:  cgf "CompletePlanetSurveyQuest.CompleteSurvey"
;
; Writes the scan-flag byte directly in the per-planet knowledge DB entry
; for each flora/fauna species in the biome — the exact byte GetSurveyPercent's
; aggregator reads. No spawning, no refs, no harvest.

Function CompleteSurvey() global
    Actor playerRef = Game.GetPlayer()

    ; Must be on the planet surface, not inside a ship or interior.
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

    CompletePlanetSurveyNative.DebugLog("biome flora=" + biomeFlora.Length + " fauna=" + biomeActors.Length + " traits=" + traitKw.Length)

    int floraCount = MarkSpecies(planetForm, biomeFlora as Form[])
    int faunaCount = MarkSpecies(planetForm, biomeActors as Form[])
    int traitCount = MarkTraits(currentPlanet, traitKw)
    ; Sweep everything the planet tracks (resources + anything missed): one-shot.
    int everything = CompletePlanetSurveyNative.MarkEverythingForPlanet(planetForm, 100)
    ; ScanNearbyRefs is disabled until the procgen-cell iteration crash is fixed.
    int refsScanned = 0
    ; MarkLocationsExplored disabled — crashes on some planets (water/ocean biomes,
    ; null pointers in location parent chain). Cosmetic feature, survey data still complete.
    int locations = 0

    float surveyAfter = currentPlanet.GetSurveyPercent()
    CompletePlanetSurveyNative.DebugLog("marked flora=" + floraCount + " fauna=" + faunaCount + " traits=" + traitCount + " all=" + everything + " refs=" + refsScanned + " survey=" + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
    Debug.Notification("Survey: " + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%) all=" + everything + " refs=" + refsScanned)
EndFunction

; Called automatically by the C++ scan hook (ScanRefNative / ID_83008).
; Checks the GPOF toggle in Settings > Gameplay before completing the survey.
; The manual cgf command calls CompleteSurvey() directly and always runs.
Function CompleteSurveyIfEnabled() global
    ; Read the GPOF toggle (FormID 0x80C in CompletePlanetSurvey.esm, EditorID
    ; CPSScanAutoComplete). FormID was assigned by Creation Kit when the GameplayOption
    ; record was created — do not change without re-checking the .esm in xEdit.
    Form gpofForm = Game.GetFormFromFile(0x80C, "CompletePlanetSurvey.esm")
    If gpofForm == None
        Return  ; ESM not loaded — scan hook is a no-op
    EndIf
    GameplayOption gpofOption = gpofForm as GameplayOption
    If gpofOption == None
        Return
    EndIf
    If gpofOption.GetValue() < 0.5
        Return  ; User disabled "Auto-Complete Survey on Scan" in Settings > Gameplay
    EndIf

    ; Guard: must be outside on the planet surface.
    Actor playerRef = Game.GetPlayer()
    If playerRef.IsInInterior()
        Return
    EndIf
    Planet currentPlanet = playerRef.GetCurrentPlanet()
    If currentPlanet == None
        Return
    EndIf

    ; Skip planets already at 100% to avoid redundant work.
    If currentPlanet.GetSurveyPercent() >= 1.0
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
