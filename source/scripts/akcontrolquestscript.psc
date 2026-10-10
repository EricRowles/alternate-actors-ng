Scriptname AKControlQuestScript extends Quest  

AK__QF_AKControlQuest_01000D92 Property akquestfrag Auto
AKControlQuest2Script property akquest2 Auto
AKControlMCMScript property akquestmcm Auto

float property akmodversion Auto
float property akstarttime Auto
float property akdurationtime Auto

ObjectReference property AKControlCloneMarker auto		; where to hide the nonused npcs
ObjectReference property AKControlCloneMarkerTemp auto	; temp use to mark where an NPC is

spell property akcontrolrevertspell auto
spell property akCstopcombatspell auto
Spell Property akcontrolmorphspell Auto

actor property akcontroltemp auto 			; used by spells to agree on the temp actor
actor property OrigionalNPC Auto 			; used by spells to move around the origional Actor
actor property akcontroltempmale auto		; male dragonborn puppet
actor property akcontroltempFemale auto		; female dragonborn puppet
actor Property Playerref Auto				; the player!

bool property MainChar = True Auto			; is the player playing the MainChar or someone else?
bool property inprogress Auto				; is the player in progress to playing as someone else?
bool property inracemenu Auto				; used to loop racemenu until it works
Int property akisdead auto					; is the main character dead? 0 = no, 1 = recently dead, 2 = dead

Faction Property CharmFaction Auto

; Morph Complete Function -----------------------------------------------------------------------------------
function AKMorphComplete(Actor akdonor, Actor akimposter, Bool duration = False, Float aktime = 0.0)
	; If target is any puppet / storage actor, revert to original form instead of morphing into puppet
	If akdonor == akcontroltemp || akdonor == akcontroltempmale || akdonor == akcontroltempFemale || akdonor == akquestfrag.alias_ControlTemp.GetReference() as Actor
		akrevert(akimposter)
		return
	EndIf

	if inprogress == 0
		inprogress = 1
		debug.setgodmode(true)
		
		; Capture original player name before any appearance or trait cloning occurs
		String origPlayerName = AK_NG.GetOriginalPlayerName()
		If origPlayerName == ""
			origPlayerName = playerref.GetActorBase().GetName()
		EndIf

		akCstopcombatspell.Cast(akimposter)
		
		If akquestmcm.Debugmode == False
			game.DisablePlayerControls(true,true,true,true,true, true, True, True)
			akquest2.akloadscreenon()
		EndIf
		
		; Ensure aliases and ghost state are initialized
		akquestfrag.alias_Ghost.Clear()
		playerref.SetGhost(False)
		
		; Dynamically select male or female puppet based on the player's gender
		if AK_NG.GetActorSex(playerref) == 0
			akcontroltemp = akcontroltempmale	
		Else
			akcontroltemp = akcontroltempFemale
		EndIf
		akquestfrag.alias_ControlTemp.ForceRefTo(akcontroltemp)
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
		; Aliases preserved on donor
		akCstopcombatspell.Cast(playerref)
		
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
		If akquestmcm.dragonbornfollower == False
			akcontroltemp.moveto(AKControlCloneMarker)
			AK_NG.SetPuppetActor(None)
		Else
			akcontroltemp.SetPlayerTeammate(True, False)
			akcontroltemp.SetDisplayName(origPlayerName, True)
			akcontroltemp.BlockActivation(True)
			AK_NG.SetPuppetActor(akcontroltemp)
			akcontroltemp.EvaluatePackage()
		EndIf

		If akquestmcm.Debugmode == False
			akquest2.akloadscreenoff()
		EndIf
		
		if akquestmcm.revertondeath == True
			akimposter.GetActorBase().SetEssential(True)
		EndIf
		
		game.EnablePlayerControls()
		debug.setgodmode(false)
		
		AKTraitTransferInvis(akcontroltemp, origionalnpc)
		playerref.addspell(akcontrolrevertspell, False) 
		
		if duration == True
			debug.Notification("You will be able to maintain control for " + (aktime as int) + " seconds.")
			akdurationtime = aktime
			akstarttime = utility.GetCurrentRealTime()
		Else
			akdurationtime = 0.0
		EndIf
		
		inprogress = 0
		MainChar = False
		AK_NG.SetControllingNPC(True)
		AK_NG.BeginImpersonation(akdonor, playerref)
		RegisterForSingleUpdate(1.0)
		RegisterForMenu("Dialogue Menu")

		Debug.Notification("Control Swapped")
	Else
		debug.notification("Already changing controls!")
	EndIf
