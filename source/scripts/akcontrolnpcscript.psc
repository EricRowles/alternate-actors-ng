Scriptname AKControlNPCScript extends ReferenceAlias  

AKControlMCMScript property akquestMCM auto Hidden
AKControlQuestScript property akquest auto Hidden
AKControlQuest2Script property akquest2 auto Hidden

Event OnDeath(Actor akKiller)
	Actor player = Game.GetPlayer()
	If player
		player.GetActorBase().SetEssential(False)
		player.DamageActorValue("Health", 99999)
	EndIf
EndEvent
