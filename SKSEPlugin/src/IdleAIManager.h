#pragma once

#include <chrono>

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

        void SetIdleTimeout(float a_seconds) { _idleTimeoutSeconds = a_seconds; }
        float GetIdleTimeout() const { return _idleTimeoutSeconds; }

        void SetActiveDonorActor(RE::Actor* a_donor) {
            _activeDonorActor = a_donor ? a_donor->GetHandle() : RE::ActorHandle();
        }
        RE::Actor* GetActiveDonorActor() const { return _activeDonorActor.get().get(); }

        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr);
        bool TriggerVoiceLine(RE::Actor* a_speaker = nullptr);

        bool IsAIActive() const { return _aiActive; }
        bool IsInSelfDialogue() const;
        void SetInSelfDialogue(bool a_inSelf) { _inSelfDialogue = a_inSelf; }

        void Clear() {
            StopIdleAI();
            _isControllingNPC = false;
            _inSelfDialogue = false;
            _activeDonorActor.reset();
        }

    private:
        IdleAIManager() = default;
        ~IdleAIManager() override = default;
        IdleAIManager(const IdleAIManager&) = delete;
        IdleAIManager& operator=(const IdleAIManager&) = delete;

        bool _initialized{ false };
        bool _enabled{ false };
        bool _isControllingNPC{ false };
        bool _aiActive{ false };
        bool _inSelfDialogue{ false };
        float _idleTimeoutSeconds{ 10.0f };
        RE::ActorHandle _activeDonorActor;
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
    };

}
