#include "PCH.h"
#include "ImpersonationManager.h"
#include "AliasManager.h"
#include "FaceManager.h"
#include "Serialization.h"

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
#include <RE/O/ObjectTypeInfo.h>
#include <RE/B/BSAtomic.h>

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

        // 2. If checking on player OR puppet, try substituting donor
        if (a_thisObj && (a_thisObj == player || (puppet && a_thisObj == puppet))) {
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
            if (a_thisObj && (a_thisObj == donor || (puppet && a_thisObj == puppet)) && player) {
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

    // --- Category B: Spatial Thunk ---
    enum class SpatialMode {
        kThisOnly,
        kThisAndP1,
        kThisAndP1AndP2
    };

    template <std::uint16_t ID, SpatialMode Mode>
    static bool SpatialThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        auto* orig = s_originalConditions[ID];
        if (!mgr.IsConditionsEnabled()) {
            return orig ? orig(a_thisObj, a_p1, a_p2, a_result) : false;
        }

        auto* player = mgr.GetPlayer();
        auto* donor = mgr.GetDonor();
        if (!player || !donor) {
            return orig ? orig(a_thisObj, a_p1, a_p2, a_result) : false;
        }

        RE::TESObjectREFR* effectiveThis = a_thisObj;
        if (effectiveThis == donor) {
            effectiveThis = player;
        }

        void* effectiveP1 = a_p1;
        void* effectiveP2 = a_p2;

        if constexpr (Mode == SpatialMode::kThisAndP1 || Mode == SpatialMode::kThisAndP1AndP2) {
            if (effectiveP1 == donor) {
                effectiveP1 = player;
            }
        }

        if constexpr (Mode == SpatialMode::kThisAndP1AndP2) {
            if (effectiveP2 == donor) {
                effectiveP2 = player;
            }
        }

        return orig ? orig(effectiveThis, effectiveP1, effectiveP2, a_result) : false;
    }

    // --- Category C: Relationship Thunk ---
    template <std::uint16_t ID>
    static bool RelationshipThunk(RE::TESObjectREFR* a_thisObj, void* a_p1, void* a_p2, double& a_result) {
        auto& mgr = ImpersonationManager::GetSingleton();
        auto* orig = s_originalConditions[ID];
        if (!mgr.IsConditionsEnabled()) {
            return orig ? orig(a_thisObj, a_p1, a_p2, a_result) : false;
        }

        auto* player = mgr.GetPlayer();
        auto* donor = mgr.GetDonor();
        auto* puppet = mgr.GetPuppet();
        if (!player || !donor) {
            return orig ? orig(a_thisObj, a_p1, a_p2, a_result) : false;
        }

        RE::TESObjectREFR* effectiveThis = a_thisObj;
        void* effectiveP1 = a_p1;

        // Skip substitution if it would make effectiveThis == effectiveP1
        bool willBeSame = (effectiveThis == player && effectiveP1 == donor) ||
                          (effectiveThis == donor && effectiveP1 == player) ||
                          (effectiveThis == puppet && effectiveP1 == donor) ||
                          (effectiveThis == donor && effectiveP1 == puppet);

        if (!willBeSame) {
            if (effectiveThis == player || (puppet && effectiveThis == puppet)) {
                effectiveThis = donor;
            }
            if (effectiveP1 == player || (puppet && effectiveP1 == puppet)) {
                effectiveP1 = donor;
            }
        }

        return orig ? orig(effectiveThis, effectiveP1, a_p2, a_result) : false;
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
                auto* puppet = mgr.GetPuppet();
                if ((player && a_this == player->GetActorBase()) ||
                    (puppet && a_this == puppet->GetActorBase())) {
                    return mgr._donorVoiceType;
                }
                if (a_this->voiceType) {
                    return a_this->voiceType;
                }
            }
        }
        return _origGetObjectVoiceType(a_this);
    }

    bool ImpersonationManager::Hook_UpdateInDialogue(RE::Actor* a_this, RE::DialogueResponse* a_response, bool a_unused) {
        if (a_this) {
            auto& mgr = ImpersonationManager::GetSingleton();
            if (mgr.IsActive()) {
                auto* puppet = mgr.GetPuppet();
                auto* player = mgr.GetPlayer();
                if (puppet && a_this == puppet && player) {
                    SKSE::log::info("Redirecting dialogue response from puppet {:08X} to player {:08X}: '{}'",
                        static_cast<std::uint32_t>(puppet->GetFormID()),
                        static_cast<std::uint32_t>(player->GetFormID()),
                        a_response ? a_response->text.c_str() : "<null>");
                    return player->UpdateInDialogue(a_response, a_unused);
                }
            }
        }
        return _origUpdateInDialogue(a_this, a_response, a_unused);
    }

    void ImpersonationManager::InstallHooks() {
        if (_hooksInstalled) return;

        // Hook TESNPC::GetObjectVoiceType on primary vtable (index 0x49)
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESNPC[0] };
        _origGetObjectVoiceType = vtbl.write_vfunc(0x49, &Hook_GetObjectVoiceType);
        SKSE::log::info("Hooked TESNPC::GetObjectVoiceType at VTABLE_TESNPC[0] idx 0x49 (orig: {:016X})",
            _origGetObjectVoiceType.address());

        // Hook Character::UpdateInDialogue on primary vtable (index 0x4C)
        REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[0] };
        _origUpdateInDialogue = charVtbl.write_vfunc(0x4C, &Hook_UpdateInDialogue);
        SKSE::log::info("Hooked Character::UpdateInDialogue at VTABLE_Character[0] idx 0x4C (orig: {:016X})",
            _origUpdateInDialogue.address());

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

        // Category B - Spatial
        hook(1, SpatialThunk<1, SpatialMode::kThisAndP1>);      // GetDistance
        hook(27, SpatialThunk<27, SpatialMode::kThisAndP1>);    // GetLineOfSight
        hook(32, SpatialThunk<32, SpatialMode::kThisAndP1>);    // GetInSameCell
        hook(45, SpatialThunk<45, SpatialMode::kThisAndP1>);    // GetDetected
        hook(99, SpatialThunk<99, SpatialMode::kThisAndP1>);    // GetHeadingAngle
        hook(584, SpatialThunk<584, SpatialMode::kThisAndP1>);  // GetRelativeAngle
        hook(639, SpatialThunk<639, SpatialMode::kThisAndP1>);  // GetWithinDistance
        hook(705, SpatialThunk<705, SpatialMode::kThisAndP1>);  // GetShouldHelp
        hook(719, SpatialThunk<719, SpatialMode::kThisAndP1>);  // IsHostileToActor

        hook(577, SpatialThunk<577, SpatialMode::kThisAndP1AndP2>); // IsCloserToAThanB

        // Category C - Relationship
        hook(258, RelationshipThunk<258>);  // HasAssociationType
        hook(259, RelationshipThunk<259>);  // HasFamilyRelationship
        hook(261, RelationshipThunk<261>);  // HasParentRelationship
        hook(403, RelationshipThunk<403>);  // GetRelationshipRank
        hook(615, RelationshipThunk<615>);  // GetHighestRelationshipRank
        hook(616, RelationshipThunk<616>);  // GetLowestRelationshipRank
        hook(617, RelationshipThunk<617>);  // HasAssociationTypeAny
        hook(618, RelationshipThunk<618>);  // HasFamilyRelationshipAny

        _hooksInstalled = true;
        SKSE::log::info("Installed 20 Impersonation condition function hooks and GetObjectVoiceType vfunc hook successfully.");
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
                oldPuppet->EvaluatePackage(false, false);
                SKSE::log::info("Disengaged follower status from puppet {:08X}",
                    static_cast<std::uint32_t>(oldPuppet->GetFormID()));
            }
            _puppetHandle.reset();
            return;
        }

        _puppetHandle = a_puppet->GetHandle();

        // 1. Set PlayerTeammate flag so puppet follows player, draws weapons, and assists in combat
        a_puppet->GetActorRuntimeData().boolBits.set(RE::Actor::BOOL_BITS::kPlayerTeammate);

        // 2. Add to CurrentFollowerFaction (0x0005C84E) with rank 0
        auto* followerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
        if (followerFaction) {
            a_puppet->AddToFaction(followerFaction, 0);
        }

        // 3. Ensure in PlayerFaction (0x00000DB1) with rank 0
        auto* playerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x00000DB1);
        if (playerFaction) {
            a_puppet->AddToFaction(playerFaction, 0);
        }

        // 4. Set puppet display name to original player name
        if (!_origPlayerName.empty()) {
            if (auto* base = skyrim_cast<RE::TESNPC*>(a_puppet->GetActorBase())) {
                base->fullName = _origPlayerName;
            }
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
        // Periodic alias swapping during Tick removed:
        // Aliases are swapped on Begin() and restored on End().
        // Continual polling here is hazardous during cell transitions and threading.
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
                        } else if (vtbl == RE::VTABLE_BGSPackageDataTargetSelector[0].address() ||
                                   vtbl == RE::VTABLE_BGSPackageDataRef[0].address()) {
                            auto* targ = *reinterpret_cast<RE::PackageTarget**>(reinterpret_cast<char*>(entry) + 0x10);
                            if (targ) {
                                CheckAndRetargetTarget(pkg, targ, a_donor, a_player, PackageRetargetType::kCustomTargHandle, PackageRetargetType::kCustomTargObject);
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
               startsWithNoCase(name, "ak__qf_akcontrolquest") ||
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

        std::uint32_t optMask = 0;
        if (_options.conditions) optMask |= (1 << 0);
        if (_options.aliases) optMask |= (1 << 1);
        if (_options.packages) optMask |= (1 << 2);
        if (_options.scripts) optMask |= (1 << 3);

        if (a_serde->OpenRecord(Serialization::kImpersonationDataRecord, Serialization::kSerializationVersion)) {
            a_serde->WriteRecordData(&active, sizeof(active));
            a_serde->WriteRecordData(&donorID, sizeof(donorID));
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

    void ImpersonationManager::Load(SKSE::SerializationInterface* a_serde, std::uint32_t /*a_version*/, std::uint32_t a_length) {
        if (!a_serde) return;

        bool active = false;
        RE::FormID donorID = 0;
        std::uint32_t optMask = 0;

        if (a_length < (sizeof(active) + sizeof(donorID) + sizeof(optMask))) {
            return;
        }

        a_serde->ReadRecordData(&active, sizeof(active));
        a_serde->ReadRecordData(&donorID, sizeof(donorID));
        a_serde->ReadRecordData(&optMask, sizeof(optMask));

        _options.conditions = (optMask & (1 << 0)) != 0;
        _options.aliases = (optMask & (1 << 1)) != 0;
        _options.packages = (optMask & (1 << 2)) != 0;
        _options.scripts = (optMask & (1 << 3)) != 0;

        if (a_serde->ResolveFormID(donorID, donorID)) {
            _savedDonorFormID = donorID;
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
    }

    void ImpersonationManager::OnPostLoadGame() {
        if (_active.load(std::memory_order_relaxed)) {
            auto* donor = _donorActor.load(std::memory_order_relaxed);
            auto* player = _playerActor.load(std::memory_order_relaxed);
            if (donor && player && _options.packages) {
                RetargetPackages(donor, player);
            }
        }
    }

}
