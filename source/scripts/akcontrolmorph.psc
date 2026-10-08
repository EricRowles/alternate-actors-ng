Scriptname AKControlMorph extends ActiveMagicEffect  
;This script allows the player to morph into the target
AKControlQuestScript property akquest Auto
AK__QF_AKControlQuest_01000D92 Property akcontrolquest Auto
bool property duration Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	Actor puppet = akcontrolquest.Alias_ControlTemp.GetReference() as Actor
	If akTarget == puppet || akTarget == akquest.akcontroltemp || akTarget == akquest.akcontroltempmale || akTarget == akquest.akcontroltempFemale || (akquest.MainChar == False && akTarget == akquest.Playerref)
		akquest.akrevert(akcaster)
	Else
		akquest.AKMorphComplete(aktarget, akcaster, duration)
	EndIf
EndEvent
