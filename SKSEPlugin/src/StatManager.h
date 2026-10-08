#pragma once

#include <vector>
#include <unordered_map>

namespace AlternateActors {

    struct StatSnapshot {
        std::unordered_map<std::uint32_t, float> baseValues;
    };

    class StatManager {
    public:
        static StatManager& GetSingleton() {
            static StatManager instance;
            return instance;
        }

        // Copies all stats and skills from donor to player, saving player's original stats
        bool TransferStatsToPlayer(RE::Actor* a_player, RE::Actor* a_donor);

        // Copies stats and skills from one actor to another without modifying saved player history
        bool CopyStats(RE::Actor* a_fromActor, RE::Actor* a_toActor);

        // Restores player's original stats and skills
        bool RestoreStatsFromPlayer(RE::Actor* a_player);

        // Helpers
        bool CaptureSnapshot(RE::Actor* a_actor, StatSnapshot& a_outSnapshot);
        bool ApplySnapshot(RE::Actor* a_targetActor, const StatSnapshot& a_snapshot);

        void Clear() {
            _savedPlayerStats.baseValues.clear();
            _hasSavedStats = false;
        }

        bool HasSavedStats() const { return _hasSavedStats; }
        const StatSnapshot& GetSavedPlayerStats() const { return _savedPlayerStats; }
        void SetSavedPlayerStats(const StatSnapshot& a_snapshot) {
            _savedPlayerStats = a_snapshot;
            _hasSavedStats = true;
        }

    private:
        StatManager() = default;
        ~StatManager() = default;
        StatManager(const StatManager&) = delete;
        StatManager& operator=(const StatManager&) = delete;

        StatSnapshot _savedPlayerStats;
        bool _hasSavedStats{ false };
    };

}
