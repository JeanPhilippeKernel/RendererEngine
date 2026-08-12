#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Rendering/Buffers/Bitmap.h>

namespace ZEngine::Importers::AssetCodec
{
    // Binary codec for ZEngine's on-disk asset formats (.zasset, .zematerial, .zetextures, .zenvmap).
    // These are the cook-time serialization helpers used by format importers to produce
    // the cooked binary artifacts that AssetManager loads at runtime.

    struct ImportConfiguration
    {
        Core::Containers::String AssetName;
        Core::Containers::String OutputAssetFile;
        Core::Containers::String OutputAssetsPath;
        Core::Containers::String InputBaseAssetFilePath;
        Core::Containers::String OutputWorkingSpacePath;
        Core::Containers::String OutputTextureFilesPath;
    };

    struct AssetMeshFileHeader
    {
        uint32_t    MagicNumber = 0xFFFFFF;
        uint32_t    Version     = 0xFFFFFF;
        uuids::uuid Id          = {};
    };

    struct EnvironmentMapFileHeader
    {
        uint32_t MagicNumber    = 0;
        uint32_t Version        = 0;
        int32_t  FaceWidth      = 0;
        int32_t  FaceHeight     = 0;
        int32_t  Channel        = 0;
        int32_t  LayerCount     = 0;
        uint64_t BufferByteSize = 0;
    };

    AssetImporterOutput        SerializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, AssetMesh& mesh, AssetNodeHierarchy& hierarchies, const ImportConfiguration& config);

    AssetImporterOutput        SerializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, AssetMaterial& material, const ImportConfiguration& config);

    AssetImporterOutput        SerializeTextureAssetFiles(Core::Memory::ArenaAllocator* arena, Core::Containers::ArrayView<AssetTexture> textures, const ImportConfiguration& config);

    AssetImporterOutput        SerializeEnvironmentMapFile(const Rendering::Buffers::Bitmap& cubemap, const ImportConfiguration& config);

    // VFS-based — writes through IVFSContext using atomic .tmp → rename protocol.
    // out_path: the VFS path to write (e.g. project://_cache/envmaps/<uuid>.zenvmap)
    Core::VFS::VFSResult<void> SerializeEnvironmentMapFileVFS(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& out_path, const Rendering::Buffers::Bitmap& cubemap);

    void                       DeserializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMesh& mesh, AssetNodeHierarchy& hierarchies);

    void                       DeserializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMaterial& material);

    void                       DeserializeTextureAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, Core::Containers::Array<AssetTexture>& textures);

    bool                       ReadAssetMeshFileHeader(const char* asset_file, AssetMeshFileHeader& header);

    bool                       DeserializeEnvironmentMapFile(const char* zenvmap_file, Rendering::Buffers::Bitmap& out_cubemap);

    bool                       ReadEnvironmentMapFileHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header);

} // namespace ZEngine::Importers::AssetCodec
