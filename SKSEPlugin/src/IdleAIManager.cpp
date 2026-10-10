#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include <cmath>
#include <cstdlib>
#include <RE/T/TESTopic.h>
#include <RE/T/TESTopicInfo.h>
#include <RE/D/DialogueItem.h>
#include <RE/T/TESDataHandler.h>

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
        ImpersonationManager::GetSingleton().Tick();
        if (auto speakerHandle = _activeProxySpeaker.get()) {
            auto* ui = RE::UI::GetSingleton();
            if (ui && ui->IsMenuOpen("Dialogue Menu"sv)) {
                HideActor(speakerHandle.get());
            }
        }

        if (_isControllingNPC && ImpersonationManager::GetSingleton().IsActive()) {
            _idleCommentElapsed += 1.0f;
            if (_idleCommentElapsed >= _nextIdleCommentInterval) {
                auto* ui = RE::UI::GetSingleton();
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (ui && !ui->GameIsPaused() && !ui->IsMenuOpen("Dialogue Menu"sv) &&
                    !ui->IsMenuOpen("Console"sv) && !ui->IsMenuOpen("Loading Menu"sv) &&
                    !ui->IsModalMenuOpen() && player && !player->IsInCombat() &&
                    !player->IsSneaking() && player->QSpeakingDone()) {

                    bool success = PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype::kIdle);
                    _idleCommentElapsed = 0.0f;
                    if (success) {
                        _nextIdleCommentInterval = 45.0f + static_cast<float>(std::rand() % 45);
                    } else {
                        _nextIdleCommentInterval = 30.0f;
                    }
                }
            }
        }
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
                if (button->IsDown()) {
                    if (_dialogueHotkey > 0 && button->GetIDCode() == static_cast<std::uint32_t>(_dialogueHotkey)) {
                        OpenSelfDialogue(nullptr);
                    }
                }
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
            if (a_event->opening) {
                if (auto speakerHandle = _activeProxySpeaker.get()) {
                    HideActor(speakerHandle.get());
                }
            } else {
                if (auto speakerHandle = _activeProxySpeaker.get()) {
                    auto* speaker = speakerHandle.get();
                    ShowActor(speaker);
                    _activeProxySpeaker.reset();
                    SKSE::log::info("Dialogue Menu closed - restored proxy speaker visibility.");
                }
                if (_inSelfDialogue) {
                    _inSelfDialogue = false;
                    PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype::kGoodBye);
                }
            }
        } else if (a_event->menuName == "RaceSex Menu"sv && a_event->opening) {
            if (_isControllingNPC || ImpersonationManager::GetSingleton().IsActive()) {
                SKSE::log::warn("RaceSex Menu opened while morphed into NPC! Forcibly closing immediately to protect custom head meshes.");
                auto* msgQueue = RE::UIMessageQueue::GetSingleton();
                if (msgQueue) {
                    msgQueue->AddMessage("RaceSex Menu"sv, RE::UI_MESSAGE_TYPE::kHide, nullptr);
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

        auto* base = player->GetActorBase();
        auto* npc = skyrim_cast<RE::TESNPC*>(base);
        if (npc) {
            auto* defaultSandbox = RE::TESForm::LookupByID<RE::TESPackage>(0x0001B619); // DefaultSandboxCurrentLocation1024
            if (defaultSandbox) {
                bool hasSandbox = false;
                for (auto* pkg : npc->aiPackages.packages) {
                    if (pkg == defaultSandbox) { hasSandbox = true; break; }
                }
                if (!hasSandbox) {
                    if (npc->aiPackages.packages.empty()) {
                        npc->aiPackages.packages.push_front(defaultSandbox);
                    } else {
                        auto last = npc->aiPackages.packages.begin();
                        for (auto it = npc->aiPackages.packages.begin(); it != npc->aiPackages.packages.end(); ++it) {
                            last = it;
                        }
                        npc->aiPackages.packages.insert_after(last, defaultSandbox);
                    }
                }
            }
        }

        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);
        player->SetAIDriven(true);
        player->EvaluatePackage(true, true);
        SKSE::log::info("Player idle timeout reached - engaged autonomous AI pathing.");
    }

    void IdleAIManager::StopIdleAI() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->AllowPCDialogue(false);
            player->SetAIDriven(false);
            player->EvaluatePackage(false, false);
            if (_aiActive) {
                SKSE::log::info("Player input detected - disengaged autonomous AI pathing.");
            }
        }
        _aiActive = false;
    }

    void IdleAIManager::SetControllingNPC(bool a_controlling) {
        _isControllingNPC = a_controlling;
        if (!_isControllingNPC) {
            StopIdleAI();
            _inSelfDialogue = false;
            _idleCommentElapsed = 0.0f;
            _dialogueCache.Clear();
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager controlling NPC set to: {}", a_controlling);
    }

    void IdleAIManager::HideActor(RE::Actor* a_actor) {
        if (!a_actor) return;

        a_actor->SetAlpha(0.0f);
        a_actor->UpdateAlpha();

        auto* root3D = a_actor->Get3D();
        if (root3D) {
            std::function<void(RE::NiAVObject*)> cullRecursive = [&](RE::NiAVObject* obj) {
                if (!obj) return;
                obj->SetAppCulled(true);
                obj->CullGeometry(true);
                obj->CullNode(true);
                if (auto* node = obj->AsNode()) {
                    for (auto& child : node->GetChildren()) {
                        if (child) {
                            cullRecursive(child.get());
                        }
                    }
                }
            };
            cullRecursive(root3D);
        }
    }

    void IdleAIManager::ShowActor(RE::Actor* a_actor) {
        if (!a_actor) return;

        a_actor->SetAlpha(1.0f);
        a_actor->UpdateAlpha();

        auto* root3D = a_actor->Get3D();
        if (root3D) {
            std::function<void(RE::NiAVObject*)> uncullRecursive = [&](RE::NiAVObject* obj) {
                if (!obj) return;
                obj->SetAppCulled(false);
                obj->CullGeometry(false);
                obj->CullNode(false);
                if (auto* node = obj->AsNode()) {
                    for (auto& child : node->GetChildren()) {
                        if (child) {
                            uncullRecursive(child.get());
                        }
                    }
                }
            };
            uncullRecursive(root3D);
        }
    }

    bool IdleAIManager::OpenSelfDialogue(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        if (!a_speaker && puppet) {
            a_speaker = puppet;
        }
        if (!a_speaker) {
            a_speaker = player;
        }

        auto* ui = RE::UI::GetSingleton();
        if (ui && (ui->GameIsPaused() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsModalMenuOpen())) {
            return false;
        }

        // Enable dialogue permissions on both
        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);

        // Open native dialogue menu directly with the speaker (the puppet)
        bool opened = a_speaker->SetDialogueWithPlayer(true, false, nullptr);
        if (opened) {
            _inSelfDialogue = true;
            SKSE::log::info("Successfully opened dialogue with actor {:08X} (isPuppet: {})",
                static_cast<std::uint32_t>(a_speaker->GetFormID()), (a_speaker == puppet));
            return true;
        } else {
            _inSelfDialogue = false;
            SKSE::log::warn("SetDialogueWithPlayer returned false for actor {:08X}, triggering hello voice line directly",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
            return PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype::kHello);
        }
    }

    bool IdleAIManager::TriggerVoiceLine(RE::Actor* /*a_speaker*/) {
        if (PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype::kIdle)) {
            return true;
        }
        return PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype::kHello);
    }

    void IdleAIManager::BuildDialogueCache() {
        _dialogueCache.Clear();

        auto* donor = GetActiveDonorActor();
        if (!donor) return;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        const auto& allTopics = dataHandler->GetFormArray<RE::TESTopic>();
        if (allTopics.empty()) return;

        const RE::TESFile* donorBaseFile = donor->GetActorBase() ? donor->GetActorBase()->GetFile(0) : nullptr;
        const RE::TESFile* donorRefFile = donor->GetFile(0);
        auto* donorVoice = ImpersonationManager::GetSingleton().GetDonorVoiceType();
        if (!donorVoice && donor->GetActorBase()) {
            donorVoice = ImpersonationManager::GetNPCVoiceType(skyrim_cast<RE::TESNPC*>(donor->GetActorBase()));
        }
        auto* donorBase = donor->GetActorBase();

        auto isTopicDonorSpecific = [&](RE::TESTopic* topic) -> bool {
            if (!topic) return false;
            auto* topicFile = topic->GetFile(0);
            if (topicFile && (topicFile == donorBaseFile || topicFile == donorRefFile)) {
                return true;
            }
            if (topic->ownerQuest) {
                auto* questFile = topic->ownerQuest->GetFile(0);
                if (questFile && (questFile == donorBaseFile || questFile == donorRefFile)) {
                    return true;
                }
            }
            if (topic->topicInfos && topic->numTopicInfos > 0) {
                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (!info) continue;
                    for (auto* item = info->objConditions.head; item; item = item->next) {
                        auto funcID = item->data.functionData.function.get();
                        if (funcID == RE::FUNCTION_DATA::FunctionID::kGetIsVoiceType) {
                            if (donorVoice && item->data.functionData.params[0] == donorVoice) {
                                return true;
                            }
                        } else if (funcID == RE::FUNCTION_DATA::FunctionID::kGetIsID) {
                            if (donorBase && item->data.functionData.params[0] == donorBase) {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        };

        for (auto* topic : allTopics) {
            if (!topic) continue;
            auto subtype = topic->data.subtype.get();
            if (subtype != RE::DIALOGUE_DATA::Subtype::kHello &&
                subtype != RE::DIALOGUE_DATA::Subtype::kGoodBye &&
                subtype != RE::DIALOGUE_DATA::Subtype::kIdle) {
                continue;
            }

            bool isDonor = isTopicDonorSpecific(topic);
            if (subtype == RE::DIALOGUE_DATA::Subtype::kHello) {
                if (isDonor) _dialogueCache.donorHelloTopics.push_back(topic);
                else _dialogueCache.genericHelloTopics.push_back(topic);
            } else if (subtype == RE::DIALOGUE_DATA::Subtype::kGoodBye) {
                if (isDonor) _dialogueCache.donorGoodbyeTopics.push_back(topic);
                else _dialogueCache.genericGoodbyeTopics.push_back(topic);
            } else if (subtype == RE::DIALOGUE_DATA::Subtype::kIdle) {
                if (isDonor) _dialogueCache.donorIdleTopics.push_back(topic);
                else _dialogueCache.genericIdleTopics.push_back(topic);
            }
        }

        _dialogueCache.cachedDonorFormID = donor->GetFormID();
        _dialogueCache.built = true;

        SKSE::log::info("Built dialogue cache for donor {:08X}: Hello({}/{}), GoodBye({}/{}), Idle({}/{})",
            static_cast<std::uint32_t>(donor->GetFormID()),
            _dialogueCache.donorHelloTopics.size(), _dialogueCache.genericHelloTopics.size(),
            _dialogueCache.donorGoodbyeTopics.size(), _dialogueCache.genericGoodbyeTopics.size(),
            _dialogueCache.donorIdleTopics.size(), _dialogueCache.genericIdleTopics.size());
    }

    bool IdleAIManager::PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype a_subtype) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        if (!player->QSpeakingDone()) {
            return false;
        }

        auto* donor = GetActiveDonorActor();
        if (!donor) return false;

        if (!_dialogueCache.built || _dialogueCache.cachedDonorFormID != donor->GetFormID()) {
            BuildDialogueCache();
        }

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        RE::Actor* listener = puppet ? puppet : player;

        const auto& donorTopics = (a_subtype == RE::DIALOGUE_DATA::Subtype::kHello) ? _dialogueCache.donorHelloTopics :
                                  (a_subtype == RE::DIALOGUE_DATA::Subtype::kGoodBye) ? _dialogueCache.donorGoodbyeTopics :
                                  _dialogueCache.donorIdleTopics;

        const auto& genericTopics = (a_subtype == RE::DIALOGUE_DATA::Subtype::kHello) ? _dialogueCache.genericHelloTopics :
                                    (a_subtype == RE::DIALOGUE_DATA::Subtype::kGoodBye) ? _dialogueCache.genericGoodbyeTopics :
                                    _dialogueCache.genericIdleTopics;

        struct Candidate {
            RE::TESTopicInfo* info{ nullptr };
            RE::DialogueResponse* resp{ nullptr };
            bool onPlayer{ true };
        };
        std::vector<Candidate> candidates;

        auto evaluateTopics = [&](const std::vector<RE::TESTopic*>& a_topicList, bool a_isDonorGroup) {
            for (auto* topic : a_topicList) {
                if (!topic) continue;
                if (topic->ownerQuest && topic->ownerQuest->IsStopped()) continue;
                if (!topic->topicInfos || topic->numTopicInfos == 0) continue;

                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (!info) continue;
                    if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                        continue;
                    }

                    // Condition check
                    bool condPass = false;
                    if (!info->objConditions.head) {
                        condPass = a_isDonorGroup;
                    } else {
                        if (info->objConditions.IsTrue(player, listener)) {
                            condPass = true;
                        } else if (donor && info->objConditions.IsTrue(donor, player)) {
                            condPass = true;
                        }
                    }

                    if (!condPass) continue;

                    auto item = info->GetDialogueData(player);
                    bool canUsePlayer = true;
                    if (item.responses.empty() && donor) {
                        item = info->GetDialogueData(donor);
                        canUsePlayer = false;
                    }

                    if (!item.responses.empty()) {
                        auto* resp = item.responses.front();
                        if (resp) {
                            candidates.push_back({ info, resp, canUsePlayer });
                        }
                    }
                }
            }
        };

        // 1. Try donor-specific topics first
        evaluateTopics(donorTopics, true);

        // 2. Fall back to generic topics if no donor-specific candidates match
        if (candidates.empty()) {
            evaluateTopics(genericTopics, false);
        }

        if (candidates.empty()) {
            return false;
        }

        // Pick candidate (random if multiple)
        std::size_t chosenIdx = 0;
        if (candidates.size() > 1) {
            chosenIdx = static_cast<std::size_t>(std::rand()) % candidates.size();
        }
        const auto& chosen = candidates[chosenIdx];

        bool spoken = false;
        if (chosen.onPlayer) {
            spoken = player->UpdateInDialogue(chosen.resp, false);
        }

        if (spoken) {
            if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                chosen.info->saidOnce = true;
            }
            SKSE::log::info("Triggered dialogue subtype {} (topic {:08X}, info {:08X})",
                static_cast<std::uint32_t>(a_subtype),
                chosen.info->parentTopic ? static_cast<std::uint32_t>(chosen.info->parentTopic->GetFormID()) : 0,
                static_cast<std::uint32_t>(chosen.info->GetFormID()));
            return true;
        }

        return false;
    }
}
