#pragma once

#include <chrono>
#include "RE/B/BSTSmartPointer.h"
#include "RE/D/DialogueItem.h"
#include "RE/T/TESAIForm.h"

namespace RE {
    class TESObjectCELL;
    class TESCombatStyle;
    class TESClass;
}

namespace AlternateActors {

    class IdleAIManager : 
        public RE::BSTEventSink<RE::InputEvent*>,
        public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static IdleAIManager& GetSingleton() {
            static IdleAIManager instance;
            return instance;
        }

        void Initialize();
        void Update();

        // BSTEventSink<RE::InputEvent*>
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

        // BSTEventSink<RE::MenuOpenCloseEvent>
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

        void OnUserInput();
        void CheckIdleAI();
        void StartIdleAI();
        void StopIdleAI();

        void SetControllingNPC(bool a_controlling);
        bool IsControllingNPC() const { return _isControllingNPC; }

        void SetIdleAIEnabled(bool a_enabled) { _enabled = a_enabled; }
        bool IsIdleAIEnabled() const { return _enabled; }

        void SetCombatAIEnabled(bool a_enabled);
        bool IsCombatAIEnabled() const { return _combatAIEnabled; }

        void SetIdleTimeout(float a_seconds) { _idleTimeoutSeconds = a_seconds; }
        float GetIdleTimeout() const { return _idleTimeoutSeconds; }

        void SetActiveDonorActor(RE::Actor* a_donor);
        RE::Actor* GetActiveDonorActor() const { return _activeDonorActor.get().get(); }

        RE::TESCombatStyle* GetDonorCombatStyle() const { return _donorCombatStyle; }
        const RE::AIDATA_GAME& GetDonorAIData() const { return _donorAIData; }
        RE::TESClass* GetDonorClass() const { return _donorClass; }

        void UpdateCombatAI();
        RE::Actor* FindBestCombatTarget();

        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr);
        bool OpenSelfDialogueDirect(RE::Actor* a_speaker);
        bool TriggerVoiceLine(RE::Actor* a_speaker = nullptr);

        bool CheckAndTriggerIdleDialogue();
        void ClearCurrentDialogueItem() { _currentIdleDialogueItem.reset(); }

        bool IsAIActive() const { return _aiActive; }
        bool IsCombatAIActive() const { return _isCombatAIActive; }
        bool IsBowDrawn() const { return _bowDrawn; }
        bool IsInSelfDialogue() const;
        void SetInSelfDialogue(bool a_inSelf) { _inSelfDialogue = a_inSelf; }

        void InstallHooks();
        static void Hook_Projectile_Launch(struct _CONTEXT& a_ctx);

        void Clear() {
            StopIdleAI();
            _isControllingNPC = false;
            _inSelfDialogue = false;
            _activeDonorActor.reset();
            _donorCombatStyle = nullptr;
            _donorClass = nullptr;
            _donorAIData = {};
            _currentTarget.reset();
            _isBlocking = false;
            _bowDrawn = false;
            _isCombatAIActive = false;
            _lastCell = nullptr;
            _currentIdleDialogueItem.reset();
            _lastIdleEvalTime = {};
        }

    private:
        IdleAIManager() = default;
        ~IdleAIManager() override = default;
        IdleAIManager(const IdleAIManager&) = delete;
        IdleAIManager& operator=(const IdleAIManager&) = delete;

        bool _initialized{ false };
        bool _hooksInstalled{ false };
        bool _enabled{ false };
        bool _combatAIEnabled{ true };
        bool _isControllingNPC{ false };
        bool _aiActive{ false };
        bool _isCombatAIActive{ false };
        bool _inSelfDialogue{ false };
        float _idleTimeoutSeconds{ 10.0f };
        RE::ActorHandle _activeDonorActor;
        RE::TESCombatStyle* _donorCombatStyle{ nullptr };
        RE::TESClass* _donorClass{ nullptr };
        RE::AIDATA_GAME _donorAIData{};
        RE::ActorHandle _currentTarget;
        bool _isBlocking{ false };
        bool _bowDrawn{ false };
        float _blockDuration{ 0.0f };
        float _currentActionCooldown{ 0.0f };
        std::chrono::steady_clock::time_point _lastCombatActionTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _bowDrawTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastIdleChatterTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _lastIdleEvalTime{};
        RE::TESObjectCELL* _lastCell{ nullptr };
        RE::BSTSmartPointer<RE::DialogueItem> _currentIdleDialogueItem;
        bool _savedUserRunning{ true };
    };

}

