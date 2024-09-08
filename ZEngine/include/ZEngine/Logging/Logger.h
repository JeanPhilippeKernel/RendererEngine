#pragma once
#include <Logging/LoggerConfiguration.h>
#include <ZEngineDef.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <map>
#include <mutex>

namespace ZEngine::Logging
{
    struct LogMessage
    {
        float       Color[4] = {0.0f};
        std::string Message;
    };

    struct Logger
    {
        using LogEventHandler = std::function<void(LogMessage)>;

        static void Initialize(const LoggerConfiguration&);
        static void Flush();
        static void Dispose();
        static void AddEventHandler(LogEventHandler handler);

        static void Info(std::string msg);
        static void Trace(std::string msg);
        static void Warn(std::string msg);
        static void Error(std::string msg);
        static void Critical(std::string msg);

    private:
        Logger()              = delete;
        Logger(const Logger&) = delete;

        static spdlog::sink_ptr                                  s_sink;
        static std::recursive_mutex                              s_mutex;
        static std::vector<std::shared_ptr<spdlog::logger>>      s_logger_collection;
        static std::vector<std::pair<uint32_t, LogEventHandler>> s_log_event_handlers;
    };

    inline void Logger::Info(std::string msg)
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
            handler.second(LogMessage{.Color = {0.0f, 1.0f, 0.0f, 1.0f}, .Message = msg});
        }
    }

    inline void Logger::Trace(std::string msg)
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
            handler.second(LogMessage{.Color = {0.5f, 0.5f, 0.5f, 1.0f}, .Message = msg});
        }
    }

    inline void Logger::Warn(std::string msg)
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
            handler.second(LogMessage{.Color = {1.0f, 0.5f, 0.0f, 1.0f}, .Message = msg});
        }
    }

    inline void Logger::Error(std::string msg)
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
            handler.second(LogMessage{.Color = {1.0f, 0.0f, 0.0f, 1.0f}, .Message = msg});
        }
    }

    inline void Logger::Critical(std::string msg)
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
            handler.second(LogMessage{.Color = {1.0f, 0.0f, 1.0f, 1.0f}, .Message = msg});
        }
    }
} // namespace ZEngine::Logging
