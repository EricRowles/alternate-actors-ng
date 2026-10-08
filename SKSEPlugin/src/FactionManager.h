#pragma once

#include <vector>
#include <unordered_map>

namespace AlternateActors {

    struct FactionRecord {
        RE::TESFaction* faction{ nullptr };
        std::int8_t rank{ 0 };
    };

    class FactionManager {
    public:
        static FactionManager& GetSingleton() {
            static FactionManager instance;
            return instance;
        }

        // Copies all base and dynamic factions from donor to player, saving player's original factions
        bool TransferFactionsToPlayer(RE::Actor* a_player, RE::Actor* a_donor);

        // Restores player's original factions and crime faction
        bool RestoreFactionsFromPlayer(RE::Actor* a_player);

        // Helpers
        std::vector<FactionRecord> GetActorFactions(RE::Actor* a_actor);
        void SetActorFactions(RE::Actor* a_actor, const std::vector<FactionRecord>& a_factions, RE::TESFaction* a_crimeFaction);

        void Clear() {
            _savedPlayerFactions.clear();
            _savedPlayerCrimeFaction = nullptr;
            _hasSavedFactions = false;
        }

        bool HasSavedFactions() const { return _hasSavedFactions; }
        const std::vector<FactionRecord>& GetSavedPlayerFactions() const { return _savedPlayerFactions; }
        RE::TESFaction* GetSavedCrimeFaction() const { return _savedPlayerCrimeFaction; }

        void SetSavedState(const std::vector<FactionRecord>& a_factions, RE::TESFaction* a_crimeFaction) {
            _savedPlayerFactions = a_factions;
            _savedPlayerCrimeFaction = a_crimeFaction;
            _hasSavedFactions = true;
        }

    private:
        FactionManager() = default;
        ~FactionManager() = default;
        FactionManager(const FactionManager&) = delete;
        FactionManager& operator=(const FactionManager&) = delete;

        std::vector<FactionRecord> _savedPlayerFactions;
        RE::TESFaction* _savedPlayerCrimeFaction{ nullptr };
        bool _hasSavedFactions{ false };
    };

}
