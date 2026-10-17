#include "PCH.h"
#include "ImpersonationManager.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "Serialization.h"
#include "IdleAIManager.h"

#include <unordered_set>
#include <RE/E/ExtraTextDisplayData.h>
#include <SKSE/ContextHook.h>
#include <RE/C/CommandTable.h>
#include <RE/T/TESCondition.h>
#include <RE/T/TESPackage.h>
#include <RE/P/PackageLocation.h>
#include <RE/T/TESCustomPackageData.h>
#include <RE/B/BGSPackageDataLocation.h>
#include <RE/B/BGSPackageDataPointerTemplate.h>
#include <RE/P/PackUnpack.h>
#include <RE/V/VirtualMachine.h>
#include <RE/A/AttachedScript.h>
#include <RE/O/Object.h>
#include <RE/B/BSAtomic.h>
#include <RE/A/AIProcess.h>
#include <RE/H/HighProcessData.h>
#include <RE/S/SubtitleManager.h>

namespace AlternateActors {

    enum class IdentityCondition : std::uint8_t {
        kGetInFaction = 0,
        kGetIsID,
        kGetFactionRank,
        kGetIsReference,
        kGetIsAliasRef,
        kGetIsVoiceType,
        kIsInList,
        kTotal
    };

    static std::array<RE::SCRIPT_FUNCTION::Condition_t*, static_cast<std::size_t>(IdentityCondition::kTotal)> s_originalConditions{ nullptr };

    // --- Category A: Identity Thunk ---
    template <IdentityCondition Cond>
    static bool IdentityThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        constexpr auto idx = static_cast<std::size_t>(Cond);
        auto* orig = s_originalConditions[idx];
        if (!orig) return false;
        if (!mgr.IsConditionsEnabled()) {
            return orig(a_thisObj, a_p1, a_p2, a_result);
        }

        auto* player = mgr.GetPlayer();
        if (!player) {
            player = RE::PlayerCharacter::GetSingleton();
        }
        auto* donor = mgr.GetDonor();
        auto* puppet = mgr.GetPuppet();
        bool isSelfDialogue = IdleAIManager::GetSingleton().IsInSelfDialogue();

