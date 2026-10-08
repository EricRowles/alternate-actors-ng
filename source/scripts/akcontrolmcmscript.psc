Scriptname AKControlMCMScript extends SKI_ConfigBase 
; This script powers Alternate Actors' MCM Menu using reliable Option-ID handlers

AKControlQuestScript property akquest auto
AKControlQuest2Script property akquest2 auto
Actor Property Playerref Auto

; MCM Property Variables
bool Property RevertOnDeath = True Auto 		; Revert to player on death
bool Property dragonbornfollower = True Auto Hidden 	; Does the dragonborn puppet appear after transfer
bool Property CanBeGhost = False Auto Hidden	; Deprecated ghost feature (retained for property compatibility)
bool Property lorefriendly = False Auto Hidden	; Hide cheat spells
bool Property DebugMode = False Auto Hidden		; Hide loading screen
bool Property IdleAIEnabled = False Auto
Float Property IdleAITimeout = 10.0 Auto
int Property DialogueHotkey = 47 Auto ; Key V
bool Property ImpersonateConditions = True Auto
bool Property ImpersonateAliases = True Auto
bool Property ImpersonatePackages = True Auto
bool Property ImpersonateScripts = False Auto

; MCM Spell Properties
Spell Property AKControlMorphSpell Auto
Spell Property AKControlRemoteSpell Auto Hidden ; Deprecated
Spell Property AKControlSaveRemoteSpell Auto Hidden ; Deprecated

; Option IDs
int _oid_idleAI
int _oid_idleTimeout
int _oid_dialogueHotkey
int _oid_revertOnDeath
int _oid_revert
int _oid_impersonateConditions
int _oid_impersonateAliases
int _oid_impersonatePackages
int _oid_impersonateScripts

event OnConfigInit()
	Pages = new string[1]
	Pages[0] = "Options"

	AK_NG.SetIdleAIEnabled(IdleAIEnabled)
	AK_NG.SetIdleAITimeout(IdleAITimeout)
	AK_NG.SetDialogueHotkey(DialogueHotkey)
	AK_NG.SetImpersonationOptions(ImpersonateConditions, ImpersonateAliases, ImpersonatePackages, ImpersonateScripts)
endEvent

event OnPageReset(string page)
	if (page == "")
		LoadCustomContent("AKAlternateActorsTitle.dds", 3, 105)
		return
	else
		UnloadCustomContent()
	endIf

	SetCursorPosition(0)
	if (page == "Options")
		SetCursorFillMode(LEFT_TO_RIGHT)
		AddTextOption("Mod Version", akquest.akmodversion, OPTION_FLAG_DISABLED)
		AddTextOption("Status", "Config Active", OPTION_FLAG_DISABLED)
		AddEmptyOption()

		; Left Column: Autonomous AI & Gameplay Settings
		SetCursorPosition(4)
		SetCursorFillMode(TOP_TO_BOTTOM)
		AddHeaderOption("Autonomous AI")
		_oid_idleAI = AddToggleOption("Idle AI Pathing", IdleAIEnabled)
		_oid_idleTimeout = AddSliderOption("Idle AI Delay", IdleAITimeout, "{1} s")
		_oid_dialogueHotkey = AddKeyMapOption("Morphed Speech Hotkey", DialogueHotkey)
		_oid_revertOnDeath = AddToggleOption("Revert on Death", RevertOnDeath)

		; Right Column: NPC Impersonation System & Controls
		SetCursorPosition(5)
		SetCursorFillMode(TOP_TO_BOTTOM)
		AddHeaderOption("NPC Impersonation")
		_oid_impersonateConditions = AddToggleOption("Spoof Conditions", ImpersonateConditions)
		_oid_impersonateAliases = AddToggleOption("Redirect Quest Aliases", ImpersonateAliases)
		_oid_impersonatePackages = AddToggleOption("Retarget AI Packages", ImpersonatePackages)
		_oid_impersonateScripts = AddToggleOption("Redirect Papyrus Scripts", ImpersonateScripts)
		AddEmptyOption()

		If akquest.MainChar == False
			_oid_revert = AddTextOption("Return to Body", "Click to Revert")
		Else
			_oid_revert = AddTextOption("Return to Body", "Already in Body", OPTION_FLAG_DISABLED)
		EndIf
	endIf
