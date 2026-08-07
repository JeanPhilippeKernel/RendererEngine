#pragma once
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <mutex>
#include <shared_mutex>

namespace ZEngine::Logging
{
    enum class LogChannel : uint8_t
    {
        ENGINE  = 0,
        ECS     = 1,
        RENDER  = 2,
        PHYSICS = 3,
        AUDIO   = 4,
        NETWORK = 5,
        VFS     = 6,
        ASSET   = 7,
        UI      = 8,
        GAME    = 9,
        COUNT   = 10
    };

    enum class LogLevel : uint8_t
    {
        TRACE    = 0,
        INFO     = 1,
        WARN     = 2,
        ERR      = 3,
        CRITICAL = 4
    };

    struct LogMessage
    {
        float       Color[4]    = {0.0f};
        const char* Message     = nullptr;
        uint64_t    TimestampNs = 0;
        uint16_t    MessageLen  = 0;
        LogLevel    Level       = LogLevel::TRACE;
        LogChannel  Channel     = LogChannel::ENGINE;
    };

    struct LogEventHandler
    {
        using LogEventFn   = void (*)(void* context, const LogMessage& message);
        LogEventFn Fn      = nullptr;
        void*      Context = nullptr;

        bool       IsValid() const
        {
            return Fn != nullptr;
        }

        void Invoke(const LogMessage& message) const
        {
            if (IsValid())
            {
                Fn(Context, message);
            }
        }
    };

    struct Logger
    {

        static void        Initialize(void* arena, const LoggerConfiguration&);
        static bool        IsInitialized();
        static void        Flush();
        static void        Dispose();
        static uint32_t    AddEventHandler(LogEventHandler handler);
        static void        RemoveEventHandler(uint32_t cookie);

        static void        Log(LogChannel channel, LogLevel level, std::string_view msg);
        static const char* LevelToString(LogLevel level);
        static void        FlushRingBufferToCrashLog(std::string_view path);
        static void        FlushRingBufferToCrashLog(); // uses CrashLogDir from LoggerConfiguration

    private:
        Logger()              = delete;
        Logger(const Logger&) = delete;
    };
} // namespace ZEngine::Logging
