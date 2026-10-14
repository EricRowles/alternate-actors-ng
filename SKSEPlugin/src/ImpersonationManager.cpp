#include "PCH.h"
#include "ImpersonationManager.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "Serialization.h"
#include "IdleAIManager.h"

#include <unordered_set>
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

namespace AlternateActors {

    static std::array<RE::SCRIPT_FUNCTION::Condition_t*, 0x300> s_originalConditions{ nullptr };

    // --- Category A: Identity Thunk ---
    template <std::uint16_t ID>
    static bool IdentityThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        auto* orig = s_originalConditions[ID];
        if (!mgr.IsConditionsEnabled()) {
            return orig ? orig(a_thisObj, a_p1, a_p2, a_result) : false;
        }

        auto* player = mgr.GetPlayer();
        auto* donor = mgr.GetDonor();
        auto* puppet = mgr.GetPuppet();

        // 1. Run original condition check first
        double res = 0.0;
        bool ok = orig ? orig(a_thisObj, a_p1, a_p2, res) : false;
        if (ok && res != 0.0) {
            a_result = res;
            return true;
        }

        // 2. If checking on player OR (puppet during self-dialogue), try substituting donor
        bool isSelfDialogue = IdleAIManager::GetSingleton().IsInSelfDialogue();
        if (a_thisObj && (a_thisObj == player || (puppet && a_thisObj == puppet && isSelfDialogue))) {
            bool isPuppetSelf = (puppet && a_thisObj == puppet && isSelfDialogue);
            if constexpr (ID == 426) {
                auto* targetVoice = static_cast<RE::TESForm*>(a_p1);
                if (targetVoice && mgr.GetDonorVoiceType() && targetVoice == mgr.GetDonorVoiceType()) {
                    if (isPuppetSelf || mgr.IsNPCVoiceEnabled()) {
                        a_result = 1.0;
                        return true;
                    }
                }
            }


            if (donor) {
                double donorRes = 0.0;
                if (orig && orig(donor, a_p1, a_p2, donorRes) && donorRes != 0.0) {
                    a_result = donorRes;
                    return true;
                }

                // Fallback for leveled donors in GetIsID (72)
                if constexpr (ID == 72) {
                    auto* targetForm = static_cast<RE::TESForm*>(a_p1);
                    if (targetForm) {
                        if (targetForm == donor->GetBaseObject()) {
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
                    }
                }
            }
        }

        // 3. Special case for GetIsAliasRef (566): if checking on donor or puppet, retry with player
        if constexpr (ID == 566) {
            if (a_thisObj && (a_thisObj == donor || (puppet && a_thisObj == puppet && isSelfDialogue)) && player) {
                double playerRes = 0.0;
                if (orig && orig(player, a_p1, a_p2, playerRes) && playerRes != 0.0) {
                    a_result = playerRes;
                    return true;
                }
            }
        }

        a_result = res;
        return ok;
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
            if (mgr.IsActive() && mgr._donorVoiceType) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player && a_this == player->GetActorBase()) {
                    if (mgr.IsNPCVoiceEnabled()) {
                        return mgr._donorVoiceType;
                    }
                    return _origGetObjectVoiceType(a_this);
                }

