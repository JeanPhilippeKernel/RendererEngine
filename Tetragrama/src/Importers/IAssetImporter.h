#pragma once
#include <atomic>
#include <mutex>
#include <future>
#include <string>
#include <Helpers/IntrusivePtr.h>

#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Scenes/GraphicScene.h>

#define REPORT_LOG(msg)          \
    {                            \
        if (m_log_callback)      \
        {                        \
            m_log_callback(msg); \
        }                        \
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
        typedef void (*on_import_complete_fn)(ImporterData&& result);
        typedef void (*on_import_progress_fn)(float progress);
        typedef void (*on_import_error_fn)(std::string_view error_message);
        typedef void (*on_import_log_fn)(std::string_view log_message);

        on_import_complete_fn m_complete_callback{nullptr};
        on_import_progress_fn m_progress_callback{nullptr};
        on_import_error_fn    m_error_callback{nullptr};
        on_import_log_fn      m_log_callback{nullptr};

        std::mutex       m_mutex;
        std::atomic_bool m_is_importing{false};

    public:
        virtual ~IAssetImporter() = default;

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

        static void SerializeImporterData(ImporterData& data, const ImportConfiguration&);
        static void SerializeMapData(std::ostream&, const std::unordered_map<uint32_t, uint32_t>&);
        static void SerializeStringArrayData(std::ostream&, std::span<std::string>);
    };

} // namespace Tetragrama::Importers
