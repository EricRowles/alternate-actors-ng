#include "PCH.h"
#include "Papyrus.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "InventoryManager.h"
#include "FactionManager.h"
#include "StatManager.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include <RE/C/ControlMap.h>

namespace AlternateActors::Papyrus {

    bool CloneAppearance(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_target) {
        return FaceManager::GetSingleton().CloneAppearance(a_donor, a_target);
    }

    bool CopyAppearance(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_target) {
        return FaceManager::GetSingleton().CopyAppearance(a_donor, a_target);
    }

    bool RevertAppearance(RE::StaticFunctionTag*, RE::Actor* a_target) {
        return FaceManager::GetSingleton().RevertAppearance(a_target);
    }

    bool TransferInventoryToPlayer(RE::StaticFunctionTag*, RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        return InventoryManager::GetSingleton().StoreAndTransferToPlayer(a_player, a_donor, a_storageFollower);
    }

    bool RestoreInventoryFromPlayer(RE::StaticFunctionTag*, RE::Actor* a_player, RE::Actor* a_donor, RE::Actor* a_storageFollower) {
        return InventoryManager::GetSingleton().RestoreAndTransferFromPlayer(a_player, a_donor, a_storageFollower);
    }

    bool TransferFactions(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_player) {
        return FactionManager::GetSingleton().TransferFactionsToPlayer(a_player, a_donor);
    }

    bool RestoreFactions(RE::StaticFunctionTag*, RE::Actor* a_player) {
        return FactionManager::GetSingleton().RestoreFactionsFromPlayer(a_player);
    }

    bool TransferStats(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_player) {
        return StatManager::GetSingleton().TransferStatsToPlayer(a_player, a_donor);
    }

    bool CopyStats(RE::StaticFunctionTag*, RE::Actor* a_fromActor, RE::Actor* a_toActor) {
        return StatManager::GetSingleton().CopyStats(a_fromActor, a_toActor);
    }

    bool RestoreStats(RE::StaticFunctionTag*, RE::Actor* a_player) {
        return StatManager::GetSingleton().RestoreStatsFromPlayer(a_player);
    }

    void SetIdleAIEnabled(RE::StaticFunctionTag*, bool a_enabled) {
        IdleAIManager::GetSingleton().SetIdleAIEnabled(a_enabled);
    }

    void SetIdleAITimeout(RE::StaticFunctionTag*, float a_seconds) {
        IdleAIManager::GetSingleton().SetIdleTimeout(a_seconds);
    }

    void SetControllingNPC(RE::StaticFunctionTag*, bool a_controlling) {
        IdleAIManager::GetSingleton().SetControllingNPC(a_controlling);
    }

    void UpdateIdleAI(RE::StaticFunctionTag*) {
        IdleAIManager::GetSingleton().Update();
    }

    bool OpenSelfDialogue(RE::StaticFunctionTag*, RE::Actor* a_speaker) {
        return IdleAIManager::GetSingleton().OpenSelfDialogue(a_speaker);
    }

    bool TriggerVoiceLine(RE::StaticFunctionTag*, RE::Actor* a_speaker) {
        return IdleAIManager::GetSingleton().TriggerVoiceLine(a_speaker);
    }

    std::int32_t GetActorSex(RE::StaticFunctionTag*, RE::Actor* a_actor) {
        if (!a_actor) return 0;
        auto* base = a_actor->GetActorBase();
        if (!base) return 0;
        return static_cast<std::int32_t>(base->GetSex());
    }

    RE::BSFixedString GetOriginalPlayerName(RE::StaticFunctionTag*) {
        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            const auto& origFace = FaceManager::GetSingleton().GetSavedAppearance();
            if (!origFace.fullName.empty()) {
                return origFace.fullName;
            }
        }
        const auto& impOrig = ImpersonationManager::GetSingleton().GetOrigPlayerName();
        if (!impOrig.empty()) {
            return RE::BSFixedString(impOrig);
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->GetActorBase()) {
            return player->GetActorBase()->fullName;
        }
        return ""sv;
    }

    std::int32_t BeginImpersonation(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_player) {
        return ImpersonationManager::GetSingleton().Begin(a_donor, a_player);
    }

    std::int32_t EndImpersonation(RE::StaticFunctionTag*) {
        return ImpersonationManager::GetSingleton().End();
    }

    void SetImpersonationOptions(RE::StaticFunctionTag*, bool a_conditions, bool a_aliases, bool a_packages, bool a_scripts) {
        ImpersonationManager::GetSingleton().SetOptions(a_conditions, a_aliases, a_packages, a_scripts);
    }

    void SetPuppetActor(RE::StaticFunctionTag*, RE::Actor* a_puppet) {
        ImpersonationManager::GetSingleton().SetPuppet(a_puppet);
    }

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            SKSE::log::error("Papyrus VM is null!");
            return false;
        }

        constexpr std::string_view className = "AK_NG";

        a_vm->RegisterFunction("CloneAppearance", className, CloneAppearance);
        a_vm->RegisterFunction("CopyAppearance", className, CopyAppearance);
        a_vm->RegisterFunction("RevertAppearance", className, RevertAppearance);
        a_vm->RegisterFunction("TransferInventoryToPlayer", className, TransferInventoryToPlayer);
        a_vm->RegisterFunction("RestoreInventoryFromPlayer", className, RestoreInventoryFromPlayer);
        a_vm->RegisterFunction("TransferFactions", className, TransferFactions);
        a_vm->RegisterFunction("RestoreFactions", className, RestoreFactions);
        a_vm->RegisterFunction("TransferStats", className, TransferStats);
        a_vm->RegisterFunction("CopyStats", className, CopyStats);
        a_vm->RegisterFunction("RestoreStats", className, RestoreStats);
        a_vm->RegisterFunction("SetIdleAIEnabled", className, SetIdleAIEnabled);
        a_vm->RegisterFunction("SetIdleAITimeout", className, SetIdleAITimeout);
        a_vm->RegisterFunction("SetControllingNPC", className, SetControllingNPC);
        a_vm->RegisterFunction("UpdateIdleAI", className, UpdateIdleAI);
        a_vm->RegisterFunction("OpenSelfDialogue", className, OpenSelfDialogue);
        a_vm->RegisterFunction("TriggerVoiceLine", className, TriggerVoiceLine);
        a_vm->RegisterFunction("GetActorSex", className, GetActorSex);
        a_vm->RegisterFunction("BeginImpersonation", className, BeginImpersonation);
        a_vm->RegisterFunction("EndImpersonation", className, EndImpersonation);
        a_vm->RegisterFunction("SetImpersonationOptions", className, SetImpersonationOptions);
        a_vm->RegisterFunction("SetPuppetActor", className, SetPuppetActor);
        a_vm->RegisterFunction("GetOriginalPlayerName", className, GetOriginalPlayerName);

        SKSE::log::info("Registered AK_NG Papyrus functions.");
        return true;
    }

}
