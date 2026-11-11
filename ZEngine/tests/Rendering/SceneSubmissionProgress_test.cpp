#include <ZEngine/Rendering/Scenes/SceneSubmissionProgress.h>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

using ZEngine::Rendering::Scenes::SceneSubmissionProgress;

TEST(SceneSubmissionProgressTest, StartsEmptyAndCountsOnlyExplicitAcceptedNotifications)
{
    SceneSubmissionProgress           progress = {};
    SceneSubmissionProgress::Snapshot snapshot = {};
    ASSERT_TRUE(progress.TryRead(&snapshot));
    EXPECT_EQ(snapshot.Generation, 0u);
    EXPECT_EQ(snapshot.Count, 0u);
    EXPECT_FALSE(progress.TryRead(nullptr));
    progress.Record(0, 10);
    progress.Record(4, 20, 8192);
    progress.Record(4, 30, 8192);
    ASSERT_TRUE(progress.TryRead(&snapshot));
    EXPECT_EQ(snapshot.Generation, 4u);
    EXPECT_EQ(snapshot.Count, 2u);
    EXPECT_EQ(snapshot.FirstTimestampNs, 20u);
    EXPECT_EQ(snapshot.DrawCount, 8192u);
    progress.Record(3, 40);
    ASSERT_TRUE(progress.TryRead(&snapshot));
    EXPECT_EQ(snapshot.Count, 2u);
    EXPECT_EQ(snapshot.DrawCount, 8192u);
    progress.Record(8, 50);
    ASSERT_TRUE(progress.TryRead(&snapshot));
    EXPECT_EQ(snapshot.Generation, 8u);
    EXPECT_EQ(snapshot.Count, 1u);
    EXPECT_EQ(snapshot.FirstTimestampNs, 50u);
    EXPECT_EQ(snapshot.DrawCount, 0u);
}

TEST(SceneSubmissionProgressTest, ConcurrentReadsNeverMixGenerationCountAndFirstTimestamp)
{
    SceneSubmissionProgress progress = {};
    std::atomic<bool>       finished = false;
    std::thread             writer([&] {
        for (uint64_t generation = 1; generation <= 100000; ++generation)
        {
            progress.Record(generation, generation * 11, static_cast<uint32_t>(generation));
            progress.Record(generation, generation * 11 + 1, static_cast<uint32_t>(generation));
        }
        finished.store(true, std::memory_order_release);
    });
    uint64_t                last_generation = 0;
    bool                    coherent        = true;
    do
    {
        SceneSubmissionProgress::Snapshot snapshot = {};
        if (progress.TryRead(&snapshot))
        {
            coherent        &= snapshot.Generation >= last_generation && snapshot.FirstTimestampNs == snapshot.Generation * 11;
            coherent        &= snapshot.Generation ? (snapshot.Count == 1 || snapshot.Count == 2) : snapshot.Count == 0;
            coherent        &= snapshot.DrawCount == snapshot.Generation;
            last_generation  = snapshot.Generation;
        }
    } while (!finished.load(std::memory_order_acquire));
    writer.join();
    EXPECT_TRUE(coherent);
    SceneSubmissionProgress::Snapshot snapshot = {};
    ASSERT_TRUE(progress.TryRead(&snapshot));
    EXPECT_EQ(snapshot.Generation, 100000u);
    EXPECT_EQ(snapshot.Count, 2u);
    EXPECT_EQ(snapshot.FirstTimestampNs, 1100000u);
}
