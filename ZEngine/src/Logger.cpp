#include <pch.h>
#include <Logging/Logger.h>

namespace ZEngine::Logging {

    Ref<spdlog::logger> Logger::m_logger;
    Ref<spdlog::logger> Logger::m_editor_logger;

    void Logger::Initialize() {
        auto sink       = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        m_logger        = std::make_shared<spdlog::logger>("ENGINE", sink);
        m_editor_logger = std::make_shared<spdlog::logger>("EDITOR", sink);

        spdlog::log(spdlog::level::info, "Engine logger initialized");
        spdlog::log(spdlog::level::info, "Editor logger initialized");
    }

    Ref<spdlog::logger>& Logger::GetEngineLogger() {
        return m_logger;
    }

    Ref<spdlog::logger>& Logger::GetEditorLogger() {
        return m_editor_logger;
    }
} // namespace ZEngine::Logging
