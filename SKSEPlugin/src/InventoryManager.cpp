#include "PCH.h"
#include "InventoryManager.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace AlternateActors {

    static bool IsModSystemSpell(RE::SpellItem* a_spell) {
        if (!a_spell) return false;
        
        auto* file = a_spell->GetDescriptionOwnerFile();
        if (!file) {
            file = a_spell->GetFile(0);
        }
        if (file) {
            std::string fn(file->GetFilename());
            std::transform(fn.begin(), fn.end(), fn.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (fn.find("alternate actors") != std::string::npos ||
                fn.find("alternateactors") != std::string::npos ||
                fn.find("ak_") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    static bool IsCastableSpell(RE::SpellItem* a_spell) {
        if (!a_spell) return false;
        auto spellType = a_spell->GetSpellType();
        return (spellType == RE::MagicSystem::SpellType::kSpell ||
                spellType == RE::MagicSystem::SpellType::kPower ||
                spellType == RE::MagicSystem::SpellType::kLesserPower ||
                spellType == RE::MagicSystem::SpellType::kVoicePower);
    }

    bool InventoryManager::StashPlayerDynamicSpells(RE::Actor* a_player) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        _playerSavedSpells.clear();
        std::vector<RE::SpellItem*> toRemove;

        auto& runtime = a_player->GetActorRuntimeData();
        for (auto* spell : runtime.addedSpells) {
            if (!spell) continue;

            // Skip mod system spells so player doesn't lose revert/control spells
            if (IsModSystemSpell(spell)) continue;

            // Save castable spells
            if (IsCastableSpell(spell)) {
                _playerSavedSpells.push_back(spell->GetFormID());
                toRemove.push_back(spell);
            }
        }

        for (auto* spell : toRemove) {
            a_player->RemoveSpell(spell);
        }

        SKSE::log::info("Stashed {} player dynamic castable spells.", _playerSavedSpells.size());
        return true;
    }

    bool InventoryManager::RestorePlayerDynamicSpells(RE::Actor* a_player) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_player) return false;

        std::int32_t restoredCount = 0;
        for (auto formID : _playerSavedSpells) {
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
            if (spell) {
                if (a_player->AddSpell(spell)) {
                    restoredCount++;
                }
            }
        }

        SKSE::log::info("Restored {} player dynamic spells.", restoredCount);
        _playerSavedSpells.clear();
        return true;
    }

    bool InventoryManager::CopyDynamicSpells(RE::Actor* a_fromActor, RE::Actor* a_toActor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_fromActor || !a_toActor) return false;

        std::int32_t copiedCount = 0;

        // 1. Copy dynamic spells from donor
        auto& fromRuntime = a_fromActor->GetActorRuntimeData();
        for (auto* spell : fromRuntime.addedSpells) {
            if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                if (a_toActor->AddSpell(spell)) {
                    _transferredSpells.push_back(spell->GetFormID());
                    copiedCount++;
                }
            }
        }

        // 2. Copy base spells from donor's TESNPC actorEffects
        auto* donorBase = a_fromActor->GetActorBase();
        if (donorBase && donorBase->actorEffects) {
            auto* effects = donorBase->actorEffects;
            for (std::uint32_t i = 0; i < effects->numSpells; ++i) {
                auto* spell = effects->spells[i];
                if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                    if (a_toActor->AddSpell(spell)) {
                        _transferredSpells.push_back(spell->GetFormID());
                        copiedCount++;
                    }
                }
            }
        }

        SKSE::log::info("Transferred {} total castable spells from {:08X} to {:08X}.",
            copiedCount,
            static_cast<std::uint32_t>(a_fromActor->GetFormID()),
            static_cast<std::uint32_t>(a_toActor->GetFormID())
        );

        return true;
    }

    bool InventoryManager::RemoveCopiedSpells(RE::Actor* a_targetActor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_targetActor) return false;

        std::int32_t removedCount = 0;
        for (auto formID : _transferredSpells) {
            auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
            if (spell) {
                if (a_targetActor->RemoveSpell(spell)) {
                    removedCount++;
                }
            }
        }
        SKSE::log::info("Removed {} copied donor spells from {:08X}.",
            removedCount,
            static_cast<std::uint32_t>(a_targetActor->GetFormID())
        );
        _transferredSpells.clear();
        return true;
    }

    bool InventoryManager::TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor, bool a_forceEquip) {
        if (!a_fromActor || !a_toActor) {
            SKSE::log::warn("TransferAllItems called with null actor pointer.");
            return false;
        }

        RE::SpellItem* leftSpell = nullptr;
        RE::SpellItem* rightSpell = nullptr;
        RE::TESShout* shout = nullptr;

        // 1. Clear the target's non-quest inventory
        auto toInv = a_toActor->GetInventory();
        for (const auto& [boundObj, entryPair] : toInv) {
            std::int32_t count = entryPair.first;
            const auto& entryData = entryPair.second;

            if (!boundObj || count <= 0) continue;
            if (entryData && entryData->IsQuestObject()) continue;
            a_toActor->RemoveItem(boundObj, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        }

        // 2. Get equipped spells
        auto& fromRuntime = a_fromActor->GetActorRuntimeData();
        leftSpell = skyrim_cast<RE::SpellItem*>(fromRuntime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]);
        rightSpell = skyrim_cast<RE::SpellItem*>(fromRuntime.selectedSpells[RE::Actor::SlotTypes::kRightHand]);
        shout = skyrim_cast<RE::TESShout*>(fromRuntime.selectedPower);

        // 3. Identify worn items and their equipped slots (left hand vs main) before transfer
        auto fromInv = a_fromActor->GetInventory();
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
        auto* leftSlot = defaultObjMgr ? defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip) : nullptr;

        struct WornItemRecord {
            RE::TESBoundObject* object{ nullptr };
            bool isLeft{ false };
        };
        std::vector<WornItemRecord> wornRecords;
        std::unordered_set<RE::TESBoundObject*> wornObjects;

        for (const auto& [boundObj, entryPair] : fromInv) {
            const auto& entryData = entryPair.second;
            if (!boundObj || !entryData) continue;
            if (entryData->IsWorn()) {
                wornObjects.insert(boundObj);
                bool foundLeft = false;
                bool foundRight = false;
                if (entryData->extraLists) {
                    for (auto* xList : *entryData->extraLists) {
                        if (!xList) continue;
                        if (xList->HasType(RE::ExtraDataType::kWornLeft)) {
                            wornRecords.push_back({ boundObj, true });
                            foundLeft = true;
                        } else if (xList->HasType(RE::ExtraDataType::kWorn)) {
                            wornRecords.push_back({ boundObj, false });
                            foundRight = true;
                        }
                    }
                }
                if (!foundLeft && !foundRight) {
                    wornRecords.push_back({ boundObj, false });
                }
            }
        }

        // 4. Transfer items
        std::int32_t itemsTransferred = 0;
        for (const auto& [boundObj, entryPair] : fromInv) {
            std::int32_t count = entryPair.first;
            const auto& entryData = entryPair.second;
            if (!boundObj || count <= 0) continue;

            bool isQuest = entryData && entryData->IsQuestObject();
            if (isQuest) {
                if (a_fromActor->IsPlayerRef() && !a_toActor->IsPlayerRef()) {
                    // Player to follower or donor:
                    // Only transfer quest item if it was a donor quest item previously transferred
                    if (_transferredDonorQuestItems.find(boundObj->GetFormID()) == _transferredDonorQuestItems.end()) {
                        continue; // Keep player's own quest object on player
                    }
                    _transferredDonorQuestItems.erase(boundObj->GetFormID());
                } else if (!a_fromActor->IsPlayerRef() && a_toActor->IsPlayerRef()) {
                    // Donor to player:
                    _transferredDonorQuestItems.insert(boundObj->GetFormID());
                }
            }

            a_fromActor->RemoveItem(boundObj, count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_toActor);
            itemsTransferred++;
        }

        // 5. Re-equip worn items on target actor with proper ExtraData and slot assignment
        auto toInvAfter = a_toActor->GetInventory();
        for (const auto& wornRec : wornRecords) {
            auto it = toInvAfter.find(wornRec.object);
            if (it != toInvAfter.end() && it->second.first > 0) {
                auto& toEntry = it->second.second;
                RE::ExtraDataList* xList = nullptr;
                if (toEntry && toEntry->extraLists && !toEntry->extraLists->empty()) {
                    for (auto* x : *toEntry->extraLists) {
                        if (x && !x->HasType(RE::ExtraDataType::kWorn) && !x->HasType(RE::ExtraDataType::kWornLeft)) {
                            xList = x;
                            break;
                        }
                    }
                    if (!xList) {
                        xList = toEntry->extraLists->front();
                    }
                }
                const RE::BGSEquipSlot* slot = wornRec.isLeft ? leftSlot : nullptr;
                std::uint32_t equipCount = wornRec.object->Is(RE::FormType::Ammo) ? static_cast<std::uint32_t>(it->second.first) : 1;
                equipMgr->EquipObject(a_toActor, wornRec.object, xList, equipCount, slot, false, a_forceEquip, false, true);
            }
        }

        // 6. If target is an NPC (such as puppet), Skyrim's native AddObjectToContainer
        // automatically equips armor pieces found in inventory even if they were NOT worn on the source actor.
        // Unequip any items that were not in the source actor's worn list.
        if (!a_toActor->IsPlayerRef()) {
            auto toInvFinal = a_toActor->GetInventory();
            for (const auto& [boundObj, entryPair] : toInvFinal) {
                const auto& entryData = entryPair.second;
                if (boundObj && entryData && entryData->IsWorn()) {
                    if (wornObjects.find(boundObj) == wornObjects.end()) {
                        equipMgr->UnequipObject(a_toActor, boundObj, nullptr, 1, nullptr, false, true, false, true);
                    }
                }
            }
        }

        // 7. Re-equip spells if target is the player
        if (a_toActor->IsPlayerRef() && defaultObjMgr) {
            auto* rightSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);
            if (leftSpell && leftSlot) {
                equipMgr->EquipSpell(a_toActor, leftSpell, leftSlot);
            }
            if (rightSpell && rightSlot) {
                equipMgr->EquipSpell(a_toActor, rightSpell, rightSlot);
            }
            if (shout) {
                equipMgr->EquipShout(a_toActor, shout);
            }
        }

        SKSE::log::info("Transferred {} item stacks from {:08X} to {:08X}.",
            itemsTransferred,
            static_cast<std::uint32_t>(a_fromActor->GetFormID()),
            static_cast<std::uint32_t>(a_toActor->GetFormID())
        );

        a_toActor->Update3DModel();
        return true;
    }

    bool InventoryManager::StoreAndTransferToPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        if (!a_player || !a_donor) return false;

        std::lock_guard<std::recursive_mutex> lock(_mutex);

        // Save player's equipped spells and shout before morphing
        auto& pRuntime = a_player->GetActorRuntimeData();
        _savedPlayerLeftSpell = skyrim_cast<RE::SpellItem*>(pRuntime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]);
        _savedPlayerRightSpell = skyrim_cast<RE::SpellItem*>(pRuntime.selectedSpells[RE::Actor::SlotTypes::kRightHand]);
        _savedPlayerShout = skyrim_cast<RE::TESShout*>(pRuntime.selectedPower);

        if (a_storageFollower) {
            TransferAllItems(a_player, a_storageFollower, true);
            SKSE::log::info("Stored player gear in DragonbornFollower ({:08X}).",
                static_cast<std::uint32_t>(a_storageFollower->GetFormID()));
        }

        // 1. Stash and strip player's dynamic castable spells
        StashPlayerDynamicSpells(a_player);

        // 2. Transfer donor's dynamic and base castable spells to player
        CopyDynamicSpells(a_donor, a_player);

        // 3. Transfer donor's items to player
        TransferAllItems(a_donor, a_player, false);

        return true;
    }

    bool InventoryManager::RestoreAndTransferFromPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        if (!a_player) return false;

        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();

        // 1. Return items from player to donor (if donor is valid)
        if (a_donor) {
            TransferAllItems(a_player, a_donor, true);
        }

        // 2. Remove copied donor spells from player
        RemoveCopiedSpells(a_player);

        // 3. Restore player's original dynamic spells
        RestorePlayerDynamicSpells(a_player);

        // 4. Return player's original items from follower to player
        if (a_storageFollower) {
            TransferAllItems(a_storageFollower, a_player, false);
        }

        // 5. Restore player's original equipped spells and shout
        auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
        if (defaultObjMgr && equipMgr) {
            auto* leftSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip);
            auto* rightSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);
            if (_savedPlayerLeftSpell && leftSlot) {
                equipMgr->EquipSpell(a_player, _savedPlayerLeftSpell, leftSlot);
            }
            if (_savedPlayerRightSpell && rightSlot) {
                equipMgr->EquipSpell(a_player, _savedPlayerRightSpell, rightSlot);
            }
            if (_savedPlayerShout) {
                equipMgr->EquipShout(a_player, _savedPlayerShout);
            }
        }
        _savedPlayerLeftSpell = nullptr;
        _savedPlayerRightSpell = nullptr;
        _savedPlayerShout = nullptr;
        _transferredDonorQuestItems.clear();

        return true;
    }

}
