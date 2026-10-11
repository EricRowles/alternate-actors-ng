Scriptname AKControlNPCScript extends ReferenceAlias  

AKControlQuestScript property akquest Auto

Actor PlayerPuppetRef

Event OnActivate(ObjectReference akActionRef)
	PlayerPuppetRef = akquest.Alias_PlayerPuppet.GetActorReference()
	If akActionRef == PlayerPuppetRef
		AK_NG.OpenSelfDialogue(PlayerPuppetRef)
	EndIf
EndEvent
