#pragma once

#include <mutex>
#include <vector>
#include <cstdint>

namespace AlternateActors {

    class AliasManager {
    public:
        static AliasManager& GetSingleton() {
            static AliasManager instance;
            return instance;
        }

        void Clear() {}

        // Returns all AI packages granted by active quest aliases that currently reference the actor,
        // sorted by quest priority (highest priority first).
        std::vector<RE::TESPackage*> GetAliasPackages(RE::Actor* a_actor) const;

        // Checks whether a quest is an internal Alternate Actors controller quest
        static bool IsModInternalQuest(RE::TESQuest* a_quest);

    private:
        AliasManager() = default;
        ~AliasManager() = default;
        AliasManager(const AliasManager&) = delete;
        AliasManager& operator=(const AliasManager&) = delete;

        mutable std::recursive_mutex _mutex;
    };

}
