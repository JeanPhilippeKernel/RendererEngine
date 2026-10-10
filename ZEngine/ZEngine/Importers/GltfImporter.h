#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <mutex>
#include <string_view>

namespace ZEngine::Importers
{
    // Handles GLB and GLTF import via fastgltf.
    // Shared scratch is serialized across runtime and editor imports to preserve
    // the bounded arena budget without allowing one import to rewind another.
    class GltfImporter : public IAssetImporter
    {
    public:
        void                         Initialize(Core::Memory::ArenaAllocator* arena);

        bool                         CanImport(const char* extension) const override;

        // Coordinator path — ingests into AssetManager RAM (no disk output).
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;

        // Importer-panel path — writes .zemesh / .zetextures / .zematerial to disk
        // and ingests into AssetManager so the asset is immediately usable this session.
        void                         ImportFile(const char* filename, const AssetCodec::ImportConfiguration& config, Core::Memory::ArenaAllocator* arena, void* context, ImportCompleteCallback on_complete, ImportProgressCallback on_progress, ImportErrorCallback on_error, ImportLogCallback on_log);

        Core::Memory::ArenaAllocator Arena = {};

    private:
        std::mutex m_import_mutex;
    };
} // namespace ZEngine::Importers
