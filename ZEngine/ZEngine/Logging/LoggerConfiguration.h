#pragma once
#include <chrono>
#include <cstddef>
#include <string>

namespace ZEngine::Logging
{
    struct LoggerConfiguration
    {
        std::string               EngineLoggerName = "ENGINE";
        std::string               OutputDirectory  = "Logs";
        std::string               LogFilename      = "engine_dump.log";
        std::string               CrashLogDir      = "CrashDumps";
        std::chrono::milliseconds PeriodicFlush    = std::chrono::milliseconds(1);
        size_t                    RingBufferSize   = 1024;
        size_t                    FileMaxBytes     = 1024 * 1024;
        size_t                    FileMaxRotations = 5;
    };
} // namespace ZEngine::Logging
