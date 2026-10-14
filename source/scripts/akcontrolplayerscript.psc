Scriptname AKControlPlayerScript extends ReferenceAlias  

AKControlQuestScript Property akquest Auto
AKControlMCMScript Property akquestmcm Auto

Event OnPlayerLoadGame()
	If akquestmcm
		akquestmcm.RefreshConfig()
	EndIf
	If akquest
		akquest.akmaintenance()
	EndIf
EndEvent

