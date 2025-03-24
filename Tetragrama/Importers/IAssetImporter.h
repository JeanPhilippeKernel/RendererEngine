#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <ZEngine/Core/Container/Array.h>
#include <ZEngine/Core/Container/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
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

namespace Tetragrama::Importers
{
    struct ImporterData
    {
        /* Meshes Properties*/
        uint32_t                                 VertexOffset            = 0;
        uint32_t                                 IndexOffset             = 0;
        ZEngine::Rendering::Scenes::SceneRawData Scene                   = {};
        ZEngine::Core::Container::String         Name                    = {};
        ZEngine::Core::Container::String         SerializedMeshesPath    = {};
        ZEngine::Core::Container::String         SerializedMaterialsPath = {};
        ZEngine::Core::Container::String         SerializedModelPath     = {};
    };

    struct ImportConfiguration
    {
        ZEngine::Core::Container::String AssetFilename;
        ZEngine::Core::Container::String InputBaseAssetFilePath;
        ZEngine::Core::Container::String OutputModelFilePath;
        ZEngine::Core::Container::String OutputMeshFilePath;
        ZEngine::Core::Container::String OutputTextureFilesPath;
        ZEngine::Core::Container::String OutputMaterialsPath;
    };

    struct IAssetImporter
    {
        typedef void (*on_import_complete_fn)(void* const, ImporterData&& result);
        typedef void (*on_import_progress_fn)(void* const, float progress);
        typedef void (*on_import_error_fn)(void* const, std::string_view error_message);
        typedef void (*on_import_log_fn)(void* const, std::string_view log_message);

        on_import_complete_fn m_complete_callback{nullptr};
        on_import_progress_fn m_progress_callback{nullptr};
        on_import_error_fn    m_error_callback{nullptr};
        on_import_log_fn      m_log_callback{nullptr};

        std::mutex            m_mutex;
        std::atomic_bool      m_is_importing{false};

        virtual ~IAssetImporter()                     = default;

        ZEngine::Core::Memory::ArenaAllocator Arena   = {};
        void*                                 Context = nullptr;

        void                                  Initialize(ZEngine::Core::Memory::ArenaAllocator* arena)
        {
            arena->CreateSubArena(ZMega(1), &Arena);
        }

        virtual void SetOnCompleteCallback(on_import_complete_fn callback)
        {
            m_complete_callback = callback;
        }

        virtual void SetOnProgressCallback(on_import_progress_fn callback)
        {
            m_progress_callback = callback;
        }

        virtual void SetOnErrorCallback(on_import_error_fn callback)
        {
            m_error_callback = callback;
        }

        virtual void SetOnLogCallback(on_import_log_fn callback)
        {
            m_log_callback = callback;
        }

        virtual bool IsImporting()
        {
            std::lock_guard l(m_mutex);
            return m_is_importing;
        }

        virtual std::future<void> ImportAsync(std::string_view filename, ImportConfiguration config = {})                                                                                        = 0;
        virtual void              SerializeImporterData(ZEngine::Core::Memory::ArenaAllocator* arena, ImporterData& data, const ImportConfiguration&)                                            = 0;
        virtual ImporterData      DeserializeImporterData(ZEngine::Core::Memory::ArenaAllocator* arena, std::string_view model_path, std::string_view mesh_path, std::string_view material_path) = 0;
    };

} // namespace Tetragrama::Importers
