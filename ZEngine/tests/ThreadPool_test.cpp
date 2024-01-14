#include <gtest/gtest.h>
#include <future>
#include <stdexcept>
#include <vector>
#include "Helpers/ThreadPool.h"

using namespace ZEngine::Helpers;

class ThreadPoolTest : public ::testing::Test
{
protected:
    ThreadPool* threadPool;

    void SetUp() override
    {
        threadPool = new ZEngine::Helpers::ThreadPool();
    }

    void TearDown() override
    {
        threadPool->shutdown();
        delete threadPool;
    }
};

TEST_F(ThreadPoolTest, ExecuteSimpleTask)
{
    auto future = threadPool->enqueue([] {
        return 42;
    });
    EXPECT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTest, ExecuteTaskWithParameters)
{
    auto future = threadPool->enqueue(
        [](int a, int b) {
            return a + b;
        },
        3,
        4);
    EXPECT_EQ(future.get(), 7);
}

TEST_F(ThreadPoolTest, ExecuteMultipleTasks)
{
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 5; ++i)
    {
        futures.push_back(threadPool->enqueue([i] {
            return i * i;
        }));
    }

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

TEST_F(ThreadPoolTest, ParallelExecutionEfficiency)
{
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i)
    {
        futures.emplace_back(threadPool->enqueue([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
        }));
    }

    for (auto& f : futures)
    {
        f.get();
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(duration, 1000);
}

TEST_F(ThreadPoolTest, StabilityUnderHeavyLoad)
{
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10000; ++i)
    {
        futures.emplace_back(threadPool->enqueue([i] {
            return i;
        }));
    }

    for (auto& f : futures)
    {
        EXPECT_NO_THROW(f.get());
    }
}

TEST_F(ThreadPoolTest, NoExecutionAfterShutdown)
{
    threadPool->shutdown();
    EXPECT_THROW(
        {
            auto future = threadPool->enqueue([] {
                return 1;
            });
            future.get();
        },
        std::exception);
}