endEvent

event OnOptionSelect(int option)
	if option == _oid_idleAI
		IdleAIEnabled = !IdleAIEnabled
		SetToggleOptionValue(_oid_idleAI, IdleAIEnabled)
		AK_NG.SetIdleAIEnabled(IdleAIEnabled)
	elseif option == _oid_revertOnDeath
		RevertOnDeath = !RevertOnDeath
		SetToggleOptionValue(_oid_revertOnDeath, RevertOnDeath)
	elseif option == _oid_impersonateConditions
		ImpersonateConditions = !ImpersonateConditions
		SetToggleOptionValue(_oid_impersonateConditions, ImpersonateConditions)
		AK_NG.SetImpersonationOptions(ImpersonateConditions, ImpersonateAliases, ImpersonatePackages, ImpersonateScripts)
	elseif option == _oid_impersonateAliases
		ImpersonateAliases = !ImpersonateAliases
		SetToggleOptionValue(_oid_impersonateAliases, ImpersonateAliases)
		AK_NG.SetImpersonationOptions(ImpersonateConditions, ImpersonateAliases, ImpersonatePackages, ImpersonateScripts)
	elseif option == _oid_impersonatePackages
		ImpersonatePackages = !ImpersonatePackages
		SetToggleOptionValue(_oid_impersonatePackages, ImpersonatePackages)
		AK_NG.SetImpersonationOptions(ImpersonateConditions, ImpersonateAliases, ImpersonatePackages, ImpersonateScripts)
	elseif option == _oid_impersonateScripts
		ImpersonateScripts = !ImpersonateScripts
		SetToggleOptionValue(_oid_impersonateScripts, ImpersonateScripts)
		AK_NG.SetImpersonationOptions(ImpersonateConditions, ImpersonateAliases, ImpersonatePackages, ImpersonateScripts)
	elseif option == _oid_revert
		if akquest.MainChar == False
			SetTextOptionValue(_oid_revert, "Reverting...")
			akquest.akrevert(playerref)
		endif
	endif
endEvent

event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
	if option == _oid_dialogueHotkey
		DialogueHotkey = keyCode
		SetKeyMapOptionValue(_oid_dialogueHotkey, DialogueHotkey)
		AK_NG.SetDialogueHotkey(DialogueHotkey)
	endif
endEvent

event OnOptionSliderOpen(int option)
	if option == _oid_idleTimeout
		SetSliderDialogStartValue(IdleAITimeout)
		SetSliderDialogDefaultValue(10.0)
		SetSliderDialogRange(1.0, 120.0)
		SetSliderDialogInterval(1.0)
	endif
endEvent

event OnOptionSliderAccept(int option, float value)
	if option == _oid_idleTimeout
		IdleAITimeout = value
		SetSliderOptionValue(_oid_idleTimeout, IdleAITimeout, "{1} s")
		AK_NG.SetIdleAITimeout(IdleAITimeout)
	endif
endEvent

event OnOptionHighlight(int option)
	if option == _oid_idleAI
		SetInfoText("Enables autonomous AI pathing on your controlled character after a few seconds of inactivity.")
	elseif option == _oid_idleTimeout
		SetInfoText("Seconds of player inactivity before autonomous AI pathing begins.")
	elseif option == _oid_dialogueHotkey
		SetInfoText("Hotkey to trigger dialogue or greeting from your morphed identity (default 'V').")
	elseif option == _oid_revertOnDeath
		SetInfoText("Revert back to your Dragonborn character if the controlled NPC falls in combat.")
	elseif option == _oid_revert
		SetInfoText("Immediately return control back to your original character.")
	elseif option == _oid_impersonateConditions
		SetInfoText("Allows dialogue and scripted events targeting the donor NPC by ID or reference to recognize you.")
	elseif option == _oid_impersonateAliases
		SetInfoText("Redirects running quest aliases pointing to the donor NPC over to the player.")
	elseif option == _oid_impersonatePackages
		SetInfoText("Redirects other NPCs' AI packages (follow, escort, approach) to target the player.")
	elseif option == _oid_impersonateScripts
		SetInfoText("EXPERIMENTAL: Redirects other mods' Papyrus script properties pointing to the donor over to the player.")
	endif
endEvent
