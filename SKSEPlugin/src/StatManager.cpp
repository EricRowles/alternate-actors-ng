#include "PCH.h"
#include "StatManager.h"

namespace AlternateActors {

    constexpr RE::ActorValue kCoreStatsAndSkills[] = {
        // Attributes
        RE::ActorValue::kHealth,
        RE::ActorValue::kMagicka,
        RE::ActorValue::kStamina,

        // Combat Skills
        RE::ActorValue::kOneHanded,
        RE::ActorValue::kTwoHanded,
        RE::ActorValue::kArchery,
        RE::ActorValue::kBlock,
        RE::ActorValue::kSmithing,
        RE::ActorValue::kHeavyArmor,

        // Stealth Skills
        RE::ActorValue::kLightArmor,
        RE::ActorValue::kPickpocket,
        RE::ActorValue::kLockpicking,
        RE::ActorValue::kSneak,
        RE::ActorValue::kAlchemy,
        RE::ActorValue::kSpeech,

        // Magic Skills
        RE::ActorValue::kAlteration,
        RE::ActorValue::kConjuration,
        RE::ActorValue::kDestruction,
        RE::ActorValue::kIllusion,
        RE::ActorValue::kRestoration,
        RE::ActorValue::kEnchanting
    };

    bool StatManager::CaptureSnapshot(RE::Actor* a_actor, StatSnapshot& a_outSnapshot) {
        if (!a_actor) return false;

        auto* avOwner = a_actor->AsActorValueOwner();
        if (!avOwner) return false;

        a_outSnapshot.baseValues.clear();
        for (auto av : kCoreStatsAndSkills) {
            float val = avOwner->GetBaseActorValue(av);
            a_outSnapshot.baseValues[static_cast<std::uint32_t>(av)] = val;
        }

        return true;
    }

    bool StatManager::ApplySnapshot(RE::Actor* a_targetActor, const StatSnapshot& a_snapshot) {
        if (!a_targetActor) return false;

        auto* avOwner = a_targetActor->AsActorValueOwner();
        if (!avOwner) return false;

        for (const auto& [avInt, val] : a_snapshot.baseValues) {
            auto av = static_cast<RE::ActorValue>(avInt);
            avOwner->SetBaseActorValue(av, val);
        }

        // Restore pool values (damage modifier = 0) so actor doesn't suffer broken health/stamina/magicka pools
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, avOwner->GetBaseActorValue(RE::ActorValue::kHealth));
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, avOwner->GetBaseActorValue(RE::ActorValue::kMagicka));
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, avOwner->GetBaseActorValue(RE::ActorValue::kStamina));

        SKSE::log::info("Applied {} stat/skill values to actor {:08X}.",
            a_snapshot.baseValues.size(),
            static_cast<std::uint32_t>(a_targetActor->GetFormID()));

        return true;
    }

    bool StatManager::CopyStats(RE::Actor* a_fromActor, RE::Actor* a_toActor) {
        if (!a_fromActor || !a_toActor) {
            SKSE::log::warn("CopyStats called with null actor pointers.");
            return false;
        }

        StatSnapshot snapshot;
        if (!CaptureSnapshot(a_fromActor, snapshot)) {
            SKSE::log::error("Failed to capture stats from actor {:08X}.",
                static_cast<std::uint32_t>(a_fromActor->GetFormID()));
            return false;
        }

        return ApplySnapshot(a_toActor, snapshot);
    }

    bool StatManager::TransferStatsToPlayer(RE::Actor* a_player, RE::Actor* a_donor) {
        if (!a_player || !a_donor) {
            SKSE::log::warn("TransferStatsToPlayer called with null actor pointers.");
            return false;
        }

        // Save player's original stats if not already saved
        if (!_hasSavedStats) {
            if (CaptureSnapshot(a_player, _savedPlayerStats)) {
                _hasSavedStats = true;
                SKSE::log::info("Saved original stats of player actor {:08X} before transfer.",
                    static_cast<std::uint32_t>(a_player->GetFormID()));
            }
        }

        return CopyStats(a_donor, a_player);
    }

    bool StatManager::RestoreStatsFromPlayer(RE::Actor* a_player) {
        if (!a_player) return false;

        if (!_hasSavedStats) {
            SKSE::log::warn("RestoreStatsFromPlayer called with no saved stats.");
            return false;
        }

        bool result = ApplySnapshot(a_player, _savedPlayerStats);
        if (result) {
            _hasSavedStats = false;
            SKSE::log::info("Restored original player stats successfully.");
        }
        return result;
    }

}
