#include "PCH.h"
#include "FactionManager.h"

namespace AlternateActors {

    std::vector<FactionRecord> FactionManager::GetActorFactions(RE::Actor* a_actor) {
        std::vector<FactionRecord> result;
        if (!a_actor) return result;

        std::unordered_map<RE::TESFaction*, std::int8_t> factionMap;

        // 1. Base factions from TESNPC
        auto* base = a_actor->GetActorBase();
        if (base) {
            auto* npc = skyrim_cast<RE::TESNPC*>(base);
            if (npc) {
                for (const auto& fData : npc->factions) {
                    if (fData.faction && fData.rank >= 0) {
                        factionMap[fData.faction] = fData.rank;
                    }
                }
            }
        }

        // 2. ExtraFactionChanges on actor ref
        auto* extraFaction = a_actor->extraList.GetByType<RE::ExtraFactionChanges>();
        if (extraFaction) {
            for (const auto& fRank : extraFaction->factionChanges) {
                if (fRank.faction) {
                    if (fRank.rank >= 0) {
                        factionMap[fRank.faction] = fRank.rank;
                    } else {
                        factionMap.erase(fRank.faction);
                    }
                }
            }
        }

        for (const auto& [faction, rank] : factionMap) {
            result.push_back({ faction, rank });
        }

        return result;
    }

    void FactionManager::SetActorFactions(RE::Actor* a_actor, const std::vector<FactionRecord>& a_factions, RE::TESFaction* a_crimeFaction) {
        if (!a_actor) return;

        // 1. Remove all current dynamic faction changes
        auto* extraFaction = a_actor->extraList.GetByType<RE::ExtraFactionChanges>();
        if (extraFaction) {
            extraFaction->factionChanges.clear();
        }

        // 2. Clear any base factions that are not in the new faction set by adding with rank -1
        auto* base = a_actor->GetActorBase();
        if (base) {
            auto* npc = skyrim_cast<RE::TESNPC*>(base);
            if (npc) {
                for (const auto& fData : npc->factions) {
                    if (fData.faction) {
                        a_actor->AddToFaction(fData.faction, -1);
                    }
                }
            }
        }

        // 3. Add all new factions with their appropriate ranks
        for (const auto& rec : a_factions) {
            if (rec.faction) {
                a_actor->AddToFaction(rec.faction, rec.rank);
            }
        }

        // 4. Set Crime Faction
        if (extraFaction) {
            extraFaction->crimeFaction = a_crimeFaction;
            extraFaction->removeCrimeFaction = (a_crimeFaction == nullptr);
        }

        SKSE::log::info("Applied {} factions (Crime: {:08X}) to actor {:08X}",
            a_factions.size(),
            a_crimeFaction ? static_cast<std::uint32_t>(a_crimeFaction->GetFormID()) : 0,
            static_cast<std::uint32_t>(a_actor->GetFormID())
        );
    }

    bool FactionManager::TransferFactionsToPlayer(RE::Actor* a_player, RE::Actor* a_donor) {
        if (!a_player || !a_donor) {
            SKSE::log::warn("TransferFactionsToPlayer called with null actor pointers.");
            return false;
        }

        // Save original player factions if not already saved
        if (!_hasSavedFactions) {
            _savedPlayerFactions = GetActorFactions(a_player);
            _savedPlayerCrimeFaction = a_player->GetCrimeFaction();
            _hasSavedFactions = true;
            SKSE::log::info("Saved {} player factions before transfer.", _savedPlayerFactions.size());
        }

        // Get donor factions and crime faction
        auto donorFactions = GetActorFactions(a_donor);
        auto* donorCrime = a_donor->GetCrimeFaction();

        SetActorFactions(a_player, donorFactions, donorCrime);
        return true;
    }

    bool FactionManager::RestoreFactionsFromPlayer(RE::Actor* a_player) {
        if (!a_player) return false;

        if (!_hasSavedFactions) {
            SKSE::log::warn("RestoreFactionsFromPlayer called, but no original factions were saved.");
            return false;
        }

        SetActorFactions(a_player, _savedPlayerFactions, _savedPlayerCrimeFaction);
        Clear();
        SKSE::log::info("Restored original player factions successfully.");
        return true;
    }

}
