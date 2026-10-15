Scriptname AKControlMCMScript extends SKI_ConfigBase 
; This script powers Alternate Actors' MCM Menu using reliable Option-ID handlers

AKControlQuestScript property akquest auto
Actor Property PlayerRef Auto

; MCM Property Variables
bool Property IdleAIEnabled = True Auto
Float Property IdleAITimeout = 5.0 Auto
bool Property CombatAIEnabled = True Auto
bool Property NPCVoiceEnabled = True Auto
bool Property PuppetFollowsPlayer = True Auto

; Option IDs
int _oid_idleAI
int _oid_idleTimeout
int _oid_combatAI
int _oid_npcVoice
int _oid_puppetFollows
int _oid_revert

int function GetVersion()
	return 5
endFunction

event OnVersionUpdate(int a_version)
	ModName = "Alternate Actors NG"
	Pages = None

	AK_NG.SetCombatAIEnabled(CombatAIEnabled)
endEvent

event OnConfigInit()
	ModName = "Alternate Actors NG"
	Pages = None

	AK_NG.SetIdleAIEnabled(IdleAIEnabled)
	AK_NG.SetIdleAITimeout(IdleAITimeout)
	AK_NG.SetCombatAIEnabled(CombatAIEnabled)
	AK_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
	AK_NG.SetPuppetFollowsPlayer(PuppetFollowsPlayer)
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
	_oid_combatAI = AddToggleOption("Autonomous Combat AI", CombatAIEnabled)

	; Right Column: Puppet & Voice Settings
	SetCursorPosition(5)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Impersonation Settings")
	_oid_npcVoice = AddToggleOption("NPC Voice", NPCVoiceEnabled)
	_oid_puppetFollows = AddToggleOption("Puppet Follows Player", PuppetFollowsPlayer)
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
	elseif option == _oid_combatAI
		CombatAIEnabled = !CombatAIEnabled
		SetToggleOptionValue(_oid_combatAI, CombatAIEnabled)
		AK_NG.SetCombatAIEnabled(CombatAIEnabled)
	elseif option == _oid_npcVoice
		NPCVoiceEnabled = !NPCVoiceEnabled
		SetToggleOptionValue(_oid_npcVoice, NPCVoiceEnabled)
		AK_NG.SetNPCVoiceEnabled(NPCVoiceEnabled)
	elseif option == _oid_puppetFollows
		PuppetFollowsPlayer = !PuppetFollowsPlayer
		SetToggleOptionValue(_oid_puppetFollows, PuppetFollowsPlayer)
		AK_NG.SetPuppetFollowsPlayer(PuppetFollowsPlayer)
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
	elseif option == _oid_combatAI
		SetInfoText("Enables autonomous combat AI (attacking, blocking, distance closing) while idle in combat.")
	elseif option == _oid_npcVoice
		SetInfoText("Enables donor NPC voice comments and voice identity on the player while possessed.")
	elseif option == _oid_puppetFollows
		SetInfoText("Controls whether the Dragonborn puppet follows you as a teammate.")
	elseif option == _oid_revert
		SetInfoText("Immediately return control back to your original character.")
	endif
endEvent
