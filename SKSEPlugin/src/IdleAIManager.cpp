#include "PCH.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESQuest.h"
#include "RE/T/TESTopic.h"
#include "RE/T/TESTopicInfo.h"
#include "RE/T/TESCombatStyle.h"
#include "RE/T/TESClass.h"
#include "RE/E/ExtraCombatStyle.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/ProcessLists.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/S/SpellItem.h"
#include "RE/E/EffectSetting.h"
#include "RE/M/MagicCaster.h"
#include "RE/C/CombatController.h"
#include "RE/C/CombatAnimation.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/T/TESAmmo.h"
#include "RE/A/AttackBlockHandler.h"
#include "RE/P/Projectile.h"
#include <SKSE/ContextHook.h>
#include "AliasManager.h"
#include <random>
#include <cmath>

namespace AlternateActors {

    void IdleAIManager::Initialize() {
        if (!_initialized) {
            auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton();
            if (inputDispatcher) {
                inputDispatcher->AddEventSink<RE::InputEvent*>(this);
                SKSE::log::info("IdleAIManager registered input event sink successfully.");
            } else {
                SKSE::log::warn("IdleAIManager failed to get BSInputDeviceManager singleton.");
            }

            auto* ui = RE::UI::GetSingleton();
            if (ui) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
                SKSE::log::info("IdleAIManager registered MenuOpenCloseEvent sink successfully.");
            } else {
                SKSE::log::warn("IdleAIManager failed to get UI singleton.");
            }

            InstallHooks();

            _initialized = true;
        }
    }

    void IdleAIManager::InstallAimControllerSafetyPatch() {
        // In Skyrim engine, function 47299 (CombatProjectileAimController raycast/aim evaluation)
        // retrieves the target Actor reference via 0x1402be9b0. When an actor or player transitions
        // cells or their 3D unloads, 0x1402be9b0 returns nullptr (rax == 0).
        // Function 47299 has a branch (je 0x140868320) that clears rdi to null, but then falls through
        // directly to `mov rax, [r14]` with r14 == 0, causing a fatal null dereference crash.
        // We patch the branch landing site at `+0x1F0` with a jump to the function epilogue (+0x25C)
        // where it safely returns nullptr (rbx == 0) to caller 47300, which already expects and handles null.
        auto funcAddr = REL::RelocationID(47299, 47299).address();
        if (!funcAddr) {
            SKSE::log::error("Failed to resolve function 47299 for aim controller safety patch!");
            return;
        }

        auto* bytes = reinterpret_cast<const std::uint8_t*>(funcAddr);
        // Look for the crash pattern within the first 0x300 bytes:
        // 49 8B 06 49 8B CE (mov rax, [r14]; mov rcx, r14)
        std::size_t crashOffset = 0;
        for (std::size_t i = 0x100; i < 0x300; ++i) {
            if (bytes[i] == 0x49 && bytes[i + 1] == 0x8B && bytes[i + 2] == 0x06 &&
                bytes[i + 3] == 0x49 && bytes[i + 4] == 0x8B && bytes[i + 5] == 0xCE) {
                crashOffset = i;
                break;
            }
        }

        if (crashOffset == 0 || crashOffset < 7) {
            SKSE::log::warn("Could not locate crash pattern in function 47299 at {:016X}", funcAddr);
            return;
        }

        std::size_t patchSite = crashOffset - 7;
        // Verify preceding bytes are mov rdi, rbx (48 8B FB) and mov [rbp - disp], rbx (48 89 5D)
        if (bytes[patchSite] != 0x48 || bytes[patchSite + 1] != 0x8B || bytes[patchSite + 2] != 0xFB) {
            SKSE::log::warn("Preceding bytes at {:016X} do not match expected mov rdi, rbx (found {:02X} {:02X} {:02X})",
                funcAddr + patchSite, bytes[patchSite], bytes[patchSite + 1], bytes[patchSite + 2]);
            return;
        }

        // Locate epilogue returning rbx: mov rax, rbx (48 8B C3)
        std::size_t epilogueOffset = 0;
        for (std::size_t i = crashOffset; i < crashOffset + 0x100; ++i) {
            if (bytes[i] == 0x48 && bytes[i + 1] == 0x8B && bytes[i + 2] == 0xC3) {
                epilogueOffset = i;
                break;
            }
        }

        if (epilogueOffset == 0) {
            SKSE::log::warn("Could not locate epilogue return in function 47299 at {:016X}", funcAddr);
            return;
        }

        // Calculate relative 32-bit displacement for jmp rel32 (E9 <disp32>)
        // disp32 = target - (patchAddress + 5)
        std::int32_t disp32 = static_cast<std::int32_t>(epilogueOffset - (patchSite + 5));
        std::uint8_t patch[7] = {
            0xE9,
            static_cast<std::uint8_t>(disp32 & 0xFF),
            static_cast<std::uint8_t>((disp32 >> 8) & 0xFF),
            static_cast<std::uint8_t>((disp32 >> 16) & 0xFF),
            static_cast<std::uint8_t>((disp32 >> 24) & 0xFF),
            0x90, // nop
            0x90  // nop
        };

        REL::safe_write(funcAddr + patchSite, patch, sizeof(patch));
        SKSE::log::info("Installed CombatProjectileAimController safety patch in function 47299 at {:016X} (jmp rel32 +0x{:X})",
            funcAddr + patchSite, disp32);
    }

    void IdleAIManager::InstallHooks() {
        if (_hooksInstalled) return;

        InstallAimControllerSafetyPatch();

        _hooksInstalled = true;
    }

    void IdleAIManager::Update() {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && player->IsInCombat()) {
                if (_aiActive.load(std::memory_order_acquire)) {
                    StopIdleAI();
                }
            } else {
                CheckIdleAI();
            }

            if (player) {
                auto* currentCell = player->GetParentCell();
                if (currentCell && currentCell != _lastCell) {
                    _lastCell = currentCell;
                    // On cell transition, allow idle dialogue evaluation after a short settle time (3s)
                    _lastIdleChatterTime = std::chrono::steady_clock::now() - std::chrono::seconds(12);
                }
            }

            if (_aiActive.load(std::memory_order_acquire) && _isControllingNPC.load(std::memory_order_acquire)) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<float>(now - _lastPackageSyncTime).count() >= 1.5f) {
                    _lastPackageSyncTime = now;
                    ImpersonationManager::GetSingleton().SyncDonorUpdatesToPlayer();
                }
            }
            CheckAndTriggerIdleDialogue();
        }
        ImpersonationManager::GetSingleton().Tick();
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        bool hasUserAction = false;
        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* button = static_cast<const RE::ButtonEvent*>(event);
                if (button->IsPressed() || button->IsDown() || button->IsHeld()) {
                    hasUserAction = true;
                    break;
                }
            } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                const auto* stick = static_cast<const RE::ThumbstickEvent*>(event);
                if (std::abs(stick->xValue) > 0.25f || std::abs(stick->yValue) > 0.25f) {
                    hasUserAction = true;
                    break;
                }
            }
        }

        if (hasUserAction) {
            OnUserInput();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl IdleAIManager::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == "Dialogue Menu"sv) {
            if (!a_event->opening) {
                auto* topicMgr = RE::MenuTopicManager::GetSingleton();
                bool isSpeaking = ImpersonationManager::GetSingleton().IsPlayerSpeaking();
                bool isGreeting = topicMgr && topicMgr->isGreetingPlayer;
                bool isGoodbye = topicMgr && topicMgr->forceGoodbye;

                // Only tear down self-dialogue state if dialogue is genuinely ending,
                // NOT during transient Scaleform menu hide events while lines are playing
                if (isGoodbye || (!isSpeaking && !isGreeting && (!topicMgr || !topicMgr->menuOpen))) {
                    _inSelfDialogue.store(false, std::memory_order_release);
                    auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
                    auto* donor = ImpersonationManager::GetSingleton().GetDonor();
                    if (puppet && donor) {
                        auto* extraFaction = puppet->extraList.GetByType<RE::ExtraFactionChanges>();
                        if (extraFaction) {
                            for (const auto& fRank : extraFaction->factionChanges) {
                                if (fRank.faction) {
                                    donor->AddToFaction(fRank.faction, fRank.rank);
                                }
                            }
                            extraFaction->factionChanges.clear();
                        }
                    }
                    if (topicMgr && puppet) {
                        auto puppetHandle = puppet->GetHandle();
                        if (topicMgr->speaker == puppetHandle) {
                            topicMgr->speaker.reset();
                        }
                        if (topicMgr->lastSpeaker == puppetHandle) {
                            topicMgr->lastSpeaker.reset();
                        }
                    }
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void IdleAIManager::OnUserInput() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _lastInputTime = std::chrono::steady_clock::now();
        if (_aiActive.load(std::memory_order_acquire)) {
            StopIdleAI();
        }
    }

    void IdleAIManager::CheckIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_enabled.load(std::memory_order_acquire) || !_isControllingNPC.load(std::memory_order_acquire)) {
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && player->IsInCombat()) {
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() ||
            ui->IsApplicationMenuOpen() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsMenuOpen("Console"sv) ||
            ui->IsMenuOpen("Loading Menu"sv) || ui->IsMenuOpen("Crafting Menu"sv) || ui->IsMenuOpen("RaceSex Menu"sv) ||
            ui->IsMenuOpen("Barter Menu"sv) || ui->IsMenuOpen("Container Menu"sv) || ui->IsMenuOpen("Journal Menu"sv) ||
            ui->IsMenuOpen("MapMenu"sv) || ui->IsMenuOpen("StatsMenu"sv) || ui->IsMenuOpen("InventoryMenu"sv) ||
            ui->IsMenuOpen("MagicMenu"sv) || ui->IsMenuOpen("TweenMenu"sv)) {
            _lastInputTime = std::chrono::steady_clock::now();
            if (_aiActive.load(std::memory_order_acquire)) {
                StopIdleAI();
            }
            return;
        }

        float timeout = _idleTimeoutSeconds.load(std::memory_order_acquire);

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - _lastInputTime).count();

        if (elapsed >= timeout && !_aiActive.load(std::memory_order_acquire)) {
            StartIdleAI();
        }
    }

    void IdleAIManager::StartIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_isControllingNPC.load(std::memory_order_acquire) || !_enabled.load(std::memory_order_acquire)) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || player->IsInCombat()) return;

        auto* pc = RE::PlayerControls::GetSingleton();
        if (pc) {
            _savedUserRunning = pc->data.running;
        }

        _aiActive.store(true, std::memory_order_release);
        _lastPackageSyncTime = std::chrono::steady_clock::now();

        ImpersonationManager::GetSingleton().SyncDonorUpdatesToPlayer();

        player->AllowBleedoutDialogue(true);
        player->SetAIDriven(true);
        player->EvaluatePackage(true, true);

        auto* currentPkg = player->GetCurrentPackage();
        if (currentPkg) {
            SKSE::log::info("Player idle timeout reached - engaged autonomous AI package: {:08X} ({})",
                currentPkg->GetFormID(), currentPkg->GetFormEditorID() ? currentPkg->GetFormEditorID() : "No EDID");
        } else {
            SKSE::log::warn("Player idle timeout reached - engaged autonomous AI, but GetCurrentPackage() returned null!");
        }
    }

    void IdleAIManager::StopIdleAI() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* pc = RE::PlayerControls::GetSingleton();
        if (player) {
            player->SetAIDriven(false);
            ImpersonationManager::GetSingleton().ClearPlayerPackages();
            player->EvaluatePackage(false, false);

            if (_aiActive.load(std::memory_order_acquire)) {
                SKSE::log::info("Player input detected - disengaged autonomous AI.");
            }
        }
        if (pc) {
            pc->data.autoMove = false;
            pc->data.moveInputVec = { 0.0f, 0.0f };
            pc->data.running = _savedUserRunning;
        }
        _aiActive.store(false, std::memory_order_release);
        _lastInputTime = std::chrono::steady_clock::now();
    }

    void IdleAIManager::SetControllingNPC(bool a_controlling) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _isControllingNPC.store(a_controlling, std::memory_order_release);
        if (!a_controlling) {
            StopIdleAI();
            _inSelfDialogue.store(false, std::memory_order_release);
        } else {
            auto* pc = RE::PlayerControls::GetSingleton();
            if (pc) {
                _savedUserRunning = pc->data.running;
            }
        }
        _lastInputTime = std::chrono::steady_clock::now();
        SKSE::log::info("IdleAIManager controlling NPC set to: {}", a_controlling);
    }

    bool IdleAIManager::OpenSelfDialogue(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!a_speaker) {
            if (puppet) {
                a_speaker = puppet;
            } else if (donor) {
                a_speaker = donor;
            } else {
                a_speaker = player;
            }
        }
        if (!a_speaker) return false;

        SKSE::log::info("OpenSelfDialogue: received request for speaker {:08X} (puppet: {:08X}, donor: {:08X})",
            static_cast<std::uint32_t>(a_speaker->GetFormID()),
            puppet ? static_cast<std::uint32_t>(puppet->GetFormID()) : 0,
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0);

        auto* task = SKSE::GetTaskInterface();
        if (task) {
            RE::ActorHandle speakerHandle = a_speaker->GetHandle();
            task->AddTask([speakerHandle]() {
                auto speaker = speakerHandle.get();
                if (!speaker) {
                    SKSE::log::warn("OpenSelfDialogue task: speaker handle is no longer valid");
                    return;
                }
                IdleAIManager::GetSingleton().OpenSelfDialogueDirect(speaker.get());
            });
            return true;
        }

        return OpenSelfDialogueDirect(a_speaker);
    }

    bool IdleAIManager::OpenSelfDialogueDirect(RE::Actor* a_speaker) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !a_speaker) return false;

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();

        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            if (ui->GameIsPaused()) {
                SKSE::log::warn("OpenSelfDialogueDirect: Game is paused, aborting");
                return false;
            }
            if (ui->IsMenuOpen("Dialogue Menu"sv)) {
                SKSE::log::warn("OpenSelfDialogueDirect: Dialogue Menu is already open, aborting");
                return false;
            }
            if (ui->IsModalMenuOpen()) {
                SKSE::log::warn("OpenSelfDialogueDirect: Modal menu is open, aborting");
                return false;
            }
        }

        if (_aiActive.load(std::memory_order_acquire)) {
            StopIdleAI();
        }

        if (player->IsInCombat()) {
            SKSE::log::warn("OpenSelfDialogueDirect: Player is currently in combat!");
        }
        if (a_speaker->IsInCombat()) {
            SKSE::log::warn("OpenSelfDialogueDirect: Speaker {:08X} is currently in combat!",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
        }

        // Enable dialogue permissions on both
        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        player->AllowPCDialogue(true);
        player->AllowBleedoutDialogue(true);

        _inSelfDialogue.store(true, std::memory_order_release);

        SKSE::log::info("OpenSelfDialogueDirect: initiating SetDialogueWithPlayer on speaker {:08X} (isPuppet: {}, donor: {:08X}, voiceType: {:08X})",
            static_cast<std::uint32_t>(a_speaker->GetFormID()),
            (a_speaker == puppet),
            donor ? static_cast<std::uint32_t>(donor->GetFormID()) : 0,
            ImpersonationManager::GetSingleton().GetDonorVoiceType() ?
                static_cast<std::uint32_t>(ImpersonationManager::GetSingleton().GetDonorVoiceType()->GetFormID()) : 0);

        // Open native dialogue menu directly with the speaker
        bool opened = a_speaker->SetDialogueWithPlayer(true, false, nullptr);
        if (opened) {
            SKSE::log::info("Successfully opened self-dialogue with actor {:08X} (isPuppet: {})",
                static_cast<std::uint32_t>(a_speaker->GetFormID()), (a_speaker == puppet));
            return true;
        } else {
            SKSE::log::warn("SetDialogueWithPlayer returned false for actor {:08X}, attempting force-greet fallback",
                static_cast<std::uint32_t>(a_speaker->GetFormID()));
            bool greeted = a_speaker->SetDialogueWithPlayer(true, true, nullptr);
            if (greeted) {
                SKSE::log::info("Successfully opened self-dialogue via force-greet fallback with actor {:08X}",
                    static_cast<std::uint32_t>(a_speaker->GetFormID()));
            } else {
                SKSE::log::error("Failed to open dialogue with actor {:08X} even with force-greet fallback",
                    static_cast<std::uint32_t>(a_speaker->GetFormID()));
                _inSelfDialogue.store(false, std::memory_order_release);
            }
            return greeted;
        }
    }

    bool IdleAIManager::IsInSelfDialogue() const {
        auto* topicMgr = RE::MenuTopicManager::GetSingleton();
        if (topicMgr && topicMgr->forceGoodbye) {
            return false;
        }

        if (_inSelfDialogue.load(std::memory_order_acquire)) {
            return true;
        }

        auto* ui = RE::UI::GetSingleton();
        bool menuOpen = (ui && ui->IsMenuOpen("Dialogue Menu"sv)) || (topicMgr && (topicMgr->menuOpen || topicMgr->isGreetingPlayer));
        if (!menuOpen && !ImpersonationManager::GetSingleton().IsPlayerSpeaking()) {
            return false;
        }

        auto* puppet = ImpersonationManager::GetSingleton().GetPuppet();
        auto* donor = ImpersonationManager::GetSingleton().GetDonor();
        if (!puppet && !donor) return false;

        if (topicMgr) {
            // If Dialogue Menu is open, check if puppet or donor is the active speaker
            if (menuOpen) {
                if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                    return true;
                }
                if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                    return true;
                }
            }

            // If player is actively speaking, speaker or lastSpeaker is valid
            if (ImpersonationManager::GetSingleton().IsPlayerSpeaking()) {
                if (puppet && (topicMgr->speaker == puppet->GetHandle() || topicMgr->lastSpeaker == puppet->GetHandle())) {
                    return true;
                }
                if (donor && (topicMgr->speaker == donor->GetHandle() || topicMgr->lastSpeaker == donor->GetHandle())) {
                    return true;
                }
            }
        }

        return false;
    }

    bool IdleAIManager::TriggerVoiceLine(RE::Actor* a_speaker) {
        if (!a_speaker) {
            a_speaker = RE::PlayerCharacter::GetSingleton();
        }
        if (!a_speaker) return false;

        a_speaker->AllowPCDialogue(true);
        a_speaker->AllowBleedoutDialogue(true);
        return a_speaker->SetDialogueWithPlayer(true, true, nullptr);
    }

    static bool SafeConditionCheck(RE::TESCondition* a_cond, RE::TESObjectREFR* a_actionRef, RE::TESObjectREFR* a_targetRef) {
        if (!a_cond || !a_cond->head) return true;
        __try {
            return a_cond->IsTrue(a_actionRef, a_targetRef);
        } __except (1) { // EXCEPTION_EXECUTE_HANDLER
            return false;
        }
    }

    static bool QuestRelatesToDonor(RE::TESQuest* a_quest, RE::Actor* a_donor, const RE::TESFile* a_donorFile) {
        if (!a_quest || !a_donor) return false;
        // 1. Same source plugin as the donor NPC
        if (a_donorFile && a_quest->GetFile(0) == a_donorFile) {
            return true;
        }
        // 2. Donor has an active alias instance for this quest
        if (auto* aliasArray = a_donor->extraList.GetByType<RE::ExtraAliasInstanceArray>()) {
            RE::BSReadLockGuard locker(aliasArray->lock);
            for (auto* inst : aliasArray->aliases) {
                if (inst && inst->quest == a_quest) {
                    return true;
                }
            }
        }
        // 3. Quest aliases explicitly point to donor
        for (auto* baseAlias : a_quest->aliases) {
            if (!baseAlias) continue;
            if (auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(baseAlias)) {
                auto* ref = refAlias->GetReference();
                if (ref && (ref == a_donor || ref->GetBaseObject() == a_donor->GetBaseObject() || ref->GetBaseObject() == a_donor->GetActorBase())) {
                    return true;
                }
            }
        }
        // 4. Quest dialogue conditions explicitly check donor ID
        if (auto* cond = a_quest->QConditions()) {
            for (auto* item = cond->head; item; item = item->next) {
                if (item->data.functionData.function.underlying() == 72) { // kGetIsID
                    auto* form = static_cast<RE::TESForm*>(item->data.functionData.params[0]);
                    if (form && (form == a_donor || form == a_donor->GetBaseObject() || form == a_donor->GetActorBase())) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool IdleAIManager::CheckAndTriggerIdleDialogue() {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto& impMgr = ImpersonationManager::GetSingleton();
        if (!_isControllingNPC.load(std::memory_order_acquire) && !impMgr.IsActive()) {
            return false;
        }

        if (!impMgr.IsNPCVoiceEnabled()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();

        if (!player) return false;

        auto* donor = impMgr.GetDonor();
        if (!donor) return false;

        auto* donorBase = donor->GetActorBase();
        const auto* donorFile = donorBase ? donorBase->GetFile(0) : nullptr;

        auto* puppet = impMgr.GetPuppet();
        auto* targetRef = puppet ? static_cast<RE::TESObjectREFR*>(puppet) : static_cast<RE::TESObjectREFR*>(player);

        // Skip if player is currently speaking or in active self-dialogue
        if (impMgr.IsPlayerSpeaking() || IsInSelfDialogue()) {
            return false;
        }

        // Skip if in combat
        if (player->IsInCombat()) {
            return false;
        }

        // Check menus
        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused() || ui->IsModalMenuOpen() || ui->IsItemMenuOpen() ||
            ui->IsApplicationMenuOpen() || ui->IsMenuOpen("Dialogue Menu"sv) || ui->IsMenuOpen("Console"sv) ||
            ui->IsMenuOpen("Loading Menu"sv) || ui->IsMenuOpen("Crafting Menu"sv) || ui->IsMenuOpen("RaceSex Menu"sv) ||
            ui->IsMenuOpen("Barter Menu"sv) || ui->IsMenuOpen("Container Menu"sv) || ui->IsMenuOpen("Journal Menu"sv) ||
            ui->IsMenuOpen("MapMenu"sv) || ui->IsMenuOpen("StatsMenu"sv) || ui->IsMenuOpen("InventoryMenu"sv) ||
            ui->IsMenuOpen("MagicMenu"sv) || ui->IsMenuOpen("TweenMenu"sv) || ui->IsMenuOpen("Book Menu"sv)) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        auto* currentCell = player->GetParentCell();
        if (currentCell && currentCell != _lastCell) {
            _lastCell = currentCell;
            // On cell transition, allow evaluation after a short settle time (3s)
            _lastIdleChatterTime = now - std::chrono::seconds(12);
        }

        float elapsed = std::chrono::duration<float>(now - _lastIdleChatterTime).count();
        constexpr float kIdleChatterCooldown = 15.0f;
        if (elapsed < kIdleChatterCooldown) {
            return false;
        }

        // Throttle quest & condition polling to once every 1.5 seconds when out of cooldown
        float evalElapsed = std::chrono::duration<float>(now - _lastIdleEvalTime).count();
        constexpr float kIdleEvalInterval = 1.5f;
        if (evalElapsed < kIdleEvalInterval) {
            return false;
        }
        _lastIdleEvalTime = now;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return false;

        static auto lastDiagnosticLogTime = std::chrono::steady_clock::now();
        bool doDiagnosticLog = std::chrono::duration<float>(now - lastDiagnosticLogTime).count() >= 15.0f;
        if (doDiagnosticLog) {
            lastDiagnosticLogTime = now;
        }

        struct Candidate {
            RE::TESQuest* quest{ nullptr };
            RE::TESTopic* topic{ nullptr };
            RE::TESTopicInfo* info{ nullptr };
        };
        std::vector<Candidate> candidates;

        const auto& quests = dataHandler->GetFormArray<RE::TESQuest>();
        for (auto* quest : quests) {
            if (!quest || !quest->IsRunning()) continue;

            // Only evaluate quests relevant to this donor (avoids querying unrelated mods)
            if (!QuestRelatesToDonor(quest, donor, donorFile)) {
                continue;
            }

            // Check quest dialogue conditions safely
            bool objCondPassed = !quest->objConditions.head || SafeConditionCheck(&quest->objConditions, player, targetRef);
            if (doDiagnosticLog) {
                SKSE::log::info("CheckAndTriggerIdleDialogue: Quest {:08X} ('{}') relatesToDonor=true, objConditions={}",
                    quest->GetFormID(), quest->GetName(), objCondPassed);
            }
            if (!objCondPassed) {
                continue;
            }

            constexpr auto kMiscIndex = RE::DIALOGUE_TYPE::kMiscellaneous - RE::DIALOGUE_TYPE::kBranchedTotal;
            if (kMiscIndex >= (RE::DIALOGUE_TYPE::kTotal - RE::DIALOGUE_TYPE::kBranchedTotal)) {
                continue;
            }

            const auto& miscTopics = quest->topics[kMiscIndex];
            for (auto* topic : miscTopics) {
                if (!topic) continue;
                auto subtypeVal = topic->data.subtype.underlying();
                // Subtype 94 is kIdle in post-Dragonborn runtimes; 88 is kIdle in Skyrim.esm/Dawnguard.esm pre-Dragonborn
                if (subtypeVal != 88 && subtypeVal != 94 && topic->data.subtype.get() != RE::DIALOGUE_DATA::Subtype::kIdle) {
                    continue;
                }

                if (topic->numTopicInfos == 0 || !topic->topicInfos) {
                    continue;
                }

                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (!info) continue;

                    if (info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kRequiresPlayerActivation)) {
                        continue;
                    }

                    if (info->saidOnce && info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                        continue;
                    }

                    if (info->objConditions.head && !SafeConditionCheck(&info->objConditions, player, targetRef)) {
                        continue;
                    }

                    candidates.push_back({ quest, topic, info });
                }
            }
        }

        if (candidates.empty()) {
            return false;
        }

        Candidate chosen = candidates[0];
        if (candidates.size() > 1) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
            chosen = candidates[dist(rng)];
        }

        _currentIdleDialogueItem = RE::make_smart<RE::DialogueItem>(chosen.quest, chosen.topic, chosen.info, player);
        if (_currentIdleDialogueItem && !_currentIdleDialogueItem->responses.empty()) {
            auto* response = _currentIdleDialogueItem->responses.front();
            if (response) {
                SKSE::log::info("Triggering autonomous idle dialogue on player: '{}' (topic {:08X}, info {:08X})",
                    response->text.c_str(), chosen.topic->GetFormID(), chosen.info->GetFormID());

                player->UpdateInDialogue(response, false);

                if (chosen.info->data.flags.any(RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS::kSayOnce)) {
                    chosen.info->saidOnce = true;
                }

                _lastIdleChatterTime = now;
                return true;
            }
        }

        return false;
    }

    void IdleAIManager::SetActiveDonorActor(RE::Actor* a_donor) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _activeDonorActor = a_donor ? a_donor->GetHandle() : RE::ActorHandle();
        if (a_donor) {
            SKSE::log::info("IdleAIManager::SetActiveDonorActor: donor {:08X}",
                static_cast<std::uint32_t>(a_donor->GetFormID()));
        } else {
            SKSE::log::info("IdleAIManager::SetActiveDonorActor: cleared active donor");
        }
    }
}
