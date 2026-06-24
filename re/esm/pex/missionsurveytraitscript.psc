ScriptName MissionSurveyTraitScript Extends MissionQuestScript conditional

;-- Variables ---------------------------------------

;-- Properties --------------------------------------
Group MissionTypeData
  sq_parentscript Property SQ_Parent Auto Const mandatory
  Keyword Property PlanetTrait Auto Const mandatory
  { planet trait this quest is looking for in target system }
  Keyword Property MissionStoryKeywordGetAllPlanets Auto Const mandatory
  { keyword to use to start this quest's "get all planets" quest }
  ReferenceAlias Property TraitCategory Auto Const mandatory
  { holds trait category marker for text replacement }
  Int Property MissionAcceptTutorialID = 2 Auto Const
  { tutorial ID when accepting this mission - see SQ_Parent.TutorialMessages }
  Int Property MissionLandTutorialID = 3 Auto Const
  { tutorial ID when landing on the target planet - see SQ_Parent.TutorialMessages }
  GlobalVariable Property RewardXPAmountGlobalActual Auto Const mandatory
  { The actual amount of XP we're going to reward the player with - possibly adjusted by specific mission factors }
EndGroup


;-- Functions ---------------------------------------

Event OnQuestStarted()
  Self.RegisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitDiscovered") ; #DEBUG_LINE_NO:29
  Parent.OnQuestStarted() ; #DEBUG_LINE_NO:31
EndEvent

Event SQ_ParentScript.SQ_PlanetTraitDiscovered(sq_parentscript akSender, Var[] akArgs)
  planet traitPlanet = akArgs[0] as planet ; #DEBUG_LINE_NO:35
  Keyword discoveredTrait = akArgs[1] as Keyword ; #DEBUG_LINE_NO:36
  Location currentLocation = traitPlanet.GetLocation() ; #DEBUG_LINE_NO:40
  If currentLocation.IsSameLocation(TargetSystemLocation.GetLocation(), MissionParent.LocTypeStarSystem) ; #DEBUG_LINE_NO:41
    If discoveredTrait == PlanetTrait ; #DEBUG_LINE_NO:43
      Self.MissionComplete() ; #DEBUG_LINE_NO:44
    EndIf
  EndIf
EndEvent

Int Function GetActualReward()
  Float reward = RewardAmountGlobal.GetValue() ; #DEBUG_LINE_NO:52
  planet targetPlanet = TargetPlanetLocation.GetLocation().GetCurrentPlanet() ; #DEBUG_LINE_NO:54
  sq_parentscript:planettraitdata theTraitData = SQ_Parent.FindMatchingPlanetTraitForKeyword(PlanetTrait, targetPlanet) ; #DEBUG_LINE_NO:55
  If theTraitData ; #DEBUG_LINE_NO:56
    TraitCategory.ForceRefTo(theTraitData.TraitCategoryMarker) ; #DEBUG_LINE_NO:58
  EndIf
  missiongetallplanetsscript getAllPlanetsQuest = None ; #DEBUG_LINE_NO:63
  missiongetallplanetsscript[] startedQuests = MissionStoryKeywordGetAllPlanets.SendStoryEventAndWait(TargetPlanetLocation.GetLocation(), None, None, 0, 0) as missiongetallplanetsscript[] ; #DEBUG_LINE_NO:64
  If startedQuests.Length > 0 ; #DEBUG_LINE_NO:66
    getAllPlanetsQuest = startedQuests[0] ; #DEBUG_LINE_NO:68
    Int systemTraitValue = getAllPlanetsQuest.GetSystemTraitValue() ; #DEBUG_LINE_NO:69
    Float systemTraitReward = MissionParent.MissionBoardSurveyTraitSystemValueMult.GetValue() * systemTraitValue as Float ; #DEBUG_LINE_NO:70
    reward += systemTraitReward ; #DEBUG_LINE_NO:72
    getAllPlanetsQuest.Stop() ; #DEBUG_LINE_NO:73
    Int I = 0 ; #DEBUG_LINE_NO:76
    GlobalVariable rewardXP = None ; #DEBUG_LINE_NO:77
    While I < MissionParent.MissionBoardSurveyTraitXPRewards.Length && rewardXP == None ; #DEBUG_LINE_NO:78
      missionparentscript:missionboardsurveytraitdata theRewardData = MissionParent.MissionBoardSurveyTraitXPRewards[I] ; #DEBUG_LINE_NO:79
      If systemTraitValue >= theRewardData.minTraitValue ; #DEBUG_LINE_NO:80
        rewardXP = theRewardData.rewardXP ; #DEBUG_LINE_NO:81
      EndIf
      I += 1 ; #DEBUG_LINE_NO:83
    EndWhile
    RewardXPAmountGlobalActual.SetValueInt(rewardXP.GetValueInt()) ; #DEBUG_LINE_NO:86
  EndIf
  Return reward as Int ; #DEBUG_LINE_NO:90
EndFunction

Function MissionAccepted(Bool bAccepted)
  Parent.MissionAccepted(bAccepted) ; #DEBUG_LINE_NO:95
  If bAccepted ; #DEBUG_LINE_NO:96
    SQ_Parent.ShowTutorialMessage(MissionAcceptTutorialID) ; #DEBUG_LINE_NO:97
  EndIf
EndFunction

Function HandlePlayerShipLanding()
  If PlayerAcceptedQuest ; #DEBUG_LINE_NO:103
    Location targetSystem = TargetSystemLocation.GetLocation() ; #DEBUG_LINE_NO:104
    Location currentLocation = PlayerShip.GetShipRef().GetCurrentLocation() ; #DEBUG_LINE_NO:105
    If currentLocation.IsSameLocation(targetSystem, SQ_Parent.LocTypeStarSystem) ; #DEBUG_LINE_NO:107
      SQ_Parent.ShowTutorialMessage(MissionLandTutorialID) ; #DEBUG_LINE_NO:109
    EndIf
  EndIf
EndFunction
