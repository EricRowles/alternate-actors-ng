#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
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

        // If no speaker specified, use active donor actor
        if (!a_speaker) {
            auto donorHandle = _activeDonorActor.get();
            if (donorHandle) {
                a_speaker = donorHandle.get();
            }
        }

        if (!a_speaker) {
            a_speaker = player;
        }

        auto* ui = RE::UI::GetSingleton();
        if (ui && (ui->GameIsPaused() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsModalMenuOpen())) {
            return false;
        }

        // When talking to donor actor, teleport directly to player and turn completely invisible
        if (a_speaker != player) {
            _activeProxySpeaker = a_speaker->GetHandle();

            // Teleport donor directly to player
            a_speaker->MoveTo(player);

            // Hide actor immediately after teleport
            HideActor(a_speaker);
        }

        // Enable dialogue permissions on both
        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        player->AllowPCDialogue(true);

        // Open native dialogue menu directly with the donor NPC
        bool opened = a_speaker->SetDialogueWithPlayer(true, false, nullptr);
        if (opened) {
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            if (topicMgr) {
                topicMgr->isGreetingPlayer = false;
            }
            if (a_speaker != player) {
                HideActor(a_speaker);
            }
            SKSE::log::info("Successfully opened proxy dialogue with donor actor {:08X}",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
            return true;
        } else {
            SKSE::log::warn("SetDialogueWithPlayer returned false for actor {:08X}, falling back to voice line",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
            return TriggerVoiceLine(a_speaker);
        }
    }

    bool IdleAIManager::TriggerVoiceLine(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!a_speaker) {
            auto donorHandle = _activeDonorActor.get();
            if (donorHandle) {
                a_speaker = donorHandle.get();
            } else {
                a_speaker = player;
            }
        }
        if (!a_speaker) return false;

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);

        static const RE::FormID candidateTopics[] = {
            0x00000142, // WIGreeting (Skyrim.esm)
            0x00013EBE, // DialogueGenericHello
            0x00013EBF, // DialogueGenericIdle
            0x00013EBD  // DialogueGenericGoodbye
        };

        for (auto id : candidateTopics) {
            auto* topic = RE::TESForm::LookupByID<RE::TESTopic>(id);
            if (topic && topic->topicInfos && topic->numTopicInfos > 0) {
                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (info && info->objConditions.IsTrue(a_speaker, player)) {
                        auto item = info->GetDialogueData(a_speaker);
                        if (!item.responses.empty()) {
                            auto* resp = item.responses.front();
                            if (resp && a_speaker->UpdateInDialogue(resp, false)) {
                                SKSE::log::info("Triggered on-demand dialogue voice line for actor {:08X} (topic {:08X})",
                                    static_cast<std::uint32_t>(a_speaker->GetFormID()),
                                    static_cast<std::uint32_t>(info->GetFormID()));
                                return true;
                            }
                        }
                    }
                }
            }
        }

        a_speaker->EvaluatePackage(true, true);
        return false;
    }
}
