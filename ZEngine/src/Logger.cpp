#include <pch.h>
#include <fmt/format.h>
#include <Logging/LoggerDefinition.h>
#include <Logging/Logger.h>
#include <spdlog/async_logger.h>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/details/thread_pool.h>

namespace ZEngine::Logging {

    std::string Logger::m_logger_output_directory   = "Logs";
    std::string Logger::m_engine_logger_output_file = "engine_dump.log";
    std::string Logger::m_editor_logger_output_file = "editor_dump.log";

    Ref<spdlog::logger>           Logger::m_engine_logger;
    Ref<spdlog::logger>           Logger::m_editor_logger;
    std::vector<spdlog::sink_ptr> Logger::m_sink_collection;

    void Logger::Initialize() {
        const auto current_directoy   = std::filesystem::current_path();
        const auto log_directory      = fmt::format("{0}/{1}", current_directoy.string(), m_logger_output_directory);
        auto       log_directory_path = std::filesystem::path(log_directory);
        if (!std::filesystem::exists(log_directory_path)) {
            auto dir_created = std::filesystem::create_directory(log_directory_path);
            if (!dir_created) {
                ZENGINE_CORE_ERROR("failed to create Logs directory");
            }
        }

        spdlog::init_thread_pool(8192, 1);
        m_sink_collection.push_back(
            std::make_shared<spdlog::sinks::daily_file_sink_mt>(fmt::format("{0}/{1}", m_logger_output_directory, m_engine_logger_output_file), 0, 0, true, 0));
        m_sink_collection.push_back(
            std::make_shared<spdlog::sinks::daily_file_sink_mt>(fmt::format("{0}/{1}", m_logger_output_directory, m_editor_logger_output_file), 0, 0, true, 0));

        m_engine_logger = std::make_shared<spdlog::async_logger>("ENGINE", m_sink_collection[0], spdlog::thread_pool());
        m_editor_logger = std::make_shared<spdlog::async_logger>("EDITOR", m_sink_collection[1], spdlog::thread_pool());

        m_engine_logger->info("Engine logger initialized");
        m_editor_logger->info("Editor logger initialized");
        spdlog::register_logger(m_engine_logger);
        spdlog::register_logger(m_editor_logger);

        spdlog::flush_every(std::chrono::seconds(1));
    }

    void Logger::Flush() {
        m_engine_logger->flush();
        m_editor_logger->flush();
    }

    Ref<spdlog::logger>& Logger::GetEngineLogger() {
        return m_engine_logger;
    }

    Ref<spdlog::logger>& Logger::GetEditorLogger() {
        return m_editor_logger;
    }
} // namespace ZEngine::Logging
