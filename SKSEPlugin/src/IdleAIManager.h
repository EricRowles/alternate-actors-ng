#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
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
        bool IsControllingNPC() const { return _isControllingNPC.load(std::memory_order_acquire); }

        void SetIdleAIEnabled(bool a_enabled) { _enabled.store(a_enabled, std::memory_order_release); }
        bool IsIdleAIEnabled() const { return _enabled.load(std::memory_order_acquire); }

        void SetCombatAIEnabled(bool a_enabled);
        bool IsCombatAIEnabled() const { return _combatAIEnabled.load(std::memory_order_acquire); }

        void SetIdleTimeout(float a_seconds) { _idleTimeoutSeconds.store(a_seconds, std::memory_order_release); }
        float GetIdleTimeout() const { return _idleTimeoutSeconds.load(std::memory_order_acquire); }

        void SetActiveDonorActor(RE::Actor* a_donor);
        RE::Actor* GetActiveDonorActor() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _activeDonorActor.get().get();
        }

        RE::TESCombatStyle* GetDonorCombatStyle() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _donorCombatStyle;
        }
        RE::AIDATA_GAME GetDonorAIData() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _donorAIData;
        }
        RE::TESClass* GetDonorClass() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _donorClass;
        }

        void UpdateCombatAI();
        RE::Actor* FindBestCombatTarget();

        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr);
        bool OpenSelfDialogueDirect(RE::Actor* a_speaker);
        bool TriggerVoiceLine(RE::Actor* a_speaker = nullptr);

        bool CheckAndTriggerIdleDialogue();
        void ClearCurrentDialogueItem() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _currentIdleDialogueItem.reset();
        }

        bool IsAIActive() const { return _aiActive.load(std::memory_order_acquire); }
        bool IsCombatAIActive() const { return _isCombatAIActive.load(std::memory_order_acquire); }
        bool IsBowDrawn() const { return _bowDrawn.load(std::memory_order_acquire); }
        bool IsInSelfDialogue() const;
        void SetInSelfDialogue(bool a_inSelf) { _inSelfDialogue.store(a_inSelf, std::memory_order_release); }

        void InstallHooks();
        static void Hook_Projectile_Launch(struct _CONTEXT& a_ctx);

        void Clear() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            StopIdleAI();
            _isControllingNPC.store(false, std::memory_order_release);
            _inSelfDialogue.store(false, std::memory_order_release);
            _activeDonorActor.reset();
            _donorCombatStyle = nullptr;
            _donorClass = nullptr;
            _donorAIData = {};
            _currentTarget.reset();
            _isBlocking = false;
            _bowDrawn.store(false, std::memory_order_release);
            _isCombatAIActive.store(false, std::memory_order_release);
            _lastCell = nullptr;
            _currentIdleDialogueItem.reset();
            _lastIdleEvalTime = {};
        }

    private:
        IdleAIManager() = default;
        ~IdleAIManager() override = default;
        IdleAIManager(const IdleAIManager&) = delete;
        IdleAIManager& operator=(const IdleAIManager&) = delete;

        mutable std::recursive_mutex _mutex;
        bool _initialized{ false };
        bool _hooksInstalled{ false };
        std::atomic<bool> _enabled{ false };
        std::atomic<bool> _combatAIEnabled{ true };
        std::atomic<bool> _isControllingNPC{ false };
        std::atomic<bool> _aiActive{ false };
        std::atomic<bool> _isCombatAIActive{ false };
        std::atomic<bool> _inSelfDialogue{ false };
        std::atomic<float> _idleTimeoutSeconds{ 10.0f };
        RE::ActorHandle _activeDonorActor;
        RE::TESCombatStyle* _donorCombatStyle{ nullptr };
        RE::TESClass* _donorClass{ nullptr };
        RE::AIDATA_GAME _donorAIData{};
        RE::ActorHandle _currentTarget;
        bool _isBlocking{ false };
        std::atomic<bool> _bowDrawn{ false };
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

