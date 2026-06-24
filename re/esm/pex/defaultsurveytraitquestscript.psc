ScriptName DefaultSurveyTraitQuestScript Extends Quest default
{ default script to set a stage when planet traits are surveyed }

;-- Structs -----------------------------------------
Struct PlanetTraitData
  Keyword PlanetTrait
  Bool traitFound = False
  Int StageToSet = -1
  Int ObjectiveToUpdate = -1
  GlobalVariable discoverCountCurrent
  GlobalVariable discoverCountRequired
EndStruct


;-- Variables ---------------------------------------

;-- Properties --------------------------------------
defaultsurveytraitquestscript:planettraitdata[] Property PlanetTraits Auto Const
{ planet traits this quest is looking for in target system }
sq_parentscript Property SQ_Parent Auto Const mandatory
{ autofill }
LocationAlias Property TargetPlanetLocation Auto Const
{ autofill - optional - planet where player is surveying traits 
    If this is not filled in, surveying a trait on any planet in the target system will qualify }
LocationAlias Property TargetSystemLocation Auto Const
{ autofill - system location where player is looking for planet traits }
Keyword Property LocTypeMajorOrbital Auto Const mandatory
{ autofill - used to check if something is a planet or moon }
Keyword Property LocTypeStarSystem Auto Const mandatory
Int Property PlanetTraitsCompleteStage = -1 Auto Const
{ optional - stage to set when all planet traits have been surveyed }

;-- Functions ---------------------------------------

Event OnQuestInit()
  Self.RegisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitDiscovered") ; #DEBUG_LINE_NO:36
  Self.RegisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitUpdated") ; #DEBUG_LINE_NO:37
EndEvent

Event SQ_ParentScript.SQ_PlanetTraitDiscovered(sq_parentscript akSender, Var[] akArgs)
  Self.HandlePlanetTraitEvent(akArgs, True) ; #DEBUG_LINE_NO:41
EndEvent

Event SQ_ParentScript.SQ_PlanetTraitUpdated(sq_parentscript akSender, Var[] akArgs)
  Self.HandlePlanetTraitEvent(akArgs, False) ; #DEBUG_LINE_NO:45
EndEvent

Function HandlePlanetTraitEvent(Var[] akArgs, Bool isDiscoveredEvent)
  planet traitPlanet = akArgs[0] as planet ; #DEBUG_LINE_NO:49
  Keyword discoveredTrait = akArgs[1] as Keyword ; #DEBUG_LINE_NO:50
  Int discoverCountCurrent = akArgs[2] as Int ; #DEBUG_LINE_NO:51
  Int discoverCountRequired = akArgs[3] as Int ; #DEBUG_LINE_NO:52
  If isDiscoveredEvent
    
  EndIf
  planet currentPlanet = Game.GetPlayer().GetCurrentPlanet() ; #DEBUG_LINE_NO:60
  Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:64
  Location currentLocation = playerRef.GetCurrentLocation() ; #DEBUG_LINE_NO:65
  If currentLocation.IsSameLocation(TargetSystemLocation.GetLocation(), LocTypeStarSystem) && (TargetPlanetLocation == None || currentLocation.IsSameLocation(TargetPlanetLocation.GetLocation(), LocTypeStarSystem)) ; #DEBUG_LINE_NO:66
    Int I = 0 ; #DEBUG_LINE_NO:68
    Int traitsFoundCount = 0 ; #DEBUG_LINE_NO:69
    While I < PlanetTraits.Length ; #DEBUG_LINE_NO:70
      defaultsurveytraitquestscript:planettraitdata theTraitData = PlanetTraits[I] ; #DEBUG_LINE_NO:71
      If discoveredTrait == theTraitData.PlanetTrait && theTraitData.traitFound == False ; #DEBUG_LINE_NO:73
        If theTraitData.ObjectiveToUpdate > -1 ; #DEBUG_LINE_NO:74
          If theTraitData.discoverCountCurrent ; #DEBUG_LINE_NO:76
            theTraitData.discoverCountCurrent.SetValue(discoverCountCurrent as Float) ; #DEBUG_LINE_NO:77
            Self.UpdateCurrentInstanceGlobal(theTraitData.discoverCountCurrent) ; #DEBUG_LINE_NO:78
          EndIf
          If theTraitData.discoverCountRequired ; #DEBUG_LINE_NO:80
            theTraitData.discoverCountRequired.SetValue(discoverCountRequired as Float) ; #DEBUG_LINE_NO:81
            Self.UpdateCurrentInstanceGlobal(theTraitData.discoverCountRequired) ; #DEBUG_LINE_NO:82
          EndIf
          Self.SetObjectiveDisplayed(theTraitData.ObjectiveToUpdate, True, False) ; #DEBUG_LINE_NO:84
        EndIf
        If isDiscoveredEvent ; #DEBUG_LINE_NO:87
          theTraitData.traitFound = True ; #DEBUG_LINE_NO:88
          If theTraitData.StageToSet > -1 ; #DEBUG_LINE_NO:89
            Self.setStage(theTraitData.StageToSet) ; #DEBUG_LINE_NO:90
          EndIf
        EndIf
      EndIf
      If theTraitData.traitFound ; #DEBUG_LINE_NO:94
        traitsFoundCount += 1 ; #DEBUG_LINE_NO:95
      EndIf
      I += 1 ; #DEBUG_LINE_NO:97
    EndWhile
    If traitsFoundCount >= PlanetTraits.Length ; #DEBUG_LINE_NO:101
      If Self.GetStageDone(PlanetTraitsCompleteStage) == False ; #DEBUG_LINE_NO:102
        Self.setStage(PlanetTraitsCompleteStage) ; #DEBUG_LINE_NO:103
        Self.UnregisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitDiscovered") ; #DEBUG_LINE_NO:104
        Self.UnregisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitUpdated") ; #DEBUG_LINE_NO:105
      EndIf
    EndIf
  EndIf
EndFunction
