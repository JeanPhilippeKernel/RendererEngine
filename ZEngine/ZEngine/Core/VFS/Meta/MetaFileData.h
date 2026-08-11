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
        uint64_t         SourceHash                        = 0; // rapidhash of source bytes at last import
        int64_t          LastImportTimeNs                  = 0;
        char             ArtifactPath[MAX_FILE_PATH_COUNT] = {};
        MetaKeyValuePair Settings[META_MAX_SETTINGS]       = {};
        uint32_t         SettingsCount                     = 0;

        // Runtime only — never written to or read from JSON
        ImportStatus     Status                            = ImportStatus::Unknown;
    };
} // namespace ZEngine::Core::VFS
