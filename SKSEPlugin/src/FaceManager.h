#pragma once

#include <vector>
#include "RE/N/NiColor.h"

namespace AlternateActors {

    struct FaceSnapshot {
        RE::TESRace* race{ nullptr };
        RE::TESNPC* faceNPC{ nullptr };
        RE::BGSVoiceType* voiceType{ nullptr };
        RE::BGSColorForm* hairColor{ nullptr };
        RE::BGSTextureSet* faceDetails{ nullptr };
        RE::Color bodyTintColor{};
        float height{ 1.0f };
        float weight{ 50.0f };
        RE::SEX sex{ RE::SEX::kNone };
        RE::BSFixedString fullName;

        // Face Morphs
        float morphs[RE::TESNPC::FaceData::Morphs::kTotal]{ 0.0f };
        std::int32_t parts[RE::TESNPC::FaceData::Parts::kTotal]{ 0 };

        // Head Parts
        std::vector<RE::BGSHeadPart*> headParts;

        // Tint Layers
        std::vector<RE::TESNPC::Layer> tintLayers;

        // 3D Rendered Hair Tint Color
        RE::NiColor hairTintColor{ 0.0f, 0.0f, 0.0f };
        bool hasHairTintColor{ false };

        // 3D Rendered Skin Tint Color
        RE::NiColor skinTintColor{ 0.0f, 0.0f, 0.0f };
        bool hasSkinTintColor{ false };

        // AI Behavior & Combat Data
        RE::TESClass* npcClass{ nullptr };
        RE::TESCombatStyle* combatStyle{ nullptr };
        RE::BGSListForm* defaultPackList{ nullptr };
        RE::AIDATA_GAME aiData{};
        std::vector<RE::TESPackage*> packages;
    };

    class FaceManager {
    public:
        static FaceManager& GetSingleton() {
            static FaceManager instance;
            return instance;
        }

        // Clones full visual appearance and AI profile from donor to target actor
        bool CloneAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Copies visual appearance and AI profile from donor to target actor without modifying saved history
        bool CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Restores saved original appearance onto target actor
        bool RevertAppearance(RE::Actor* a_target);

        // Restores active donor appearance onto target actor (using saved donor snapshot if available, or donor actor fallback)
        bool RestoreDonorAppearance(RE::Actor* a_donor, RE::Actor* a_target);

        // Restores saved original Dragonborn appearance onto puppet actor
        bool RestoreOriginalPuppetAppearance(RE::Actor* a_puppet);

        // Snapshot utilities
        bool CaptureSnapshot(RE::Actor* a_actor, FaceSnapshot& a_outSnapshot);
        bool ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot);

        // State & Serialization
        bool HasSavedAppearance() const { return _hasSavedAppearance; }
        const FaceSnapshot& GetSavedAppearance() const { return _savedOriginalAppearance; }
        void SetSavedAppearance(const FaceSnapshot& a_snapshot) { _savedOriginalAppearance = a_snapshot; _hasSavedAppearance = true; }
        void ClearSavedAppearance() { _hasSavedAppearance = false; }

        bool HasDonorAppearance() const { return _hasDonorAppearance; }
        const FaceSnapshot& GetDonorAppearance() const { return _donorAppearance; }
        void SetDonorAppearance(const FaceSnapshot& a_snapshot) { _donorAppearance = a_snapshot; _hasDonorAppearance = true; }
        void ClearDonorAppearance() { _hasDonorAppearance = false; }

    private:
        FaceManager() = default;
        ~FaceManager() = default;
        FaceManager(const FaceManager&) = delete;
        FaceManager& operator=(const FaceManager&) = delete;

        FaceSnapshot _savedOriginalAppearance;
        bool _hasSavedAppearance{ false };
        FaceSnapshot _donorAppearance;
        bool _hasDonorAppearance{ false };
    };

}
