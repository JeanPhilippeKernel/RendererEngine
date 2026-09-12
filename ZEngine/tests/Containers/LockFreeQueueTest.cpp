#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/Containers/SPSCQueue.h>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

namespace
{
    struct QueueLifetimeProbe
    {
        QueueLifetimeProbe()
        {
            ++LiveCount;
        }

        ~QueueLifetimeProbe()
        {
            --LiveCount;
        }

        static int LiveCount;
    };

    int QueueLifetimeProbe::LiveCount = 0;
} // namespace

TEST(MPSCQueueLifetimeTest, ConstructsAndDestroysEachSlotOnce)
{
    EXPECT_EQ(QueueLifetimeProbe::LiveCount, 0);
    {
        ZEngine::Core::Containers::MPSCQueue<QueueLifetimeProbe, 4> queue = {};
        EXPECT_EQ(QueueLifetimeProbe::LiveCount, 4);
    }
    EXPECT_EQ(QueueLifetimeProbe::LiveCount, 0);
}

TEST(MPSCQueueTest, DeliversEachConcurrentProducerItemExactlyOnce)
{
    static constexpr uint32_t                          producer_count         = 4;
    static constexpr uint32_t                          items_per_producer     = 512;
    static constexpr uint32_t                          total_item_count       = producer_count * items_per_producer;

    ZEngine::Core::Containers::MPSCQueue<uint32_t, 64> queue                  = {};
    std::atomic<uint32_t>                              seen[total_item_count] = {};
    std::thread                                        producers[producer_count];

    for (uint32_t producer = 0; producer < producer_count; ++producer)
    {
        producers[producer] = std::thread([&queue, producer] {
            for (uint32_t item = 0; item < items_per_producer; ++item)
            {
                const uint32_t value = producer * items_per_producer + item;
                while (!queue.push(value))
                    std::this_thread::yield();
            }
        });
    }

    uint32_t received = 0;
    while (received < total_item_count)
    {
        uint32_t value = 0;
        if (!queue.pop(value))
        {
            std::this_thread::yield();
            continue;
        }

        ASSERT_LT(value, total_item_count);
        seen[value].fetch_add(1, std::memory_order_relaxed);
        ++received;
    }

    for (std::thread& producer : producers)
        producer.join();
    for (uint32_t item = 0; item < total_item_count; ++item)
        EXPECT_EQ(seen[item].load(std::memory_order_relaxed), 1u);
}

TEST(SPSCQueueLifetimeTest, ConstructsAndDestroysEachSlotOnce)
{
    EXPECT_EQ(QueueLifetimeProbe::LiveCount, 0);
    {
        ZEngine::Core::Containers::SPSCQueue<QueueLifetimeProbe, 4> queue = {};
        EXPECT_EQ(QueueLifetimeProbe::LiveCount, 4);
    }
    EXPECT_EQ(QueueLifetimeProbe::LiveCount, 0);
}
