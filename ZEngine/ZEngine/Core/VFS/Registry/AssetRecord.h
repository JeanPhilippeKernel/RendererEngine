#pragma once
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Managers/AssetTypes.h>
#include <uuid.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    // Lifecycle state machine.
    //   Unregistered ──Register──► Registered
    //   Registered   ──Import ──► Importing
    //   Importing    ──Done   ──► Loaded
    //   Loaded       ──Modify ──► Stale
    //   Stale        ──Import ──► Importing
    //   Any          ──Remove ──► (record erased)
    enum class AssetState : uint8_t
    {
        Unregistered = 0,
        Registered   = 1,
        Importing    = 2,
        Loaded       = 3,
        Stale        = 4,
        Failed       = 5,
    };

    constexpr uint32_t MAX_ASSET_NAME_LEN = 128;

    // Runtime-only meta snapshot — only the fields needed during frame execution.
    // The full MetaFileData (with importer settings) stays on disk in the .meta sidecar.
    struct AssetMetaSnapshot
    {
        uint64_t SourceHash                        = 0; // rapidhash of source bytes; used for stale detection
        char     ImporterName[64]                  = {};
        char     ArtifactPath[MAX_FILE_PATH_COUNT] = {}; // path to compiled artifact (.spv, .zasset, etc.)
    };

    struct AssetRecord
    {
        // Identity
        uuids::uuid           UUID                     = {};
        Managers::AssetType   Type                     = Managers::AssetType::MESH;

        // Location
        Core::VFS::VFSPath    Path                     = {};
        char                  Name[MAX_ASSET_NAME_LEN] = {};

        // Runtime meta snapshot — updated on reimport.
        AssetMetaSnapshot     Meta                     = {};

        // Packed slot index into AssetManager's flat CPU buffer (type in high 4 bits, index in low 28).
        Managers::AssetHandle SlotHandle               = 0;

        // State
        AssetState            State                    = AssetState::Unregistered;

        bool                  IsValid() const
        {
            return !UUID.is_nil();
        }
        bool IsLoaded() const
        {
            return State == AssetState::Loaded;
        }
        bool IsStale() const
        {
            return State == AssetState::Stale;
        }
    };
} // namespace ZEngine::Core::VFS
