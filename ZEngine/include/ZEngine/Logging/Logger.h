#pragma once
#include <mutex>
#include <ZEngineDef.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

namespace ZEngine::Logging {

    class Logger {
    public:
        Logger()              = delete;
        Logger(const Logger&) = delete;

        static void                 Initialize();
        static void                 Flush();
        static Ref<spdlog::logger>& GetEngineLogger();
        static Ref<spdlog::logger>& GetEditorLogger();

    private:
        static std::string                   m_logger_output_directory;
        static std::string                   m_engine_logger_output_file;
        static std::string                   m_editor_logger_output_file;
        static Ref<spdlog::logger>           m_engine_logger;
        static Ref<spdlog::logger>           m_editor_logger;
        static std::vector<spdlog::sink_ptr> m_sink_collection;
    };
} // namespace ZEngine::Logging
