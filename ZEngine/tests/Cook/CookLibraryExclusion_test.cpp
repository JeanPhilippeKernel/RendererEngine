#include <ZEngine/Cook/CookLibraryExclusion.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <gtest/gtest.h>
#include <barrier>
#include <thread>
#include <type_traits>

namespace ZEngine::Cook
{
    TEST(CookLibraryExclusionTest, MultipleReadersExcludeWritersAndMutationExcludesEveryOtherLease)
    {
        static_assert(!std::is_copy_constructible_v<CookLibraryReadLease>);
        static_assert(std::is_nothrow_move_assignable_v<CookLibraryMutationLease>);
        static_assert(!std::is_move_constructible_v<CookLibraryExclusion>);
        CookLibraryExclusion     owner = {};
        CookLibraryReadLease     first = {}, second = {};
        CookLibraryMutationLease writer = {}, other_writer = {};
        EXPECT_FALSE(first.IsValid());
        EXPECT_EQ(first.Generation(), 0u);
        EXPECT_EQ(owner.TryAcquireRead(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(owner.TryAcquireMutation(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        ASSERT_TRUE(owner.TryAcquireRead(&first).Succeeded());
        ASSERT_TRUE(owner.TryAcquireRead(&second).Succeeded());
        EXPECT_EQ(first.Generation(), second.Generation());
        EXPECT_EQ(owner.TryAcquireRead(&first).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(owner.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        first.Reset();
        EXPECT_EQ(owner.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        const auto old_generation = second.Generation();
        second.Reset();
        ASSERT_TRUE(owner.TryAcquireMutation(&writer).Succeeded());
        EXPECT_EQ(owner.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(owner.TryAcquireMutation(&other_writer).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(owner.TryAcquireRead(&first).Error(), Core::VFS::VFSError::AlreadyExists);
        writer.Reset();
        writer.Reset();
        ASSERT_TRUE(owner.TryAcquireRead(&first).Succeeded());
        EXPECT_GT(first.Generation(), old_generation);
    }

    TEST(CookLibraryExclusionTest, MovesAndDestructionReleaseEachTokenExactlyOnce)
    {
        CookLibraryExclusion owner = {}, other = {};
        CookLibraryReadLease reader = {}, destination = {};
        ASSERT_TRUE(owner.TryAcquireRead(&reader).Succeeded());
        ASSERT_TRUE(other.TryAcquireRead(&destination).Succeeded());
        const auto generation = reader.Generation();
        destination           = std::move(reader);
        EXPECT_FALSE(reader.IsValid());
        EXPECT_EQ(reader.Generation(), 0u);
        EXPECT_EQ(destination.Generation(), generation);
        CookLibraryMutationLease writer = {};
        ASSERT_TRUE(other.TryAcquireMutation(&writer).Succeeded());
        {
            CookLibraryReadLease moved(std::move(destination));
            auto*                same = &moved;
            moved                     = std::move(*same);
            EXPECT_TRUE(moved.IsValid());
        }
        CookLibraryMutationLease second = {};
        ASSERT_TRUE(owner.TryAcquireMutation(&second).Succeeded());
        writer = std::move(second);
        EXPECT_FALSE(second.IsValid());
        {
            CookLibraryMutationLease moved(std::move(writer));
            auto*                    same = &moved;
            moved                         = std::move(*same);
            EXPECT_TRUE(moved.IsValid());
        }
        ASSERT_TRUE(owner.TryAcquireRead(&reader).Succeeded());
        ASSERT_TRUE(other.TryAcquireRead(&destination).Succeeded());
    }

    TEST(CookLibraryExclusionTest, FailedAcquisitionPreservesAnExistingLeaseAndItsOwner)
    {
        CookLibraryExclusion     first = {}, second = {};
        CookLibraryReadLease     reader = {};
        CookLibraryMutationLease writer = {};
        ASSERT_TRUE(first.TryAcquireRead(&reader).Succeeded());
        const auto generation = reader.Generation();
        EXPECT_EQ(second.TryAcquireRead(&reader).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(reader.Generation(), generation);
        EXPECT_EQ(first.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(second.TryAcquireMutation(&writer).Succeeded());
        EXPECT_EQ(first.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(writer.IsValid());
    }

    TEST(CookLibraryExclusionTest, ConcurrentReadAcquisitionKeepsMutationExcludedUntilTheLastRelease)
    {
        CookLibraryExclusion                         owner = {};
        std::barrier                                 ready(5), finish(5);
        Core::Containers::FixedArray<std::thread, 4> readers = {};
        for (auto& thread : readers)
            thread = std::thread([&] {
                CookLibraryReadLease lease = {};
                EXPECT_TRUE(owner.TryAcquireRead(&lease).Succeeded());
                ready.arrive_and_wait();
                finish.arrive_and_wait();
            });
        ready.arrive_and_wait();
        CookLibraryMutationLease writer = {};
        EXPECT_EQ(owner.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        CookLibraryReadLease extra = {};
        EXPECT_TRUE(owner.TryAcquireRead(&extra).Succeeded());
        extra.Reset();
        finish.arrive_and_wait();
        for (auto& thread : readers)
            thread.join();
        ASSERT_TRUE(owner.TryAcquireMutation(&writer).Succeeded());
    }
} // namespace ZEngine::Cook
