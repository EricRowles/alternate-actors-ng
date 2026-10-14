#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

namespace AlternateActors {

    struct ImpersonationOptions {
        bool conditions{ true };
        bool aliases{ true };
        bool packages{ true };
        bool scripts{ false };
    };

    struct PapyrusRedirectRecord {
        RE::VMHandle holderHandle{ 0 };
        std::string scriptName;
        std::uint32_t varIndex{ 0 };
    };

    enum class PackageRetargetType : std::uint8_t {
        kPackLocHandle,
        kPackLocObject,
        kPackTargHandle,
        kPackTargObject,
        kCustomLocHandle,
        kCustomLocObject,
        kCustomTargHandle,
        kCustomTargObject
    };

    struct PackageRetargetRecord {
        RE::TESPackage* package{ nullptr };
        PackageRetargetType type{ PackageRetargetType::kPackLocHandle };
        void* targetPtr{ nullptr };
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

        bool IsActive() const { return _active.load(std::memory_order_relaxed); }
        RE::Actor* GetDonor() const {
            if (!_active.load(std::memory_order_relaxed)) return nullptr;
            auto handle = _donorHandle.get();
            return handle ? handle.get() : nullptr;
        }
        RE::Actor* GetPlayer() const { return _playerActor.load(std::memory_order_relaxed); }

        const std::string& GetDonorName() const { return _donorName; }
        const std::string& GetOrigPlayerName() const { return _origPlayerName; }
        RE::BGSVoiceType* GetDonorVoiceType() const { return _donorVoiceType; }
        void SetPuppet(RE::Actor* a_puppet);
        RE::Actor* GetPuppet() const {
            auto handle = _puppetHandle.get();
            return handle ? handle.get() : nullptr;
        }

        const ImpersonationOptions& GetOptions() const { return _options; }
        void SetOptions(bool a_conditions, bool a_aliases, bool a_packages, bool a_scripts);
        bool IsConditionsEnabled() const { return _active.load(std::memory_order_relaxed) && _options.conditions; }

        void Save(SKSE::SerializationInterface* a_serde);
        void Load(SKSE::SerializationInterface* a_serde, std::uint32_t a_version, std::uint32_t a_length);
        void OnRevert();
        void OnPostLoadGame();

        static RE::BGSVoiceType* GetNPCVoiceType(const RE::TESNPC* a_npc);

        void ResetSpeechTracking();
        bool CheckPlayerSpeakingDone();
        void OnDialogueResponseStarted(RE::DialogueResponse* a_response);
        bool IsPlayerSpeaking() const { return _playerSpeaking.load(std::memory_order_relaxed); }
        void SetPlayerSpeaking(bool a_speaking) { _playerSpeaking.store(a_speaking, std::memory_order_relaxed); }
        void CompleteSpeaking(RE::Actor* a_player);
        bool IsInCompleteSpeaking() const { return _inCompleteSpeaking.load(std::memory_order_relaxed); }
        static void AttachSoundHandlesToHead(RE::Actor* a_actor);
        bool HandleProxyDialogueOutput(RE::Actor* a_speaker, RE::DialogueResponse* a_response, bool a_unused);
        void SyncDonorUpdatesToPlayer();

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


        void RetargetPackages(RE::Actor* a_donor, RE::Actor* a_player);
        void RestorePackages();
        void CheckAndRetargetLocation(
            RE::TESPackage* a_pkg,
            RE::PackageLocation* a_loc,
            RE::Actor* a_donor,
            RE::Actor* a_player,
            PackageRetargetType a_handleType,
            PackageRetargetType a_objType);
        void CheckAndRetargetTarget(
            RE::TESPackage* a_pkg,
            RE::PackageTarget* a_targ,
            RE::Actor* a_donor,
            RE::Actor* a_player,
            PackageRetargetType a_handleType,
            PackageRetargetType a_objType);

        void RedirectPapyrusProperties(RE::Actor* a_donor, RE::Actor* a_player);
        void RestorePapyrusProperties(RE::Actor* a_donor, RE::Actor* a_player);

        std::atomic<bool> _active{ false };
        std::atomic<RE::Actor*> _donorActor{ nullptr };
        std::atomic<RE::Actor*> _playerActor{ nullptr };
        RE::NiPointer<RE::Actor> _donorKeepAlive;
        RE::ActorHandle _donorHandle;
        RE::FormID _savedDonorFormID{ 0 };
        RE::BGSVoiceType* _donorVoiceType{ nullptr };
        std::string _donorName;
        std::string _origPlayerName;
        RE::ActorHandle _puppetHandle;

        std::atomic<bool> _playerSpeaking{ false };
        std::atomic<bool> _inCompleteSpeaking{ false };
        std::atomic<bool> _speechHadSound{ false };
        std::chrono::steady_clock::time_point _speechStartTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point _speechAudioEndTime{ std::chrono::steady_clock::time_point{} };
        float _speechMinDuration{ 2.0f };
        std::uint32_t _dialogueSoundID{ static_cast<std::uint32_t>(-1) };

        ImpersonationOptions _options;
        std::vector<PackageRetargetRecord> _retargetedPackages;
        std::vector<PapyrusRedirectRecord> _papyrusRedirects;
        bool _hooksInstalled{ false };
    };

}
