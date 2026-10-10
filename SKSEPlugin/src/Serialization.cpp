#include "PCH.h"
#include "Serialization.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "InventoryManager.h"
#include "FactionManager.h"
#include "StatManager.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"

namespace AlternateActors::Serialization {

    void Register() {
        auto* serde = SKSE::GetSerializationInterface();
        if (!serde) {
            SKSE::log::error("Failed to obtain SKSE Serialization Interface!");
            return;
        }

        serde->SetUniqueID('ALTA');
        serde->SetSaveCallback(SaveCallback);
        serde->SetLoadCallback(LoadCallback);
        serde->SetRevertCallback(RevertCallback);
        SKSE::log::info("Registered SKSE Co-Save Serialization callbacks for ALTA.");
    }

    void SaveCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        // 1. Save Swapped Aliases
        const auto& swappedAliases = AliasManager::GetSingleton().GetRecords();
        if (!swappedAliases.empty()) {
            if (a_serde->OpenRecord(kAliasDataRecord, kSerializationVersion)) {
                std::uint32_t count = static_cast<std::uint32_t>(swappedAliases.size());
                a_serde->WriteRecordData(&count, sizeof(count));
                for (const auto& record : swappedAliases) {
                    a_serde->WriteRecordData(&record.questFormID, sizeof(record.questFormID));
                    a_serde->WriteRecordData(&record.aliasID, sizeof(record.aliasID));
                    a_serde->WriteRecordData(&record.originalActorFormID, sizeof(record.originalActorFormID));
                }
            }
        }

