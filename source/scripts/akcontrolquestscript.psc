Scriptname AKControlQuestScript extends Quest  

AKControlMCMScript property akquestmcm Auto

float property akmodversion = 2.0 Auto

ObjectReference property AKControlCloneMarker auto		; where to hide the nonused npcs
ObjectReference property AKControlCloneMarkerTemp auto	; temp use to mark where an NPC is

spell property akcontrolrevertspell auto
Spell Property akcontrolmorphspell Auto

ReferenceAlias Property Alias_PlayerPuppet Auto 	; Temp Actor currently behaving as player
ReferenceAlias Property Alias_OriginalDonor Auto 	; Original Donor NPC

actor property akcontroltempmale auto			; male dragonborn puppet
actor property akcontroltempFemale auto			; female dragonborn puppet
actor Property PlayerRef Auto					; the player!

actor PlayerPuppetRef
actor OriginalDonorRef

; Initiation Function -----------------------------------------------------------------------------------
Event OnInit()
	akmaintenance()
EndEvent

; Note: Quests do not receive engine OnPlayerLoadGame events; game reloads are
; forwarded to akmaintenance() via AKControlPlayerScript on the Player alias.


; Maintenance Function -----------------------------------------------------------------------------------
function akmaintenance()
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()

	If !PlayerRef.HasSpell(akcontrolmorphspell)
		PlayerRef.AddSpell(akcontrolmorphspell, false)
	EndIf

	If OriginalDonorRef && PlayerRef.HasSpell(akcontrolrevertspell)
		PlayerRef.RemoveSpell(akcontrolrevertspell)
	EndIf

	; Synchronize SKSE Native settings from MCM properties on game load
	AK_NG.SetIdleAIEnabled(akquestmcm.IdleAIEnabled)
	AK_NG.SetIdleAITimeout(akquestmcm.IdleAITimeout)
	AK_NG.SetNPCVoiceEnabled(akquestmcm.NPCVoiceEnabled)
	AK_NG.SetPuppetFollowsPlayer(akquestmcm.PuppetFollowsPlayer)

	If OriginalDonorRef
		AK_NG.SetControllingNPC(True)
		PlayerPuppetRef = Alias_PlayerPuppet.GetActorReference()
		If PlayerPuppetRef
			AK_NG.SetPuppetActor(PlayerPuppetRef)
		EndIf
		AK_NG.CopyAppearance(OriginalDonorRef, PlayerRef)
	EndIf
EndFunction

; Morph Complete Function -----------------------------------------------------------------------------------
function AKMorphComplete(Actor akdonor, Actor akimposter)
	if akdonor == Alias_PlayerPuppet.GetActorReference()
		akrevert(akimposter)
		return
	EndIf

	; Capture original player name before any appearance or trait cloning occurs
	String origPlayerName = AK_NG.GetOriginalPlayerName()
	If origPlayerName == ""
		origPlayerName = PlayerRef.GetActorBase().GetName()
	EndIf

	akimposter.StopCombat()
	akdonor.StopCombat()
	
	Game.FadeOutGame(true, true, 0.0, 0.3)
	
	; Dynamically select male or female puppet based on the player's gender
	Actor currentPuppet = Alias_PlayerPuppet.GetActorReference()
	If !currentPuppet
		if AK_NG.GetActorSex(PlayerRef) == 0
			Alias_PlayerPuppet.ForceRefTo(akcontroltempmale)
		Else
			Alias_PlayerPuppet.ForceRefTo(akcontroltempfemale)
		EndIf
		currentPuppet = Alias_PlayerPuppet.GetActorReference()
	EndIf
	PlayerPuppetRef = currentPuppet
	AK_NG.SetPuppetActor(PlayerPuppetRef)

	; Transfer traits from PC back to orignpc before getting new traits if we were already controlling an NPC
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()
	If OriginalDonorRef
		AK_NG.EndImpersonation()
		OriginalDonorRef.moveto(akdonor)
		Utility.Wait(0.1)
		AK_NG.RestoreInventoryFromPlayer(PlayerRef, OriginalDonorRef, None)
		AK_NG.CopyStats(PlayerRef, OriginalDonorRef)
	Else
		PlayerPuppetRef.moveto(akdonor)
		Utility.Wait(0.1) ; Allow 3D node attachment in loaded cell before copying appearance
	EndIf
	
	; Make the player look and have stats like the donor NPC
	If OriginalDonorRef
		AK_NG.ApplyDonorProfile(akdonor, PlayerRef, None)
	Else
		AK_NG.CopyAppearance(PlayerRef, PlayerPuppetRef)
		AK_NG.CopyStats(PlayerRef, PlayerPuppetRef)	
		AK_NG.ApplyDonorProfile(akdonor, PlayerRef, PlayerPuppetRef)
	EndIf
	
	Alias_OriginalDonor.ForceRefTo(akdonor)

	akdonor.moveto(AKControlCloneMarker)
	PlayerPuppetRef.SetDisplayName(origPlayerName, True)
	PlayerPuppetRef.BlockActivation(True)
	AK_NG.SetPuppetActor(PlayerPuppetRef)
	
	PlayerRef.addspell(akcontrolrevertspell, False) 
	
	AK_NG.SetControllingNPC(True)
	AK_NG.BeginImpersonation(akdonor, PlayerRef)

	Game.FadeOutGame(false, true, 0.0, 0.4)

	Debug.Notification("Control Swapped")
EndFunction

; Revert Function -----------------------------------------------------------------------------------
function akrevert(actor akimposter, Bool AKDrainmagic = False)
	OriginalDonorRef = Alias_OriginalDonor.GetActorReference()
	if OriginalDonorRef
		PlayerPuppetRef = Alias_PlayerPuppet.GetActorReference()

		Game.FadeOutGame(true, true, 0.0, 0.3)

		AK_NG.SetControllingNPC(False)
		AK_NG.EndImpersonation()		
		
		akcontrolclonemarkertemp.MoveTo(akimposter)
		akimposter.moveto(PlayerPuppetRef)
		
		akimposter.StopCombat()

		if OriginalDonorRef
			OriginalDonorRef.moveto(akimposter)
			OriginalDonorRef.StopCombat()
		EndIf
		akimposter.RemoveSpell(akcontrolrevertspell)
		
		AK_NG.RestorePlayerProfile(PlayerRef, OriginalDonorRef, PlayerPuppetRef)

		PlayerPuppetRef.BlockActivation(False)
		AK_NG.SetPuppetActor(None)
		PlayerPuppetRef.moveto(akcontrolclonemarker)
		akcontrolclonemarkertemp.moveto(akcontrolclonemarker)
		Alias_OriginalDonor.Clear()
		Alias_PlayerPuppet.Clear()

		Game.FadeOutGame(false, true, 0.0, 0.4)
		
		Debug.Notification("Returned to Body")
	EndIf
EndFunction

