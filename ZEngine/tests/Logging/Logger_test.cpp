#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Logging;

class LoggerTest : public ::testing::Test
{
protected:
    MemoryManager      m_manager{};
    ArenaAllocator     m_logger_arena{};
    static std::string s_log_dir;
    static std::string s_crash_dir;

    void               SetUp() override
    {
        m_manager.Initialize(ZMega(4), {});
        m_manager.MainArena.CreateSubArena(ZMega(2), &m_logger_arena);

        auto tmp    = std::filesystem::temp_directory_path();
        s_log_dir   = (tmp / "zengine_logger_test_logs").string();
        s_crash_dir = (tmp / "zengine_logger_test_crash").string();
        std::filesystem::create_directories(s_log_dir);
        std::filesystem::create_directories(s_crash_dir);

        LoggerConfiguration cfg{};
        cfg.OutputDirectory = s_log_dir;
        cfg.RingBufferSize  = 64;
        cfg.CrashLogDir     = s_crash_dir;
        Logger::Initialize(&m_logger_arena, cfg);
        Logger::SetMinLevelAllChannels(LogLevel::TRACE);
    }

    void TearDown() override
    {
        Logger::Dispose();
        m_logger_arena.Shutdown();
        m_manager.Shutdown();
    }
};

std::string LoggerTest::s_log_dir;
std::string LoggerTest::s_crash_dir;

TEST_F(LoggerTest, IsInitializedAfterInit)
{
    EXPECT_TRUE(Logger::IsInitialized());
}

TEST_F(LoggerTest, HandlerReceivesMessage)
{
    struct Capture
    {
        LogMessage last{};
        int        count = 0;
    };

    Capture cap{};
    auto    cookie = Logger::AddEventHandler({[](void* ctx, const LogMessage& msg) {
                                                 auto* c  = static_cast<Capture*>(ctx);
                                                 c->last  = msg;
                                                 c->count = c->count + 1;
                                             },
                                             &cap});

    Logger::Log(LogChannel::ENGINE, LogLevel::INFO, "hello handler");

    EXPECT_EQ(cap.count, 1);
    EXPECT_EQ(cap.last.Level, LogLevel::INFO);
    EXPECT_EQ(cap.last.Channel, LogChannel::ENGINE);
    EXPECT_NE(cap.last.Message, nullptr);
    EXPECT_STREQ(cap.last.Message, "hello handler");

    Logger::RemoveEventHandler(cookie);
}

TEST_F(LoggerTest, HandlerNotCalledAfterRemoval)
{
    std::atomic<int> count{0};
    auto             cookie = Logger::AddEventHandler({[](void* ctx, const LogMessage&) { (*static_cast<std::atomic<int>*>(ctx))++; }, &count});

    Logger::RemoveEventHandler(cookie);
    Logger::Log(LogChannel::ENGINE, LogLevel::INFO, "should not reach handler");

    EXPECT_EQ(count.load(), 0);
}

TEST_F(LoggerTest, MultipleHandlersAllReceive)
{
    std::atomic<int> a{0}, b{0};

    auto             ca = Logger::AddEventHandler({[](void* ctx, const LogMessage&) { (*static_cast<std::atomic<int>*>(ctx))++; }, &a});
    auto             cb = Logger::AddEventHandler({[](void* ctx, const LogMessage&) { (*static_cast<std::atomic<int>*>(ctx))++; }, &b});

    Logger::Log(LogChannel::ENGINE, LogLevel::WARN, "multi");

    Logger::RemoveEventHandler(ca);
    Logger::RemoveEventHandler(cb);

    EXPECT_EQ(a.load(), 1);
    EXPECT_EQ(b.load(), 1);
}

TEST_F(LoggerTest, ColorSetCorrectlyPerLevel)
{
    struct Capture
    {
        LogMessage msg{};
    };

    auto run = [&](LogLevel level) -> LogMessage {
        Capture cap{};
        auto    cookie = Logger::AddEventHandler({[](void* ctx, const LogMessage& m) { static_cast<Capture*>(ctx)->msg = m; }, &cap});
        Logger::Log(LogChannel::ENGINE, level, "color check");
        Logger::RemoveEventHandler(cookie);
        return cap.msg;
    };

    auto trace    = run(LogLevel::TRACE);
    auto info     = run(LogLevel::INFO);
    auto warn     = run(LogLevel::WARN);
    auto err      = run(LogLevel::ERR);
    auto critical = run(LogLevel::CRITICAL);

    EXPECT_GT(trace.Color[0], 0.5f);
    EXPECT_GT(trace.Color[1], 0.5f);
    EXPECT_GT(trace.Color[2], 0.5f);
    EXPECT_EQ(trace.Color[3], 1.0f);

    EXPECT_LT(info.Color[0], 0.5f);
    EXPECT_GT(info.Color[1], 0.5f);
    EXPECT_LT(info.Color[2], 0.5f);
    EXPECT_EQ(info.Color[3], 1.0f);

    EXPECT_GT(warn.Color[0], 0.8f);
    EXPECT_GT(warn.Color[1], 0.5f);
    EXPECT_EQ(warn.Color[2], 0.0f);
    EXPECT_EQ(warn.Color[3], 1.0f);

    EXPECT_GT(err.Color[0], 0.8f);
    EXPECT_LT(err.Color[1], 0.5f);
    EXPECT_LT(err.Color[2], 0.5f);
    EXPECT_EQ(err.Color[3], 1.0f);

    EXPECT_GT(critical.Color[0], 0.8f);
    EXPECT_LT(critical.Color[1], 0.5f);
    EXPECT_EQ(critical.Color[3], 1.0f);
}

