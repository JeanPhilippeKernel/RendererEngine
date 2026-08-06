#pragma once
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <map>
#include <mutex>

namespace ZEngine::Logging
{
    enum class LogMessageType : uint8_t
    {
        Info = 0,
        Trace,
        Warn,
        Error,
        Critical
    };

    struct LogMessage
    {
        float          Color[4] = {0.0f};
        LogMessageType Type     = LogMessageType::Info;
        std::string    Message  = "";
    };

    struct Logger
    {
        using LogEventHandler = std::function<void(LogMessage)>;

        static void        Initialize(void* arena, const LoggerConfiguration&);
        static bool        IsInitialized();
        static void        Flush();
        static void        Dispose();
        static uint32_t    AddEventHandler(LogEventHandler handler);
        static void        RemoveEventHandler(uint32_t cookie);

        static void        Info(std::string msg);
        static void        Trace(std::string msg);
        static void        Warn(std::string msg);
        static void        Error(std::string msg);
        static void        Critical(std::string msg);

        static const char* MessageTypeToString(LogMessageType type);

    private:
        Logger()              = delete;
        Logger(const Logger&) = delete;
    };
} // namespace ZEngine::Logging
