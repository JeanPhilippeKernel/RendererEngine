#include <pch.h>
#include <Logging/Logger.h>
#include <Logging/LoggerDefinition.h>
#include <fmt/format.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace ZEngine::Logging
{
    static std::atomic_uint32_t                  g_cookie                     = 0;
    spdlog::sink_ptr                             Logger::s_sink               = nullptr;
    std::recursive_mutex                         Logger::s_mutex              = {};
    std::vector<std::shared_ptr<spdlog::logger>> Logger::s_logger_collection  = {};
    std::map<uint32_t, Logger::LogEventHandler>  Logger::s_log_event_handlers = {};

    void                                         Logger::Initialize(const LoggerConfiguration& configuration)
    {
        const auto current_directoy   = std::filesystem::current_path();
        const auto log_directory      = fmt::format("{0}/{1}", current_directoy.string(), configuration.OutputDirectory);
        auto       log_directory_path = std::filesystem::path(log_directory);
        if (!std::filesystem::exists(log_directory_path))
        {
            bool dir_created = std::filesystem::create_directory(log_directory_path);
            if (!dir_created)
            {
                ZENGINE_CORE_CRITICAL("Failed to create log directory at : {}", log_directory_path.string())
                ZENGINE_EXIT_FAILURE()
            }
        }

        spdlog::init_thread_pool(8192, 2);
        spdlog::flush_every(std::chrono::duration_cast<std::chrono::seconds>(configuration.PeriodicFlush));

        s_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{0}/{1}", configuration.OutputDirectory, configuration.LogFilename), 1024 * 1024, 5, false);
        s_logger_collection.emplace_back(std::make_shared<spdlog::async_logger>(configuration.EngineLoggerName, s_sink, spdlog::thread_pool()));

        for (auto& logger : s_logger_collection)
        {
            spdlog::register_logger(logger);
        }
    }

    void Logger::Flush()
    {
        for (auto& logger : s_logger_collection)
        {
            logger->flush();
        }
    }

    void Logger::Dispose()
    {
        s_log_event_handlers.clear();
        Flush();

        s_logger_collection.clear();
    }

    uint32_t Logger::AddEventHandler(LogEventHandler handler)
    {
        std::unique_lock l(s_mutex);
        uint32_t         cookie      = g_cookie++;
        s_log_event_handlers[cookie] = handler;
        return cookie;
    }

    void Logger::RemoveEventHandler(uint32_t cookie)
    {
        std::unique_lock l(s_mutex);
        s_log_event_handlers.erase(cookie);
    }

} // namespace ZEngine::Logging
