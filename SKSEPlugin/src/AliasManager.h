#pragma once

#include <vector>
#include <cstdint>

namespace AlternateActors {

    struct SwappedAliasRecord {
        RE::FormID questFormID{ 0 };
        std::uint32_t aliasID{ 0 };
        RE::FormID originalActorFormID{ 0 };
    };

    class AliasManager {
    public:
        static AliasManager& GetSingleton() {
            static AliasManager instance;
            return instance;
        }

        // Swaps all running quest aliases referencing a_donor over to a_player
        std::int32_t SwapAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player);

        // Restores all previously swapped aliases back to their original actors
        std::int32_t RestoreAliases();

        // Savegame state helpers
        void Clear();
        const std::vector<SwappedAliasRecord>& GetRecords() const { return _swappedRecords; }
        void AddRecord(const SwappedAliasRecord& record) { _swappedRecords.push_back(record); }
        bool HasRecord(RE::FormID a_questFormID, std::uint32_t a_aliasID) const;
        bool HasQuest(RE::FormID a_questFormID) const {
            for (const auto& rec : _swappedRecords) {
                if (rec.questFormID == a_questFormID) return true;
            }
            return false;
        }

        // Returns all AI packages granted by active quest aliases that currently reference the actor,
        // sorted by quest priority (highest priority first).
        std::vector<RE::TESPackage*> GetAliasPackages(RE::Actor* a_actor) const;

    private:
        AliasManager() = default;
        ~AliasManager() = default;
        AliasManager(const AliasManager&) = delete;
        AliasManager& operator=(const AliasManager&) = delete;

        std::vector<SwappedAliasRecord> _swappedRecords;
    };

}
