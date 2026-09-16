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
    inline constexpr uint32_t ENVIRONMENT_MAP_FILE_VERSION     = 2;
    inline constexpr uint32_t ENVIRONMENT_MAP_IMPORTER_VERSION = 1;
    inline constexpr uint32_t ENVIRONMENT_MAP_MAX_FACE_SIZE    = 1024;

    enum class EnvironmentMapColorSpace : uint32_t
    {
        LinearScene = 1,
    };

    enum class EnvironmentMapOrientation : uint32_t
    {
        RendererCanonical = 1,
    };

    enum class EnvironmentMapMipPolicy : uint32_t
    {
        GenerateOnGpu = 1,
    };

    // Binary codec for ZEngine's on-disk asset formats (.zasset, .zematerial, .zetextures, .zenvmap).
    // These are the cook-time serialization helpers used by format importers to produce
    // the cooked binary artifacts that AssetManager loads at runtime.

    struct ImportOptions
    {
        float   UniformScale    = 1.0f;
        bool    AxisUpIsZ       = false;
        bool    FlipUVs         = false;
        uint8_t NormalsMode     = 1; // 0 = off, 1 = flat, 2 = smooth
        bool    MergeVertices   = true;
        bool    ImportMaterials = true;
        bool    ImportTextures  = true;
    };

    struct ImportConfiguration
    {
        Core::Containers::String AssetName;
        Core::Containers::String OutputAssetFile;
        Core::Containers::String OutputAssetsPath;
        Core::Containers::String OutputMaterialPath;
        Core::Containers::String InputBaseAssetFilePath;
        Core::Containers::String OutputWorkingSpacePath;
        Core::Containers::String OutputTextureFilesPath;
        Core::VFS::IVFSContext*  VFS     = nullptr;
        ImportOptions            Options = {};
    };

    struct AssetMeshFileHeader
    {
        uint32_t    MagicNumber = 0xFFFFFF;
        uint32_t    Version     = 0xFFFFFF;
        uuids::uuid Id          = {};
    };

    struct EnvironmentMapFileHeader
    {
        uint32_t MagicNumber     = 0;
        uint32_t Version         = 0;
        uint32_t HeaderByteSize  = 0;
        uint32_t ImporterVersion = 0;
        uint64_t SourceHash      = 0;
        uint32_t FaceWidth       = 0;
        uint32_t FaceHeight      = 0;
        uint32_t Channel         = 0;
        uint32_t LayerCount      = 0;
        uint32_t MipCount        = 0;
        uint32_t ColorSpace      = 0;
        uint32_t Orientation     = 0;
        uint32_t MipPolicy       = 0;
        float    Exposure        = 1.0f;
        uint32_t Reserved        = 0;
        uint64_t BufferByteSize  = 0;
    };
    static_assert(sizeof(EnvironmentMapFileHeader) == 72, "Environment-map artifact header must remain stable");

    struct EnvironmentMapCookMetadata
    {
        uint64_t SourceHash      = 0;
        float    Exposure        = 1.0f;
        uint32_t ImporterVersion = ENVIRONMENT_MAP_IMPORTER_VERSION;
    };

    AssetImporterOutput        SerializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, AssetMesh& mesh, AssetNodeHierarchy& hierarchies, const ImportConfiguration& config);

    AssetImporterOutput        SerializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, AssetMaterial& material, const ImportConfiguration& config);

    AssetImporterOutput        SerializeTextureAssetFiles(Core::Memory::ArenaAllocator* arena, Core::Containers::ArrayView<AssetTexture> textures, const ImportConfiguration& config);

    [[nodiscard]] uint32_t     GetEnvironmentMapFullMipCount(uint32_t face_size);
    [[nodiscard]] bool         IsEnvironmentMapFileHeaderValid(const EnvironmentMapFileHeader& header);
    [[nodiscard]] bool         DoesEnvironmentMapHeaderMatchSource(const EnvironmentMapFileHeader& header, uint64_t source_hash);

    // VFS-based — writes through IVFSContext using atomic .tmp → rename protocol.
    // out_path: the VFS path to write (e.g. project://_cache/envmaps/<uuid>.zenvmap)
    Core::VFS::VFSResult<void> SerializeEnvironmentMapFileVFS(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& out_path, const Rendering::Buffers::Bitmap& cubemap, const EnvironmentMapCookMetadata& metadata = {});

    void                       DeserializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMesh& mesh, AssetNodeHierarchy& hierarchies);

    void                       DeserializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMaterial& material);

    void                       DeserializeTextureAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, Core::Containers::Array<AssetTexture>& textures);

    bool                       ReadAssetMeshFileHeader(const char* asset_file, AssetMeshFileHeader& header);

    bool                       DeserializeEnvironmentMapFile(const char* zenvmap_file, Rendering::Buffers::Bitmap& out_cubemap);

    bool                       ReadEnvironmentMapFileHeader(const char* zenvmap_file, EnvironmentMapFileHeader& out_header);

} // namespace ZEngine::Importers::AssetCodec
