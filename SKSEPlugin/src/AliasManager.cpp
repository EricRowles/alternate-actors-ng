#include "PCH.h"
#include "AliasManager.h"

namespace AlternateActors {

    static void DispatchForceRefTo(RE::BGSRefAlias* a_alias, RE::TESObjectREFR* a_target) {
        if (!a_alias || !a_target) return;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return;

        auto* handlePolicy = vm->GetObjectHandlePolicy();
        if (!handlePolicy) return;

        auto handle = handlePolicy->GetHandleForObject(RE::BGSRefAlias::VMTYPEID, a_alias);
        if (handle == handlePolicy->EmptyHandle()) return;

        auto* args = RE::MakeFunctionArguments(std::move(a_target));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        auto className = RE::BSFixedString("ReferenceAlias");
        auto funcName = RE::BSFixedString("ForceRefTo");
        vm->DispatchMethodCall(handle, className, funcName, args, callback);
    }

    static bool IsModInternalQuest(RE::TESQuest* a_quest) {
        if (!a_quest) return false;
        auto* file = a_quest->GetDescriptionOwnerFile();
        if (!file) {
            file = a_quest->GetFile(0);
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

    bool AliasManager::HasRecord(RE::FormID a_questFormID, std::uint32_t a_aliasID) const {
        for (const auto& r : _swappedRecords) {
            if (r.questFormID == a_questFormID && r.aliasID == a_aliasID) {
                return true;
            }
        }
        return false;
    }

    std::int32_t AliasManager::SwapAliasesToPlayer(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) {
            SKSE::log::warn("SwapAliasesToPlayer called with null actor pointers.");
            return 0;
        }

        auto* aliasArray = a_donor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
        if (!aliasArray) {
            SKSE::log::info("Donor actor {:08X} has no ExtraAliasInstanceArray (no active quest aliases).", static_cast<std::uint32_t>(a_donor->GetFormID()));
            return 0;
        }

        std::int32_t swappedCount = 0;
        RE::FormID donorID = a_donor->GetFormID();

        for (auto* instance : aliasArray->aliases) {
            if (!instance || !instance->quest || !instance->alias) {
                continue;
            }

            // Skip internal Alternate Actors quests to prevent corrupting mod control aliases
            if (IsModInternalQuest(instance->quest)) {
                continue;
            }

            // Only redirect aliases on quests that are currently active/running
            if (!instance->quest->IsRunning()) {
                continue;
            }

            auto* refAlias = static_cast<RE::BGSRefAlias*>(const_cast<RE::BGSBaseAlias*>(instance->alias));
            if (!refAlias) {
                continue;
            }

            // Verify that the alias currently holds the donor actor
            auto currentRef = refAlias->GetReference();
            if (currentRef != a_donor) {
                continue;
            }

            // Deduplicate records
            if (HasRecord(instance->quest->GetFormID(), refAlias->aliasID)) {
                continue;
            }

            SKSE::log::info("Redirecting alias '{}' (ID: {}) in quest {:08X} from {:08X} to player ({:08X})",
                refAlias->aliasName.c_str(),
                refAlias->aliasID,
                static_cast<std::uint32_t>(instance->quest->GetFormID()),
                static_cast<std::uint32_t>(donorID),
                static_cast<std::uint32_t>(a_player->GetFormID())
            );

            // Record this alias for restoration later
            SwappedAliasRecord record;
            record.questFormID = instance->quest->GetFormID();
            record.aliasID = refAlias->aliasID;
            record.originalActorFormID = donorID;
            _swappedRecords.push_back(record);

            // Force the alias to point to the player
            DispatchForceRefTo(refAlias, a_player);
            swappedCount++;
        }

        if (swappedCount > 0) {
            SKSE::log::info("SwapAliasesToPlayer: {} new aliases redirected.", swappedCount);
        }
        return swappedCount;
    }

    std::int32_t AliasManager::RestoreAliases() {
        if (_swappedRecords.empty()) {
            SKSE::log::info("RestoreAliases called, but no swapped aliases were tracked.");
            return 0;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        std::int32_t restoredCount = 0;

        for (const auto& record : _swappedRecords) {
            auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(record.questFormID);
            auto* origActor = RE::TESForm::LookupByID<RE::TESObjectREFR>(record.originalActorFormID);

            if (!quest || !origActor) {
                SKSE::log::warn("Could not find quest {:08X} or original actor {:08X} for restoration.",
                    static_cast<std::uint32_t>(record.questFormID), static_cast<std::uint32_t>(record.originalActorFormID));
                continue;
            }

            if (!quest->IsRunning()) {
                SKSE::log::info("Quest {:08X} is no longer running, skipping alias restoration.", static_cast<std::uint32_t>(quest->GetFormID()));
                continue;
            }

            // Find matching ReferenceAlias in the quest by aliasID
            for (auto* alias : quest->aliases) {
                if (alias && alias->aliasID == record.aliasID) {
                    auto* refAlias = static_cast<RE::BGSRefAlias*>(alias);
                    if (refAlias) {
                        if (player && refAlias->GetReference() != player) {
                            SKSE::log::info("Alias '{}' (ID: {}) in quest {:08X} no longer holds player, skipping restoration.",
                                refAlias->aliasName.c_str(),
                                refAlias->aliasID,
                                static_cast<std::uint32_t>(quest->GetFormID())
                            );
                            break;
                        }

                        SKSE::log::info("Restoring alias '{}' (ID: {}) in quest {:08X} back to actor {:08X}",
                            refAlias->aliasName.c_str(),
                            refAlias->aliasID,
                            static_cast<std::uint32_t>(quest->GetFormID()),
                            static_cast<std::uint32_t>(origActor->GetFormID())
                        );
                        DispatchForceRefTo(refAlias, origActor);
                        restoredCount++;
                    }
                    break;
                }
            }
        }

        _swappedRecords.clear();
        SKSE::log::info("RestoreAliases complete: {} aliases restored.", restoredCount);
        return restoredCount;
    }

    void AliasManager::Clear() {
        _swappedRecords.clear();
    }

}
