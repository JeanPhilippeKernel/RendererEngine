#include <ZEngine/Managers/CookedObjectSnapshot.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <utility>
#include "CookObjectTestSupport.h"

using ZEngine::Managers::CookedObjectSnapshot;

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct SnapshotRoute final : Core::Memory::IAllocationRoute
        {
            alignas(std::max_align_t) std::array<uint8_t, 512> Storage = {};
            std::atomic<bool>* CancelOnAllocate                        = nullptr;
            bool               InUse                                   = false;
            bool               Refuse                                  = false;
            uint32_t           Allocations                             = 0;
            uint32_t           Frees                                   = 0;

            void*              Allocate(size_t bytes) noexcept override
            {
                ++Allocations;
                if (Refuse || InUse || bytes > Storage.size())
                {
                    return nullptr;
                }
                InUse = true;
                if (CancelOnAllocate)
                {
                    CancelOnAllocate->store(true, std::memory_order_release);
                }
                return Storage.data();
            }
            void* Reallocate(void*, size_t, size_t) noexcept override
            {
                return nullptr;
            }
            void Free(void* pointer) noexcept override
            {
                if (pointer)
                {
                    EXPECT_EQ(pointer, Storage.data());
                    EXPECT_TRUE(InUse);
                    InUse = false;
                    ++Frees;
                }
            }
        };
    } // namespace

    class CookedObjectSnapshotTest : public testing::Test
    {
    protected:
        Tests::ObjectStore   Store    = {};
        SnapshotRoute        Route    = {};
        CookObjectDescriptor Object   = {};
        CookedObjectSnapshot Snapshot = {};

        void                 SetUp() override
        {
            std::array<uint8_t, 128> bytes = {};
            for (size_t i = 0; i < bytes.size(); ++i)
            {
                bytes[i] = static_cast<uint8_t>(i);
            }
            Object = Store.Entries[Store.Add({bytes.data(), bytes.size()}, "zescenebin")].Object;
        }
        void TearDown() override
        {
            Snapshot.Reset();
            EXPECT_FALSE(Route.InUse);
        }
        VFS::VFSResult<void> Capture(size_t max_bytes = 128, const std::atomic<bool>* cancelled = nullptr)
        {
            return CookedObjectSnapshot::Capture(Store.Reader(), Object, "zescenebin", Route, max_bytes, &Snapshot, cancelled);
        }
    };

    TEST_F(CookedObjectSnapshotTest, RetainsExactlyTheVerifiedBytesAfterTheSourceChanges)
    {
        Store.Entries[0].File.ReadLimit = 7;
        ASSERT_TRUE(Capture().Succeeded());
        ASSERT_EQ(Snapshot.Bytes().size(), 128u);
        EXPECT_EQ(Store.Entries[0].File.Closes, 1u);
        EXPECT_EQ(Store.Entries[0].File.Stats, 2u);
        Store.Entries[0].Bytes[0] = 255;
        EXPECT_EQ(Snapshot.Bytes()[0], 0u);
        EXPECT_EQ(Snapshot.Bytes()[127], 127u);
        EXPECT_EQ(Capture().Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Store.Opens, 1u);
        CookedObjectSnapshot moved = std::move(Snapshot);
        EXPECT_TRUE(Snapshot.Bytes().empty());
        EXPECT_EQ(moved.Bytes()[127], 127u);
        moved.Reset();
        EXPECT_EQ(Route.Frees, 1u);
    }

    TEST_F(CookedObjectSnapshotTest, RejectsInvalidPathsAndOverBudgetBeforeOpening)
    {
        EXPECT_EQ(Capture(127).Error(), VFS::VFSError::SizeLimitExceeded);
        auto invalid             = Object;
        invalid.Digest.Bytes[0] ^= 1;
        EXPECT_EQ(CookedObjectSnapshot::Capture(Store.Reader(), invalid, "zescenebin", Route, 128, &Snapshot).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(CookedObjectSnapshot::Capture(Store.Reader(), Object, "png", Route, 128, &Snapshot).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Store.Opens, 0u);
        EXPECT_EQ(Route.Allocations, 0u);
    }

    TEST_F(CookedObjectSnapshotTest, RejectsCorruptionShortReadsAndFailuresWithoutRetainingStorage)
    {
        auto& file                 = Store.Entries[0].File;
        Store.Entries[0].Bytes[0] ^= 1;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::Corrupted);
        Store.Entries[0].Bytes[0] ^= 1;
        file.EOFAt                 = Object.Size - 1;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::Corrupted);
        file.EOFAt      = UINT64_MAX;
        file.Overreport = true;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::Corrupted);
        file.Overreport  = false;
        file.ChangedSize = Object.Size + 1;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::Corrupted);
        file.ChangedSize   = UINT64_MAX;
        file.ReadFailureAt = 0;
        file.ReadError     = VFS::VFSError::PermissionDenied;
        file.CloseError    = VFS::VFSError::IOError;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::PermissionDenied);
        file.ReadFailureAt = UINT64_MAX;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::IOError);
        EXPECT_TRUE(Snapshot.Bytes().empty());
        EXPECT_EQ(file.Closes, 6u);
        EXPECT_EQ(Route.Allocations, Route.Frees);
    }

    TEST_F(CookedObjectSnapshotTest, CancellationAndAllocationRefusalCloseAndReleaseOwnedWork)
    {
        std::atomic<bool> cancelled = true;
        EXPECT_EQ(Capture(128, &cancelled).Error(), VFS::VFSError::Cancelled);
        EXPECT_EQ(Store.Opens, 0u);
        cancelled.store(false);
        Route.CancelOnAllocate = &cancelled;
        EXPECT_EQ(Capture(128, &cancelled).Error(), VFS::VFSError::Cancelled);
        EXPECT_EQ(Store.Entries[0].File.Closes, 1u);
        EXPECT_FALSE(Route.InUse);
        cancelled.store(false);
        Route.CancelOnAllocate = nullptr;
        Route.Refuse           = true;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(Store.Entries[0].File.Closes, 2u);
        EXPECT_TRUE(Snapshot.Bytes().empty());
    }

    TEST_F(CookedObjectSnapshotTest, OpenAndStatFailuresReleaseResourcesAndAllowRetry)
    {
        auto& entry     = Store.Entries[0];
        auto& file      = entry.File;
        entry.OpenError = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Route.Allocations, 0u);
        EXPECT_EQ(file.Closes, 0u);
        entry.OpenError  = VFS::VFSError::OK;
        entry.NullHandle = true;
        EXPECT_EQ(Capture().Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Route.Allocations, 0u);
        EXPECT_EQ(file.Closes, 0u);
        entry.NullHandle = false;

        file.StatError   = VFS::VFSError::IOError;
        for (uint32_t boundary : {1u, 2u})
        {
            file.Stats         = 0;
            file.StatFailureAt = boundary;
            EXPECT_EQ(Capture().Error(), VFS::VFSError::IOError);
            EXPECT_TRUE(Snapshot.Bytes().empty());
            EXPECT_FALSE(Route.InUse);
        }
        EXPECT_EQ(file.Closes, 2u);
        EXPECT_EQ(Route.Allocations, Route.Frees);
        file.StatError = VFS::VFSError::OK;
        file.Stats     = 0;
        ASSERT_TRUE(Capture().Succeeded());
        ASSERT_EQ(Snapshot.Bytes().size(), 128u);
        EXPECT_EQ(Snapshot.Bytes()[127], 127u);
        Snapshot.Reset();
        EXPECT_EQ(Route.Allocations, Route.Frees);
    }
} // namespace ZEngine::Cook
