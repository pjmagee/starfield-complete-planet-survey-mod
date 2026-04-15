Scriptname CPSRemoteScanQuestScript extends Quest
{Registers OnShipScan on the player ship (via alias 0) and forwards scanned planet to CompletePlanetSurveyQuest.HandleShipScan}

Event OnQuestInit()
    TryRegister()
EndEvent

; Idempotent: force-fill alias 0 (PlayerShipAlias in CK) with the player's
; current home ship, then register for OnShipScan against that alias. Must
; register against a ReferenceAlias — the event does not dispatch through
; raw spaceshipreference ScriptObjects.
Function TryRegister()
    ReferenceAlias shipAlias = Self.GetAlias(0) as ReferenceAlias
    If shipAlias == None
        CompletePlanetSurveyNative.DebugLog("CPSRemoteScanQuestScript: alias 0 not found")
        Return
    EndIf
    spaceshipreference ship = Game.GetPlayerHomeSpaceShip()
    If ship == None
        CompletePlanetSurveyNative.DebugLog("CPSRemoteScanQuestScript: no player ship yet")
        Return
    EndIf
    shipAlias.ForceRefTo(ship as ObjectReference)
    RegisterForRemoteEvent(shipAlias as ScriptObject, "OnShipScan")
    CompletePlanetSurveyNative.DebugLog("CPSRemoteScanQuestScript: registered OnShipScan on alias (ship 0x" + ship.GetFormID() + ")")
EndFunction

Event ReferenceAlias.OnShipScan(ReferenceAlias akSource, Location aPlanet, ObjectReference[] aMarkersArray)
    int locId = 0
    int markerCount = 0
    If aPlanet != None
        locId = aPlanet.GetFormID()
    EndIf
    If aMarkersArray != None
        markerCount = aMarkersArray.Length
    EndIf
    CompletePlanetSurveyNative.DebugLog("OnShipScan: location=0x" + locId + " markers=" + markerCount)

    If aPlanet == None
        CompletePlanetSurveyNative.DebugLog("OnShipScan: aPlanet Location is null")
        Return
    EndIf
    Planet scannedPlanet = aPlanet.GetCurrentPlanet()
    If scannedPlanet == None
        CompletePlanetSurveyNative.DebugLog("OnShipScan: Location.GetCurrentPlanet returned null for loc=0x" + locId)
        Return
    EndIf
    int planetFid = (scannedPlanet as Form).GetFormID()
    CompletePlanetSurveyNative.DebugLog("OnShipScan: resolved planet=0x" + planetFid + ", forwarding to HandleShipScan")
    CompletePlanetSurveyQuest.HandleShipScan(scannedPlanet)
EndEvent
