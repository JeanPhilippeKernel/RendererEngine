#pragma once
#include <atomic>
#include <mutex>
#include <future>
#include <string>
#include <Helpers/IntrusivePtr.h>

namespace Tetragrama::Importers
{
    struct IAssetImporter : public ZEngine::Helpers::RefCounted
    {
    protected:
        typedef void (*on_import_complete_fn)(const void* const result);
        typedef void (*on_import_progress_fn)(float progress);
        typedef void (*on_import_error_fn)(std::string_view error_message);

        on_import_complete_fn m_complete_callback{nullptr};
        on_import_progress_fn m_progress_callback{nullptr};
        on_import_error_fn    m_error_callback{nullptr};

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

        virtual bool IsImporting()
        {
            std::lock_guard l(m_mutex);
            return m_is_importing;
        }

        virtual std::future<void> ImportAsync(std::string_view filename) = 0;
    };

} // namespace Tetragrama::Importers
