#pragma once

#include <chrono>
#include <functional>
#include <vector>
#include <RE/T/TESTopic.h>

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

        void SetDialogueHotkey(std::int32_t a_key) { _dialogueHotkey = a_key; }
        std::int32_t GetDialogueHotkey() const { return _dialogueHotkey; }

        void SetActiveDonorActor(RE::Actor* a_donor) {
            _activeDonorActor = a_donor ? a_donor->GetHandle() : RE::ActorHandle();
            _dialogueCache.Clear();
        }
        RE::Actor* GetActiveDonorActor() const { return _activeDonorActor.get().get(); }

        bool OpenSelfDialogue(RE::Actor* a_speaker = nullptr);
        bool TriggerVoiceLine(RE::Actor* a_speaker = nullptr);
        bool PlayMorphedDialogue(RE::DIALOGUE_DATA::Subtype a_subtype);
        void BuildDialogueCache();
        void ClearDialogueCache() { _dialogueCache.Clear(); }

        void HideActor(RE::Actor* a_actor);
        void ShowActor(RE::Actor* a_actor);

        bool IsAIActive() const { return _aiActive; }

        void Clear() {
            StopIdleAI();
            _isControllingNPC = false;
            _inSelfDialogue = false;
            _idleCommentElapsed = 0.0f;
            _activeProxySpeaker.reset();
            _dialogueCache.Clear();
        }

    private:
        IdleAIManager() = default;
        ~IdleAIManager() override = default;
        IdleAIManager(const IdleAIManager&) = delete;
        IdleAIManager& operator=(const IdleAIManager&) = delete;

        struct MorphedDialogueCache {
            std::vector<RE::TESTopic*> donorHelloTopics;
            std::vector<RE::TESTopic*> donorGoodbyeTopics;
            std::vector<RE::TESTopic*> donorIdleTopics;

            std::vector<RE::TESTopic*> genericHelloTopics;
            std::vector<RE::TESTopic*> genericGoodbyeTopics;
            std::vector<RE::TESTopic*> genericIdleTopics;

            RE::FormID cachedDonorFormID{ 0 };
            bool built{ false };

            void Clear() {
                donorHelloTopics.clear();
                donorGoodbyeTopics.clear();
                donorIdleTopics.clear();
                genericHelloTopics.clear();
                genericGoodbyeTopics.clear();
                genericIdleTopics.clear();
                cachedDonorFormID = 0;
                built = false;
            }
        };

        bool _initialized{ false };
        bool _enabled{ false };
        bool _isControllingNPC{ false };
        bool _aiActive{ false };
        bool _inSelfDialogue{ false };
        float _idleTimeoutSeconds{ 10.0f };
        float _idleCommentElapsed{ 0.0f };
        float _nextIdleCommentInterval{ 60.0f };
        std::int32_t _dialogueHotkey{ -1 }; // Disabled by default; dummy activation triggers self-dialogue
        RE::ActorHandle _activeDonorActor;
        RE::ActorHandle _activeProxySpeaker;
        MorphedDialogueCache _dialogueCache;
        std::chrono::steady_clock::time_point _lastInputTime{ std::chrono::steady_clock::now() };
    };

}
