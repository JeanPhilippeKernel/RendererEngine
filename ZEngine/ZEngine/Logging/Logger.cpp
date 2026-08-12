#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <fmt/format.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Logging
{
    //clang-format off
    static PaddedAtomic<uint32_t>                                        g_cookie                                            = {0};
    static PaddedAtomic<uint32_t>                                        s_ring_buffer_index                                 = {0};
    static spdlog::sink_ptr                                              s_sink                                              = nullptr;
    static std::shared_mutex                                             s_handler_mutex                                     = {};
    static Core::Containers::Array<std::shared_ptr<spdlog::logger>>      s_logger_collection                                 = {};
    static Core::Containers::Array<LogMessage>                           s_log_message_rb                                    = {};
    static Core::Containers::Array<Core::Containers::String>             s_log_raw_string_rb                                 = {};
    static Core::Containers::UnorderedHashMap<uint32_t, LogEventHandler> s_log_event_handlers                                = {};
    static char                                                          s_crash_log_dir[256]                                = {};
    //clang-format on

    // One entry per LogChannel ordinal. Initialized from CMake-defined per-channel minimums
    // (e.g. ZENGINE_LOG_LEVEL_ENGINE → ZENGINE_LOG_LEVEL_WARN → 2 in Release).
    // Logger::Log uses this to gate ring buffer writes and handler dispatch — not just spdlog.
    // Mutable so tests can call SetMinLevel / SetMinLevelAllChannels to lower the gate.
    static int                                                           k_min_level[static_cast<size_t>(LogChannel::COUNT)] = {
        ZENGINE_LOG_LEVEL_ENGINE,
        ZENGINE_LOG_LEVEL_ECS,
        ZENGINE_LOG_LEVEL_RENDER,
        ZENGINE_LOG_LEVEL_PHYSICS,
        ZENGINE_LOG_LEVEL_AUDIO,
        ZENGINE_LOG_LEVEL_NETWORK,
        ZENGINE_LOG_LEVEL_VFS,
        ZENGINE_LOG_LEVEL_ASSET,
        ZENGINE_LOG_LEVEL_UI,
        ZENGINE_LOG_LEVEL_GAME,
    };

    void Logger::Initialize(void* arena, const LoggerConfiguration& configuration)
    {
        auto memory_arena = reinterpret_cast<ArenaAllocator*>(arena);

        s_logger_collection.init(memory_arena, 1);
        s_log_event_handlers.init(memory_arena, 3);

        s_log_message_rb.init(memory_arena, configuration.RingBufferSize, configuration.RingBufferSize);
        s_log_raw_string_rb.init(memory_arena, configuration.RingBufferSize, configuration.RingBufferSize);
        for (uint32_t i = 0; i < configuration.RingBufferSize; ++i)
        {
            s_log_raw_string_rb[i].init(memory_arena, 2048);
        }

        std::strncpy(s_crash_log_dir, configuration.CrashLogDir.c_str(), sizeof(s_crash_log_dir) - 1);

        auto output_path = std::filesystem::path(configuration.OutputDirectory);
        if (output_path.is_relative())
        {
            output_path = std::filesystem::current_path() / output_path;
        }
        auto log_directory_path = output_path;
        if (!std::filesystem::exists(log_directory_path))
        {
            bool dir_created = std::filesystem::create_directory(log_directory_path);
            if (!dir_created)
            {
                ZENGINE_CORE_CRITICAL("Failed to create log directory at : {}", log_directory_path.string())
                ZENGINE_EXIT_FAILURE()
            }
        }

        spdlog::init_thread_pool(8192, 2);
        spdlog::flush_every(std::chrono::duration_cast<std::chrono::seconds>(configuration.PeriodicFlush));
        spdlog::flush_on(spdlog::level::critical);

        s_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>((log_directory_path / configuration.LogFilename).string(), 1024 * 1024, 5, false);
        s_logger_collection.push(std::make_shared<spdlog::async_logger>(configuration.EngineLoggerName, s_sink, spdlog::thread_pool()));

        for (auto& logger : s_logger_collection)
        {
            spdlog::register_logger(logger);
        }
    }

    bool Logger::IsInitialized()
    {
        return s_logger_collection.size() > 0;
    }

    static spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::TRACE:
                return spdlog::level::trace;
            case LogLevel::INFO:
                return spdlog::level::info;
            case LogLevel::WARN:
                return spdlog::level::warn;
            case LogLevel::ERR:
                return spdlog::level::err;
            case LogLevel::CRITICAL:
                return spdlog::level::critical;
            default:
                return spdlog::level::info;
        }
    }

    static void SetColor(LogMessage& msg, LogLevel level)
    {
        switch (level)
        {
            case LogLevel::TRACE:
                msg.Color[0] = 0.60f;
                msg.Color[1] = 0.60f;
                msg.Color[2] = 0.60f;
                msg.Color[3] = 1.0f;
                break; // grey
            case LogLevel::INFO:
                msg.Color[0] = 0.20f;
                msg.Color[1] = 0.80f;
                msg.Color[2] = 0.20f;
                msg.Color[3] = 1.0f;
                break; // green
            case LogLevel::WARN:
                msg.Color[0] = 1.00f;
                msg.Color[1] = 0.65f;
                msg.Color[2] = 0.00f;
                msg.Color[3] = 1.0f;
                break; // orange
            case LogLevel::ERR:
                msg.Color[0] = 0.90f;
                msg.Color[1] = 0.10f;
                msg.Color[2] = 0.10f;
                msg.Color[3] = 1.0f;
                break; // red
            case LogLevel::CRITICAL:
                msg.Color[0] = 0.90f;
                msg.Color[1] = 0.10f;
                msg.Color[2] = 0.10f;
                msg.Color[3] = 1.0f;
                break; // red
            default:
                msg.Color[0] = 1.00f;
                msg.Color[1] = 1.00f;
                msg.Color[2] = 1.00f;
                msg.Color[3] = 1.0f;
                break;
        }
    }

    void Logger::Log(LogChannel channel, LogLevel level, std::string_view msg)
    {
        const int min = k_min_level[static_cast<size_t>(channel)];
        if (static_cast<int>(level) < min)
        {
            return;
        }

        for (auto& logger : s_logger_collection)
        {
            logger->log(ToSpdlogLevel(level), msg);
        }

        LogMessage log_message  = {};
        log_message.Level       = level;
        log_message.Channel     = channel;
        log_message.TimestampNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        SetColor(log_message, level);

        auto buffer_index = s_ring_buffer_index.value.fetch_add(1, std::memory_order_relaxed) % s_log_message_rb.size();
        s_log_raw_string_rb[buffer_index].clear();
        s_log_raw_string_rb[buffer_index].append(msg.data());
        s_log_message_rb[buffer_index]         = log_message;
        s_log_message_rb[buffer_index].Message = s_log_raw_string_rb[buffer_index].c_str();

        {
            std::shared_lock l(s_handler_mutex);
            for (const auto& [id, handler] : s_log_event_handlers)
            {
                handler.Invoke(s_log_message_rb[buffer_index]);
            }
        }
    }

    const char* Logger::LevelToString(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::TRACE:
                return "trace";
            case LogLevel::INFO:
                return "info";
            case LogLevel::WARN:
                return "warn";
            case LogLevel::ERR:
                return "error";
            case LogLevel::CRITICAL:
                return "critical";
            default:
                return "";
        }
    }

    void Logger::SetMinLevel(LogChannel channel, LogLevel level)
    {
        k_min_level[static_cast<size_t>(channel)] = static_cast<int>(level);
    }

    void Logger::SetMinLevelAllChannels(LogLevel level)
    {
        const int v = static_cast<int>(level);
        for (size_t i = 0; i < static_cast<size_t>(LogChannel::COUNT); ++i)
        {
            k_min_level[i] = v;
        }
    }

    void Logger::FlushRingBufferToCrashLog(std::string_view path)
    {
        char path_buf[4096] = {};
        std::memcpy(path_buf, path.data(), std::min(path.size(), sizeof(path_buf) - 1));

        std::FILE* f = std::fopen(path_buf, "w");
        if (!f)
        {
            return;
        }

        // Write in reverse-chronological order: highest index (most recent) first.
        const uint32_t capacity = static_cast<uint32_t>(s_log_message_rb.size());
        const uint32_t head     = s_ring_buffer_index.value.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < capacity; ++i)
        {
            uint32_t          idx = (head - 1 - i + capacity) % capacity;
            const LogMessage& lm  = s_log_message_rb[idx];
            const auto&       str = s_log_raw_string_rb[idx];
            if (str.size() == 0)
            {
                continue;
            }
            std::fprintf(f, "[%s] %.*s\n", LevelToString(lm.Level), static_cast<int>(str.size()), str.c_str());
        }
        std::fclose(f);
    }

    void Logger::FlushRingBufferToCrashLog()
    {
        if (s_crash_log_dir[0] == '\0')
        {
            return;
        }
        char path[4096] = {};
        std::snprintf(path, sizeof(path), "%s/ring_buffer.log", s_crash_log_dir);
        FlushRingBufferToCrashLog(path);
    }

    void Logger::Flush()
    {
        for (auto& logger : s_logger_collection)
        {
            logger->flush();
        }
    }

    void Logger::Dispose()
    {
        Flush();
        s_log_event_handlers.clear();
        for (auto& logger : s_logger_collection)
        {
            spdlog::drop(logger->name());
        }
        s_logger_collection.clear();
        s_crash_log_dir[0] = '\0';
    }

    uint32_t Logger::AddEventHandler(LogEventHandler handler)
    {
        uint32_t cookie = g_cookie.value.fetch_add(1, std::memory_order_relaxed);
        {
            std::unique_lock l(s_handler_mutex);
            s_log_event_handlers.insert(cookie, handler);
        }
        return cookie;
    }

    void Logger::RemoveEventHandler(uint32_t cookie)
    {
        std::unique_lock l(s_handler_mutex);
        s_log_event_handlers.remove(cookie);
    }
} // namespace ZEngine::Logging
