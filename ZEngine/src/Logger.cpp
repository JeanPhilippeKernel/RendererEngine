#include <pch.h>
#include <fmt/format.h>
#include <Logging/LoggerDefinition.h>
#include <Logging/Logger.h>
#include <spdlog/async_logger.h>
#include <spdlog/async.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/details/thread_pool.h>

namespace ZEngine::Logging {
    Ref<spdlog::logger>                           Logger::m_engine_logger;
    Ref<spdlog::logger>                           Logger::m_editor_logger;
    Ref<spdlog::logger>                           Logger::m_aggregate_logger;
    std::vector<spdlog::sink_ptr>                 Logger::m_sink_collection;
    std::thread                                   Logger::m_callback_invoker;
    bool                                          Logger::m_is_invoker_running;
    std::chrono::seconds                          Logger::m_periodic_invoke_callback_interval;
    std::function<void(std::vector<std::string>)> Logger::m_message_callback;

    void Logger::Initialize(const LoggerConfiguration& configuration) {
        const auto current_directoy   = std::filesystem::current_path();
        const auto log_directory      = fmt::format("{0}/{1}", current_directoy.string(), configuration.OutputDirectory);
        auto       log_directory_path = std::filesystem::path(log_directory);
        if (!std::filesystem::exists(log_directory_path)) {
            auto dir_created = std::filesystem::create_directory(log_directory_path);
            if (!dir_created) {
                ZENGINE_CORE_ERROR("failed to create Logs directory");
            }
        }

        spdlog::init_thread_pool(8192, 1);

        m_sink_collection.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{0}/{1}", configuration.OutputDirectory, configuration.EngineLogFile), 1024 * 1024, 5, false));
        m_sink_collection.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{0}/{1}", configuration.OutputDirectory, configuration.EditorLogFile), 1024 * 1024, 5, false));
        m_sink_collection.push_back(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(200));

        m_engine_logger    = std::make_shared<spdlog::async_logger>(configuration.EngineLoggerName, m_sink_collection[0], spdlog::thread_pool());
        m_editor_logger    = std::make_shared<spdlog::async_logger>(configuration.EditorLoggerName, m_sink_collection[1], spdlog::thread_pool());
        m_aggregate_logger = std::make_shared<spdlog::async_logger>("Logger", m_sink_collection[2], spdlog::thread_pool());

        m_engine_logger->info("Engine logger initialized");
        m_editor_logger->info("Editor logger initialized");
        m_aggregate_logger->info("Aggregate logger initialized");

        spdlog::register_logger(m_engine_logger);
        spdlog::register_logger(m_editor_logger);
        spdlog::register_logger(m_aggregate_logger);

        spdlog::flush_every(configuration.PeriodicFlush);

        m_is_invoker_running                = true;
        m_periodic_invoke_callback_interval = configuration.PeriodicInvokeCallbackInterval;
        m_message_callback                  = configuration.MessageCallback;

        if (m_callback_invoker.get_id() == std::thread::id::id()) {
            m_callback_invoker = std::thread([&]() {
                auto current_time = std::chrono::system_clock::now();

                while (m_is_invoker_running) {
                    auto elapsed_time        = std::chrono::system_clock::now() - current_time;
                    auto elapsed_time_second = std::chrono::duration_cast<std::chrono::seconds>(elapsed_time);

                    if (elapsed_time_second.count() >= m_periodic_invoke_callback_interval.count()) {
                        current_time           = std::chrono::system_clock::now();
                        auto& current_sink_ptr = m_aggregate_logger->sinks().at(0);
                        auto  ringbuffer_sink  = reinterpret_cast<spdlog::sinks::ringbuffer_sink_mt*>(current_sink_ptr.get());
                        if (m_message_callback) {
                            auto message = ringbuffer_sink->last_formatted(200);
                            current_sink_ptr.reset(new spdlog::sinks::ringbuffer_sink_mt{200});
                            m_message_callback(message);
                        }
                    }
                }
            });
        }
    }

    void Logger::Flush() {
        m_engine_logger->flush();
        m_editor_logger->flush();
        m_aggregate_logger->flush();

        if (m_callback_invoker.joinable()) {
            m_is_invoker_running = false;
            m_callback_invoker.join();
        }
    }

    Ref<spdlog::logger>& Logger::GetEngineLogger() {
        return m_engine_logger;
    }

    Ref<spdlog::logger>& Logger::GetEditorLogger() {
        return m_editor_logger;
    }

    Ref<spdlog::logger>& Logger::GetLogger() {
        return m_aggregate_logger;
    }
} // namespace ZEngine::Logging
