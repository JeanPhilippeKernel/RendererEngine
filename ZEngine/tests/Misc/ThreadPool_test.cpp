#include <ZEngine/Helpers/ThreadPool.h>
#include <gtest/gtest.h>

using namespace ZEngine::Helpers;

namespace
{
    void SetExecuted(void* context)
    {
        static_cast<std::atomic<bool>*>(context)->store(true, std::memory_order_release);
    }

    void Increment(void* context)
    {
        static_cast<std::atomic<int>*>(context)->fetch_add(1, std::memory_order_relaxed);
    }

    void SlowIncrement(void* context)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Increment(context);
    }

    void SubmitTasks(std::atomic<int>* counter)
    {
        static constexpr int tasks_per_producer = 128;
        for (int i = 0; i < tasks_per_producer; ++i)
            ThreadPoolHelper::Submit(counter, &Increment);
    }

    struct BlockingTaskContext
    {
        std::atomic<bool> Started = false;
        std::atomic<bool> Release = false;
    };

    void BlockWorker(void* context)
    {
        auto* blocking = static_cast<BlockingTaskContext*>(context);
        blocking->Started.store(true, std::memory_order_release);
        while (!blocking->Release.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
} // namespace

class ThreadPoolTest : public ::testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(ThreadPoolTest, Submit)
{
    std::atomic<bool> executed = false;
    ThreadPoolHelper::Submit(&executed, &SetExecuted);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, TaskExecution)
{
    std::atomic<int> counter = 0;
    for (int i = 0; i < 5; ++i)
    {
        ThreadPoolHelper::Submit(&counter, &Increment);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(counter, 5);
}

TEST_F(ThreadPoolTest, MultipleTasksExecution)
{
    std::atomic<int> counter       = 0;
    const int        numberOfTasks = 10;

    for (int i = 0; i < numberOfTasks; ++i)
    {
        ThreadPoolHelper::Submit(&counter, &SlowIncrement);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_EQ(counter, numberOfTasks);
}

TEST_F(ThreadPoolTest, ConcurrentSubmitters)
{
    static constexpr int producer_count      = 4;
    static constexpr int tasks_per_producer  = 128;
    static constexpr int expected_task_count = producer_count * tasks_per_producer;

    std::atomic<int>     counter             = 0;
    std::thread          producers[producer_count];
    for (std::thread& producer : producers)
    {
        producer = std::thread(&SubmitTasks, &counter);
    }
    for (std::thread& producer : producers)
        producer.join();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (counter.load(std::memory_order_acquire) != expected_task_count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    EXPECT_EQ(counter.load(std::memory_order_acquire), expected_task_count);
}

TEST_F(ThreadPoolTest, SubmitToWorker)
{
    ASSERT_NE(ThreadPoolHelper::Pool, nullptr);
    ASSERT_GT(ThreadPoolHelper::Pool->WorkerCount, 0u);

    std::atomic<int> counter = 0;
    EXPECT_FALSE(ThreadPoolHelper::SubmitToWorker(static_cast<uint32_t>(ThreadPoolHelper::Pool->WorkerCount), &counter, &Increment));
    EXPECT_TRUE(ThreadPoolHelper::SubmitToWorker(0, &counter, &Increment));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (counter.load(std::memory_order_acquire) != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    EXPECT_EQ(counter.load(std::memory_order_acquire), 1);
}

TEST_F(ThreadPoolTest, SubmitToWorkerRejectsAFilledQueue)
{
    ThreadPool          pool(1);
    BlockingTaskContext blocking = {};
    ASSERT_TRUE(pool.SubmitToWorker(0, &blocking, &BlockWorker));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!blocking.Started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    ASSERT_TRUE(blocking.Started.load(std::memory_order_acquire));

    std::atomic<int> counter = 0;
    for (uint32_t i = 0; i < ThreadPool::MAX_TASKS_PER_WORKER; ++i)
        ASSERT_TRUE(pool.SubmitToWorker(0, &counter, &Increment));
    EXPECT_FALSE(pool.SubmitToWorker(0, &counter, &Increment));

    blocking.Release.store(true, std::memory_order_release);
}
