ScriptName Fragments:Quests:QF_PlayerSkills_002C59E4 Extends Quest Const hidden

;-- Variables ---------------------------------------

;-- Properties --------------------------------------
ActorValue Property Health Auto Const mandatory
ActorValue Property CarryWeight Auto Const mandatory
ActorValue Property Oxygen Auto Const mandatory
Int Property WellnessBonus Auto Const mandatory
Int Property FitnessBonus Auto Const mandatory
Int Property WeightLiftingBonus Auto Const mandatory
Int Property BotanyBonus Auto Const mandatory
Int Property GeologyBonus Auto Const mandatory
ActorValue Property ScanningPowerLevel Auto Const mandatory
ActorValue Property ZoologyRank Auto Const mandatory
ActorValue Property BotanyRank Auto Const mandatory
GlobalVariable Property Outpost_BuildLimit_CrewStations Auto Const
Int Property OutpostManagementBonus = 1 Auto Const
GlobalVariable Property Outpost_BuildLimit_CargoLinks Auto Const
Int Property OutpostManagementCargoLinksBonus Auto Const
ActorValue Property OutpostMaxDeployed Auto Const
Int Property OutpostMaxDeployedBonus = 20 Auto Const
ActorValue Property PayloadLevel Auto Const
ActorValue Property SecurityMenuRingHighlightingEnabled Auto Const mandatory
ActorValue Property SecurityMenuDisableUnusedKeysOptionEnabled Auto Const mandatory
ActorValue Property SecurityMenuAutoattemptPointsMultiplier Auto Const mandatory
ActorValue Property SecurityMenuMaxAutoattemptPoints Auto Const mandatory
GlobalVariable Property Outpost_BuildLimit_Robots Auto Const
ActorValue Property GalaxyBodyScanAbility Auto Const mandatory
ActorValue Property SurveyingPowerLevel Auto Const mandatory
ActorValue Property SurveyingTraitBonus Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeHighImportance_Cheap Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeHighImportance_Expensive Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeLowImportance_Cheap Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeLowImportance_Expensive Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeHighImportance_CheapBase Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeHighImportance_ExpensiveBase Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeLowImportance_CheapBase Auto Const mandatory
GlobalVariable Property SpeechChallengeBribeLowImportance_ExpensiveBase Auto Const mandatory
GlobalVariable Property FloraPlantRareMin Auto Const mandatory
GlobalVariable Property FloraMineralRareMin Auto Const mandatory
GlobalVariable Property Ships_Piracy_Low Auto Const mandatory
GlobalVariable Property Ships_Piracy_High Auto Const mandatory
GlobalVariable Property Ships_Piracy_High_Base Auto Const mandatory
GlobalVariable Property Ships_Piracy_Low_Base Auto Const mandatory
GlobalVariable Property Skill_Astrophysics_DiscoverTraitChance Auto Const mandatory

;-- Functions ---------------------------------------

Function Fragment_Stage_0501_Item_00()
  ; Empty function
EndFunction

Function Fragment_Stage_0502_Item_00()
  ; Empty function
EndFunction

Function Fragment_Stage_0503_Item_00()
  ; Empty function
EndFunction

Function Fragment_Stage_0201_Item_00()
  Game.GetPlayer().SetValue(SecurityMenuMaxAutoattemptPoints, Game.GetPlayer().GetValue(SecurityMenuMaxAutoattemptPoints) + 1.0) ; #DEBUG_LINE_NO:8
EndFunction

Function Fragment_Stage_0202_Item_00()
  Self.SetStage(201) ; #DEBUG_LINE_NO:17
  Actor player = Game.GetPlayer() ; #DEBUG_LINE_NO:19
  player.SetValue(SecurityMenuRingHighlightingEnabled, 1.0) ; #DEBUG_LINE_NO:20
  player.SetValue(SecurityMenuMaxAutoattemptPoints, Game.GetPlayer().GetValue(SecurityMenuMaxAutoattemptPoints) + 1.0) ; #DEBUG_LINE_NO:22
EndFunction

Function Fragment_Stage_0203_Item_00()
  Self.SetStage(202) ; #DEBUG_LINE_NO:31
  Actor player = Game.GetPlayer() ; #DEBUG_LINE_NO:33
  player.SetValue(SecurityMenuMaxAutoattemptPoints, Game.GetPlayer().GetValue(SecurityMenuMaxAutoattemptPoints) + 1.0) ; #DEBUG_LINE_NO:36
EndFunction

