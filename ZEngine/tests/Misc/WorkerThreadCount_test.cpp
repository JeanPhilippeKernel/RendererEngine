#include <ZEngine/Helpers/ThreadPool.h>
#include <gtest/gtest.h>

// Mirrors the formula in Engine::Initialize exactly.
// If the formula changes there, update it here too.
static uint32_t ComputeWorkerThreadCount(size_t hardware_concurrency)
{
    ZEngine::Helpers::ThreadPool pool(hardware_concurrency);
    return std::max(1u, (uint32_t) (pool.MaxThreadCount / 2u));
}

class WorkerThreadCountTest : public ::testing::Test
{
};

// 1-vCPU: MaxThreadCount = 0 after reserving 1, halved = 0, clamped to 1.
TEST_F(WorkerThreadCountTest, SingleCoreClampsToOne)
{
    EXPECT_EQ(ComputeWorkerThreadCount(1), 1u);
}

// 2-vCPU (Linux CI): MaxThreadCount = 1 after reserving 1, halved = 0, clamped to 1.
// This is the exact configuration that triggered the crash.
TEST_F(WorkerThreadCountTest, TwoCoreLinuxCIClampsToOne)
{
    EXPECT_EQ(ComputeWorkerThreadCount(2), 1u);
}

// 3-vCPU (macOS CI): MaxThreadCount = 2, halved = 1. No clamp needed.
TEST_F(WorkerThreadCountTest, ThreeCoreReturnsOne)
{
    EXPECT_EQ(ComputeWorkerThreadCount(3), 1u);
}

// 4-vCPU: MaxThreadCount = 3, halved = 1.
TEST_F(WorkerThreadCountTest, FourCoreReturnsOne)
{
    EXPECT_EQ(ComputeWorkerThreadCount(4), 1u);
}

// 8-vCPU (typical dev machine): MaxThreadCount = 7, halved = 3.
TEST_F(WorkerThreadCountTest, EightCoreReturnsThree)
{
    EXPECT_EQ(ComputeWorkerThreadCount(8), 3u);
}

// Result is always at least 1 regardless of core count.
TEST_F(WorkerThreadCountTest, ResultIsNeverZero)
{
    for (size_t cores = 1; cores <= 32; ++cores)
    {
        EXPECT_GE(ComputeWorkerThreadCount(cores), 1u) << "failed for core count: " << cores;
    }
}
