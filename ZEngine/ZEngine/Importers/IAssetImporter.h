#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Importers/AssetTypes.h>

namespace ZEngine::Importers
{
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth);

    // Lightweight per-format importer interface used by ImportCoordinator.
    // Implementations must be stateless — a single instance handles concurrent imports.
    // UUID must NOT be generated inside Import(); read it from meta.AssetUUID which was
    // assigned by MetaFileIO at scan time and is stable across reimports.
    struct IAssetImporter
    {
        virtual ~IAssetImporter()                                                                                                                   = default;

        // Returns true if this importer handles the given extension (without dot, e.g. "glb").
        // Must be cheap and stateless — called once per importer during routing.
        virtual bool                       CanImport(const char* extension) const                                                                   = 0;

        // Perform the full import through the VFS. Must not throw.
        // On failure: return a Fail result; ImportCoordinator records the diagnostic.
        virtual Core::VFS::VFSResult<void> Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) = 0;
    };
} // namespace ZEngine::Importers
