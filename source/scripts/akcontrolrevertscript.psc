Scriptname AKControlRevertScript extends ActiveMagicEffect  
;This script allows the player to revert to their origional form
AKControlQuestScript property akquest Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	akquest.akrevert(akcaster)
EndEvent

