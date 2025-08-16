#pragma once
#include <AssetTypes.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/Strings.h>
#include <Core/Memory/Allocator.h>
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <atomic>
#include <future>
#include <mutex>

#define REPORT_LOG(ctx, msg)          \
    {                                 \
        if (m_log_callback)           \
        {                             \
            m_log_callback(ctx, msg); \
        }                             \
    }

namespace ZEngine::Importers
{
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth);

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

    struct AssetImporterOutput
    {
        AssetFileType Type     = AssetFileType::UNKNOWN;
        std::string   Path     = "";
        std::string   RootPath = "";
    };

    struct IAssetImporter
    {
        typedef void (*on_import_complete_fn)(void* const, Core::Containers::ArrayView<AssetImporterOutput> result);
        typedef void (*on_import_progress_fn)(void* const, float progress);
        typedef void (*on_import_error_fn)(void* const, std::string_view error_message);
        typedef void (*on_import_log_fn)(void* const, std::string_view log_message);

        on_import_complete_fn        m_complete_callback = nullptr;
        on_import_progress_fn        m_progress_callback = nullptr;
        on_import_error_fn           m_error_callback    = nullptr;
        on_import_log_fn             m_log_callback      = nullptr;

        std::mutex                   m_mutex;
        std::atomic_bool             m_is_importing = false;

        Core::Memory::ArenaAllocator Arena          = {};
        void*                        Context        = nullptr;

        virtual ~IAssetImporter() {}

        void                       Initialize(Core::Memory::ArenaAllocator* arena);

        virtual void               SetOnCompleteCallback(on_import_complete_fn callback);
        virtual void               SetOnProgressCallback(on_import_progress_fn callback);
        virtual void               SetOnErrorCallback(on_import_error_fn callback);
        virtual void               SetOnLogCallback(on_import_log_fn callback);
        virtual bool               IsImporting();

        virtual std::future<void>  ImportAsync(const char* filename, const ImportConfiguration& config) = 0;

        static AssetImporterOutput SerializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, AssetMesh& data, AssetNodeHierarchy& hierarchies, const ImportConfiguration&);
        static AssetImporterOutput SerializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, AssetMaterial& data, const ImportConfiguration&);
        static AssetImporterOutput SerializeTextureAssetFiles(Core::Memory::ArenaAllocator* arena, Core::Containers::ArrayView<AssetTexture> data, const ImportConfiguration&);

        static void                DeserializeMeshAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMesh&, AssetNodeHierarchy&);
        static void                DeserializeMaterialAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, AssetMaterial&);
        static void                DeserializeTextureAssetFile(Core::Memory::ArenaAllocator* arena, const char* asset_file, Core::Containers::Array<AssetTexture>&);

        static bool                ReadAssetMeshFileHeader(cstring asset_file, AssetMeshFileHeader&);
    };
} // namespace ZEngine::Importers
