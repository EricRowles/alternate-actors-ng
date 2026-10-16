#pragma once

#include <atomic>
#include <mutex>
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
        float scale{ 1.0f };
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
        bool HasSavedAppearance() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _hasSavedAppearance;
        }
        FaceSnapshot GetSavedAppearance() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _savedOriginalAppearance;
        }
        void SetSavedAppearance(const FaceSnapshot& a_snapshot) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _savedOriginalAppearance = a_snapshot;
            _hasSavedAppearance = true;
        }
        void ClearSavedAppearance() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _hasSavedAppearance = false;
        }

        bool HasDonorAppearance() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _hasDonorAppearance;
        }
        FaceSnapshot GetDonorAppearance() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _donorAppearance;
        }
        void SetDonorAppearance(const FaceSnapshot& a_snapshot) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _donorAppearance = a_snapshot;
            _hasDonorAppearance = true;
        }
        void ClearDonorAppearance() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _hasDonorAppearance = false;
        }

        bool NeedsPuppetPost3DUpdate() const { return _puppetNeedsPost3DUpdate.load(std::memory_order_relaxed); }
        void SetPuppetNeedsPost3DUpdate(bool a_needs) { _puppetNeedsPost3DUpdate.store(a_needs, std::memory_order_relaxed); }
        void ApplyPuppetPost3DLighting(RE::Actor* a_puppet);

    private:
        FaceManager() = default;
        ~FaceManager() = default;
        FaceManager(const FaceManager&) = delete;
        FaceManager& operator=(const FaceManager&) = delete;

        mutable std::recursive_mutex _mutex;
        FaceSnapshot _savedOriginalAppearance;
        bool _hasSavedAppearance{ false };
        FaceSnapshot _donorAppearance;
        bool _hasDonorAppearance{ false };
        std::atomic<bool> _puppetNeedsPost3DUpdate{ false };
    };

}
