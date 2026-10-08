#include "PCH.h"
#include <ShlObj.h>
#include <spdlog/sinks/msvc_sink.h>
#include "Papyrus.h"
#include "Serialization.h"
#include "IdleAIManager.h"
#include "ImpersonationManager.h"

SKSEPluginInfo(
    .Version = { 1, 0, 0, 0 },
    .Name = "AlternateActorsNG",
    .Author = "Antigravity",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
)

void InitializeLogging() {
    std::vector<spdlog::sink_ptr> sinks;

    // 1. Direct resolution via Windows Known Folders (Documents -> My Games\Skyrim Special Edition\SKSE)
    wchar_t* localDocPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &localDocPath))) {
        std::filesystem::path docDir(localDocPath);
        CoTaskMemFree(localDocPath);

        auto seDir = docDir / "My Games" / "Skyrim Special Edition" / "SKSE";
        std::error_code ec;
        std::filesystem::create_directories(seDir, ec);
        auto seLog = seDir / "AlternateActorsNG.log";

        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(seLog.string(), true));
        } catch (...) {}
    }

    // 2. CommonLibSSE log directory
    auto path = SKSE::log::log_directory();
    if (path) {
        auto pathStr = path->string();
        if (pathStr.find("Skyrim.INI") != std::string::npos) {
            auto fixed = std::filesystem::path(pathStr.substr(0, pathStr.find("Skyrim.INI"))) / "Skyrim Special Edition" / "SKSE";
            std::error_code ec;
            std::filesystem::create_directories(fixed, ec);
            fixed /= "AlternateActorsNG.log";
            try {
                sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(fixed.string(), true));
            } catch (...) {}
        }

        *path /= "AlternateActorsNG.log"sv;
        std::error_code ec;
        std::filesystem::create_directories(path->parent_path(), ec);
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
        } catch (...) {}
    }

    // 3. Visual Studio / Debugger Output Sink
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

    if (sinks.empty()) {
        return;
    }

    auto log = std::make_shared<spdlog::logger>("global log"s, sinks.begin(), sinks.end());
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);

    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v"s);
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
    if (!a_msg) return;

    if (a_msg->type == SKSE::MessagingInterface::kInputLoaded || a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
        AlternateActors::IdleAIManager::GetSingleton().Initialize();
        AlternateActors::ImpersonationManager::GetSingleton().Initialize();
    } else if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame) {
        AlternateActors::ImpersonationManager::GetSingleton().OnPostLoadGame();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLogging();

    auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    SKSE::log::info("{} v{} loading...", plugin->GetName(), plugin->GetVersion().string());

    SKSE::Init(a_skse);

    // Register Papyrus Native functions
    auto* papyrus = SKSE::GetPapyrusInterface();
    if (papyrus) {
        papyrus->Register(AlternateActors::Papyrus::Register);
    } else {
        SKSE::log::error("Failed to obtain SKSE Papyrus Interface!");
        return false;
    }

    // Register SKSE Messaging Listener
    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(MessageHandler);
    }

    // Register Co-Save Serialization
    AlternateActors::Serialization::Register();

    SKSE::log::info("{} loaded successfully.", plugin->GetName());
    return true;
}
