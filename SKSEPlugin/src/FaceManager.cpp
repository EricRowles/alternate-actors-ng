#include "PCH.h"
#include "FaceManager.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "RE/B/BSFaceGenNiNode.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/B/BSVisit.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderMaterialHairTint.h"

namespace AlternateActors {

    bool FaceManager::CaptureSnapshot(RE::Actor* a_actor, FaceSnapshot& a_outSnapshot) {
        if (!a_actor) return false;

        auto* base = a_actor->GetActorBase();
        if (!base) return false;

        auto* npc = skyrim_cast<RE::TESNPC*>(base);
        if (!npc) return false;

        a_outSnapshot.race = npc->GetRace();
        a_outSnapshot.faceNPC = (npc->faceNPC && npc->faceNPC != npc) ? npc->faceNPC : npc;
        a_outSnapshot.fullName = npc->GetFullName();
        a_outSnapshot.height = npc->height;
        a_outSnapshot.weight = npc->weight;
        a_outSnapshot.sex = npc->GetSex();
        a_outSnapshot.bodyTintColor = npc->bodyTintColor;

        // Voice Type resolution
        a_outSnapshot.voiceType = ImpersonationManager::GetNPCVoiceType(npc);

        if (npc->headRelatedData) {
            a_outSnapshot.hairColor = npc->headRelatedData->hairColor;
            a_outSnapshot.faceDetails = npc->headRelatedData->faceDetails;
        } else {
            a_outSnapshot.hairColor = nullptr;
            a_outSnapshot.faceDetails = nullptr;
        }

        // Capture 3D rendered hair tint directly from 3D geometry
        a_outSnapshot.hasHairTintColor = false;
        auto* hairObj = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair);
        if (hairObj) {
            RE::BSVisit::TraverseScenegraphGeometries(hairObj, [&](RE::BSGeometry* geom) {
                if (geom) {
                    auto* prop = geom->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kProperty].get();
                    if (auto* shaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(prop)) {
                        if (auto* hairMat = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderProp->material)) {
                            a_outSnapshot.hairTintColor = hairMat->tintColor;
                            a_outSnapshot.hasHairTintColor = true;
                            return RE::BSVisit::BSVisitControl::kStop;
                        }
                    }
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        // Face morphs & parts
        if (npc->faceData) {
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
                a_outSnapshot.morphs[i] = npc->faceData->morphs[i];
            }
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
                a_outSnapshot.parts[i] = npc->faceData->parts[i];
            }
        }

