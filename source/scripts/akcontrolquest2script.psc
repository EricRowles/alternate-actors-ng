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
	If akquest.akmodversion < 2.0
		akquest.akmodversion = 2.0
		Debug.Notification("Alternate Actors NG initialized.")
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
		If akquest.akcontroltemp
			AK_NG.SetPuppetActor(akquest.akcontroltemp)
		EndIf
		If akquest.origionalnpc
			AK_NG.CopyAppearance(akquest.origionalnpc, playerref)
		EndIf
	EndIf
EndFunction
