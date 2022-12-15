#include <pch.h>
#include <fmt/format.h>
#include <Logging/LoggerDefinition.h>
#include <Logging/Logger.h>
#include <spdlog/async_logger.h>
#include <spdlog/async.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/details/thread_pool.h>

namespace ZEngine::Logging
{
    Ref<spdlog::logger>              Logger::m_engine_logger;
    Ref<spdlog::logger>              Logger::m_editor_logger;
    Ref<spdlog::logger>              Logger::m_aggregate_logger;
    std::vector<spdlog::sink_ptr>    Logger::m_sink_collection;
    std::ostringstream               Logger::m_log_message;
    std::atomic_bool                 Logger::m_is_invoker_running;
    std::chrono::milliseconds        Logger::m_periodic_invoke_callback_interval;
    std::function<void(std::string)> Logger::m_message_callback;

    std::string_view Logger::m_error_pattern{"[error]"};
    std::string_view Logger::m_warning_pattern{"[warning]"};
    std::string_view Logger::m_info_pattern{"[info]"};
    std::string_view Logger::m_debug_pattern{"[debug]"};
    std::string_view Logger::m_trace_pattern{"[trace]"};
    std::string_view Logger::m_critical_pattern{"[critical]"};
    std::string_view Logger::m_log_pattern{"[log]"};

    void Logger::Initialize(const LoggerConfiguration& configuration)
    {
        const auto current_directoy   = std::filesystem::current_path();
        const auto log_directory      = fmt::format("{0}/{1}", current_directoy.string(), configuration.OutputDirectory);
        auto       log_directory_path = std::filesystem::path(log_directory);
        if (!std::filesystem::exists(log_directory_path))
        {
            auto dir_created = std::filesystem::create_directory(log_directory_path);
            if (!dir_created)
            {
                ZENGINE_EXIT_FAILURE()
                // ZENGINE_CORE_ERROR("failed to create Logs directory")
            }
        }

        spdlog::init_thread_pool(8192, 1);

        m_sink_collection.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{0}/{1}", configuration.OutputDirectory, configuration.EngineLogFile), 1024 * 1024, 5, false));
        m_sink_collection.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{0}/{1}", configuration.OutputDirectory, configuration.EditorLogFile), 1024 * 1024, 5, false));

        m_sink_collection.push_back(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_log_message));

        m_engine_logger    = std::make_shared<spdlog::async_logger>(configuration.EngineLoggerName, m_sink_collection[0], spdlog::thread_pool());
        m_editor_logger    = std::make_shared<spdlog::async_logger>(configuration.EditorLoggerName, m_sink_collection[1], spdlog::thread_pool());
        m_aggregate_logger = std::make_shared<spdlog::async_logger>("Logger", m_sink_collection[2], spdlog::thread_pool());

        m_engine_logger->info("Engine logger initialized");
        m_editor_logger->info("Editor logger initialized");
        m_aggregate_logger->info("Aggregate logger initialized");

        spdlog::register_logger(m_engine_logger);
        spdlog::register_logger(m_editor_logger);
        spdlog::register_logger(m_aggregate_logger);

        spdlog::flush_every(std::chrono::duration_cast<std::chrono::seconds>(configuration.PeriodicFlush));

        m_is_invoker_running                = true;
        m_periodic_invoke_callback_interval = configuration.PeriodicInvokeCallbackInterval;
        m_message_callback                  = configuration.MessageCallback;
    }

    void Logger::Flush()
    {
        m_engine_logger->flush();
        m_editor_logger->flush();
        m_aggregate_logger->flush();
    }

    Ref<spdlog::logger> Logger::GetEngineLogger()
    {
        return m_engine_logger;
    }

    Ref<spdlog::logger> Logger::GetEditorLogger()
    {
        return m_editor_logger;
    }

    Ref<spdlog::logger> Logger::GetLogger()
    {
        return m_aggregate_logger;
    }

    bool Logger::GetLogMessage(LoggerMessage& message)
    {
        auto message_string = m_log_message.str();
        if (message_string.empty())
        {
            return false;
        }

        if (std::strstr(message_string.c_str(), m_error_pattern.data()))
        {
            message = LoggerMessage{.Color = {1.0f, 0.4f, 0.4f, 1.0f}, .Message = std::move(message_string)};
        }

        else if (std::strstr(message_string.c_str(), m_warning_pattern.data()))
        {
            message = LoggerMessage{.Color = {1.0f, 0.87f, 0.37f, 1.f}, .Message = std::move(message_string)};
        }

        else if (std::strstr(message_string.c_str(), m_debug_pattern.data()))
        {
            message = LoggerMessage{.Color = {0.94f, 0.39f, 0.13f, 1.0f}, .Message = std::move(message_string)};
        }

        else if (std::strstr(message_string.c_str(), m_info_pattern.data()))
        {
            message = LoggerMessage{.Color = {0.46f, 0.96f, 0.46f, 1.f}, .Message = std::move(message_string)};
        }

        else if (std::strstr(message_string.c_str(), m_trace_pattern.data()))
        {
            message = LoggerMessage{.Color = {1.0f, 1.0f, 1.0f, 1.f}, .Message = std::move(message_string)};
        }

        else if (std::strstr(message_string.c_str(), m_critical_pattern.data()))
        {
            message = LoggerMessage{.Color = {0.47f, 0.47f, 0.69f, 0.40f}, .Message = std::move(message_string)};
        }

        else if (std::strstr(message_string.c_str(), m_log_pattern.data()))
        {
            message = LoggerMessage{.Color = {1.f, 1.f, 1.f, 0.5f}, .Message = std::move(message_string)};
        }

        return true;
    }
} // namespace ZEngine::Logging
