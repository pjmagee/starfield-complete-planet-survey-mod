ScriptName SQ_ParentScript Extends Quest
{ holds overall system data/functionality }

;-- Structs -----------------------------------------
Struct ActorValueDatum
  ActorValue ActorValueToDamage
  { An actor value to damage. }
  Float DamagePercent = 1.0
  { The percent to damage the value by, 0-1, with 1 being 100% damage. }
EndStruct

Struct OutpostAttackSpecialCase
  conditionform specialCaseCondition
  Float specialCaseAttackChance
  Keyword storyEventKeyword
EndStruct

Struct PlanetAbundanceData
  Keyword planetKeyword
  Float surveyDifficulty = 1.0
EndStruct

Struct PlanetSurveySlateData
  Book planetSurveySlate
  Int minTraitValue = 0
  Float minSurveyPercent = 0.25
  GlobalVariable RewardXP
EndStruct

Struct PlanetTraitData
  Keyword PlanetTrait
  ActorValue PlanetTraitAV
  ObjectReference TraitNameMarker
  ObjectReference TraitCategoryMarker
  Perk AssociatedSkill
  Perk AssociatedSkill02
  Int discoverCountRequired = 1
EndStruct

Struct RandomShipDamageDatum
  ActorValue ActorValueToDamage
  { The ship system actor value to damage. }
  Float ChanceDestroyed = 0.5
  { Chance that this ship system is fully destroyed. }
  Float MinDamagePercent = 0.0
  { If this system is not destroyed, the minimum amount to damage it by. }
  Float MaxDamagePercent = 1.0
  { If this system is not destroyed, the maximum amount to damage it by. }
EndStruct

Struct SurveySite
  ObjectReference siteMarker
  { map marker (?) that holds survey site information }
EndStruct

Struct SurveySiteType
  Activator siteTypeMarker
  GlobalVariable siteTypeCount
EndStruct

Struct SystemSurveyData
  Int minTraitValue = 0
  GlobalVariable RewardXP
EndStruct

Struct TutorialMessage
  Int tutorialID
  Message TutorialMessage
EndStruct

Struct mapMarkerName
  Keyword groupTypeKeyword
  Form nameMarker
EndStruct


;-- Variables ---------------------------------------
Float attackChanceBase = 0.100000001 Const
Float attackChanceCargoLinkMult = 0.050000001 Const
Float attackChanceCreature = 0.079999998 Const
Float attackChanceResourceMult = 0.01 Const
Int criticalHitType0_Reactor = 0 Const
Int criticalHitType1_Crew = 1 Const
Int criticalHitType2_Hull = 2 Const
Int criticalHitType3_System = 3 Const
Int criticalHitType4_EM = 4 Const
Int criticalHitType5_Engine = 5 Const
Int criticalHitType6_Shield = 6 Const
Float minDaysSinceLastAttack = 7.0 Const
Float minDaysSinceLastAttackRoll = 0.039999999 Const
defaulttutorialquestscript tutorialQuest

;-- Guards ------------------------------------------
;*** WARNING: Guard declaration syntax is EXPERIMENTAL, subject to change
Guard ShipCriticalHitGuard
Guard computersAchievementGuard
Guard discoverMatchingTraitsBusy
Guard harvestLock
Guard planetSurveyLock
Guard tutorialMessageGuard

;-- Properties --------------------------------------
Group SurveySiteGroup
  sq_parentscript:surveysite[] Property SurveySites Auto
  { array holds survey sites that the player has discovered but not used }
  Message Property SQ_SurveySiteDiscoveredMessage Auto Const mandatory
  sq_parentscript:surveysitetype[] Property surveySiteTypes Auto Const
EndGroup

Group PlanetTraitGroup
  RefCollectionAlias Property SurveySlates Auto Const mandatory
  { holds planet survey slates until player sells them }
  sq_parentscript:planettraitdata[] Property PlanetTraits Auto Const
  sq_parentscript:planetabundancedata[] Property PlanetAbundanceKeywords Auto Const
  Message Property SQ_ParentPlanetTraitScanMessage Auto Const mandatory
  Message Property SQ_ParentPlanetTraitUpdateMessage Auto Const mandatory
  Message Property SQ_ParentPlanetTraitDiscoverMessage Auto Const mandatory
  Message Property SQ_ParentPlanetTraitExploreMessage Auto Const mandatory
  Int Property KeywordType_PlanetTrait = 44 Auto Const hidden
  { used to get PlanetTrait keyword types using GetKeywordTypeList native function }
  Int Property KeywordType_PlanetFloraAbundance = 47 Auto Const hidden
  { used to get Flora Abundance keyword types using GetKeywordTypeList native function }
  Int Property KeywordType_PlanetFaunaAbundance = 48 Auto Const hidden
  { used to get Flora Abundance keyword types using GetKeywordTypeList native function }
  ReferenceAlias Property PlanetTraitCategory Auto Const mandatory
  { used for message text replacement }
  ReferenceAlias Property PlanetTraitName Auto Const mandatory
  { used for message text replacement }
  LocationRefType Property PlanetTraitScanTargetLocRef Auto Const mandatory
  { used to count planet trait scan targets in a location }
  ReferenceAlias Property ScanTarget Auto Const mandatory
  { used for stamping scan target with "real name" after scanning }
  ReferenceAlias Property ScanTargetName Auto Const mandatory
  { used for stamping scan target with "real name" after scanning }
  ActorValue Property PlanetTraitLocationScanCount Auto Const mandatory
  { actor value used to count how many scan targets have been scanned on a location }
  ActorValue Property PlanetTraitLocationScanCountRequired Auto Const mandatory
  { actor value used to set total number of scan targets in location (default AV used by UI) }
  ActorValue Property SQ_PlanetLastSurveyReward Auto Const mandatory
  { actor value used to stamp the last survey reward slate % on a planet }
  ActorValue Property SurveyingTraitBonus Auto Const mandatory
  { used to reduce number of POIs needed to discover a trait }
  ReferenceAlias Property SurveySlate Auto Const mandatory
  { used for stamping survey slate with the planet name }
  sq_parentscript:planetsurveyslatedata[] Property PlanetSurveySlates Auto Const
  { array of planet survey slate data
	  NOTE: needs to be ordered with highest value slate in 0 position and going down in value from there }
  Float[] Property SurveyRewardThresholds Auto Const
  { if new survey % goes above one of these, time to reward a survey slate }
  sq_parentscript:systemsurveydata[] Property SystemSurveyRewards Auto Const
  { array of rewards for completing system surveys }
  GlobalVariable Property PlanetTraitXPReward Auto Const mandatory
  GlobalVariable Property PlanetTraitXPRewardSkillBonus Auto Const mandatory
  GlobalVariable Property PlanetTraitXPRewardAstrophysicsMult Auto Const mandatory
  GlobalVariable Property Skill_Astrophysics_DiscoverTraitChance Auto Const mandatory
  { chance to discover a trait when scanning a planet }
  Keyword Property SQ_PlanetTraitOverrideLink Auto Const mandatory
  { keyword used to link scan targets to map marker for cases where the location can work for multiple planet traits }
  conditionform Property SQ_PlanetIsHabitable Auto Const mandatory
  { used to determine if a planet is habitable }
  Keyword Property SQ_PlanetIsHabitableKeyword Auto Const mandatory
  { stamp survey slates with this keyword for habitable planets }
  missionparentscript Property MB_Parent Auto Const mandatory
  { mission parent quest }
  Location[] Property SystemsFullySurveyed Auto
  { array that will be updated to hold system locations that have been completely surveyed - all planets at 100% }
  Keyword Property SQ_SurveySlateEvent Auto Const mandatory
  { keyword used to start SQ_SurveySlate quests to stamp survey slates with the correct name }
EndGroup

Group TextReplacement
  ReferenceAlias Property PlanetReference Auto Const mandatory
  Keyword Property LocTypeMajorOrbital Auto Const mandatory
  Keyword Property LocTypeStarSystem Auto Const mandatory
  LocationAlias Property PlanetReferencePlanetLocation Auto Const mandatory
  { current planet matching PlanetReference }
  LocationAlias Property PlanetReferenceSystemLocation Auto Const mandatory
  { current system location matching PlanetReference }
  ReferenceAlias Property PlayerShip Auto Const mandatory
  { player current ship }
  LocationAlias[] Property SystemPlanets Auto Const
  { array of location aliases for all planets in current system - many will be empty }
EndGroup

Group Skills
  ActorValue Property ZoologyNonLethalHarvestCount Auto Const mandatory
  { used to track how many times the player has harvested from a creature }
  GlobalVariable Property ZoologyNonLethalHarvestCountMax Auto Const mandatory
  { current max harvest count }
EndGroup

Group Companions
  companionaffinityeventsscript Property SQ_Companions Auto Const mandatory
  { autofill }
EndGroup

Group Quests
  rad01_list_questscript Property Rad01_LIST Auto Const mandatory
  { autofill }
EndGroup

Group DerelictShips
  sq_parentscript:randomshipdamagedatum[] Property RandomShipDamageData Auto Const mandatory
  ReferenceAlias Property DerelictShipNameAlias Auto Const mandatory
  Keyword Property BEDerelict Auto Const mandatory
  Message Property DerelictShipNameOverrideMessage Auto Const mandatory
  ActorValue Property SpaceshipCrew Auto Const mandatory
  ActorValue Property SpaceshipExternalLightsDisabledValue Auto Const mandatory
EndGroup

Group outpostAttacks
  ActorValue Property OutpostLastAttackTimestamp Auto Const mandatory
  { used to timestamp last outpost attack }
  ActorValue Property OutpostLastAttackRollTimestamp Auto Const mandatory
  { used to timestamp last outpost attack roll }
  ActorValue Property OutpostCargoLinkAV Auto Const mandatory
  { used to weight attacks based on cargo links at the outpost }
  Keyword Property SQ_OutpostEventAttack Auto Const mandatory
  { used to trigger outpost attacks by NPCs }
  Keyword Property SQ_OutpostEventAttackCreatures Auto Const mandatory
  { used to trigger outpost attacks by creatures }
  sq_parentscript:outpostattackspecialcase[] Property OutpostAttackSpecialCases Auto
  { used to increase attack chance for special cases }
EndGroup

Group OE_CivilianData
  sq_parentscript:mapmarkername[] Property MapMarkerNames Auto Const
  { array to use to rename map marker based on keyword on location }
  Keyword Property MapMarkerLinkedRefNameOverride Auto Const mandatory
  { use to link to override name }
  FormList Property OE_CiviliansVisibleFromStarmapKeywords Auto Const mandatory
  { locations with one of these keywords are visible on starmap }
EndGroup

Group Tutorials
  sq_parentscript:tutorialmessage[] Property TutorialMessages Auto
  { array of tutorial messages. Array entries are removed after the message has been triggered. }
EndGroup

