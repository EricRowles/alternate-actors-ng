Scriptname AKControlMorph extends ActiveMagicEffect  
; This script allows the player to morph into the target
AKControlQuestScript property akquest Auto
bool property duration Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	If !akquest
		Return
	EndIf

	Actor puppet = None
	ReferenceAlias puppetAlias = akquest.GetControlTempAlias()
	If puppetAlias
		puppet = puppetAlias.GetReference() as Actor
	EndIf
	If !puppet
		puppet = akquest.akcontroltemp
	EndIf

	If akTarget == puppet || akTarget == akquest.akcontroltempmale || akTarget == akquest.akcontroltempFemale || (!akquest.MainChar && akTarget == akquest.Playerref)
		akquest.akrevert(akcaster)
	Else
		akquest.AKMorphComplete(akTarget, akcaster, duration)
	EndIf
EndEvent
