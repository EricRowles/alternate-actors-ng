#pragma once

#include <vector>

namespace AlternateActors {

    class InventoryManager {
    public:
        static InventoryManager& GetSingleton() {
            static InventoryManager instance;
            return instance;
        }

        bool StoreAndTransferToPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower = nullptr);
        bool RestoreAndTransferFromPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower = nullptr);

        // Individual helpers
        bool TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor, bool a_autoEquipWorn = true);
        bool StashPlayerDynamicSpells(RE::Actor* a_player);
        bool RestorePlayerDynamicSpells(RE::Actor* a_player);
        bool CopyDynamicSpells(RE::Actor* a_fromActor, RE::Actor* a_toActor);
        bool RemoveCopiedSpells(RE::Actor* a_targetActor);

        void Clear() {
            _transferredSpells.clear();
            _playerSavedSpells.clear();
            _savedPlayerWornFormIDs.clear();
        }

        const std::vector<RE::FormID>& GetTransferredSpellIDs() const { return _transferredSpells; }
        void AddTransferredSpellID(RE::FormID a_id) { _transferredSpells.push_back(a_id); }

        const std::vector<RE::FormID>& GetSavedPlayerSpellIDs() const { return _playerSavedSpells; }
        void AddSavedPlayerSpellID(RE::FormID a_id) { _playerSavedSpells.push_back(a_id); }
        void SetSavedPlayerSpellIDs(const std::vector<RE::FormID>& a_ids) { _playerSavedSpells = a_ids; }

        const std::vector<RE::FormID>& GetSavedPlayerWornFormIDs() const { return _savedPlayerWornFormIDs; }
        void AddSavedPlayerWornFormID(RE::FormID a_id) { _savedPlayerWornFormIDs.push_back(a_id); }
        void SetSavedPlayerWornFormIDs(const std::vector<RE::FormID>& a_ids) { _savedPlayerWornFormIDs = a_ids; }

    private:
        InventoryManager() = default;
        ~InventoryManager() = default;
        InventoryManager(const InventoryManager&) = delete;
        InventoryManager& operator=(const InventoryManager&) = delete;

        std::vector<RE::FormID> _transferredSpells;
        std::vector<RE::FormID> _playerSavedSpells;
        std::vector<RE::FormID> _savedPlayerWornFormIDs;
    };

}
