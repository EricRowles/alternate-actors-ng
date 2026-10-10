#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"

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
                if (!ImpersonationManager::GetSingleton().IsPlayerSpeaking()) {
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
        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);
        player->SetAIDriven(true);
        player->EvaluatePackage(true, true);
        SKSE::log::info("Player idle timeout reached - engaged autonomous donor AI package pathing.");
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
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager controlling NPC set to: {}", a_controlling);
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

        if (_aiActive) {
            StopIdleAI();
        }

        // Enable dialogue permissions on both
        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);

        _inSelfDialogue = true;

        // Open native dialogue menu directly with the speaker (the puppet)
        bool opened = a_speaker->SetDialogueWithPlayer(true, false, nullptr);
        if (opened) {
            SKSE::log::info("Successfully opened self-dialogue with actor {:08X} (isPuppet: {})",
                static_cast<std::uint32_t>(a_speaker->GetFormID()), (a_speaker == puppet));
            return true;
        } else {
            SKSE::log::warn("SetDialogueWithPlayer returned false for actor {:08X}, attempting force-greet fallback",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
            bool greeted = a_speaker->SetDialogueWithPlayer(true, true, nullptr);
            if (!greeted) {
                _inSelfDialogue = false;
            }
            return greeted;
        }
    }

    bool IdleAIManager::IsInSelfDialogue() const {
        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        if (!puppet) return false;

        auto* ui = RE::UI::GetSingleton();
        bool dialogueMenuOpen = ui && ui->IsMenuOpen("Dialogue Menu"sv);

        if (dialogueMenuOpen) {
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            if (topicMgr && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                return true;
            }
            if (_inSelfDialogue) {
                return true;
            }
        }

        if (_inSelfDialogue) {
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            if (topicMgr && topicMgr->speaker == puppet->GetHandle()) {
                return true;
            }
            if (ImpersonationManager::GetSingleton().IsPlayerSpeaking()) {
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

}
