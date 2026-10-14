Scriptname AKControlNPCScript extends ReferenceAlias  

AKControlQuestScript property akquest Auto

Actor PlayerPuppetRef

Event OnActivate(ObjectReference akActionRef)
	Debug.Trace("AKControlNPCScript: OnActivate triggered by " + akActionRef)
	Actor player = Game.GetPlayer()
	If akActionRef == player || (akquest && akActionRef == akquest.PlayerRef)
		PlayerPuppetRef = GetActorReference()
		If !PlayerPuppetRef && akquest && akquest.Alias_PlayerPuppet
			PlayerPuppetRef = akquest.Alias_PlayerPuppet.GetActorReference()
		EndIf
		Debug.Trace("AKControlNPCScript: Opening self-dialogue with puppet: " + PlayerPuppetRef)
		Bool opened = AK_NG.OpenSelfDialogue(PlayerPuppetRef)
		Debug.Trace("AKControlNPCScript: OpenSelfDialogue returned " + opened)
	EndIf
EndEvent