Group Achievements
  Int Property ComputersAchievementID = 35 Auto Const
  ActorValue Property ComputersAchievementAV Auto Const mandatory
  { AV on player to track number of terminals accessed }
  GlobalVariable Property ComputersAchievementCount Auto Const mandatory
  { how many computers accessed required for achievement? }
  Bool Property ComputersAchievementAwarded = False Auto hidden
  { set to true when player gets this achievement }
EndGroup

Group SmugglingMinigame
  Message Property SQ_GuardShipSmugglingMinigameSuccess Auto Const mandatory
  { temp message box for smuggling minigame }
  Message Property SQ_GuardShipSmugglingMinigameFailure Auto Const mandatory
  { temp message box for smuggling minigame }
  Float Property fSmugglingBaseChance = 60.0 Auto
  Float Property fSmugglingTargetSkillMult = -5.0 Auto Const
  Float Property fSmugglingCapacityMult = 1.0 Auto Const
  Float Property fSmugglingWeightPower = 0.5 Auto Const
  Float Property fSmugglingWeightMult = -1.0 Auto Const
  Float Property fSmugglingWeightMax = 2000.0 Auto Const
  Float Property fSmugglingMaxChance = 90.0 Auto Const
  Float Property fSmugglingMinChance = 10.0 Auto Const
  Float[] Property ScanJammerMults Auto Const mandatory
  { this will hold the values to use in the formula for each level of SpaceshipScanJammer, which will be ints }
  Float[] Property PlayerSkillMults Auto Const mandatory
  { this will hold the values to use in the formula for each level of player skill, which will be ints }
  ActorValue Property CarryWeightShielded Auto Const mandatory
  ActorValue Property Perception Auto Const mandatory
  ActorValue Property PayloadLevel Auto Const mandatory
  { player's Payloads skill }
  ActorValue Property SpaceshipScanJammer Auto Const mandatory
  { ship's scan jammer level }
  Int Property SmugglingAchievementID = 45 Auto Const
EndGroup

Group ShipCriticalHits
  ActorValue Property ShipSystemReactorHealth Auto Const mandatory
  { reactor system health }
  ActorValue Property Health Auto Const mandatory
  { ship health }
  ActorValue Property ShipSystemEngineHealth Auto Const mandatory
  { engine system health - needed if crew goes to 0 }
  ActorValue Property SpaceshipCriticalHitCrew Auto Const mandatory
  { set to 1 when crew is wiped out by a critical }
  Int Property maxCrewDamage = 6 Auto Const
  { max number of crew that can be killed in a single critical hit }
  Float Property minReactorHealthDamage = 0.100000001 Auto Const
  { min % of reactor health to damage }
  Float Property maxReactorHealthDamage = 0.25 Auto Const
  { max % of reactor health to damage }
  Float Property minHullHealthDamage = 0.100000001 Auto Const
  { min % of Hull health to damage }
  Float Property maxHullHealthDamage = 0.25 Auto Const
  { max % of reactor health to damage }
  Float Property minSystemHealthDamage = 1.0 Auto Const
  { min % of system health to damage }
  Float Property maxSystemHealthDamage = 1.25 Auto Const
  { max % of system health to damage }
  ActorValue[] Property SystemHealthAVs Auto Const
  { system health actor values }
  ActorValue[] Property SystemEMHealthAVs Auto Const
  { system EM health actor values }
  Message[] Property CriticalHitMessages Auto Const
  { array of critical hit messages - index matches the hit type }
  wwiseevent Property UICriticalHitShip Auto Const mandatory
  { critical hit sound }
EndGroup


;-- Functions ---------------------------------------

Function TestGetBaseActor(Actor testActor)
  ; Empty function
EndFunction

Function TestGetEditorLocation(ObjectReference testRef)
  ; Empty function
EndFunction

Function TestGetMatchingActorBase(Actor theActor)
  ; Empty function
EndFunction

Function TestIsInSameLocation(ObjectReference refToTest, Location locationToTest, Keyword locationKeyword)
  ; Empty function
EndFunction

Function testConditionForm(ObjectReference testRef, conditionform testCondition)
  ; Empty function
EndFunction

Function testGetRefsLinkedToMe(ObjectReference refToCheck, Keyword keywordToCheck)
  ; Empty function
EndFunction

Function testRandomInt(Int Min, Int Max)
  ; Empty function
EndFunction

Event OnQuestInit()
  SurveySites = new sq_parentscript:surveysite[0] ; #DEBUG_LINE_NO:271
  tutorialQuest = (Self as Quest) as defaulttutorialquestscript ; #DEBUG_LINE_NO:272
  Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:273
  Self.RegisterForRemoteEvent(playerRef as ScriptObject, "OnPlayerPlanetSurveyComplete") ; #DEBUG_LINE_NO:274
  Self.RegisterForRemoteEvent(playerRef as ScriptObject, "OnPlayerScannedObject") ; #DEBUG_LINE_NO:275
  SystemsFullySurveyed = new Location[0] ; #DEBUG_LINE_NO:276
EndEvent

Event Actor.OnPlayerPlanetSurveyComplete(Actor akSource, planet akPlanet)
  Self.CheckCompletePlanetSurvey(akPlanet, False) ; #DEBUG_LINE_NO:281
EndEvent

Event Actor.OnPlayerScannedObject(Actor akSource, ObjectReference akScannedRef)
  Self.CheckCompletePlanetSurvey(akScannedRef.GetCurrentPlanet(), False) ; #DEBUG_LINE_NO:286
EndEvent

Event ObjectReference.OnSell(ObjectReference akSource, Actor akSeller)
  Int index = SurveySlates.Find(akSource) ; #DEBUG_LINE_NO:291
  If index > -1 ; #DEBUG_LINE_NO:292
    Self.UnregisterForRemoteEvent(akSource as ScriptObject, "OnSell") ; #DEBUG_LINE_NO:293
    SurveySlates.RemoveRef(akSource) ; #DEBUG_LINE_NO:294
  EndIf
EndEvent

Function SendPlanetTraitDiscoveredEvent(planet traitPlanet, Keyword PlanetTrait, Int discoverCountCurrent, Int discoverCountRequired)
  Var[] kargs = new Var[4] ; #DEBUG_LINE_NO:325
  kargs[0] = traitPlanet as Var ; #DEBUG_LINE_NO:326
  kargs[1] = PlanetTrait as Var ; #DEBUG_LINE_NO:327
  kargs[2] = discoverCountCurrent as Var ; #DEBUG_LINE_NO:328
  kargs[3] = discoverCountRequired as Var ; #DEBUG_LINE_NO:329
  Self.SendCustomEvent("sq_parentscript_SQ_PlanetTraitDiscovered", kargs) ; #DEBUG_LINE_NO:330
EndFunction

Function SendPlanetTraitUpdatedEvent(planet traitPlanet, Keyword PlanetTrait, Int discoverCountCurrent, Int discoverCountRequired)
  Var[] kargs = new Var[4] ; #DEBUG_LINE_NO:335
  kargs[0] = traitPlanet as Var ; #DEBUG_LINE_NO:336
  kargs[1] = PlanetTrait as Var ; #DEBUG_LINE_NO:337
  kargs[2] = discoverCountCurrent as Var ; #DEBUG_LINE_NO:338
  kargs[3] = discoverCountRequired as Var ; #DEBUG_LINE_NO:339
  Self.SendCustomEvent("sq_parentscript_SQ_PlanetTraitUpdated", kargs) ; #DEBUG_LINE_NO:340
EndFunction

Function SendBEStartedEvent(ObjectReference enemyShip, bescript BEQuest)
  Var[] kargs = new Var[2] ; #DEBUG_LINE_NO:344
  kargs[0] = enemyShip as Var ; #DEBUG_LINE_NO:345
  kargs[1] = BEQuest as Var ; #DEBUG_LINE_NO:346
  Self.SendCustomEvent("sq_parentscript_SQ_BEStarted", kargs) ; #DEBUG_LINE_NO:348
EndFunction

Function SendBEForceStopEvent()
  Self.SendCustomEvent("sq_parentscript_SQ_BEForceStop", None) ; #DEBUG_LINE_NO:352
EndFunction

Function SendNativeTerminalActorUnconsciousEvent(ObjectReference terminalRef, Actor actorRef, Bool bSetUnconscious)
  Var[] kargs = new Var[3] ; #DEBUG_LINE_NO:357
  kargs[0] = terminalRef as Var ; #DEBUG_LINE_NO:358
  kargs[1] = actorRef as Var ; #DEBUG_LINE_NO:359
  kargs[2] = bSetUnconscious as Var ; #DEBUG_LINE_NO:360
  Self.SendCustomEvent("sq_parentscript_SQ_NativeTerminalActor_Unconscious", kargs) ; #DEBUG_LINE_NO:361
EndFunction

Function SendNativeTerminalActorAllyEvent(ObjectReference terminalRef, Actor actorRef)
  Var[] kargs = new Var[2] ; #DEBUG_LINE_NO:366
  kargs[0] = terminalRef as Var ; #DEBUG_LINE_NO:367
  kargs[1] = actorRef as Var ; #DEBUG_LINE_NO:368
  Self.SendCustomEvent("sq_parentscript_SQ_NativeTerminalActor_Ally", kargs) ; #DEBUG_LINE_NO:369
EndFunction

Function SendNativeTerminalActorFrenzyEvent(ObjectReference terminalRef, Actor actorRef)
  Var[] kargs = new Var[2] ; #DEBUG_LINE_NO:374
  kargs[0] = terminalRef as Var ; #DEBUG_LINE_NO:375
  kargs[1] = actorRef as Var ; #DEBUG_LINE_NO:376
  Self.SendCustomEvent("sq_parentscript_SQ_NativeTerminalActor_Frenzy", kargs) ; #DEBUG_LINE_NO:377
EndFunction

Function SendAlarmTriggeredOnEvent(ObjectReference alarmRef, Actor alarmActor, sq_alarmscript SQ_AlarmQuest)
  If alarmRef as Bool && SQ_AlarmQuest as Bool ; #DEBUG_LINE_NO:383
    Var[] kargs = new Var[4] ; #DEBUG_LINE_NO:384
    kargs[0] = alarmRef.GetCurrentLocation() as Var ; #DEBUG_LINE_NO:385
    kargs[1] = SQ_AlarmQuest as Var ; #DEBUG_LINE_NO:386
    kargs[2] = alarmRef as Var ; #DEBUG_LINE_NO:387
    kargs[3] = alarmActor as Var ; #DEBUG_LINE_NO:388
    Self.SendCustomEvent("sq_parentscript_SQ_AlarmTriggeredOn", kargs) ; #DEBUG_LINE_NO:389
  EndIf
EndFunction

Function SendAlarmTriggeredOffEvent(ObjectReference alarmRef, sq_alarmscript SQ_AlarmQuest)
  If alarmRef as Bool && SQ_AlarmQuest as Bool ; #DEBUG_LINE_NO:397
    Var[] kargs = new Var[3] ; #DEBUG_LINE_NO:398
    kargs[0] = alarmRef.GetCurrentLocation() as Var ; #DEBUG_LINE_NO:399
    kargs[1] = SQ_AlarmQuest as Var ; #DEBUG_LINE_NO:400
    kargs[2] = alarmRef as Var ; #DEBUG_LINE_NO:401
    Self.SendCustomEvent("sq_parentscript_SQ_AlarmTriggeredOff", kargs) ; #DEBUG_LINE_NO:402
  EndIf
EndFunction

Function SendPiracyEvent(ObjectReference piratedShip, Bool piracySuccess)
  Var[] kargs = new Var[2] ; #DEBUG_LINE_NO:409
  kargs[0] = piratedShip as Var ; #DEBUG_LINE_NO:410
  kargs[1] = piracySuccess as Var ; #DEBUG_LINE_NO:411
  Self.SendCustomEvent("sq_parentscript_SQ_PiracyEvent", kargs) ; #DEBUG_LINE_NO:413
EndFunction

Function SendContrabandConfiscatedEvent(Faction crimeFaction)
  Self.SendCustomEvent("sq_parentscript_SQ_ContrabandConfiscated", None) ; #DEBUG_LINE_NO:418
EndFunction

Function UpdateTextReplacementAliasesPlanet(planet planetToUse)
  Location planetLocation = planetToUse.GetLocation() ; #DEBUG_LINE_NO:425
  Location systemLocation = planetLocation.GetParentLocations(LocTypeStarSystem)[0] ; #DEBUG_LINE_NO:426
  PlanetReferencePlanetLocation.ForceLocationTo(planetLocation) ; #DEBUG_LINE_NO:428
  PlanetReferenceSystemLocation.ForceLocationTo(systemLocation) ; #DEBUG_LINE_NO:429
EndFunction

Function UpdateTextReplacementAliases(ObjectReference siteMarkerRef)
  PlanetReference.ForceRefTo(siteMarkerRef) ; #DEBUG_LINE_NO:434
  Location siteMarkerLocation = siteMarkerRef.GetCurrentLocation() ; #DEBUG_LINE_NO:435
  Location sitePlanetLocation = siteMarkerLocation.GetParentLocations(LocTypeMajorOrbital)[0] ; #DEBUG_LINE_NO:436
  Location siteSystemLocation = siteMarkerLocation.GetParentLocations(LocTypeStarSystem)[0] ; #DEBUG_LINE_NO:437
  PlanetReferencePlanetLocation.ForceLocationTo(sitePlanetLocation) ; #DEBUG_LINE_NO:439
  PlanetReferenceSystemLocation.ForceLocationTo(siteSystemLocation) ; #DEBUG_LINE_NO:440
EndFunction

sq_parentscript:planettraitdata Function GetPlanetTraitData(ObjectReference refToCheck)
  planettraitscantargetscript scanTargetRef = refToCheck as planettraitscantargetscript ; #DEBUG_LINE_NO:448
  sq_parentscript:planettraitdata theData = None ; #DEBUG_LINE_NO:451
  planet planetToCheck = refToCheck.GetCurrentPlanet() ; #DEBUG_LINE_NO:452
  Location locationToCheck = refToCheck.GetCurrentLocation() ; #DEBUG_LINE_NO:453
  mapmarkerplanettraitscript mapMarkerRef = None ; #DEBUG_LINE_NO:456
  If scanTargetRef as Bool && scanTargetRef.GetLinkedRef(SQ_PlanetTraitOverrideLink) is mapmarkerplanettraitscript ; #DEBUG_LINE_NO:458
    mapMarkerRef = scanTargetRef.GetLinkedRef(SQ_PlanetTraitOverrideLink) as mapmarkerplanettraitscript ; #DEBUG_LINE_NO:459
  Else
    mapMarkerRef = refToCheck as mapmarkerplanettraitscript ; #DEBUG_LINE_NO:461
  EndIf
  If mapMarkerRef as Bool && mapMarkerRef.LocationTraitOverride as Bool ; #DEBUG_LINE_NO:464
    theData = Self.FindMatchingPlanetTraitForKeyword(mapMarkerRef.LocationTraitOverride, planetToCheck) ; #DEBUG_LINE_NO:466
  Else
    theData = Self.FindMatchingPlanetTrait(locationToCheck, planetToCheck) ; #DEBUG_LINE_NO:469
  EndIf
  Return theData ; #DEBUG_LINE_NO:471
EndFunction

Function DiscoverMatchingPlanetTraits(ObjectReference refToCheck, Bool incrementScanCount)
  Guard discoverMatchingTraitsBusy ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:478
    sq_parentscript:planettraitdata theData = Self.GetPlanetTraitData(refToCheck) ; #DEBUG_LINE_NO:481
    planettraitscantargetscript scanTargetRef = refToCheck as planettraitscantargetscript ; #DEBUG_LINE_NO:484
    If scanTargetRef ; #DEBUG_LINE_NO:485
      ScanTargetName.ForceRefTo(scanTargetRef.PlanetTraitScanTargetRef) ; #DEBUG_LINE_NO:486
      ScanTarget.ForceRefTo(scanTargetRef as ObjectReference) ; #DEBUG_LINE_NO:487
    EndIf
    Location locationToCheck = refToCheck.GetCurrentLocation() ; #DEBUG_LINE_NO:490
    If theData ; #DEBUG_LINE_NO:492
      Int scanCountNeeded = locationToCheck.GetRefTypeAliveCount(PlanetTraitScanTargetLocRef) ; #DEBUG_LINE_NO:496
      Int scanCount = locationToCheck.GetValue(PlanetTraitLocationScanCount) as Int ; #DEBUG_LINE_NO:497
      If incrementScanCount ; #DEBUG_LINE_NO:498
        scanCount += 1 ; #DEBUG_LINE_NO:499
      EndIf
      locationToCheck.SetValue(PlanetTraitLocationScanCount, scanCount as Float) ; #DEBUG_LINE_NO:502
      If scanCount >= scanCountNeeded ; #DEBUG_LINE_NO:505
        If locationToCheck.IsExplored() == False ; #DEBUG_LINE_NO:506
          locationToCheck.SetExplored(True) ; #DEBUG_LINE_NO:508
          Self.UpdatePlanetTraitDiscoveryPrivate(refToCheck, theData, 1) ; #DEBUG_LINE_NO:510
        EndIf
      Else
        Self.UpdateTextReplacementAliases(refToCheck) ; #DEBUG_LINE_NO:513
        PlanetTraitCategory.ForceRefTo(theData.TraitCategoryMarker) ; #DEBUG_LINE_NO:514
        PlanetTraitName.ForceRefTo(theData.TraitNameMarker) ; #DEBUG_LINE_NO:515
        If incrementScanCount
          
        ElseIf locationToCheck.IsExplored() == False ; #DEBUG_LINE_NO:520
          SQ_ParentPlanetTraitExploreMessage.Show(scanCount as Float, scanCountNeeded as Float, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) ; #DEBUG_LINE_NO:522
        EndIf
      EndIf
    EndIf
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

Function UpdatePlanetTraitDiscovery(ObjectReference refToCheck, sq_parentscript:planettraitdata theData, Int incrementAmount)
  Guard discoverMatchingTraitsBusy ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:532
    Self.UpdatePlanetTraitDiscoveryPrivate(refToCheck, theData, incrementAmount) ; #DEBUG_LINE_NO:533
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

Function UpdatePlanetTraitDiscoveryPrivate(ObjectReference refToCheck, sq_parentscript:planettraitdata theData, Int incrementAmount)
  If theData as Bool && refToCheck as Bool ; #DEBUG_LINE_NO:538
    planet planetToCheck = refToCheck.GetCurrentPlanet() ; #DEBUG_LINE_NO:540
    Self.UpdateTextReplacementAliases(refToCheck) ; #DEBUG_LINE_NO:542
    Location planetLocation = PlanetReferencePlanetLocation.GetLocation() ; #DEBUG_LINE_NO:543
    PlanetTraitCategory.ForceRefTo(theData.TraitCategoryMarker) ; #DEBUG_LINE_NO:545
    PlanetTraitName.ForceRefTo(theData.TraitNameMarker) ; #DEBUG_LINE_NO:546
    If planetToCheck.IsTraitKnown(theData.PlanetTrait) ; #DEBUG_LINE_NO:549
      
    Else
      Int traitValue = planetLocation.GetValue(theData.PlanetTraitAV) as Int + incrementAmount ; #DEBUG_LINE_NO:554
      planetLocation.SetValue(theData.PlanetTraitAV, traitValue as Float) ; #DEBUG_LINE_NO:557
      Int surveyingBonus = Game.GetPlayer().GetValueInt(SurveyingTraitBonus) ; #DEBUG_LINE_NO:560
      Int discoverCountRequiredActual = 1 ; #DEBUG_LINE_NO:564
      If traitValue >= discoverCountRequiredActual ; #DEBUG_LINE_NO:568
        Self.DiscoverPlanetTrait(planetToCheck, theData, traitValue, 1.0) ; #DEBUG_LINE_NO:570
      Else
        SQ_ParentPlanetTraitUpdateMessage.Show(traitValue as Float, discoverCountRequiredActual as Float, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) ; #DEBUG_LINE_NO:574
        Self.SendPlanetTraitUpdatedEvent(planetToCheck, theData.PlanetTrait, traitValue, theData.discoverCountRequired) ; #DEBUG_LINE_NO:576
      EndIf
    EndIf
  EndIf
EndFunction

Function DiscoverPlanetTrait(planet planetToCheck, sq_parentscript:planettraitdata theData, Int traitValue, Float xpMult)
  PlanetTraitCategory.ForceRefTo(theData.TraitCategoryMarker) ; #DEBUG_LINE_NO:586
  PlanetTraitName.ForceRefTo(theData.TraitNameMarker) ; #DEBUG_LINE_NO:587
  planetToCheck.SetTraitKnown(theData.PlanetTrait, True) ; #DEBUG_LINE_NO:590
  Int xpReward = (PlanetTraitXPReward.GetValueInt() as Float * xpMult) as Int ; #DEBUG_LINE_NO:594
  Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:596
  If (theData.AssociatedSkill as Bool && playerRef.HasPerk(theData.AssociatedSkill)) || (theData.AssociatedSkill02 as Bool && playerRef.HasPerk(theData.AssociatedSkill)) ; #DEBUG_LINE_NO:597
    xpReward += PlanetTraitXPRewardSkillBonus.GetValueInt() ; #DEBUG_LINE_NO:598
  EndIf
  Game.RewardPlayerXP(xpReward, False) ; #DEBUG_LINE_NO:601
  Self.SendPlanetTraitDiscoveredEvent(planetToCheck, theData.PlanetTrait, traitValue, theData.discoverCountRequired) ; #DEBUG_LINE_NO:604
EndFunction

Function CheckForScanTargetUpdate(ObjectReference refToCheck)
  Location locationToCheck = refToCheck.GetCurrentLocation() ; #DEBUG_LINE_NO:608
  planet planetToCheck = refToCheck.GetCurrentPlanet() ; #DEBUG_LINE_NO:609
  sq_parentscript:planettraitdata theData = Self.FindMatchingPlanetTrait(locationToCheck, planetToCheck) ; #DEBUG_LINE_NO:610
  If theData ; #DEBUG_LINE_NO:611
    If planetToCheck.IsTraitKnown(theData.PlanetTrait) ; #DEBUG_LINE_NO:613
      Self.UpdateScanTarget(refToCheck) ; #DEBUG_LINE_NO:615
      locationToCheck.SetExplored(True) ; #DEBUG_LINE_NO:617
    EndIf
  EndIf
EndFunction

Function UpdateScanTarget(ObjectReference refToCheck)
  planettraitscantargetscript scanTargetRef = refToCheck as planettraitscantargetscript ; #DEBUG_LINE_NO:624
  If scanTargetRef ; #DEBUG_LINE_NO:625
    ScanTargetName.ForceRefTo(scanTargetRef.PlanetTraitScanTargetRef) ; #DEBUG_LINE_NO:626
    ScanTarget.ForceRefTo(scanTargetRef as ObjectReference) ; #DEBUG_LINE_NO:627
    scanTargetRef.SetScanned(True) ; #DEBUG_LINE_NO:628
  EndIf
EndFunction

sq_parentscript:planettraitdata Function FindMatchingPlanetTrait(Location locationToCheck, planet planetToCheck)
  Keyword[] matchingKeywords = planetToCheck.GetKeywordTypeList(KeywordType_PlanetTrait) ; #DEBUG_LINE_NO:633
  sq_parentscript:planettraitdata theData = None ; #DEBUG_LINE_NO:636
  Int I = 0 ; #DEBUG_LINE_NO:638
  While I < matchingKeywords.Length && theData == None ; #DEBUG_LINE_NO:639
    Keyword planetTraitKeyword = matchingKeywords[I] ; #DEBUG_LINE_NO:640
    If locationToCheck.HasKeyword(planetTraitKeyword) ; #DEBUG_LINE_NO:642
      Int planetTraitIndex = PlanetTraits.findstruct("PlanetTrait", planetTraitKeyword, 0) ; #DEBUG_LINE_NO:645
      If planetTraitIndex > -1 ; #DEBUG_LINE_NO:646
        theData = PlanetTraits[planetTraitIndex] ; #DEBUG_LINE_NO:647
      EndIf
    EndIf
    I += 1 ; #DEBUG_LINE_NO:652
  EndWhile
  Return theData ; #DEBUG_LINE_NO:655
EndFunction

sq_parentscript:planettraitdata Function FindMatchingPlanetTraitForKeyword(Keyword keywordToCheck, planet planetToCheck)
  Keyword[] matchingKeywords = planetToCheck.GetKeywordTypeList(KeywordType_PlanetTrait) ; #DEBUG_LINE_NO:659
  sq_parentscript:planettraitdata theData = None ; #DEBUG_LINE_NO:662
  Int I = 0 ; #DEBUG_LINE_NO:664
  While I < matchingKeywords.Length && theData == None ; #DEBUG_LINE_NO:665
    Keyword planetTraitKeyword = matchingKeywords[I] ; #DEBUG_LINE_NO:666
    If keywordToCheck == planetTraitKeyword ; #DEBUG_LINE_NO:668
      Int planetTraitIndex = PlanetTraits.findstruct("PlanetTrait", planetTraitKeyword, 0) ; #DEBUG_LINE_NO:671
      If planetTraitIndex > -1 ; #DEBUG_LINE_NO:672
        theData = PlanetTraits[planetTraitIndex] ; #DEBUG_LINE_NO:673
      EndIf
    EndIf
    I += 1 ; #DEBUG_LINE_NO:678
  EndWhile
  Return theData ; #DEBUG_LINE_NO:681
EndFunction

Function OnPlayerScanPlanet(planet planetToCheck)
  Int discoverTraitChance = Skill_Astrophysics_DiscoverTraitChance.GetValueInt() ; #DEBUG_LINE_NO:686
  If discoverTraitChance > 0 ; #DEBUG_LINE_NO:689
    Keyword[] matchingKeywords = planetToCheck.GetKeywordTypeList(KeywordType_PlanetTrait) ; #DEBUG_LINE_NO:690
    If matchingKeywords.Length > 0 ; #DEBUG_LINE_NO:692
      Int discoveryRoll = Utility.RandomInt(0, 100) ; #DEBUG_LINE_NO:694
      If discoveryRoll <= discoverTraitChance ; #DEBUG_LINE_NO:696
        Int I = matchingKeywords.Length - 1 ; #DEBUG_LINE_NO:698
        While I > -1 ; #DEBUG_LINE_NO:699
          If planetToCheck.IsTraitKnown(matchingKeywords[I]) ; #DEBUG_LINE_NO:700
            matchingKeywords.remove(I, 1) ; #DEBUG_LINE_NO:701
          EndIf
          I += -1 ; #DEBUG_LINE_NO:703
        EndWhile
        If matchingKeywords.Length > 0 ; #DEBUG_LINE_NO:705
          Int randomKeyword = Utility.RandomInt(0, matchingKeywords.Length - 1) ; #DEBUG_LINE_NO:706
          Keyword traitToDiscover = matchingKeywords[randomKeyword] ; #DEBUG_LINE_NO:707
          sq_parentscript:planettraitdata theData = Self.FindMatchingPlanetTraitForKeyword(traitToDiscover, planetToCheck) ; #DEBUG_LINE_NO:709
          Guard discoverMatchingTraitsBusy ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:710
            Self.DiscoverPlanetTrait(planetToCheck, theData, 99, PlanetTraitXPRewardAstrophysicsMult.GetValue()) ; #DEBUG_LINE_NO:711
          EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
        EndIf
      EndIf
    EndIf
  EndIf
  Self.CheckCompletePlanetSurvey(planetToCheck, False) ; #DEBUG_LINE_NO:721
EndFunction

Function CheckCompletePlanetSurvey(planet planetToCheck, Bool bForceCompletion)
  Guard planetSurveyLock ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:730
    If planetToCheck.HasKeyword(LocTypeMajorOrbital) ; #DEBUG_LINE_NO:734
      Location planetLocation = planetToCheck.GetLocation() ; #DEBUG_LINE_NO:735
      Float lastSurveyReward = planetLocation.GetValue(SQ_PlanetLastSurveyReward) ; #DEBUG_LINE_NO:738
      Float currentSurvey = planetToCheck.GetSurveyPercent() ; #DEBUG_LINE_NO:739
      If currentSurvey > lastSurveyReward ; #DEBUG_LINE_NO:742
        Float actualRewardThreshold = 0.0 ; #DEBUG_LINE_NO:744
        Float currentRewardThreshold = 0.0 ; #DEBUG_LINE_NO:745
        Int I = 0 ; #DEBUG_LINE_NO:747
        While I < SurveyRewardThresholds.Length && actualRewardThreshold == 0.0 ; #DEBUG_LINE_NO:749
          If currentSurvey >= SurveyRewardThresholds[I] ; #DEBUG_LINE_NO:750
            actualRewardThreshold = SurveyRewardThresholds[I] ; #DEBUG_LINE_NO:751
          EndIf
          I += 1 ; #DEBUG_LINE_NO:753
        EndWhile
        currentRewardThreshold = actualRewardThreshold ; #DEBUG_LINE_NO:755
        If bForceCompletion ; #DEBUG_LINE_NO:756
          currentRewardThreshold = 1.0 ; #DEBUG_LINE_NO:757
        EndIf
        If currentRewardThreshold > lastSurveyReward ; #DEBUG_LINE_NO:761
          planetLocation.SetValue(SQ_PlanetLastSurveyReward, currentRewardThreshold) ; #DEBUG_LINE_NO:765
          sq_planetsurveyslatescript slateToReplace = None ; #DEBUG_LINE_NO:768
          If lastSurveyReward > 0.0 ; #DEBUG_LINE_NO:770
            sq_planetsurveyslatescript[] surveySlateArray = SurveySlates.GetArray() as sq_planetsurveyslatescript[] ; #DEBUG_LINE_NO:772
            I = 0 ; #DEBUG_LINE_NO:773
            While I < surveySlateArray.Length && slateToReplace == None ; #DEBUG_LINE_NO:774
              sq_planetsurveyslatescript theSlate = surveySlateArray[I] ; #DEBUG_LINE_NO:775
              If theSlate.myPlanet == planetToCheck ; #DEBUG_LINE_NO:776
                slateToReplace = theSlate ; #DEBUG_LINE_NO:777
                Self.UnregisterForRemoteEvent(slateToReplace as ScriptObject, "OnSell") ; #DEBUG_LINE_NO:778
              EndIf
              I += 1 ; #DEBUG_LINE_NO:780
            EndWhile
            If slateToReplace == None ; #DEBUG_LINE_NO:783
              currentRewardThreshold -= lastSurveyReward ; #DEBUG_LINE_NO:786
            EndIf
          EndIf
          Int totalTraitValue = Self.GetPlanetTraitValue(planetToCheck) + Self.GetPlanetAbundanceValue(planetToCheck) as Int ; #DEBUG_LINE_NO:793
          Actor player = Game.GetPlayer() ; #DEBUG_LINE_NO:797
          spaceshipreference playerShipRef = PlayerShip.GetShipRef() ; #DEBUG_LINE_NO:798
          If playerShipRef == None ; #DEBUG_LINE_NO:799
            
          ElseIf MB_Parent.IsPlanetSurveyTarget(playerShipRef as ObjectReference) ; #DEBUG_LINE_NO:802
            
          Else
            Book planetSurveySlate = None ; #DEBUG_LINE_NO:807
            GlobalVariable RewardXP = None ; #DEBUG_LINE_NO:808
            sq_parentscript:planetsurveyslatedata theData = Self.GetSurveySlateData(totalTraitValue, currentRewardThreshold) ; #DEBUG_LINE_NO:809
            If theData ; #DEBUG_LINE_NO:810
              planetSurveySlate = theData.planetSurveySlate ; #DEBUG_LINE_NO:811
              RewardXP = theData.RewardXP ; #DEBUG_LINE_NO:812
            EndIf
            Self.UpdateTextReplacementAliasesPlanet(planetToCheck) ; #DEBUG_LINE_NO:816
            Keyword[] keywords = new Keyword[0] ; #DEBUG_LINE_NO:817
            If SQ_PlanetIsHabitable.IsTrue(playerShipRef as ObjectReference, None) ; #DEBUG_LINE_NO:818
              keywords.add(SQ_PlanetIsHabitableKeyword, 1) ; #DEBUG_LINE_NO:820
              Rad01_LIST.SurveyCompleted() ; #DEBUG_LINE_NO:823
            EndIf
            ObjectReference currentSlateContainer = player as ObjectReference ; #DEBUG_LINE_NO:826
            If slateToReplace ; #DEBUG_LINE_NO:827
              currentSlateContainer = slateToReplace.GetContainer() ; #DEBUG_LINE_NO:828
            EndIf
            sq_planetsurveyslatescript newSlate = None ; #DEBUG_LINE_NO:831
            Bool addNewItem = False ; #DEBUG_LINE_NO:832
            If currentSlateContainer ; #DEBUG_LINE_NO:834
              addNewItem = True ; #DEBUG_LINE_NO:836
              If slateToReplace ; #DEBUG_LINE_NO:837
                SurveySlates.RemoveRef(slateToReplace as ObjectReference) ; #DEBUG_LINE_NO:838
                currentSlateContainer.RemoveItem(slateToReplace as Form, 1, True, None) ; #DEBUG_LINE_NO:839
                addNewItem = False ; #DEBUG_LINE_NO:840
              EndIf
              newSlate = currentSlateContainer.AddAliasedItemWithKeywordsSingle(planetSurveySlate as Form, SurveySlate as Alias, True, keywords) as sq_planetsurveyslatescript ; #DEBUG_LINE_NO:842
            Else
              newSlate = slateToReplace.PlaceAtMe(planetSurveySlate as Form, 1, False, True, True, None, SurveySlate as Alias, True) as sq_planetsurveyslatescript ; #DEBUG_LINE_NO:845
              If keywords != None && keywords.Length > 0 ; #DEBUG_LINE_NO:847
                I = 0 ; #DEBUG_LINE_NO:848
                While I < keywords.Length ; #DEBUG_LINE_NO:849
                  newSlate.AddKeyword(keywords[I]) ; #DEBUG_LINE_NO:850
                  I += 1 ; #DEBUG_LINE_NO:851
                EndWhile
              EndIf
              SurveySlates.RemoveRef(slateToReplace as ObjectReference) ; #DEBUG_LINE_NO:854
              slateToReplace.DisableNoWait(False) ; #DEBUG_LINE_NO:856
              newSlate.EnableNoWait(False) ; #DEBUG_LINE_NO:857
            EndIf
            SurveySlates.AddRef(newSlate as ObjectReference) ; #DEBUG_LINE_NO:859
            Int rewardThresholdPercent = (actualRewardThreshold * 100.0) as Int ; #DEBUG_LINE_NO:861
            Int addNewItemInt = 0 ; #DEBUG_LINE_NO:862
            If addNewItem ; #DEBUG_LINE_NO:863
              addNewItemInt = 1 ; #DEBUG_LINE_NO:864
            EndIf
            Quest[] surveySlateQuests = SQ_SurveySlateEvent.SendStoryEventAndWait(None, None, None, addNewItemInt, rewardThresholdPercent) ; #DEBUG_LINE_NO:866
            If surveySlateQuests == None ; #DEBUG_LINE_NO:867
              
            EndIf
            newSlate.myPlanet = planetToCheck ; #DEBUG_LINE_NO:871
            Self.RegisterForRemoteEvent(newSlate as ScriptObject, "OnSell") ; #DEBUG_LINE_NO:872
            If currentSurvey >= 1.0 ; #DEBUG_LINE_NO:874
              If RewardXP ; #DEBUG_LINE_NO:875
                Game.RewardPlayerXP(RewardXP.GetValueInt(), False) ; #DEBUG_LINE_NO:876
              EndIf
              Self.CheckCompleteSystemSurvey(planetToCheck) ; #DEBUG_LINE_NO:879
            EndIf
          EndIf
        EndIf
      EndIf
    EndIf
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

sq_parentscript:planetsurveyslatedata Function GetSurveySlateData(Int planetRewardValue, Float currentRewardThreshold)
  sq_parentscript:planetsurveyslatedata rewardSlateData = None ; #DEBUG_LINE_NO:893
  Int S = 0 ; #DEBUG_LINE_NO:896
  While S < PlanetSurveySlates.Length && rewardSlateData == None ; #DEBUG_LINE_NO:897
    sq_parentscript:planetsurveyslatedata theData = PlanetSurveySlates[S] ; #DEBUG_LINE_NO:898
    If theData.minTraitValue <= planetRewardValue && currentRewardThreshold >= theData.minSurveyPercent ; #DEBUG_LINE_NO:899
      rewardSlateData = theData ; #DEBUG_LINE_NO:900
    EndIf
    S += 1 ; #DEBUG_LINE_NO:902
  EndWhile
  Return rewardSlateData ; #DEBUG_LINE_NO:905
EndFunction

Function CheckCompleteSystemSurvey(planet planetToCheck)
  Location currentSystem = PlanetReferenceSystemLocation.GetLocation() ; #DEBUG_LINE_NO:909
  If currentSystem ; #DEBUG_LINE_NO:911
    Int I = 0 ; #DEBUG_LINE_NO:912
    Bool foundMatch = False ; #DEBUG_LINE_NO:913
    While I < SystemsFullySurveyed.Length && foundMatch == False ; #DEBUG_LINE_NO:914
      Location theSystem = SystemsFullySurveyed[I] ; #DEBUG_LINE_NO:915
      foundMatch = theSystem == currentSystem ; #DEBUG_LINE_NO:916
      I += 1 ; #DEBUG_LINE_NO:917
    EndWhile
    If foundMatch == False ; #DEBUG_LINE_NO:919
      I = 0 ; #DEBUG_LINE_NO:922
      Float surveyPercentage = 1.0 ; #DEBUG_LINE_NO:923
      Int systemTraitValue = 0 ; #DEBUG_LINE_NO:924
      While I < SystemPlanets.Length && surveyPercentage >= 1.0 ; #DEBUG_LINE_NO:925
        LocationAlias planetAlias = SystemPlanets[I] ; #DEBUG_LINE_NO:926
        planetAlias.RefillAlias() ; #DEBUG_LINE_NO:927
        Location planetLocation = planetAlias.GetLocation() ; #DEBUG_LINE_NO:928
        If planetLocation ; #DEBUG_LINE_NO:929
          planet thePlanet = planetLocation.GetCurrentPlanet() ; #DEBUG_LINE_NO:930
          surveyPercentage = thePlanet.GetSurveyPercent() ; #DEBUG_LINE_NO:931
          If surveyPercentage >= 1.0 ; #DEBUG_LINE_NO:933
            systemTraitValue += Self.GetPlanetTraitValue(thePlanet) ; #DEBUG_LINE_NO:935
          EndIf
        EndIf
        I += 1 ; #DEBUG_LINE_NO:938
      EndWhile
      If surveyPercentage >= 1.0 ; #DEBUG_LINE_NO:943
        I = 0 ; #DEBUG_LINE_NO:946
        GlobalVariable RewardXP = None ; #DEBUG_LINE_NO:947
        While I < SystemSurveyRewards.Length && RewardXP == None ; #DEBUG_LINE_NO:948
          sq_parentscript:systemsurveydata theRewardData = SystemSurveyRewards[I] ; #DEBUG_LINE_NO:949
          If systemTraitValue >= theRewardData.minTraitValue ; #DEBUG_LINE_NO:950
            RewardXP = theRewardData.RewardXP ; #DEBUG_LINE_NO:951
          EndIf
          I += 1 ; #DEBUG_LINE_NO:953
        EndWhile
        If RewardXP ; #DEBUG_LINE_NO:956
          Game.RewardPlayerXP(RewardXP.GetValueInt(), False) ; #DEBUG_LINE_NO:957
        EndIf
        SystemsFullySurveyed.add(currentSystem, 1) ; #DEBUG_LINE_NO:959
      EndIf
    EndIf
  EndIf
EndFunction

Int Function GetPlanetTraitValue(planet planetToCheck)
  Keyword[] matchingKeywords = planetToCheck.GetKeywordTypeList(KeywordType_PlanetTrait) ; #DEBUG_LINE_NO:966
  Int I = 0 ; #DEBUG_LINE_NO:967
  Int totalTraitValue = 0 ; #DEBUG_LINE_NO:968
  While I < matchingKeywords.Length ; #DEBUG_LINE_NO:969
    Keyword planetTraitKeyword = matchingKeywords[I] ; #DEBUG_LINE_NO:970
    Bool allTraitsKnown = planetToCheck.IsTraitKnown(planetTraitKeyword) ; #DEBUG_LINE_NO:971
    Int planetTraitIndex = PlanetTraits.findstruct("PlanetTrait", planetTraitKeyword, 0) ; #DEBUG_LINE_NO:973
    If planetTraitIndex > -1 ; #DEBUG_LINE_NO:974
      sq_parentscript:planettraitdata theData = PlanetTraits[planetTraitIndex] ; #DEBUG_LINE_NO:975
      totalTraitValue += 1 ; #DEBUG_LINE_NO:977
    EndIf
    I += 1 ; #DEBUG_LINE_NO:980
  EndWhile
  Return totalTraitValue ; #DEBUG_LINE_NO:983
EndFunction

Float Function GetPlanetAbundanceValue(planet planetToCheck)
  Keyword[] matchingKeywords = planetToCheck.GetKeywordTypeList(KeywordType_PlanetFloraAbundance) ; #DEBUG_LINE_NO:987
  Int I = 0 ; #DEBUG_LINE_NO:988
  Float totalSurveyValue = 0.0 ; #DEBUG_LINE_NO:989
  While I < matchingKeywords.Length ; #DEBUG_LINE_NO:990
    Keyword planetKeyword = matchingKeywords[I] ; #DEBUG_LINE_NO:991
    Int planetTraitIndex = PlanetAbundanceKeywords.findstruct("planetKeyword", planetKeyword, 0) ; #DEBUG_LINE_NO:993
    If planetTraitIndex > -1 ; #DEBUG_LINE_NO:994
      sq_parentscript:planetabundancedata theData = PlanetAbundanceKeywords[planetTraitIndex] ; #DEBUG_LINE_NO:995
      totalSurveyValue += theData.surveyDifficulty ; #DEBUG_LINE_NO:996
    EndIf
    I += 1 ; #DEBUG_LINE_NO:999
  EndWhile
  matchingKeywords = planetToCheck.GetKeywordTypeList(KeywordType_PlanetFaunaAbundance) ; #DEBUG_LINE_NO:1002
  I = 0 ; #DEBUG_LINE_NO:1003
  While I < matchingKeywords.Length ; #DEBUG_LINE_NO:1004
    Keyword planetkeyword = matchingKeywords[I] ; #DEBUG_LINE_NO:1005
    Int planettraitindex = PlanetAbundanceKeywords.findstruct("planetKeyword", planetkeyword, 0) ; #DEBUG_LINE_NO:1007
    If planettraitindex > -1 ; #DEBUG_LINE_NO:1008
      sq_parentscript:planetabundancedata thedata = PlanetAbundanceKeywords[planettraitindex] ; #DEBUG_LINE_NO:1009
      totalSurveyValue += thedata.surveyDifficulty ; #DEBUG_LINE_NO:1010
    EndIf
    I += 1 ; #DEBUG_LINE_NO:1013
  EndWhile
  Return totalSurveyValue ; #DEBUG_LINE_NO:1017
EndFunction

Function CheckForRenameMapMarker(ObjectReference mapMarkerRef)
  Location myLocation = mapMarkerRef.GetCurrentLocation() ; #DEBUG_LINE_NO:1023
  If myLocation as Bool && Self.VisibleOnStarmap(myLocation) ; #DEBUG_LINE_NO:1024
    Int I = 0 ; #DEBUG_LINE_NO:1026
    Bool found = False ; #DEBUG_LINE_NO:1027
    While found == False && I < MapMarkerNames.Length ; #DEBUG_LINE_NO:1028
      sq_parentscript:mapmarkername theMarkerNameData = MapMarkerNames[I] ; #DEBUG_LINE_NO:1029
      found = myLocation.HasKeyword(theMarkerNameData.groupTypeKeyword) ; #DEBUG_LINE_NO:1030
      If found ; #DEBUG_LINE_NO:1031
        ObjectReference myRenameMarker = mapMarkerRef.PlaceAtMe(theMarkerNameData.nameMarker, 1, False, False, True, None, None, True) ; #DEBUG_LINE_NO:1033
        mapMarkerRef.SetLinkedRef(myRenameMarker, MapMarkerLinkedRefNameOverride, True) ; #DEBUG_LINE_NO:1034
      EndIf
      I += 1 ; #DEBUG_LINE_NO:1036
    EndWhile
  EndIf
EndFunction

Bool Function VisibleOnStarmap(Location locationToCheck)
  Int I = 0 ; #DEBUG_LINE_NO:1042
  Bool isVisible = False ; #DEBUG_LINE_NO:1043
  While I < OE_CiviliansVisibleFromStarmapKeywords.GetSize() && isVisible == False ; #DEBUG_LINE_NO:1044
    Keyword theKeyword = OE_CiviliansVisibleFromStarmapKeywords.GetAt(I) as Keyword ; #DEBUG_LINE_NO:1045
    isVisible = theKeyword as Bool && locationToCheck.HasKeyword(theKeyword) ; #DEBUG_LINE_NO:1046
    I += 1 ; #DEBUG_LINE_NO:1047
  EndWhile
  Return isVisible ; #DEBUG_LINE_NO:1050
EndFunction

Function HarvestActor(Actor HarvestActor)
  Guard harvestLock ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:1059
    Actor player = Game.GetPlayer() ; #DEBUG_LINE_NO:1061
    If HarvestActor ; #DEBUG_LINE_NO:1062
      If HarvestActor.GetValue(ZoologyNonLethalHarvestCount) < ZoologyNonLethalHarvestCountMax.GetValue() ; #DEBUG_LINE_NO:1063
        HarvestActor.ModValue(ZoologyNonLethalHarvestCount, 1.0) ; #DEBUG_LINE_NO:1065
        resource[] resources = HarvestActor.GetValueResources() ; #DEBUG_LINE_NO:1066
        Int I = 0 ; #DEBUG_LINE_NO:1067
        While I < resources.Length ; #DEBUG_LINE_NO:1068
          MiscObject harvestItem = resources[I].GetCreatedObject() as MiscObject ; #DEBUG_LINE_NO:1069
          If harvestItem ; #DEBUG_LINE_NO:1070
            player.AddItem(harvestItem as Form, 1, False) ; #DEBUG_LINE_NO:1071
            SQ_Companions.SendAffinityEvent(SQ_Companions.COM_Event_Action_Harvest_Animal, None, False) ; #DEBUG_LINE_NO:1075
          EndIf
          I += 1 ; #DEBUG_LINE_NO:1078
        EndWhile
        Game.RequestHUDRolloverRefresh() ; #DEBUG_LINE_NO:1080
      EndIf
    EndIf
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

Function SetupDamagedShip(spaceshipreference damagedShip, Bool shouldBeDestroyed, Bool shouldBeDerelict, Bool shouldApplyDerelictName, Bool shouldDamageRandomSystems, Bool shouldDisableExteriorLights, Bool shouldBlockRepairOfDestroyedParts, sq_parentscript:actorvaluedatum[] additionalActorValuesToDamage)
  If damagedShip == None ; #DEBUG_LINE_NO:1092
    
  ElseIf shouldBeDestroyed
    damagedShip.KillSilent(None) ; #DEBUG_LINE_NO:1097
  Else
    If shouldBeDerelict ; #DEBUG_LINE_NO:1099
      damagedShip.AddKeyword(BEDerelict) ; #DEBUG_LINE_NO:1100
      damagedShip.DamageValue(SpaceshipCrew, damagedShip.GetValue(SpaceshipCrew)) ; #DEBUG_LINE_NO:1101
    EndIf
    If shouldApplyDerelictName ; #DEBUG_LINE_NO:1103
      DerelictShipNameAlias.ApplyToRef(damagedShip as ObjectReference) ; #DEBUG_LINE_NO:1104
    EndIf
    If shouldDamageRandomSystems ; #DEBUG_LINE_NO:1106
      Int I = 0 ; #DEBUG_LINE_NO:1107
      While I < RandomShipDamageData.Length ; #DEBUG_LINE_NO:1108
        Float DamagePercent = 0.0 ; #DEBUG_LINE_NO:1109
        If Utility.RandomFloat(0.0, 1.0) < RandomShipDamageData[I].ChanceDestroyed ; #DEBUG_LINE_NO:1110
          DamagePercent = 1.0 ; #DEBUG_LINE_NO:1111
        Else
          DamagePercent = 1.0 - Utility.RandomFloat(RandomShipDamageData[I].MinDamagePercent, RandomShipDamageData[I].MaxDamagePercent) ; #DEBUG_LINE_NO:1113
        EndIf
        damagedShip.DamageValue(RandomShipDamageData[I].ActorValueToDamage, damagedShip.GetValue(RandomShipDamageData[I].ActorValueToDamage) * DamagePercent) ; #DEBUG_LINE_NO:1115
        I += 1 ; #DEBUG_LINE_NO:1116
      EndWhile
    EndIf
    If shouldDisableExteriorLights ; #DEBUG_LINE_NO:1119
      damagedShip.SetValue(SpaceshipExternalLightsDisabledValue, 1.0) ; #DEBUG_LINE_NO:1120
    EndIf
    If shouldBlockRepairOfDestroyedParts ; #DEBUG_LINE_NO:1122
      damagedShip.EnablePartRepair(None, False) ; #DEBUG_LINE_NO:1123
    EndIf
    If additionalActorValuesToDamage != None ; #DEBUG_LINE_NO:1125
      Int i = 0 ; #DEBUG_LINE_NO:1126
      While i < additionalActorValuesToDamage.Length ; #DEBUG_LINE_NO:1127
        damagedShip.DamageValue(additionalActorValuesToDamage[i].ActorValueToDamage, damagedShip.GetValue(additionalActorValuesToDamage[i].ActorValueToDamage) * additionalActorValuesToDamage[i].DamagePercent) ; #DEBUG_LINE_NO:1128
        i += 1 ; #DEBUG_LINE_NO:1129
      EndWhile
    EndIf
  EndIf
EndFunction

Float Function GetSmugglingChance(spaceshipreference playerShipRef, spaceshipreference scanningShipRef)
  Int contrabandStatus = playerShipRef.CheckContrabandStatus(True) ; #DEBUG_LINE_NO:1174
  Float realChance = 0.0 ; #DEBUG_LINE_NO:1175
  If contrabandStatus < 0 ; #DEBUG_LINE_NO:1177
    realChance = 100.0 ; #DEBUG_LINE_NO:1180
  ElseIf contrabandStatus > 0 ; #DEBUG_LINE_NO:1181
    realChance = 0.0 ; #DEBUG_LINE_NO:1183
  Else
    Float contrabandWeight = playerShipRef.GetContrabandWeight(False) ; #DEBUG_LINE_NO:1191
    Float contrabandWeightShip = playerShipRef.GetContrabandWeight(True) ; #DEBUG_LINE_NO:1192
    Float contrabandCapacity = playerShipRef.GetValue(CarryWeightShielded) ; #DEBUG_LINE_NO:1193
    Int playerSmugglingSkillValue = Math.Clamp(Game.GetPlayer().GetValueInt(PayloadLevel) as Float, 0.0, (PlayerSkillMults.Length - 1) as Float) as Int ; #DEBUG_LINE_NO:1195
    Float playerSmugglingSkillBonus = PlayerSkillMults[playerSmugglingSkillValue] ; #DEBUG_LINE_NO:1196
    Int playerScanJammerValue = Math.Clamp(playerShipRef.GetValueInt(SpaceshipScanJammer) as Float, 0.0, (ScanJammerMults.Length - 1) as Float) as Int ; #DEBUG_LINE_NO:1198
    Float playerScanJammerBonus = ScanJammerMults[playerScanJammerValue] ; #DEBUG_LINE_NO:1199
    Float scanningShipPerception = scanningShipRef.GetValue(Perception) ; #DEBUG_LINE_NO:1201
    Float targetSkillFactor = fSmugglingTargetSkillMult * scanningShipPerception ; #DEBUG_LINE_NO:1210
    Float contrabandWeightFactor = fSmugglingWeightMult * Math.pow(contrabandWeight, fSmugglingWeightPower) * contrabandWeight / contrabandCapacity ; #DEBUG_LINE_NO:1211
    Float baseChance = fSmugglingBaseChance + targetSkillFactor + contrabandWeightFactor ; #DEBUG_LINE_NO:1218
    realChance = baseChance * (1.0 + playerScanJammerBonus) * (1.0 + playerSmugglingSkillBonus) ; #DEBUG_LINE_NO:1221
    realChance = Math.Max(realChance, fSmugglingMinChance) ; #DEBUG_LINE_NO:1223
    realChance = Math.Min(realChance, fSmugglingMaxChance) ; #DEBUG_LINE_NO:1224
  EndIf
  Return realChance ; #DEBUG_LINE_NO:1228
EndFunction

Bool Function SmugglingMinigame(spaceshipreference playerShipRef, spaceshipreference scanningShipRef)
  Float realChance = Self.GetSmugglingChance(playerShipRef, scanningShipRef) ; #DEBUG_LINE_NO:1232
  Float dieRoll = Utility.RandomFloat(1.0, 100.0) ; #DEBUG_LINE_NO:1234
  Bool bSuccess = dieRoll <= realChance ; #DEBUG_LINE_NO:1236
  If bSuccess ; #DEBUG_LINE_NO:1254
    Game.AddAchievement(SmugglingAchievementID) ; #DEBUG_LINE_NO:1256
  EndIf
  Return bSuccess ; #DEBUG_LINE_NO:1259
EndFunction

Function CheckForAttack(outpostbeaconscript outpostRef, Bool bForceAttack, Int iForceAttackType, Float fDieRoll)
  If outpostRef.AllowAttacks == False ; #DEBUG_LINE_NO:1285
    Return  ; #DEBUG_LINE_NO:1287
  EndIf
  If Game.GetPlayer().GetCurrentPlanet() != outpostRef.GetCurrentPlanet() ; #DEBUG_LINE_NO:1291
    Return  ; #DEBUG_LINE_NO:1293
  EndIf
  Location outpostLocation = outpostRef.GetCurrentLocation() ; #DEBUG_LINE_NO:1296
  Location[] parentLocations = outpostLocation.GetParentLocations(LocTypeMajorOrbital) ; #DEBUG_LINE_NO:1297
  If !parentLocations ; #DEBUG_LINE_NO:1298
    Return  ; #DEBUG_LINE_NO:1300
  EndIf
  Location myPlanet = parentLocations[0] ; #DEBUG_LINE_NO:1302
  Float lastAttackRoll = myPlanet.GetValue(OutpostLastAttackRollTimestamp) ; #DEBUG_LINE_NO:1305
  Float daysSinceLastRoll = Utility.GetCurrentGameTime() - lastAttackRoll ; #DEBUG_LINE_NO:1306
  If lastAttackRoll > 0.0 && minDaysSinceLastAttackRoll > daysSinceLastRoll && bForceAttack == False ; #DEBUG_LINE_NO:1307
    Return  ; #DEBUG_LINE_NO:1310
  EndIf
  Float lastAttack = myPlanet.GetValue(OutpostLastAttackTimestamp) ; #DEBUG_LINE_NO:1314
  Float daysSinceLastAttack = 0.0 ; #DEBUG_LINE_NO:1316
  If lastAttack <= 0.0 ; #DEBUG_LINE_NO:1317
    daysSinceLastAttack = minDaysSinceLastAttack + 1.0 ; #DEBUG_LINE_NO:1318
  Else
    daysSinceLastAttack = Utility.GetCurrentGameTime() - lastAttack ; #DEBUG_LINE_NO:1320
  EndIf
  If minDaysSinceLastAttack > daysSinceLastAttack && bForceAttack == False ; #DEBUG_LINE_NO:1324
    Return  ; #DEBUG_LINE_NO:1327
  EndIf
  Int I = 0 ; #DEBUG_LINE_NO:1331
  Float specialCaseRating = 0.0 ; #DEBUG_LINE_NO:1332
  While I < OutpostAttackSpecialCases.Length ; #DEBUG_LINE_NO:1333
    sq_parentscript:outpostattackspecialcase theSpecialCase = OutpostAttackSpecialCases[I] ; #DEBUG_LINE_NO:1334
    If theSpecialCase.specialCaseCondition.IsTrue(outpostRef as ObjectReference, None) ; #DEBUG_LINE_NO:1335
      If theSpecialCase.storyEventKeyword ; #DEBUG_LINE_NO:1336
        If Game.GetDieRollSuccess((theSpecialCase.specialCaseAttackChance * 100.0) as Int, 1, 100, -1, -1) || fDieRoll > 0.0 && fDieRoll <= theSpecialCase.specialCaseAttackChance ; #DEBUG_LINE_NO:1338
          Quest[] specialCaseAttacks = theSpecialCase.storyEventKeyword.SendStoryEventAndWait(None, outpostRef as ObjectReference, None, 0, 0) ; #DEBUG_LINE_NO:1339
          If specialCaseAttacks ; #DEBUG_LINE_NO:1340
            Return  ; #DEBUG_LINE_NO:1342
          EndIf
        EndIf
      Else
        specialCaseRating += theSpecialCase.specialCaseAttackChance ; #DEBUG_LINE_NO:1347
      EndIf
    EndIf
    I += 1 ; #DEBUG_LINE_NO:1350
  EndWhile
  missionparentscript:missionsupplytype[] resourceAVs = MB_Parent.resourceAVs ; #DEBUG_LINE_NO:1356
  Float resourceRating = 0.0 ; #DEBUG_LINE_NO:1358
  I = 0 ; #DEBUG_LINE_NO:1359
  While I < resourceAVs.Length ; #DEBUG_LINE_NO:1360
    Float resourceValue = outpostRef.GetValue(resourceAVs[I].supplyAV) ; #DEBUG_LINE_NO:1361
    If resourceValue > 0.0 ; #DEBUG_LINE_NO:1362
      resourceRating += resourceValue ; #DEBUG_LINE_NO:1364
    EndIf
    I += 1 ; #DEBUG_LINE_NO:1366
  EndWhile
  Float cargoLinkRating = outpostRef.GetValue(OutpostCargoLinkAV) ; #DEBUG_LINE_NO:1370
  Float attackChance = attackChanceBase + attackChanceResourceMult * resourceRating + attackChanceCargoLinkMult * cargoLinkRating + specialCaseRating ; #DEBUG_LINE_NO:1380
  Float attackRoll = Utility.RandomFloat(0.0, 1.0) ; #DEBUG_LINE_NO:1383
  If fDieRoll > 0.0 ; #DEBUG_LINE_NO:1384
    attackRoll = fDieRoll ; #DEBUG_LINE_NO:1385
  EndIf
  If attackRoll <= attackChanceCreature && iForceAttackType == 0 || iForceAttackType > 1 ; #DEBUG_LINE_NO:1389
    SQ_OutpostEventAttackCreatures.SendStoryEvent(None, outpostRef as ObjectReference, None, 0, 0) ; #DEBUG_LINE_NO:1391
  ElseIf attackRoll <= attackChance || iForceAttackType == 1 ; #DEBUG_LINE_NO:1392
    SQ_OutpostEventAttack.SendStoryEvent(None, outpostRef as ObjectReference, None, 0, 0) ; #DEBUG_LINE_NO:1394
  EndIf
  myPlanet.SetValue(OutpostLastAttackRollTimestamp, Utility.GetCurrentGameTime()) ; #DEBUG_LINE_NO:1397
EndFunction

Function ShowTutorialMessage(Int tutorialID)
  Guard tutorialMessageGuard ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:1405
    Int messageIndex = TutorialMessages.findstruct("tutorialID", tutorialID, 0) ; #DEBUG_LINE_NO:1406
    If messageIndex > -1 ; #DEBUG_LINE_NO:1407
      sq_parentscript:tutorialmessage theMessage = TutorialMessages[messageIndex] ; #DEBUG_LINE_NO:1408
      theMessage.TutorialMessage.Show(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) ; #DEBUG_LINE_NO:1409
      TutorialMessages.remove(messageIndex, 1) ; #DEBUG_LINE_NO:1411
    EndIf
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

Function IncrementComputersAchievement(ObjectReference terminalRef)
  Guard computersAchievementGuard ;*** WARNING: Experimental syntax, may be incorrect: Guard  ; #DEBUG_LINE_NO:1420
    If ComputersAchievementAwarded == False ; #DEBUG_LINE_NO:1421
      If terminalRef.GetValue(ComputersAchievementAV) == 0.0 ; #DEBUG_LINE_NO:1422
        terminalRef.SetValue(ComputersAchievementAV, 1.0) ; #DEBUG_LINE_NO:1425
        Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:1427
        playerRef.ModValue(ComputersAchievementAV, 1.0) ; #DEBUG_LINE_NO:1428
        Int computerCount = playerRef.GetValueInt(ComputersAchievementAV) ; #DEBUG_LINE_NO:1429
        If computerCount >= ComputersAchievementCount.GetValueInt() ; #DEBUG_LINE_NO:1431
          Game.AddAchievement(ComputersAchievementID) ; #DEBUG_LINE_NO:1433
          ComputersAchievementAwarded = True ; #DEBUG_LINE_NO:1434
        EndIf
      EndIf
    EndIf
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

Function HandleCriticalHit(spaceshipreference targetShip, shipcriticalhiteffectscript:criticalhiteffect[] CriticalHitEffects)
  TryGuard ShipCriticalHitGuard ;*** WARNING: Experimental syntax, may be incorrect: TryGuard  ; #DEBUG_LINE_NO:1502
    Int totalCriticalHitEffectWeight = 0 ; #DEBUG_LINE_NO:1504
    Int I = 0 ; #DEBUG_LINE_NO:1505
    While I < CriticalHitEffects.Length ; #DEBUG_LINE_NO:1506
      shipcriticalhiteffectscript:criticalhiteffect theCriticalHit = CriticalHitEffects[I] ; #DEBUG_LINE_NO:1507
      totalCriticalHitEffectWeight += theCriticalHit.weight ; #DEBUG_LINE_NO:1509
      I += 1 ; #DEBUG_LINE_NO:1510
    EndWhile
    Int criticalEffectRoll = Utility.RandomInt(1, totalCriticalHitEffectWeight) ; #DEBUG_LINE_NO:1513
    I = 0 ; #DEBUG_LINE_NO:1516
    Int currentWeight = 0 ; #DEBUG_LINE_NO:1517
    Int criticalHitEffectIndex = -1 ; #DEBUG_LINE_NO:1518
    While I < CriticalHitEffects.Length && criticalHitEffectIndex < 0 ; #DEBUG_LINE_NO:1519
      shipcriticalhiteffectscript:criticalhiteffect thecriticalhit = CriticalHitEffects[I] ; #DEBUG_LINE_NO:1520
      currentWeight += thecriticalhit.weight ; #DEBUG_LINE_NO:1521
      If criticalEffectRoll <= currentWeight ; #DEBUG_LINE_NO:1522
        criticalHitEffectIndex = I ; #DEBUG_LINE_NO:1523
      EndIf
      I += 1 ; #DEBUG_LINE_NO:1525
    EndWhile
    If criticalHitEffectIndex > -1 ; #DEBUG_LINE_NO:1528
      shipcriticalhiteffectscript:criticalhiteffect thecriticalhit = CriticalHitEffects[criticalHitEffectIndex] ; #DEBUG_LINE_NO:1529
      If thecriticalhit.criticalHitType == criticalHitType0_Reactor ; #DEBUG_LINE_NO:1532
        Float baseReactorHealth = targetShip.GetBaseValue(ShipSystemReactorHealth) ; #DEBUG_LINE_NO:1534
        Float minHealthDamage = baseReactorHealth * minReactorHealthDamage ; #DEBUG_LINE_NO:1535
        Float maxHealthDamage = baseReactorHealth * maxReactorHealthDamage ; #DEBUG_LINE_NO:1536
        Float damageHealth = Utility.RandomFloat(minHealthDamage, maxHealthDamage) ; #DEBUG_LINE_NO:1538
        targetShip.DamageValue(ShipSystemReactorHealth, damageHealth) ; #DEBUG_LINE_NO:1540
      ElseIf thecriticalhit.criticalHitType == criticalHitType1_Crew ; #DEBUG_LINE_NO:1541
        If targetShip.GetValue(SpaceshipCrew) > 0.0 ; #DEBUG_LINE_NO:1543
          Int currentCrew = targetShip.GetValueInt(SpaceshipCrew) ; #DEBUG_LINE_NO:1544
          Int crewDamage = Utility.RandomInt(1, currentCrew) ; #DEBUG_LINE_NO:1545
          crewDamage = Math.Min(maxCrewDamage as Float, crewDamage as Float) as Int ; #DEBUG_LINE_NO:1546
          targetShip.DamageValue(SpaceshipCrew, crewDamage as Float) ; #DEBUG_LINE_NO:1548
          If targetShip.GetValueInt(SpaceshipCrew) == 0 ; #DEBUG_LINE_NO:1549
            Float currentEngineHealth = targetShip.GetValue(ShipSystemEngineHealth) ; #DEBUG_LINE_NO:1551
            targetShip.DamageValue(ShipSystemEngineHealth, currentEngineHealth) ; #DEBUG_LINE_NO:1552
            targetShip.SetValue(SpaceshipCriticalHitCrew, 1.0) ; #DEBUG_LINE_NO:1554
          EndIf
        EndIf
      ElseIf thecriticalhit.criticalHitType == criticalHitType2_Hull ; #DEBUG_LINE_NO:1557
        Float baseHullHealth = targetShip.GetBaseValue(Health) ; #DEBUG_LINE_NO:1559
        Float minhealthdamage = baseHullHealth * minHullHealthDamage ; #DEBUG_LINE_NO:1560
        Float maxhealthdamage = baseHullHealth * maxHullHealthDamage ; #DEBUG_LINE_NO:1561
        Float damagehealth = Utility.RandomFloat(minhealthdamage, maxhealthdamage) ; #DEBUG_LINE_NO:1563
        targetShip.DamageValue(Health, damagehealth) ; #DEBUG_LINE_NO:1565
      ElseIf thecriticalhit.criticalHitType == criticalHitType3_System || thecriticalhit.criticalHitType == criticalHitType5_Engine || thecriticalhit.criticalHitType == criticalHitType6_Shield ; #DEBUG_LINE_NO:1566
        ActorValue systemAV = thecriticalhit.systemAV ; #DEBUG_LINE_NO:1570
        If systemAV == None ; #DEBUG_LINE_NO:1571
          ActorValue[] validSystemAVs = Self.GetValidSystemAVs(SystemHealthAVs, targetShip) ; #DEBUG_LINE_NO:1573
          Int systemIndex = Utility.RandomInt(0, validSystemAVs.Length - 1) ; #DEBUG_LINE_NO:1574
          systemAV = validSystemAVs[systemIndex] ; #DEBUG_LINE_NO:1575
        EndIf
        Float baseSystemHealth = targetShip.GetBaseValue(systemAV) ; #DEBUG_LINE_NO:1579
        Float minhealthdamage = baseSystemHealth * minSystemHealthDamage ; #DEBUG_LINE_NO:1580
        Float maxhealthdamage = baseSystemHealth * maxSystemHealthDamage ; #DEBUG_LINE_NO:1581
        Float damagehealth = Utility.RandomFloat(minhealthdamage, maxhealthdamage) ; #DEBUG_LINE_NO:1583
        targetShip.DamageValue(systemAV, damagehealth) ; #DEBUG_LINE_NO:1586
      ElseIf thecriticalhit.criticalHitType == criticalHitType4_EM ; #DEBUG_LINE_NO:1587
        ActorValue systemav = thecriticalhit.systemAV ; #DEBUG_LINE_NO:1590
        If systemav == None ; #DEBUG_LINE_NO:1591
          ActorValue[] validsystemavs = Self.GetValidSystemAVs(SystemEMHealthAVs, targetShip) ; #DEBUG_LINE_NO:1593
          Int systemindex = Utility.RandomInt(0, validsystemavs.Length - 1) ; #DEBUG_LINE_NO:1594
          systemav = validsystemavs[systemindex] ; #DEBUG_LINE_NO:1595
        EndIf
        Float basesystemhealth = targetShip.GetBaseValue(systemav) ; #DEBUG_LINE_NO:1598
        targetShip.DamageValue(systemav, basesystemhealth) ; #DEBUG_LINE_NO:1600
      EndIf
      Message criticalHitMessage = CriticalHitMessages[thecriticalhit.criticalHitType] ; #DEBUG_LINE_NO:1602
      If criticalHitMessage ; #DEBUG_LINE_NO:1603
        criticalHitMessage.Show(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) ; #DEBUG_LINE_NO:1605
        UICriticalHitShip.Play(Game.GetPlayer() as ObjectReference, None, None) ; #DEBUG_LINE_NO:1606
      EndIf
    EndIf
  EndGuard ;*** WARNING: Experimental syntax, may be incorrect: EndGuard 
EndFunction

ActorValue[] Function GetValidSystemAVs(ActorValue[] systemAVs, spaceshipreference targetShip)
  ActorValue[] validSystemAVs = new ActorValue[0] ; #DEBUG_LINE_NO:1614
  Int I = 0 ; #DEBUG_LINE_NO:1615
  While I < systemAVs.Length ; #DEBUG_LINE_NO:1616
    If targetShip.GetValue(systemAVs[I]) > 0.0 ; #DEBUG_LINE_NO:1617
      validSystemAVs.add(systemAVs[I], 1) ; #DEBUG_LINE_NO:1618
    EndIf
    I += 1 ; #DEBUG_LINE_NO:1620
  EndWhile
  Return validSystemAVs ; #DEBUG_LINE_NO:1623
EndFunction

Function TestSetTraitKnown(Keyword planetTraitKeyword)
  planet planetToCheck = Game.GetPlayer().GetCurrentPlanet() ; #DEBUG_LINE_NO:1638
  planetToCheck.SetTraitKnown(planetTraitKeyword, True) ; #DEBUG_LINE_NO:1639
  Self.UpdateTextReplacementAliases(Game.GetPlayer() as ObjectReference) ; #DEBUG_LINE_NO:1640
  sq_parentscript:planettraitdata theData = Self.FindMatchingPlanetTraitForKeyword(planetTraitKeyword, planetToCheck) ; #DEBUG_LINE_NO:1641
  If theData ; #DEBUG_LINE_NO:1642
    Self.UpdatePlanetTraitDiscovery(Game.GetPlayer() as ObjectReference, theData, 99) ; #DEBUG_LINE_NO:1643
  EndIf
EndFunction

Function testSetGroupFaction(Actor testActor, Faction testFaction)
  testActor.SetGroupFaction(testFaction) ; #DEBUG_LINE_NO:1650
EndFunction

Function testEnableSpaceTravel(Location testLocation, Bool bEnable)
  testLocation.EnableSpaceTravel(Self as Quest, bEnable) ; #DEBUG_LINE_NO:1655
EndFunction

Function testSetSmugglingBaseChance(Float baseChance)
  fSmugglingBaseChance = baseChance ; #DEBUG_LINE_NO:1659
EndFunction

Function testSetScanned(ObjectReference scannableRef)
  scannableRef.SetScanned(True) ; #DEBUG_LINE_NO:1668
EndFunction

Function testGetHoursUntil(Float afTargetHourLocal)
  Utility.GetHoursUntilLocalHour(afTargetHourLocal) ; #DEBUG_LINE_NO:1672
EndFunction

Function TestGetArray(FormList formlistToCheck)
  Form[] formArray = formlistToCheck.GetArray(False) ; #DEBUG_LINE_NO:1682
EndFunction

Function TestMoveToEditorLocation(ObjectReference testRef)
  testRef.MoveToMyEditorLocation() ; #DEBUG_LINE_NO:1696
EndFunction

Function TestCompleteResearchProject(researchproject testProject)
  testProject.Complete() ; #DEBUG_LINE_NO:1700
EndFunction

Function TestFindRefsWithKeyword(Keyword testKeyword)
  ObjectReference[] refArray = Game.GetPlayer().FindAllReferencesWithKeyword(testKeyword as Form, 5000.0) ; #DEBUG_LINE_NO:1705
  Self.testTraceRefArray(refArray) ; #DEBUG_LINE_NO:1707
EndFunction

Function TestFindRefsByID(Form baseForm)
  ObjectReference[] refArray = Game.GetPlayer().FindAllReferencesOfType(baseForm, 5000.0) ; #DEBUG_LINE_NO:1711
  Self.testTraceRefArray(refArray) ; #DEBUG_LINE_NO:1713
EndFunction

Function testTraceRefArray(ObjectReference[] refArray)
  ObjectReference playerRef = Game.GetPlayer() as ObjectReference ; #DEBUG_LINE_NO:1717
  Int I = 0 ; #DEBUG_LINE_NO:1718
  While I < refArray.Length ; #DEBUG_LINE_NO:1719
    I += 1 ; #DEBUG_LINE_NO:1721
  EndWhile
EndFunction

Bool Function AddSurveySite(surveysitediscovermarkerscript newSite)
  Int siteIndex = SurveySites.findstruct("siteMarker", newSite as ObjectReference, 0) ; #DEBUG_LINE_NO:1733
  If siteIndex > -1 ; #DEBUG_LINE_NO:1734
    Return False ; #DEBUG_LINE_NO:1735
  Else
    sq_parentscript:surveysite theSite = new sq_parentscript:surveysite ; #DEBUG_LINE_NO:1737
    theSite.siteMarker = newSite as ObjectReference ; #DEBUG_LINE_NO:1738
    SurveySites.add(theSite, 1) ; #DEBUG_LINE_NO:1739
    PlanetReference.ForceRefTo(newSite as ObjectReference) ; #DEBUG_LINE_NO:1740
    SQ_SurveySiteDiscoveredMessage.Show(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) ; #DEBUG_LINE_NO:1741
    Self.SendSurveySiteDiscoveredEvent(newSite as ObjectReference) ; #DEBUG_LINE_NO:1742
    Return True ; #DEBUG_LINE_NO:1743
  EndIf
EndFunction

Bool Function RemoveSurveySite(surveysitediscovermarkerscript siteToRemove, Bool countAsBuilt)
  Int siteIndex = SurveySites.findstruct("siteMarker", siteToRemove as ObjectReference, 0) ; #DEBUG_LINE_NO:1749
  If siteIndex > -1 ; #DEBUG_LINE_NO:1750
    SurveySites.remove(siteIndex, 1) ; #DEBUG_LINE_NO:1751
    If countAsBuilt ; #DEBUG_LINE_NO:1752
      siteToRemove.BuildSite() ; #DEBUG_LINE_NO:1754
    EndIf
    Return True ; #DEBUG_LINE_NO:1756
  Else
    Return False ; #DEBUG_LINE_NO:1758
  EndIf
EndFunction

Function SendSurveySiteDiscoveredEvent(ObjectReference newSite)
  Var[] kargs = new Var[1] ; #DEBUG_LINE_NO:1764
  kargs[0] = newSite as Var ; #DEBUG_LINE_NO:1765
  Self.SendCustomEvent("sq_parentscript_SQ_SurveySiteDiscovered", kargs) ; #DEBUG_LINE_NO:1766
EndFunction

Function TestAddSurveySite(ObjectReference newSite)
  Self.AddSurveySite(newSite as surveysitediscovermarkerscript) ; #DEBUG_LINE_NO:1770
EndFunction
