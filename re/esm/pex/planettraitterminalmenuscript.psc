ScriptName PlanetTraitTerminalMenuScript Extends TerminalMenu
{ award planet trait discovery data, optionally set a quest stage }

;-- Variables ---------------------------------------

;-- Properties --------------------------------------
sq_parentscript Property SQ_Parent Auto Const mandatory
{ used to get planet traits }
Int Property DiscoverTraitMenuItemID = 1 Auto Const
{ the ID of the menu item that should discover the planet trait }
Bool Property DiscoverTraitCompletely = True Auto Const
{ TRUE = player fully discovers the trait
    FALSE = player increments discovery as if they had fully explored a trait overlay }
TerminalMenu Property SQ_PlanetTraitTerminalSubmenu Auto Const mandatory
{ autofill }
Int Property DiscoverTraitBodyTextIndex = 0 Auto Const
Int Property NoTraitBodyTextIndex = 1 Auto Const

;-- Functions ---------------------------------------

Event OnTerminalMenuEnter(TerminalMenu akTerminalBase, ObjectReference akTerminalRef)
  planettraitterminalscript myTerminalRef = akTerminalRef as planettraitterminalscript ; #DEBUG_LINE_NO:23
  If myTerminalRef as Bool && myTerminalRef.planetToCheck == None ; #DEBUG_LINE_NO:24
    Form[] textReplacementArray = None ; #DEBUG_LINE_NO:25
    Int bodyTextIndex = NoTraitBodyTextIndex ; #DEBUG_LINE_NO:26
    myTerminalRef.planetToCheck = akTerminalRef.GetCurrentPlanet() ; #DEBUG_LINE_NO:29
    planet planetToCheck = myTerminalRef.planetToCheck ; #DEBUG_LINE_NO:30
    Keyword[] matchingKeywords = planetToCheck.GetKeywordTypeList(SQ_Parent.KeywordType_PlanetTrait) ; #DEBUG_LINE_NO:32
    If matchingKeywords.Length > 0 ; #DEBUG_LINE_NO:34
      Int I = matchingKeywords.Length - 1 ; #DEBUG_LINE_NO:36
      While I > -1 ; #DEBUG_LINE_NO:37
        If planetToCheck.IsTraitKnown(matchingKeywords[I]) ; #DEBUG_LINE_NO:38
          matchingKeywords.remove(I, 1) ; #DEBUG_LINE_NO:39
        EndIf
        I += -1 ; #DEBUG_LINE_NO:41
      EndWhile
      If matchingKeywords.Length == 0 ; #DEBUG_LINE_NO:43
        matchingKeywords = planetToCheck.GetKeywordTypeList(SQ_Parent.KeywordType_PlanetTrait) ; #DEBUG_LINE_NO:45
      EndIf
      Int randomKeyword = Utility.RandomInt(0, matchingKeywords.Length - 1) ; #DEBUG_LINE_NO:47
      myTerminalRef.traitKeywordToDiscover = matchingKeywords[randomKeyword] ; #DEBUG_LINE_NO:48
      textReplacementArray = Self.GetTextReplacementArray(myTerminalRef) ; #DEBUG_LINE_NO:49
      bodyTextIndex = DiscoverTraitBodyTextIndex ; #DEBUG_LINE_NO:50
    EndIf
    SQ_PlanetTraitTerminalSubmenu.ClearDynamicBodyTextItems(myTerminalRef as ObjectReference) ; #DEBUG_LINE_NO:54
    SQ_PlanetTraitTerminalSubmenu.AddDynamicBodyTextItem(myTerminalRef as ObjectReference, bodyTextIndex, 1, textReplacementArray) ; #DEBUG_LINE_NO:55
  EndIf
EndEvent

Event OnTerminalMenuItemRun(Int auiMenuItemID, TerminalMenu akTerminalBase, ObjectReference akTerminalRef)
  planettraitterminalscript myTerminalRef = akTerminalRef as planettraitterminalscript ; #DEBUG_LINE_NO:63
  If myTerminalRef ; #DEBUG_LINE_NO:64
    Keyword traitKeywordToDiscover = myTerminalRef.traitKeywordToDiscover ; #DEBUG_LINE_NO:65
    planet planetToCheck = myTerminalRef.planetToCheck ; #DEBUG_LINE_NO:66
    If (auiMenuItemID == DiscoverTraitMenuItemID && traitKeywordToDiscover as Bool) && myTerminalRef.traitDataAwarded == False && planetToCheck.IsTraitKnown(traitKeywordToDiscover) == False ; #DEBUG_LINE_NO:68
      sq_parentscript:planettraitdata theData = SQ_Parent.FindMatchingPlanetTraitForKeyword(traitKeywordToDiscover, planetToCheck) ; #DEBUG_LINE_NO:69
      myTerminalRef.traitDataAwarded = True ; #DEBUG_LINE_NO:70
      Int traitScansToAward = 1 ; #DEBUG_LINE_NO:71
      If DiscoverTraitCompletely ; #DEBUG_LINE_NO:72
        traitScansToAward = 99 ; #DEBUG_LINE_NO:73
      EndIf
      SQ_Parent.UpdatePlanetTraitDiscovery(akTerminalRef, theData, traitScansToAward) ; #DEBUG_LINE_NO:75
    EndIf
  EndIf
EndEvent

Form[] Function GetTextReplacementArray(planettraitterminalscript myTerminalRef)
  Form[] textReplacementArray = new Form[0] ; #DEBUG_LINE_NO:82
  textReplacementArray.add(myTerminalRef.traitKeywordToDiscover as Form, 1) ; #DEBUG_LINE_NO:87
  Return textReplacementArray ; #DEBUG_LINE_NO:89
EndFunction