        // Comprehensive Head parts capture (all types 0..6)
        a_outSnapshot.headParts.clear();
        for (int t = 0; t <= static_cast<int>(RE::BGSHeadPart::HeadPartType::kEyebrows); ++t) {
            auto type = static_cast<RE::BGSHeadPart::HeadPartType>(t);
            auto* part = npc->GetCurrentHeadPartByType(type);
            if (!part) part = npc->GetHeadPartByType(type);
            if (part) {
                a_outSnapshot.headParts.push_back(part);
            }
        }
        if (npc->headParts && npc->numHeadParts > 0) {
            for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                if (npc->headParts[i]) {
                    bool found = false;
                    for (auto* existing : a_outSnapshot.headParts) {
                        if (existing == npc->headParts[i]) { found = true; break; }
                    }
                    if (!found) a_outSnapshot.headParts.push_back(npc->headParts[i]);
                }
            }
        }

        // Tint layers
        a_outSnapshot.tintLayers.clear();
        if (npc->tintLayers) {
            for (auto* layer : *npc->tintLayers) {
                if (layer) {
                    a_outSnapshot.tintLayers.push_back(*layer);
                }
            }
        }

        // AI & Combat Profile Capture
        a_outSnapshot.npcClass = npc->npcClass;
        a_outSnapshot.combatStyle = npc->combatStyle;
        a_outSnapshot.defaultPackList = npc->defaultPackList;
        a_outSnapshot.aiData = npc->aiData;

        a_outSnapshot.packages.clear();
        for (auto* pkg : npc->aiPackages.packages) {
            if (pkg) {
                a_outSnapshot.packages.push_back(pkg);
            }
        }

        return true;
    }

    bool FaceManager::ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot) {
        if (!a_targetActor) return false;

        auto* base = a_targetActor->GetActorBase();
        if (!base) return false;

        auto* targetNPC = skyrim_cast<RE::TESNPC*>(base);
        if (!targetNPC) return false;

        // 1. Race & Face NPC
        if (a_snapshot.race) {
            targetNPC->race = a_snapshot.race;
            targetNPC->originalRace = a_snapshot.race;
        }
        targetNPC->faceNPC = (a_snapshot.faceNPC && a_snapshot.faceNPC != targetNPC) ? a_snapshot.faceNPC : nullptr;

        if (!a_snapshot.fullName.empty()) {
            targetNPC->fullName = a_snapshot.fullName;
        }

        // 2. Sex & Voice Type
        if (a_snapshot.sex == RE::SEX::kFemale) {
            targetNPC->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
        } else {
            targetNPC->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kFemale);
        }

        targetNPC->voiceType = a_snapshot.voiceType;

        // 3. Body scales & tint
        targetNPC->height = a_snapshot.height;
        targetNPC->weight = a_snapshot.weight;
        targetNPC->bodyTintColor = a_snapshot.bodyTintColor;

        // 4. Hair & Face details
        if (!targetNPC->headRelatedData) {
            auto* mm = RE::MemoryManager::GetSingleton();
            targetNPC->headRelatedData = static_cast<RE::TESNPC::HeadRelatedData*>(
                mm ? mm->Allocate(sizeof(RE::TESNPC::HeadRelatedData), 0, false) : new RE::TESNPC::HeadRelatedData()
            );
            if (targetNPC->headRelatedData) {
                targetNPC->headRelatedData->hairColor = nullptr;
                targetNPC->headRelatedData->faceDetails = nullptr;
            }
        }
        if (targetNPC->headRelatedData) {
            targetNPC->headRelatedData->hairColor = a_snapshot.hairColor;
            targetNPC->headRelatedData->faceDetails = a_snapshot.faceDetails;
        }

        // 5. Face morphs & parts
        if (!targetNPC->faceData) {
            auto* mm = RE::MemoryManager::GetSingleton();
            targetNPC->faceData = static_cast<RE::TESNPC::FaceData*>(
                mm ? mm->Allocate(sizeof(RE::TESNPC::FaceData), 0, false) : new RE::TESNPC::FaceData()
            );
            if (targetNPC->faceData) {
                std::memset(targetNPC->faceData, 0, sizeof(RE::TESNPC::FaceData));
            }
        }
        if (targetNPC->faceData) {
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
                targetNPC->faceData->morphs[i] = a_snapshot.morphs[i];
            }
            for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
                targetNPC->faceData->parts[i] = a_snapshot.parts[i];
            }
        }

        // 6. Complete Headparts Replacement
        if (!a_snapshot.headParts.empty()) {
            auto count = static_cast<std::int8_t>(a_snapshot.headParts.size());
            auto* mm = RE::MemoryManager::GetSingleton();
            if (mm && count > 0) {
                auto* newParts = static_cast<RE::BGSHeadPart**>(mm->Allocate(sizeof(RE::BGSHeadPart*) * count, 0, false));
                if (newParts) {
                    for (std::int8_t i = 0; i < count; ++i) {
                        newParts[i] = a_snapshot.headParts[i];
                    }
                    targetNPC->headParts = newParts;
                    targetNPC->numHeadParts = count;
                }
            }
        }

        // 6b. Tint Layers Replication
        if (!a_snapshot.tintLayers.empty()) {
            if (!targetNPC->tintLayers) {
                auto* mm = RE::MemoryManager::GetSingleton();
                targetNPC->tintLayers = static_cast<RE::BSTArray<RE::TESNPC::Layer*>*>(
                    mm ? mm->Allocate(sizeof(RE::BSTArray<RE::TESNPC::Layer*>), 0, false) : new RE::BSTArray<RE::TESNPC::Layer*>()
                );
                if (targetNPC->tintLayers) {
                    new (targetNPC->tintLayers) RE::BSTArray<RE::TESNPC::Layer*>();
                }
            }
            if (targetNPC->tintLayers) {
                for (auto* oldLayer : *targetNPC->tintLayers) {
                    if (oldLayer) {
                        auto* mm = RE::MemoryManager::GetSingleton();
                        if (mm) mm->Deallocate(oldLayer, false);
                        else delete oldLayer;
                    }
                }
                targetNPC->tintLayers->clear();

                auto* mm = RE::MemoryManager::GetSingleton();
                for (const auto& layer : a_snapshot.tintLayers) {
                    auto* newLayer = static_cast<RE::TESNPC::Layer*>(
                        mm ? mm->Allocate(sizeof(RE::TESNPC::Layer), 0, false) : new RE::TESNPC::Layer()
                    );
                    if (newLayer) {
                        *newLayer = layer;
                        targetNPC->tintLayers->push_back(newLayer);
                    }
                }
            }
        }

        // 7. AI Behavior & Combat Profile Transfer
        targetNPC->npcClass = a_snapshot.npcClass;
        targetNPC->combatStyle = a_snapshot.combatStyle;
        targetNPC->defaultPackList = a_snapshot.defaultPackList;
        targetNPC->aiData = a_snapshot.aiData;

        targetNPC->aiPackages.packages.clear();
        for (auto* pkg : a_snapshot.packages) {
            if (pkg) {
                targetNPC->aiPackages.packages.push_front(pkg);
            }
        }
        auto* defaultSandbox = RE::TESForm::LookupByID<RE::TESPackage>(0x0001B619);
        if (defaultSandbox) {
            if (targetNPC->aiPackages.packages.empty()) {
                targetNPC->aiPackages.packages.push_front(defaultSandbox);
            } else {
                auto last = targetNPC->aiPackages.packages.begin();
                for (auto it = targetNPC->aiPackages.packages.begin(); it != targetNPC->aiPackages.packages.end(); ++it) {
                    last = it;
                }
                targetNPC->aiPackages.packages.insert_after(last, defaultSandbox);
            }
        }

        // 8. Synchronous 3D mesh and skin update
        a_targetActor->UpdateHairColor();
        a_targetActor->UpdateSkinColor();
        a_targetActor->DoReset3D(true);
        a_targetActor->Update3DModel();

        // Apply hair tint directly to hair geometry
        if (a_snapshot.hasHairTintColor) {
            auto* targetHair = a_targetActor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair);
            if (targetHair) {
                RE::BSVisit::TraverseScenegraphGeometries(targetHair, [&](RE::BSGeometry* geom) {
                    if (geom) {
                        auto* prop = geom->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kProperty].get();
                        if (auto* shaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(prop)) {
                            if (auto* hairMat = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderProp->material)) {
                                hairMat->tintColor = a_snapshot.hairTintColor;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }
        }


        SKSE::log::info("Applied appearance and AI snapshot (Race: {:08X}, Sex: {}, Voice: {:08X}, Tints: {}, Parts: {}) to actor {:08X}",
            a_snapshot.race ? static_cast<std::uint32_t>(a_snapshot.race->GetFormID()) : 0,
            a_snapshot.sex == RE::SEX::kFemale ? "Female" : "Male",
            targetNPC->voiceType ? static_cast<std::uint32_t>(targetNPC->voiceType->GetFormID()) : 0,
            a_snapshot.tintLayers.size(),
            a_snapshot.headParts.size(),
            static_cast<std::uint32_t>(a_targetActor->GetFormID())
        );

        return true;
    }

    bool FaceManager::CopyAppearance(RE::Actor* a_donor, RE::Actor* a_target) {
        if (!a_donor || !a_target) {
            SKSE::log::warn("CopyAppearance called with null actor pointers.");
            return false;
        }

        FaceSnapshot donorSnapshot;
        if (!CaptureSnapshot(a_donor, donorSnapshot)) {
            SKSE::log::error("Failed to capture appearance of donor actor {:08X}.",
                static_cast<std::uint32_t>(a_donor->GetFormID()));
            return false;
        }

        return ApplySnapshot(a_target, donorSnapshot);
    }

    bool FaceManager::CloneAppearance(RE::Actor* a_donor, RE::Actor* a_target) {
        IdleAIManager::GetSingleton().SetActiveDonorActor(a_donor);
        if (!a_donor || !a_target) {
            SKSE::log::warn("CloneAppearance called with null actor pointers.");
            return false;
        }

        if (!_hasSavedAppearance) {
            if (CaptureSnapshot(a_target, _savedOriginalAppearance)) {
                _savedOriginalAppearance.faceNPC = nullptr; // Ensure player original never self-references
                _hasSavedAppearance = true;
                SKSE::log::info("Saved original appearance of target actor {:08X} before cloning.",
                    static_cast<std::uint32_t>(a_target->GetFormID()));
            }
        }

        return CopyAppearance(a_donor, a_target);
    }

    bool FaceManager::RevertAppearance(RE::Actor* a_target) {
        IdleAIManager::GetSingleton().SetActiveDonorActor(nullptr);
        if (!a_target) return false;

        if (!_hasSavedAppearance) {
            SKSE::log::warn("RevertAppearance called, but no original appearance was saved.");
            return false;
        }

        bool result = ApplySnapshot(a_target, _savedOriginalAppearance);
        if (result) {
            _hasSavedAppearance = false;
            SKSE::log::info("Successfully reverted appearance of actor {:08X} to original.",
                static_cast<std::uint32_t>(a_target->GetFormID()));
        }
        return result;
    }

}
