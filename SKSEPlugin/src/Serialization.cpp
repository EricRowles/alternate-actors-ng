#include "PCH.h"
#include "Serialization.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "InventoryManager.h"
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

    static void WriteFaceSnapshotRecord(SKSE::SerializationInterface* a_serde, std::uint32_t a_recordType, const FaceSnapshot& a_face) {
        if (!a_serde->OpenRecord(a_recordType, kSerializationVersion)) {
            return;
        }

        RE::FormID raceID = a_face.race ? a_face.race->GetFormID() : 0;
        RE::FormID faceNPCID = (a_face.faceNPC && a_face.faceNPC->GetFormID() != 0x07) ? a_face.faceNPC->GetFormID() : 0;
        RE::FormID hairColorID = a_face.hairColor ? a_face.hairColor->GetFormID() : 0;
        RE::FormID voiceTypeID = a_face.voiceType ? a_face.voiceType->GetFormID() : 0;

        a_serde->WriteRecordData(&raceID, sizeof(raceID));
        a_serde->WriteRecordData(&faceNPCID, sizeof(faceNPCID));
        a_serde->WriteRecordData(&hairColorID, sizeof(hairColorID));
        a_serde->WriteRecordData(&voiceTypeID, sizeof(voiceTypeID));
        a_serde->WriteRecordData(&a_face.bodyTintColor, sizeof(a_face.bodyTintColor));
        a_serde->WriteRecordData(&a_face.height, sizeof(a_face.height));
        a_serde->WriteRecordData(&a_face.scale, sizeof(a_face.scale));
        a_serde->WriteRecordData(&a_face.weight, sizeof(a_face.weight));

        std::uint8_t sexVal = static_cast<std::uint8_t>(a_face.sex);
        a_serde->WriteRecordData(&sexVal, sizeof(sexVal));

        a_serde->WriteRecordData(a_face.morphs, sizeof(a_face.morphs));
        a_serde->WriteRecordData(a_face.parts, sizeof(a_face.parts));

        std::uint32_t hpCount = static_cast<std::uint32_t>(a_face.headParts.size());
        a_serde->WriteRecordData(&hpCount, sizeof(hpCount));
        for (auto* hp : a_face.headParts) {
            RE::FormID hpID = hp ? hp->GetFormID() : 0;
            a_serde->WriteRecordData(&hpID, sizeof(hpID));
        }

        a_serde->WriteRecordData(&a_face.hasHairTintColor, sizeof(a_face.hasHairTintColor));
        a_serde->WriteRecordData(&a_face.hairTintColor.red, sizeof(a_face.hairTintColor.red));
        a_serde->WriteRecordData(&a_face.hairTintColor.green, sizeof(a_face.hairTintColor.green));
        a_serde->WriteRecordData(&a_face.hairTintColor.blue, sizeof(a_face.hairTintColor.blue));

        // Tint layers
        std::uint32_t tintCount = static_cast<std::uint32_t>(a_face.tintLayers.size());
        a_serde->WriteRecordData(&tintCount, sizeof(tintCount));
        for (const auto& layer : a_face.tintLayers) {
            a_serde->WriteRecordData(&layer, sizeof(RE::TESNPC::Layer));
        }

        // Skin tint color
        a_serde->WriteRecordData(&a_face.hasSkinTintColor, sizeof(a_face.hasSkinTintColor));
        a_serde->WriteRecordData(&a_face.skinTintColor.red, sizeof(a_face.skinTintColor.red));
        a_serde->WriteRecordData(&a_face.skinTintColor.green, sizeof(a_face.skinTintColor.green));
        a_serde->WriteRecordData(&a_face.skinTintColor.blue, sizeof(a_face.skinTintColor.blue));

        // Full name
        std::string nameStr(a_face.fullName.c_str());
        std::uint32_t nameLen = static_cast<std::uint32_t>(nameStr.size());
        a_serde->WriteRecordData(&nameLen, sizeof(nameLen));
        if (nameLen > 0) {
            a_serde->WriteRecordData(nameStr.data(), nameLen);
        }

        // Face details texture set (v4+)
        RE::FormID faceDetailsID = a_face.faceDetails ? a_face.faceDetails->GetFormID() : 0;
        a_serde->WriteRecordData(&faceDetailsID, sizeof(faceDetailsID));
    }

    void SaveCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        // 1. Save Face Snapshot (Original appearance to revert to)
        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            WriteFaceSnapshotRecord(a_serde, kFaceDataRecord, FaceManager::GetSingleton().GetSavedAppearance());
        }

        // 1b. Save Active Donor Face Snapshot
        if (FaceManager::GetSingleton().HasDonorAppearance()) {
            WriteFaceSnapshotRecord(a_serde, kDonorFaceDataRecord, FaceManager::GetSingleton().GetDonorAppearance());
        }

        // 2. Save Dynamic Spells, Quest Items & Equipped Spells
        const auto& transferredSpells = InventoryManager::GetSingleton().GetTransferredSpellIDs();
        const auto& savedPlayerSpells = InventoryManager::GetSingleton().GetSavedPlayerSpellIDs();
        const auto& donorQuestItems = InventoryManager::GetSingleton().GetTransferredDonorQuestItems();
        auto* savedLeftSpell = InventoryManager::GetSingleton().GetSavedPlayerLeftSpell();
        auto* savedRightSpell = InventoryManager::GetSingleton().GetSavedPlayerRightSpell();
        auto* savedShout = InventoryManager::GetSingleton().GetSavedPlayerShout();

        if (!transferredSpells.empty() || !savedPlayerSpells.empty() || !donorQuestItems.empty() ||
            savedLeftSpell || savedRightSpell || savedShout) {
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

                // Donor quest items (v4+)
                std::uint32_t questCount = static_cast<std::uint32_t>(donorQuestItems.size());
                a_serde->WriteRecordData(&questCount, sizeof(questCount));
                for (auto id : donorQuestItems) {
                    a_serde->WriteRecordData(&id, sizeof(id));
                }

                // Equipped spells & shout (v4+)
                RE::FormID leftSpellID = savedLeftSpell ? savedLeftSpell->GetFormID() : 0;
                RE::FormID rightSpellID = savedRightSpell ? savedRightSpell->GetFormID() : 0;
                RE::FormID shoutID = savedShout ? savedShout->GetFormID() : 0;
                a_serde->WriteRecordData(&leftSpellID, sizeof(leftSpellID));
                a_serde->WriteRecordData(&rightSpellID, sizeof(rightSpellID));
                a_serde->WriteRecordData(&shoutID, sizeof(shoutID));
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

        // 6. Save Idle AI & Active Donor Actor
        if (a_serde->OpenRecord(kIdleAIDataRecord, kSerializationVersion)) {
            bool enabled = IdleAIManager::GetSingleton().IsIdleAIEnabled();
            float timeout = IdleAIManager::GetSingleton().GetIdleTimeout();
            RE::FormID donorID = IdleAIManager::GetSingleton().GetActiveDonorActor() ?
                IdleAIManager::GetSingleton().GetActiveDonorActor()->GetFormID() : 0;

            a_serde->WriteRecordData(&enabled, sizeof(enabled));
            a_serde->WriteRecordData(&timeout, sizeof(timeout));
            a_serde->WriteRecordData(&donorID, sizeof(donorID));
        }

        // 7. Save Impersonation State
        ImpersonationManager::GetSingleton().Save(a_serde);

        SKSE::log::info("Completed SKSE Co-Save serialization write.");
    }

    static bool ReadFaceSnapshotRecord(SKSE::SerializationInterface* a_serde, FaceSnapshot& a_outFace) {
        RE::FormID raceID = 0, faceNPCID = 0, hairColorID = 0, voiceTypeID = 0;

        if (!a_serde->ReadRecordData(&raceID, sizeof(raceID))) return false;
        a_serde->ReadRecordData(&faceNPCID, sizeof(faceNPCID));
        a_serde->ReadRecordData(&hairColorID, sizeof(hairColorID));
        a_serde->ReadRecordData(&voiceTypeID, sizeof(voiceTypeID));
        a_serde->ReadRecordData(&a_outFace.bodyTintColor, sizeof(a_outFace.bodyTintColor));
        a_serde->ReadRecordData(&a_outFace.height, sizeof(a_outFace.height));
        a_serde->ReadRecordData(&a_outFace.scale, sizeof(a_outFace.scale));
        a_serde->ReadRecordData(&a_outFace.weight, sizeof(a_outFace.weight));

        std::uint8_t sexVal = 0;
        a_serde->ReadRecordData(&sexVal, sizeof(sexVal));
        a_outFace.sex = static_cast<RE::SEX>(sexVal);

        a_serde->ReadRecordData(a_outFace.morphs, sizeof(a_outFace.morphs));
        a_serde->ReadRecordData(a_outFace.parts, sizeof(a_outFace.parts));

        std::uint32_t hpCount = 0;
        if (a_serde->ReadRecordData(&hpCount, sizeof(hpCount))) {
            for (std::uint32_t i = 0; i < hpCount; ++i) {
                RE::FormID hpID = 0;
                a_serde->ReadRecordData(&hpID, sizeof(hpID));
                if (a_serde->ResolveFormID(hpID, hpID)) {
                    auto* hp = RE::TESForm::LookupByID<RE::BGSHeadPart>(hpID);
                    if (hp) a_outFace.headParts.push_back(hp);
                }
            }
        }

        a_serde->ReadRecordData(&a_outFace.hasHairTintColor, sizeof(a_outFace.hasHairTintColor));
        a_serde->ReadRecordData(&a_outFace.hairTintColor.red, sizeof(a_outFace.hairTintColor.red));
        a_serde->ReadRecordData(&a_outFace.hairTintColor.green, sizeof(a_outFace.hairTintColor.green));
        a_serde->ReadRecordData(&a_outFace.hairTintColor.blue, sizeof(a_outFace.hairTintColor.blue));

        if (a_serde->ResolveFormID(raceID, raceID)) {
            a_outFace.race = RE::TESForm::LookupByID<RE::TESRace>(raceID);
        }
        if (a_serde->ResolveFormID(faceNPCID, faceNPCID)) {
            if (faceNPCID != 0x07) {
                a_outFace.faceNPC = RE::TESForm::LookupByID<RE::TESNPC>(faceNPCID);
            } else {
                a_outFace.faceNPC = nullptr;
            }
        }
        if (a_serde->ResolveFormID(hairColorID, hairColorID)) {
            a_outFace.hairColor = RE::TESForm::LookupByID<RE::BGSColorForm>(hairColorID);
        }
        if (a_serde->ResolveFormID(voiceTypeID, voiceTypeID)) {
            a_outFace.voiceType = RE::TESForm::LookupByID<RE::BGSVoiceType>(voiceTypeID);
        }

        // Tint layers
        std::uint32_t tintCount = 0;
        if (a_serde->ReadRecordData(&tintCount, sizeof(tintCount))) {
            for (std::uint32_t i = 0; i < tintCount; ++i) {
                RE::TESNPC::Layer layer{};
                if (a_serde->ReadRecordData(&layer, sizeof(RE::TESNPC::Layer))) {
                    a_outFace.tintLayers.push_back(layer);
                }
            }
        }

        // Skin tint color
        if (a_serde->ReadRecordData(&a_outFace.hasSkinTintColor, sizeof(a_outFace.hasSkinTintColor))) {
            a_serde->ReadRecordData(&a_outFace.skinTintColor.red, sizeof(a_outFace.skinTintColor.red));
            a_serde->ReadRecordData(&a_outFace.skinTintColor.green, sizeof(a_outFace.skinTintColor.green));
            a_serde->ReadRecordData(&a_outFace.skinTintColor.blue, sizeof(a_outFace.skinTintColor.blue));
        }

        std::uint32_t nameLen = 0;
        if (a_serde->ReadRecordData(&nameLen, sizeof(nameLen)) && nameLen > 0) {
            std::string nameStr(nameLen, '\0');
            if (a_serde->ReadRecordData(nameStr.data(), nameLen)) {
                a_outFace.fullName = nameStr.c_str();
            }
        }

        // Face details texture set
        RE::FormID faceDetailsID = 0;
        if (a_serde->ReadRecordData(&faceDetailsID, sizeof(faceDetailsID))) {
            if (faceDetailsID != 0 && a_serde->ResolveFormID(faceDetailsID, faceDetailsID)) {
                a_outFace.faceDetails = RE::TESForm::LookupByID<RE::BGSTextureSet>(faceDetailsID);
            }
        }

        return true;
    }

    void LoadCallback(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        RevertCallback(a_serde);

        std::uint32_t type;
        std::uint32_t version;
        std::uint32_t length;

        while (a_serde->GetNextRecordInfo(type, version, length)) {
            if (version > kSerializationVersion) {
                SKSE::log::warn("Skipping record {:08X} with newer unknown version {}", type, version);
                continue;
            }

            switch (type) {
                case kFaceDataRecord: {
                    FaceSnapshot face{};
                    if (ReadFaceSnapshotRecord(a_serde, face)) {
                        FaceManager::GetSingleton().SetSavedAppearance(face);
                        SKSE::log::info("Loaded saved player face appearance from co-save.");
                    }
                    break;
                }

                case kDonorFaceDataRecord: {
                    FaceSnapshot donorFace{};
                    if (ReadFaceSnapshotRecord(a_serde, donorFace)) {
                        FaceManager::GetSingleton().SetDonorAppearance(donorFace);
                        SKSE::log::info("Loaded saved donor face appearance from co-save.");
                    }
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

                    std::uint32_t questCount = 0;
                    if (a_serde->ReadRecordData(&questCount, sizeof(questCount))) {
                        for (std::uint32_t i = 0; i < questCount; ++i) {
                            RE::FormID qID = 0;
                            a_serde->ReadRecordData(&qID, sizeof(qID));
                            if (a_serde->ResolveFormID(qID, qID)) {
                                InventoryManager::GetSingleton().AddTransferredDonorQuestItem(qID);
                            }
                        }
                    }

                    RE::FormID leftSpellID = 0, rightSpellID = 0, shoutID = 0;
                    RE::SpellItem* leftSpell = nullptr;
                    RE::SpellItem* rightSpell = nullptr;
                    RE::TESShout* shout = nullptr;
                    if (a_serde->ReadRecordData(&leftSpellID, sizeof(leftSpellID))) {
                        if (leftSpellID && a_serde->ResolveFormID(leftSpellID, leftSpellID)) {
                            leftSpell = RE::TESForm::LookupByID<RE::SpellItem>(leftSpellID);
                        }
                        if (a_serde->ReadRecordData(&rightSpellID, sizeof(rightSpellID))) {
                            if (rightSpellID && a_serde->ResolveFormID(rightSpellID, rightSpellID)) {
                                rightSpell = RE::TESForm::LookupByID<RE::SpellItem>(rightSpellID);
                            }
                        }
                        if (a_serde->ReadRecordData(&shoutID, sizeof(shoutID))) {
                            if (shoutID && a_serde->ResolveFormID(shoutID, shoutID)) {
                                shout = RE::TESForm::LookupByID<RE::TESShout>(shoutID);
                            }
                        }
                        InventoryManager::GetSingleton().SetSavedPlayerEquippedSpells(leftSpell, rightSpell, shout);
                    }

                    SKSE::log::info("Loaded {} transferred spells, {} saved player spells, and {} donor quest items from co-save.",
                        transCount, savedCount, questCount);
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
                    RE::FormID donorID = 0;

                    a_serde->ReadRecordData(&enabled, sizeof(enabled));
                    a_serde->ReadRecordData(&timeout, sizeof(timeout));
                    a_serde->ReadRecordData(&donorID, sizeof(donorID));
                    if (donorID != 0 && a_serde->ResolveFormID(donorID, donorID)) {
                        auto* donor = RE::TESForm::LookupByID<RE::Actor>(donorID);
                        IdleAIManager::GetSingleton().SetActiveDonorActor(donor);
                    }

                    IdleAIManager::GetSingleton().SetIdleAIEnabled(enabled);
                    IdleAIManager::GetSingleton().SetIdleTimeout(timeout);
                    SKSE::log::info("Loaded Idle AI state from co-save.");
                    break;
                }

                case kImpersonationDataRecord: {
                    ImpersonationManager::GetSingleton().Load(a_serde, length);
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
        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && player->GetActorBase()) {
                FaceManager::GetSingleton().RevertAppearance(player, false);
                SKSE::log::info("Reverted player appearance to original before save load.");
            }
        }
        AliasManager::GetSingleton().Clear();
        FaceManager::GetSingleton().ClearSavedAppearance();
        FaceManager::GetSingleton().ClearDonorAppearance();
        InventoryManager::GetSingleton().Clear();
        StatManager::GetSingleton().Clear();
        IdleAIManager::GetSingleton().Clear();
        ImpersonationManager::GetSingleton().OnRevert();
        SKSE::log::info("Reverted all AlternateActors saved manager states.");
    }

}
