#pragma once
#include <mutex>
#include <thread>
#include <sstream>
#include <condition_variable>
#include <queue>
#include <regex>
#include <ZEngineDef.h>
#include <spdlog/spdlog.h>
#include <Logging/LoggerConfiguration.h>
#include <spdlog/sinks/base_sink.h>

namespace ZEngine::Logging
{
    struct LoggerMessage
    {
        float       Color[4] = {0.0f};
        std::string Message;
    };

    struct Logger
    {
        Logger()              = delete;
        Logger(const Logger&) = delete;

        static void                            Initialize(const LoggerConfiguration&);
        static void                            Flush();
        static std::shared_ptr<spdlog::logger> GetEngineLogger();
        static std::shared_ptr<spdlog::logger> GetEditorLogger();
        static std::shared_ptr<spdlog::logger> GetLogger();

        static bool GetLogMessage(LoggerMessage& message);

    private:
        static std::shared_ptr<spdlog::logger>  m_engine_logger;
        static std::shared_ptr<spdlog::logger>  m_editor_logger;
        static std::shared_ptr<spdlog::logger>  m_aggregate_logger;
        static std::vector<spdlog::sink_ptr>    m_sink_collection;
        static std::ostringstream               m_log_message;
        static std::atomic_bool                 m_is_invoker_running;
        static std::chrono::milliseconds        m_periodic_invoke_callback_interval;
        static std::function<void(std::string)> m_message_callback;

    private:
        static std::string_view m_error_pattern;
        static std::string_view m_warning_pattern;
        static std::string_view m_info_pattern;
        static std::string_view m_debug_pattern;
        static std::string_view m_trace_pattern;
        static std::string_view m_critical_pattern;
        static std::string_view m_log_pattern;
    };
} // namespace ZEngine::Logging
