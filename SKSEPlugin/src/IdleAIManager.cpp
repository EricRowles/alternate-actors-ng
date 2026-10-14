#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "AliasManager.h"
#include <random>

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

            _initialized = true;
        }
    }

    void IdleAIManager::Update() {
        CheckIdleAI();
        CheckAndTriggerIdleDialogue();
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
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
                auto* donor = ImpersonationManager::GetSingleton().GetDonor();
                bool topicActive = false;
                if (topicMgr) {
                    if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                        topicActive = true;
                    }
                    if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                        topicActive = true;
                    }
                }
                if (!ImpersonationManager::GetSingleton().IsPlayerSpeaking() && !topicActive) {
                    _inSelfDialogue = false;
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnUserInput() {
        _lastInputTime = std::chrono::steady_clock::now();
        if (_aiActive) {
            StopIdleAI();
        }
    }

    void IdleAIManager::CheckIdleAI() {
        if (!_enabled || !_isControllingNPC) {
            if (_aiActive) {
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
            if (_aiActive) {
                StopIdleAI();
            }
            return;
        }

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _lastInputTime).count();

        if (elapsed >= _idleTimeoutSeconds && !_aiActive) {
            StartIdleAI();
        }
    }

    void IdleAIManager::StartIdleAI() {
        if (!_isControllingNPC || !_enabled) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        _aiActive = true;

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

    void IdleAIManager::StopIdleAI() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->SetAIDriven(false);
            player->EvaluatePackage(false, false);

            if (_aiActive) {
                SKSE::log::info("Player input detected - disengaged autonomous AI pathing.");
            }
        }
        _aiActive = false;
        _lastInputTime = std::chrono::steady_clock::now();
    }

    void IdleAIManager::SetControllingNPC(bool a_controlling) {
        _isControllingNPC = a_controlling;
        if (!_isControllingNPC) {
            StopIdleAI();
            _inSelfDialogue = false;
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

        if (_aiActive) {
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

        _inSelfDialogue = true;

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
                _inSelfDialogue = false;
            }
            return greeted;
        }
    }

    bool IdleAIManager::IsInSelfDialogue() const {
        if (_inSelfDialogue) {
            return true;
        }

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!puppet && !donor) return false;

        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr) {
            if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                return true;
            }
            if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                return true;
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
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!_isControllingNPC && !impMgr.IsActive()) {
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

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return false;

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
            if (quest->objConditions.head && !SafeConditionCheck(&quest->objConditions, player, targetRef)) {
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
            _lastIdleChatterTime = now;
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

        _lastIdleChatterTime = now;
        return false;
    }

}


