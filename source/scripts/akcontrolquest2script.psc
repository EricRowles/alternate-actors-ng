Scriptname AKControlQuest2Script extends Quest
; 2nd quest script - handles maintenance and load screens

AKControlQuestScript property akquest Auto
AKControlMCMScript property akquestmcm Auto
Actor Property playerref Auto

int property akloadscreen = -1 Auto ; -1 == false, 1 == on

; Initiation Function -----------------------------------------------------------------------------------
Event OnInit()
	akmaintenance()
EndEvent

; Maintenance Function -----------------------------------------------------------------------------------
function akmaintenance()
	If akquest.akmodversion < 1.2
		if akquest.akmodversion == 1.1 || akquest.akmodversion == 1.0
			Debug.MessageBox("Revert, then make a clean save to start Alternate Actors!")
			Return
		Else
			akquest.akmodversion = 1.2
			debug.Notification("Welcome to Alternate Actors " + akquest.akmodversion + "!")	
		EndIf
	EndIf

	If akquestmcm.LoreFriendly == False
		If !playerref.HasSpell(akquest.akcontrolmorphspell)
			playerref.AddSpell(akquest.akcontrolmorphspell, false)
		EndIf
	EndIf

	If akquest.MainChar == True && playerref.HasSpell(akquest.akcontrolrevertspell)
		playerref.RemoveSpell(akquest.akcontrolrevertspell)
	EndIf

	; Synchronize SKSE Native settings from MCM properties on game load
	AK_NG.SetIdleAIEnabled(akquestmcm.IdleAIEnabled)
	AK_NG.SetIdleAITimeout(akquestmcm.IdleAITimeout)
	AK_NG.SetDialogueHotkey(akquestmcm.DialogueHotkey)
	AK_NG.SetImpersonationOptions(akquestmcm.ImpersonateConditions, akquestmcm.ImpersonateAliases, akquestmcm.ImpersonatePackages, akquestmcm.ImpersonateScripts)

	If akquest.MainChar == False
		AK_NG.SetControllingNPC(True)
	EndIf
EndFunction

; Console / RaceMenu Functions -----------------------------------------------------------------------------------
Event OnMenuOpen(String MenuName)
	if menuname == "Console"
		utility.WaitMenuMode(0.5)
		input.tapkey(25) ;p
		utility.WaitMenuMode(0.1)
		input.tapkey(38) ;l
		utility.WaitMenuMode(0.1)
		input.tapkey(30) ;a
		utility.WaitMenuMode(0.1)
		input.tapkey(21) ;y
		utility.WaitMenuMode(0.1)
		input.tapkey(18) ;e
		utility.WaitMenuMode(0.1)
		input.tapkey(19) ;r
		utility.WaitMenuMode(0.1)
		input.tapkey(52) ;.
		utility.WaitMenuMode(0.1)
		input.tapkey(31) ;s
		utility.WaitMenuMode(0.1)
		input.tapkey(18) ;e
		utility.WaitMenuMode(0.1)
		input.tapkey(45) ;x
		utility.WaitMenuMode(0.1)
		input.tapkey(46) ;c
		utility.WaitMenuMode(0.1)
		input.tapkey(35) ;h
		utility.WaitMenuMode(0.1)
		input.tapkey(30) ;a
		utility.WaitMenuMode(0.1)
		input.tapkey(49) ;n
		utility.WaitMenuMode(0.1)
		input.tapkey(34) ;g
		utility.WaitMenuMode(0.1)
		input.tapkey(18) ;e
		utility.WaitMenuMode(0.1)
		input.tapkey(28) ;enter
		utility.WaitMenuMode(0.1)
		UnRegisterForMenu("Console")
		input.tapkey(input.getmappedkey("Console", 0))
	elseif menuname == ("RaceSex Menu")
		if akquest.inprogress == 1
			if input.getmappedkey("Ready Weapon") == 278  ;player is using controller		
				If akquestmcm.Debugmode == False
					akloadscreenOFF()
				EndIf
			EndIf
			While akquest.inracemenu == True ;used to loop, make keyboard proof
				utility.WaitMenuMode(0.5)
				input.tapkey(19) ;r (to finish character Creation, go to name menu)
				utility.WaitMenuMode(0.2)
				input.tapkey(28) ;enter (Finish and save character? Yes Please!)
				utility.WaitMenuMode(0.2)
				input.tapkey(30) ;a (name character)
				utility.WaitMenuMode(0.1)
				input.tapkey(28) ;enter (save name and finish racemenu)
				utility.WaitMenuMode(0.5)
			EndWhile
			if akquestmcm.Debugmode == False && input.getmappedkey("Ready Weapon") == 278 ;toggle menus back off so script can turn them back on.
				debug.ToggleMenus()
			EndIf
		EndIf
	EndIf
EndEvent

Event OnMenuClose(String MenuName)
	if menuname == ("RaceSex Menu") 
		if akquest.inracemenu == True
			akquest.inracemenu = False
		EndIf
	EndIf
EndEvent

Function AKLoadScreenON()
	akloadscreen = 1
	ImageSpaceModifier fadeImod = Game.GetForm(0x000F756D) as ImageSpaceModifier
	ImageSpaceModifier holdImod = Game.GetForm(0x000F756E) as ImageSpaceModifier
	If fadeImod
		fadeImod.Apply(1.0)
	EndIf
	Game.FadeOutGame(true, true, 0.0, 0.3)
	Utility.Wait(0.35)
	If holdImod
		holdImod.Apply(1.0)
	EndIf
EndFunction

Function AKLoadScreenOFF()
	Utility.Wait(0.6)
	ImageSpaceModifier holdImod = Game.GetForm(0x000F756E) as ImageSpaceModifier
	ImageSpaceModifier backImod = Game.GetForm(0x000F756F) as ImageSpaceModifier
	If holdImod
		holdImod.Remove()
	EndIf
	If backImod
		backImod.Apply(1.0)
	EndIf
	Game.FadeOutGame(false, true, 0.0, 0.4)
	Utility.Wait(0.45)
	akloadscreen = -1
EndFunction