                auto* puppet = mgr.GetPuppet();
                if (puppet && a_this == puppet->GetActorBase()) {
                    if (IdleAIManager::GetSingleton().IsInSelfDialogue()) {
                        return mgr._donorVoiceType;
                    }
                    return nullptr;
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
        _playerSpeaking.store(true, std::memory_order_relaxed);
        _speechStartTime = std::chrono::steady_clock::now();
        _speechAudioEndTime = std::chrono::steady_clock::time_point{};
        _speechHadSound.store(false, std::memory_order_relaxed);
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

        _playerSpeaking.store(false, std::memory_order_relaxed);
        _speechHadSound.store(false, std::memory_order_relaxed);
        _speechAudioEndTime = std::chrono::steady_clock::time_point{};

        if (a_player) {
            a_player->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
            _origPlayerSetSpeakingDone(a_player, true);
            if (auto* faceData = a_player->GetFaceGenAnimationData()) {
                RE::BSSpinLockGuard locker(faceData->lock);
                faceData->Reset(0.0f, false, true, false, false);
                faceData->ClearExpressionOverride();
            }
            a_player->SetEmotionType(RE::EmotionType::kNeutral);
            a_player->SetEmotionValue(0);
        }

        auto* puppet = GetPuppet();
        if (puppet) {
            puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kSpeakingDone);
            puppet->SetEmotionType(RE::EmotionType::kNeutral);
            puppet->SetEmotionValue(0);
            if (auto* spkFace = puppet->GetFaceGenAnimationData()) {
                RE::BSSpinLockGuard locker(spkFace->lock);
                spkFace->Reset(0.0f, false, true, false, false);
                spkFace->ClearExpressionOverride();
            }
        }

