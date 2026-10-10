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

    private:
        ImpersonationManager() = default;
        ~ImpersonationManager() = default;
        ImpersonationManager(const ImpersonationManager&) = delete;
        ImpersonationManager& operator=(const ImpersonationManager&) = delete;

        void InstallHooks();
        static RE::BGSVoiceType* Hook_GetObjectVoiceType(const RE::TESNPC* a_this);
        inline static REL::Relocation<decltype(Hook_GetObjectVoiceType)> _origGetObjectVoiceType;

        static bool Hook_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused);
        using UpdateInDialogue_t = bool(RE::Actor*, RE::DialogueResponse*, bool);
        inline static REL::Relocation<UpdateInDialogue_t> _origUpdateInDialogue;


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

        ImpersonationOptions _options;
        std::vector<PackageRetargetRecord> _retargetedPackages;
        std::vector<PapyrusRedirectRecord> _papyrusRedirects;
        bool _hooksInstalled{ false };
    };

}
