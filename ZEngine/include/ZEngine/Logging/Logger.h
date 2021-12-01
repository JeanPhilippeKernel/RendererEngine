#pragma once
#include <ZEngineDef.h>
#include <spdlog/spdlog.h>

namespace ZEngine::Logging {

    class Logger {
    public:
        Logger()              = delete;
        Logger(const Logger&) = delete;

        static void                 Initialize();
        static Ref<spdlog::logger>& GetEngineLogger();
        static Ref<spdlog::logger>& GetEditorLogger();

    private:
        static Ref<spdlog::logger> m_logger;
        static Ref<spdlog::logger> m_editor_logger;
    };
} // namespace ZEngine::Logging
