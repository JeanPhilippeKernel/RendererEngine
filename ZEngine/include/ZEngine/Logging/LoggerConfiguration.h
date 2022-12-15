#pragma once
#include <string>
#include <chrono>

namespace ZEngine::Logging
{

    struct LoggerConfiguration
    {
        std::string EngineLoggerName = "ENGINE";
        std::string EditorLoggerName = "EDITOR";

        std::string OutputDirectory = "Logs";
        std::string EngineLogFile   = "engine_dump.log";
        std::string EditorLogFile   = "editor_dump.log";

        std::chrono::milliseconds PeriodicFlush                  = std::chrono::milliseconds(3);
        std::chrono::milliseconds PeriodicInvokeCallbackInterval = std::chrono::milliseconds(1);

        std::function<void(std::string)> MessageCallback;
    };
} // namespace ZEngine::Logging
