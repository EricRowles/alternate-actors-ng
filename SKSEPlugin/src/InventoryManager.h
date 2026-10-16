#pragma once

#include <mutex>
#include <vector>
#include <unordered_set>

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
            _transferredDonorQuestItems.clear();
            _savedPlayerLeftSpell = nullptr;
            _savedPlayerRightSpell = nullptr;
            _savedPlayerShout = nullptr;
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

        std::unordered_set<RE::FormID> GetTransferredDonorQuestItems() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _transferredDonorQuestItems;
        }
        void SetTransferredDonorQuestItems(const std::unordered_set<RE::FormID>& a_items) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _transferredDonorQuestItems = a_items;
        }
        void AddTransferredDonorQuestItem(RE::FormID a_id) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _transferredDonorQuestItems.insert(a_id);
        }

        RE::SpellItem* GetSavedPlayerLeftSpell() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _savedPlayerLeftSpell;
        }
        RE::SpellItem* GetSavedPlayerRightSpell() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _savedPlayerRightSpell;
        }
        RE::TESShout* GetSavedPlayerShout() const {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            return _savedPlayerShout;
        }
        void SetSavedPlayerEquippedSpells(RE::SpellItem* a_left, RE::SpellItem* a_right, RE::TESShout* a_shout) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _savedPlayerLeftSpell = a_left;
            _savedPlayerRightSpell = a_right;
            _savedPlayerShout = a_shout;
        }

    private:
        InventoryManager() = default;
        ~InventoryManager() = default;
        InventoryManager(const InventoryManager&) = delete;
        InventoryManager& operator=(const InventoryManager&) = delete;

        mutable std::recursive_mutex _mutex;
        std::vector<RE::FormID> _transferredSpells;
        std::vector<RE::FormID> _playerSavedSpells;
        std::unordered_set<RE::FormID> _transferredDonorQuestItems;
        RE::SpellItem* _savedPlayerLeftSpell{ nullptr };
        RE::SpellItem* _savedPlayerRightSpell{ nullptr };
        RE::TESShout* _savedPlayerShout{ nullptr };
    };

}
