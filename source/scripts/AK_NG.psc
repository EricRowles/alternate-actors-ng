; Native bridge for Alternate Actors NG SKSE64 Plugin
Scriptname AK_NG Hidden

; Redirects all running quest aliases currently referencing akDonor to point to akPlayer instead.
; Returns the number of aliases successfully redirected.
Int Function SwapAliases(Actor akDonor, Actor akPlayer) Global Native

; Restores all previously redirected quest aliases back to their original actor references.
; Returns the number of aliases successfully restored.
Int Function RevertAliases() Global Native

; Instantly copies full facial structure, headparts, hair color, race, skin, and tints from akDonor to akTarget.
; Automatically saves akTarget's previous appearance for restoration.
Bool Function CloneAppearance(Actor akDonor, Actor akTarget) Global Native

; Instantly copies full facial structure, headparts, hair color, race, skin, and tints from akDonor to akTarget.
; Does not record or modify the saved player appearance.
Bool Function CopyAppearance(Actor akDonor, Actor akTarget) Global Native

; Restores akTarget's original appearance from before the last CloneAppearance call.
Bool Function RevertAppearance(Actor akTarget) Global Native

; Stores akPlayer's current inventory into akStorageFollower (if provided) then transfers akDonor's
; inventory + dynamic spells to akPlayer. Pass None for akStorageFollower if DragonbornFollower is not used.
Bool Function TransferInventoryToPlayer(Actor akPlayer, Actor akDonor, Actor akStorageFollower) Global Native

; Removes previously copied donor spells from akPlayer, returns akPlayer's inventory back to akDonor,
; and retrieves akPlayer's original gear from akStorageFollower (if provided).
Bool Function RestoreInventoryFromPlayer(Actor akPlayer, Actor akDonor, Actor akStorageFollower) Global Native

; Copies all base and dynamic factions (ranks and crime faction) from akDonor to akPlayer,
; saving akPlayer's original factions for restoration.
Bool Function TransferFactions(Actor akDonor, Actor akPlayer) Global Native

; Restores akPlayer's original factions and crime faction.
Bool Function RestoreFactions(Actor akPlayer) Global Native

; Copies all base stats (Health, Magicka, Stamina) and 18 skills from akDonor to akPlayer,
; saving akPlayer's original stats for restoration.
Bool Function TransferStats(Actor akDonor, Actor akPlayer) Global Native

; Copies all base stats and 18 skills from akFrom to akTo without modifying saved player stats.
Bool Function CopyStats(Actor akFrom, Actor akTo) Global Native

; Restores akPlayer's original base stats and skills.
Bool Function RestoreStats(Actor akPlayer) Global Native

; Enables or disables autonomous idle AI pathing on the player when inactive.
Function SetIdleAIEnabled(Bool abEnabled) Global Native

; Sets the idle timeout in seconds before autonomous AI pathing begins (default 10.0s).
Function SetIdleAITimeout(Float afSeconds) Global Native

; Returns whether autonomous idle AI is currently active on the controlled player.
Bool Function IsIdleAIActive() Global Native

; Sets whether the player is currently actively controlling an alternate NPC.
Function SetControllingNPC(Bool abControlling) Global Native

; Updates idle timer and initiates autonomous pathing if inactive threshold is met.
Function UpdateIdleAI() Global Native

; Opens Skyrim's native Dialogue Menu directly with the controlled actor's morphed identity.
Bool Function OpenSelfDialogue(Actor akActor = None) Global Native

; Triggers an immediate on-demand voice line / greeting from the controlled actor's morphed voice.
Bool Function TriggerVoiceLine(Actor akActor = None) Global Native

; Returns the base biological sex of the actor (0 = Male, 1 = Female).
Int Function GetActorSex(Actor akActor) Global Native

; Initiates full impersonation of akDonor by akPlayer (conditions, aliases, packages, scripts).
Int Function BeginImpersonation(Actor akDonor, Actor akPlayer) Global Native

; Ends active impersonation and restores original targets and properties.
Int Function EndImpersonation() Global Native

; Configures which impersonation layers are active (Conditions, Aliases, Packages, Scripts).
Function SetImpersonationOptions(Bool abConditions, Bool abAliases, Bool abPackages, Bool abScripts) Global Native

; Returns whether impersonation is currently active.
Bool Function IsImpersonating() Global Native

; Returns the currently impersonated donor actor, or None if inactive.
Actor Function GetImpersonatedActor() Global Native

; Registers the active puppet actor holding the player's true appearance/identity.
Function SetPuppetActor(Actor akPuppet) Global Native

; Returns the player's true original character name.
String Function GetOriginalPlayerName() Global Native

