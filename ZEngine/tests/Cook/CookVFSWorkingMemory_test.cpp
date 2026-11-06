#include <ZEngine/Cook/CookVFSWorkingMemory.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/Memory/NativePages.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <gtest/gtest.h>
#include <new>
#include <type_traits>
#include "../TemporaryDirectory.h"
#include "CookPublicationTestSupport.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookVFSWorkingMemoryTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(32), 4> Parent    = {};
        CookBudgetAllocator                 Allocator = {};
        CookVFSWorkingMemory                Budget    = {};
        static constexpr uint64_t           Maximum   = ZMega(8);

        void                                SetUp() override
        {
            ASSERT_TRUE(Allocator.Initialize(Parent, Maximum).Succeeded());
            ASSERT_TRUE(Budget.Initialize(Allocator).Succeeded());
        }
        void TearDown() override
        {
            EXPECT_EQ(Budget.ReservationCount(), 0u);
            EXPECT_TRUE(Budget.Reset().Succeeded());
            EXPECT_EQ(Allocator.OutstandingAllocations(), 0u);
            EXPECT_EQ(Parent.Statistics().LiveBytes, 0u);
        }
        static VFS::VFSPath Path(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }
        static VFS::VFSRelativePath Relative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }
    };

    TEST_F(CookVFSWorkingMemoryTest, InlineAccountingAllocatesNothingAndDefaultsFailClosed)
    {
        static_assert(!std::is_copy_constructible_v<CookVFSWorkingMemory>);
        static_assert(!std::is_move_constructible_v<CookVFSWorkingMemory>);
        static_assert(std::is_nothrow_move_constructible_v<VFS::VFSWorkingMemoryReservation>);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, sizeof(Allocator) + sizeof(Budget));
        EXPECT_EQ(Parent.Allocations, 0u);
        EXPECT_EQ(Budget.Initialize(Allocator).Error(), VFS::VFSError::AlreadyExists);
        VFS::VFSWorkingMemoryReservation reservation = {};
        EXPECT_EQ(reservation.Acquire(Budget, {}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(
            reservation
                .Acquire(
                    Budget,
                    {
                    .Storage = {&Budget, 0}
        })
                .Error(),
            VFS::VFSError::InvalidPath);
        EXPECT_EQ(
            reservation
                .Acquire(
                    Budget,
                    {
                    .Storage = {nullptr, 1}
        })
                .Error(),
            VFS::VFSError::InvalidPath);
        EXPECT_EQ(
            reservation
                .Acquire(
                    Budget,
                    {
                    .Storage = {&Budget, 1},
                      .Mapping = {&Budget, 1}
        })
                .Error(),
            VFS::VFSError::InvalidPath);
        ASSERT_TRUE(reservation.Acquire(Budget, {.StateBytes = 10}).Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), 10u);
        EXPECT_EQ(Budget.Reset().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(reservation.Acquire(Budget, {.StateBytes = 1}).Error(), VFS::VFSError::InvalidPath);
        reservation.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
        EXPECT_EQ(Parent.Allocations, 0u);
        CookVFSWorkingMemory inactive = {};
        EXPECT_EQ(reservation.Acquire(inactive, {.StateBytes = 1}).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookVFSWorkingMemoryTest, SharedDomainsAreChargedOnceAndMoveTransfersOneRelease)
    {
        int                              storage = 0, mapping = 0;
        VFS::VFSWorkingMemoryReservation first = {}, second = {};
        ASSERT_TRUE(first
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 100, .Storage = {&storage, 200},
                                 .Mapping = {&mapping, 300}
        })
                        .Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), 600u);
        ASSERT_TRUE(second
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 50, .Storage = {&storage, 200},
                                 .Mapping = {&mapping, 300}
        })
                        .Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), 650u);
        EXPECT_EQ(Budget.DomainCount(), 2u);
        VFS::VFSWorkingMemoryReservation moved(std::move(first));
        EXPECT_EQ(first.Budget(), nullptr);
        EXPECT_EQ(Budget.ReservationCount(), 2u);
        moved = std::move(second);
        EXPECT_EQ(second.Budget(), nullptr);
        EXPECT_EQ(Budget.ReservedBytes(), 550u);
        EXPECT_EQ(Budget.ReservationCount(), 1u);
        moved.Reset();
        EXPECT_EQ(Budget.DomainCount(), 0u);
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
        EXPECT_EQ(Parent.Allocations, 0u);
    }

    TEST_F(CookVFSWorkingMemoryTest, InconsistentCapacitiesOverflowAndRefusalPreserveLiveReservations)
    {
        int                              storage = 0;
        VFS::VFSWorkingMemoryReservation first = {}, refused = {};
        ASSERT_TRUE(first
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 100, .Storage = {&storage, 200}
        })
                        .Succeeded());
        EXPECT_EQ(
            refused
                .Acquire(
                    Budget,
                    {
                    .StateBytes = 100, .Storage = {&storage, 201}
        })
                .Error(),
            VFS::VFSError::Corrupted);
        EXPECT_EQ(refused.Acquire(Budget, {.StateBytes = UINT64_MAX}).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(
            refused
                .Acquire(
                    Budget,
                    {
                    .StateBytes = 1, .Mapping = {&refused, UINT64_MAX}
        })
                .Error(),
            VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Budget.ReservedBytes(), 300u);
        EXPECT_EQ(Budget.ReservationCount(), 1u);
        CookMemoryCharge blocker = {};
        ASSERT_TRUE(Allocator.TryCharge(Maximum - Allocator.Statistics().LiveBytes, CookMemoryCategory::Codecs, &blocker).Succeeded());
        EXPECT_EQ(
            refused
                .Acquire(
                    Budget,
                    {
                    .StateBytes = 1, .Storage = {&storage, 200}
        })
                .Error(),
            VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(refused.Budget(), nullptr);
        EXPECT_EQ(Budget.ReservedBytes(), 300u);
        EXPECT_EQ(Parent.Allocations, 0u);
        first.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
    }

    TEST_F(CookVFSWorkingMemoryTest, LargeDomainSetsGrowGeometricallyThroughEngineStorageAndReleaseOldBacking)
    {
        int                                                                identities[40] = {};
        Core::Containers::FixedArray<VFS::VFSWorkingMemoryReservation, 40> reservations   = {};
        for (size_t i = 0; i < reservations.size(); ++i)
        {
            ASSERT_TRUE(
                reservations[i]
                    .Acquire(
                        Budget,
                        {
                        .StateBytes = 1, .Storage = {&identities[i], 10}
            })
                    .Succeeded());
            EXPECT_EQ(Budget.ReservedBytes(), (i + 1) * 11);
            if (i < 8)
            {
                EXPECT_EQ(Parent.Allocations, 0u);
            }
        }
        EXPECT_EQ(Parent.Allocations, 3u);
        EXPECT_EQ(Parent.Frees, 2u);
        EXPECT_EQ(Allocator.OutstandingAllocations(), 1u);
        for (auto& reservation : reservations)
        {
            reservation.Reset();
        }
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
        ASSERT_TRUE(Budget.Reset().Succeeded());
        EXPECT_EQ(Parent.Frees, Parent.Allocations);
        ASSERT_TRUE(Budget.Initialize(Allocator).Succeeded());
        EXPECT_EQ(Parent.Allocations, 3u);
    }

    TEST_F(CookVFSWorkingMemoryTest, EveryGrowthFailurePreservesTheExistingDomainIndex)
    {
        int                                                                identities[33] = {};
        Core::Containers::FixedArray<VFS::VFSWorkingMemoryReservation, 33> reservations   = {};
        for (size_t i = 0; i < reservations.size(); ++i)
        {
            if (i == 8 || i == 16 || i == 32)
            {
                const auto before = Budget.ReservedBytes();
                Parent.Failure    = VFS::VFSError::OutOfMemory;
                EXPECT_EQ(
                    reservations[i]
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 1, .Storage = {&identities[i], 10}
                })
                        .Error(),
                    VFS::VFSError::OutOfMemory);
                EXPECT_EQ(Budget.ReservedBytes(), before);
                EXPECT_EQ(Budget.DomainCount(), i);
                EXPECT_EQ(reservations[i].Budget(), nullptr);
                Parent.Failure = VFS::VFSError::OK;
            }
            ASSERT_TRUE(
                reservations[i]
                    .Acquire(
                        Budget,
                        {
                        .StateBytes = 1, .Storage = {&identities[i], 10}
            })
                    .Succeeded());
        }
        for (auto& reservation : reservations)
        {
            reservation.Reset();
        }
    }

    TEST_F(CookVFSWorkingMemoryTest, ExistingDomainReferencesNeverNeedCompactionStorage)
    {
        int                              identities[5] = {};
        VFS::VFSWorkingMemoryReservation anchor        = {};
        ASSERT_TRUE(anchor
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 1, .Storage = {&identities[0], 2}
        })
                        .Succeeded());
        for (size_t i = 1; i < 5; ++i)
        {
            VFS::VFSWorkingMemoryReservation current = {};
            ASSERT_TRUE(current
                            .Acquire(
                                Budget,
                                {
                                .StateBytes = 1, .Storage = {&identities[i], 2}
            })
                            .Succeeded());
        }
        const auto allocations                    = Parent.Allocations;
        Parent.Failure                            = VFS::VFSError::OutOfMemory;
        VFS::VFSWorkingMemoryReservation existing = {};
        ASSERT_TRUE(existing
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 1, .Storage = {&identities[0], 2}
        })
                        .Succeeded());
        EXPECT_EQ(Parent.Allocations, allocations);
        EXPECT_EQ(Budget.ReservedBytes(), 4u);
        EXPECT_EQ(Budget.DomainCount(), 1u);
    }

    TEST_F(CookVFSWorkingMemoryTest, DomainChurnCompactsTombstonesWithoutLosingSurvivingReservations)
    {
        int                              identities[64] = {};
        VFS::VFSWorkingMemoryReservation anchor         = {};
        ASSERT_TRUE(anchor
                        .Acquire(
                            Budget,
                            {
                            .StateBytes = 1, .Storage = {&identities[0], 2}
        })
                        .Succeeded());
        for (size_t i = 1; i < 64; ++i)
        {
            VFS::VFSWorkingMemoryReservation current = {};
            ASSERT_TRUE(current
                            .Acquire(
                                Budget,
                                {
                                .StateBytes = 1, .Storage = {&identities[i], 2}
            })
                            .Succeeded());
            EXPECT_EQ(Budget.DomainCount(), 2u);
            EXPECT_EQ(Budget.ReservedBytes(), 6u);
        }
        EXPECT_EQ(Budget.ReservedBytes(), 3u);
        EXPECT_GT(Parent.Frees, 0u);
        EXPECT_LE(Allocator.OutstandingAllocations(), 1u);
    }

    TEST_F(CookVFSWorkingMemoryTest, OwnedMemoryPoolAndMappingAreSharedAcrossBudgetedParentChildAndBorrowedRoots)
    {
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        VFS::VFSContext       context = {};
        ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        ASSERT_TRUE(backend.CreateDir(Path("/output/target")).Succeeded());
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        const auto profile = backend.RootProvider()->MemoryRequirements();
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_GE(profile.Value().Storage.Bytes, ZKilo(512));
        const auto page = Core::Memory::NativePageSize();
        ASSERT_GT(page, 0u);
        EXPECT_EQ(profile.Value().StateBytes % page, 0u);
        EXPECT_EQ(profile.Value().Storage.Bytes % page, 0u);
        VFS::VFSPublicationRootLease parent = {}, child = {};
        VFS::VFSReadRootLease        borrowed = {};
        ASSERT_TRUE(context.AcquirePublicationRoot(Path("/output"), &parent, &Budget).Succeeded());
        const auto first = Budget.ReservedBytes();
        EXPECT_GT(first, profile.Value().Storage.Bytes + profile.Value().StateBytes);
        EXPECT_EQ((first - profile.Value().Storage.Bytes - profile.Value().StateBytes) % page, 0u);
        ASSERT_TRUE(parent.AcquireChildRoot(Relative("target"), &child).Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), first + profile.Value().StateBytes);
        EXPECT_EQ(Budget.DomainCount(), 2u);
        EXPECT_EQ(Parent.Allocations, 0u);
        ASSERT_TRUE(context.AcquireReadRoot(Path("/output"), &borrowed).Succeeded());
        VFS::VFSWorkingMemoryReservation borrowed_memory = {};
        ASSERT_TRUE(borrowed.ReserveWorkingMemory(Budget, &borrowed_memory).Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), first + 2 * profile.Value().StateBytes);
        VFS::VFSWorkingMemoryReservation duplicate = {};
        EXPECT_EQ(parent.ReserveWorkingMemory(Budget, &duplicate).Error(), VFS::VFSError::AlreadyExists);
        parent.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), first + profile.Value().StateBytes);
        context.Shutdown();
        borrowed.Reset();
        borrowed_memory.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), first);
        const uint8_t bytes[] = {1, 2, 3};
        ASSERT_TRUE(VFS::WriteFileAtomically(child, Relative("manifest.json"), bytes).Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), first);
        child.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
        EXPECT_EQ(Parent.Allocations, 0u);
    }

    TEST_F(CookVFSWorkingMemoryTest, UnsupportedProviderBoundsFailClosedEvenWhenRootCapabilitiesAreOtherwiseValid)
    {
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        ASSERT_TRUE(backend.CreateDir(Path("/output")).Succeeded());
        Tests::PublicationFaults faults   = {};
        faults.MemoryBounds               = VFS::VFSError::Unsupported;
        Tests::PublicationBackend wrapper = {};
        wrapper.Inner                     = &backend;
        wrapper.Provider                  = new (std::nothrow) Tests::PublicationProvider();
        ASSERT_NE(wrapper.Provider, nullptr);
        wrapper.Provider->Inner  = backend.RootProvider();
        wrapper.Provider->Faults = &faults;
        VFS::VFSContext context  = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&wrapper, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease root = {};
        EXPECT_EQ(context.AcquirePublicationRoot(Path("/output"), &root, &Budget).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(root.IsValid());
        EXPECT_EQ(Budget.ReservationCount(), 0u);
        ASSERT_TRUE(context.AcquirePublicationRoot(Path("/output"), &root).Succeeded());
        VFS::VFSWorkingMemoryReservation reservation = {};
        EXPECT_EQ(root.ReserveWorkingMemory(Budget, &reservation).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(reservation.Budget(), nullptr);
        root.Reset();
        ASSERT_TRUE(context.Unmount(VFS::VFSPath::Root()).Succeeded());
        context.Shutdown();
    }

    TEST_F(CookVFSWorkingMemoryTest, IndependentContextMappingsDoNotDuplicateTheirSharedBackendPool)
    {
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        ASSERT_TRUE(backend.CreateDir(Path("/output")).Succeeded());
        VFS::VFSContext first_context = {}, second_context = {};
        first_context.Initialize(&memory.MainArena);
        second_context.Initialize(&memory.MainArena);
        ASSERT_TRUE(first_context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        ASSERT_TRUE(second_context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease first = {}, second = {};
        ASSERT_TRUE(first_context.AcquirePublicationRoot(Path("/output"), &first, &Budget).Succeeded());
        const auto baseline      = Budget.ReservedBytes();
        const auto profile       = backend.RootProvider()->MemoryRequirements().Value();
        const auto mapping_bytes = baseline - profile.StateBytes - profile.Storage.Bytes;
        ASSERT_TRUE(second_context.AcquirePublicationRoot(Path("/output"), &second, &Budget).Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), baseline + profile.StateBytes + mapping_bytes);
        EXPECT_EQ(Budget.DomainCount(), 3u);
        first_context.Shutdown();
        first.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), baseline);
        second_context.Shutdown();
        second.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
    }

    TEST_F(CookVFSWorkingMemoryTest, FailedRootAndChildAcquisitionsRestoreAllReservationsAndDoNotCreateFiles)
    {
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        VFS::VFSContext       context = {};
        ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        ASSERT_TRUE(backend.CreateDir(Path("/output/target")).Succeeded());
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease parent = {}, child = {};
        EXPECT_EQ(context.AcquirePublicationRoot(Path("/missing"), &parent, &Budget).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Budget.ReservationCount(), 0u);
        ASSERT_TRUE(context.AcquirePublicationRoot(Path("/output"), &parent, &Budget).Succeeded());
        const auto live = Budget.ReservedBytes();
        EXPECT_EQ(parent.AcquireChildRoot(Relative("missing"), &child).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Budget.ReservedBytes(), live);
        CookMemoryCharge blocker = {};
        ASSERT_TRUE(Allocator.TryCharge(Maximum - Allocator.Statistics().LiveBytes, CookMemoryCategory::Codecs, &blocker).Succeeded());
        EXPECT_EQ(parent.AcquireChildRoot(Relative("target"), &child).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_FALSE(child.IsValid());
        EXPECT_EQ(Budget.ReservedBytes(), live);
        EXPECT_FALSE(backend.Exists(Path("/output/missing")));
        blocker.Release();
        parent.Reset();
        ASSERT_TRUE(context.Unmount(VFS::VFSPath::Root()).Succeeded());
        context.Shutdown();
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    TEST_F(CookVFSWorkingMemoryTest, NativeRootStorageRemainsChargedUntilTheLastPinnedChildIsReleased)
    {
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("cook_vfs_budget", error));
        ASSERT_TRUE(std::filesystem::create_directories(directory.Path() / "output/target", error));
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSDiskBackend backend = {};
        const auto          native  = directory.Path().generic_u8string();
        backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &memory.MainArena);
        VFS::VFSContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        const auto profile = backend.RootProvider()->MemoryRequirements();
        ASSERT_TRUE(profile.Succeeded());
        VFS::VFSPublicationRootLease parent = {}, child = {};
        ASSERT_TRUE(context.AcquirePublicationRoot(Path("/output"), &parent, &Budget).Succeeded());
        const auto first = Budget.ReservedBytes();
        ASSERT_TRUE(parent.AcquireChildRoot(Relative("target"), &child).Succeeded());
        EXPECT_EQ(Budget.ReservedBytes(), first + profile.Value().StateBytes);
        parent.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), first);
        context.Shutdown();
        backend.RootProvider()->Retire();
        const uint8_t bytes[] = {1, 2, 3};
        ASSERT_TRUE(VFS::WriteFileAtomically(child, Relative("manifest.json"), bytes).Succeeded());
        child.Reset();
        EXPECT_EQ(Budget.ReservedBytes(), 0u);
        EXPECT_EQ(Parent.Allocations, 0u);
    }
#endif
} // namespace ZEngine::Cook
