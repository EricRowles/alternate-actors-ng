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

    std::int32_t SwapAliases(RE::StaticFunctionTag*, RE::Actor* a_donor, RE::Actor* a_player) {
        return AliasManager::GetSingleton().SwapAliasesToPlayer(a_donor, a_player);
    }

    std::int32_t RevertAliases(RE::StaticFunctionTag*) {
        return AliasManager::GetSingleton().RestoreAliases();
    }

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

    bool IsIdleAIActive(RE::StaticFunctionTag*) {
        return IdleAIManager::GetSingleton().IsAIActive();
    }

    void SetControllingNPC(RE::StaticFunctionTag*, bool a_controlling) {
        IdleAIManager::GetSingleton().SetControllingNPC(a_controlling);
    }

    void UpdateIdleAI(RE::StaticFunctionTag*) {
        IdleAIManager::GetSingleton().Update();
    }

    void SetDialogueHotkey(RE::StaticFunctionTag*, std::int32_t a_key) {
        IdleAIManager::GetSingleton().SetDialogueHotkey(a_key);
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

    bool IsImpersonating(RE::StaticFunctionTag*) {
        return ImpersonationManager::GetSingleton().IsActive();
    }

    RE::Actor* GetImpersonatedActor(RE::StaticFunctionTag*) {
        return ImpersonationManager::GetSingleton().GetDonor();
    }

    void SetPuppetActor(RE::StaticFunctionTag*, RE::Actor* a_puppet) {
        ImpersonationManager::GetSingleton().SetPuppet(a_puppet);
    }

    // ControlMap::ToggleControls notifies camera/input handlers and must run on the main thread,
    // not on a Papyrus VM thread, otherwise it races the render/update loop.
    static void ToggleControlsOnMainThread(
        bool a_enable,
        bool a_movement,
        bool a_fighting,
        bool a_camSwitch,
        bool a_looking,
        bool a_sneaking,
        bool a_menu,
        bool a_activate)
    {
        using Flag = RE::UserEvents::USER_EVENT_FLAG;
        std::uint32_t mask = 0;
        if (a_movement) mask |= static_cast<std::uint32_t>(Flag::kMovement);
        if (a_fighting) mask |= static_cast<std::uint32_t>(Flag::kFighting);
        if (a_camSwitch) mask |= static_cast<std::uint32_t>(Flag::kPOVSwitch);
        if (a_looking) mask |= static_cast<std::uint32_t>(Flag::kLooking);
        if (a_sneaking) mask |= static_cast<std::uint32_t>(Flag::kSneaking);
        if (a_menu) mask |= static_cast<std::uint32_t>(Flag::kMenu);
        if (a_activate) mask |= static_cast<std::uint32_t>(Flag::kActivate);

        SKSE::log::info("{}PlayerControls requested (mask=0x{:X}), queueing to main thread.",
            a_enable ? "Enable" : "Disable", mask);

        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) return;
        tasks->AddTask([mask, a_enable]() {
            auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) return;
            controlMap->ToggleControls(static_cast<Flag>(mask), a_enable);
            SKSE::log::info("{}PlayerControls applied on main thread (mask=0x{:X}).",
                a_enable ? "Enable" : "Disable", mask);
        });
    }

    void DisablePlayerControls(
        RE::StaticFunctionTag*,
        bool a_movement,
        bool a_fighting,
        bool a_camSwitch,
        bool a_looking,
        bool a_sneaking,
        bool a_menu,
        bool a_activate)
    {
        ToggleControlsOnMainThread(false, a_movement, a_fighting, a_camSwitch, a_looking, a_sneaking, a_menu, a_activate);
    }

    void EnablePlayerControls(
        RE::StaticFunctionTag*,
        bool a_movement,
        bool a_fighting,
        bool a_camSwitch,
        bool a_looking,
        bool a_sneaking,
        bool a_menu,
        bool a_activate)
    {
        ToggleControlsOnMainThread(true, a_movement, a_fighting, a_camSwitch, a_looking, a_sneaking, a_menu, a_activate);
    }

    bool Register(RE::BSScript::IVirtualMachine* a_vm) {
        if (!a_vm) {
            SKSE::log::error("Papyrus VM is null!");
            return false;
        }

        constexpr std::string_view className = "AK_NG";

        a_vm->RegisterFunction("SwapAliases", className, SwapAliases);
        a_vm->RegisterFunction("RevertAliases", className, RevertAliases);
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
        a_vm->RegisterFunction("IsIdleAIActive", className, IsIdleAIActive);
        a_vm->RegisterFunction("SetControllingNPC", className, SetControllingNPC);
        a_vm->RegisterFunction("UpdateIdleAI", className, UpdateIdleAI);
        a_vm->RegisterFunction("SetDialogueHotkey", className, SetDialogueHotkey);
        a_vm->RegisterFunction("OpenSelfDialogue", className, OpenSelfDialogue);
        a_vm->RegisterFunction("TriggerVoiceLine", className, TriggerVoiceLine);
        a_vm->RegisterFunction("GetActorSex", className, GetActorSex);
        a_vm->RegisterFunction("BeginImpersonation", className, BeginImpersonation);
        a_vm->RegisterFunction("EndImpersonation", className, EndImpersonation);
        a_vm->RegisterFunction("SetImpersonationOptions", className, SetImpersonationOptions);
        a_vm->RegisterFunction("IsImpersonating", className, IsImpersonating);
        a_vm->RegisterFunction("GetImpersonatedActor", className, GetImpersonatedActor);
        a_vm->RegisterFunction("SetPuppetActor", className, SetPuppetActor);
        a_vm->RegisterFunction("GetOriginalPlayerName", className, GetOriginalPlayerName);
        a_vm->RegisterFunction("DisablePlayerControls", className, DisablePlayerControls);
        a_vm->RegisterFunction("EnablePlayerControls", className, EnablePlayerControls);

        SKSE::log::info("Registered AK_NG Papyrus functions.");
        return true;
    }

}
