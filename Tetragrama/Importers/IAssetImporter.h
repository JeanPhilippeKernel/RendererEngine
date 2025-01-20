#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <atomic>
#include <future>
#include <mutex>
#include <string>

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
        std::string                              Name                    = {};
        std::string                              SerializedMeshesPath    = {};
        std::string                              SerializedMaterialsPath = {};
        std::string                              SerializedModelPath     = {};
    };

    struct ImportConfiguration
    {
        std::string AssetFilename;
        std::string InputBaseAssetFilePath;
        std::string OutputModelFilePath;
        std::string OutputMeshFilePath;
        std::string OutputTextureFilesPath;
        std::string OutputMaterialsPath;
    };

    struct IAssetImporter : public ZEngine::Helpers::RefCounted
    {
    protected:
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

    public:
        virtual ~IAssetImporter() = default;

        void*        Context      = nullptr;

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

        virtual std::future<void> ImportAsync(std::string_view filename, ImportConfiguration config = {}) = 0;

        static void               SerializeImporterData(ImporterData& data, const ImportConfiguration&);
        static ImporterData       DeserializeImporterData(std::string_view model_path, std::string_view mesh_path, std::string_view material_path);
    };

} // namespace Tetragrama::Importers
