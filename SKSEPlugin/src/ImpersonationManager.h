#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <cstdint>

namespace AlternateActors {

    struct ImpersonationOptions {
        bool npcVoice{ true };
        bool puppetFollowsPlayer{ true };
    };

    struct RetargetedPackageTarget {
        RE::TESPackage* package{ nullptr };
        RE::FormID packageFormID{ 0 };
        std::uint8_t targType{ 0 };
        RE::ObjectRefHandle origHandle;
        RE::TESForm* origObject{ nullptr };
    };

    class ImpersonationManager {
    public:
        static ImpersonationManager& GetSingleton() {
            static ImpersonationManager instance;
            return instance;
        }

        void Initialize();
        std::int32_t Begin(RE::Actor* a_donor, RE::Actor* a_player);
        std::int32_t End();
        void Tick();

        bool IsActive() const { return _active.load(std::memory_order_acquire); }
        RE::Actor* GetDonor() const {
            if (!_active.load(std::memory_order_acquire)) return nullptr;
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto handle = _donorHandle.get();
            return handle ? handle.get() : nullptr;
        }
        RE::Actor* GetPlayer() const { return _playerActor.load(std::memory_order_acquire); }

        std::string GetDonorName() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _donorName;
        }
        std::string GetOrigPlayerName() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _origPlayerName;
        }
        RE::BGSVoiceType* GetDonorVoiceType() const { return _donorVoiceType.load(std::memory_order_acquire); }
        RE::BGSVoiceType* GetOrigPlayerVoiceType() const { return _origPlayerVoiceType.load(std::memory_order_acquire); }
        bool IsPuppetBase(const RE::TESNPC* a_npc) const {
            if (!a_npc) return false;
            return a_npc == _puppetBaseMale.load(std::memory_order_acquire) ||
                   a_npc == _puppetBaseFemale.load(std::memory_order_acquire);
        }
        bool IsPuppetBase(const RE::TESForm* a_form) const {
            if (!a_form) return false;
            return a_form == static_cast<const RE::TESForm*>(_puppetBaseMale.load(std::memory_order_acquire)) ||
                   a_form == static_cast<const RE::TESForm*>(_puppetBaseFemale.load(std::memory_order_acquire));
        }
        void SetPuppet(RE::Actor* a_puppet);
        RE::Actor* GetPuppet() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto handle = _puppetHandle.get();
            return handle ? handle.get() : nullptr;
        }

        ImpersonationOptions GetOptions() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _options;
        }
        bool IsConditionsEnabled() const { return _active.load(std::memory_order_acquire); }

        void SetNPCVoiceEnabled(bool a_enabled) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _options.npcVoice = a_enabled;
            _npcVoiceEnabled.store(a_enabled, std::memory_order_release);
        }
        bool IsNPCVoiceEnabled() const { return _npcVoiceEnabled.load(std::memory_order_acquire); }


        void SetPuppetFollowsPlayer(bool a_follows);
        bool DoesPuppetFollowPlayer() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _options.puppetFollowsPlayer;
        }


        void Save(SKSE::SerializationInterface* a_serde);
        void Load(SKSE::SerializationInterface* a_serde, std::uint32_t a_length);
        void OnRevert();
        void OnPostLoadGame();

        static RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc);

        void ResetSpeechTracking();
        bool CheckPlayerSpeakingDone();
        void OnDialogueResponseStarted(RE::DialogueResponse* a_response);
        bool IsPlayerSpeaking() const { return _playerSpeaking.load(std::memory_order_acquire); }
        void SetPlayerSpeaking(bool a_speaking) { _playerSpeaking.store(a_speaking, std::memory_order_release); }
        void CompleteSpeaking(RE::Actor* a_player);
        bool IsInCompleteSpeaking() const { return _inCompleteSpeaking.load(std::memory_order_acquire); }
        static void AttachSoundHandlesToHead(RE::Actor* a_actor);
        bool HandleProxyDialogueOutput(RE::Actor* a_speaker, RE::DialogueResponse* a_response, bool a_unused);
        void SyncDonorUpdatesToPlayer();
        void ClearPlayerPackages();
        RE::ObjectRefHandle GetCloneMarker() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _cloneMarker;
        }

    private:
        ImpersonationManager() = default;
        ~ImpersonationManager() = default;
        ImpersonationManager(const ImpersonationManager&) = delete;
        ImpersonationManager& operator=(const ImpersonationManager&) = delete;

        void InstallHooks();
        static void Hook_ForceRefIntoAlias(struct _CONTEXT& a_ctx);
        static RE::BGSVoiceType* Hook_GetObjectVoiceType(const RE::TESNPC* a_this);
        inline static REL::Relocation<decltype(Hook_GetObjectVoiceType)> _origGetObjectVoiceType;

        static bool Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        using UpdateInDialogue_t = bool(RE::Actor*, RE::DialogueResponse*, bool);
        inline static REL::Relocation<UpdateInDialogue_t> _origCharUpdateInDialogue;

        static bool Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        inline static REL::Relocation<UpdateInDialogue_t> _origPlayerUpdateInDialogue;

        static bool Hook_Character_QSpeakingDone(const RE::Actor* a_this);
        using QSpeakingDone_t = bool(const RE::Actor*);
        inline static REL::Relocation<QSpeakingDone_t> _origCharQSpeakingDone;

        static bool Hook_Player_QSpeakingDone(const RE::Actor* a_this);
        inline static REL::Relocation<QSpeakingDone_t> _origPlayerQSpeakingDone;

        static void Hook_Character_SetSpeakingDone(RE::Actor* a_this, bool a_set);
        using SetSpeakingDone_t = void(RE::Actor*, bool);
        inline static REL::Relocation<SetSpeakingDone_t> _origCharSetSpeakingDone;

        static void Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set);
        inline static REL::Relocation<SetSpeakingDone_t> _origPlayerSetSpeakingDone;

        static bool Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic);
        using SetDialogueWithPlayer_t = bool(RE::Actor*, bool, bool, RE::TESTopicInfo*);
        inline static REL::Relocation<SetDialogueWithPlayer_t> _origSetDialogueWithPlayer;

        using UpdateAnimation_t = void(RE::Actor*, float);
        static void Hook_Player_UpdateAnimation(RE::Actor* a_this, float a_delta);
        inline static REL::Relocation<UpdateAnimation_t> _origPlayerUpdateAnimation;

        static RE::BGSLocation* Hook_Player_GetEditorLocation1(const RE::TESObjectREFR* a_this);
        using GetEditorLocation1_t = RE::BGSLocation*(const RE::TESObjectREFR*);
        inline static REL::Relocation<GetEditorLocation1_t> _origPlayerGetEditorLocation1;

        static bool Hook_Player_GetEditorLocation2(
            RE::TESObjectREFR* a_this,
            RE::NiPoint3& a_outPos,
            RE::NiPoint3& a_outRot,
            RE::TESForm*& a_outWorldOrCell,
            RE::TESObjectCELL* a_fallback);
        using GetEditorLocation2_t = bool(RE::TESObjectREFR*, RE::NiPoint3&, RE::NiPoint3&, RE::TESForm*&, RE::TESObjectCELL*);
        inline static REL::Relocation<GetEditorLocation2_t> _origPlayerGetEditorLocation2;


        mutable std::recursive_mutex _mutex;
        std::atomic<bool> _active{ false };
        std::atomic<RE::Actor*> _donorActor{ nullptr };
        std::atomic<RE::Actor*> _playerActor{ nullptr };
        RE::NiPointer<RE::Actor> _donorKeepAlive;
        RE::ActorHandle _donorHandle;
        RE::FormID _savedDonorFormID{ 0 };
        std::atomic<RE::BGSVoiceType*> _donorVoiceType{ nullptr };
        std::atomic<RE::BGSVoiceType*> _origPlayerVoiceType{ nullptr };
        std::atomic<RE::TESNPC*> _puppetBaseMale{ nullptr };
        std::atomic<RE::TESNPC*> _puppetBaseFemale{ nullptr };
        std::atomic<bool> _npcVoiceEnabled{ true };
        std::string _donorName;
        std::string _origPlayerName;
        RE::ActorHandle _puppetHandle;
        RE::ObjectRefHandle _cloneMarker;

        std::atomic<bool> _playerSpeaking{ false };
        std::atomic<bool> _inCompleteSpeaking{ false };
        std::atomic<bool> _speechHadSound{ false };
        std::chrono::steady_clock::time_point _speechStartTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _speechAudioEndTime{ std::chrono::steady_clock::time_point{} };
        float _speechMinDuration{ 2.0f };
        std::uint32_t _dialogueSoundID{ static_cast<std::uint32_t>(-1) };

        ImpersonationOptions _options;
        std::vector<RetargetedPackageTarget> _retargetedPackages;
        bool _hooksInstalled{ false };
    };



}
