#include <ZEngine/Profiling/ProfilerBuffer.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using ZEngine::Profiling::ProfilerBuffer;

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
