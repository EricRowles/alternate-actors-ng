Scriptname AKControlPlayerScript extends ReferenceAlias  

AKControlQuestScript Property akquest Auto
AKControlQuest2Script Property akquest2 Auto
AKControlMCMScript Property akquestmcm Auto
Actor Property playerref Auto

Event OnPlayerLoadGame()
	akQuest2.akMaintenance()
EndEvent

Event OnEnterBleedout()
	If akquest.MainChar == False && akquestmcm.RevertOnDeath == True
		playerref.RestoreActorValue("Health", 100.0)
		
		Actor deadNPC = akquest.origionalnpc

		; Return player back to original character safely
		akquest.akrevert(playerref)
		
		; If the original NPC should die on death and is not essential:
		If deadNPC != None
			If !deadNPC.GetActorBase().IsEssential()
				If deadNPC.Is3DLoaded()
					deadNPC.Kill()
				Else
					deadNPC.DamageActorValue("Health", 99999)
				EndIf
			EndIf
		EndIf
		
		Debug.Notification("Controlled NPC fell in combat - returned to body.")
	Else
		; Normal death - remove essential flag so game over / reload screen triggers naturally
		playerref.GetActorBase().SetEssential(False)
		playerref.DamageActorValue("Health", 99999)
	EndIf
EndEvent
