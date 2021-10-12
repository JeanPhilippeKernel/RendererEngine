#include <Logging/Logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace ZEngine::Logging {
    
    Ref<spdlog::logger> Logger::m_logger;

    void Logger::Initialize() {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        m_logger = std::make_shared<spdlog::logger>("ENGINE", sink);

        spdlog::log(spdlog::level::info, "Logger initialized");
    }

    Ref<spdlog::logger>& Logger::GetCurrent() {
        return m_logger;
    }
}