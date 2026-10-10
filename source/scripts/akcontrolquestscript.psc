Scriptname AKControlQuestScript extends Quest  

AKControlQuest2Script property akquest2 Auto
AKControlMCMScript property akquestmcm Auto

float property akmodversion = 2.0 Auto

ObjectReference property AKControlCloneMarker auto		; where to hide the nonused npcs
ObjectReference property AKControlCloneMarkerTemp auto	; temp use to mark where an NPC is

spell property akcontrolrevertspell auto
Spell Property akcontrolmorphspell Auto

actor property akcontroltemp auto 			; used by spells to agree on the temp actor
actor property OrigionalNPC Auto 			; used by spells to move around the origional Actor
actor property akcontroltempmale auto		; male dragonborn puppet
actor property akcontroltempFemale auto		; female dragonborn puppet
actor Property Playerref Auto				; the player!

bool property MainChar = True Auto			; is the player playing the MainChar or someone else?

ReferenceAlias Function GetControlTempAlias()
	Return GetAliasByName("ControlTemp") as ReferenceAlias
EndFunction

; Morph Complete Function -----------------------------------------------------------------------------------
function AKMorphComplete(Actor akdonor, Actor akimposter, Bool duration = False, Float aktime = 0.0)
	ReferenceAlias controlTempAlias = GetControlTempAlias()
	Actor puppetActor = None
	If controlTempAlias
		puppetActor = controlTempAlias.GetReference() as Actor
	EndIf
	If akdonor == akcontroltemp || akdonor == akcontroltempmale || akdonor == akcontroltempFemale || (puppetActor && akdonor == puppetActor)
		akrevert(akimposter)
		return
	EndIf

	; Capture original player name before any appearance or trait cloning occurs
	String origPlayerName = AK_NG.GetOriginalPlayerName()
	If origPlayerName == ""
		origPlayerName = playerref.GetActorBase().GetName()
	EndIf

	akimposter.StopCombat()
	akdonor.StopCombat()
	
	Game.FadeOutGame(true, true, 0.0, 0.3)
	
	; Dynamically select male or female puppet based on the player's gender
	if AK_NG.GetActorSex(playerref) == 0
		akcontroltemp = akcontroltempmale	
	Else
		akcontroltemp = akcontroltempFemale
	EndIf
	If controlTempAlias
		controlTempAlias.ForceRefTo(akcontroltemp)
	EndIf
	AK_NG.SetPuppetActor(akcontroltemp)

	; Transfer traits from PC back to orignpc before getting new traits if we were already controlling an NPC
	If MainChar == False
		AK_NG.EndImpersonation()
		origionalnpc.moveto(akdonor)
		Utility.Wait(0.1)
		AK_NG.RestoreInventoryFromPlayer(playerref, origionalnpc, None)
		AK_NG.CopyStats(playerref, origionalnpc)
	Else
		akcontroltemp.moveto(akdonor)
		Utility.Wait(0.1) ; Allow 3D node attachment in loaded cell before copying appearance
	EndIf
	
	origionalnpc = akdonor
	
	If mainchar == True
		AK_NG.CopyAppearance(playerref, akcontroltemp)
		AK_NG.CopyStats(playerref, akcontroltemp)			
	EndIf
	
	; Make the player look and have stats like the donor NPC
	AK_NG.CloneAppearance(akdonor, playerref)
	AK_NG.TransferStats(akdonor, playerref)

	; Native Faction Transfer
	AK_NG.TransferFactions(akdonor, playerref)

	; Native Inventory Transfer
	If MainChar == True
		AK_NG.TransferInventoryToPlayer(playerref, akdonor, akcontroltemp)
	Else
		AK_NG.TransferInventoryToPlayer(playerref, akdonor, None)
	EndIf

	origionalnpc.moveto(AKControlCloneMarker)
	akcontroltemp.SetDisplayName(origPlayerName, True)
	akcontroltemp.BlockActivation(True)

	Game.FadeOutGame(false, true, 0.0, 0.4)
	
	if akquestmcm.revertondeath == True
		akimposter.GetActorBase().SetEssential(True)
	EndIf
	
	
	playerref.addspell(akcontrolrevertspell, False) 
	
	MainChar = False
	AK_NG.SetControllingNPC(True)
	AK_NG.BeginImpersonation(akdonor, playerref)
	RegisterForSingleUpdate(1.0)

	Debug.Notification("Control Swapped")
EndFunction

; Revert Function -----------------------------------------------------------------------------------
function akrevert(actor akimposter, Bool AKDrainmagic = False)
	if MainChar == False
		AK_NG.SetControllingNPC(False)
		AK_NG.EndImpersonation()
		UnregisterForUpdate()
		
		Game.FadeOutGame(true, true, 0.0, 0.3)
		
		akcontrolclonemarkertemp.MoveTo(akimposter)
		akimposter.moveto(akcontroltemp)
		
		origionalnpc.moveto(akimposter)
		
		akimposter.StopCombat()
		If origionalnpc
			origionalnpc.StopCombat()
		EndIf
		akimposter.RemoveSpell(akcontrolrevertspell)
		
		AK_NG.CopyStats(playerref, origionalnpc)
		AK_NG.RevertAppearance(playerref)
		AK_NG.RestoreStats(playerref)
		AK_NG.RestoreInventoryFromPlayer(playerref, origionalnpc, akcontroltemp)
		AK_NG.RestoreFactions(playerref)
		
		ReferenceAlias controlTempAlias = GetControlTempAlias()
		If controlTempAlias
			controlTempAlias.Clear()
		EndIf

		akcontroltemp.BlockActivation(False)
		akcontroltemp.SetDisplayName("")
		AK_NG.SetPuppetActor(None)
		akcontroltemp.moveto(akcontrolclonemarker)
		akcontrolclonemarkertemp.moveto(akcontrolclonemarker)
		origionalnpc = None
		akcontroltemp = None

		playerref.GetActorBase().SetEssential(False)
		MainChar = True

		Game.FadeOutGame(false, true, 0.0, 0.4)
		
		Debug.Notification("Returned to Body")
	EndIf
EndFunction

Event OnUpdate()
	If MainChar == False
		AK_NG.UpdateIdleAI()
		RegisterForSingleUpdate(1.0)
	EndIf
EndEvent