Function Fragment_Stage_0204_Item_00()
  Self.SetStage(203) ; #DEBUG_LINE_NO:45
  Actor player = Game.GetPlayer() ; #DEBUG_LINE_NO:47
  player.SetValue(SecurityMenuAutoattemptPointsMultiplier, 2.0) ; #DEBUG_LINE_NO:48
  player.SetValue(SecurityMenuDisableUnusedKeysOptionEnabled, 1.0) ; #DEBUG_LINE_NO:49
  player.SetValue(SecurityMenuMaxAutoattemptPoints, Game.GetPlayer().GetValue(SecurityMenuMaxAutoattemptPoints) + 1.0) ; #DEBUG_LINE_NO:51
EndFunction

Function Fragment_Stage_0301_Item_00()
  Game.GetPlayer().ModValue(Health, WellnessBonus as Float) ; #DEBUG_LINE_NO:59
EndFunction

Function Fragment_Stage_0302_Item_00()
  Game.GetPlayer().ModValue(Health, WellnessBonus as Float) ; #DEBUG_LINE_NO:67
EndFunction

Function Fragment_Stage_0303_Item_00()
  Game.GetPlayer().ModValue(Health, WellnessBonus as Float) ; #DEBUG_LINE_NO:75
EndFunction

Function Fragment_Stage_0304_Item_00()
  Game.GetPlayer().ModValue(Health, (WellnessBonus * 2) as Float) ; #DEBUG_LINE_NO:83
EndFunction

Function Fragment_Stage_0401_Item_00()
  Game.GetPlayer().SetValue(BotanyRank, 1.0) ; #DEBUG_LINE_NO:91
EndFunction

Function Fragment_Stage_0402_Item_00()
  Game.GetPlayer().SetValue(BotanyRank, 2.0) ; #DEBUG_LINE_NO:99
EndFunction

Function Fragment_Stage_0403_Item_00()
  Game.GetPlayer().SetValue(BotanyRank, 3.0) ; #DEBUG_LINE_NO:107
EndFunction

Function Fragment_Stage_0404_Item_00()
  Game.GetPlayer().SetValue(BotanyRank, 4.0) ; #DEBUG_LINE_NO:115
EndFunction

Function Fragment_Stage_0601_Item_00()
  Game.GetPlayer().SetValue(ScanningPowerLevel, 1.0) ; #DEBUG_LINE_NO:147
EndFunction

Function Fragment_Stage_0602_Item_00()
  Game.GetPlayer().SetValue(ScanningPowerLevel, 2.0) ; #DEBUG_LINE_NO:155
EndFunction

Function Fragment_Stage_0603_Item_00()
  Game.GetPlayer().SetValue(ScanningPowerLevel, 3.0) ; #DEBUG_LINE_NO:163
EndFunction

Function Fragment_Stage_0604_Item_00()
  Game.GetPlayer().SetValue(ScanningPowerLevel, 4.0) ; #DEBUG_LINE_NO:171
EndFunction

Function Fragment_Stage_0701_Item_00()
  Outpost_BuildLimit_CargoLinks.Mod(OutpostManagementCargoLinksBonus as Float) ; #DEBUG_LINE_NO:179
EndFunction

Function Fragment_Stage_0702_Item_00()
  Outpost_BuildLimit_Robots.Mod(OutpostManagementBonus as Float) ; #DEBUG_LINE_NO:187
EndFunction

Function Fragment_Stage_0703_Item_00()
  Outpost_BuildLimit_CrewStations.Mod(OutpostManagementBonus as Float) ; #DEBUG_LINE_NO:195
EndFunction

Function Fragment_Stage_0801_Item_00()
  Game.GetPlayer().SetValue(ZoologyRank, 1.0) ; #DEBUG_LINE_NO:203
EndFunction

Function Fragment_Stage_0802_Item_00()
  Game.GetPlayer().SetValue(ZoologyRank, 2.0) ; #DEBUG_LINE_NO:211
EndFunction

Function Fragment_Stage_0803_Item_00()
  Game.GetPlayer().SetValue(ZoologyRank, 3.0) ; #DEBUG_LINE_NO:219
EndFunction

Function Fragment_Stage_0804_Item_00()
  Game.GetPlayer().SetValue(ZoologyRank, 4.0) ; #DEBUG_LINE_NO:227
EndFunction

Function Fragment_Stage_0901_Item_00()
  Game.GetPlayer().SetValue(OutpostMaxDeployed, 12.0) ; #DEBUG_LINE_NO:235
EndFunction

Function Fragment_Stage_0902_Item_00()
  Game.GetPlayer().SetValue(OutpostMaxDeployed, 16.0) ; #DEBUG_LINE_NO:243
EndFunction

