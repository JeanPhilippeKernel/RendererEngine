#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>

namespace ZEngine::Importers
{
    class FbxImporter : public IAssetImporter
    {
    public:
        void                         Initialize(Core::Memory::ArenaAllocator* arena);

        Core::Memory::ArenaAllocator Arena = {};

        bool                         CanImport(const char* extension) const override;
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;
        void                         ImportFile(const char* filename, const AssetCodec::ImportConfiguration& config, Core::Memory::ArenaAllocator* arena, void* context, ImportCompleteCallback on_complete, ImportProgressCallback on_progress, ImportErrorCallback on_error, ImportLogCallback on_log);

    private:
        void CopyTextureFiles(Core::Memory::ArenaAllocator* arena, Core::Containers::Array<AssetTexture>& textures, const AssetCodec::ImportConfiguration& config);
    };
} // namespace ZEngine::Importers
