ScriptName rq_scantraitscript Extends RQScript

;-- Variables ---------------------------------------
sq_parentscript:planettraitdata targetTraitData

;-- Properties --------------------------------------
Group Scan_Properties
  sq_parentscript Property SQ_Parent Auto Const mandatory
  Int Property ObjectiveToDisplayForScanning = 500 Auto Const
  { Objective for scanning }
  Int Property StageToSetOnCompletelyScanned = 600 Auto Const
  { Stage to set when scanning target base object's GetPercentageKnown() >= 1 }
  GlobalVariable Property PercentageScannedTextDisplay Auto Const mandatory
  { Quest instance global to use to display percentage in quest objective }
  ReferenceAlias Property TargetMapMarker Auto Const mandatory
  { target location map marker - use to get planet trait from location }
  ReferenceAlias Property Trait Auto Const mandatory
  { used for text replacement for trait name }
  LocationAlias Property Alias_Planet Auto Const mandatory
  { planet location }
  GlobalVariable Property CreditsRewardRadiantQuestLarge Auto Const mandatory
  { base value for quest reward }
  GlobalVariable Property RQ_ScanQuestReward Auto Const mandatory
  { actual value for quest reward - modified by number of trait overlays needed to discover trait }
  Float Property RewardTraitOverlayMult = 500.0 Auto Const
  { multiplier based on total number of overlays needed to be explored for this trait }
EndGroup


;-- Functions ---------------------------------------

Function QuestStartedSpecific()
  If Self.InitializeScanTarget() ; #DEBUG_LINE_NO:39
    Self.RegisterForRemoteEvent(Game.GetPlayer() as ScriptObject, "OnPlayerScannedObject") ; #DEBUG_LINE_NO:40
    Self.RegisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitDiscovered") ; #DEBUG_LINE_NO:42
    Self.RegisterForCustomEvent(SQ_Parent as ScriptObject, "sq_parentscript_SQ_PlanetTraitUpdated") ; #DEBUG_LINE_NO:43
  Else
    Self.Shutdown() ; #DEBUG_LINE_NO:46
  EndIf
EndFunction

Bool Function InitializeScanTarget()
  Bool foundTrait = False ; #DEBUG_LINE_NO:53
  ObjectReference targetRef = TargetMapMarker.GetRef() ; #DEBUG_LINE_NO:55
  sq_parentscript:planettraitdata theData = SQ_Parent.GetPlanetTraitData(targetRef) ; #DEBUG_LINE_NO:57
  If theData ; #DEBUG_LINE_NO:58
    targetTraitData = theData ; #DEBUG_LINE_NO:59
    If targetRef.GetCurrentPlanet().IsTraitKnown(targetTraitData.PlanetTrait) == False ; #DEBUG_LINE_NO:60
      foundTrait = True ; #DEBUG_LINE_NO:61
      Trait.ForceRefTo(targetTraitData.TraitNameMarker) ; #DEBUG_LINE_NO:63
      Int surveyingBonus = Game.GetPlayer().GetValueInt(SQ_Parent.SurveyingTraitBonus) ; #DEBUG_LINE_NO:66
      Int discoverCountRequiredActual = Math.Max(1.0, (targetTraitData.discoverCountRequired - surveyingBonus) as Float) as Int ; #DEBUG_LINE_NO:68
      Location planetLocation = Alias_Planet.GetLocation() ; #DEBUG_LINE_NO:69
      Int discoverCountCurrent = planetLocation.GetValue(targetTraitData.PlanetTraitAV) as Int ; #DEBUG_LINE_NO:70
      Self.UpdateScanPercentage(discoverCountCurrent as Float, discoverCountRequiredActual as Float) ; #DEBUG_LINE_NO:72
      Float reward = CreditsRewardRadiantQuestLarge.GetValue() + (targetTraitData.discoverCountRequired as Float * RewardTraitOverlayMult) ; #DEBUG_LINE_NO:74
      RQ_ScanQuestReward.SetValueInt(reward as Int) ; #DEBUG_LINE_NO:75
    EndIf
  EndIf
  Return foundTrait ; #DEBUG_LINE_NO:78
EndFunction

Event SQ_ParentScript.SQ_PlanetTraitDiscovered(sq_parentscript akSender, Var[] akArgs)
  planet traitPlanet = akArgs[0] as planet ; #DEBUG_LINE_NO:82
  Keyword discoveredTrait = akArgs[1] as Keyword ; #DEBUG_LINE_NO:83
  If Self.IsMatchingDiscoveryEvent(discoveredTrait) ; #DEBUG_LINE_NO:85
    Self.SetStage(StageToSetOnCompletelyScanned) ; #DEBUG_LINE_NO:87
  EndIf
EndEvent

Event SQ_ParentScript.SQ_PlanetTraitUpdated(sq_parentscript akSender, Var[] akArgs)
  planet traitPlanet = akArgs[0] as planet ; #DEBUG_LINE_NO:93
  Keyword discoveredTrait = akArgs[1] as Keyword ; #DEBUG_LINE_NO:94
  If Self.IsMatchingDiscoveryEvent(discoveredTrait) ; #DEBUG_LINE_NO:95
    Int discoverCountCurrent = akArgs[2] as Int ; #DEBUG_LINE_NO:96
    Int discoverCountRequired = akArgs[3] as Int ; #DEBUG_LINE_NO:97
    Self.UpdateScanPercentage(discoverCountCurrent as Float, discoverCountRequired as Float) ; #DEBUG_LINE_NO:99
  EndIf
EndEvent

Bool Function IsMatchingDiscoveryEvent(Keyword discoveredTrait)
  Bool isMatching = False ; #DEBUG_LINE_NO:104
  If discoveredTrait == targetTraitData.PlanetTrait ; #DEBUG_LINE_NO:105
    ObjectReference targetRef = TargetMapMarker.GetRef() ; #DEBUG_LINE_NO:107
    Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:108
    planet currentPlanet = playerRef.GetCurrentPlanet() ; #DEBUG_LINE_NO:109
    planet targetPlanet = targetRef.GetCurrentPlanet() ; #DEBUG_LINE_NO:110
    isMatching = currentPlanet == targetPlanet ; #DEBUG_LINE_NO:112
  EndIf
  Return isMatching ; #DEBUG_LINE_NO:114
EndFunction

Function UpdateScanPercentage(Float discoverCountCurrent, Float discoverCountRequired)
  Float percentageKnown = discoverCountCurrent / discoverCountRequired ; #DEBUG_LINE_NO:118
  Int precentAsInt = (percentageKnown * 100.0) as Int ; #DEBUG_LINE_NO:122
  PercentageScannedTextDisplay.SetValue(precentAsInt as Float) ; #DEBUG_LINE_NO:123
  Self.UpdateCurrentInstanceGlobal(PercentageScannedTextDisplay) ; #DEBUG_LINE_NO:124
EndFunction
