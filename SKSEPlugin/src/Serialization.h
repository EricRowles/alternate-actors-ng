#pragma once

#include <cstdint>

namespace AlternateActors::Serialization {

    inline constexpr std::uint32_t kSerializationVersion = 1;

    inline constexpr std::uint32_t kAliasDataRecord = 'ALIS';
    inline constexpr std::uint32_t kFaceDataRecord = 'FACE';
    inline constexpr std::uint32_t kInventoryDataRecord = 'INVN';
    inline constexpr std::uint32_t kFactionDataRecord = 'FACT';
    inline constexpr std::uint32_t kStatDataRecord = 'STAT';
    inline constexpr std::uint32_t kIdleAIDataRecord = 'IDLE';
    inline constexpr std::uint32_t kImpersonationDataRecord = 'IMPR';

    void Register();
    void SaveCallback(SKSE::SerializationInterface* a_serde);
    void LoadCallback(SKSE::SerializationInterface* a_serde);
    void RevertCallback(SKSE::SerializationInterface* a_serde);

}
