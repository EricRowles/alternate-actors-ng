Scriptname AKControlQuest2Script extends Quest
; 2nd quest script - handles maintenance and load screens

AKControlQuestScript property akquest Auto
AKControlMCMScript property akquestmcm Auto
Actor Property playerref Auto

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

	If !playerref.HasSpell(akquest.akcontrolmorphspell)
		playerref.AddSpell(akquest.akcontrolmorphspell, false)
	EndIf

	If akquest.MainChar == True && playerref.HasSpell(akquest.akcontrolrevertspell)
		playerref.RemoveSpell(akquest.akcontrolrevertspell)
	EndIf

	; Synchronize SKSE Native settings from MCM properties on game load
	AK_NG.SetIdleAIEnabled(akquestmcm.IdleAIEnabled)
	AK_NG.SetIdleAITimeout(akquestmcm.IdleAITimeout)
	AK_NG.SetImpersonationOptions(akquestmcm.ImpersonateConditions, akquestmcm.ImpersonateAliases, akquestmcm.ImpersonatePackages, akquestmcm.ImpersonateScripts)

	If akquest.MainChar == False
		AK_NG.SetControllingNPC(True)
	EndIf
EndFunction
