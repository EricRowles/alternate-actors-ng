#include "PCH.h"
#include "ImpersonationManager.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "Serialization.h"
#include "IdleAIManager.h"

#include <unordered_set>
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
            if constexpr (ID == 426) {
                auto* targetVoice = static_cast<RE::TESForm*>(a_p1);
                if (targetVoice && mgr.GetDonorVoiceType() && targetVoice == mgr.GetDonorVoiceType()) {
                    a_result = 1.0;
                    return true;
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
                    return mgr._donorVoiceType;
                }
                if (a_this->voiceType) {
                    return a_this->voiceType;
                }
            }
        }
        return _origGetObjectVoiceType(a_this);
    }

    void ImpersonationManager::OnDialogueResponseStarted(RE::DialogueResponse* a_response) {
        _playerSpeaking.store(true, std::memory_order_relaxed);
        _speechStartTime = std::chrono::steady_clock::now();
        _speechHadSound.store(false, std::memory_order_relaxed);
        float textLen = 0.0f;
        if (a_response && !a_response->text.empty()) {
            textLen = static_cast<float>(a_response->text.length());
        }
        _speechMinDuration = std::max(2.0f, textLen * 0.065f);
    }

    void ImpersonationManager::ResetSpeechTracking() {
        _playerSpeaking.store(false, std::memory_order_relaxed);
        _speechHadSound.store(false, std::memory_order_relaxed);
        if (auto* player = GetPlayer()) {
            player->SetSpeakingDone(true);
            player->SetEmotionType(RE::EmotionType::kNeutral);
            player->SetEmotionValue(0);
        }
        if (auto* puppet = GetPuppet()) {
            puppet->SetSpeakingDone(true);
            puppet->SetEmotionType(RE::EmotionType::kNeutral);
            puppet->SetEmotionValue(0);
        }
    }

    bool ImpersonationManager::CheckPlayerSpeakingDone() {
        if (!_playerSpeaking.load(std::memory_order_relaxed)) {
            return true;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return true;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _speechStartTime).count();

        // Check if any audio sound handle on player is currently playing
        bool soundPlaying = false;
        auto* process = player->GetActorRuntimeData().currentProcess;
        if (process && process->high) {
            for (auto& soundHandle : process->high->soundHandles) {
                if (soundHandle.IsValid() && soundHandle.IsPlaying()) {
                    soundPlaying = true;
                    if (!_speechHadSound.load(std::memory_order_relaxed)) {
                        _speechStartTime = now;
                        elapsed = 0.0f;
                    }
                    _speechHadSound.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        }

        if (soundPlaying) {
            // Safety timeout of 20s in case sound loops
            if (elapsed < 20.0f) {
                return false;
            }
        }

        if (_speechHadSound.load(std::memory_order_relaxed)) {
            // Audio was playing and has now finished!
            _playerSpeaking.store(false, std::memory_order_relaxed);
            _speechHadSound.store(false, std::memory_order_relaxed);
            player->SetSpeakingDone(true);
            if (auto* puppet = GetPuppet()) {
                puppet->SetSpeakingDone(true);
            }
            SKSE::log::info("Player dialogue speech completed (audio finished after {:.2f}s)", elapsed);
            auto* ui = RE::UI::GetSingleton();
            if (ui && !ui->IsMenuOpen("Dialogue Menu"sv)) {
                IdleAIManager::GetSingleton().SetInSelfDialogue(false);
            }
            return true;
        }

        // If no sound was played (e.g. unvoiced line / text-only dialogue)
        if (elapsed < _speechMinDuration) {
            return false;
        }

        _playerSpeaking.store(false, std::memory_order_relaxed);
        _speechHadSound.store(false, std::memory_order_relaxed);
        player->SetSpeakingDone(true);
        if (auto* puppet = GetPuppet()) {
            puppet->SetSpeakingDone(true);
        }
        SKSE::log::info("Player dialogue speech completed (timer elapsed after {:.2f}s)", elapsed);
        auto* ui = RE::UI::GetSingleton();
        if (ui && !ui->IsMenuOpen("Dialogue Menu"sv)) {
            IdleAIManager::GetSingleton().SetInSelfDialogue(false);
        }
        return true;
    }

    void ImpersonationManager::SyncDonorUpdatesToPlayer() {
        if (!_active.load(std::memory_order_relaxed)) return;
        auto* donor = GetDonor();
        auto* player = GetPlayer();
        if (!donor || !player) return;

        // 1. Sync any updated aliases
        if (_options.aliases) {
            AliasManager::GetSingleton().SwapAliasesToPlayer(donor, player);
        }

        // 2. Sync AI packages from donor to player base
        if (_options.packages) {
            auto* donorBase = donor->GetActorBase();
            auto* playerBase = skyrim_cast<RE::TESNPC*>(player->GetActorBase());
            if (donorBase && playerBase) {
                std::vector<RE::TESPackage*> donorPkgs;
                for (auto* pkg : donorBase->aiPackages.packages) {
                    if (pkg) {
                        donorPkgs.push_back(pkg);
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
    }

    bool ImpersonationManager::Hook_Character_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (puppet && player && a_this == puppet) {
                    if (a_response != nullptr) {
                        bool inSelf = IdleAIManager::GetSingleton().IsInSelfDialogue();
                        auto* ui = RE::UI::GetSingleton();
                        bool menuOpen = ui && ui->IsMenuOpen("Dialogue Menu"sv);
                        if (inSelf || menuOpen) {
                            SKSE::log::info("Redirecting puppet dialogue to player: text='{}', voice='{}', sound={:08X}",
                                a_response->text.c_str(),
                                a_response->voice.c_str(),
                                a_response->voiceSound ? static_cast<std::uint32_t>(a_response->voiceSound->GetFormID()) : 0);

                            // Save original response fields
                            auto origVoice = a_response->voice;
                            auto* origVoiceSound = a_response->voiceSound;
                            bool origSoundLip = a_response->soundLip;
                            bool origUseEmotion = a_response->useEmotion;
                            auto* origSpeakerIdle = a_response->speakerIdle;

                            // Temporarily strip from response so puppet does NOT play voice, lip, emotion, or idle
                            a_response->voice = ""sv;
                            a_response->voiceSound = nullptr;
                            a_response->soundLip = false;
                            a_response->useEmotion = false;
                            a_response->speakerIdle = nullptr;

                            // 1. Advance puppet natively in engine so MenuTopicManager selects topic and runs result scripts
                            bool nativeRet = _origCharUpdateInDialogue(puppet, a_response, a_unused);

                            // Suppress puppet face, mouth, and audio
                            if (auto* faceData = puppet->GetFaceGenAnimationData()) {
                                faceData->Reset(0.0f, true, true, true, false);
                                faceData->ClearExpressionOverride();
                            }
                            puppet->SetEmotionType(RE::EmotionType::kNeutral);
                            puppet->SetEmotionValue(0);

                            auto* puppetProcess = puppet->GetActorRuntimeData().currentProcess;
                            if (puppetProcess && puppetProcess->high) {
                                for (auto& handle : puppetProcess->high->soundHandles) {
                                    if (handle.IsValid()) {
                                        handle.SetVolume(0.0f);
                                        handle.Stop();
                                    }
                                }
                            }

                            // Restore original fields on response for player
                            a_response->voice = origVoice;
                            a_response->voiceSound = origVoiceSound;
                            a_response->soundLip = origSoundLip;
                            a_response->useEmotion = origUseEmotion;
                            a_response->speakerIdle = origSpeakerIdle;

                            // 2. Apply facial emotion and expression directly to player
                            if (a_response->useEmotion) {
                                player->SetEmotionType(a_response->animFaceArchType.get());
                                player->SetEmotionValue(a_response->percent);
                            }

                            // 3. Play line on player and track speech
                            mgr.OnDialogueResponseStarted(a_response);
                            _origPlayerUpdateInDialogue(player, a_response, a_unused);

                            // 4. Sync any donor updates (packages/aliases) to player
                            mgr.SyncDonorUpdatesToPlayer();

                            return nativeRet;
                        } else {
                            // Suppress puppet spontaneous idle speech outside self-dialogue
                            SKSE::log::info("Suppressing puppet spontaneous idle speech: '{}'", a_response->text.c_str());
                            return false;
                        }
                    } else {
                        // Ticks with a_response == nullptr:
                        if (auto* faceData = puppet->GetFaceGenAnimationData()) {
                            faceData->ClearExpressionOverride();
                        }

                        auto* puppetProcess = puppet->GetActorRuntimeData().currentProcess;
                        if (puppetProcess && puppetProcess->high) {
                            for (auto& handle : puppetProcess->high->soundHandles) {
                                if (handle.IsValid()) {
                                    handle.SetVolume(0.0f);
                                    handle.Stop();
                                }
                            }
                        }

                        // Let temp NPC head-track player as listener
                        _origCharUpdateInDialogue(puppet, nullptr, a_unused);

                        // Always tick player so lip-sync phonemes, mouth movement, and speech advance
                        _origPlayerUpdateInDialogue(player, nullptr, a_unused);
                        return true;
                    }
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
                mgr.OnDialogueResponseStarted(a_response);
            }
        }
        return _origPlayerUpdateInDialogue(a_this, a_response, a_unused);
    }

    bool ImpersonationManager::Hook_Character_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                if (puppet && a_this == puppet) {
                    return mgr.CheckPlayerSpeakingDone();
                }
            }
        }
        return _origCharQSpeakingDone(a_this);
    }

    bool ImpersonationManager::Hook_Player_QSpeakingDone(const RE::Actor* a_this) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive() && mgr.IsPlayerSpeaking()) {
                return mgr.CheckPlayerSpeakingDone();
            }
        }
        return _origPlayerQSpeakingDone(a_this);
    }

    void ImpersonationManager::Hook_Character_SetSpeakingDone(RE::Actor* a_this, bool a_set) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                if (puppet && a_this == puppet) {
                    if (a_set) {
                        mgr.ResetSpeechTracking();
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
                    mgr.ResetSpeechTracking();
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
                if (puppet && a_this == puppet) {
                    IdleAIManager::GetSingleton().SetInSelfDialogue(a_flag);
                }
            }
        }
        return _origSetDialogueWithPlayer(a_this, a_flag, a_forceGreet, a_topic);
    }

    void ImpersonationManager::InstallHooks() {
        if (_hooksInstalled) return;

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

        // Hook PlayerCharacter vtable (0x4C UpdateInDialogue, 0x107 QSpeakingDone, 0x108 SetSpeakingDone)
        REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[0] };
        _origPlayerUpdateInDialogue = playerVtbl.write_vfunc(0x4C, &Hook_Player_UpdateInDialogue);
        _origPlayerQSpeakingDone = playerVtbl.write_vfunc(0x107, &Hook_Player_QSpeakingDone);
        _origPlayerSetSpeakingDone = playerVtbl.write_vfunc(0x108, &Hook_Player_SetSpeakingDone);
        SKSE::log::info("Hooked PlayerCharacter dialogue vfuncs (0x4C, 0x107, 0x108)");

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

    void ImpersonationManager::SetOptions(bool a_conditions, bool a_aliases, bool a_packages, bool a_scripts) {
        bool hadPackages = _options.packages;
        bool hadScripts = _options.scripts;
        _options.conditions = a_conditions;
        _options.aliases = a_aliases;
        _options.packages = a_packages;
        _options.scripts = a_scripts;

        SKSE::log::info("Updated Impersonation Options: Conditions={}, Aliases={}, Packages={}, Scripts={}",
            a_conditions, a_aliases, a_packages, a_scripts);

        if (_active.load(std::memory_order_relaxed)) {
            auto* donor = _donorActor.load(std::memory_order_relaxed);
            auto* player = _playerActor.load(std::memory_order_relaxed);
            if (donor && player) {
                if (!hadPackages && a_packages) {
                    RetargetPackages(donor, player);
                } else if (hadPackages && !a_packages) {
                    RestorePackages();
                }

                if (!hadScripts && a_scripts) {
                    RedirectPapyrusProperties(donor, player);
                } else if (hadScripts && !a_scripts) {
                    RestorePapyrusProperties(donor, player);
                }
            }
        }
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
        if (_options.aliases) {
            AliasManager::GetSingleton().SwapAliasesToPlayer(a_donor, a_player);
        }

        // Layer 3: AI Packages
        if (_options.packages) {
            RetargetPackages(a_donor, a_player);
        }

        // Layer 4: Papyrus Properties
        if (_options.scripts) {
            RedirectPapyrusProperties(a_donor, a_player);
        }

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

        // Layer 4: Papyrus Properties
        if (_options.scripts) {
            RestorePapyrusProperties(donor, player);
        }

        // Layer 3: AI Packages
        RestorePackages();

        // Layer 2: Quest Aliases
        if (_options.aliases) {
            AliasManager::GetSingleton().RestoreAliases();
        }

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
            if (auto* oldPuppet = GetPuppet()) {
                oldPuppet->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kPlayerTeammate);
                auto* followerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E); // CurrentFollowerFaction
                if (followerFaction) {
                    oldPuppet->AddToFaction(followerFaction, -1);
                }
                auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1); // PlayerFaction
                if (playerFaction) {
                    oldPuppet->AddToFaction(playerFaction, -1);
                }
                if (auto* base = skyrim_cast<RE::TESNPC*>(oldPuppet->GetActorBase())) {
                    auto* followPkg = RE::TESForm::LookupByID<RE::TESPackage>(0x0004B037); // DefaultFollowPlayer
                    if (followPkg && !base->aiPackages.packages.empty()) {
                        std::vector<RE::TESPackage*> remaining;
                        for (auto* pkg : base->aiPackages.packages) {
                            if (pkg && pkg != followPkg) {
                                remaining.push_back(pkg);
                            }
                        }
                        base->aiPackages.packages.clear();
                        for (auto it = remaining.rbegin(); it != remaining.rend(); ++it) {
                            base->aiPackages.packages.push_front(*it);
                        }
                    }
                }
                oldPuppet->EvaluatePackage(false, false);
                SKSE::log::info("Disengaged follower status from puppet {:08X}",
                    static_cast<std::uint32_t>(oldPuppet->GetFormID()));
            }
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

        // 1. Set PlayerTeammate flag so puppet follows player, draws weapons, and assists in combat
        a_puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);

        // 2. Add DefaultFollowPlayer package (0x0004B037) so puppet paths and follows player
        if (auto* base = skyrim_cast<RE::TESNPC*>(a_puppet->GetActorBase())) {
            auto* followPkg = RE::TESForm::LookupByID<RE::TESPackage>(0x0004B037); // DefaultFollowPlayer
            if (followPkg) {
                bool hasPkg = false;
                for (auto* pkg : base->aiPackages.packages) {
                    if (pkg == followPkg) {
                        hasPkg = true;
                        break;
                    }
                }
                if (!hasPkg) {
                    base->aiPackages.packages.push_front(followPkg);
                }
            }
        }

        // 3. Add to CurrentFollowerFaction (0x0005C84E) with rank 0
        auto* followerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
        if (followerFaction) {
            a_puppet->AddToFaction(followerFaction, 0);
        }

        // 4. Ensure in PlayerFaction (0x00000DB1) with rank 0
        auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
        if (playerFaction) {
            a_puppet->AddToFaction(playerFaction, 0);
        }

        // 5. Clear combat, allow dialogue, and evaluate package immediately
        a_puppet->AllowPCDialogue(true);
        a_puppet->AllowBleedoutDialogue(true);
        a_puppet->StopCombat();
        a_puppet->EvaluatePackage(true, true);

        SKSE::log::info("Configured puppet {:08X} as active player follower.",
            static_cast<std::uint32_t>(a_puppet->GetFormID()));
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

    // --- Layer 4: Papyrus Properties Redirection ---
    static bool IsOurScript(std::string_view name) {
        auto startsWithNoCase = [](std::string_view str, std::string_view prefix) {
            if (str.size() < prefix.size()) return false;
            return _strnicmp(str.data(), prefix.data(), prefix.size()) == 0;
        };
        return startsWithNoCase(name, "akc") ||
               startsWithNoCase(name, "akcontrol") ||
               startsWithNoCase(name, "ak_ng");
    }

    void ImpersonationManager::RedirectPapyrusProperties(RE::Actor* a_donor, RE::Actor* a_player) {
        if (!a_donor || !a_player) return;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return;
        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) return;

        auto donorHandle = policy->GetHandleForObject(RE::Actor::FORMTYPE, a_donor);
        auto playerHandle = policy->GetHandleForObject(RE::Actor::FORMTYPE, a_player);

        struct Candidate {
            RE::VMHandle holderHandle;
            std::string scriptName;
            std::uint32_t varIndex;
        };
        std::vector<Candidate> candidates;

        {
            RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
            for (const auto& [holderHandle, scriptArray] : vm->attachedScripts) {
                if (holderHandle == donorHandle || holderHandle == playerHandle) {
                    continue;
                }

                for (auto& attached : scriptArray) {
                    auto objPtr = attached.get();
                    if (!objPtr || !objPtr->IsConstructed() || !objPtr->IsValid()) {
                        continue;
                    }

                    auto* typeInfo = objPtr->GetTypeInfo();
                    if (!typeInfo) continue;

                    const char* scriptName = typeInfo->GetName();
                    if (!scriptName || IsOurScript(scriptName)) {
                        continue;
                    }

                    std::uint32_t totalVars = typeInfo->GetTotalNumVariables();
                    for (std::uint32_t i = 0; i < totalVars; ++i) {
                        auto& var = objPtr->variables[i];
                        if (var.IsObject()) {
                            auto targetObj = var.GetObject();
                            if (!targetObj) continue;

                            auto targetHandle = targetObj->GetHandle();
                            auto* resolved = policy->GetObjectForHandle(RE::Actor::FORMTYPE, targetHandle);
                            if (resolved == a_donor) {
                                auto* targetType = targetObj->GetTypeInfo();
                                if (targetType) {
                                    const char* tn = targetType->GetName();
                                    if (tn && (_stricmp(tn, "Actor") == 0 ||
                                               _stricmp(tn, "ObjectReference") == 0 ||
                                               _stricmp(tn, "Form") == 0)) {
                                        candidates.push_back({ holderHandle, scriptName, i });
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        for (const auto& cand : candidates) {
            RE::BSTSmartPointer<RE::BSScript::Object> boundObj;
            if (vm->FindBoundObject(cand.holderHandle, cand.scriptName.c_str(), boundObj) && boundObj) {
                auto* typeInfo = boundObj->GetTypeInfo();
                if (typeInfo && cand.varIndex < typeInfo->GetTotalNumVariables()) {
                    auto& var = boundObj->variables[cand.varIndex];
                    var.Pack(a_player);
                    _papyrusRedirects.push_back({ cand.holderHandle, cand.scriptName, cand.varIndex });
                }
            }
        }

        SKSE::log::info("Redirected {} Papyrus script properties from donor to player.", _papyrusRedirects.size());
    }

    void ImpersonationManager::RestorePapyrusProperties(RE::Actor* a_donor, RE::Actor* a_player) {
        if (_papyrusRedirects.empty() || !a_donor) {
            _papyrusRedirects.clear();
            return;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            _papyrusRedirects.clear();
            return;
        }
        auto* policy = vm->GetObjectHandlePolicy();

        for (const auto& rec : _papyrusRedirects) {
            RE::BSTSmartPointer<RE::BSScript::Object> boundObj;
            if (vm->FindBoundObject(rec.holderHandle, rec.scriptName.c_str(), boundObj) && boundObj) {
                auto* typeInfo = boundObj->GetTypeInfo();
                if (typeInfo && rec.varIndex < typeInfo->GetTotalNumVariables()) {
                    auto& var = boundObj->variables[rec.varIndex];
                    if (var.IsObject()) {
                        auto targetObj = var.GetObject();
                        if (targetObj && policy) {
                            auto targetHandle = targetObj->GetHandle();
                            auto* resolved = policy->GetObjectForHandle(RE::Actor::FORMTYPE, targetHandle);
                            if (resolved == a_player) {
                                var.Pack(a_donor);
                            }
                        }
                    }
                }
            }
        }

        SKSE::log::info("Restored {} redirected Papyrus script properties back to donor.", _papyrusRedirects.size());
        _papyrusRedirects.clear();
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

        std::uint32_t optMask = 0;
        if (_options.conditions) optMask |= (1 << 0);
        if (_options.aliases) optMask |= (1 << 1);
        if (_options.packages) optMask |= (1 << 2);
        if (_options.scripts) optMask |= (1 << 3);

        if (a_serde->OpenRecord(Serialization::kImpersonationDataRecord, Serialization::kSerializationVersion)) {
            a_serde->WriteRecordData(&active, sizeof(active));
            a_serde->WriteRecordData(&donorID, sizeof(donorID));
            a_serde->WriteRecordData(&puppetID, sizeof(puppetID));
            a_serde->WriteRecordData(&optMask, sizeof(optMask));

            std::uint32_t redirectCount = static_cast<std::uint32_t>(_papyrusRedirects.size());
            a_serde->WriteRecordData(&redirectCount, sizeof(redirectCount));
            for (const auto& r : _papyrusRedirects) {
                a_serde->WriteRecordData(&r.holderHandle, sizeof(r.holderHandle));
                std::uint16_t nameLen = static_cast<std::uint16_t>(r.scriptName.size());
                a_serde->WriteRecordData(&nameLen, sizeof(nameLen));
                a_serde->WriteRecordData(r.scriptName.data(), nameLen);
                a_serde->WriteRecordData(&r.varIndex, sizeof(r.varIndex));
            }
        }
    }

    void ImpersonationManager::Load(SKSE::SerializationInterface* a_serde, std::uint32_t a_version, std::uint32_t a_length) {
        if (!a_serde) return;

        bool active = false;
        RE::FormID donorID = 0;
        RE::FormID puppetID = 0;
        std::uint32_t optMask = 0;

        if (a_length < (sizeof(active) + sizeof(donorID) + sizeof(optMask))) {
            return;
        }

        a_serde->ReadRecordData(&active, sizeof(active));
        a_serde->ReadRecordData(&donorID, sizeof(donorID));

        if (a_version >= 2 || a_length >= (sizeof(active) + sizeof(donorID) + sizeof(puppetID) + sizeof(optMask))) {
            a_serde->ReadRecordData(&puppetID, sizeof(puppetID));
        }

        a_serde->ReadRecordData(&optMask, sizeof(optMask));

        _options.conditions = (optMask & (1 << 0)) != 0;
        _options.aliases = (optMask & (1 << 1)) != 0;
        _options.packages = (optMask & (1 << 2)) != 0;
        _options.scripts = (optMask & (1 << 3)) != 0;

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

        std::uint32_t redirectCount = 0;
        if (a_serde->ReadRecordData(&redirectCount, sizeof(redirectCount))) {
            _papyrusRedirects.clear();
            for (std::uint32_t i = 0; i < redirectCount; ++i) {
                RE::VMHandle oldHandle = 0;
                a_serde->ReadRecordData(&oldHandle, sizeof(oldHandle));
                std::uint16_t nameLen = 0;
                a_serde->ReadRecordData(&nameLen, sizeof(nameLen));
                std::string scriptName(nameLen, '\0');
                if (nameLen > 0) {
                    a_serde->ReadRecordData(scriptName.data(), nameLen);
                }
                std::uint32_t varIndex = 0;
                a_serde->ReadRecordData(&varIndex, sizeof(varIndex));

                RE::VMHandle newHandle = 0;
                if (a_serde->ResolveHandle(oldHandle, newHandle)) {
                    _papyrusRedirects.push_back({ newHandle, scriptName, varIndex });
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
        _papyrusRedirects.clear();
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
            if (donor && player && _options.packages) {
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
