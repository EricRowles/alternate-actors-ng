Scriptname AKControlMCMScript extends SKI_ConfigBase 
; This script powers Alternate Actors' MCM Menu using reliable Option-ID handlers

AKControlQuestScript property akquest auto
Actor Property PlayerRef Auto

; MCM Property Variables
bool Property IdleAIEnabled = True Auto
Float Property IdleAITimeout = 5.0 Auto
bool Property ImpersonateConditions = True Auto
bool Property ImpersonateAliases = True Auto
bool Property ImpersonatePackages = True Auto
bool Property ImpersonateScripts = False Auto

; Option IDs
int _oid_idleAI
int _oid_idleTimeout
int _oid_revert
int _oid_impersonateConditions
int _oid_impersonateAliases
int _oid_impersonatePackages
int _oid_impersonateScripts

int function GetVersion()
	return 3
endFunction

event OnVersionUpdate(int a_version)
	ModName = "Alternate Actors NG"
	Pages = None
endEvent

event OnConfigInit()
	ModName = "Alternate Actors NG"
	Pages = None

	AK_NG.SetIdleAIEnabled(IdleAIEnabled)
	AK_NG.SetIdleAITimeout(IdleAITimeout)
	AK_NG.SetImpersonationOptions(ImpersonateConditions, ImpersonateAliases, ImpersonatePackages, ImpersonateScripts)
endEvent

function RefreshConfig()
	OnGameReload()
endFunction


event OnPageReset(string page)
	SetCursorPosition(0)
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

	; Right Column: NPC Impersonation System & Controls
	SetCursorPosition(5)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("NPC Impersonation")
	_oid_impersonateConditions = AddToggleOption("Spoof Conditions", ImpersonateConditions)
	_oid_impersonateAliases = AddToggleOption("Redirect Quest Aliases", ImpersonateAliases)
	_oid_impersonatePackages = AddToggleOption("Retarget AI Packages", ImpersonatePackages)
	_oid_impersonateScripts = AddToggleOption("Redirect Papyrus Scripts", ImpersonateScripts)
	AddEmptyOption()

	If akquest.Alias_OriginalDonor.GetActorReference()
		_oid_revert = AddTextOption("Return to Body", "Click to Revert")
	Else
		_oid_revert = AddTextOption("Return to Body", "Already in Body", OPTION_FLAG_DISABLED)
	EndIf
endEvent


event OnOptionSelect(int option)
	if option == _oid_idleAI
		IdleAIEnabled = !IdleAIEnabled
		SetToggleOptionValue(_oid_idleAI, IdleAIEnabled)
		AK_NG.SetIdleAIEnabled(IdleAIEnabled)
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
		if akquest.Alias_OriginalDonor.GetActorReference()
			SetTextOptionValue(_oid_revert, "Reverting...")
			akquest.akrevert(PlayerRef)
		endif
	endif
endEvent

event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
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
		SetInfoText("Enables autonomous AI pathing on your player after a few seconds of inactivity.")
	elseif option == _oid_idleTimeout
		SetInfoText("Seconds of player inactivity before autonomous AI pathing begins.")
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
