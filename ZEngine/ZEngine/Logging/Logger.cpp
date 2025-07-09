#include <pch.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/HashMap.h>
#include <Core/Memory/Allocator.h>
#include <Logging/Logger.h>
#include <Logging/LoggerDefinition.h>
#include <fmt/format.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/rotating_file_sink.h>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Logging
{
    static std::atomic_uint32_t                                                  g_cookie             = 0;
    static spdlog::sink_ptr                                                      s_sink               = nullptr;
    static std::recursive_mutex                                                  s_mutex              = {};
    static ZEngine::Core::Containers::Array<std::shared_ptr<spdlog::logger>>     s_logger_collection  = {};
    static ZEngine::Core::Containers::HashMap<uint32_t, Logger::LogEventHandler> s_log_event_handlers = {};

    void                                                                         Logger::Initialize(void* arena, const LoggerConfiguration& configuration)
    {
        s_logger_collection.init(reinterpret_cast<ArenaAllocator*>(arena), 1);
        s_log_event_handlers.init(reinterpret_cast<ArenaAllocator*>(arena), 3);

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
        s_logger_collection.push(std::make_shared<spdlog::async_logger>(configuration.EngineLoggerName, s_sink, spdlog::thread_pool()));

        for (auto& logger : s_logger_collection)
        {
            spdlog::register_logger(logger);
        }
    }

    void Logger::Info(std::string msg)
    {
        for (auto& logger : s_logger_collection)
        {
            logger->info(msg);
        }

        decltype(s_log_event_handlers) handlers;
        {
            std::unique_lock l(s_mutex);
            handlers = s_log_event_handlers;
        }

        for (const auto& handler : handlers)
        {
            handler.second(
                LogMessage{
                .Color = {0.0f, 1.0f, 0.0f, 1.0f},
                  .Message = msg
            });
        }
    }

    void Logger::Trace(std::string msg)
    {
        for (auto& logger : s_logger_collection)
        {
            logger->trace(msg);
        }

        decltype(s_log_event_handlers) handlers;
        {
            std::unique_lock l(s_mutex);
            handlers = s_log_event_handlers;
        }

        for (const auto& handler : handlers)
        {
            handler.second(
                LogMessage{
                .Color = {0.5f, 0.5f, 0.5f, 1.0f},
                  .Message = msg
            });
        }
    }

    void Logger::Warn(std::string msg)
    {
        for (auto& logger : s_logger_collection)
        {
            logger->warn(msg);
        }

        decltype(s_log_event_handlers) handlers;
        {
            std::unique_lock l(s_mutex);
            handlers = s_log_event_handlers;
        }

        for (const auto& handler : handlers)
        {
            handler.second(
                LogMessage{
                .Color = {1.0f, 0.5f, 0.0f, 1.0f},
                  .Message = msg
            });
        }
    }

    void Logger::Error(std::string msg)
    {
        for (auto& logger : s_logger_collection)
        {
            logger->error(msg);
        }

        decltype(s_log_event_handlers) handlers;
        {
            std::unique_lock l(s_mutex);
            handlers = s_log_event_handlers;
        }

        for (const auto& handler : handlers)
        {
            handler.second(
                LogMessage{
                .Color = {1.0f, 0.0f, 0.0f, 1.0f},
                  .Message = msg
            });
        }
    }

    void Logger::Critical(std::string msg)
    {
        for (auto& logger : s_logger_collection)
        {
            logger->critical(msg);
        }

        decltype(s_log_event_handlers) handlers;
        {
            std::unique_lock l(s_mutex);
            handlers = s_log_event_handlers;
        }

        for (const auto& handler : handlers)
        {
            handler.second(
                LogMessage{
                .Color = {1.0f, 0.0f, 1.0f, 1.0f},
                  .Message = msg
            });
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
        Flush();
        s_log_event_handlers.clear();
        s_logger_collection.clear();
    }

    uint32_t Logger::AddEventHandler(LogEventHandler handler)
    {
        uint32_t cookie = g_cookie++;
        s_log_event_handlers.insert(cookie, handler);
        return cookie;
    }

    void Logger::RemoveEventHandler(uint32_t cookie)
    {
        std::unique_lock l(s_mutex);
        s_log_event_handlers.remove(cookie);
    }
} // namespace ZEngine::Logging