Function Fragment_Stage_0903_Item_00()
  Game.GetPlayer().SetValue(OutpostMaxDeployed, 20.0) ; #DEBUG_LINE_NO:251
EndFunction

Function Fragment_Stage_0904_Item_00()
  Game.GetPlayer().SetValue(OutpostMaxDeployed, 24.0) ; #DEBUG_LINE_NO:259
EndFunction

Function Fragment_Stage_1101_Item_00()
  Game.GetPlayer().SetValue(GalaxyBodyScanAbility, 1.0) ; #DEBUG_LINE_NO:267
  Skill_Astrophysics_DiscoverTraitChance.SetValueInt(10) ; #DEBUG_LINE_NO:268
EndFunction

Function Fragment_Stage_1102_Item_00()
  Game.GetPlayer().SetValue(GalaxyBodyScanAbility, 2.0) ; #DEBUG_LINE_NO:276
  Skill_Astrophysics_DiscoverTraitChance.SetValueInt(20) ; #DEBUG_LINE_NO:277
EndFunction

Function Fragment_Stage_1103_Item_00()
  Game.GetPlayer().SetValue(GalaxyBodyScanAbility, 3.0) ; #DEBUG_LINE_NO:285
  Skill_Astrophysics_DiscoverTraitChance.SetValueInt(30) ; #DEBUG_LINE_NO:286
EndFunction

Function Fragment_Stage_1104_Item_00()
  Game.GetPlayer().SetValue(GalaxyBodyScanAbility, 3.0) ; #DEBUG_LINE_NO:294
  Skill_Astrophysics_DiscoverTraitChance.SetValueInt(50) ; #DEBUG_LINE_NO:295
EndFunction

Function Fragment_Stage_1201_Item_00()
  Game.GetPlayer().SetValue(SurveyingPowerLevel, 1.0) ; #DEBUG_LINE_NO:303
EndFunction

Function Fragment_Stage_1202_Item_00()
  Game.GetPlayer().SetValue(SurveyingPowerLevel, 2.0) ; #DEBUG_LINE_NO:311
EndFunction

Function Fragment_Stage_1203_Item_00()
  Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:319
  playerRef.SetValue(SurveyingPowerLevel, 3.0) ; #DEBUG_LINE_NO:320
  playerRef.SetValue(SurveyingTraitBonus, 1.0) ; #DEBUG_LINE_NO:321
EndFunction

Function Fragment_Stage_1204_Item_00()
  Actor playerRef = Game.GetPlayer() ; #DEBUG_LINE_NO:329
  playerRef.SetValue(SurveyingPowerLevel, 4.0) ; #DEBUG_LINE_NO:330
  playerRef.SetValue(SurveyingTraitBonus, 2.0) ; #DEBUG_LINE_NO:331
EndFunction

Function Fragment_Stage_1302_Item_00()
  Float bribeLowExpensive = SpeechChallengeBribeLowImportance_ExpensiveBase.GetValue() ; #DEBUG_LINE_NO:339
  Float bribeHighExpensive = SpeechChallengeBribeHighImportance_ExpensiveBase.GetValue() ; #DEBUG_LINE_NO:340
  Float bribeLowCheap = SpeechChallengeBribeLowImportance_CheapBase.GetValue() ; #DEBUG_LINE_NO:341
  Float bribeHighCheap = SpeechChallengeBribeHighImportance_CheapBase.GetValue() ; #DEBUG_LINE_NO:342
  Float costMult = 0.75 ; #DEBUG_LINE_NO:343
  SpeechChallengeBribeLowImportance_Expensive.SetValueInt((bribeLowExpensive * costMult) as Int) ; #DEBUG_LINE_NO:345
  SpeechChallengeBribeHighImportance_Expensive.SetValueInt((bribeHighExpensive * costMult) as Int) ; #DEBUG_LINE_NO:346
  SpeechChallengeBribeLowImportance_Cheap.SetValueInt((bribeLowCheap * costMult) as Int) ; #DEBUG_LINE_NO:347
  SpeechChallengeBribeHighImportance_Cheap.SetValueInt((bribeHighCheap * costMult) as Int) ; #DEBUG_LINE_NO:348
EndFunction

