#include "PCH.h"
#include "InventoryManager.h"
#include <algorithm>
#include <cctype>

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
            SKSE::log::info("Stashed and removed player dynamic spell '{:08X}' ({})",
                static_cast<std::uint32_t>(spell->GetFormID()),
                spell->GetName()
            );
        }

        SKSE::log::info("Stashed {} player dynamic castable spells.", _playerSavedSpells.size());
        return true;
    }

    bool InventoryManager::RestorePlayerDynamicSpells(RE::Actor* a_player) {
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
        if (!a_fromActor || !a_toActor) return false;

        std::int32_t copiedCount = 0;

        // 1. Copy dynamic spells from donor
        auto& fromRuntime = a_fromActor->GetActorRuntimeData();
        for (auto* spell : fromRuntime.addedSpells) {
            if (spell && IsCastableSpell(spell) && !IsModSystemSpell(spell)) {
                if (a_toActor->AddSpell(spell)) {
                    _transferredSpells.push_back(spell->GetFormID());
                    copiedCount++;
                    SKSE::log::info("Copied dynamic spell '{:08X}' ({}) from {:08X} to {:08X}",
                        static_cast<std::uint32_t>(spell->GetFormID()),
                        spell->GetName(),
                        static_cast<std::uint32_t>(a_fromActor->GetFormID()),
                        static_cast<std::uint32_t>(a_toActor->GetFormID())
                    );
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
                        SKSE::log::info("Copied base spell '{:08X}' ({}) from {:08X} to {:08X}",
                            static_cast<std::uint32_t>(spell->GetFormID()),
                            spell->GetName(),
                            static_cast<std::uint32_t>(a_fromActor->GetFormID()),
                            static_cast<std::uint32_t>(a_toActor->GetFormID())
                        );
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

    bool InventoryManager::TransferAllItems(RE::Actor* a_fromActor, RE::Actor* a_toActor, bool a_autoEquipWorn) {
        if (!a_fromActor || !a_toActor) {
            SKSE::log::warn("TransferAllItems called with null actor pointer.");
            return false;
        }

        std::vector<RE::TESBoundObject*> wornItems;
        RE::SpellItem* leftSpell = nullptr;
        RE::SpellItem* rightSpell = nullptr;
        RE::TESShout* shout = nullptr;

        // 1. Snapshot inventory and gather equipped state (before we modify anything)
        auto fromInv = a_fromActor->GetInventory();

        if (a_autoEquipWorn) {
            for (const auto& [boundObj, entryPair] : fromInv) {
                const auto& entryData = entryPair.second;
                if (boundObj && entryData && entryData->IsWorn()) {
                    wornItems.push_back(boundObj);
                }
            }

            auto& fromRuntime = a_fromActor->GetActorRuntimeData();
            leftSpell = skyrim_cast<RE::SpellItem*>(fromRuntime.selectedSpells[RE::Actor::SlotTypes::kLeftHand]);
            rightSpell = skyrim_cast<RE::SpellItem*>(fromRuntime.selectedSpells[RE::Actor::SlotTypes::kRightHand]);
            shout = skyrim_cast<RE::TESShout*>(fromRuntime.selectedPower);
        }

        // 2. Transfer non-quest items with duplicate outfit protection
        auto toInv = a_toActor->GetInventory();
        std::int32_t itemsTransferred = 0;
        for (const auto& [boundObj, entryPair] : fromInv) {
            std::int32_t count = entryPair.first;
            const auto& entryData = entryPair.second;

            if (!boundObj || count <= 0) continue;

            // Skip quest items - they cannot be removed from the inventory
            if (entryData && entryData->IsQuestObject()) {
                SKSE::log::debug("Skipping quest item '{}' ({:08X}) during transfer.",
                    boundObj->GetName(),
                    static_cast<std::uint32_t>(boundObj->GetFormID()));
                continue;
            }

            std::int32_t toCount = 0;
            auto toIt = toInv.find(boundObj);
            if (toIt != toInv.end()) {
                toCount = toIt->second.first;
            }

            // If recipient already has this item (e.g. from default outfit spawning), do not duplicate
            if (!a_toActor->IsPlayerRef() && toCount >= count) {
                a_fromActor->RemoveItem(boundObj, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            } else {
                std::int32_t transferCount = (!a_toActor->IsPlayerRef() && toCount > 0) ? (count - toCount) : count;
                if (transferCount > 0) {
                    a_fromActor->RemoveItem(boundObj, transferCount, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_toActor);
                    itemsTransferred++;
                }
                if (count > transferCount) {
                    a_fromActor->RemoveItem(boundObj, count - transferCount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
            }
        }

        // 3. Re-equip on recipient
        if (a_autoEquipWorn) {
            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (equipMgr) {
                for (auto* item : wornItems) {
                    equipMgr->EquipObject(a_toActor, item, nullptr, 1, nullptr, false, true, false, true);
                }

                auto* defaultObjMgr = RE::BGSDefaultObjectManager::GetSingleton();
                if (defaultObjMgr) {
                    auto* leftSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kLeftHandEquip);
                    auto* rightSlot = defaultObjMgr->GetObject<RE::BGSEquipSlot>(RE::BGSDefaultObjectManager::DefaultObject::kRightHandEquip);

                    if (leftSpell && leftSlot) {
                        equipMgr->EquipSpell(a_toActor, leftSpell, leftSlot);
                    }
                    if (rightSpell && rightSlot) {
                        equipMgr->EquipSpell(a_toActor, rightSpell, rightSlot);
                    }
                }

                if (shout) {
                    equipMgr->EquipShout(a_toActor, shout);
                }
            }
        }

        SKSE::log::info("Transferred {} item stacks from {:08X} to {:08X} (re-equipped {} worn items).",
            itemsTransferred,
            static_cast<std::uint32_t>(a_fromActor->GetFormID()),
            static_cast<std::uint32_t>(a_toActor->GetFormID()),
            wornItems.size()
        );

        return true;
    }

    bool InventoryManager::StoreAndTransferToPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        if (!a_player || !a_donor) return false;

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
        TransferAllItems(a_donor, a_player, true);

        return true;
    }

    bool InventoryManager::RestoreAndTransferFromPlayer(RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        if (!a_player || !a_donor) return false;

        // 1. Return items from player to donor
        TransferAllItems(a_player, a_donor, true);

        // 2. Remove copied donor spells from player
        RemoveCopiedSpells(a_player);

        // 3. Restore player's original dynamic spells
        RestorePlayerDynamicSpells(a_player);

        if (a_storageFollower) {
            TransferAllItems(a_storageFollower, a_player, true);
            SKSE::log::info("Retrieved player gear from DragonbornFollower ({:08X}).",
                static_cast<std::uint32_t>(a_storageFollower->GetFormID()));
        }

        return true;
    }

}
