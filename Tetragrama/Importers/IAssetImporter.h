#pragma once
#include <AssetTypes.h>
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
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
    int AddNode(AssetNodeHierarchy& hierarchy, int parent, int depth);

    struct ImportConfiguration
    {
        ZEngine::Core::Containers::String AssetName;
        ZEngine::Core::Containers::String OutputAssetFile;
        ZEngine::Core::Containers::String OutputAssetsPath;
        ZEngine::Core::Containers::String InputBaseAssetFilePath;
        ZEngine::Core::Containers::String OutputWorkingSpacePath;
        ZEngine::Core::Containers::String OutputTextureFilesPath;
    };

    struct IAssetImporter
    {
        typedef void (*on_import_complete_fn)(void* const, const char* result);
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
            arena->CreateSubArena(ZMega(5), &Arena);
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
            return m_is_importing.load(std::memory_order_acquire);
        }

        virtual std::future<void> ImportAsync(const char* filename, ImportConfiguration& config) = 0;
    };

} // namespace Tetragrama::Importers
