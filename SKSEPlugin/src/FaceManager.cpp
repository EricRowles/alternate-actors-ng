#include "PCH.h"
#include "FaceManager.h"
#include "AliasManager.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "RE/B/BSFaceGenNiNode.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/B/BSVisit.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderMaterialHairTint.h"
#include "RE/B/BSLightingShaderMaterialFacegenTint.h"
#include "RE/C/CombatController.h"
#include "RE/E/ExtraScale.h"
#include "RE/E/ExtraTextDisplayData.h"
#include "RE/E/ExtraCombatStyle.h"

namespace AlternateActors {

    bool FaceManager::CaptureSnapshot(RE::Actor* a_actor, FaceSnapshot& a_outSnapshot) {
        if (!a_actor) return false;

        auto* base = a_actor->GetActorBase();
        if (!base) return false;

        auto* npc = skyrim_cast<RE::TESNPC*>(base);
        if (!npc) return false;

        a_outSnapshot.race = npc->GetRace();
        if (npc->GetFormID() == 0x07) {
            a_outSnapshot.faceNPC = nullptr;
        } else {
            a_outSnapshot.faceNPC = (npc->faceNPC && npc->faceNPC != npc) ? npc->faceNPC : npc;
        }
        const char* dispName = a_actor->GetDisplayFullName();
        if (dispName && dispName[0] != '\0') {
            a_outSnapshot.fullName = dispName;
        } else {
            a_outSnapshot.fullName = npc->GetFullName();
        }
        a_outSnapshot.height = npc->height;

        auto* extraScale = a_actor->extraList.GetByType<RE::ExtraScale>();
        a_outSnapshot.scale = extraScale ? extraScale->scale : 1.0f;

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
                    if (auto* shaderProp = geom->lightingShaderProp_cast()) {
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

        // Capture 3D rendered skin tint directly from face 3D geometry
        a_outSnapshot.hasSkinTintColor = false;
        auto* faceObj = a_actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
        if (faceObj) {
            RE::BSVisit::TraverseScenegraphGeometries(faceObj, [&](RE::BSGeometry* geom) {
                if (geom) {
                    if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                        if (auto* faceMat = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderProp->material)) {
                            a_outSnapshot.skinTintColor = faceMat->tintColor;
                            a_outSnapshot.hasSkinTintColor = true;
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
        auto* extraStyle = a_actor->extraList.GetByType<RE::ExtraCombatStyle>();
        if (extraStyle && extraStyle->combatStyle) {
            a_outSnapshot.combatStyle = extraStyle->combatStyle;
        } else {
            a_outSnapshot.combatStyle = npc->combatStyle;
        }
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

    bool FaceManager::ApplySnapshot(RE::Actor* a_targetActor, const FaceSnapshot& a_snapshot, bool a_update3D) {
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

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        bool isPuppet = (puppet && a_targetActor == puppet);
        if (!isPuppet && a_snapshot.faceNPC && a_snapshot.faceNPC != targetNPC && a_snapshot.faceNPC->GetFormID() != 0x07) {
            targetNPC->faceNPC = a_snapshot.faceNPC;
        } else {
            targetNPC->faceNPC = nullptr;
        }

        if (!a_snapshot.fullName.empty()) {
            targetNPC->fullName = a_snapshot.fullName;
            auto* textData = a_targetActor->extraList.GetByType<RE::ExtraTextDisplayData>();
            if (!textData) {
                textData = new RE::ExtraTextDisplayData(a_snapshot.fullName.c_str());
                a_targetActor->extraList.Add(textData);
            } else {
                textData->SetName(a_snapshot.fullName.c_str());
            }
        }

        // 2. Sex & Voice Type
        if (a_snapshot.sex == RE::SEX::kFemale) {
            targetNPC->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
        } else {
            targetNPC->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kFemale);
        }

        if (isPuppet || (base && ImpersonationManager::GetSingleton().IsPuppetBase(base))) {
            targetNPC->voiceType = nullptr;
        } else {
            targetNPC->voiceType = a_snapshot.voiceType;
        }

        // 3. Body scales & tint
        targetNPC->height = a_snapshot.height;
        if (a_update3D) {
            a_targetActor->SetScale(a_snapshot.scale);
        }
        targetNPC->weight = a_snapshot.weight;
        targetNPC->bodyTintColor = a_snapshot.bodyTintColor;

        // 4. Hair & Face details
        if (!targetNPC->headRelatedData) {
            targetNPC->headRelatedData = RE::calloc<RE::TESNPC::HeadRelatedData>(1);
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
            targetNPC->faceData = RE::calloc<RE::TESNPC::FaceData>(1);
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

        // 6. Complete Headparts Replacement (free previous buffer first to avoid leaking on FormHeap)
        if (targetNPC->headParts) {
            RE::free(targetNPC->headParts);
            targetNPC->headParts = nullptr;
            targetNPC->numHeadParts = 0;
        }
        if (!a_snapshot.headParts.empty()) {
            auto count = static_cast<std::int8_t>(a_snapshot.headParts.size());
            if (count > 0) {
                auto* newParts = RE::calloc<RE::BGSHeadPart*>(static_cast<std::size_t>(count));
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
                auto* raw = RE::calloc<RE::BSTArray<RE::TESNPC::Layer*>>(1);
                if (raw) {
                    targetNPC->tintLayers = new (raw) RE::BSTArray<RE::TESNPC::Layer*>();
                }
            }
            if (targetNPC->tintLayers) {
                for (auto* oldLayer : *targetNPC->tintLayers) {
                    if (oldLayer) {
                        RE::free(oldLayer);
                    }
                }
                targetNPC->tintLayers->clear();

                for (const auto& layer : a_snapshot.tintLayers) {
                    auto* newLayer = RE::calloc<RE::TESNPC::Layer>(1);
                    if (newLayer) {
                        *newLayer = layer;
                        targetNPC->tintLayers->push_back(newLayer);
                    }
                }
            }
        } else if (targetNPC->tintLayers) {
            for (auto* oldLayer : *targetNPC->tintLayers) {
                if (oldLayer) {
                    RE::free(oldLayer);
                }
            }
            targetNPC->tintLayers->clear();
        }

        // 7. AI Behavior & Combat Profile Transfer
        targetNPC->npcClass = a_snapshot.npcClass;
        targetNPC->combatStyle = a_snapshot.combatStyle;
        if (a_snapshot.combatStyle) {
            auto* extraStyle = a_targetActor->extraList.GetByType<RE::ExtraCombatStyle>();
            if (extraStyle) {
                extraStyle->combatStyle = a_snapshot.combatStyle;
            }
        }
        auto& runtime = a_targetActor->GetActorRuntimeData();
        if (runtime.combatController) {
            runtime.combatController->combatStyle = a_snapshot.combatStyle;
        }
        targetNPC->defaultPackList = a_snapshot.defaultPackList;
        targetNPC->aiData = a_snapshot.aiData;

        // Preserve donor's exact package priority order (head-to-tail)
        targetNPC->aiPackages.packages.clear();

        // When transferring AI packages to the player, append DefaultSandboxCurrentLocation1024 (0x000BFB6B)
        // at the tail as a universal fallback so autonomous idle AI can wander anywhere in Skyrim
        if (a_targetActor && a_targetActor->IsPlayerRef() && a_snapshot.faceNPC != nullptr) {
            auto* defaultSandbox = RE::TESForm::LookupByID<RE::TESPackage>(0x000BFB6B);
            if (defaultSandbox) {
                targetNPC->aiPackages.packages.push_front(defaultSandbox);
            }
        }

        // Base NPC packages
        for (auto it = a_snapshot.packages.rbegin(); it != a_snapshot.packages.rend(); ++it) {
            if (*it) {
                targetNPC->aiPackages.packages.push_front(*it);
            }
        }

        // When applying to player, alias packages from the donor's quests must evaluate BEFORE base NPC packages
        if (a_targetActor && a_targetActor->IsPlayerRef()) {
            auto* donorActor = IdleAIManager::GetSingleton().GetActiveDonorActor();
            if (!donorActor) {
                donorActor = ImpersonationManager::GetSingleton().GetDonor();
            }
            if (donorActor) {
                auto aliasPkgs = AliasManager::GetSingleton().GetAliasPackages(donorActor);
                for (auto it = aliasPkgs.rbegin(); it != aliasPkgs.rend(); ++it) {
                    if (*it) {
                        targetNPC->aiPackages.packages.push_front(*it);
                    }
                }
            }
        }

        // Resolve skin tint color for body tint
        RE::NiColor skinTint;
        bool hasSkinTint = false;
        if (a_snapshot.hasSkinTintColor) {
            skinTint = a_snapshot.skinTintColor;
            hasSkinTint = true;
        } else if (a_snapshot.bodyTintColor.red != 0 || a_snapshot.bodyTintColor.green != 0 || a_snapshot.bodyTintColor.blue != 0) {
            skinTint.red = static_cast<float>(a_snapshot.bodyTintColor.red) / 255.0f;
            skinTint.green = static_cast<float>(a_snapshot.bodyTintColor.green) / 255.0f;
            skinTint.blue = static_cast<float>(a_snapshot.bodyTintColor.blue) / 255.0f;
            hasSkinTint = true;
        } else {
            // Find skin tone layer specifically from race data
            auto sexIdx = (a_snapshot.sex == RE::SEX::kFemale) ? 1 : 0;
            if (a_snapshot.race && a_snapshot.race->faceRelatedData[sexIdx] && a_snapshot.race->faceRelatedData[sexIdx]->tintMasks) {
                const auto* tintMasks = a_snapshot.race->faceRelatedData[sexIdx]->tintMasks;
                for (const auto* asset : *tintMasks) {
                    if (asset && asset->texture.skinTone == RE::TESRace::FaceRelatedData::TintAsset::TintLayer::SkinTone::kSkinTone) {
                        auto skinIndex = asset->texture.index;
                        for (const auto& layer : a_snapshot.tintLayers) {
                            if (layer.tintIndex == skinIndex) {
                                skinTint.red = static_cast<float>(layer.tintColor.red) / 255.0f;
                                skinTint.green = static_cast<float>(layer.tintColor.green) / 255.0f;
                                skinTint.blue = static_cast<float>(layer.tintColor.blue) / 255.0f;
                                hasSkinTint = true;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        if (hasSkinTint) {
            targetNPC->bodyTintColor.red = static_cast<std::uint8_t>(std::clamp(skinTint.red * 255.0f, 0.0f, 255.0f));
            targetNPC->bodyTintColor.green = static_cast<std::uint8_t>(std::clamp(skinTint.green * 255.0f, 0.0f, 255.0f));
            targetNPC->bodyTintColor.blue = static_cast<std::uint8_t>(std::clamp(skinTint.blue * 255.0f, 0.0f, 255.0f));
        }

        if (a_update3D) {
            // 8. Synchronous 3D mesh and skin update
            a_targetActor->UpdateHairColor();
            a_targetActor->UpdateSkinColor();
            a_targetActor->DoReset3D(true);
            a_targetActor->Update3DModel();
            a_targetActor->UpdateHairColor();
            a_targetActor->UpdateSkinColor();

            // Apply hair tint directly to hair geometry
            if (a_snapshot.hasHairTintColor) {
                auto* targetHair = a_targetActor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair);
                if (targetHair) {
                    RE::BSVisit::TraverseScenegraphGeometries(targetHair, [&](RE::BSGeometry* geom) {
                        if (geom) {
                            if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                                if (auto* hairMat = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderProp->material)) {
                                    hairMat->tintColor = a_snapshot.hairTintColor;
                                }
                            }
                        }
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });
                }
            }

            // Apply skin tint and soft matte lighting strictly to face headpart FacegenTint material
            auto* targetFace = a_targetActor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
            if (targetFace) {
                RE::BSVisit::TraverseScenegraphGeometries(targetFace, [&](RE::BSGeometry* geom) {
                    if (geom) {
                        if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                            if (auto* faceMat = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderProp->material)) {
                                if (hasSkinTint) {
                                    faceMat->tintColor = skinTint;
                                }
                                faceMat->specularColorScale = 0.20f;
                                faceMat->specularPower = 10.0f;
                                faceMat->subSurfaceLightRolloff = 0.6f;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }

            if (isPuppet) {
                _puppetNeedsPost3DUpdate.store(true, std::memory_order_relaxed);
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
        std::lock_guard<std::recursive_mutex> lock(_mutex);
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
        std::lock_guard<std::recursive_mutex> lock(_mutex);
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

        if (CaptureSnapshot(a_donor, _donorAppearance)) {
            _hasDonorAppearance = true;
            return ApplySnapshot(a_target, _donorAppearance);
        }

        return CopyAppearance(a_donor, a_target);
    }

    bool FaceManager::RevertAppearance(RE::Actor* a_target, bool a_update3D) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        IdleAIManager::GetSingleton().SetActiveDonorActor(nullptr);
        if (!a_target) return false;

        if (!_hasSavedAppearance) {
            SKSE::log::warn("RevertAppearance called, but no original appearance was saved.");
            return false;
        }

        bool result = ApplySnapshot(a_target, _savedOriginalAppearance, a_update3D);
        if (result) {
            _hasSavedAppearance = false;
            _hasDonorAppearance = false;
            SKSE::log::info("Successfully reverted appearance of actor {:08X} to original (update3D={}).",
                static_cast<std::uint32_t>(a_target->GetFormID()),
                a_update3D);
        }
        return result;
    }

    bool FaceManager::RestoreDonorAppearance(RE::Actor* a_donor, RE::Actor* a_target) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_target) return false;

        if (_hasDonorAppearance) {
            SKSE::log::info("Restoring donor appearance from saved donor snapshot onto actor {:08X}",
                static_cast<std::uint32_t>(a_target->GetFormID()));
            return ApplySnapshot(a_target, _donorAppearance);
        }

        if (a_donor) {
            SKSE::log::info("Restoring donor appearance from donor actor {:08X} onto actor {:08X}",
                static_cast<std::uint32_t>(a_donor->GetFormID()),
                static_cast<std::uint32_t>(a_target->GetFormID()));
            if (CaptureSnapshot(a_donor, _donorAppearance)) {
                _hasDonorAppearance = true;
                return ApplySnapshot(a_target, _donorAppearance);
            }
            return CopyAppearance(a_donor, a_target);
        }

        return false;
    }

    bool FaceManager::RestoreOriginalPuppetAppearance(RE::Actor* a_puppet) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_puppet) return false;

        if (auto* base = skyrim_cast<RE::TESNPC*>(a_puppet->GetActorBase())) {
            base->faceNPC = nullptr;
        }

        if (!_hasSavedAppearance) {
            SKSE::log::warn("RestoreOriginalPuppetAppearance called, but no original appearance was saved.");
            return false;
        }

        SKSE::log::info("Restoring original Dragonborn appearance onto puppet actor {:08X}",
            static_cast<std::uint32_t>(a_puppet->GetFormID()));
        return ApplySnapshot(a_puppet, _savedOriginalAppearance);
    }

    void FaceManager::ApplyPuppetPost3DLighting(RE::Actor* a_puppet) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_puppet) return;

        auto* puppet3D = a_puppet->Get3D();
        if (!puppet3D) return;

        auto* targetFace = a_puppet->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
        if (!targetFace) return;

        if (_savedOriginalAppearance.hasHairTintColor) {
            auto* targetHair = a_puppet->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kHair);
            if (targetHair) {
                RE::BSVisit::TraverseScenegraphGeometries(targetHair, [&](RE::BSGeometry* geom) {
                    if (geom) {
                        if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                            if (auto* hairMat = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderProp->material)) {
                                hairMat->tintColor = _savedOriginalAppearance.hairTintColor;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            }
        }

        RE::NiColor skinTint;
        bool hasSkinTint = false;
        if (_savedOriginalAppearance.hasSkinTintColor) {
            skinTint = _savedOriginalAppearance.skinTintColor;
            hasSkinTint = true;
        } else if (_savedOriginalAppearance.bodyTintColor.red != 0 || _savedOriginalAppearance.bodyTintColor.green != 0 || _savedOriginalAppearance.bodyTintColor.blue != 0) {
            skinTint.red = static_cast<float>(_savedOriginalAppearance.bodyTintColor.red) / 255.0f;
            skinTint.green = static_cast<float>(_savedOriginalAppearance.bodyTintColor.green) / 255.0f;
            skinTint.blue = static_cast<float>(_savedOriginalAppearance.bodyTintColor.blue) / 255.0f;
            hasSkinTint = true;
        }

        RE::BSVisit::TraverseScenegraphGeometries(targetFace, [&](RE::BSGeometry* geom) {
            if (geom) {
                if (auto* shaderProp = geom->lightingShaderProp_cast()) {
                    if (auto* faceMat = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderProp->material)) {
                        if (hasSkinTint) {
                            faceMat->tintColor = skinTint;
                        }
                        faceMat->specularColorScale = 0.20f;
                        faceMat->specularPower = 10.0f;
                        faceMat->subSurfaceLightRolloff = 0.6f;
                    }
                }
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });

        _puppetNeedsPost3DUpdate.store(false, std::memory_order_relaxed);
        SKSE::log::info("Applied post-3D skin, hair, and soft matte face lighting to puppet {:08X}",
            static_cast<std::uint32_t>(a_puppet->GetFormID()));
    }

}