        // 2. Save Face Snapshot
        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            const auto& face = FaceManager::GetSingleton().GetSavedAppearance();
            if (a_serde->OpenRecord(kFaceDataRecord, kSerializationVersion)) {
                RE::FormID raceID = face.race ? face.race->GetFormID() : 0;
                RE::FormID faceNPCID = face.faceNPC ? face.faceNPC->GetFormID() : 0;
                RE::FormID hairColorID = face.hairColor ? face.hairColor->GetFormID() : 0;
                RE::FormID voiceTypeID = face.voiceType ? face.voiceType->GetFormID() : 0;
                RE::FormID classID = face.npcClass ? face.npcClass->GetFormID() : 0;
                RE::FormID combatStyleID = face.combatStyle ? face.combatStyle->GetFormID() : 0;
                RE::FormID defaultPackListID = face.defaultPackList ? face.defaultPackList->GetFormID() : 0;

                a_serde->WriteRecordData(&raceID, sizeof(raceID));
                a_serde->WriteRecordData(&faceNPCID, sizeof(faceNPCID));
                a_serde->WriteRecordData(&hairColorID, sizeof(hairColorID));
                a_serde->WriteRecordData(&voiceTypeID, sizeof(voiceTypeID));
                a_serde->WriteRecordData(&face.bodyTintColor, sizeof(face.bodyTintColor));
                a_serde->WriteRecordData(&face.height, sizeof(face.height));
                a_serde->WriteRecordData(&face.weight, sizeof(face.weight));

                std::uint8_t sexVal = static_cast<std::uint8_t>(face.sex);
                a_serde->WriteRecordData(&sexVal, sizeof(sexVal));

                a_serde->WriteRecordData(face.morphs, sizeof(face.morphs));
                a_serde->WriteRecordData(face.parts, sizeof(face.parts));

                std::uint32_t hpCount = static_cast<std::uint32_t>(face.headParts.size());
                a_serde->WriteRecordData(&hpCount, sizeof(hpCount));
                for (auto* hp : face.headParts) {
                    RE::FormID hpID = hp ? hp->GetFormID() : 0;
                    a_serde->WriteRecordData(&hpID, sizeof(hpID));
                }

                a_serde->WriteRecordData(&face.hasHairTintColor, sizeof(face.hasHairTintColor));
                a_serde->WriteRecordData(&face.hairTintColor.red, sizeof(face.hairTintColor.red));
                a_serde->WriteRecordData(&face.hairTintColor.green, sizeof(face.hairTintColor.green));
                a_serde->WriteRecordData(&face.hairTintColor.blue, sizeof(face.hairTintColor.blue));

                a_serde->WriteRecordData(&classID, sizeof(classID));
                a_serde->WriteRecordData(&combatStyleID, sizeof(combatStyleID));
                a_serde->WriteRecordData(&defaultPackListID, sizeof(defaultPackListID));
                a_serde->WriteRecordData(&face.aiData, sizeof(face.aiData));

                std::uint32_t pkgCount = static_cast<std::uint32_t>(face.packages.size());
                a_serde->WriteRecordData(&pkgCount, sizeof(pkgCount));
                for (auto* pkg : face.packages) {
                    RE::FormID pkgID = pkg ? pkg->GetFormID() : 0;
                    a_serde->WriteRecordData(&pkgID, sizeof(pkgID));
                }
            }
        }

        // 3. Save Dynamic Spells & Worn Items
        const auto& transferredSpells = InventoryManager::GetSingleton().GetTransferredSpellIDs();
        const auto& savedPlayerSpells = InventoryManager::GetSingleton().GetSavedPlayerSpellIDs();
        const auto& savedPlayerWorn = InventoryManager::GetSingleton().GetSavedPlayerWornFormIDs();
        if (!transferredSpells.empty() || !savedPlayerSpells.empty() || !savedPlayerWorn.empty()) {
            if (a_serde->OpenRecord(kInventoryDataRecord, kSerializationVersion)) {
                std::uint32_t transCount = static_cast<std::uint32_t>(transferredSpells.size());
                a_serde->WriteRecordData(&transCount, sizeof(transCount));
                for (auto id : transferredSpells) {
                    a_serde->WriteRecordData(&id, sizeof(id));
                }

                std::uint32_t savedCount = static_cast<std::uint32_t>(savedPlayerSpells.size());
                a_serde->WriteRecordData(&savedCount, sizeof(savedCount));
                for (auto id : savedPlayerSpells) {
                    a_serde->WriteRecordData(&id, sizeof(id));
                }

                std::uint32_t wornCount = static_cast<std::uint32_t>(savedPlayerWorn.size());
                a_serde->WriteRecordData(&wornCount, sizeof(wornCount));
                for (auto id : savedPlayerWorn) {
                    a_serde->WriteRecordData(&id, sizeof(id));
                }
            }
        }

        // 4. Save Factions
        if (FactionManager::GetSingleton().HasSavedFactions()) {
            const auto& factions = FactionManager::GetSingleton().GetSavedPlayerFactions();
            auto* crimeFaction = FactionManager::GetSingleton().GetSavedCrimeFaction();
            if (a_serde->OpenRecord(kFactionDataRecord, kSerializationVersion)) {
                RE::FormID crimeID = crimeFaction ? crimeFaction->GetFormID() : 0;
                a_serde->WriteRecordData(&crimeID, sizeof(crimeID));

                std::uint32_t count = static_cast<std::uint32_t>(factions.size());
                a_serde->WriteRecordData(&count, sizeof(count));
                for (const auto& rec : factions) {
                    RE::FormID factID = rec.faction ? rec.faction->GetFormID() : 0;
                    a_serde->WriteRecordData(&factID, sizeof(factID));
                    a_serde->WriteRecordData(&rec.rank, sizeof(rec.rank));
                }
            }
        }

        // 5. Save Stats
        if (StatManager::GetSingleton().HasSavedStats()) {
            const auto& stats = StatManager::GetSingleton().GetSavedPlayerStats();
            if (a_serde->OpenRecord(kStatDataRecord, kSerializationVersion)) {
                std::uint32_t count = static_cast<std::uint32_t>(stats.baseValues.size());
                a_serde->WriteRecordData(&count, sizeof(count));
                for (const auto& [av, val] : stats.baseValues) {
                    a_serde->WriteRecordData(&av, sizeof(av));
                    a_serde->WriteRecordData(&val, sizeof(val));
                }
            }
        }

        // 6. Save Idle AI, Hotkey & Active Donor Actor
        if (a_serde->OpenRecord(kIdleAIDataRecord, kSerializationVersion)) {
            bool enabled = IdleAIManager::GetSingleton().IsIdleAIEnabled();
            float timeout = IdleAIManager::GetSingleton().GetIdleTimeout();
            std::int32_t hotkey = IdleAIManager::GetSingleton().GetDialogueHotkey();
            RE::FormID donorID = IdleAIManager::GetSingleton().GetActiveDonorActor() ?
                IdleAIManager::GetSingleton().GetActiveDonorActor()->GetFormID() : 0;

            a_serde->WriteRecordData(&enabled, sizeof(enabled));
            a_serde->WriteRecordData(&timeout, sizeof(timeout));
            a_serde->WriteRecordData(&hotkey, sizeof(hotkey));
            a_serde->WriteRecordData(&donorID, sizeof(donorID));
        }

        // 7. Save Impersonation State
        ImpersonationManager::GetSingleton().Save(a_serde);

        SKSE::log::info("Completed SKSE Co-Save serialization write.");
    }

    void LoadCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        RevertCallback(a_serde);

        std::uint32_t type;
        std::uint32_t version;
        std::uint32_t length;

        while (a_serde->GetNextRecordInfo(type, version, length)) {
            if (version != kSerializationVersion) {
                SKSE::log::warn("Skipping record {:08X} with unknown version {}", type, version);
                continue;
            }

            switch (type) {
                case kAliasDataRecord: {
                    std::uint32_t count = 0;
                    a_serde->ReadRecordData(&count, sizeof(count));
                    for (std::uint32_t i = 0; i < count; ++i) {
                        SwappedAliasRecord record{};
                        a_serde->ReadRecordData(&record.questFormID, sizeof(record.questFormID));
                        a_serde->ReadRecordData(&record.aliasID, sizeof(record.aliasID));
                        a_serde->ReadRecordData(&record.originalActorFormID, sizeof(record.originalActorFormID));

                        a_serde->ResolveFormID(record.questFormID, record.questFormID);
                        a_serde->ResolveFormID(record.originalActorFormID, record.originalActorFormID);
                        AliasManager::GetSingleton().AddRecord(record);
                    }
                    SKSE::log::info("Loaded {} swapped alias records from co-save.", count);
                    break;
                }

                case kFaceDataRecord: {
                    FaceSnapshot face{};
                    RE::FormID raceID = 0, faceNPCID = 0, hairColorID = 0, voiceTypeID = 0;
                    RE::FormID classID = 0, combatStyleID = 0, defaultPackListID = 0;

                    a_serde->ReadRecordData(&raceID, sizeof(raceID));
                    a_serde->ReadRecordData(&faceNPCID, sizeof(faceNPCID));
                    a_serde->ReadRecordData(&hairColorID, sizeof(hairColorID));
                    a_serde->ReadRecordData(&voiceTypeID, sizeof(voiceTypeID));
                    a_serde->ReadRecordData(&face.bodyTintColor, sizeof(face.bodyTintColor));
                    a_serde->ReadRecordData(&face.height, sizeof(face.height));
                    a_serde->ReadRecordData(&face.weight, sizeof(face.weight));

                    std::uint8_t sexVal = 0;
                    a_serde->ReadRecordData(&sexVal, sizeof(sexVal));
                    face.sex = static_cast<RE::SEX>(sexVal);

                    a_serde->ReadRecordData(face.morphs, sizeof(face.morphs));
                    a_serde->ReadRecordData(face.parts, sizeof(face.parts));

                    std::uint32_t hpCount = 0;
                    a_serde->ReadRecordData(&hpCount, sizeof(hpCount));
                    for (std::uint32_t i = 0; i < hpCount; ++i) {
                        RE::FormID hpID = 0;
                        a_serde->ReadRecordData(&hpID, sizeof(hpID));
                        if (a_serde->ResolveFormID(hpID, hpID)) {
                            auto* hp = RE::TESForm::LookupByID<RE::BGSHeadPart>(hpID);
                            if (hp) face.headParts.push_back(hp);
                        }
                    }

                    a_serde->ReadRecordData(&face.hasHairTintColor, sizeof(face.hasHairTintColor));
                    a_serde->ReadRecordData(&face.hairTintColor.red, sizeof(face.hairTintColor.red));
                    a_serde->ReadRecordData(&face.hairTintColor.green, sizeof(face.hairTintColor.green));
                    a_serde->ReadRecordData(&face.hairTintColor.blue, sizeof(face.hairTintColor.blue));

                    a_serde->ReadRecordData(&classID, sizeof(classID));
                    a_serde->ReadRecordData(&combatStyleID, sizeof(combatStyleID));
                    a_serde->ReadRecordData(&defaultPackListID, sizeof(defaultPackListID));
                    a_serde->ReadRecordData(&face.aiData, sizeof(face.aiData));

                    std::uint32_t pkgCount = 0;
                    a_serde->ReadRecordData(&pkgCount, sizeof(pkgCount));
                    for (std::uint32_t i = 0; i < pkgCount; ++i) {
                        RE::FormID pkgID = 0;
                        a_serde->ReadRecordData(&pkgID, sizeof(pkgID));
                        if (a_serde->ResolveFormID(pkgID, pkgID)) {
                            auto* pkg = RE::TESForm::LookupByID<RE::TESPackage>(pkgID);
                            if (pkg) face.packages.push_back(pkg);
                        }
                    }

                    if (a_serde->ResolveFormID(raceID, raceID)) {
                        face.race = RE::TESForm::LookupByID<RE::TESRace>(raceID);
                    }
                    if (a_serde->ResolveFormID(faceNPCID, faceNPCID)) {
                        face.faceNPC = RE::TESForm::LookupByID<RE::TESNPC>(faceNPCID);
                    }
                    if (a_serde->ResolveFormID(hairColorID, hairColorID)) {
                        face.hairColor = RE::TESForm::LookupByID<RE::BGSColorForm>(hairColorID);
                    }
                    if (a_serde->ResolveFormID(voiceTypeID, voiceTypeID)) {
                        face.voiceType = RE::TESForm::LookupByID<RE::BGSVoiceType>(voiceTypeID);
                    }
                    if (a_serde->ResolveFormID(classID, classID)) {
                        face.npcClass = RE::TESForm::LookupByID<RE::TESClass>(classID);
                    }
                    if (a_serde->ResolveFormID(combatStyleID, combatStyleID)) {
                        face.combatStyle = RE::TESForm::LookupByID<RE::TESCombatStyle>(combatStyleID);
                    }
                    if (a_serde->ResolveFormID(defaultPackListID, defaultPackListID)) {
                        face.defaultPackList = RE::TESForm::LookupByID<RE::BGSListForm>(defaultPackListID);
                    }

                    FaceManager::GetSingleton().SetSavedAppearance(face);
                    SKSE::log::info("Loaded saved player face appearance from co-save.");
                    break;
                }

                case kInventoryDataRecord: {
                    std::uint32_t transCount = 0;
                    a_serde->ReadRecordData(&transCount, sizeof(transCount));
                    for (std::uint32_t i = 0; i < transCount; ++i) {
                        RE::FormID spellID = 0;
                        a_serde->ReadRecordData(&spellID, sizeof(spellID));
                        if (a_serde->ResolveFormID(spellID, spellID)) {
                            InventoryManager::GetSingleton().AddTransferredSpellID(spellID);
                        }
                    }

                    std::uint32_t savedCount = 0;
                    a_serde->ReadRecordData(&savedCount, sizeof(savedCount));
                    for (std::uint32_t i = 0; i < savedCount; ++i) {
                        RE::FormID spellID = 0;
                        a_serde->ReadRecordData(&spellID, sizeof(spellID));
                        if (a_serde->ResolveFormID(spellID, spellID)) {
                            InventoryManager::GetSingleton().AddSavedPlayerSpellID(spellID);
                        }
                    }

                    std::uint32_t wornCount = 0;
                    if (a_serde->ReadRecordData(&wornCount, sizeof(wornCount))) {
                        for (std::uint32_t i = 0; i < wornCount; ++i) {
                            RE::FormID itemID = 0;
                            a_serde->ReadRecordData(&itemID, sizeof(itemID));
                            if (a_serde->ResolveFormID(itemID, itemID)) {
                                InventoryManager::GetSingleton().AddSavedPlayerWornFormID(itemID);
                            }
                        }
                    }
                    SKSE::log::info("Loaded {} transferred spells, {} saved player spells, and {} worn items from co-save.",
                        transCount, savedCount, wornCount);
                    break;
                }

                case kFactionDataRecord: {
                    RE::FormID crimeID = 0;
                    a_serde->ReadRecordData(&crimeID, sizeof(crimeID));
                    RE::TESFaction* crimeFaction = nullptr;
                    if (a_serde->ResolveFormID(crimeID, crimeID)) {
                        crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(crimeID);
                    }

                    std::uint32_t count = 0;
                    a_serde->ReadRecordData(&count, sizeof(count));
                    std::vector<FactionRecord> factions;
                    for (std::uint32_t i = 0; i < count; ++i) {
                        RE::FormID factID = 0;
                        std::int8_t rank = 0;
                        a_serde->ReadRecordData(&factID, sizeof(factID));
                        a_serde->ReadRecordData(&rank, sizeof(rank));
                        if (a_serde->ResolveFormID(factID, factID)) {
                            auto* fact = RE::TESForm::LookupByID<RE::TESFaction>(factID);
                            if (fact) {
                                factions.push_back({ fact, rank });
                            }
                        }
                    }
                    FactionManager::GetSingleton().SetSavedState(factions, crimeFaction);
                    SKSE::log::info("Loaded {} saved player factions from co-save.", factions.size());
                    break;
                }

                case kStatDataRecord: {
                    std::uint32_t count = 0;
                    a_serde->ReadRecordData(&count, sizeof(count));
                    StatSnapshot snapshot{};
                    for (std::uint32_t i = 0; i < count; ++i) {
                        std::uint32_t av = 0;
                        float val = 0.0f;
                        a_serde->ReadRecordData(&av, sizeof(av));
                        a_serde->ReadRecordData(&val, sizeof(val));
                        snapshot.baseValues[av] = val;
                    }
                    StatManager::GetSingleton().SetSavedPlayerStats(snapshot);
                    SKSE::log::info("Loaded {} saved player stats from co-save.", count);
                    break;
                }

                case kIdleAIDataRecord: {
                    bool enabled = false;
                    float timeout = 10.0f;
                    std::int32_t hotkey = 47;
                    RE::FormID donorID = 0;

                    a_serde->ReadRecordData(&enabled, sizeof(enabled));
                    a_serde->ReadRecordData(&timeout, sizeof(timeout));
                    a_serde->ReadRecordData(&hotkey, sizeof(hotkey));
                    if (length >= (sizeof(enabled) + sizeof(timeout) + sizeof(hotkey) + sizeof(donorID))) {
                        a_serde->ReadRecordData(&donorID, sizeof(donorID));
                        if (a_serde->ResolveFormID(donorID, donorID)) {
                            auto* donor = RE::TESForm::LookupByID<RE::Actor>(donorID);
                            IdleAIManager::GetSingleton().SetActiveDonorActor(donor);
                        }
                    }

                    IdleAIManager::GetSingleton().SetIdleAIEnabled(enabled);
                    IdleAIManager::GetSingleton().SetIdleTimeout(timeout);
                    IdleAIManager::GetSingleton().SetDialogueHotkey(hotkey);
                    SKSE::log::info("Loaded Idle AI state from co-save.");
                    break;
                }

                case kImpersonationDataRecord: {
                    ImpersonationManager::GetSingleton().Load(a_serde, version, length);
                    SKSE::log::info("Loaded Impersonation state from co-save.");
                    break;
                }

                default:
                    SKSE::log::warn("Unknown record type {:08X} in co-save", type);
                    break;
            }
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        AliasManager::GetSingleton().Clear();
        FaceManager::GetSingleton().ClearSavedAppearance();
        InventoryManager::GetSingleton().Clear();
        FactionManager::GetSingleton().Clear();
        StatManager::GetSingleton().Clear();
        IdleAIManager::GetSingleton().Clear();
        ImpersonationManager::GetSingleton().OnRevert();
        SKSE::log::info("Reverted all AlternateActors saved manager states.");
    }

}