TEST_F(LoggerTest, BackwardsCompatMacroRoutesToEngineChannel)
{
    LogMessage captured{};
    auto       cookie = Logger::AddEventHandler({[](void* ctx, const LogMessage& m) { *static_cast<LogMessage*>(ctx) = m; }, &captured});

    ZENGINE_CORE_INFO("compat {}", 42);

    Logger::RemoveEventHandler(cookie);

    EXPECT_EQ(captured.Channel, LogChannel::ENGINE);
    EXPECT_EQ(captured.Level, LogLevel::INFO);
    EXPECT_NE(captured.Message, nullptr);
}

TEST_F(LoggerTest, RuntimeLevelFilterPassesCritical)
{
    std::atomic<int> count{0};
    auto             cookie = Logger::AddEventHandler({[](void* ctx, const LogMessage&) { (*static_cast<std::atomic<int>*>(ctx))++; }, &count});

    Logger::Log(LogChannel::ENGINE, LogLevel::CRITICAL, "always passes");

    Logger::RemoveEventHandler(cookie);
    EXPECT_EQ(count.load(), 1);
}

TEST_F(LoggerTest, RingBufferWraps)
{
    const int capacity = 64;
    const int total    = capacity + 16;

    for (int i = 0; i < total; ++i)
    {
        Logger::Log(LogChannel::ENGINE, LogLevel::INFO, fmt::format("msg {}", i));
    }

    auto        path_str = (std::filesystem::temp_directory_path() / "zengine_ringbuf_wrap_test.log").string();
    const char* path     = path_str.c_str();
    Logger::FlushRingBufferToCrashLog(path);

    std::FILE* f = std::fopen(path, "r");
    ASSERT_NE(f, nullptr);

    int  lines = 0;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), f))
    {
        ++lines;
    }
    std::fclose(f);
    std::remove(path);

    EXPECT_LE(lines, capacity);
    EXPECT_GT(lines, 0);
}

TEST_F(LoggerTest, FlushRingBufferWritesReverseChronological)
{
    Logger::Log(LogChannel::ENGINE, LogLevel::INFO, "first");
    Logger::Log(LogChannel::ENGINE, LogLevel::WARN, "second");
    Logger::Log(LogChannel::ENGINE, LogLevel::ERR, "third");

    auto        path_str = (std::filesystem::temp_directory_path() / "zengine_flush_order_test.log").string();
    const char* path     = path_str.c_str();
    Logger::FlushRingBufferToCrashLog(path);

    std::FILE* f = std::fopen(path, "r");
    ASSERT_NE(f, nullptr);

    char lines[3][256] = {};
    int  n             = 0;
    while (n < 3 && std::fgets(lines[n], sizeof(lines[n]), f))
    {
        ++n;
    }
    std::fclose(f);
    std::remove(path);

    ASSERT_EQ(n, 3);
    EXPECT_NE(std::strstr(lines[0], "third"), nullptr) << "got: " << lines[0];
    EXPECT_NE(std::strstr(lines[1], "second"), nullptr);
    EXPECT_NE(std::strstr(lines[2], "first"), nullptr);
}

TEST_F(LoggerTest, FlushRingBufferNoArgUsesCrashLogDir)
{
    Logger::Log(LogChannel::ENGINE, LogLevel::CRITICAL, "crash entry");
    Logger::FlushRingBufferToCrashLog();

    auto path = std::filesystem::path(s_crash_dir) / "ring_buffer.log";
    EXPECT_TRUE(std::filesystem::exists(path));

    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path);
    }
}

TEST_F(LoggerTest, AddEventHandlerConcurrentRegistration)
{
    constexpr int            threads = 8;
    std::atomic<int>         total{0};
    std::vector<uint32_t>    cookies(threads);

    auto                     register_fn = [&](int idx) { cookies[idx] = Logger::AddEventHandler({[](void* ctx, const LogMessage&) { (*static_cast<std::atomic<int>*>(ctx))++; }, &total}); };

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (int i = 0; i < threads; ++i)
    {
        workers.emplace_back(register_fn, i);
    }
    for (auto& t : workers)
    {
        t.join();
    }

    Logger::Log(LogChannel::ENGINE, LogLevel::INFO, "concurrent");

    for (auto c : cookies)
    {
        Logger::RemoveEventHandler(c);
    }

    EXPECT_EQ(total.load(), threads);
}

TEST_F(LoggerTest, ConcurrentLogAndHandlerNoRace)
{
    constexpr int            msg_count = 200;
    std::atomic<int>         received{0};

    auto                     cookie       = Logger::AddEventHandler({[](void* ctx, const LogMessage&) { (*static_cast<std::atomic<int>*>(ctx))++; }, &received});

    constexpr int            writer_count = 4;
    std::vector<std::thread> writers;
    writers.reserve(writer_count);
    for (int i = 0; i < writer_count; ++i)
    {
        writers.emplace_back([i] {
            for (int j = 0; j < msg_count / writer_count; ++j)
            {
                Logger::Log(LogChannel::ENGINE, LogLevel::INFO, fmt::format("t{} msg{}", i, j));
            }
        });
    }
    for (auto& t : writers)
    {
        t.join();
    }

    Logger::RemoveEventHandler(cookie);

    EXPECT_EQ(received.load(), msg_count);
}

TEST_F(LoggerTest, LevelToStringAllValues)
{
    EXPECT_STREQ(Logger::LevelToString(LogLevel::TRACE), "trace");
    EXPECT_STREQ(Logger::LevelToString(LogLevel::INFO), "info");
    EXPECT_STREQ(Logger::LevelToString(LogLevel::WARN), "warn");
    EXPECT_STREQ(Logger::LevelToString(LogLevel::ERR), "error");
    EXPECT_STREQ(Logger::LevelToString(LogLevel::CRITICAL), "critical");
}
