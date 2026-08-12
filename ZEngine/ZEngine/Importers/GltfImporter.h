#pragma once
#include <ZEngine/Importers/IAssetImporter.h>

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
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;

        Core::Memory::ArenaAllocator Arena = {};
    };
} // namespace ZEngine::Importers
