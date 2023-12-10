#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include "Helpers/ThreadPool.h"

using namespace ZEngine::Helpers;

TEST(ThreadPoolTest, ExecuteTasks)
{
    ThreadPool       pool(4);
    std::atomic<int> counter = 0;

    for (int i = 0; i < 10; ++i)
    {
        pool.Submit([&counter]() {
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(counter, 10);
}

TEST(ThreadPoolTest, ExecuteTasksUsingHelper)
{
    std::atomic<int> counter = 0;

    for (int i = 0; i < 10; ++i)
    {
        ThreadPoolHelper::Submit([&counter]() {
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(counter, 10);
}

TEST(ThreadPoolTest, Shutdown)
{
    std::atomic<bool> executed = false;

    {
        ThreadPool pool(2);
        pool.Submit([&executed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            executed = true;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(executed);
}

TEST(ThreadPoolTest, NoExecuteAfterShutdown)
{
    ThreadPool        pool(2);
    std::atomic<bool> executed = false;

    pool.Shutdown();

    pool.Submit([&executed]() {
        executed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_FALSE(executed);
}

TEST(ThreadPoolTest, ConcurrentTaskExecution)
{
    ThreadPool       pool(4);
    std::atomic<int> counter = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 4; ++i)
    {
        pool.Submit([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300)); 

    auto endTime  = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    EXPECT_EQ(counter, 4);
    EXPECT_LT(duration, 400);
}

TEST(ThreadPoolTest, TaskExceptionHandling)
{
    ThreadPool        pool(2);
    std::atomic<bool> taskExecuted    = false;
    std::atomic<bool> exceptionCaught = false;

    pool.Submit([&]() {
        try
        {
            throw std::runtime_error("Task exception");
        }
        catch (const std::runtime_error&)
        {
            exceptionCaught = true;
        }
        taskExecuted = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(taskExecuted);
    EXPECT_TRUE(exceptionCaught);
}


TEST(ThreadPoolTest, ResourceCleanup) {
    std::atomic<int> activeThreadsBefore = 0;
    std::atomic<int> activeThreadsAfter = 0;

    activeThreadsBefore = std::thread::hardware_concurrency();

    {
        ThreadPool pool(4);
    }

    activeThreadsAfter = std::thread::hardware_concurrency();

    EXPECT_EQ(activeThreadsBefore, activeThreadsAfter);
}
