#include <gtest/gtest.h>
#include <future>
#include <stdexcept>
#include <vector>
#include "Helpers/ThreadPool.h"

using namespace ZEngine::Helpers;

class ThreadPoolTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ThreadPoolHelper::Initialize(); 
    }

    void TearDown() override
    {
        ThreadPoolHelper::Shutdown();
    }
};

TEST_F(ThreadPoolTest, TaskExecution)
{
    std::atomic<int> counter = 0;
    auto             task    = [&counter]() {
        counter++;
    };

    ThreadPoolHelper::Submit(task);
    ThreadPoolHelper::Submit(task);
    ThreadPoolHelper::Submit(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(counter, 3);
}

TEST_F(ThreadPoolTest, ExecuteTaskWithParameters)
{
    std::atomic<int> result = 0;
    auto             task   = [&result](int value) {
        result = value;
    };

    ThreadPoolHelper::Submit(std::bind(task, 42));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(result, 42);
}

TEST_F(ThreadPoolTest, ParallelExecutionEfficiency)
{
    std::atomic<int> counter = 0;
    auto             task    = [&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        counter++;
    };

    for (int i = 0; i < 10; ++i)
    {
        ThreadPoolHelper::Submit(task);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_GE(counter, 5);
}
TEST_F(ThreadPoolTest, StabilityUnderHeavyLoad)
{
    const int        numTasks = 1000;
    std::atomic<int> counter  = 0;
    for (int i = 0; i < numTasks; ++i)
    {
        ThreadPoolHelper::Submit([&counter]() {
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_EQ(counter, numTasks);
}
