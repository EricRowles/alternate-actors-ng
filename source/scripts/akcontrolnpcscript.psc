Scriptname AKControlNPCScript extends ReferenceAlias  


Event OnDeath(Actor akKiller)
	Actor player = Game.GetPlayer()
	If player
		player.GetActorBase().SetEssential(False)
		player.DamageActorValue("Health", 99999)
	EndIf
EndEvent

Event OnActivate(ObjectReference akActionRef)
	If akActionRef == Game.GetPlayer()
		Actor puppet = GetReference() as Actor
		AK_NG.OpenSelfDialogue(puppet)
	EndIf
EndEvent
