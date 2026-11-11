#include <ZEngine/Profiling/ProfilerBuffer.h>
#include <ZEngine/Profiling/ThreadCpuTime.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using ZEngine::Profiling::ProfilerBuffer;
using ZEngine::Profiling::ThreadExecutionTimes;

TEST(ThreadExecutionTimesTest, SeparatesCpuAndNonCpuWithoutInferringTheBlockingCause)
{
    const auto result = ThreadExecutionTimes::Between({100, true}, {400, true}, 1000);
    ASSERT_TRUE(result.Valid);
    EXPECT_EQ(result.CpuNanoseconds, 300u);
    EXPECT_EQ(result.NonCpuNanoseconds, 700u);
}

TEST(ThreadExecutionTimesTest, InvalidOrInconsistentClocksCannotMasqueradeAsZeroCpuWork)
{
    EXPECT_FALSE(ThreadExecutionTimes::Between({}, {400, true}, 1000).Valid);
    EXPECT_FALSE(ThreadExecutionTimes::Between({100, true}, {}, 1000).Valid);
    EXPECT_FALSE(ThreadExecutionTimes::Between({400, true}, {100, true}, 1000).Valid);
    EXPECT_FALSE(ThreadExecutionTimes::Between({100, true}, {400, true}, 299).Valid);
    const auto exact = ThreadExecutionTimes::Between({100, true}, {400, true}, 300);
    ASSERT_TRUE(exact.Valid);
    EXPECT_EQ(exact.NonCpuNanoseconds, 0u);
}

TEST(ThreadExecutionTimesTest, CallingThreadClockIsMonotonicOrExplicitlyUnavailable)
{
    const auto first  = ZEngine::Profiling::ReadThreadCpuTime();
    const auto second = ZEngine::Profiling::ReadThreadCpuTime();
#if defined(__APPLE__) || defined(__linux__)
    ASSERT_TRUE(first.Valid);
    ASSERT_TRUE(second.Valid);
#else
    EXPECT_EQ(first.Valid, second.Valid);
#endif
    if (first.Valid && second.Valid)
    {
        EXPECT_GE(second.Nanoseconds, first.Nanoseconds);
    }
}

TEST(ProfilerCaptureTest, CapturesAcrossFramesFiltersNamesAndExportsOnlyAfterStopping)
{
    const auto file = std::filesystem::temp_directory_path() / ("profiler-capture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
    ProfilerBuffer::StopCapture();
    ASSERT_TRUE(ProfilerBuffer::StartCapture("Test."));
    EXPECT_FALSE(ProfilerBuffer::StartCapture());
    ProfilerBuffer::Record("Ignored", 999);
    ProfilerBuffer::Record("Test.Scope", 123);
    ProfilerBuffer::BeginFrame();
    ProfilerBuffer::RecordValue("Test.\"Value\"", 4);
    EXPECT_FALSE(ProfilerBuffer::WriteCaptureCsv(file.string().c_str()));
    ProfilerBuffer::StopCapture();
    EXPECT_EQ(ProfilerBuffer::CaptureCount(), 2u);
    EXPECT_EQ(ProfilerBuffer::CaptureDropped(), 0u);
    ASSERT_TRUE(ProfilerBuffer::WriteCaptureCsv(file.string().c_str()));
    {
        // Windows cannot delete a file while this read handle remains open.
        std::ifstream input(file);
        ASSERT_TRUE(input.is_open());
        const std::string csv(std::istreambuf_iterator<char>{input}, {});
        EXPECT_NE(csv.find("scope_ns,\"Test.Scope\","), std::string::npos);
        EXPECT_NE(csv.find("value,\"Test.\"\"Value\"\"\","), std::string::npos);
        EXPECT_EQ(csv.find("Ignored"), std::string::npos);
    }
    EXPECT_TRUE(std::filesystem::remove(file));
}

TEST(ProfilerCaptureTest, ByteCountsAboveFloatPrecisionRoundTripExactlyInCapture)
{
    const auto file = std::filesystem::temp_directory_path() / ("profiler-byte-count-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
    ProfilerBuffer::StopCapture();
    ASSERT_TRUE(ProfilerBuffer::StartCapture("Test.Memory."));
    ProfilerBuffer::RecordValue("Test.Memory.PeakBytes", 134217729.0);
    ProfilerBuffer::StopCapture();
    ASSERT_TRUE(ProfilerBuffer::WriteCaptureCsv(file.string().c_str()));
    {
        std::ifstream input(file);
        ASSERT_TRUE(input.is_open());
        const std::string csv(std::istreambuf_iterator<char>{input}, {});
        EXPECT_NE(csv.find(",134217729\n"), std::string::npos);
        EXPECT_EQ(csv.find(",134217728\n"), std::string::npos);
    }
    EXPECT_TRUE(std::filesystem::remove(file));
}

TEST(ProfilerCaptureTest, ConcurrentWritersUseFixedStorageAndReportSaturationWithoutOverflow)
{
    ProfilerBuffer::StopCapture();
    ASSERT_TRUE(ProfilerBuffer::StartCapture());
    constexpr uint32_t each = ProfilerBuffer::CAPTURE_CAPACITY / 2 + 7;
    std::thread        first([] {
        for (uint32_t i = 0; i < each; ++i)
            ProfilerBuffer::Record("Scope", i);
    });
    std::thread        second([] {
        for (uint32_t i = 0; i < each; ++i)
            ProfilerBuffer::RecordValue("Value", static_cast<float>(i));
    });
    first.join();
    second.join();
    ProfilerBuffer::StopCapture();
    EXPECT_EQ(ProfilerBuffer::CaptureCount(), ProfilerBuffer::CAPTURE_CAPACITY);
    EXPECT_EQ(ProfilerBuffer::CaptureDropped(), 14u);
    ASSERT_TRUE(ProfilerBuffer::StartCapture("Fresh"));
    EXPECT_EQ(ProfilerBuffer::CaptureCount(), 0u);
    EXPECT_EQ(ProfilerBuffer::CaptureDropped(), 0u);
    ProfilerBuffer::StopCapture();
}

TEST(ProfilerCaptureTest, TimedCaptureStopsBeforeLaterSamplesWithoutDebuggerIntervention)
{
    ProfilerBuffer::StopCapture();
    EXPECT_FALSE(ProfilerBuffer::StartTimedCapture(0));
    ASSERT_TRUE(ProfilerBuffer::StartTimedCapture(100, "Timed."));
    ProfilerBuffer::RecordValue("Timed.Marker", 1);
    EXPECT_EQ(ProfilerBuffer::CaptureCount(), 1u);
    EXPECT_FALSE(ProfilerBuffer::StartTimedCapture(100, "Other."));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    ProfilerBuffer::Record("Timed.TooLate", 10);
    EXPECT_EQ(ProfilerBuffer::CaptureCount(), 1u);
    EXPECT_EQ(ProfilerBuffer::CaptureDropped(), 0u);
    ASSERT_TRUE(ProfilerBuffer::StartCapture("Fresh."));
    ProfilerBuffer::Record("Fresh.NoOldDeadline", 10);
    EXPECT_EQ(ProfilerBuffer::CaptureCount(), 1u);
    ProfilerBuffer::StopCapture();
}
