#pragma once

#include <mutex>
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
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _transferredSpells.clear();
            _playerSavedSpells.clear();
        }

        std::vector<RE::FormID> GetTransferredSpellIDs() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _transferredSpells;
        }
        void AddTransferredSpellID(RE::FormID a_id) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _transferredSpells.push_back(a_id);
        }

        std::vector<RE::FormID> GetSavedPlayerSpellIDs() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _playerSavedSpells;
        }
        void AddSavedPlayerSpellID(RE::FormID a_id) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _playerSavedSpells.push_back(a_id);
        }
        void SetSavedPlayerSpellIDs(const std::vector<RE::FormID>& a_ids) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _playerSavedSpells = a_ids;
        }

    private:
        InventoryManager() = default;
        ~InventoryManager() = default;
        InventoryManager(const InventoryManager&) = delete;
        InventoryManager& operator=(const InventoryManager&) = delete;

        mutable std::recursive_mutex _mutex;
        std::vector<RE::FormID> _transferredSpells;
        std::vector<RE::FormID> _playerSavedSpells;
    };

}
