#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using ZEngine::Core::MainThreadScheduler;
using ZEngine::Core::Memory::MemoryManager;

namespace
{
    void Increment(void* context)
    {
        static_cast<std::atomic<int>*>(context)->fetch_add(1, std::memory_order_relaxed);
    }

    void IncrementAndPost(void* context)
    {
        Increment(context);
        MainThreadScheduler::Post(context, &Increment);
    }

    std::future<int> AwaitRValueFuture(std::future<int>&& source)
    {
        co_return co_await std::move(source);
    }

    class CoroutineSchedulerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_manager.Initialize(ZMega(1), {});
            MainThreadScheduler::Initialize(&m_manager.MainArena);
        }

        void TearDown() override
        {
            MainThreadScheduler::Shutdown();
            m_manager.Shutdown();
        }

        MemoryManager m_manager;
    };
} // namespace

TEST_F(CoroutineSchedulerTest, ReentrantPostRunsOnTheNextDrain)
{
    std::atomic<int> executions = 0;
    MainThreadScheduler::Post(&executions, &IncrementAndPost);

    MainThreadScheduler::Drain();
    EXPECT_EQ(executions.load(std::memory_order_relaxed), 1);

    MainThreadScheduler::Drain();
    EXPECT_EQ(executions.load(std::memory_order_relaxed), 2);
}

TEST_F(CoroutineSchedulerTest, ConcurrentPostsAreAllDrained)
{
    static constexpr uint32_t ProducerCount    = 4;
    static constexpr uint32_t TasksPerProducer = 64;

    std::atomic<int>          executions       = 0;
    std::thread               producers[ProducerCount];
    for (std::thread& producer : producers)
    {
        producer = std::thread([&executions] {
            for (uint32_t i = 0; i < TasksPerProducer; ++i)
                MainThreadScheduler::Post(&executions, &Increment);
        });
    }
    for (std::thread& producer : producers)
        producer.join();

    MainThreadScheduler::Drain();
    EXPECT_EQ(executions.load(std::memory_order_relaxed), ProducerCount * TasksPerProducer);
}

TEST_F(CoroutineSchedulerTest, AwaitingRValueFutureRetainsItsStateUntilCompletion)
{
    std::promise<int> source;
    std::future<int>  result = AwaitRValueFuture(source.get_future());
    ASSERT_EQ(result.wait_for(std::chrono::milliseconds::zero()), std::future_status::timeout);

    source.set_value(42);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (result.wait_for(std::chrono::milliseconds::zero()) != std::future_status::ready && std::chrono::steady_clock::now() < deadline)
    {
        MainThreadScheduler::Drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(result.wait_for(std::chrono::milliseconds::zero()), std::future_status::ready);
    EXPECT_EQ(result.get(), 42);
}
