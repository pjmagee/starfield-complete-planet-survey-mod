ScriptName CompletePlanetSurveyQuest

; Complete the planet survey for the planet the player is currently on.
; Call via console: cgf "CompletePlanetSurveyQuest.CompleteSurvey"
; No SFSE plugin required - pure Papyrus.

Function CompleteSurvey() global
    Actor playerRef = Game.GetPlayer()
    Planet currentPlanet = playerRef.GetCurrentPlanet()

    If currentPlanet == None
        Debug.Notification("Survey: Not on a planet")
        Return
    EndIf

    float surveyBefore = currentPlanet.GetSurveyPercent()
    Debug.Notification("Survey: Completing planet survey...")

    CompleteTraits(currentPlanet)
    CompleteBiomeFlora(playerRef)
    CompleteBiomeFauna(playerRef)
    CompleteResources(playerRef)

    float surveyAfter = currentPlanet.GetSurveyPercent()
    Debug.Notification("Survey: " + (surveyAfter * 100) as int + "% (was " + (surveyBefore * 100) as int + "%)")
EndFunction

Function CompleteTraits(Planet akPlanet) global
    Keyword[] traitKeywords = akPlanet.GetKeywordTypeList(44)

    If traitKeywords.Length == 0
        Return
    EndIf

    int discovered = 0
    int i = 0
    While i < traitKeywords.Length
        If !akPlanet.IsTraitKnown(traitKeywords[i])
            akPlanet.SetTraitKnown(traitKeywords[i], true)
            discovered += 1
        EndIf
        i += 1
    EndWhile

    If discovered > 0
        Debug.Notification("Survey: " + discovered + " traits discovered")
    EndIf
EndFunction

Function CompleteBiomeFlora(Actor playerRef) global
    ObjectReference playerRef_OR = playerRef as ObjectReference
    Flora[] biomeFlora = playerRef_OR.GetBiomeFlora(0.99)

    If biomeFlora.Length == 0
        Return
    EndIf

    int scanned = 0
    int i = 0
    While i < biomeFlora.Length
        If biomeFlora[i] != None
            ObjectReference floraRef = Game.FindRandomReferenceOfType(biomeFlora[i] as Form, playerRef.X, playerRef.Y, playerRef.Z, 100000.0)
            If floraRef != None
                floraRef.SetScanned(true)
                scanned += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    If scanned > 0
        Debug.Notification("Survey: " + scanned + " flora scanned")
    EndIf
EndFunction

Function CompleteBiomeFauna(Actor playerRef) global
    ObjectReference playerRef_OR = playerRef as ObjectReference
    ActorBase[] biomeActors = playerRef_OR.GetBiomeActors(0.99)

    If biomeActors.Length == 0
        Return
    EndIf

    int scanned = 0
    int i = 0
    While i < biomeActors.Length
        If biomeActors[i] != None
            ObjectReference actorRef = Game.FindRandomReferenceOfType(biomeActors[i] as Form, playerRef.X, playerRef.Y, playerRef.Z, 100000.0)
            If actorRef != None
                actorRef.SetScanned(true)
                scanned += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    If scanned > 0
        Debug.Notification("Survey: " + scanned + " fauna scanned")
    EndIf
EndFunction

Function CompleteResources(Actor playerRef) global
    ; Scan all flora references near the player.
    ; Resource deposits are Flora objects - scanning them registers the resource.
    ; We search a large radius to catch deposits in the area.
    ; Also scan the player's reference which can help register nearby resources.
    ObjectReference playerRef_OR = playerRef as ObjectReference
    Resource[] localResources = playerRef_OR.GetValueResources()

    ; Mark the player reference as scanned to trigger area resource discovery
    playerRef_OR.SetScanned(true)

    Debug.Notification("Survey: " + localResources.Length + " resources in current area")
EndFunction
