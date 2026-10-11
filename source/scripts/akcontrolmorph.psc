Scriptname AKControlMorph extends ActiveMagicEffect  
; This script allows the player to morph into the target
AKControlQuestScript property akquest Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	akquest.AKMorphComplete(akTarget, akcaster)
EndEvent
