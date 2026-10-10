Scriptname AKControlMorph extends ActiveMagicEffect  
;This script allows the player to morph into the target
AKControlQuestScript property akquest Auto
AK__QF_AKControlQuest_01000D92 Property akcontrolquest Auto
bool property duration Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	Actor puppet = None
	If akcontrolquest
		puppet = akcontrolquest.Alias_ControlTemp.GetReference() as Actor
	EndIf
	If !puppet && akquest
		puppet = akquest.akcontroltemp
	EndIf
	If akTarget == puppet || (akquest && (akTarget == akquest.akcontroltemp || akTarget == akquest.akcontroltempmale || akTarget == akquest.akcontroltempFemale || (akquest.MainChar == False && akTarget == akquest.Playerref)))
		If akquest
			akquest.akrevert(akcaster)
		EndIf
	Else
		If akquest
			akquest.AKMorphComplete(aktarget, akcaster, duration)
		EndIf
	EndIf
EndEvent
