#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/T/TESCombatStyle.h"
#include "RE/T/TESClass.h"
#include "RE/E/ExtraCombatStyle.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/ProcessLists.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/S/SpellItem.h"
#include "RE/E/EffectSetting.h"
#include "RE/M/MagicCaster.h"
#include "RE/C/CombatController.h"
#include "RE/C/CombatAnimation.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/T/TESAmmo.h"
#include "RE/A/AttackBlockHandler.h"
#include "RE/P/Projectile.h"
#include <SKSE/ContextHook.h>
#include "AliasManager.h"
#include <random>
#include <cmath>

namespace AlternateActors {

    void IdleAIManager::Initialize() {
        if (!_initialized) {
            auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton();
            if (inputDispatcher) {
                inputDispatcher->AddEventSink<RE::InputEvent*>(this);
                SKSE::log::info("IdleAIManager registered input event sink successfully.");
            } else {
                SKSE::log::warn("IdleAIManager failed to get BSInputDeviceManager singleton.");
            }

            auto* ui = RE::UI::GetSingleton();
            if (ui) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
                SKSE::log::info("IdleAIManager registered MenuOpenCloseEvent sink successfully.");
            } else {
                SKSE::log::warn("IdleAIManager failed to get UI singleton.");
            }

            InstallHooks();

            _initialized = true;
        }
    }

    void IdleAIManager::Hook_Projectile_Launch(CONTEXT& a_ctx) {
        auto* launchData = reinterpret_cast<RE::Projectile::LaunchData*>(a_ctx.Rdx);
        if (!launchData) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (launchData->shooter == player) {
            if (IdleAIManager::GetSingleton().IsCombatAIActive() || IdleAIManager::GetSingleton().IsBowDrawn()) {
                auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
                auto* donor = IdleAIManager::GetSingleton().GetActiveDonorActor();
                auto* substitute = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(donor);
                if (substitute) {
                    launchData->shooter = substitute;
                    SKSE::log::info("Hook_Projectile_Launch: Substituted player with {:08X} to bypass player draw-timer override.",
                        substitute->GetFormID());
                }
            }
        }
    }

    void IdleAIManager::InstallHooks() {
        if (_hooksInstalled) return;

        auto launchAddr = REL::RelocationID(42928, 44108).address();
        if (SKSE::stl::install_context_hook(launchAddr, 5, &Hook_Projectile_Launch, -5)) {
            SKSE::log::info("Installed Projectile::Launch context hook at {:016X}", launchAddr);
        } else {
            SKSE::log::error("Failed to install Projectile::Launch context hook!");
        }

        _hooksInstalled = true;
    }

    void IdleAIManager::Update() {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            CheckIdleAI();
            if (_aiActive.load(std::memory_order_acquire) && _isControllingNPC.load(std::memory_order_acquire)) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    if (player->IsInCombat()) {
                        if (!_combatAIEnabled.load(std::memory_order_acquire)) {
                            StopIdleAI();
                        } else {
                            if (!_isCombatAIActive.load(std::memory_order_acquire)) {
                                _isCombatAIActive.store(true, std::memory_order_release);
                                player->SetAIDriven(false);
                            }
                            UpdateCombatAI();
                        }
                    } else {
                        if (_isCombatAIActive.load(std::memory_order_acquire)) {
                            _isCombatAIActive.store(false, std::memory_order_release);
                            player->SetAIDriven(true);
                            player->EvaluatePackage(true, true);
                        }
                    }
                }
            }
            CheckAndTriggerIdleDialogue();
        }
        ImpersonationManager::GetSingleton().Tick();
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        bool hasUserAction = false;
        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* button = static_cast<const RE::ButtonEvent*>(event);
                if (button->IsPressed() || button->IsDown() || button->IsHeld()) {
                    hasUserAction = true;
                    break;
                }
            } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                const auto* stick = static_cast<const RE::ThumbstickEvent*>(event);
                if (std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f) {
                    hasUserAction = true;
                    break;
                }
            }
        }

        if (hasUserAction) {
            OnUserInput();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == "Dialogue Menu"sv) {
            if (!a_event->opening) {
                _inSelfDialogue.store(false, std::memory_order_release);
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
                if (topicMgr && puppet) {
                    auto puppetHandle = puppet->GetHandle();
                    if (topicMgr->speaker == puppetHandle) {
                        topicMgr->speaker.reset();
                    }
                    if (topicMgr->lastSpeaker == puppetHandle) {
                        topicMgr->lastSpeaker.reset();
                    }
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnUserInput() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _lastInputTime = std::chrono::steady_clock::now();
        if (_aiActive.load(std::memory_order_acquire)) {
            StopIdleAI();
        }
    }

    void IdleAIManager::CheckIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_enabled.load(std::memory_order_acquire) || !_isControllingNPC.load(std::memory_order_acquire)) {
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() ||
            ui->IsApplicationMenuOpen() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsMenuOpen("Console"sv) ||
            ui->IsMenuOpen("Loading Menu"sv) || ui->IsMenuOpen("Crafting Menu"sv) || ui->IsMenuOpen("RaceSex Menu"sv) ||
            ui->IsMenuOpen("Barter Menu"sv) || ui->IsMenuOpen("Container Menu"sv) || ui->IsMenuOpen("Journal Menu"sv) ||
            ui->IsMenuOpen("MapMenu"sv) || ui->IsMenuOpen("StatsMenu"sv) || ui->IsMenuOpen("InventoryMenu"sv) ||
            ui->IsMenuOpen("MagicMenu"sv) || ui->IsMenuOpen("TweenMenu"sv)) {
            _lastInputTime = std::chrono::steady_clock::now();
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->IsInCombat()) {
            if (!_combatAIEnabled.load(std::memory_order_acquire)) {
                if (_aiActive.load(std::memory_order_acquire)) {
                    StopIdleAI();
                }
                return;
            }
        }

        float timeout = (player && player->IsInCombat()) ? 0.75f : _idleTimeoutSeconds.load(std::memory_order_acquire);

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _lastInputTime).count();

        if (elapsed >= timeout && !_aiActive.load(std::memory_order_acquire)) {
            StartIdleAI();
        }
    }

    void IdleAIManager::StartIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_isControllingNPC.load(std::memory_order_acquire) || !_enabled.load(std::memory_order_acquire)) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        if (player->IsInCombat() && !_combatAIEnabled.load(std::memory_order_acquire)) {
            return;
        }

        auto* pc = RE::PlayerControls::GetSingleton();
        if (pc) {
            _savedUserRunning = pc->data.running;
        }

        _aiActive.store(true, std::memory_order_release);

        if (player->IsInCombat()) {
            _isCombatAIActive.store(true, std::memory_order_release);
            _lastCombatActionTime = std::chrono::steady_clock::now();
            auto* actorState = player->AsActorState();
            if (actorState && actorState->GetWeaponState() == RE::WEAPON_STATE::kSheathed) {
                player->DrawWeaponMagicHands(true);
            }
            player->SetAIDriven(false);
            SKSE::log::info("Player idle timeout reached in combat - engaged autonomous combat AI");
        } else {
            _isCombatAIActive.store(false, std::memory_order_release);
            player->AllowBleedoutDialogue(true);
            player->SetAIDriven(true);
            player->EvaluatePackage(true, true);

            auto* currentPkg = player->GetCurrentPackage();
            if (currentPkg) {
                SKSE::log::info("Player idle timeout reached - engaged autonomous AI package: {:08X} ({})",
                    currentPkg->GetFormID(), currentPkg->GetFormEditorID() ? currentPkg->GetFormEditorID() : "No EDID");
            } else {
                SKSE::log::warn("Player idle timeout reached - engaged autonomous AI, but GetCurrentPackage() returned null!");
            }
        }
    }

    void IdleAIManager::StopIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pc = RE::PlayerControls::GetSingleton();
        if (player) {
            player->SetAIDriven(false);
            if (player->IsBlocking()) {
                player->NotifyAnimationGraph("blockStop");
            }
            if (_bowDrawn.load(std::memory_order_acquire)) {
                player->NotifyAnimationGraph("attackStop");
                player->NotifyAnimationGraph("arrowRelease");
            }
            player->EvaluatePackage(false, false);

            if (_aiActive.load(std::memory_order_acquire)) {
                SKSE::log::info("Player input detected - disengaged autonomous AI.");
            }
        }
        if (pc) {
            pc->data.autoMove = false;
            pc->data.moveInputVec = { 0.0f, 0.0f };
            pc->data.running = _savedUserRunning;
        }
        _currentTarget.reset();
        _isBlocking = false;
        _bowDrawn.store(false, std::memory_order_release);
        _isCombatAIActive.store(false, std::memory_order_release);
        _aiActive.store(false, std::memory_order_release);
        _lastInputTime = std::chrono::steady_clock::now();
    }

    void IdleAIManager::SetCombatAIEnabled(bool a_enabled) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _combatAIEnabled.store(a_enabled, std::memory_order_release);
        if (!a_enabled && _isCombatAIActive.load(std::memory_order_acquire)) {
            StopIdleAI();
        }
        SKSE::log::info("IdleAIManager combat AI enabled set to: {}", a_enabled);
    }

    void IdleAIManager::SetControllingNPC(bool a_controlling) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _isControllingNPC.store(a_controlling, std::memory_order_release);
        if (!a_controlling) {
            StopIdleAI();
            _inSelfDialogue.store(false, std::memory_order_release);
        } else {
            auto* pc = RE::PlayerControls::GetSingleton();
            if (pc) {
                _savedUserRunning = pc->data.running;
            }
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager controlling NPC set to: {}", a_controlling);
    }

    bool IdleAIManager::OpenSelfDialogue(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!a_speaker) {
            if (puppet) {
                a_speaker = puppet;
            } else if (donor) {
                a_speaker = donor;
            } else {
                a_speaker = player;
            }
        }
        if (!a_speaker) return false;

        SKSE::log::info("OpenSelfDialogue: received request for speaker {:08X} (puppet: {:08X}, donor: {:08X})",
            static_cast<std::uint32_t>(a_speaker->GetFormID()),
            puppet ? static_cast<std::uint32_t>(puppet->GetFormID()) : 0,
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0);

        auto* task = SKSE::GetTaskInterface();
        if (task) {
            RE::ActorHandle speakerHandle = a_speaker->GetHandle();
            task->AddTask([speakerHandle]() {
                auto speaker = speakerHandle.get();
                if (!speaker) {
                    SKSE::log::warn("OpenSelfDialogue task: speaker handle is no longer valid");
                    return;
                }
                IdleAIManager::GetSingleton().OpenSelfDialogueDirect(speaker.get());
            });
            return true;
        }

        return OpenSelfDialogueDirect(a_speaker);
    }

    bool IdleAIManager::OpenSelfDialogueDirect(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_speaker) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            if (ui->GameIsPaused()) {
                SKSE::log::warn("OpenSelfDialogueDirect: Game is paused, aborting");
                return false;
            }
            if (ui->IsMenuOpen("Dialogue Menu"sv)) {
                SKSE::log::warn("OpenSelfDialogueDirect: Dialogue Menu is already open, aborting");
                return false;
            }
            if (ui->IsModalMenuOpen()) {
                SKSE::log::warn("OpenSelfDialogueDirect: Modal menu is open, aborting");
                return false;
            }
        }

        if (_aiActive.load(std::memory_order_acquire)) {
            StopIdleAI();
        }

        if (player->IsInCombat()) {
            SKSE::log::warn("OpenSelfDialogueDirect: Player is currently in combat!");
        }
        if (a_speaker->IsInCombat()) {
            SKSE::log::warn("OpenSelfDialogueDirect: Speaker {:08X} is currently in combat!",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
        }

        // Enable dialogue permissions on both
        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);

        _inSelfDialogue.store(true, std::memory_order_release);

        SKSE::log::info("OpenSelfDialogueDirect: initiating SetDialogueWithPlayer on speaker {:08X} (isPuppet: {}, donor: {:08X}, voiceType: {:08X})",
            static_cast<std::uint32_t>(a_speaker->GetFormID()),
            (a_speaker == puppet),
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0,
            ImpersonationManager::GetSingleton().GetDonorVoiceType() ?
                static_cast<std::uint32_t>(ImpersonationManager::GetSingleton().GetDonorVoiceType()->GetFormID()) : 0);

        // Open native dialogue menu directly with the speaker
        bool opened = a_speaker->SetDialogueWithPlayer(true, false, nullptr);
        if (opened) {
            SKSE::log::info("Successfully opened self-dialogue with actor {:08X} (isPuppet: {})",
                static_cast<std::uint32_t>(a_speaker->GetFormID()), (a_speaker == puppet));
            return true;
        } else {
            SKSE::log::warn("SetDialogueWithPlayer returned false for actor {:08X}, attempting force-greet fallback",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
            bool greeted = a_speaker->SetDialogueWithPlayer(true, true, nullptr);
            if (greeted) {
                SKSE::log::info("Successfully opened self-dialogue via force-greet fallback with actor {:08X}",
                    static_cast<std::uint32_t>(a_speaker->GetFormID()));
            } else {
                SKSE::log::error("Failed to open dialogue with actor {:08X} even with force-greet fallback",
                    static_cast<std::uint32_t>(a_speaker->GetFormID()));
                _inSelfDialogue.store(false, std::memory_order_release);
            }
            return greeted;
        }
    }

    bool IdleAIManager::IsInSelfDialogue() const {
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr && (topicMgr->forceGoodbye || topicMgr->shutMenu || topicMgr->closeInProgress)) {
            return false;
        }

        if (_inSelfDialogue.load(std::memory_order_acquire)) {
            return true;
        }

        auto* ui = RE::UI::GetSingleton();
        bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && (topicMgr->menuOpen || topicMgr->isGreetingPlayer));
        if (!menuOpen && !ImpersonationManager::GetSingleton().IsPlayerSpeaking()) {
            return false;
        }

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!puppet && !donor) return false;

        if (topicMgr) {
            // If Dialogue Menu is open, check if puppet or donor is the active speaker
            if (menuOpen) {
                if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                    return true;
                }
                if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                    return true;
                }
            }

            // If player is actively speaking, speaker or lastSpeaker is valid
            if (ImpersonationManager::GetSingleton().IsPlayerSpeaking()) {
                if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                    return true;
                }
                if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                    return true;
                }
            }
        }

        return false;
    }

    bool IdleAIManager::TriggerVoiceLine(RE::Actor* a_speaker) {
        if (!a_speaker) {
            a_speaker = RE::PlayerCharacter::GetSingleton();
        }
        if (!a_speaker) return false;

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        return a_speaker->SetDialogueWithPlayer(true, true, nullptr);
    }

    static bool SafeConditionCheck(RE::TESCondition* a_cond, RE::TESObjectREFR* a_actionRef, RE::TESObjectREFR* a_targetRef) {
        if (!a_cond || !a_cond->head) return true;
        __try {
            return a_cond->IsTrue(a_actionRef, a_targetRef);
        } __except (1) { // EXCEPTION_EXECUTE_HANDLER
            return false;
        }
    }

    static bool QuestRelatesToDonor(RE::TESQuest* a_quest, RE::Actor* a_donor, const RE::TESFile* a_donorFile) {
        if (!a_quest || !a_donor) return false;
        // 1. Same source plugin as the donor NPC
        if (a_donorFile && a_quest->GetFile(0) == a_donorFile) {
            return true;
        }
        // 2. Donor is in a swapped quest alias
        if (AliasManager::GetSingleton().HasQuest(a_quest->GetFormID())) {
            return true;
        }
        // 3. Quest dialogue conditions explicitly check donor ID
        if (auto* cond = a_quest->QConditions()) {
            for (auto* item = cond->head; item; item = item->next) {
                if (item->data.functionData.function.underlying() == 72) { // kGetIsID
                    auto* form = static_cast<RE::TESForm*>(item->data.functionData.params[0]);
                    if (form && (form == a_donor || form == a_donor->GetBaseObject() || form == a_donor->GetActorBase())) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool IdleAIManager::CheckAndTriggerIdleDialogue() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!_isControllingNPC.load(std::memory_order_acquire) && !impMgr.IsActive()) {
            return false;
        }

        if (!impMgr.IsNPCVoiceEnabled()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();

        if (!player) return false;

        auto* donor = impMgr.GetDonor();
        if (!donor) return false;

        auto* donorBase = donor->GetActorBase();
        const auto* donorFile = donorBase ? donorBase->GetFile(0) : nullptr;

        auto* puppet = impMgr.GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(player);

        // Skip if player is currently speaking
        if (impMgr.IsPlayerSpeaking()) {
            return false;
        }

        // Skip if in combat
        if (player->IsInCombat()) {
            return false;
        }

        // Check menus
        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() ||
            ui->IsApplicationMenuOpen() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsMenuOpen("Console"sv) ||
            ui->IsMenuOpen("Loading Menu"sv) || ui->IsMenuOpen("Crafting Menu"sv) || ui->IsMenuOpen("RaceSex Menu"sv) ||
            ui->IsMenuOpen("Barter Menu"sv) || ui->IsMenuOpen("Container Menu"sv) || ui->IsMenuOpen("Journal Menu"sv) ||
            ui->IsMenuOpen("MapMenu"sv) || ui->IsMenuOpen("StatsMenu"sv) || ui->IsMenuOpen("InventoryMenu"sv) ||
            ui->IsMenuOpen("MagicMenu"sv) || ui->IsMenuOpen("TweenMenu"sv) || ui->IsMenuOpen("Book Menu"sv)) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        auto* currentCell = player->GetParentCell();
        if (currentCell && currentCell != _lastCell) {
            _lastCell = currentCell;
            // On cell transition, allow evaluation after a short settle time (3s)
            _lastIdleChatterTime = now - std::chrono::seconds(12);
        }

        float elapsed = std::chrono::duration<float>(now - _lastIdleChatterTime).count();
        constexpr float kIdleChatterCooldown = 15.0f;
        if (elapsed < kIdleChatterCooldown) {
            return false;
        }

        // Throttle quest & condition polling to once every 1.5 seconds when out of cooldown
        float evalElapsed = std::chrono::duration<float>(now - _lastIdleEvalTime).count();
        constexpr float kIdleEvalInterval = 1.5f;
        if (evalElapsed < kIdleEvalInterval) {
            return false;
        }
        _lastIdleEvalTime = now;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return false;

        static auto lastDiagnosticLogTime = std::chrono::steady_clock::now();
        bool doDiagnosticLog = std::chrono::duration<float>(now - lastDiagnosticLogTime).count() >= 15.0f;
        if (doDiagnosticLog) {
            lastDiagnosticLogTime = now;
        }

        struct Candidate {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
            RE::TESTopicInfo* info{ nullptr };
        };
        std::vector<Candidate> candidates;

        const auto& quests = dataHandler->GetFormArray<RE::TESQuest>();
        for (auto* quest : quests) {
            if (!quest || !quest->IsRunning()) continue;

            // Only evaluate quests relevant to this donor (avoids querying unrelated mods)
            if (!QuestRelatesToDonor(quest, donor, donorFile)) {
                continue;
            }

            // Check quest dialogue conditions safely
            bool objCondPassed = !quest->objConditions.head || SafeConditionCheck(&quest->objConditions, player, targetRef);
            if (doDiagnosticLog) {
                SKSE::log::info("CheckAndTriggerIdleDialogue: Quest {:08X} ('{}') relatesToDonor=true, objConditions={}",
                    quest->GetFormID(), quest->GetName(), objCondPassed);
            }
            if (!objCondPassed) {
                continue;
            }

            constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
            if (kMiscIndex >= (RE::DIALOGUE_TYPE::kTotal - RE::DIALOGUE_TYPE::kBranchedTotal)) {
                continue;
            }

            const auto& miscTopics = quest->topics[kMiscIndex];
            for (auto* topic : miscTopics) {
                if (!topic) continue;
                if (topic->data.subtype.get() != RE::DIALOGUE_DATA::Subtype::kIdle) {
                    continue;
                }

                if (topic->numTopicInfos == 0 || !topic->topicInfos) {
                    continue;
                }

                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (!info) continue;

                    if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRequiresPlayerActivation)) {
                        continue;
                    }

                    if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                        continue;
                    }

                    if (info->objConditions.head && !SafeConditionCheck(&info->objConditions, player, targetRef)) {
                        continue;
                    }

                    candidates.push_back({ quest, topic, info });
                }
            }
        }

        if (candidates.empty()) {
            return false;
        }

        Candidate chosen = candidates[0];
        if (candidates.size() > 1) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
            chosen = candidates[dist(rng)];
        }

        _currentIdleDialogueItem = RE::make_smart<RE::DialogueItem>(chosen.quest, chosen.topic, chosen.info, player);
        if (_currentIdleDialogueItem && !_currentIdleDialogueItem->responses.empty()) {
            auto* response = _currentIdleDialogueItem->responses.front();
            if (response) {
                SKSE::log::info("Triggering autonomous idle dialogue on player: '{}' (topic {:08X}, info {:08X})",
                    response->text.c_str(), chosen.topic->GetFormID(), chosen.info->GetFormID());

                player->UpdateInDialogue(response, false);

                if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                    chosen.info->saidOnce = true;
                }

                _lastIdleChatterTime = now;
                return true;
            }
        }

        return false;
    }

    void IdleAIManager::SetActiveDonorActor(RE::Actor* a_donor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _activeDonorActor = a_donor ? a_donor->GetHandle() : RE::ActorHandle();
        if (a_donor) {
            auto* extraStyle = a_donor->extraList.GetByType<RE::ExtraCombatStyle>();
            if (extraStyle && extraStyle->combatStyle) {
                _donorCombatStyle = extraStyle->combatStyle;
            } else if (auto* base = a_donor->GetActorBase()) {
                _donorCombatStyle = base->GetCombatStyle();
            }
            if (auto* base = a_donor->GetActorBase()) {
                _donorAIData = base->aiData;
                _donorClass = base->npcClass;
            }
            SKSE::log::info("IdleAIManager::SetActiveDonorActor: donor {:08X}, combatStyle {:08X}, class {:08X}",
                static_cast<std::uint32_t>(a_donor->GetFormID()),
                _donorCombatStyle ? static_cast<std::uint32_t>(_donorCombatStyle->GetFormID()) : 0,
                _donorClass ? static_cast<std::uint32_t>(_donorClass->GetFormID()) : 0);
        } else {
            _donorCombatStyle = nullptr;
            _donorClass = nullptr;
            _donorAIData = {};
            SKSE::log::info("IdleAIManager::SetActiveDonorActor: cleared active donor");
        }
    }

    RE::Actor* IdleAIManager::FindBestCombatTarget() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();

        // 1. Existing valid combat target
        auto targetHandle = player->GetActorRuntimeData().currentCombatTarget;
        auto target = targetHandle.get();
        if (target && target.get() != player && target.get() != puppet && target.get() != donor &&
            !target->IsDead() && !target->IsInKillMove() && !target->IsGhost() &&
            target->GetParentCell() == player->GetParentCell()) {
            return target.get();
        }

        // 2. Existing combatController target
        auto& runtime = player->GetActorRuntimeData();
        if (runtime.combatController) {
            auto ccTarget = runtime.combatController->targetHandle.get();
            if (ccTarget && ccTarget.get() != player && ccTarget.get() != puppet && ccTarget.get() != donor &&
                !ccTarget->IsDead() && !ccTarget->IsInKillMove() && !ccTarget->IsGhost() &&
                ccTarget->GetParentCell() == player->GetParentCell()) {
                return ccTarget.get();
            }
        }

        // 3. Scan high actors for hostiles
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return nullptr;

        RE::Actor* bestActor = nullptr;
        float bestDist = 3500.0f;
        RE::NiPoint3 playerPos = player->GetPosition();

        for (const auto& handle : processLists->highActorHandles) {
            auto actor = handle.get();
            if (!actor || actor.get() == player || actor.get() == puppet || actor.get() == donor) {
                continue;
            }
            if (actor->IsDead() || actor->IsInKillMove() || actor->IsGhost()) {
                continue;
            }
            if (actor->GetParentCell() != player->GetParentCell()) {
                continue;
            }

            bool isHostile = actor->IsHostileToActor(player) ||
                             (actor->GetActorRuntimeData().currentCombatTarget == player->GetHandle()) ||
                             player->IsHostileToActor(actor.get());

            if (!isHostile) {
                continue;
            }

            float dist = playerPos.GetDistance(actor->GetPosition());
            if (dist < bestDist) {
                bestDist = dist;
                bestActor = actor.get();
            }
        }

        return bestActor;
    }

    void IdleAIManager::UpdateCombatAI() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->IsInCombat()) return;

        auto* pc = RE::PlayerControls::GetSingleton();

        // 1. Acquire target
        auto* target = FindBestCombatTarget();
        if (!target) {
            if (pc) {
                pc->data.autoMove = false;
                pc->data.moveInputVec = { 0.0f, 0.0f };
            }
            return;
        }

        _currentTarget = target->GetHandle();
        player->GetActorRuntimeData().currentCombatTarget = target->GetHandle();

        // Ensure combat controller has donor combat style
        auto& runtime = player->GetActorRuntimeData();
        if (runtime.combatController) {
            if (_donorCombatStyle && runtime.combatController->combatStyle != _donorCombatStyle) {
                runtime.combatController->combatStyle = _donorCombatStyle;
            }
            runtime.combatController->targetHandle = target->GetHandle();
        }

        // 2. Weapon draw state check
        auto* actorState = player->AsActorState();
        auto weaponState = actorState ? actorState->GetWeaponState() : RE::WEAPON_STATE::kSheathed;
        if (weaponState == RE::WEAPON_STATE::kSheathed) {
            player->DrawWeaponMagicHands(true);
            SKSE::log::info("Combat AI: Drawing weapon for player...");
            return;
        }
        if (weaponState == RE::WEAPON_STATE::kDrawing || weaponState == RE::WEAPON_STATE::kSheathing) {
            // Wait for draw/sheathe animation to complete
            return;
        }

        // 3. Aiming & Heading
        RE::NiPoint3 pPos = player->GetPosition();
        RE::NiPoint3 tPos = target->GetPosition();
        float dx = tPos.x - pPos.x;
        float dy = tPos.y - pPos.y;
        float dz = tPos.z - pPos.z;
        float horizDist = std::sqrt(dx * dx + dy * dy);
        float fullDist = std::sqrt(dx * dx + dy * dy + dz * dz);

        float desiredHeading = std::atan2(dx, dy);
        player->SetHeading(desiredHeading);
        if (horizDist > 0.001f) {
            float desiredPitch = -std::atan2(dz, horizDist);
            player->SetLooking(desiredPitch);
        }

        // 4. Equipment analysis
        auto* leftObj = player->GetEquippedObject(true);
        auto* rightObj = player->GetEquippedObject(false);

        bool hasBow = false;
        bool hasCrossbow = false;
        bool hasShield = false;
        bool hasMeleeWeapon = false;
        auto* leftSpell = leftObj ? skyrim_cast<RE::SpellItem*>(leftObj) : nullptr;
        auto* rightSpell = rightObj ? skyrim_cast<RE::SpellItem*>(rightObj) : nullptr;
        bool hasSpells = (leftSpell != nullptr || rightSpell != nullptr);

        if (rightObj && rightObj->Is(RE::FormType::Weapon)) {
            auto* weap = skyrim_cast<RE::TESObjectWEAP*>(rightObj);
            if (weap) {
                if (weap->GetWeaponType() == RE::WEAPON_TYPE::kBow) {
                    hasBow = true;
                } else if (weap->GetWeaponType() == RE::WEAPON_TYPE::kCrossbow) {
                    hasCrossbow = true;
                } else {
                    hasMeleeWeapon = true;
                }
            }
        }

        if (leftObj && leftObj->Is(RE::FormType::Armor)) {
            auto* armo = skyrim_cast<RE::TESObjectARMO*>(leftObj);
            if (armo && armo->IsShield()) {
                hasShield = true;
            }
        }

        if (!hasBow && !hasCrossbow && !hasMeleeWeapon && !hasSpells) {
            hasMeleeWeapon = true; // Unarmed / fists
        }

        // Ensure ammo is equipped if wielding a bow or crossbow
        if (hasBow || hasCrossbow) {
            if (!player->GetCurrentAmmo()) {
                auto inv = player->GetInventory();
                auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                if (equipMgr) {
                    for (const auto& [boundObj, entryPair] : inv) {
                        if (boundObj && boundObj->Is(RE::FormType::Ammo) && entryPair.first > 0) {
                            equipMgr->EquipObject(player, boundObj);
                            SKSE::log::info("Combat AI: Auto-equipped ammo {:08X} for bow/crossbow", boundObj->GetFormID());
                            break;
                        }
                    }
                }
            }
        }

        // Combat style coefficients
        float offensiveMult = _donorCombatStyle ? _donorCombatStyle->generalData.offensiveMult : 0.75f;
        float defensiveMult = _donorCombatStyle ? _donorCombatStyle->generalData.defensiveMult : 0.5f;
        float powerAttackMult = _donorCombatStyle ? _donorCombatStyle->meleeData.powerAttackIncapacitatedMult : 0.5f;
        float bashMult = _donorCombatStyle ? _donorCombatStyle->meleeData.bashMult : 0.4f;

        if (!pc) return;
        auto& pcd = pc->data;

        auto now = std::chrono::steady_clock::now();
        float actionElapsed = std::chrono::duration<float>(now - _lastCombatActionTime).count();

        // 5. Bow Draw & Release handling
        if (_bowDrawn.load(std::memory_order_acquire)) {
            float drawElapsed = std::chrono::duration<float>(now - _bowDrawTime).count();
            float requiredDraw = hasCrossbow ? 0.3f : 1.2f;

            if (drawElapsed >= requiredDraw) {
                player->NotifyAnimationGraph("attackRelease");
                player->NotifyAnimationGraph("arrowRelease");
                bool relAnim = RE::CombatAnimation::Execute(player, target, RE::CombatAnimation::ANIM::kActionRightRelease);
                _bowDrawn.store(false, std::memory_order_release);
                _lastCombatActionTime = now;
                _currentActionCooldown = 1.3f;
                SKSE::log::info("Combat AI: Released arrow/bolt at target {:08X} (dist: {:.1f}, exec={})",
                    target->GetFormID(), fullDist, relAnim);
            }
            pcd.autoMove = false;
            pcd.moveInputVec = { 0.0f, 0.0f };
            return;
        }

        // 6. Block Release handling
        if (_isBlocking) {
            if (actionElapsed >= _blockDuration) {
                player->NotifyAnimationGraph("blockStop");
                _isBlocking = false;
                _lastCombatActionTime = now;
                _currentActionCooldown = 0.5f;
                // Immediate counter-strike or bash
                if (hasShield && bashMult > 0.35f && (std::rand() % 100 < 50)) {
                    player->NotifyAnimationGraph("blockBash");
                } else {
                    RE::CombatAnimation::Execute(player, target, RE::CombatAnimation::ANIM::kActionRightAttack);
                    player->NotifyAnimationGraph("attackStart");
                }
            }
            pcd.autoMove = false;
            pcd.moveInputVec = { 0.0f, 0.0f };
            return;
        }

        // 7. Tactical Movement (using autoMove without corrupting user's running toggle)
        if (hasBow || hasCrossbow) {
            if (fullDist > 1400.0f) {
                pcd.autoMove = true;
            } else {
                pcd.autoMove = false;
                pcd.moveInputVec = { 0.0f, 0.0f };
            }
        } else if (hasSpells && !hasMeleeWeapon) {
            if (fullDist > 1000.0f) {
                pcd.autoMove = true;
            } else {
                pcd.autoMove = false;
                pcd.moveInputVec = { 0.0f, 0.0f };
            }
        } else {
            // Melee / unarmed
            if (fullDist > 140.0f) {
                pcd.autoMove = true;
            } else {
                pcd.autoMove = false;
                pcd.moveInputVec = { 0.0f, 0.0f };
            }
        }

        // 8. Action Execution
        if (actionElapsed < _currentActionCooldown) {
            return;
        }

        // Bow / Crossbow attack
        if (hasBow || hasCrossbow) {
            if (fullDist <= 1800.0f) {
                _bowDrawn.store(true, std::memory_order_release);
                _bowDrawTime = now;
                _lastCombatActionTime = now;

                if (hasCrossbow) {
                    player->NotifyAnimationGraph("crossbowAttackStart");
                } else {
                    player->NotifyAnimationGraph("bowAttackStart");
                }
                player->NotifyAnimationGraph("attackStart");
                bool ok = RE::CombatAnimation::Execute(player, target, RE::CombatAnimation::ANIM::kActionRightAttack);

                SKSE::log::info("Combat AI: Drawing {} on target {:08X} (dist: {:.1f}, exec={})",
                    hasCrossbow ? "crossbow" : "bow", target->GetFormID(), fullDist, ok);
                return;
            }
        }

        // Spell casting
        if (hasSpells) {
            if (fullDist <= 1200.0f) {
                auto* avOwner = player->AsActorValueOwner();
                float curHealth = avOwner ? avOwner->GetActorValue(RE::ActorValue::kHealth) : 100.0f;
                float maxHealth = avOwner ? avOwner->GetPermanentActorValue(RE::ActorValue::kHealth) : 100.0f;
                bool needHeal = (maxHealth > 0.0f && (curHealth / maxHealth) < 0.6f);

                auto isHealSpell = [](RE::SpellItem* sp) {
                    if (!sp) return false;
                    for (auto* eff : sp->effects) {
                        if (eff && eff->baseEffect) {
                            if (eff->baseEffect->data.primaryAV == RE::ActorValue::kHealth &&
                                (eff->baseEffect->data.archetype == RE::EffectArchetypes::ArchetypeID::kValueModifier ||
                                 eff->baseEffect->data.archetype == RE::EffectArchetypes::ArchetypeID::kPeakValueModifier)) {
                                return true;
                            }
                        }
                    }
                    return false;
                };

                bool casted = false;
                if (leftSpell) {
                    auto* leftCaster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand);
                    if (leftCaster) {
                        RE::TESObjectREFR* castTarget = (needHeal && isHealSpell(leftSpell)) ? static_cast<RE::TESObjectREFR*>(player) : static_cast<RE::TESObjectREFR*>(target);
                        leftCaster->CastSpellImmediate(leftSpell, false, castTarget, 1.0f, false, 0.0f, player);
                        player->NotifyAnimationGraph("castStart");
                        casted = true;
                    }
                }
                if (rightSpell && !casted) {
                    auto* rightCaster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
                    if (rightCaster) {
                        RE::TESObjectREFR* castTarget = (needHeal && isHealSpell(rightSpell)) ? static_cast<RE::TESObjectREFR*>(player) : static_cast<RE::TESObjectREFR*>(target);
                        rightCaster->CastSpellImmediate(rightSpell, false, castTarget, 1.0f, false, 0.0f, player);
                        player->NotifyAnimationGraph("castStart");
                        casted = true;
                    }
                }
                if (casted) {
                    _currentActionCooldown = 1.2f + (1.0f - offensiveMult) * 0.5f;
                    _lastCombatActionTime = now;
                    SKSE::log::info("Combat AI: Cast spell on target {:08X} (dist: {:.1f})", target->GetFormID(), fullDist);
                    return;
                }
            }
        }

        // Melee attack
        if (hasMeleeWeapon || (!hasBow && !hasCrossbow && !hasSpells)) {
            if (fullDist <= 190.0f) {
                // Check if target is attacking and we should block
                bool targetAttacking = target->IsAttacking() || target->IsPowerAttacking();
                float blockRoll = static_cast<float>(std::rand() % 100) / 100.0f;
                if (targetAttacking && (hasShield || hasMeleeWeapon) && (blockRoll < defensiveMult * 0.7f)) {
                    player->NotifyAnimationGraph("blockStart");
                    _isBlocking = true;
                    _blockDuration = 0.5f + (std::rand() % 35) / 100.0f;
                    _lastCombatActionTime = now;
                    _currentActionCooldown = _blockDuration;
                    SKSE::log::info("Combat AI: Blocking incoming attack from target {:08X}", target->GetFormID());
                    return;
                }

                // Offensive attack
                auto* avOwner = player->AsActorValueOwner();
                float stam = avOwner ? avOwner->GetActorValue(RE::ActorValue::kStamina) : 100.0f;
                float paRoll = static_cast<float>(std::rand() % 100) / 100.0f;
                if (stam > 35.0f && (paRoll < powerAttackMult * 0.5f * offensiveMult)) {
                    bool ok = RE::CombatAnimation::Execute(player, target, RE::CombatAnimation::ANIM::kActionRightPowerAttack);
                    if (!ok) {
                        player->NotifyAnimationGraph("attackPowerStart");
                    }
                    _currentActionCooldown = 1.35f;
                    SKSE::log::info("Combat AI: Executed power attack on target {:08X} (dist: {:.1f}, exec={})",
                        target->GetFormID(), fullDist, ok);
                } else {
                    bool ok = RE::CombatAnimation::Execute(player, target, RE::CombatAnimation::ANIM::kActionRightAttack);
                    if (!ok) {
                        player->NotifyAnimationGraph("attackStart");
                    }
                    _currentActionCooldown = 0.85f + (1.0f - offensiveMult) * 0.35f;
                    SKSE::log::info("Combat AI: Executed normal attack on target {:08X} (dist: {:.1f}, exec={})",
                        target->GetFormID(), fullDist, ok);
                }
                _lastCombatActionTime = now;
            }
        }
    }

}


