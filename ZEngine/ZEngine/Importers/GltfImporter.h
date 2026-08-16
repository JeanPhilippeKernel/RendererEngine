#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <string_view>

namespace ZEngine::Importers
{
    // Handles GLB and GLTF import via fastgltf.
    // Stateless across concurrent imports — each Import() call carves a scratch
    // sub-arena from the importer's own Arena for intermediate geometry data.
    class GltfImporter : public IAssetImporter
    {
    public:
        void                         Initialize(Core::Memory::ArenaAllocator* arena);

        bool                         CanImport(const char* extension) const override;

        // Coordinator path — ingests into AssetManager RAM (no disk output).
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;

        // Importer-panel path — writes .zemesh / .zetextures / .zematerial to disk
        // and ingests into AssetManager so the asset is immediately usable this session.
        void                         ImportFile(const char* filename, const AssetCodec::ImportConfiguration& config, Core::Memory::ArenaAllocator* arena, void* context, void (*on_complete)(void*, Core::Containers::ArrayView<AssetImporterOutput>), void (*on_progress)(void*, float), void (*on_error)(void*, std::string_view), void (*on_log)(void*, std::string_view));

        Core::Memory::ArenaAllocator Arena = {};
    };
} // namespace ZEngine::Importers
