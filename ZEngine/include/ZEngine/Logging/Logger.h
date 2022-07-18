#pragma once
#include <mutex>
#include <ZEngineDef.h>
#include <spdlog/spdlog.h>
#include <Logging/LoggerConfiguration.h>
#include <spdlog/sinks/base_sink.h>

namespace ZEngine::Logging {

    class Logger {
    public:
        Logger()              = delete;
        Logger(const Logger&) = delete;

        static void                 Initialize(const LoggerConfiguration&);
        static void                 Flush();
        static Ref<spdlog::logger>& GetEngineLogger();
        static Ref<spdlog::logger>& GetEditorLogger();
        static Ref<spdlog::logger>& GetLogger();

    private:
        static Ref<spdlog::logger>                           m_engine_logger;
        static Ref<spdlog::logger>                           m_editor_logger;
        static Ref<spdlog::logger>                           m_aggregate_logger;
        static std::vector<spdlog::sink_ptr>                 m_sink_collection;
        static std::thread                                   m_callback_invoker;
        static bool                                          m_is_invoker_running;
        static std::chrono::seconds                          m_periodic_invoke_callback_interval;
        static std::function<void(std::vector<std::string>)> m_message_callback;
    };
} // namespace ZEngine::Logging