Function Fragment_Stage_1303_Item_00()
  Float bribeLowExpensive = SpeechChallengeBribeLowImportance_ExpensiveBase.GetValue() ; #DEBUG_LINE_NO:356
  Float bribeHighExpensive = SpeechChallengeBribeHighImportance_ExpensiveBase.GetValue() ; #DEBUG_LINE_NO:357
  Float bribeLowCheap = SpeechChallengeBribeLowImportance_CheapBase.GetValue() ; #DEBUG_LINE_NO:358
  Float bribeHighCheap = SpeechChallengeBribeHighImportance_CheapBase.GetValue() ; #DEBUG_LINE_NO:359
  Float costMult = 0.5 ; #DEBUG_LINE_NO:360
  SpeechChallengeBribeLowImportance_Expensive.SetValueInt((bribeLowExpensive * costMult) as Int) ; #DEBUG_LINE_NO:362
  SpeechChallengeBribeHighImportance_Expensive.SetValueInt((bribeHighExpensive * costMult) as Int) ; #DEBUG_LINE_NO:363
  SpeechChallengeBribeLowImportance_Cheap.SetValueInt((bribeLowCheap * costMult) as Int) ; #DEBUG_LINE_NO:364
  SpeechChallengeBribeHighImportance_Cheap.SetValueInt((bribeHighCheap * costMult) as Int) ; #DEBUG_LINE_NO:365
EndFunction

Function Fragment_Stage_1401_Item_00()
  Game.GetPlayer().SetValue(PayloadLevel, 1.0) ; #DEBUG_LINE_NO:373
  Float piracyLow = Ships_Piracy_Low_Base.GetValue() ; #DEBUG_LINE_NO:375
  Float piracyHigh = Ships_Piracy_High_Base.GetValue() ; #DEBUG_LINE_NO:376
  Float piracyMult = 1.100000024 ; #DEBUG_LINE_NO:379
  Float piracyLowNew = piracyLow * piracyMult ; #DEBUG_LINE_NO:381
  Ships_Piracy_Low.SetValue(piracyLowNew) ; #DEBUG_LINE_NO:382
  Float piracyHighNew = piracyHigh * piracyMult ; #DEBUG_LINE_NO:384
  Ships_Piracy_High.SetValue(piracyHighNew) ; #DEBUG_LINE_NO:385
EndFunction

Function Fragment_Stage_1402_Item_00()
  Game.GetPlayer().SetValue(PayloadLevel, 2.0) ; #DEBUG_LINE_NO:393
  Float piracyLow = Ships_Piracy_Low_Base.GetValue() ; #DEBUG_LINE_NO:395
  Float piracyHigh = Ships_Piracy_High_Base.GetValue() ; #DEBUG_LINE_NO:396
  Float piracyMult = 1.200000048 ; #DEBUG_LINE_NO:399
  Float piracyLowNew = piracyLow * piracyMult ; #DEBUG_LINE_NO:401
  Ships_Piracy_Low.SetValue(piracyLowNew) ; #DEBUG_LINE_NO:402
  Float piracyHighNew = piracyHigh * piracyMult ; #DEBUG_LINE_NO:404
  Ships_Piracy_High.SetValue(piracyHighNew) ; #DEBUG_LINE_NO:405
EndFunction

Function Fragment_Stage_1403_Item_00()
  Game.GetPlayer().SetValue(PayloadLevel, 3.0) ; #DEBUG_LINE_NO:413
  Float piracyLow = Ships_Piracy_Low_Base.GetValue() ; #DEBUG_LINE_NO:415
  Float piracyHigh = Ships_Piracy_High_Base.GetValue() ; #DEBUG_LINE_NO:416
  Float piracyMult = 1.299999952 ; #DEBUG_LINE_NO:419
  Float piracyLowNew = piracyLow * piracyMult ; #DEBUG_LINE_NO:421
  Ships_Piracy_Low.SetValue(piracyLowNew) ; #DEBUG_LINE_NO:422
  Float piracyHighNew = piracyHigh * piracyMult ; #DEBUG_LINE_NO:424
  Ships_Piracy_High.SetValue(piracyHighNew) ; #DEBUG_LINE_NO:425
EndFunction

Function Fragment_Stage_1404_Item_00()
  Game.GetPlayer().SetValue(PayloadLevel, 4.0) ; #DEBUG_LINE_NO:433
  Float piracyLow = Ships_Piracy_Low_Base.GetValue() ; #DEBUG_LINE_NO:435
  Float piracyHigh = Ships_Piracy_High_Base.GetValue() ; #DEBUG_LINE_NO:436
  Float piracyMult = 1.5 ; #DEBUG_LINE_NO:439
  Float piracyLowNew = piracyLow * piracyMult ; #DEBUG_LINE_NO:441
  Ships_Piracy_Low.SetValue(piracyLowNew) ; #DEBUG_LINE_NO:442
  Float piracyHighNew = piracyHigh * piracyMult ; #DEBUG_LINE_NO:444
  Ships_Piracy_High.SetValue(piracyHighNew) ; #DEBUG_LINE_NO:445
EndFunction
