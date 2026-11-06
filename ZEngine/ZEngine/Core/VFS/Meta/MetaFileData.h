#pragma once
#include <ZEngine/Core/VFS/Meta/ImportStatus.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    inline constexpr uint32_t META_MAX_SETTINGS = 32;

    struct MetaKeyValuePair
    {
        char Key[64]    = {};
        char Value[128] = {};
    };

    struct MetaFileData
    {
        uuids::uuid      AssetUUID                         = {};
        char             ImporterName[64]                  = {};
        char             SourcePath[MAX_FILE_PATH_COUNT]   = {}; // absolute native path to the original source file
        uint64_t         SourceHash                        = 0;  // rapidhash of source bytes at last import
        int64_t          LastImportTimeNs                  = 0;
        char             ArtifactPath[MAX_FILE_PATH_COUNT] = {};
        MetaKeyValuePair Settings[META_MAX_SETTINGS]       = {};
        uint32_t         SettingsCount                     = 0;

        // Runtime only — never written to or read from JSON
        ImportStatus     Status                            = ImportStatus::Unknown;
    };

    enum class MetaFileField : uint32_t
    {
        None         = 0,
        UUID         = BIT(0),
        Importer     = BIT(1),
        Settings     = BIT(2),
        SourcePath   = BIT(3),
        SourceHash   = BIT(4),
        ImportTimeNs = BIT(5),
        ArtifactPath = BIT(6),
    };

    // Presence is required by strict consumers: an absent source_hash is not a
    // recorded zero hash. All strings/settings are owned, inline, and untruncated.
    struct StrictMetaFileData
    {
        MetaFileData       Data   = {};
        uint32_t           Fields = 0;

        [[nodiscard]] bool Has(MetaFileField field) const
        {
            const auto bits = static_cast<uint32_t>(field);
            return bits != 0 && (Fields & bits) == bits;
        }
    };
} // namespace ZEngine::Core::VFS
