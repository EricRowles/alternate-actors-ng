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