EndFunction

; Revert Function -----------------------------------------------------------------------------------
function akrevert(actor akimposter, Bool AKDrainmagic = False)
	if MainChar == False
		if inprogress == 0
			inprogress = 1
			AK_NG.SetControllingNPC(False)
			AK_NG.EndImpersonation()
			UnregisterForUpdate()
			akdurationtime = 0.0
			
			If akquestmcm.Debugmode == False
				game.DisablePlayerControls(true,true,true,true,true, true, True, True)
				akquest2.akloadscreenon()
			EndIf
			
			akcontrolclonemarkertemp.MoveTo(akimposter)
			
			If akquestmcm.dragonbornfollower == True
				akimposter.moveto(akcontroltemp)
			EndIf
			
			debug.setgodmode(true)
			origionalnpc.moveto(akimposter)
			
			akCstopcombatspell.Cast(akimposter)
			akimposter.RemoveSpell(akcontrolrevertspell)
			
			AK_NG.CopyStats(playerref, origionalnpc)
			AK_NG.RevertAppearance(playerref)
			AK_NG.RestoreStats(playerref)
			AK_NG.RestoreInventoryFromPlayer(playerref, origionalnpc, akcontroltemp)
			AK_NG.RestoreFactions(playerref)
			If origionalnpc != None
				origionalnpc.SetAlpha(1.0)
				origionalnpc.SetGhost(False)
			EndIf
			UnregisterForMenu("Dialogue Menu")
			
			akcontroltemp.BlockActivation(False)
			akcontroltemp.SetDisplayName("")
			akcontroltemp.SetPlayerTeammate(False, False)
			AK_NG.SetPuppetActor(None)
			akcontroltemp.moveto(akcontrolclonemarker)
			akcontrolclonemarkertemp.moveto(akcontrolclonemarker)

			AKQuestFrag.Alias_Ghost.Clear()
			playerref.SetGhost(False)
			akisdead = 0
			
			debug.setgodmode(false)
			playerref.GetActorBase().SetEssential(False)
			MainChar = True

			If akquestmcm.Debugmode == False
				akquest2.akloadscreenoff()
			EndIf

			game.EnablePlayerControls()
			inprogress = 0
			
			Debug.Notification("Returned to Body")
		Else
			debug.notification("Already reverting controls!")
		EndIf
	EndIf
EndFunction

; Compatibility helper
Function AKTraitTransferInvis(Actor akdonor, Actor akimposter)
	; Kept for backwards compatibility
EndFunction

Event OnUpdate()
	If MainChar == False
		If akdurationtime > 0.0
			Float elapsed = Utility.GetCurrentRealTime() - akstarttime
			If elapsed >= akdurationtime
				akdurationtime = 0.0
				akrevert(playerref)
				Return
			EndIf
		EndIf
		AK_NG.UpdateIdleAI()
		RegisterForSingleUpdate(1.0)
	EndIf
EndEvent

Event OnMenuOpen(String MenuName)
	If MenuName == "Dialogue Menu" && MainChar == False
		If origionalnpc != None
			origionalnpc.SetAlpha(0.0)
		EndIf
	EndIf
EndEvent

Event OnMenuClose(String MenuName)
	If MenuName == "Dialogue Menu" && MainChar == False
		If origionalnpc != None
			origionalnpc.SetAlpha(1.0)
			origionalnpc.SetGhost(False)
			origionalnpc.MoveTo(AKControlCloneMarker)
		EndIf
	EndIf
EndEvent
