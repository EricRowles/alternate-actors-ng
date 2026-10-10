Scriptname AKControlStopCombatScript extends ActiveMagicEffect  

AKControlQuestScript Property akquest Auto
AKControlMCMScript Property akquestMCM Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	If akTarget.GetCombatTarget() == akquest.playerref
		akTarget.AddToFaction(akquest.CharmFaction)
	ElseIf akTarget.GetCombatTarget() == akquest.origionalNPC || akTarget.GetCombatTarget() == akquest.akcontroltemp
		akTarget.AddToFaction(akquest.CharmFaction)
	EndIf
	
	akTarget.StopCombat()		
EndEvent
