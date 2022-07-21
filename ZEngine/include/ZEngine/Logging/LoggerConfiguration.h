#pragma once
#include <string>
#include <chrono>

namespace ZEngine::Logging {

    struct LoggerConfiguration {
        std::string EngineLoggerName = "ENGINE";
        std::string EditorLoggerName = "EDITOR";

        std::string OutputDirectory = "Logs";
        std::string EngineLogFile   = "engine_dump.log";
        std::string EditorLogFile   = "editor_dump.log";

        std::chrono::seconds PeriodicFlush                  = std::chrono::seconds(1);
        std::chrono::seconds PeriodicInvokeCallbackInterval = std::chrono::seconds(1);

        std::function<void(std::vector<std::string>)> MessageCallback;
    };
} // namespace ZEngine::Logging