        // Advance MenuTopicManager
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr) {
            SKSE::log::info("CompleteSpeaking: signaling MenuTopicManager to advance (waitingToAdvance: {})",
                topicMgr->waitingToAdvance);
            topicMgr->canSkip = true;
            topicMgr->advanceRequested = true;
            if (puppet) {
                topicMgr->lastSpeaker = puppet->GetHandle();
            }
        }

        IdleAIManager::GetSingleton().ClearCurrentDialogueItem();

        _inCompleteSpeaking.store(false);
    }

    void ImpersonationManager::ResetSpeechTracking() {
        auto* player = GetPlayer();
        CompleteSpeaking(player ? player : RE::PlayerCharacter::GetSingleton());
    }

    bool ImpersonationManager::CheckPlayerSpeakingDone() {
        if (!_playerSpeaking.load(std::memory_order_relaxed)) {
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
            if (!_speechHadSound.load(std::memory_order_relaxed)) {
                _speechStartTime = now;
                elapsed = 0.0f;
            }
            _speechHadSound.store(true, std::memory_order_relaxed);

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

        if (_speechHadSound.load(std::memory_order_relaxed)) {
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
        if (!_active.load(std::memory_order_relaxed)) return;
        auto* donor = GetDonor();
        auto* player = GetPlayer();
        if (!donor || !player) return;

        // 1. Sync any updated aliases
        AliasManager::GetSingleton().SwapAliasesToPlayer(donor, player);

        // 2. Sync AI packages from donor to player base
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
            bool inSelf = IdleAIManager::GetSingleton().IsInSelfDialogue();
            auto* ui = RE::UI::GetSingleton();
            bool menuOpen = ui && ui->IsMenuOpen("Dialogue Menu"sv);
            auto* donor = GetDonor();
            bool isDonor = (donor && a_speaker == donor);
            auto* puppet = GetPuppet();
            bool isPuppet = (puppet && a_speaker == puppet);

            auto* topicMgr = RE::MenuTopicManager::GetSingleton();
            bool inTopicWithSpeaker = false;
            if (topicMgr && (isPuppet || isDonor)) {
                auto spkHandle = a_speaker->GetHandle();
                if (topicMgr->speaker == spkHandle || topicMgr->lastSpeaker == spkHandle) {
                    inTopicWithSpeaker = true;
                }
            }

            // Suppress automatic puppet NPC goodbye farewell blurbs (subtype kGoodBye = 78)
            bool isNpcGoodbye = (topicMgr && topicMgr->currentTopicInfo && topicMgr->currentTopicInfo->parentTopic &&
                                 topicMgr->currentTopicInfo->parentTopic->data.subtype.get() == RE::DIALOGUE_DATA::Subtype::kGoodBye);
            if (isPuppet && isNpcGoodbye) {
                SKSE::log::info("Suppressing puppet NPC goodbye farewell line: '{}'", a_response->text.c_str());
                return false;
            }

            if (inSelf || menuOpen || inTopicWithSpeaker || isDonor || isPuppet) {
                SKSE::log::info("Decoupled proxy dialogue: text='{}', voice='{}', sound={:08X}",
                    a_response->text.c_str(),
                    a_response->voice.c_str(),
                    a_response->voiceSound ? static_cast<std::uint32_t>(a_response->voiceSound->GetFormID()) : 0);

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

                // 2. Prepare puppet response clone: has text for subtitles/topic advance,
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

                // 3. Start speech tracking for player duration / audio completion
                OnDialogueResponseStarted(&playerResp);

                // If there is a next response in this topic, advance currentResponse and inform MenuTopicManager
                if (topicMgr && topicMgr->lastSelectedDialogue && topicMgr->lastSelectedDialogue->currentResponse) {
                    if (topicMgr->lastSelectedDialogue->currentResponse->next != nullptr) {
                        topicMgr->waitingToAdvance = true;
                        topicMgr->lastSelectedDialogue->currentResponse = topicMgr->lastSelectedDialogue->currentResponse->next;
                        SKSE::log::info("Multi-line topic detected - advanced currentResponse to next line and set waitingToAdvance = true");
                    } else {
                        topicMgr->waitingToAdvance = false;
                        topicMgr->lastSelectedDialogue->currentResponse = nullptr;
                    }
                }

                // 4. Dispatch sensory playback directly onto PlayerCharacter natively!
                if (playerResp.useEmotion) {
                    player->SetEmotionType(playerResp.animFaceArchType.get());
                    player->SetEmotionValue(playerResp.percent);
                }
                _origCharUpdateInDialogue(player, &playerResp, a_unused);
                AttachSoundHandlesToHead(player);

                // 5. Dispatch silent logical response to puppet (updates MenuTopicManager and subtitle display)
                bool ret = _origCharUpdateInDialogue(a_speaker, &puppetResp, a_unused);

                // 6. Ensure puppet face and emotion remain strictly neutral
                if (auto* spkFace = a_speaker->GetFaceGenAnimationData()) {
                    RE::BSSpinLockGuard locker(spkFace->lock);
                    spkFace->ClearExpressionOverride();
                    spkFace->Reset(0.0f, true, false, false, false);
                }
                a_speaker->SetEmotionType(RE::EmotionType::kNeutral);
                a_speaker->SetEmotionValue(0);

                // 7. Sync donor updates to player
                SyncDonorUpdatesToPlayer();

                return ret;
            } else {
                // Suppress proxy actor spontaneous idle speech outside self-dialogue
                SKSE::log::info("Suppressing proxy actor spontaneous idle speech: '{}'", a_response->text.c_str());
                return false;
            }
        } else {
            // Ticks with a_response == nullptr from MenuTopicManager during dialogue:
            // 1. Keep player's dialogue state and audio emitter updated
            if (_playerSpeaking.load(std::memory_order_relaxed)) {
                _origCharUpdateInDialogue(player, nullptr, a_unused);
                AttachSoundHandlesToHead(player);
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
            if (puppet && targetRef == puppet) {
                if (!AliasManager::IsModInternalQuest(quest)) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* donor = mgr.GetDonor();
                    if (player && donor) {
                        SKSE::log::info("Hook_ForceRefIntoAlias: Intercepted external quest {:08X} assigning alias ID {} to puppet {:08X}. Redirecting to player {:08X} (donor {:08X}).",
                            static_cast<std::uint32_t>(quest->GetFormID()),
                            aliasID,
                            static_cast<std::uint32_t>(puppet->GetFormID()),
                            static_cast<std::uint32_t>(player->GetFormID()),
                            static_cast<std::uint32_t>(donor->GetFormID()));

                        if (!AliasManager::GetSingleton().HasRecord(quest->GetFormID(), aliasID)) {
                            SwappedAliasRecord record;
                            record.questFormID = quest->GetFormID();
                            record.aliasID = aliasID;
                            record.originalActorFormID = donor->GetFormID();
                            AliasManager::GetSingleton().AddRecord(record);
                        }

                        a_ctx.R8 = reinterpret_cast<DWORD64>(player);
                    }
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

        auto* commands = RE::SCRIPT_FUNCTION::GetFirstScriptCommand();
        if (!commands) {
            SKSE::log::error("SCRIPT_FUNCTION::GetFirstScriptCommand returned null!");
            return;
        }

        auto hook = [&](std::uint16_t id, RE::SCRIPT_FUNCTION::Condition_t* newFunc) {
            if (id >= s_originalConditions.size()) return;
            if (!commands[id].conditionFunction) {
                SKSE::log::warn("Script command ID {} has no conditionFunction, skipping hook.", id);
                return;
            }
            s_originalConditions[id] = commands[id].conditionFunction;
            REL::safe_write(reinterpret_cast<std::uintptr_t>(&commands[id].conditionFunction), reinterpret_cast<std::uintptr_t>(newFunc));
        };

        // Category A - Identity Conditions
        hook(72, IdentityThunk<72>);    // GetIsID
        hook(73, IdentityThunk<73>);    // GetInFaction
        hook(136, IdentityThunk<136>);  // GetIsReference
        hook(372, IdentityThunk<372>);  // IsInList
        hook(426, IdentityThunk<426>);  // GetIsVoiceType
        hook(566, IdentityThunk<566>);  // GetIsAliasRef

        _hooksInstalled = true;
        SKSE::log::info("Installed 6 Impersonation identity condition function hooks and dialogue vfunc hooks successfully.");
    }

    void ImpersonationManager::Initialize() {
        InstallHooks();
    }

    std::int32_t ImpersonationManager::Begin(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) {
            SKSE::log::warn("ImpersonationManager::Begin called with null actor!");
            return 0;
        }

        if (_active.load(std::memory_order_relaxed)) {
            End();
        }

        _donorActor.store(a_donor, std::memory_order_relaxed);
        _playerActor.store(a_player, std::memory_order_relaxed);
        _donorKeepAlive.reset(a_donor);
        _donorHandle = a_donor->GetHandle();
        _savedDonorFormID = a_donor->GetFormID();
        _donorName = a_donor->GetName();
        if (FaceManager::GetSingleton().HasSavedAppearance() &&
            !FaceManager::GetSingleton().GetSavedAppearance().fullName.empty()) {
            _origPlayerName = FaceManager::GetSingleton().GetSavedAppearance().fullName.c_str();
        } else {
            _origPlayerName = a_player->GetName();
        }
        if (auto* base = a_donor->GetActorBase()) {
            _donorVoiceType = GetNPCVoiceType(base);
        } else {
            _donorVoiceType = nullptr;
        }
        _active.store(true, std::memory_order_release);

        SKSE::log::info("Beginning impersonation: Donor {:08X} ({}) -> Player {:08X}",
            static_cast<std::uint32_t>(a_donor->GetFormID()),
            a_donor->GetName(),
            static_cast<std::uint32_t>(a_player->GetFormID())
        );

        // Layer 2: Quest Aliases
        AliasManager::GetSingleton().SwapAliasesToPlayer(a_donor, a_player);

        // Layer 3: AI Packages
        RetargetPackages(a_donor, a_player);

        return 1;
    }

    std::int32_t ImpersonationManager::End() {
        if (!_active.load(std::memory_order_relaxed)) {
            return 0;
        }

        auto* donor = _donorActor.load(std::memory_order_relaxed);
        auto* player = _playerActor.load(std::memory_order_relaxed);

        SKSE::log::info("Ending impersonation of donor {:08X}",
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : static_cast<std::uint32_t>(_savedDonorFormID)
        );

        // Layer 3: AI Packages
        RestorePackages();

        // Layer 2: Quest Aliases
        AliasManager::GetSingleton().RestoreAliases();


        _active.store(false, std::memory_order_release);
        _donorActor.store(nullptr, std::memory_order_relaxed);
        _playerActor.store(nullptr, std::memory_order_relaxed);
        _donorKeepAlive.reset();
        _donorHandle.reset();
        _savedDonorFormID = 0;
        _donorVoiceType = nullptr;
        _donorName.clear();
        _origPlayerName.clear();
        SetPuppet(nullptr);

        return 1;
    }

    void ImpersonationManager::SetPuppet(RE::Actor* a_puppet) {
        if (!a_puppet) {
            _puppetHandle.reset();
            return;
        }

        _puppetHandle = a_puppet->GetHandle();

        // Ensure puppet actor base never has faceNPC template attached
        if (auto* base = skyrim_cast<RE::TESNPC*>(a_puppet->GetActorBase())) {
            base->faceNPC = nullptr;
        }

        // Restore original Dragonborn appearance onto puppet if saved appearance exists
        if (FaceManager::GetSingleton().HasSavedAppearance()) {
            FaceManager::GetSingleton().RestoreOriginalPuppetAppearance(a_puppet);
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
        _options.puppetFollowsPlayer = a_follows;
        if (auto* puppet = GetPuppet()) {
            if (a_follows) {
                puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);
            } else {
                puppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
            }
            puppet->EvaluatePackage(true, true);
            SKSE::log::info("Updated puppet follows player: {}", a_follows);
        }
    }


    void ImpersonationManager::Tick() {
        if (_playerSpeaking.load(std::memory_order_relaxed)) {
            CheckPlayerSpeakingDone();
        }
    }

    // --- Layer 3: AI Packages Retargeting ---
    void ImpersonationManager::CheckAndRetargetLocation(
        RE::TESPackage* a_pkg,
        RE::PackageLocation* a_loc,
        RE::Actor* a_donor,
        RE::Actor* a_player,
        PackageRetargetType a_handleType,
        PackageRetargetType a_objType)
    {
        if (!a_pkg || !a_loc || !a_donor || !a_player) return;

        auto donorHandle = a_donor->GetHandle();
        auto playerHandle = a_player->GetHandle();
        auto* donorBase = a_donor->GetBaseObject();
        auto* donorActorBase = a_donor->GetActorBase();
        auto* playerBase = a_player->GetBaseObject();

        if (a_loc->locType == RE::PackageLocation::Type::kNearReference) {
            if (a_loc->data.refHandle == donorHandle) {
                PackageRetargetRecord rec;
                rec.package = a_pkg;
                rec.type = a_handleType;
                rec.targetPtr = a_loc;
                rec.origHandle = a_loc->data.refHandle;
                rec.origObject = a_loc->data.object;
                _retargetedPackages.push_back(rec);

                a_loc->data.refHandle = playerHandle;
            }
        } else if (a_loc->locType == RE::PackageLocation::Type::kObjectID) {
            if (a_loc->data.object == donorBase || a_loc->data.object == donorActorBase || a_loc->data.object == a_donor) {
                PackageRetargetRecord rec;
                rec.package = a_pkg;
                rec.type = a_objType;
                rec.targetPtr = a_loc;
                rec.origHandle = a_loc->data.refHandle;
                rec.origObject = a_loc->data.object;
                _retargetedPackages.push_back(rec);

                a_loc->data.object = playerBase;
            }
        }
    }

    void ImpersonationManager::CheckAndRetargetTarget(
        RE::TESPackage* a_pkg,
        RE::PackageTarget* a_targ,
        RE::Actor* a_donor,
        RE::Actor* a_player,
        PackageRetargetType a_handleType,
        PackageRetargetType a_objType)
    {
        if (!a_pkg || !a_targ || !a_donor || !a_player) return;

        auto donorHandle = a_donor->GetHandle();
        auto playerHandle = a_player->GetHandle();
        auto* donorBase = a_donor->GetBaseObject();
        auto* donorActorBase = a_donor->GetActorBase();
        auto* playerBase = a_player->GetBaseObject();

        if (a_targ->targType == 0) { // Specific Reference
            if (a_targ->target.handle == donorHandle || a_targ->target.object == a_donor) {
                PackageRetargetRecord rec;
                rec.package = a_pkg;
                rec.type = a_handleType;
                rec.targetPtr = a_targ;
                rec.origHandle = a_targ->target.handle;
                rec.origObject = a_targ->target.object;
                _retargetedPackages.push_back(rec);

                a_targ->target.handle = playerHandle;
            }
        } else if (a_targ->targType == 1) { // ObjectID
            if (a_targ->target.object == donorBase || a_targ->target.object == donorActorBase || a_targ->target.object == a_donor) {
                PackageRetargetRecord rec;
                rec.package = a_pkg;
                rec.type = a_objType;
                rec.targetPtr = a_targ;
                rec.origHandle = a_targ->target.handle;
                rec.origObject = a_targ->target.object;
                _retargetedPackages.push_back(rec);

                a_targ->target.object = playerBase;
            }
        }
    }

    void ImpersonationManager::RetargetPackages(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) return;

        RestorePackages();

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        const auto& packages = dataHandler->GetFormArray<RE::TESPackage>();
        for (auto* pkg : packages) {
            if (!pkg) continue;

            if (pkg->packLoc) {
                CheckAndRetargetLocation(pkg, pkg->packLoc, a_donor, a_player, PackageRetargetType::kPackLocHandle, PackageRetargetType::kPackLocObject);
            }
            if (pkg->packTarg) {
                CheckAndRetargetTarget(pkg, pkg->packTarg, a_donor, a_player, PackageRetargetType::kPackTargHandle, PackageRetargetType::kPackTargObject);
            }

            if (pkg->data) {
                auto* customData = skyrim_cast<RE::TESCustomPackageData*>(pkg->data);
                if (customData && customData->data.data) {
                    for (std::uint16_t i = 0; i < customData->data.dataSize; ++i) {
                        auto* entry = customData->data.data[i];
                        if (!entry) continue;

                        auto vtbl = *reinterpret_cast<std::uintptr_t*>(entry);
                        if (vtbl == RE::VTABLE_BGSPackageDataLocation[0].address()) {
                            auto* locData = static_cast<RE::BGSPackageDataLocation*>(entry);
                            if (locData && locData->pointer) {
                                CheckAndRetargetLocation(pkg, locData->pointer, a_donor, a_player, PackageRetargetType::kCustomLocHandle, PackageRetargetType::kCustomLocObject);
                            }
                        }
                    }
                }
            }
        }

        SKSE::log::info("Retargeted {} AI package locations/targets to player.", _retargetedPackages.size());
    }

    void ImpersonationManager::RestorePackages() {
        if (_retargetedPackages.empty()) return;

        for (const auto& rec : _retargetedPackages) {
            if (!rec.targetPtr) continue;

            switch (rec.type) {
                case PackageRetargetType::kPackLocHandle:
                case PackageRetargetType::kCustomLocHandle: {
                    auto* loc = static_cast<RE::PackageLocation*>(rec.targetPtr);
                    loc->data.refHandle = rec.origHandle;
                    loc->data.object = rec.origObject;
                    break;
                }
                case PackageRetargetType::kPackLocObject:
                case PackageRetargetType::kCustomLocObject: {
                    auto* loc = static_cast<RE::PackageLocation*>(rec.targetPtr);
                    loc->data.object = rec.origObject;
                    loc->data.refHandle = rec.origHandle;
                    break;
                }
                case PackageRetargetType::kPackTargHandle:
                case PackageRetargetType::kCustomTargHandle: {
                    auto* targ = static_cast<RE::PackageTarget*>(rec.targetPtr);
                    targ->target.handle = rec.origHandle;
                    targ->target.object = rec.origObject;
                    break;
                }
                case PackageRetargetType::kPackTargObject:
                case PackageRetargetType::kCustomTargObject: {
                    auto* targ = static_cast<RE::PackageTarget*>(rec.targetPtr);
                    targ->target.object = rec.origObject;
                    targ->target.handle = rec.origHandle;
                    break;
                }
            }
        }

        SKSE::log::info("Restored {} retargeted AI packages.", _retargetedPackages.size());
        _retargetedPackages.clear();
    }

    // --- Co-Save Serialization ---
    void ImpersonationManager::Save(SKSE::SerializationInterface* a_serde) {
        if (!a_serde) return;

        bool active = _active.load(std::memory_order_relaxed);
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
        }
    }

    void ImpersonationManager::Load(SKSE::SerializationInterface* a_serde, std::uint32_t /*a_version*/, std::uint32_t a_length) {
        if (!a_serde) return;

        bool active = false;
        RE::FormID donorID = 0;
        RE::FormID puppetID = 0;

        if (a_length < (sizeof(active) + sizeof(donorID) + sizeof(puppetID) + sizeof(_options.npcVoice) + sizeof(_options.puppetFollowsPlayer))) {
            return;
        }

        a_serde->ReadRecordData(&active, sizeof(active));
        a_serde->ReadRecordData(&donorID, sizeof(donorID));
        a_serde->ReadRecordData(&puppetID, sizeof(puppetID));
        a_serde->ReadRecordData(&_options.npcVoice, sizeof(_options.npcVoice));
        a_serde->ReadRecordData(&_options.puppetFollowsPlayer, sizeof(_options.puppetFollowsPlayer));

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
                _donorActor.store(donor, std::memory_order_relaxed);
                _playerActor.store(player, std::memory_order_relaxed);
                _donorKeepAlive.reset(donor);
                _donorHandle = donor->GetHandle();
                _donorName = donor->GetName();
                _origPlayerName = player->GetName();
                if (auto* base = donor->GetActorBase()) {
                    _donorVoiceType = GetNPCVoiceType(base);
                } else {
                    _donorVoiceType = nullptr;
                }
                _active.store(true, std::memory_order_release);
                SKSE::log::info("Restored impersonation state from co-save: Donor {:08X}", static_cast<std::uint32_t>(_savedDonorFormID));
            }
        }
    }

    void ImpersonationManager::OnRevert() {
        RestorePackages();
        _active.store(false, std::memory_order_release);
        _donorActor.store(nullptr, std::memory_order_relaxed);
        _playerActor.store(nullptr, std::memory_order_relaxed);
        _donorKeepAlive.reset();
        _donorHandle.reset();
        _savedDonorFormID = 0;
        _donorVoiceType = nullptr;
        _donorName.clear();
        _origPlayerName.clear();
        SetPuppet(nullptr);
        FaceManager::GetSingleton().ClearDonorAppearance();
    }

    void ImpersonationManager::OnPostLoadGame() {
        if (_active.load(std::memory_order_relaxed)) {
            auto* donor = _donorActor.load(std::memory_order_relaxed);
            auto* player = _playerActor.load(std::memory_order_relaxed);
            if (donor && player) {
                RetargetPackages(donor, player);
            }

            if (donor) {
                if (auto* base = donor->GetActorBase()) {
                    _donorVoiceType = GetNPCVoiceType(base);
                    SKSE::log::info("OnPostLoadGame: verified donor voice type {:08X}",
                        _donorVoiceType ? static_cast<std::uint32_t>(_donorVoiceType->GetFormID()) : 0);
                }
            }
            if (donor && player) {
                SKSE::log::info("OnPostLoadGame: Re-applying donor appearance (donor: {:08X}, player: {:08X})",
                    static_cast<std::uint32_t>(donor->GetFormID()),
                    static_cast<std::uint32_t>(player->GetFormID()));

                FaceManager::GetSingleton().RestoreDonorAppearance(donor, player);

                auto* task = SKSE::GetTaskInterface();
                if (task) {
                    task->AddTask([donor, player]() {
                        if (donor && player) {
                            FaceManager::GetSingleton().RestoreDonorAppearance(donor, player);
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