        // 1. If evaluating on player OR on puppet acting as proxy speaker during self-dialogue, prioritize donor identity!
        if (a_thisObj && (a_thisObj == player || (puppet && a_thisObj == puppet && isSelfDialogue))) {
            if constexpr (Cond == IdentityCondition::kGetIsID) {
                auto* targetForm = static_cast<RE::TESForm*>(a_p1);
                if (donor && targetForm) {
                    if (targetForm == donor || targetForm == donor->GetBaseObject()) {
                        a_result = 1.0;
                        return true;
                    }
                    if (auto* actorBase = donor->GetActorBase()) {
                        if (targetForm == actorBase || targetForm == actorBase->GetRootFaceNPC()) {
                            a_result = 1.0;
                            return true;
                        }
                    }
                    if (auto* extraLeveled = donor->extraList.GetByType<RE::ExtraLeveledCreature>()) {
                        if (targetForm == extraLeveled->originalBase || targetForm == extraLeveled->templateBase) {
                            a_result = 1.0;
                            return true;
                        }
                    }
                    double donorRes = 0.0;
                    if (orig(donor, a_p1, a_p2, donorRes) && donorRes != 0.0) {
                        a_result = 1.0;
                        return true;
                    }
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetIsVoiceType) {
                auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_p1);
                if ((isSelfDialogue || mgr.IsNPCVoiceEnabled()) && mgr.GetDonorVoiceType()) {
                    a_result = (targetVoice && targetVoice == mgr.GetDonorVoiceType()) ? 1.0 : 0.0;
                    return true;
                }
                auto* origVoice = mgr.GetOrigPlayerVoiceType();
                a_result = (origVoice && targetVoice && targetVoice == origVoice) ? 1.0 : 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetInFaction) {
                if (donor) {
                    double donorRes = 0.0;
                    if (orig(donor, a_p1, a_p2, donorRes)) {
                        a_result = (donorRes != 0.0) ? 1.0 : 0.0;
                        return true;
                    }
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetFactionRank) {
                if (donor) {
                    double donorRes = -1.0;
                    if (orig(donor, a_p1, a_p2, donorRes)) {
                        a_result = donorRes;
                        return true;
                    }
                }
                a_result = -1.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetIsReference) {
                auto* targetRef = static_cast<RE::TESObjectREFR*>(a_p1);
                if (donor && targetRef && targetRef == donor) {
                    a_result = 1.0;
                    return true;
                }
                return orig(a_thisObj, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetIsAliasRef) {
                double res = 0.0;
                if (player && orig(player, a_p1, a_p2, res) && res != 0.0) {
                    a_result = res;
                    return true;
                }
                if (donor && orig(donor, a_p1, a_p2, res) && res != 0.0) {
                    a_result = res;
                    return true;
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kIsInList) {
                double res = 0.0;
                if (donor && orig(donor, a_p1, a_p2, res) && res != 0.0) {
                    a_result = res;
                    return true;
                }
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            }
        }

        // 2. If evaluating on puppet outside self-dialogue, puppet represents the Dragonborn in the world!
        if (a_thisObj && puppet && a_thisObj == puppet && !isSelfDialogue) {
            if constexpr (Cond == IdentityCondition::kGetIsID) {
                auto* targetForm = static_cast<RE::TESForm*>(a_p1);
                if (targetForm && (targetForm == player || (player && targetForm == player->GetActorBase()) ||
                    targetForm->GetFormID() == 0x07 || targetForm->GetFormID() == 0x14)) {
                    a_result = 1.0;
                    return true;
                }
                a_result = 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetIsVoiceType) {
                auto* targetVoice = static_cast<RE::BGSVoiceType*>(a_p1);
                auto* origVoice = mgr.GetOrigPlayerVoiceType();
                a_result = (origVoice && targetVoice && targetVoice == origVoice) ? 1.0 : 0.0;
                return true;
            } else if constexpr (Cond == IdentityCondition::kGetInFaction) {
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetFactionRank) {
                return orig(player ? player : a_thisObj, a_p1, a_p2, a_result);
            } else if constexpr (Cond == IdentityCondition::kGetIsReference) {
                auto* targetRef = static_cast<RE::TESObjectREFR*>(a_p1);
                if (player && targetRef && targetRef == player) {
                    a_result = 1.0;
                    return true;
                }
                return orig(puppet, a_p1, a_p2, a_result);
            } else {
                return orig(puppet, a_p1, a_p2, a_result);
            }
        }

        // 3. Normal evaluation for all other actors
        return orig(a_thisObj, a_p1, a_p2, a_result);
    }

    // --- Category B: Spatial Condition Proxying ---
    enum class SpatialCondition : std::uint8_t {
        kGetInCell = 0,
        kGetInCurrentLoc,
        kGetDistance,
        kIsInInterior,
        kGetInSameCell,
        kGetInWorldspace,
        kGetPos,
        kGetHeadingAngle,
        kGetAngle,
        kTotal
    };

    static std::array<RE::SCRIPT_FUNCTION::Condition_t*, static_cast<std::size_t>(SpatialCondition::kTotal)> s_originalSpatialConditions{ nullptr };

    template <SpatialCondition Cond>
    static bool SpatialConditionThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        constexpr auto idx = static_cast<std::size_t>(Cond);
        auto* orig = s_originalSpatialConditions[idx];
        if (!orig) return false;
        if (!mgr.IsConditionsEnabled()) {
            return orig(a_thisObj, a_p1, a_p2, a_result);
        }

        auto* player = mgr.GetPlayer();
        if (!player) {
            player = RE::PlayerCharacter::GetSingleton();
        }
        auto* donor = mgr.GetDonor();

        auto* evalThis = a_thisObj;
        auto* evalP1 = a_p1;

        // If condition is running on donor, proxy spatial evaluation to player
        if (donor && evalThis == donor) {
            evalThis = player;
        }

        // For conditions taking a target reference as parameter
        if constexpr (Cond == SpatialCondition::kGetDistance ||
                      Cond == SpatialCondition::kGetInSameCell ||
                      Cond == SpatialCondition::kGetHeadingAngle) {
            if (donor && evalP1 == donor) {
                evalP1 = player;
            }

            // If evaluating player relative to player (e.g. idle dialogue "player nearby" or proxying donor to player)
            if (evalThis == player && evalP1 == player) {
                if constexpr (Cond == SpatialCondition::kGetDistance) {
                    a_result = 0.0;
                    return true;
                } else if constexpr (Cond == SpatialCondition::kGetInSameCell) {
                    a_result = 1.0;
                    return true;
                } else if constexpr (Cond == SpatialCondition::kGetHeadingAngle) {
                    a_result = 0.0;
                    return true;
                }
            }
        }

        return orig(evalThis, evalP1, a_p2, a_result);
    }

    RE::BGSVoiceType* ImpersonationManager::GetNPCVoiceType(const RE::TESNPC* a_npc) {
        if (!a_npc) return nullptr;
        // 1. Direct member variable access (avoids any vtable dispatch mismatch)
        if (a_npc->voiceType) return a_npc->voiceType;
        // 2. Real engine function via original function pointer
        if (_origGetObjectVoiceType.address() != 0) {
            return _origGetObjectVoiceType(a_npc);
        }
        // 3. Race default fallback
        if (a_npc->race) {
            std::size_t sexIdx = (a_npc->GetSex() == RE::SEX::kFemale) ? 1 : 0;
            return a_npc->race->defaultVoiceTypes[sexIdx];
        }
        return nullptr;
    }

    RE::BGSVoiceType* ImpersonationManager::Hook_GetObjectVoiceType(const RE::TESNPC* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            auto* donorVoice = mgr._donorVoiceType.load(std::memory_order_acquire);
            if (mgr.IsActive() && donorVoice) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && a_this == player->GetActorBase()) {
                    if (mgr.IsNPCVoiceEnabled()) {
                        return donorVoice;
                    }
                    return mgr.GetOrigPlayerVoiceType();
                }

                auto* puppet = mgr.GetPuppet();
                bool isPuppet = (puppet && a_this == puppet->GetActorBase()) || mgr.IsPuppetBase(a_this);
                if (isPuppet) {
                    auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                    if (topicMgr && topicMgr->forceGoodbye) {
                        return nullptr;
                    }
                    return donorVoice;
                }
                if (a_this->voiceType) {
                    return a_this->voiceType;
                }
            }
        }
        return _origGetObjectVoiceType(a_this);
    }

    void ImpersonationManager::AttachSoundHandlesToHead(RE::Actor* a_actor) {
        if (!a_actor) return;
        auto* process = a_actor->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) return;

        auto* root3D = a_actor->Get3D();
        RE::NiAVObject* headNode = root3D ? root3D->GetObjectByName("NPC Head [Head]"sv) : nullptr;
        if (!headNode) {
            headNode = a_actor->GetFaceNodeSkinned();
        }
        if (!headNode && root3D) {
            headNode = root3D;
        }
        if (!headNode) return;

        for (auto& handle : process->high->soundHandles) {
            if (handle.IsValid() && handle.IsPlaying()) {
                handle.SetPosition(headNode->world.translate);
            }
        }
    }


    void ImpersonationManager::OnDialogueResponseStarted(RE::DialogueResponse* a_response) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _playerSpeaking.store(true, std::memory_order_release);
        _speechStartTime = std::chrono::steady_clock::now();
        _speechAudioEndTime = std::chrono::steady_clock::time_point{};
        _speechHadSound.store(false, std::memory_order_release);
        float textLen = 0.0f;
        if (a_response && !a_response->text.empty()) {
            textLen = static_cast<float>(a_response->text.length());
        }
        _speechMinDuration = std::max(1.5f, textLen * 0.065f);
    }

    void ImpersonationManager::CompleteSpeaking(RE::Actor* a_player) {
        if (_inCompleteSpeaking.exchange(true)) {
            return;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _playerSpeaking.store(false, std::memory_order_release);
            _speechHadSound.store(false, std::memory_order_release);
            _speechAudioEndTime = std::chrono::steady_clock::time_point{};
        }

        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        auto* dial = topicMgr ? topicMgr->lastSelectedDialogue : nullptr;
        bool hasNextResponse = (dial && dial->currentResponse && dial->currentResponse->next);

        if (a_player) {
            // Stop any remaining sound handles on player
            auto* process = a_player->GetActorRuntimeData().currentProcess;
            if (process && process->high) {
                for (auto& handle : process->high->soundHandles) {
                    if (handle.IsValid() && handle.IsPlaying()) {
                        handle.Stop();
                    }
                }
            }

            // Set kSoundFileDone and kVoiceFileDone so Character::UpdateInDialogue
            // at 0x68CF34 will load and play voice audio on the next line!
            a_player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
            a_player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
            a_player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
            a_player->GetActorRuntimeData().voiceTimer = 0.0f;
            _origPlayerSetSpeakingDone(a_player, true);
            if (auto* faceData = a_player->GetFaceGenAnimationData()) {
                RE::BSSpinLockGuard locker(faceData->lock);
                faceData->Reset(0.0f, false, true, false, false);
                faceData->ClearExpressionOverride();
            }
            // Clear current emotion and reset face
            a_player->SetEmotionType(RE::EmotionType::kNeutral);
            a_player->SetEmotionValue(0);
        }

        auto* puppet = GetPuppet();
        if (puppet) {
            // Set kSoundFileDone and kVoiceFileDone on puppet so Actor::IsCurrentlySpeaking (0x1406754B0)
            // returns false when the user clicks a dialogue option in DialogueMenu!
            puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
            puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
            puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
            puppet->GetActorRuntimeData().voiceTimer = 0.0f;
            puppet->SetEmotionType(RE::EmotionType::kNeutral);
            puppet->SetEmotionValue(0);
            if (auto* spkFace = puppet->GetFaceGenAnimationData()) {
                RE::BSSpinLockGuard locker(spkFace->lock);
                spkFace->Reset(0.0f, false, true, false, false);
                spkFace->ClearExpressionOverride();
            }
            _origCharSetSpeakingDone(puppet, true);
        }

        auto* donor = GetDonor();
        if (donor) {
            donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
            donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
            donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
            donor->GetActorRuntimeData().voiceTimer = 0.0f;
            _origCharSetSpeakingDone(donor, true);
        }

        // Advance MenuTopicManager
        if (topicMgr) {
            bool wasGreeting = topicMgr->isGreetingPlayer;
            topicMgr->isGreetingPlayer = false;

            if (puppet) {
                topicMgr->lastSpeaker = puppet->GetHandle();
            }

            if (dial) {
                if (hasNextResponse) {
                    // Multi-line topic response: advance to the next response in the chain
                    // and set advanceRequested = true so MenuTopicManager::Update calls AdvanceDialogue (0x1405EFE90)
                    dial->currentResponse = dial->currentResponse->next;
                    topicMgr->waitingToAdvance = true;
                    topicMgr->canSkip = true;
                    topicMgr->advanceRequested = true;
                    SKSE::log::info("CompleteSpeaking: advancing multi-line topic to next response (item: {:p})",
                        (void*)dial->currentResponse->item);
                } else {
                    // Final response in topic: do NOT wipe currentTopicInfo!
                    // MenuTopicManager::FinishResponseOrRunPapyrus (0x1405EFDF0) and EndDialogue (0x1405F0AD0)
                    // check currentTopicInfo to execute the End Papyrus script fragment (e.g. recruitment, quest stage).
                    // We advance currentResponse to nullptr and trigger advanceRequested so MenuTopicManager
                    // natively runs the End fragment and rebuilds the dialogue list.
                    dial->currentResponse = nullptr;
                    topicMgr->waitingToAdvance = true;
                    topicMgr->canSkip = true;
                    topicMgr->advanceRequested = true;
                    SKSE::log::info("CompleteSpeaking: topic finished, advancing to run End Papyrus fragment and return to menu");
                }
            } else {
                // Greeting line finished (no dial chain): preserve currentTopicInfo if present for greeting end fragments
                topicMgr->waitingToAdvance = true;
                topicMgr->canSkip = true;
                topicMgr->advanceRequested = true;
                SKSE::log::info("CompleteSpeaking: greeting line finished (wasGreeting: {})", wasGreeting);
            }
        }

        if (!hasNextResponse) {
            IdleAIManager::GetSingleton().ClearCurrentDialogueItem();
        }

        _inCompleteSpeaking.store(false);
    }

    void ImpersonationManager::ResetSpeechTracking() {
        auto* player = GetPlayer();
        CompleteSpeaking(player ? player : RE::PlayerCharacter::GetSingleton());
    }

    bool ImpersonationManager::CheckPlayerSpeakingDone() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_playerSpeaking.load(std::memory_order_acquire)) {
            return true;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return true;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _speechStartTime).count();

        // Check if player clicked to advance/skip line via MenuTopicManager
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr && topicMgr->advanceRequested && elapsed > 0.25f) {
            SKSE::log::info("Player requested dialogue line skip via MenuTopicManager (elapsed: {:.2f}s)", elapsed);
            CompleteSpeaking(player);
            return true;
        }

        // 1. Check if audio sound handle on player is currently playing
        bool soundPlaying = false;
        auto* process = player->GetActorRuntimeData().currentProcess;
        if (process && process->high) {
            for (auto& soundHandle : process->high->soundHandles) {
                if (soundHandle.IsValid() && soundHandle.IsPlaying()) {
                    soundPlaying = true;
                    break;
                }
            }
        }

        if (soundPlaying) {
            AttachSoundHandlesToHead(player);
            if (!_speechHadSound.load(std::memory_order_acquire)) {
                _speechStartTime = now;
                elapsed = 0.0f;
            }
            _speechHadSound.store(true, std::memory_order_release);

            // Safety timeout of 25.0s in case sound loops
            if (elapsed < 25.0f) {
                return false;
            }
        }

        // 2. Check BSFaceGenAnimationData phoneme completion on player
        bool phonemesActive = false;
        if (auto* faceData = player->GetFaceGenAnimationData()) {
            RE::BSSpinLockGuard locker(faceData->lock);
            auto checkActiveKeyframe = [](const RE::BSFaceGenKeyframeMultiple& kf) -> bool {
                if (kf.values && kf.count > 0 && kf.count < 64) {
                    for (std::uint32_t i = 0; i < kf.count; ++i) {
                        if (kf.values[i] > 0.005f) {
                            return true;
                        }
                    }
                }
                return false;
            };
            if (checkActiveKeyframe(faceData->phoneme1) ||
                checkActiveKeyframe(faceData->phoneme3) ||
                checkActiveKeyframe(faceData->phenomeKeyFrame)) {
                phonemesActive = true;
            }
        }

        if (_speechHadSound.load(std::memory_order_acquire)) {
            // Audio was playing and has now finished!
            // Allow a brief grace period (up to 0.4s) if phonemes are still settling back to neutral
            if (phonemesActive && elapsed < 25.0f && _speechAudioEndTime == std::chrono::steady_clock::time_point{}) {
                _speechAudioEndTime = now;
                return false;
            }
            if (phonemesActive && _speechAudioEndTime != std::chrono::steady_clock::time_point{}) {
                float phonemeWait = std::chrono::duration<float>(now - _speechAudioEndTime).count();
                if (phonemeWait < 0.4f) {
                    return false;
                }
            }

            SKSE::log::info("Player dialogue speech completed (native audio & phonemes finished after {:.2f}s)", elapsed);
            CompleteSpeaking(player);
            return true;
        }

        // Unvoiced line: wait for minimal duration based on text length
        if (elapsed < _speechMinDuration) {
            return false;
        }

        SKSE::log::info("Player dialogue speech completed (unvoiced line finished after {:.2f}s)", elapsed);
        CompleteSpeaking(player);
        return true;
    }

    void ImpersonationManager::SyncDonorUpdatesToPlayer() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_active.load(std::memory_order_acquire)) return;
        auto* donor = GetDonor();
        auto* player = GetPlayer();
        if (!donor || !player) return;

        if (!IdleAIManager::GetSingleton().IsAIActive()) {
            return;
        }

        // Sync AI packages from donor to player base
        auto* donorBase = donor->GetActorBase();
        auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
        if (donorBase && playerBase) {
            std::vector<RE::TESPackage*> donorPkgs;

            // Priority 1: Quest alias packages (MG01, etc.)
            auto aliasPkgs = AliasManager::GetSingleton().GetAliasPackages(donor);
            for (auto* pkg : aliasPkgs) {
                if (pkg) {
                    donorPkgs.push_back(pkg);
                }
            }

            // Priority 2: Base NPC packages
            for (auto* pkg : donorBase->aiPackages.packages) {
                if (pkg) {
                    bool alreadyAdded = false;
                    for (auto* p : donorPkgs) {
                        if (p == pkg) {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if (!alreadyAdded) {
                        donorPkgs.push_back(pkg);
                    }
                }
            }

            // Priority 3: Fallback sandbox
            auto* defaultSandbox = RE::TESForm::LookupByID<RE::TESPackage>(0x000BFB6B);
            if (defaultSandbox) {
                bool hasSandbox = false;
                for (auto* pkg : donorPkgs) {
                    if (pkg == defaultSandbox) {
                        hasSandbox = true;
                        break;
                    }
                }
                if (!hasSandbox) {
                    donorPkgs.push_back(defaultSandbox);
                }
            }

            // Retarget any donor package targeting Player (0x14 / 0x07) to Puppet so force-greet packages walk to Puppet
            if (auto* puppet = GetPuppet()) {
                auto playerHandle = player->GetHandle();
                auto puppetHandle = puppet->GetHandle();
                RE::TESForm* playerBaseForm = player->GetBaseObject();

                for (auto* pkg : donorPkgs) {
                    if (!pkg || !pkg->packTarg) continue;

                    bool alreadyTracked = false;
                    for (const auto& entry : _retargetedPackages) {
                        if (entry.package == pkg) {
                            alreadyTracked = true;
                            break;
                        }
                    }

                    std::uint8_t tType = static_cast<std::uint8_t>(pkg->packTarg->targType);
                    if (tType == 0) {
                        if (pkg->packTarg->target.handle == playerHandle) {
                            if (!alreadyTracked) {
                                RetargetedPackageTarget rec;
                                rec.package = pkg;
                                rec.packageFormID = pkg->GetFormID();
                                rec.targType = tType;
                                rec.origHandle = pkg->packTarg->target.handle;
                                _retargetedPackages.push_back(rec);
                            }
                            pkg->packTarg->target.handle = puppetHandle;
                            SKSE::log::info("SyncDonorUpdatesToPlayer: Retargeted package {:08X} handle target from Player to Puppet {:08X}",
                                pkg->GetFormID(), puppet->GetFormID());
                        }
                    } else if (tType == 1) {
                        if (pkg->packTarg->target.object == player || pkg->packTarg->target.object == playerBaseForm) {
                            if (!alreadyTracked) {
                                RetargetedPackageTarget rec;
                                rec.package = pkg;
                                rec.packageFormID = pkg->GetFormID();
                                rec.targType = tType;
                                rec.origObject = pkg->packTarg->target.object;
                                _retargetedPackages.push_back(rec);
                            }
                            pkg->packTarg->target.object = puppet;
                            SKSE::log::info("SyncDonorUpdatesToPlayer: Retargeted package {:08X} object target from Player to Puppet {:08X}",
                                pkg->GetFormID(), puppet->GetFormID());
                        }
                    }
                }
            }

            std::vector<RE::TESPackage*> playerPkgs;
            for (auto* pkg : playerBase->aiPackages.packages) {
                if (pkg) {
                    playerPkgs.push_back(pkg);
                }
            }

            if (donorPkgs != playerPkgs) {
                playerBase->aiPackages.packages.clear();
                for (auto it = donorPkgs.rbegin(); it != donorPkgs.rend(); ++it) {
                    playerBase->aiPackages.packages.push_front(*it);
                }
                player->EvaluatePackage(false, false);
                SKSE::log::info("Synchronized updated donor AI packages to player.");
            }
        }
    }

    bool ImpersonationManager::HandleProxyDialogueOutput(RE::Actor* a_speaker, RE::DialogueResponse* a_response, bool a_unused) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_speaker) {
            return _origCharUpdateInDialogue(a_speaker, a_response, a_unused);
        }

        if (a_response != nullptr) {
            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            bool inSelf = IdleAIManager::GetSingleton().IsInSelfDialogue();
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && (topicMgr->menuOpen || topicMgr->isGreetingPlayer));
            auto* donor = GetDonor();
            bool isDonor = (donor && a_speaker == donor);
            auto* puppet = GetPuppet();
            bool isPuppet = (puppet && a_speaker == puppet);

            bool inTopicWithSpeaker = false;
            if (topicMgr && (isPuppet || isDonor)) {
                auto spkHandle = a_speaker->GetHandle();
                if (menuOpen && (topicMgr->speaker == spkHandle || topicMgr->lastSpeaker == spkHandle)) {
                    inTopicWithSpeaker = true;
                }
            }

            // Suppress automatic puppet NPC goodbye farewell blurbs
            bool isGoodbye = false;
            if (topicMgr) {
                if (topicMgr->forceGoodbye) {
                    isGoodbye = true;
                }
                auto checkSubtypeGoodbye = [](RE::TESTopicInfo* info) -> bool {
                    if (!info || !info->parentTopic) return false;
                    auto st = info->parentTopic->data.subtype.get();
                    return (st == RE::DIALOGUE_DATA::Subtype::kGoodBye || st == RE::DIALOGUE_DATA::Subtype::kTimeToGo);
                };
                if (checkSubtypeGoodbye(topicMgr->currentTopicInfo)) {
                    isGoodbye = true;
                }
            }

            // Allow speech ONLY if actively in conversation:
            // self-dialogue flag set, dialogue menu actively open, or MenuTopicManager active with this speaker.
            bool inActiveConversation = inSelf || menuOpen || inTopicWithSpeaker;

            if (isPuppet && (isGoodbye || !inActiveConversation)) {
                SKSE::log::info("Suppressing puppet dialogue exit/goodbye/ambient line: '{}'", a_response->text.c_str());
                return false;
            }

            if (inActiveConversation || isDonor) {
                SKSE::log::info("Decoupled proxy dialogue: text='{}', voice='{}', sound={:08X}",
                    a_response->text.c_str(),
                    a_response->voice.c_str(),
                    a_response->voiceSound ? static_cast<std::uint32_t>(a_response->voiceSound->GetFormID()) : 0);

                // Stop any previous sound handles still playing on player
                auto* playerProcess = player->GetActorRuntimeData().currentProcess;
                if (playerProcess && playerProcess->high) {
                    for (auto& handle : playerProcess->high->soundHandles) {
                        if (handle.IsValid() && handle.IsPlaying()) {
                            handle.Stop();
                        }
                    }
                }

                // Reset advanceRequested on topicMgr so new line is not prematurely skipped
                if (topicMgr) {
                    topicMgr->advanceRequested = false;
                    topicMgr->lineSkipped = false;
                }

                // 1. Prepare player response clone with sensory data (sound, lip, emotion, idle)
                RE::DialogueResponse playerResp;
                playerResp.text = a_response->text;
                playerResp.animFaceArchType = a_response->animFaceArchType;
                playerResp.percent = a_response->percent;
                playerResp.voice = a_response->voice;
                playerResp.speakerIdle = a_response->speakerIdle;
                playerResp.listenIdle = a_response->listenIdle;
                playerResp.voiceSound = a_response->voiceSound;
                playerResp.useEmotion = a_response->useEmotion;
                playerResp.soundLip = a_response->soundLip;

                // 2. Prepare puppet response clone: has text for topic advance,
                // but completely stripped of audio, mouth phonemes, emotion, and speaking gestures.
                RE::DialogueResponse puppetResp;
                puppetResp.text = a_response->text;
                puppetResp.animFaceArchType = RE::EmotionType::kNeutral;
                puppetResp.percent = 0;
                puppetResp.voice = ""sv;
                puppetResp.speakerIdle = nullptr;
                puppetResp.listenIdle = nullptr;
                puppetResp.voiceSound = nullptr;
                puppetResp.useEmotion = false;
                puppetResp.soundLip = false;

                // 3. Ensure player boolBits have kSoundFileDone and kVoiceFileDone set, kSpeakingDone cleared,
                // so Character::UpdateInDialogue takes the new-speech branch
                player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
                player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
                player->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
                player->GetActorRuntimeData().voiceTimer = 0.0f;
                _origPlayerSetSpeakingDone(player, false);

                // 4. Ensure puppet has speakingDone cleared, and voiceDone/soundDone cleared so it stays silent
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSoundFileDone);
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kVoiceFileDone);
                a_speaker->GetActorRuntimeData().voiceTimer = 0.0f;
                _origCharSetSpeakingDone(a_speaker, false);

                // 5. Start speech tracking for player duration / audio completion
                OnDialogueResponseStarted(&playerResp);

                // 6. Explicitly queue or force subtitle display associated with the PLAYER
                if (!a_response->text.empty()) {
                    if (auto* subMgr = RE::SubtitleManager::GetSingleton()) {
                        RE::BSSpinLockGuard locker(subMgr->lock);
                        bool alreadyQueued = false;
                        for (auto& sub : subMgr->subtitles) {
                            if (sub.subtitle == a_response->text.c_str()) {
                                alreadyQueued = true;
                                sub.speaker = player->GetHandle();
                                sub.targetDistance = 0.0f;
                                sub.forceDisplay = true;
                                break;
                            }
                        }
                        if (!alreadyQueued) {
                            RE::SubtitleInfo info;
                            info.speaker = player->GetHandle();
                            info.subtitle = a_response->text;
                            info.targetDistance = 0.0f;
                            info.forceDisplay = true;
                            subMgr->subtitles.push_back(info);
                        }
                    }
                }

                // 7. Dispatch sensory playback directly onto PlayerCharacter natively!
                if (playerResp.useEmotion) {
                    player->SetEmotionType(playerResp.animFaceArchType.get());
                    player->SetEmotionValue(playerResp.percent);
                }
                _origCharUpdateInDialogue(player, &playerResp, a_unused);
                AttachSoundHandlesToHead(player);

                // Set player offset 0x278 = 1 so Character::UpdateSoundCallBack (0x14068E150) treats
                // player voice completion as an external dialogue line rather than mode 2 (MenuTopicManager),
                // preventing UpdateSoundCallBack from racing CompleteSpeaking and prematurely advancing MenuTopicManager.
                *reinterpret_cast<std::uint8_t*>(reinterpret_cast<char*>(player) + 0x278) = 1;

                // 8. Dispatch silent logical response to puppet (updates MenuTopicManager and subtitle display)
                bool ret = _origCharUpdateInDialogue(a_speaker, &puppetResp, a_unused);

                // Ensure puppet kSoundFileDone and kVoiceFileDone remain cleared while player is speaking
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSoundFileDone);
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kVoiceFileDone);

                // 9. Ensure puppet face and emotion remain strictly neutral
                if (auto* spkFace = a_speaker->GetFaceGenAnimationData()) {
                    RE::BSSpinLockGuard locker(spkFace->lock);
                    spkFace->ClearExpressionOverride();
                    spkFace->Reset(0.0f, true, false, false, false);
                }
                a_speaker->SetEmotionType(RE::EmotionType::kNeutral);
                a_speaker->SetEmotionValue(0);

                return ret;
            } else {
                // Suppress proxy actor spontaneous idle speech outside active conversation
                SKSE::log::info("Suppressing proxy actor spontaneous idle speech outside conversation: '{}'", a_response->text.c_str());
                return false;
            }
        } else {
            // Ticks with a_response == nullptr from MenuTopicManager during dialogue:
            if (_playerSpeaking.load(std::memory_order_relaxed)) {
                // While player is speaking, keep puppet's kVoiceFileDone and kSoundFileDone reset to 0
                // so Actor::IsCurrentlySpeaking (0x1406754B0) returns true and 0x14068D2AA does not
                // prematurely advance lastSelectedDialogue before CompleteSpeaking runs.
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSoundFileDone);
                a_speaker->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kVoiceFileDone);

                // 1. Keep player's dialogue state and audio emitter updated
                _origCharUpdateInDialogue(player, nullptr, a_unused);
                AttachSoundHandlesToHead(player);
            } else {
                // When player is NOT speaking (waiting for user to select a dialogue option),
                // kSoundFileDone and kVoiceFileDone MUST remain SET (1) on puppet so
                // Actor::IsCurrentlySpeaking (0x1406754B0) returns false when the user clicks a topic
                // in MenuTopicManager::PlaySelectedTopic (0x1405EFB90)!
                a_speaker->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
                a_speaker->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
                a_speaker->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
                a_speaker->GetActorRuntimeData().voiceTimer = 0.0f;

                // Keep high->voiceTimer positive while DialogueMenu is open so 0x14068D3E4 does not
                // prematurely call MenuTopicManager::EndDialogue (0x1405F0AD0) while browsing topics.
                auto* ui = RE::UI::GetSingleton();
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                if ((ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && topicMgr->menuOpen)) {
                    if (auto* spkProcess = a_speaker->GetActorRuntimeData().currentProcess; spkProcess && spkProcess->high) {
                        spkProcess->high->voiceTimer = 10.0f;
                    }
                }
            }

            // 2. Allow puppet to tick native UpdateInDialogue so MenuTopicManager stays synchronized
            return _origCharUpdateInDialogue(a_speaker, nullptr, a_unused);
        }
    }

    bool ImpersonationManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                auto* donor = mgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    return mgr.HandleProxyDialogueOutput(a_this, a_response, a_unused);
                }
            }
        }
        return _origCharUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool ImpersonationManager::Hook_Player_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        if (a_this && a_response != nullptr) {
            SKSE::log::info("Player {:08X} speaking dialogue line: '{}'",
                static_cast<std::uint32_t>(a_this->GetFormID()),
                a_response->text.c_str());
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                if (a_response->useEmotion) {
                    a_this->SetEmotionType(a_response->animFaceArchType.get());
                    a_this->SetEmotionValue(a_response->percent);
                }

                // Explicitly queue subtitle for player
                if (!a_response->text.empty()) {
                    if (auto* subMgr = RE::SubtitleManager::GetSingleton()) {
                        RE::BSSpinLockGuard locker(subMgr->lock);
                        bool alreadyQueued = false;
                        for (auto& sub : subMgr->subtitles) {
                            if (sub.subtitle == a_response->text.c_str()) {
                                alreadyQueued = true;
                                sub.speaker = a_this->GetHandle();
                                sub.targetDistance = 0.0f;
                                sub.forceDisplay = true;
                                break;
                            }
                        }
                        if (!alreadyQueued) {
                            RE::SubtitleInfo info;
                            info.speaker = a_this->GetHandle();
                            info.subtitle = a_response->text;
                            info.targetDistance = 0.0f;
                            info.forceDisplay = true;
                            subMgr->subtitles.push_back(info);
                        }
                    }
                }

                // Ensure player boolBits allow Character::UpdateInDialogue to play audio and phonemes
                a_this->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSoundFileDone);
                a_this->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kVoiceFileDone);
                a_this->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
                a_this->GetActorRuntimeData().voiceTimer = 0.0f;

                mgr.OnDialogueResponseStarted(a_response);
                bool ret = _origCharUpdateInDialogue(a_this, a_response, a_unused);
                AttachSoundHandlesToHead(a_this);
                return ret;
            }
        }
        bool ret = _origPlayerUpdateInDialogue(a_this, a_response, a_unused);
        if (a_this && a_response != nullptr) {
            AttachSoundHandlesToHead(a_this);
        }
        return ret;
    }

    bool ImpersonationManager::Hook_Character_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                auto* donor = mgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    if (!mgr.IsPlayerSpeaking()) {
                        return true;
                    }
                    return mgr.CheckPlayerSpeakingDone();
                }
            }
        }
        return _origCharQSpeakingDone(a_this);
    }

    bool ImpersonationManager::Hook_Player_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                if (!mgr.IsPlayerSpeaking()) {
                    return true;
                }
                return mgr.CheckPlayerSpeakingDone();
            }
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void ImpersonationManager::Hook_Character_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        if (a_this && a_set) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                auto* donor = mgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    if (mgr.IsPlayerSpeaking()) {
                        // Block early completion while player is still speaking
                        return;
                    }
                    auto* player = mgr.GetPlayer();
                    if (!player) player = RE::PlayerCharacter::GetSingleton();
                    if (player) {
                        player->SetEmotionType(RE::EmotionType::kNeutral);
                        player->SetEmotionValue(0);
                        if (auto* faceData = player->GetFaceGenAnimationData()) {
                            RE::BSSpinLockGuard locker(faceData->lock);
                            faceData->Reset(0.0f, false, true, false, false);
                            faceData->ClearExpressionOverride();
                        }
                    }
                }
            }
        }
        _origCharSetSpeakingDone(a_this, a_set);
    }

    void ImpersonationManager::Hook_Player_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                if (a_set) {
                    if (mgr.IsPlayerSpeaking() && !mgr.IsInCompleteSpeaking()) {
                        // Block early completion while player is still speaking
                        return;
                    }
                    if (!mgr.IsInCompleteSpeaking()) {
                        mgr.ResetSpeechTracking();
                    }
                    a_this->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
                } else {
                    mgr.SetPlayerSpeaking(true);
                    a_this->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kSpeakingDone);
                }
            }
        }
        _origPlayerSetSpeakingDone(a_this, a_set);
    }

    bool ImpersonationManager::Hook_SetDialogueWithPlayer(RE::Actor* a_this, bool a_flag, bool a_forceGreet, RE::TESTopicInfo* a_topic) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                auto* donor = mgr.GetDonor();
                if ((puppet && a_this == puppet) || (donor && a_this == donor)) {
                    SKSE::log::info("Hook_SetDialogueWithPlayer: actor {:08X} (isPuppet: {}), flag: {}, forceGreet: {}",
                        static_cast<std::uint32_t>(a_this->GetFormID()), (a_this == puppet), a_flag, a_forceGreet);
                    IdleAIManager::GetSingleton().SetInSelfDialogue(a_flag);
                    if (!a_flag && a_this == puppet) {
                        // Forward dynamic faction changes from puppet to donor
                        if (donor) {
                            auto* extraFaction = puppet->extraList.GetByType<RE::ExtraFactionChanges>();
                            if (extraFaction) {
                                for (const auto& fRank : extraFaction->factionChanges) {
                                    if (fRank.faction) {
                                        donor->AddToFaction(fRank.faction, fRank.rank);
                                        SKSE::log::info("Hook_SetDialogueWithPlayer: Forwarded faction {:08X} rank {} from puppet to donor.",
                                            fRank.faction->GetFormID(), fRank.rank);

                                        // If recruited into follower or teammate faction, ensure donor has kPlayerTeammate set
                                        if (fRank.rank >= 0) {
                                            auto fid = fRank.faction->GetFormID();
                                            if (fid == 0x0005C84E /*CurrentFollowerFaction*/ || fid == 0x000BD738 /*PlayerFollowerFaction*/) {
                                                donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
                                                SKSE::log::info("Hook_SetDialogueWithPlayer: Set kPlayerTeammate on donor {:08X} due to faction {:08X}",
                                                    donor->GetFormID(), fid);
                                            }
                                        }
                                    }
                                }
                                extraFaction->factionChanges.clear();
                            }
                        }

                        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                        if (topicMgr) {
                            if (topicMgr->speaker == a_this->GetHandle()) {
                                topicMgr->speaker.reset();
                            }
                            if (topicMgr->lastSpeaker == a_this->GetHandle()) {
                                topicMgr->lastSpeaker.reset();
                            }
                        }
                    }
                }
            }
        }
        return _origSetDialogueWithPlayer(a_this, a_flag, a_forceGreet, a_topic);
    }


    void ImpersonationManager::Hook_Player_UpdateAnimation(RE::Actor* a_this, float a_delta) {
        _origPlayerUpdateAnimation(a_this, a_delta);
        if (a_this) {
            IdleAIManager::GetSingleton().Update();
        }
    }

    RE::BGSLocation* ImpersonationManager::Hook_Player_GetEditorLocation1(const RE::TESObjectREFR* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* donor = mgr.GetDonor();
                if (donor) {
                    return donor->GetEditorLocation1();
                }
            }
        }
        return _origPlayerGetEditorLocation1(a_this);
    }

    bool ImpersonationManager::Hook_Player_GetEditorLocation2(
        RE::TESObjectREFR* a_this,
        RE::NiPoint3& a_outPos,
        RE::NiPoint3& a_outRot,
        RE::TESForm*& a_outWorldOrCell,
        RE::TESObjectCELL* a_fallback)
    {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* donor = mgr.GetDonor();
                if (donor) {
                    return donor->GetEditorLocation2(a_outPos, a_outRot, a_outWorldOrCell, a_fallback);
                }
            }
        }
        return _origPlayerGetEditorLocation2(a_this, a_outPos, a_outRot, a_outWorldOrCell, a_fallback);
    }

    void ImpersonationManager::Hook_ForceRefIntoAlias(CONTEXT& a_ctx) {
        if (!a_ctx.Rcx || !a_ctx.R8) return;

        auto* quest = reinterpret_cast<RE::TESQuest*>(a_ctx.Rcx);
        auto aliasID = static_cast<std::uint32_t>(a_ctx.Rdx);
        auto* targetRef = reinterpret_cast<RE::TESObjectREFR*>(a_ctx.R8);

        auto& mgr = ImpersonationManager::GetSingleton();
        if (mgr.IsActive()) {
            auto* puppet = mgr.GetPuppet();
            auto* donor = mgr.GetDonor();
            if (puppet && targetRef == puppet && !AliasManager::IsModInternalQuest(quest)) {
                if (donor) {
                    SKSE::log::info("Hook_ForceRefIntoAlias: Intercepted external quest {:08X} assigning alias ID {} to puppet {:08X}. Redirecting to donor {:08X}.",
                        static_cast<std::uint32_t>(quest->GetFormID()),
                        aliasID,
                        static_cast<std::uint32_t>(targetRef->GetFormID()),
                        static_cast<std::uint32_t>(donor->GetFormID()));

                    RE::BGSBaseAlias* alias = nullptr;
                    for (auto* a : quest->aliases) {
                        if (a && a->aliasID == aliasID) {
                            alias = a;
                            break;
                        }
                    }
                    if (alias) {
                        std::string aliasName = alias->aliasName.c_str();
                        std::transform(aliasName.begin(), aliasName.end(), aliasName.begin(), ::tolower);
                        if (aliasName.find("follower") != std::string::npos || aliasName.find("hireling") != std::string::npos) {
                            donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
                            SKSE::log::info("Hook_ForceRefIntoAlias: Set kPlayerTeammate on donor {:08X} for alias '{}'",
                                donor->GetFormID(), alias->aliasName.c_str());
                        }
                    }

                    a_ctx.R8 = reinterpret_cast<DWORD64>(donor);
                }
            }
        }
    }

    void ImpersonationManager::InstallHooks() {
        if (_hooksInstalled) return;

        // Hook TESQuest::ForceRefIntoAlias (RELOCATION_ID(24523, 25052)) via context hook
        auto forceRefAddr = REL::RelocationID(24523, 25052).address();
        if (SKSE::stl::install_context_hook(forceRefAddr, 5, &Hook_ForceRefIntoAlias, -5)) {
            SKSE::log::info("Installed TESQuest::ForceRefIntoAlias context hook at {:016X}", forceRefAddr);
        } else {
            SKSE::log::error("Failed to install TESQuest::ForceRefIntoAlias context hook!");
        }

        // Hook TESNPC::GetObjectVoiceType on primary vtable (index 0x49)
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESNPC[0] };
        _origGetObjectVoiceType = vtbl.write_vfunc(0x49, &Hook_GetObjectVoiceType);
        SKSE::log::info("Hooked TESNPC::GetObjectVoiceType at VTABLE_TESNPC[0] idx 0x49 (orig: {:016X})",
            _origGetObjectVoiceType.address());

        // Hook Character::SetDialogueWithPlayer on primary vtable (index 0x41)
        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[0] };
        _origSetDialogueWithPlayer = charVtbl.write_vfunc(0x41, &Hook_SetDialogueWithPlayer);
        SKSE::log::info("Hooked Character::SetDialogueWithPlayer at VTABLE_Character[0] idx 0x41 (orig: {:016X})",
            _origSetDialogueWithPlayer.address());

        // Hook Character vtable (0x4C UpdateInDialogue, 0x107 QSpeakingDone, 0x108 SetSpeakingDone)
        _origCharUpdateInDialogue = charVtbl.write_vfunc(0x4C, &Hook_Character_UpdateInDialogue);
        _origCharQSpeakingDone = charVtbl.write_vfunc(0x107, &Hook_Character_QSpeakingDone);
        _origCharSetSpeakingDone = charVtbl.write_vfunc(0x108, &Hook_Character_SetSpeakingDone);
        SKSE::log::info("Hooked Character dialogue vfuncs (0x4C, 0x107, 0x108)");

        // Hook PlayerCharacter vtable (0x3C GetEditorLocation1, 0x3D GetEditorLocation2, 0x4C UpdateInDialogue, 0x7D UpdateAnimation, 0x107 QSpeakingDone, 0x108 SetSpeakingDone)
        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerGetEditorLocation1 = playerVtbl.write_vfunc(0x3C, &Hook_Player_GetEditorLocation1);
        _origPlayerGetEditorLocation2 = playerVtbl.write_vfunc(0x3D, &Hook_Player_GetEditorLocation2);
        _origPlayerUpdateInDialogue = playerVtbl.write_vfunc(0x4C, &Hook_Player_UpdateInDialogue);
        _origPlayerUpdateAnimation = playerVtbl.write_vfunc(0x7D, &Hook_Player_UpdateAnimation);
        _origPlayerQSpeakingDone = playerVtbl.write_vfunc(0x107, &Hook_Player_QSpeakingDone);
        _origPlayerSetSpeakingDone = playerVtbl.write_vfunc(0x108, &Hook_Player_SetSpeakingDone);
        SKSE::log::info("Hooked PlayerCharacter editor location, dialogue & update vfuncs (0x3C, 0x3D, 0x4C, 0x7D, 0x107, 0x108)");

        auto hookEntry = [](IdentityCondition cond, std::string_view name, RE::SCRIPT_FUNCTION::Condition_t* thunk) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(name);
            if (func) {
                auto idx = static_cast<std::size_t>(cond);
                s_originalConditions[idx] = func->conditionFunction;
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(thunk));
                SKSE::log::info("Hooked condition '{}' via LocateScriptCommand (orig: {:016X})",
                    name, reinterpret_cast<std::uintptr_t>(s_originalConditions[idx]));
                return true;
            } else {
                SKSE::log::error("Failed to locate script command '{}'!", name);
                return false;
            }
        };

        std::size_t hookedCount = 0;
        if (hookEntry(IdentityCondition::kGetInFaction, "GetInFaction"sv, &IdentityThunk<IdentityCondition::kGetInFaction>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsID, "GetIsID"sv, &IdentityThunk<IdentityCondition::kGetIsID>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetFactionRank, "GetFactionRank"sv, &IdentityThunk<IdentityCondition::kGetFactionRank>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsReference, "GetIsReference"sv, &IdentityThunk<IdentityCondition::kGetIsReference>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsAliasRef, "GetIsAliasRef"sv, &IdentityThunk<IdentityCondition::kGetIsAliasRef>)) hookedCount++;
        if (hookEntry(IdentityCondition::kGetIsVoiceType, "GetIsVoiceType"sv, &IdentityThunk<IdentityCondition::kGetIsVoiceType>)) hookedCount++;
        if (hookEntry(IdentityCondition::kIsInList, "IsInList"sv, &IdentityThunk<IdentityCondition::kIsInList>)) hookedCount++;

        auto hookSpatialEntry = [](SpatialCondition cond, std::string_view name, RE::SCRIPT_FUNCTION::Condition_t* thunk) {
            auto* func = RE::SCRIPT_FUNCTION::LocateScriptCommand(name);
            if (func) {
                auto idx = static_cast<std::size_t>(cond);
                s_originalSpatialConditions[idx] = func->conditionFunction;
                REL::safe_write(reinterpret_cast<std::uintptr_t>(&func->conditionFunction), reinterpret_cast<std::uintptr_t>(thunk));
                SKSE::log::info("Hooked spatial condition '{}' via LocateScriptCommand (orig: {:016X})",
                    name, reinterpret_cast<std::uintptr_t>(s_originalSpatialConditions[idx]));
                return true;
            } else {
                SKSE::log::error("Failed to locate script command '{}'!", name);
                return false;
            }
        };

        if (hookSpatialEntry(SpatialCondition::kGetInCell, "GetInCell"sv, &SpatialConditionThunk<SpatialCondition::kGetInCell>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetInCurrentLoc, "GetInCurrentLoc"sv, &SpatialConditionThunk<SpatialCondition::kGetInCurrentLoc>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetDistance, "GetDistance"sv, &SpatialConditionThunk<SpatialCondition::kGetDistance>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kIsInInterior, "IsInInterior"sv, &SpatialConditionThunk<SpatialCondition::kIsInInterior>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetInSameCell, "GetInSameCell"sv, &SpatialConditionThunk<SpatialCondition::kGetInSameCell>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetInWorldspace, "GetInWorldspace"sv, &SpatialConditionThunk<SpatialCondition::kGetInWorldspace>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetPos, "GetPos"sv, &SpatialConditionThunk<SpatialCondition::kGetPos>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetHeadingAngle, "GetHeadingAngle"sv, &SpatialConditionThunk<SpatialCondition::kGetHeadingAngle>)) hookedCount++;
        if (hookSpatialEntry(SpatialCondition::kGetAngle, "GetAngle"sv, &SpatialConditionThunk<SpatialCondition::kGetAngle>)) hookedCount++;

        SKSE::log::info("Successfully installed {} script condition hooks via CommonLibSSE-NG.", hookedCount);

        _hooksInstalled = true;
        SKSE::log::info("Installed Impersonation identity and spatial condition function hooks and dialogue vfunc hooks successfully.");
    }

    void ImpersonationManager::Initialize() {
        InstallHooks();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (dataHandler) {
            auto* malePuppet = dataHandler->LookupForm<RE::TESNPC>(0x00000D93, "Alternate Actors NG.esp"sv);
            auto* femalePuppet = dataHandler->LookupForm<RE::TESNPC>(0x00000D94, "Alternate Actors NG.esp"sv);
            _puppetBaseMale.store(malePuppet, std::memory_order_release);
            _puppetBaseFemale.store(femalePuppet, std::memory_order_release);
            SKSE::log::info("Resolved puppet base forms: male={:08X}, female={:08X}",
                malePuppet ? malePuppet->GetFormID() : 0,
                femalePuppet ? femalePuppet->GetFormID() : 0);

            auto* marker = dataHandler->LookupForm<RE::TESObjectREFR>(0x00000D97, "Alternate Actors NG.esp"sv);
            if (marker) {
                _cloneMarker = marker->GetHandle();
                SKSE::log::info("Resolved clone marker: {:08X}", marker->GetFormID());
            } else {
                SKSE::log::warn("Failed to resolve clone marker 0x00000D97 from Alternate Actors NG.esp!");
            }
        }
    }

    std::int32_t ImpersonationManager::Begin(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) {
            SKSE::log::warn("ImpersonationManager::Begin called with null actor!");
            return 0;
        }

        std::lock_guard<std::recursive_mutex> lock(_mutex);

        if (_active.load(std::memory_order_acquire)) {
            End();
        }

        _donorActor.store(a_donor, std::memory_order_release);
        _playerActor.store(a_player, std::memory_order_release);
        _donorKeepAlive.reset(a_donor);
        _donorHandle = a_donor->GetHandle();
        _savedDonorFormID = a_donor->GetFormID();
        _donorName = a_donor->GetName();
        const char* dispName = a_player->GetDisplayFullName();
        if (FaceManager::GetSingleton().HasSavedAppearance() &&
            !FaceManager::GetSingleton().GetSavedAppearance().fullName.empty()) {
            _origPlayerName = FaceManager::GetSingleton().GetSavedAppearance().fullName.c_str();
        } else if (dispName && dispName[0] != '\0') {
            _origPlayerName = dispName;
        } else {
            _origPlayerName = a_player->GetName();
        }

        // Apply donor name onto player display
        if (!_donorName.empty()) {
            if (auto* base = a_player->GetActorBase()) {
                base->fullName = _donorName.c_str();
            }
            auto* textData = a_player->extraList.GetByType<RE::ExtraTextDisplayData>();
            if (!textData) {
                textData = new RE::ExtraTextDisplayData(_donorName.c_str());
                a_player->extraList.Add(textData);
            } else {
                textData->SetName(_donorName.c_str());
            }
        }

        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            _origPlayerVoiceType.store(FaceManager::GetSingleton().GetSavedAppearance().voiceType, std::memory_order_release);
        } else if (auto* pBase = a_player->GetActorBase()) {
            _origPlayerVoiceType.store(GetNPCVoiceType(pBase), std::memory_order_release);
        } else {
            _origPlayerVoiceType.store(nullptr, std::memory_order_release);
        }

        if (auto* base = a_donor->GetActorBase()) {
            _donorVoiceType.store(GetNPCVoiceType(base), std::memory_order_release);
        } else {
            _donorVoiceType.store(nullptr, std::memory_order_release);
        }
        _active.store(true, std::memory_order_release);

        SKSE::log::info("Beginning impersonation: Donor {:08X} ({}) -> Player {:08X}",
            static_cast<std::uint32_t>(a_donor->GetFormID()),
            a_donor->GetName(),
            static_cast<std::uint32_t>(a_player->GetFormID())
        );

        // Clear packages from player
        ClearPlayerPackages();

        return 1;
    }

    std::int32_t ImpersonationManager::End() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_active.load(std::memory_order_acquire)) {
            return 0;
        }

        auto* donor = _donorActor.load(std::memory_order_acquire);
        auto* player = _playerActor.load(std::memory_order_acquire);

        SKSE::log::info("Ending impersonation of donor {:08X}",
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : static_cast<std::uint32_t>(_savedDonorFormID)
        );

        // Clear packages from player
        ClearPlayerPackages();
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (pc) {
            pc->SetAIDriven(false);
            pc->EvaluatePackage(false, false);
        }

        // Restore player's original name
        if (player && !_origPlayerName.empty()) {
            if (auto* base = player->GetActorBase()) {
                base->fullName = _origPlayerName.c_str();
            }
            auto* textData = player->extraList.GetByType<RE::ExtraTextDisplayData>();
            if (!textData) {
                textData = new RE::ExtraTextDisplayData(_origPlayerName.c_str());
                player->extraList.Add(textData);
            } else {
                textData->SetName(_origPlayerName.c_str());
            }
        }

        _active.store(false, std::memory_order_release);
        _donorActor.store(nullptr, std::memory_order_release);
        _playerActor.store(nullptr, std::memory_order_release);
        _donorKeepAlive.reset();
        _donorHandle.reset();
        _savedDonorFormID = 0;
        _donorVoiceType.store(nullptr, std::memory_order_release);
        _origPlayerVoiceType.store(nullptr, std::memory_order_release);
        _donorName.clear();
        _origPlayerName.clear();

        return 1;
    }

    void ImpersonationManager::SetPuppet(RE::Actor* a_puppet) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!a_puppet) {
            _puppetHandle.reset();
            return;
        }

        if (_puppetHandle == a_puppet->GetHandle()) {
            return;
        }

        _puppetHandle = a_puppet->GetHandle();

        // Ensure puppet actor base never has faceNPC template attached
        if (auto* base = skyrim_cast<RE::TESNPC*>(a_puppet->GetActorBase())) {
            base->faceNPC = nullptr;
        }

        // Ensure saved appearance exists before configuring puppet on initial possession
        if (!FaceManager::GetSingleton().HasSavedAppearance()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                FaceSnapshot origPlayerSnapshot;
                if (FaceManager::GetSingleton().CaptureSnapshot(player, origPlayerSnapshot)) {
                    origPlayerSnapshot.faceNPC = nullptr;
                    FaceManager::GetSingleton().SetSavedAppearance(origPlayerSnapshot);
                    SKSE::log::info("SetPuppet: captured original player appearance for puppet restoration.");
                }
            }
        }

        // Restore original Dragonborn appearance onto puppet if saved appearance exists
        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            FaceManager::GetSingleton().RestoreOriginalPuppetAppearance(a_puppet);
        }

        // Apply original player name to puppet display
        if (_origPlayerName.empty()) {
            if (FaceManager::GetSingleton().HasSavedAppearance() &&
                !FaceManager::GetSingleton().GetSavedAppearance().fullName.empty()) {
                _origPlayerName = FaceManager::GetSingleton().GetSavedAppearance().fullName.c_str();
            }
        }
        if (!_origPlayerName.empty()) {
            if (auto* base = a_puppet->GetActorBase()) {
                base->fullName = _origPlayerName.c_str();
            }
            auto* textData = a_puppet->extraList.GetByType<RE::ExtraTextDisplayData>();
            if (!textData) {
                textData = new RE::ExtraTextDisplayData(_origPlayerName.c_str());
                a_puppet->extraList.Add(textData);
            } else {
                textData->SetName(_origPlayerName.c_str());
            }
        }

        // 1. Set PlayerTeammate flag if puppet follows player
        if (_options.puppetFollowsPlayer) {
            a_puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
        } else {
            a_puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
        }

        // 2. Clear combat, allow dialogue, and evaluate package immediately
        a_puppet->AllowPCDialogue(true);
        a_puppet->AllowBleedoutDialogue(true);
        a_puppet->StopCombat();
        a_puppet->EvaluatePackage(true, true);

        SKSE::log::info("Configured puppet {:08X} (followsPlayer={}).",
            static_cast<std::uint32_t>(a_puppet->GetFormID()), _options.puppetFollowsPlayer);
    }

    void ImpersonationManager::SetPuppetFollowsPlayer(bool a_follows) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _options.puppetFollowsPlayer = a_follows;
        if (auto* puppet = GetPuppet()) {
            if (a_follows) {
                puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
                if (_active.load(std::memory_order_acquire)) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (player) {
                        if (puppet->GetParentCell() != player->GetParentCell() || puppet->GetPosition().GetDistance(player->GetPosition()) > 1000.0f) {
                            puppet->MoveTo(player);
                            SKSE::log::info("SetPuppetFollowsPlayer: Teleported puppet to player.");
                        }
                    }
                }
            } else {
                puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
            }
            puppet->EvaluatePackage(true, true);
            SKSE::log::info("Updated puppet follows player: {}", a_follows);
        }
    }


    void ImpersonationManager::Tick() {
        if (_playerSpeaking.load(std::memory_order_acquire)) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = ui && ui->IsMenuOpen("Dialogue Menu"sv);
            if (player && !menuOpen) {
                _origCharUpdateInDialogue(player, nullptr, false);
                AttachSoundHandlesToHead(player);
            }
            CheckPlayerSpeakingDone();
        }

        if (FaceManager::GetSingleton().NeedsPuppetPost3DUpdate()) {
            if (auto* puppet = GetPuppet()) {
                FaceManager::GetSingleton().ApplyPuppetPost3DLighting(puppet);
            }
        }

        // Forward any latent faction changes from puppet to donor (e.g. from End Papyrus fragments)
        if (_active.load(std::memory_order_acquire)) {
            if (auto* puppet = GetPuppet()) {
                if (auto* donor = GetDonor()) {
                    auto* extraFaction = puppet->extraList.GetByType<RE::ExtraFactionChanges>();
                    if (extraFaction && !extraFaction->factionChanges.empty()) {
                        for (const auto& fRank : extraFaction->factionChanges) {
                            if (fRank.faction) {
                                donor->AddToFaction(fRank.faction, fRank.rank);
                                SKSE::log::info("Tick: Forwarded latent faction {:08X} rank {} from puppet to donor.",
                                    fRank.faction->GetFormID(), fRank.rank);
                                if (fRank.rank >= 0) {
                                    auto fid = fRank.faction->GetFormID();
                                    if (fid == 0x0005C84E || fid == 0x000BD738) {
                                        donor->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
                                        SKSE::log::info("Tick: Set kPlayerTeammate on donor {:08X} due to faction {:08X}",
                                            donor->GetFormID(), fid);
                                    }
                                }
                            }
                        }
                        extraFaction->factionChanges.clear();
                    }
                }
            }
        }

        // Donor holding-cell leash
        if (_active.load(std::memory_order_acquire)) {
            auto* donor = GetDonor();
            auto markerHandle = _cloneMarker;
            auto marker = markerHandle.get();
            if (donor && marker) {
                bool needsLeash = false;
                if (donor->GetParentCell() != marker->GetParentCell()) {
                    needsLeash = true;
                } else if (donor->GetPosition().GetDistance(marker->GetPosition()) > 500.0f) {
                    needsLeash = true;
                }

                if (needsLeash) {
                    donor->MoveTo(marker.get());
                    SKSE::log::info("Tick: Donor {:08X} moved away from holding marker; leashed back.",
                        donor->GetFormID());
                    if (IdleAIManager::GetSingleton().IsAIActive()) {
                        SyncDonorUpdatesToPlayer();
                    }
                }
            }
        }

        // Check if player has an active dialogue/force-greet package targeting puppet
        if (IdleAIManager::GetSingleton().IsAIActive()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* puppet = GetPuppet();
            if (player && puppet) {
                auto* curPkg = player->GetCurrentPackage();
                if (curPkg && curPkg->packTarg) {
                    if (curPkg->packTarg->target.handle == puppet->GetHandle() || curPkg->packTarg->target.object == puppet) {
                        float dist = player->GetDistance(puppet, false, false);
                        if (dist <= 250.0f) {
                            SKSE::log::info("Player force-greet package reached puppet target (dist: {:.1f}) - opening self-dialogue.", dist);
                            IdleAIManager::GetSingleton().OpenSelfDialogue(puppet);
                        }
                    }
                }
            }
        }
    }

    void ImpersonationManager::ClearPlayerPackages() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (const auto& entry : _retargetedPackages) {
            RE::TESPackage* pkg = entry.package;
            if (!pkg && entry.packageFormID != 0) {
                pkg = RE::TESForm::LookupByID<RE::TESPackage>(entry.packageFormID);
            }
            if (pkg && pkg->packTarg) {
                if (entry.targType == 0) {
                    pkg->packTarg->target.handle = entry.origHandle;
                } else if (entry.targType == 1) {
                    pkg->packTarg->target.object = entry.origObject;
                }
                SKSE::log::info("ClearPlayerPackages: Restored original target on package {:08X}", pkg->GetFormID());
            }
        }
        _retargetedPackages.clear();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
        if (playerBase) {
            playerBase->aiPackages.packages.clear();
        }
    }

    // --- Co-Save Serialization ---
    void ImpersonationManager::Save(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        bool active = _active.load(std::memory_order_acquire);
        RE::FormID donorID = _savedDonorFormID;
        if (active && donorID == 0 && _donorActor.load()) {
            donorID = _donorActor.load()->GetFormID();
        }

        RE::FormID puppetID = 0;
        if (auto* puppet = GetPuppet()) {
            puppetID = puppet->GetFormID();
        }

        if (a_serde->OpenRecord(Serialization::kImpersonationDataRecord, Serialization::kSerializationVersion)) {
            a_serde->WriteRecordData(&active, sizeof(active));
            a_serde->WriteRecordData(&donorID, sizeof(donorID));
            a_serde->WriteRecordData(&puppetID, sizeof(puppetID));
            a_serde->WriteRecordData(&_options.npcVoice, sizeof(_options.npcVoice));
            a_serde->WriteRecordData(&_options.puppetFollowsPlayer, sizeof(_options.puppetFollowsPlayer));

            std::uint32_t origLen = static_cast<std::uint32_t>(_origPlayerName.size());
            a_serde->WriteRecordData(&origLen, sizeof(origLen));
            if (origLen > 0) {
                a_serde->WriteRecordData(_origPlayerName.data(), origLen);
            }

            std::uint32_t donorLen = static_cast<std::uint32_t>(_donorName.size());
            a_serde->WriteRecordData(&donorLen, sizeof(donorLen));
            if (donorLen > 0) {
                a_serde->WriteRecordData(_donorName.data(), donorLen);
            }
        }
    }

    void ImpersonationManager::Load(SKSE::SerializationInterface* a_serde, std::uint32_t a_length) {
        if (!a_serde) return;
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        bool active = false;
        RE::FormID donorID = 0;
        RE::FormID puppetID = 0;

        constexpr std::size_t baseSize = sizeof(active) + sizeof(donorID) + sizeof(puppetID) + sizeof(_options.npcVoice) + sizeof(_options.puppetFollowsPlayer);
        if (a_length < baseSize) {
            return;
        }

        a_serde->ReadRecordData(&active, sizeof(active));
        a_serde->ReadRecordData(&donorID, sizeof(donorID));
        a_serde->ReadRecordData(&puppetID, sizeof(puppetID));
        a_serde->ReadRecordData(&_options.npcVoice, sizeof(_options.npcVoice));
        a_serde->ReadRecordData(&_options.puppetFollowsPlayer, sizeof(_options.puppetFollowsPlayer));
        _npcVoiceEnabled.store(_options.npcVoice, std::memory_order_release);

        std::size_t bytesRead = baseSize;
        if (a_length >= bytesRead + sizeof(std::uint32_t)) {
            std::uint32_t origLen = 0;
            a_serde->ReadRecordData(&origLen, sizeof(origLen));
            bytesRead += sizeof(origLen);
            if (origLen > 0 && a_length >= bytesRead + origLen) {
                std::string s(origLen, '\0');
                a_serde->ReadRecordData(s.data(), origLen);
                _origPlayerName = s;
                bytesRead += origLen;
            }
            if (a_length >= bytesRead + sizeof(std::uint32_t)) {
                std::uint32_t donorLen = 0;
                a_serde->ReadRecordData(&donorLen, sizeof(donorLen));
                bytesRead += sizeof(donorLen);
                if (donorLen > 0 && a_length >= bytesRead + donorLen) {
                    std::string s(donorLen, '\0');
                    a_serde->ReadRecordData(s.data(), donorLen);
                    _donorName = s;
                    bytesRead += donorLen;
                }
            }
        }

        if (a_serde->ResolveFormID(donorID, donorID)) {
            _savedDonorFormID = donorID;
        }

        if (puppetID != 0) {
            if (a_serde->ResolveFormID(puppetID, puppetID)) {
                if (auto* puppet = RE::TESForm::LookupByID<RE::Actor>(puppetID)) {
                    _puppetHandle = puppet->GetHandle();
                    SKSE::log::info("Restored puppet {:08X} from co-save.", static_cast<std::uint32_t>(puppetID));
                }
            }
        }

        if (active && _savedDonorFormID != 0) {
            auto* donor = RE::TESForm::LookupByID<RE::Actor>(_savedDonorFormID);
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (donor && player) {
                _donorActor.store(donor, std::memory_order_release);
                _playerActor.store(player, std::memory_order_release);
                _donorKeepAlive.reset(donor);
                _donorHandle = donor->GetHandle();
                if (_donorName.empty()) {
                    _donorName = donor->GetName();
                }
                if (_origPlayerName.empty()) {
                    if (FaceManager::GetSingleton().HasSavedAppearance() &&
                        !FaceManager::GetSingleton().GetSavedAppearance().fullName.empty()) {
                        _origPlayerName = FaceManager::GetSingleton().GetSavedAppearance().fullName.c_str();
                    }
                }
                if (FaceManager::GetSingleton().HasSavedAppearance()) {
                    _origPlayerVoiceType.store(FaceManager::GetSingleton().GetSavedAppearance().voiceType, std::memory_order_release);
                }
                if (auto* base = donor->GetActorBase()) {
                    _donorVoiceType.store(GetNPCVoiceType(base), std::memory_order_release);
                } else {
                    _donorVoiceType.store(nullptr, std::memory_order_release);
                }
                _active.store(true, std::memory_order_release);
                SKSE::log::info("Restored impersonation state from co-save: Donor {:08X}", static_cast<std::uint32_t>(_savedDonorFormID));
            }
        }
    }

    void ImpersonationManager::OnRevert() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        ClearPlayerPackages();
        _active.store(false, std::memory_order_release);
        _donorActor.store(nullptr, std::memory_order_release);
        _playerActor.store(nullptr, std::memory_order_release);
        _donorKeepAlive.reset();
        _donorHandle.reset();
        _savedDonorFormID = 0;
        _donorVoiceType.store(nullptr, std::memory_order_release);
        _origPlayerVoiceType.store(nullptr, std::memory_order_release);
        _donorName.clear();
        _origPlayerName.clear();
        SetPuppet(nullptr);
        FaceManager::GetSingleton().ClearDonorAppearance();
    }

    void ImpersonationManager::OnPostLoadGame() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_active.load(std::memory_order_acquire)) {
            if (_origPlayerName.empty()) {
                if (FaceManager::GetSingleton().HasSavedAppearance() &&
                    !FaceManager::GetSingleton().GetSavedAppearance().fullName.empty()) {
                    _origPlayerName = FaceManager::GetSingleton().GetSavedAppearance().fullName.c_str();
                }
            }
            if (FaceManager::GetSingleton().HasSavedAppearance()) {
                _origPlayerVoiceType.store(FaceManager::GetSingleton().GetSavedAppearance().voiceType, std::memory_order_release);
            }

            auto* donor = _donorActor.load(std::memory_order_acquire);
            auto* player = _playerActor.load(std::memory_order_acquire);
            if (!player) {
                player = RE::PlayerCharacter::GetSingleton();
            }

            if (donor) {
                if (auto* base = donor->GetActorBase()) {
                    auto* voiceType = GetNPCVoiceType(base);
                    _donorVoiceType.store(voiceType, std::memory_order_release);
                    SKSE::log::info("OnPostLoadGame: verified donor voice type {:08X}",
                        voiceType ? static_cast<std::uint32_t>(voiceType->GetFormID()) : 0);
                }
            }
            if (donor && player) {
                SKSE::log::info("OnPostLoadGame: Re-applying donor appearance (donor: {:08X}, player: {:08X})",
                    static_cast<std::uint32_t>(donor->GetFormID()),
                    static_cast<std::uint32_t>(player->GetFormID()));

                FaceManager::GetSingleton().RestoreDonorAppearance(donor, player);

                auto* task = SKSE::GetTaskInterface();
                if (task) {
                    auto donorHandle = donor->GetHandle();
                    auto playerHandle = player->GetHandle();
                    task->AddTask([donorHandle, playerHandle]() {
                        auto d = donorHandle.get();
                        auto p = playerHandle.get();
                        if (d && p) {
                            FaceManager::GetSingleton().RestoreDonorAppearance(d.get(), p.get());
                        }
                    });
                }
            }

            if (auto* puppet = GetPuppet()) {
                if (auto* base = skyrim_cast<RE::TESNPC*>(puppet->GetActorBase())) {
                    base->faceNPC = nullptr;
                }
                if (FaceManager::GetSingleton().HasSavedAppearance()) {
                    FaceManager::GetSingleton().RestoreOriginalPuppetAppearance(puppet);
                }
            }
        }
    }

}
