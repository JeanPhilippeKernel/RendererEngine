#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <new>

#if defined(__linux__)
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

extern char** environ;
#endif

using namespace ZEngine;
using namespace ZEngine::Core::Memory;

TEST(AllocatorTest, ArenaInit)
{
    MemoryManager manager{};
    manager.Initialize(200, {});
    manager.Shutdown();
}

TEST(AllocatorTest, TrackerStorageIsPageRoundedAndChargedOnlyByItsOwningRoot)
{
    ArenaAllocator root = {}, child = {}, borrowed = {};
    EXPECT_EQ(root.AllocationTrackingBytes(), 0u);
    root.Initialize(ZKilo(64), 0, "TrackerStorageTest");
    ASSERT_NE(root.m_memory, nullptr);
    const auto tracking = root.AllocationTrackingBytes();
    EXPECT_GT(tracking, 0u);
    EXPECT_EQ(tracking % root.m_mem_page_size, 0u);
    root.CreateSubArena(root.m_mem_page_size, &child, "SharedTrackerChild");
    ASSERT_NE(child.m_memory, nullptr);
    ASSERT_NE(child.AllocateNoZero(1, 1), nullptr);
    EXPECT_EQ(child.AllocationTrackingBytes(), 0u);
    EXPECT_EQ(root.AllocationTrackingBytes(), tracking);
    child.Shutdown();
    alignas(16) uint8_t bytes[64] = {};
    ASSERT_TRUE(borrowed.InitializeBorrowed(bytes, sizeof(bytes)));
    EXPECT_EQ(borrowed.AllocationTrackingBytes(), 0u);
    ArenaAllocator moved(std::move(root));
    EXPECT_EQ(root.AllocationTrackingBytes(), 0u);
    EXPECT_EQ(moved.AllocationTrackingBytes(), tracking);
    moved.Shutdown();
    EXPECT_EQ(moved.AllocationTrackingBytes(), 0u);
}

TEST(AllocatorTest, RetainedStorageCountsNativePagesAcrossDiscontiguousRootAndChildAllocations)
{
    ZEngine::Core::Memory::ArenaAllocator       root = {}, first = {}, second = {};
    ZEngine::Core::Memory::ArenaRetainedStorage snapshot = {};
    root.Initialize(ZMega(4), 0, "RetainedStorageRoot");
    ASSERT_NE(root.m_memory, nullptr);
    const auto page = root.m_mem_page_size;
    ASSERT_TRUE(root.RetainedStorage(&snapshot));
    EXPECT_NE(snapshot.Identity, nullptr);
    EXPECT_EQ(snapshot.Owner, &root);
    EXPECT_EQ(snapshot.CommittedBytes, 0u);
    EXPECT_EQ(snapshot.TrackingBytes, root.AllocationTrackingBytes());
    EXPECT_EQ(snapshot.ReservedAddressBytes, root.m_reserved_size);
    ASSERT_NE(root.AllocateNoZero(1, 1), nullptr);
    root.CreateSubArena(4 * page, &first);
    root.CreateSubArena(4 * page, &second);
    ASSERT_NE(first.AllocateNoZero(1, 1), nullptr);
    ASSERT_NE(second.AllocateNoZero(1, 1), nullptr);
    ASSERT_NE(root.AllocateNoZero(1, 1), nullptr);
    ASSERT_TRUE(root.RetainedStorage(&snapshot));
    EXPECT_EQ(snapshot.CommittedBytes, 4 * page);
    EXPECT_EQ(root.m_committed_size, 2 * page);
    EXPECT_EQ(first.m_committed_size, page);
    EXPECT_EQ(second.m_committed_size, page);
    for (const auto* arena : {&first, &second})
    {
        ZEngine::Core::Memory::ArenaRetainedStorage child = {};
        ASSERT_TRUE(arena->RetainedStorage(&child));
        EXPECT_EQ(child.Identity, snapshot.Identity);
        EXPECT_EQ(child.CommittedBytes, snapshot.CommittedBytes);
        EXPECT_EQ(child.TrackingBytes, snapshot.TrackingBytes);
        EXPECT_EQ(child.ReservedAddressBytes, snapshot.ReservedAddressBytes);
        EXPECT_EQ(child.Owner, &root);
    }
    first.Clear();
    ASSERT_NE(first.AllocateNoZero(1, 1), nullptr);
    ASSERT_TRUE(root.RetainedStorage(&snapshot));
    EXPECT_EQ(snapshot.CommittedBytes, 4 * page); // Reusing a page does not charge it twice.
    first.Shutdown();
    second.Shutdown();
    ASSERT_TRUE(root.RetainedStorage(&snapshot));
    EXPECT_EQ(snapshot.CommittedBytes, 4 * page); // Child teardown does not unmap root pages.
    ZEngine::Core::Memory::ArenaAllocator       moved(std::move(root));
    ZEngine::Core::Memory::ArenaRetainedStorage retained = {};
    EXPECT_FALSE(root.RetainedStorage(&retained));
    ASSERT_TRUE(moved.RetainedStorage(&retained));
    EXPECT_EQ(retained.Identity, snapshot.Identity);
    EXPECT_EQ(retained.Owner, &moved);
    EXPECT_EQ(retained.CommittedBytes, snapshot.CommittedBytes);
    moved.Shutdown();
    EXPECT_FALSE(moved.RetainedStorage(&retained));
    EXPECT_EQ(retained.CommittedBytes, snapshot.CommittedBytes); // Failure preserves output.
}

TEST(AllocatorTest, RetainedStorageQueriesRejectUnknownBackingAndDoNotCommitOrAdvanceCursors)
{
    ZEngine::Core::Memory::ArenaAllocator       root = {}, borrowed = {};
    ZEngine::Core::Memory::ArenaRetainedStorage retained = {.Identity = &root, .CommittedBytes = 73};
    EXPECT_FALSE(root.RetainedStorage(&retained));
    EXPECT_EQ(retained.CommittedBytes, 73u);
    alignas(16) uint8_t storage[128] = {};
    ASSERT_TRUE(borrowed.InitializeBorrowed(storage, sizeof(storage)));
    EXPECT_FALSE(borrowed.RetainedStorage(&retained));
    EXPECT_EQ(retained.CommittedBytes, 73u);
    root.Initialize(ZMega(4), 0);
    ASSERT_NE(root.AllocateNoZero(1, 1), nullptr);
    const auto offset = root.m_current_offset;
    const auto pages  = root.m_committed_size;
    for (size_t repeat = 0; repeat < 128; ++repeat)
    {
        ASSERT_TRUE(root.RetainedStorage(&retained));
    }
    EXPECT_EQ(root.m_current_offset, offset);
    EXPECT_EQ(root.m_committed_size, pages);
    EXPECT_EQ(retained.CommittedBytes, pages);
    EXPECT_FALSE(root.RetainedStorage(nullptr));
}

TEST(AllocatorTest, NativeOwnerIdentityFollowsMovesWhileSubArenaViewsRemainAlive)
{
    ArenaAllocator root = {}, child = {}, destination = {};
    root.Initialize(ZMega(4), 0);
    root.CreateSubArena(ZKilo(64), &child);
    ASSERT_NE(child.AllocateNoZero(1, 1), nullptr);
    ArenaRetainedStorage before = {};
    ASSERT_TRUE(child.RetainedStorage(&before));
    EXPECT_EQ(before.Owner, &root);
    ArenaAllocator       moved(std::move(root));
    ArenaRetainedStorage after = {};
    ASSERT_TRUE(child.RetainedStorage(&after));
    EXPECT_EQ(after.Identity, before.Identity);
    EXPECT_EQ(after.CommittedBytes, before.CommittedBytes);
    EXPECT_EQ(after.Owner, &moved);
    destination = std::move(moved);
    ASSERT_TRUE(child.RetainedStorage(&after));
    EXPECT_EQ(after.Identity, before.Identity);
    EXPECT_EQ(after.Owner, &destination);
    ArenaAllocator moved_child(std::move(child));
    ASSERT_TRUE(moved_child.RetainedStorage(&after));
    EXPECT_EQ(after.Owner, &destination);
    moved_child.Shutdown();
    destination.Shutdown();
}

TEST(AllocatorTest, RetainsStorageChecksEveryCommittedPageInTheWholeNativeFamilyWithoutMutation)
{
    ArenaAllocator root = {}, first = {}, second = {};
    root.Initialize(ZMega(4), 0);
    ASSERT_NE(root.m_memory, nullptr);
    const auto page = root.m_mem_page_size;
    root.CreateSubArena(4 * page, &first);
    root.CreateSubArena(4 * page, &second);
    auto* one = first.AllocateNoZero(page + 1, 1);
    auto* two = second.AllocateNoZero(1, 1);
    ASSERT_NE(one, nullptr);
    ASSERT_NE(two, nullptr);
    const auto offset    = root.m_current_offset;
    const auto committed = root.m_committed_size;
    for (size_t repeat = 0; repeat < 128; ++repeat)
    {
        EXPECT_TRUE(root.RetainsStorage(one, 2 * page));
        EXPECT_TRUE(first.RetainsStorage(two, 1));
        EXPECT_TRUE(second.RetainsStorage(one, 1));
        EXPECT_FALSE(root.RetainsStorage(nullptr, 1));
        EXPECT_FALSE(root.RetainsStorage(one, 0));
        EXPECT_FALSE(root.RetainsStorage(one, SIZE_MAX));
        EXPECT_FALSE(root.RetainsStorage(&root, 1));
        EXPECT_FALSE(root.RetainsStorage(one, 2 * page + 1));
        EXPECT_FALSE(root.RetainsStorage(one, 4 * page + 1)); // Commits on both ends do not cover the gap.
        EXPECT_FALSE(root.RetainsStorage(root.m_memory + root.m_reserved_size, 1));
    }
    EXPECT_EQ(root.m_current_offset, offset);
    EXPECT_EQ(root.m_committed_size, committed);
    first.Shutdown();
    EXPECT_TRUE(root.RetainsStorage(one, 1)); // Root retains the native pages, not the object's lifetime.
    second.Shutdown();
    root.Shutdown();
    EXPECT_FALSE(root.RetainsStorage(one, 1));
    alignas(16) uint8_t bytes[64] = {};
    ArenaAllocator      borrowed  = {};
    ASSERT_TRUE(borrowed.InitializeBorrowed(bytes, sizeof(bytes)));
    EXPECT_FALSE(borrowed.RetainsStorage(bytes, 1));
}

TEST(AllocatorBorrowedTest, CheckedViewPreservesBackingAndBoundsAcrossMoveAndShutdown)
{
    alignas(16) uint8_t backing[256] = {};
    ArenaAllocator      arena        = {};
    ASSERT_TRUE(arena.InitializeBorrowed(backing, sizeof(backing), "TestBorrowed"));
    ASSERT_EQ(arena.AllocateNoZero(32, 16), backing);
    backing[0]        = 73;
    const auto cursor = arena.m_current_offset;
    EXPECT_EQ(arena.AllocateNoZero(sizeof(backing), 16), nullptr);
    EXPECT_EQ(arena.m_current_offset, cursor);
    EXPECT_NE(arena.LastFailure().Kind, ArenaAllocationFailureKind::None);
    ASSERT_NE(arena.AllocateNoZero(16, 16), nullptr);
    EXPECT_EQ(arena.LastFailure().Kind, ArenaAllocationFailureKind::None);
    EXPECT_FALSE(arena.InitializeBorrowed(backing, sizeof(backing)));
    ArenaAllocator moved = std::move(arena);
    EXPECT_EQ(arena.m_memory, nullptr);
    EXPECT_TRUE(moved.m_is_borrowed);
    moved.Shutdown();
    EXPECT_EQ(backing[0], 73);
    EXPECT_EQ(moved.m_memory, nullptr);
    EXPECT_FALSE(moved.m_is_borrowed);
    ASSERT_TRUE(moved.InitializeBorrowed(backing, sizeof(backing)));
    EXPECT_EQ(moved.AllocateNoZero(sizeof(backing), 1), backing);
}

TEST(AllocatorBorrowedTest, ChildViewAndTempRollbackNeverReleaseOrClearCallerStorage)
{
    alignas(16) uint8_t backing[256] = {};
    ArenaAllocator      arena = {}, child = {};
    ASSERT_TRUE(arena.InitializeBorrowed(backing, sizeof(backing)));
    arena.CreateSubArena(64, &child, "BorrowedChild");
    EXPECT_TRUE(child.m_is_borrowed);
    EXPECT_EQ(child.AllocateNoZero(64, 1), backing);
    backing[0]      = 17;
    const auto mark = BeginTempArena(&arena);
    ASSERT_NE(arena.AllocateNoZero(64, 16), nullptr);
    EndTempArena(mark);
    EXPECT_EQ(arena.m_current_offset, 64u);
    child.Shutdown();
    EXPECT_EQ(backing[0], 17);
    arena.Clear();
    EXPECT_EQ(arena.AllocateNoZero(256, 1), backing);
}

TEST(AllocatorBorrowedTest, InvalidViewLeavesAnUninitializedArenaUnchanged)
{
    uint8_t        backing[16] = {};
    ArenaAllocator arena       = {};
    EXPECT_FALSE(arena.InitializeBorrowed(nullptr, 1));
    EXPECT_FALSE(arena.InitializeBorrowed(backing, 0));
    EXPECT_FALSE(arena.InitializeBorrowed(backing, SIZE_MAX));
    EXPECT_EQ(arena.m_memory, nullptr);
    EXPECT_EQ(arena.m_current_offset, 0u);
}

namespace ZEngine::Core::Memory::Tests
{
    struct GrowthRoute final : IAllocationRoute
    {
        struct Block
        {
            void*  Pointer = nullptr;
            size_t Bytes   = 0;
        };
        Block  Blocks[64] = {};
        size_t Calls      = 0;
        size_t Frees      = 0;
        size_t FailAt     = 0;
        size_t LiveBytes  = 0;

        void*  Allocate(size_t bytes) noexcept override
        {
            if (++Calls == FailAt)
            {
                return nullptr;
            }
            for (auto& block : Blocks)
            {
                if (block.Pointer)
                {
                    continue;
                }
                void* memory = ::operator new(bytes, std::align_val_t(DEFAULT_ALIGNMENT), std::nothrow);
                if (!memory)
                {
                    return nullptr;
                }
                block      = {memory, bytes};
                LiveBytes += bytes;
                return memory;
            }
            ADD_FAILURE() << "Growth-route tracking slots exhausted.";
            return nullptr;
        }
        void* Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept override
        {
            void* memory = Allocate(new_bytes);
            if (memory && pointer)
            {
                Helpers::secure_memcpy(memory, new_bytes, pointer, std::min(old_bytes, new_bytes));
                Free(pointer);
            }
            return memory;
        }
        void Free(void* pointer) noexcept override
        {
            if (!pointer)
            {
                return;
            }
            for (auto& block : Blocks)
            {
                if (block.Pointer != pointer)
                {
                    continue;
                }
                LiveBytes -= block.Bytes;
                block      = {};
                ++Frees;
                ::operator delete(pointer, std::align_val_t(DEFAULT_ALIGNMENT));
                return;
            }
            ADD_FAILURE() << "Growth-route foreign or duplicate release.";
        }
        ~GrowthRoute() override
        {
            EXPECT_EQ(LiveBytes, 0u);
            for (auto& block : Blocks)
            {
                if (block.Pointer)
                {
                    Free(block.Pointer);
                }
            }
        }
    };

    TEST(AllocatorGrowingTest, InvalidInitializationPreservesStateAndDoesNotAllocate)
    {
        alignas(16) uint8_t bytes[64] = {};
        GrowthRoute         route     = {};
        ArenaAllocator      arena     = {};
        EXPECT_FALSE(arena.InitializeGrowingBorrowed(nullptr, 64, 128, route));
        EXPECT_FALSE(arena.InitializeGrowingBorrowed(bytes, 0, 128, route));
        EXPECT_FALSE(arena.InitializeGrowingBorrowed(bytes, 64, 32, route));
        EXPECT_EQ(arena.m_memory, nullptr);
        ASSERT_TRUE(arena.InitializeGrowingBorrowed(bytes, sizeof(bytes), 1024, route));
        EXPECT_FALSE(arena.InitializeGrowingBorrowed(bytes, sizeof(bytes), 1024, route));
        EXPECT_EQ(arena.m_total_size, 1024u);
        EXPECT_EQ(arena.m_committed_size, sizeof(bytes));
        EXPECT_EQ(arena.AllocationTrackingBytes(), 0u);
        EXPECT_EQ(route.Calls, 0u);
        ArenaAllocator child = {};
        arena.CreateSubArena(16, &child);
        EXPECT_EQ(child.m_memory, nullptr);
        EXPECT_EQ(arena.m_current_offset, 0u);
    }

    TEST(AllocatorGrowingTest, RetainedStorageCannotInferExternalBackingBeforeOrAfterRoutedGrowth)
    {
        alignas(16) uint8_t initial[64] = {};
        GrowthRoute         route       = {};
        ArenaAllocator      arena       = {};
        ASSERT_TRUE(arena.InitializeGrowingBorrowed(initial, sizeof(initial), ZKilo(8), route));
        ArenaRetainedStorage snapshot = {.Identity = &route, .CommittedBytes = 73};
        EXPECT_FALSE(arena.RetainedStorage(&snapshot));
        ASSERT_NE(arena.AllocateNoZero(ZKilo(1), 16), nullptr);
        const auto calls  = route.Calls;
        const auto bytes  = route.LiveBytes;
        const auto offset = arena.m_current_offset;
        EXPECT_GT(bytes, 0u);
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            EXPECT_FALSE(arena.RetainedStorage(&snapshot));
        }
        EXPECT_EQ(snapshot.Identity, &route);
        EXPECT_EQ(snapshot.CommittedBytes, 73u);
        EXPECT_EQ(route.Calls, calls);
        EXPECT_EQ(route.LiveBytes, bytes);
        EXPECT_EQ(arena.m_current_offset, offset);
    }

    TEST(AllocatorGrowingTest, StableGeometricBlocksAreReusedAfterCrossBlockRewinds)
    {
        alignas(16) uint8_t initial[64] = {};
        GrowthRoute         route       = {};
        ArenaAllocator      arena       = {};
        ASSERT_TRUE(arena.InitializeGrowingBorrowed(initial, sizeof(initial), ZKilo(8), route));
        auto* retained = static_cast<uint8_t*>(arena.Allocate(48, 16));
        ASSERT_EQ(retained, initial);
        retained[0]           = 71;
        const auto mark       = BeginTempArena(&arena);
        void*      blocks[20] = {};
        for (auto& block : blocks)
        {
            block = arena.Allocate(160, 16);
            ASSERT_NE(block, nullptr);
        }
        EXPECT_LE(route.Calls, 5u);
        EXPECT_GT(route.LiveBytes, arena.m_committed_size - sizeof(initial)); // Includes inline headers.
        EXPECT_LE(arena.m_committed_size, arena.m_total_size);
        const auto calls = route.Calls;
        const auto bytes = route.LiveBytes;
        for (size_t iteration = 0; iteration < 8; ++iteration)
        {
            EndTempArena(mark);
            for (const auto block : blocks)
            {
                EXPECT_EQ(arena.Allocate(160, 16), block);
            }
            EXPECT_EQ(route.Calls, calls);
            EXPECT_EQ(route.LiveBytes, bytes);
            EXPECT_EQ(retained[0], 71);
        }
        arena.Clear();
        EXPECT_EQ(arena.Allocate(48, 16), retained);
        arena.Shutdown();
        EXPECT_EQ(route.LiveBytes, 0u);
        EXPECT_EQ(route.Frees, calls);
        EXPECT_EQ(initial[0], 0u); // Clear does not zero; the final Allocate does.
    }

    TEST(AllocatorGrowingTest, FailedGrowthAndHardCapacityPreserveCursorBackingAndOldValues)
    {
        alignas(16) uint8_t initial[64] = {};
        GrowthRoute         route       = {};
        ArenaAllocator      arena       = {};
        ASSERT_TRUE(arena.InitializeGrowingBorrowed(initial, sizeof(initial), 256, route));
        auto* retained = static_cast<uint8_t*>(arena.Allocate(64));
        ASSERT_NE(retained, nullptr);
        retained[0]  = 93;
        route.FailAt = 1;
        EXPECT_EQ(arena.TryResize(retained, 64, 96), nullptr);
        EXPECT_EQ(arena.m_current_offset, 64u);
        EXPECT_EQ(arena.m_previous_offset, 0u);
        EXPECT_EQ(arena.m_committed_size, 64u);
        EXPECT_EQ(arena.LastFailure().Kind, ArenaAllocationFailureKind::CommitFailed);
        EXPECT_EQ(retained[0], 93);
        route.FailAt = 0;
        auto* grown  = static_cast<uint8_t*>(arena.TryResize(retained, 64, 96));
        ASSERT_NE(grown, nullptr);
        EXPECT_EQ(grown[0], 93);
        EXPECT_EQ(arena.LastFailure().Kind, ArenaAllocationFailureKind::None);
        const auto calls  = route.Calls;
        const auto offset = arena.m_current_offset;
        EXPECT_EQ(arena.Allocate(257), nullptr);
        EXPECT_EQ(arena.m_current_offset, offset);
        EXPECT_EQ(route.Calls, calls);
        EXPECT_EQ(arena.LastFailure().Kind, ArenaAllocationFailureKind::CapacityExceeded);
    }

    TEST(AllocatorGrowingTest, ResizeValidatesRegionsAndHandlesInPlaceGrowthShrinkAndAlignment)
    {
        alignas(16) uint8_t initial[64] = {};
        GrowthRoute         route       = {};
        ArenaAllocator      arena       = {};
        ASSERT_TRUE(arena.InitializeGrowingBorrowed(initial, sizeof(initial), ZKilo(8), route));
        auto* first = static_cast<uint8_t*>(arena.Allocate(16));
        ASSERT_NE(first, nullptr);
        first[0] = 17;
        ASSERT_EQ(arena.TryResize(first, 16, 48), first);
        for (size_t index = 16; index < 48; ++index)
        {
            EXPECT_EQ(first[index], 0u);
        }
        auto* second = static_cast<uint8_t*>(arena.Allocate(100, 64));
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(second) % 64, 0u);
        second[0] = 29;
        EXPECT_EQ(arena.TryResize(second, 100, 80, 64), second);
        for (size_t index = 80; index < 100; ++index)
        {
            EXPECT_EQ(second[index], 0u);
        }
        auto* copy = static_cast<uint8_t*>(arena.TryResize(first, 48, 256));
        ASSERT_NE(copy, nullptr);
        EXPECT_EQ(copy[0], 17);
        EXPECT_EQ(first[0], 17);
        EXPECT_EQ(second[0], 29);
        const auto cursor      = arena.m_current_offset;
        uint8_t    foreign[16] = {};
        EXPECT_EQ(arena.TryResize(foreign, sizeof(foreign), 32), nullptr);
        EXPECT_EQ(arena.TryResize(first + 1, 15, 32, 16), nullptr);
        EXPECT_EQ(arena.TryResize(first, 65, 128), nullptr);
        EXPECT_EQ(arena.m_current_offset, cursor);
        const auto mark  = BeginTempArena(&arena);
        void*      stale = arena.Allocate(32);
        ASSERT_NE(stale, nullptr);
        EndTempArena(mark);
        EXPECT_EQ(arena.TryResize(stale, 32, 64), nullptr);
    }

    TEST(AllocatorGrowingTest, MoveAndReplacementReleaseOnlyTheOriginalRoutesGrowthBlocks)
    {
        alignas(16) uint8_t initial_a[64] = {}, initial_b[64] = {};
        GrowthRoute         route_a = {}, route_b = {};
        ArenaAllocator      a = {}, b = {};
        ASSERT_TRUE(a.InitializeGrowingBorrowed(initial_a, sizeof(initial_a), 1024, route_a));
        ASSERT_TRUE(b.InitializeGrowingBorrowed(initial_b, sizeof(initial_b), 1024, route_b));
        auto* retained = static_cast<uint8_t*>(a.Allocate(256));
        ASSERT_NE(retained, nullptr);
        retained[0] = 37;
        ASSERT_NE(b.Allocate(256), nullptr);
        b = std::move(a);
        EXPECT_EQ(route_b.LiveBytes, 0u);
        EXPECT_EQ(a.m_memory, nullptr);
        EXPECT_EQ(retained[0], 37);
        ArenaAllocator moved(std::move(b));
        EXPECT_EQ(b.m_memory, nullptr);
        moved.Shutdown();
        moved.Shutdown();
        EXPECT_EQ(route_a.LiveBytes, 0u);
        EXPECT_EQ(route_a.Frees, 1u);
        EXPECT_EQ(route_b.Frees, 1u);
    }
} // namespace ZEngine::Core::Memory::Tests

TEST(MemoryBudgetConfigTest, BuiltinProfilesFitRootReservation)
{
    const MemoryBudgetConfig default_budget = MemoryBudgetConfig::Default();
    const MemoryBudgetConfig budget         = MemoryBudgetConfig::Editor();
    const MemoryBudgetConfig server_budget  = MemoryBudgetConfig::Server();

    EXPECT_EQ(budget.AssetManager.SizeBytes, ZMega(1280));
    EXPECT_EQ(budget.EditorContext.SizeBytes, ZMega(320));
    EXPECT_EQ(budget.EditorSceneLoadA.SizeBytes, ZMega(200));
    EXPECT_EQ(budget.EditorSceneLoadB.SizeBytes, ZMega(200));
    EXPECT_EQ(budget.AnimationManager.SizeBytes, 0);
    EXPECT_EQ(budget.Swapchain.SizeBytes, 0);
    EXPECT_EQ(budget.ShaderCache.SizeBytes, 0);
    EXPECT_EQ(budget.Serializer.SizeBytes, 0);
    EXPECT_EQ(default_budget.Bootstrap.SizeBytes, ZMega(32));
    EXPECT_EQ(budget.ImportPipeline.SizeBytes, ZGiga(4));
    EXPECT_EQ(default_budget.TotalCapacity(), ZMega(7604));
    EXPECT_EQ(budget.TotalCapacity(), ZMega(7868));
    EXPECT_EQ(server_budget.TotalCapacity(), ZMega(6324));
    EXPECT_TRUE(default_budget.Validate(ZGiga(8)));
    EXPECT_TRUE(budget.Validate(ZGiga(8)));
    EXPECT_TRUE(server_budget.Validate(ZGiga(8)));
}

TEST(MemoryBudgetConfigTest, CapacityOverrunIdentifiesEveryConfiguredOwner)
{
    MemoryBudgetConfig config{};
    config.Bootstrap    = {"BootstrapOwner", ZMega(2)};
    config.AssetManager = {"AssetsOwner", ZMega(1)};

    EXPECT_DEATH_IF_SUPPORTED((void) config.Validate(ZMega(1)), "Bootstrap.*BootstrapOwner.*AssetsOwner");
}

TEST(MemoryManagerTest, MaterializesBootstrapOwner)
{
    MemoryBudgetConfig config{};
    config.Bootstrap = {"Bootstrap", ZKilo(64)};

    MemoryManager manager{};
    manager.Initialize(ZKilo(128), config);

    EXPECT_NE(manager.BootstrapArena.m_memory, nullptr);
    EXPECT_FALSE(manager.BootstrapArena.m_is_sub_arena);
    EXPECT_EQ(manager.BootstrapArena.m_total_size, ZKilo(64));
    EXPECT_STREQ(manager.BootstrapArena.m_owner_name, "Bootstrap");
    EXPECT_EQ(manager.MainArena.m_memory, nullptr);

    manager.Shutdown();
}

TEST(MemoryManagerTest, ConfiguredOwnersAreIndependentlyReserved)
{
    MemoryBudgetConfig config{};
    config.Bootstrap    = {"Bootstrap", ZKilo(64)};
    config.AssetManager = {"AssetManager", ZKilo(64)};

    MemoryManager manager{};
    manager.Initialize(ZMega(1), config);

    ArenaAllocator asset_arena{};
    manager.CreateBudgetedArena(config.AssetManager, &asset_arena);

    EXPECT_EQ(manager.MainArena.m_memory, nullptr);
    EXPECT_FALSE(asset_arena.m_is_sub_arena);
    EXPECT_NE(asset_arena.m_memory, nullptr);
    EXPECT_NE(manager.BootstrapArena.m_memory, asset_arena.m_memory);
    EXPECT_NE(asset_arena.Allocate(1), nullptr);

    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocate)
{
    MemoryManager manager{};
    manager.Initialize(200, {});
    auto& arena = manager.MainArena;

    for (int i = 0; i < 10; ++i)
    {
        int*   x;
        float* f;
        char*  str;

        arena.Clear();

        x   = reinterpret_cast<int*>(arena.Allocate(sizeof(int)));
        f   = reinterpret_cast<float*>(arena.Allocate(sizeof(float)));
        str = reinterpret_cast<char*>(arena.Allocate(10));

        *x  = 123;
        *f  = 987.0f;
        Helpers::secure_memmove(str, 10, "hellope", 7);

        EXPECT_EQ(*x, 123);
        EXPECT_EQ(*f, 987.0f);
        EXPECT_STREQ(str, "hellope");

        str = reinterpret_cast<char*>(arena.Resize(str, 10, 16));
        Helpers::secure_memmove(str + 7, 7, " world!", 7);

        EXPECT_STREQ(str, "hellope world!");
        EXPECT_EQ(*x, 123);
        EXPECT_EQ(*f, 987.0f);
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaStruct)
{
    struct Foo
    {
        int   x = 0;
        float y = 0.f;
        void  Func() {}
    };

    MemoryManager manager{};
    manager.Initialize(200, {});
    auto arena  = &(manager.MainArena);

    auto fooPtr = (Foo*) arena->Allocate(sizeof(Foo));
    fooPtr->x   = 23;
    fooPtr->y   = 100.f;

    manager.Shutdown();
    fooPtr = nullptr;
}

TEST(AllocatorTest, ArenaMemoryManager)
{
    MemoryManager manager{};

    manager.Initialize(ZKilo(8), {});

    struct Foo
    {
        int   x = 0;
        float y = 0.f;
        void  Func() {}
    };

    int* intPtr    = ZPushArray<int>(&(manager.MainArena), 1);
    auto structPtr = ZPushStruct<Foo>(&(manager.MainArena));

    *intPtr        = 12;
    structPtr->x   = 12;
    structPtr->y   = 798.0f;

    char* str      = ZPushString(&(manager.MainArena), 12);
    Helpers::secure_memmove(str, 12, "hello", 5);

    EXPECT_EQ(*intPtr, 12);
    EXPECT_EQ(structPtr->x, 12);
    EXPECT_EQ(structPtr->y, 798.0f);
    EXPECT_STREQ(str, "hello");

    manager.Shutdown();
}

struct Foo
{
    int   x = 0;
    float y = 0.f;
    char* name;
    void  Func() {}
};
void ComPareFoo(ArenaAllocator* arena, const Foo& f)
{
    auto  scratch       = ZGetScratch(arena);
    char* internal_name = ZPushString(arena, 12);
    Helpers::secure_memmove(internal_name, 12, "Foo::Name", 10);

    auto cmp = strncmp(internal_name, f.name, 10);
    ZReleaseScratch(scratch);
}

TEST(AllocatorTest, ArenaMemoryTemp)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(10), {});
    auto arena = &(manager.MainArena);
    {
        auto fooPtr  = ZPushStruct<Foo>(arena);
        fooPtr->x    = 10;
        fooPtr->y    = 789.f;
        fooPtr->name = ZPushString(arena, 23);
        Helpers::secure_strcpy(fooPtr->name, 23, "Foo::Name");

        ComPareFoo(arena, *fooPtr);

        EXPECT_EQ(fooPtr->x, 10);
        EXPECT_EQ(fooPtr->y, 789.f);
        EXPECT_STREQ(fooPtr->name, "Foo::Name");
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaMemoryPool)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(10), {});
    auto arena = &(manager.MainArena);
    {
        PoolAllocator pool;
        pool.Initialize(arena, sizeof(Foo) * 100, sizeof(Foo));

        auto fooPtr  = ZPushDynamicArray<Foo>(&pool);
        auto fooPtr1 = ZPushDynamicArray<Foo>(&pool);
        auto fooPtr2 = ZPushDynamicArray<Foo>(&pool);
        fooPtr->name = ZPushString(arena, 5);
        Helpers::secure_strcpy(fooPtr->name, 5, "helo");

        EXPECT_STREQ(fooPtr->name, "helo");

        pool.Free(fooPtr);
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateAlignment)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto arena = &(manager.MainArena);

    for (size_t alignment : {1u, 8u, 16u, 64u})
    {
        arena->Clear();
        void* ptr = arena->Allocate(1, alignment);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignment, 0u) << "misaligned for alignment=" << alignment;
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateZeroesMemory)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto             arena = &(manager.MainArena);

    constexpr size_t sz    = 64;
    auto*            ptr   = reinterpret_cast<uint8_t*>(arena->Allocate(sz));
    ASSERT_NE(ptr, nullptr);
    for (size_t i = 0; i < sz; ++i)
    {
        EXPECT_EQ(ptr[i], 0u) << "non-zero byte at index " << i;
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateOOM)
{
    MemoryManager manager{};
    manager.Initialize(128, {});
    auto  arena = &(manager.MainArena);

    void* ptr   = arena->Allocate(256);
    EXPECT_EQ(ptr, nullptr);
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaCapacityFailureIdentifiesOwnerAndRequest)
{
    ArenaAllocator arena{};
    arena.Initialize(64, 4096, "TinyArena");

    ASSERT_NE(arena.Allocate(64), nullptr);
    EXPECT_EQ(arena.Allocate(1), nullptr);

    const ArenaAllocationFailure& failure = arena.LastFailure();
    EXPECT_EQ(failure.Kind, ArenaAllocationFailureKind::CapacityExceeded);
    EXPECT_STREQ(failure.OwnerName, "TinyArena");
    EXPECT_EQ(failure.RequestedSize, 1u);
    EXPECT_EQ(failure.CurrentUsage, 64u);
    EXPECT_EQ(failure.Capacity, 64u);

    arena.Shutdown();
}

TEST(AllocatorTest, ArenaResizeSlowPath)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto arena = &(manager.MainArena);

    int* a     = reinterpret_cast<int*>(arena->Allocate(sizeof(int)));
    int* b     = reinterpret_cast<int*>(arena->Allocate(sizeof(int)));
    *a         = 42;
    *b         = 99;

    int* a2    = reinterpret_cast<int*>(arena->Resize(a, sizeof(int), sizeof(int) * 4));
    ASSERT_NE(a2, nullptr);
    EXPECT_NE(a2, a);
    EXPECT_EQ(*a2, 42);
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaResizeInPlaceShrink)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto             arena  = &(manager.MainArena);

    constexpr size_t old_sz = 32;
    constexpr size_t new_sz = 16;
    auto*            ptr    = reinterpret_cast<uint8_t*>(arena->Allocate(old_sz));
    ASSERT_NE(ptr, nullptr);

    auto* ptr2 = reinterpret_cast<uint8_t*>(arena->Resize(ptr, old_sz, new_sz));
    EXPECT_EQ(ptr2, ptr);
    for (size_t i = new_sz; i < old_sz; ++i)
    {
        EXPECT_EQ(ptr[i], 0u) << "tail byte " << i << " not zeroed after shrink";
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaResizeNullDelegatesToAllocate)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto  arena = &(manager.MainArena);

    void* ptr   = arena->Resize(nullptr, 0, sizeof(int));
    EXPECT_NE(ptr, nullptr);
    manager.Shutdown();
}

TEST(AllocatorTest, CheckedResizeFailurePreservesAllocationAndCursors)
{
    ArenaAllocator arena = {};
    arena.Initialize(128, 4096);
    auto* memory = static_cast<uint8_t*>(arena.Allocate(32));
    ASSERT_NE(memory, nullptr);
    memory[0]           = 71;
    const auto current  = arena.m_current_offset;
    const auto previous = arena.m_previous_offset;
    EXPECT_EQ(arena.TryResize(memory, 32, SIZE_MAX), nullptr);
    EXPECT_EQ(arena.m_current_offset, current);
    EXPECT_EQ(arena.m_previous_offset, previous);
    EXPECT_EQ(memory[0], 71);
    EXPECT_EQ(arena.LastFailure().Kind, ArenaAllocationFailureKind::CapacityExceeded);
    EXPECT_EQ(arena.TryResize(memory, 32, 64), memory);
    EXPECT_EQ(memory[0], 71);
    for (size_t i = 32; i < 64; ++i)
    {
        EXPECT_EQ(memory[i], 0);
    }
}

TEST(AllocatorTest, CheckedResizeRejectsInvalidRangesAndAlignments)
{
    ArenaAllocator arena = {};
    arena.Initialize(128, 4096);
    auto* memory = static_cast<uint8_t*>(arena.Allocate(32));
    ASSERT_NE(memory, nullptr);
    uint8_t foreign = 0;
    EXPECT_EQ(arena.TryResize(&foreign, 1, 32), nullptr);
    EXPECT_EQ(arena.TryResize(memory, 33, 64), nullptr);
    EXPECT_EQ(arena.TryResize(memory, 32, 64, 3), nullptr);
    EXPECT_EQ(arena.TryResize(memory, 32, 0), nullptr);
    EXPECT_EQ(arena.m_current_offset, 32u);
    EXPECT_EQ(arena.TryResize(memory, 32, 64), memory);
}

TEST(AllocatorTest, CheckedResizeSlowPathPreservesBothExistingAllocations)
{
    ArenaAllocator arena = {};
    arena.Initialize(128, 4096);
    auto* first  = static_cast<uint8_t*>(arena.Allocate(16));
    auto* second = static_cast<uint8_t*>(arena.Allocate(16));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first[0]           = 41;
    second[0]          = 53;
    const auto current = arena.m_current_offset;
    EXPECT_EQ(arena.TryResize(first, 16, 128), nullptr);
    EXPECT_EQ(arena.m_current_offset, current);
    auto* resized = static_cast<uint8_t*>(arena.TryResize(first, 16, 32));
    ASSERT_NE(resized, nullptr);
    EXPECT_NE(resized, first);
    EXPECT_EQ(resized[0], 41);
    EXPECT_EQ(first[0], 41);
    EXPECT_EQ(second[0], 53);
}

TEST(AllocatorTest, ArenaShutdownIdempotent)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    manager.Shutdown();
    EXPECT_NO_FATAL_FAILURE(manager.Shutdown());
}

TEST(AllocatorTest, ArenaSubArenaLifecycle)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(64), {});
    auto           parent = &(manager.MainArena);

    ArenaAllocator sub{};
    parent->CreateSubArena(ZKilo(4), &sub);

    ASSERT_NE(sub.m_memory, nullptr);
    EXPECT_TRUE(sub.m_is_sub_arena);
    EXPECT_EQ(sub.m_total_size, ZKilo(4));

    // All platforms reserve child address space without making it writable. The first
    // allocation promotes only its first page.
    EXPECT_EQ(sub.m_committed_size, 0u);

    int* val = reinterpret_cast<int*>(sub.Allocate(sizeof(int)));
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(sub.m_committed_size, sub.m_mem_page_size);
    *val = 77;
    EXPECT_EQ(*val, 77);

    sub.Shutdown();
    EXPECT_EQ(sub.m_memory, nullptr);

    void* after = parent->Allocate(sizeof(int));
    EXPECT_NE(after, nullptr);

    manager.Shutdown();
}

TEST(AllocatorTest, ParentAndChildCommitOnlyOwnedPages)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(4), {});
    auto* parent = &manager.MainArena;

    ASSERT_NE(parent->Allocate(1), nullptr);
    EXPECT_EQ(parent->m_committed_size, parent->m_mem_page_size);

    ArenaAllocator child{};
    parent->CreateSubArena(ZMega(1), &child, "ChildArena");
    EXPECT_EQ(child.m_committed_size, 0u);

    ASSERT_NE(child.Allocate(1), nullptr);
    EXPECT_EQ(child.m_committed_size, child.m_mem_page_size);
    EXPECT_EQ(parent->m_committed_size, parent->m_mem_page_size);

    // This allocation follows the child arena in the root range. It must promote a
    // single parent page, not the child's entire unused reservation.
    ASSERT_NE(parent->Allocate(1), nullptr);
    EXPECT_EQ(parent->m_committed_size, 2 * parent->m_mem_page_size);
    EXPECT_EQ(child.m_committed_size, child.m_mem_page_size);

    child.Shutdown();
    manager.Shutdown();
}

#if defined(__APPLE__) || defined(__linux__)
TEST(AllocatorTest, PosixReservationLeavesUnallocatedPagesProtected)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(1), {});
    ArenaAllocator& arena = manager.MainArena;

    ASSERT_NE(arena.Allocate(1), nullptr);
    ASSERT_EQ(arena.m_committed_size, arena.m_mem_page_size);

    // The second page remains PROT_NONE until an allocation reaches it. This guards
    // against regressing to a full read/write mmap on macOS or Linux.
    EXPECT_DEATH(
        {
            volatile uint8_t* uncommitted_page = arena.m_memory + arena.m_mem_page_size;
            *uncommitted_page                  = 1;
        },
        "");

    manager.Shutdown();
}
#endif

#if defined(__linux__)
namespace
{
    constexpr const char* LinuxAddressLimitChildEnvironment = "ZENGINE_TEST_ADDRESS_LIMIT_CHILD";

    uint64_t              CurrentLinuxAddressSpaceBytes()
    {
        FILE* file = fopen("/proc/self/statm", "r");
        if (!file)
        {
            return 0;
        }

        unsigned long pages = 0;
        const int     read  = fscanf(file, "%lu", &pages);
        fclose(file);
        if (read != 1)
        {
            return 0;
        }

        const long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0 || pages > std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(page_size))
        {
            return 0;
        }
        return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
    }
} // namespace

TEST(MemoryManagerTest, LinuxConfiguredOwnersStartBelowRootAddressLimit)
{
    if (std::getenv(LinuxAddressLimitChildEnvironment))
    {
        const uint64_t current_address_space = CurrentLinuxAddressSpaceBytes();
        if (current_address_space == 0)
        {
            GTEST_SKIP() << "unable to read /proc/self/statm";
        }

        struct rlimit previous_limit = {};
        if (getrlimit(RLIMIT_AS, &previous_limit) != 0)
        {
            GTEST_SKIP() << "RLIMIT_AS is unavailable";
        }

        constexpr rlim_t headroom = static_cast<rlim_t>(ZMega(128));
        if (current_address_space > std::numeric_limits<rlim_t>::max() - headroom)
        {
            GTEST_SKIP() << "current address space is too large for an RLIMIT_AS test";
        }

        const rlim_t constrained_limit = static_cast<rlim_t>(current_address_space) + headroom;
        if (previous_limit.rlim_cur != RLIM_INFINITY && previous_limit.rlim_cur < constrained_limit)
        {
            GTEST_SKIP() << "inherited RLIMIT_AS is already more restrictive";
        }

        struct rlimit limit = previous_limit;
        limit.rlim_cur      = constrained_limit;
        if (setrlimit(RLIMIT_AS, &limit) != 0)
        {
            GTEST_SKIP() << "unable to set constrained RLIMIT_AS";
        }

        MemoryBudgetConfig config{};
        config.Bootstrap    = {"Bootstrap", ZMega(4)};
        config.AssetManager = {"ConstrainedAsset", ZMega(32)};

        MemoryManager manager{};
        manager.Initialize(ZGiga(8), config);

        ArenaAllocator asset_arena{};
        manager.CreateBudgetedArena(config.AssetManager, &asset_arena);

        ASSERT_EQ(manager.MainArena.m_memory, nullptr);
        ASSERT_NE(manager.BootstrapArena.m_memory, nullptr);
        ASSERT_NE(asset_arena.m_memory, nullptr);
        ASSERT_NE(asset_arena.Allocate(1), nullptr);
        EXPECT_EQ(asset_arena.m_committed_size, asset_arena.m_mem_page_size);

        manager.Shutdown();
        return;
    }

    std::vector<char*> child_environment;
    for (char** entry = environ; *entry; ++entry)
    {
        child_environment.push_back(*entry);
    }

    char child_marker[] = "ZENGINE_TEST_ADDRESS_LIMIT_CHILD=1";
    child_environment.push_back(child_marker);
    child_environment.push_back(nullptr);

    char  child_path[]   = "/proc/self/exe";
    char  child_filter[] = "--gtest_filter=MemoryManagerTest.LinuxConfiguredOwnersStartBelowRootAddressLimit";
    char  child_brief[]  = "--gtest_brief=1";
    char* child_argv[]   = {child_path, child_filter, child_brief, nullptr};

    pid_t child_pid      = 0;
    ASSERT_EQ(posix_spawn(&child_pid, child_path, nullptr, nullptr, child_argv, child_environment.data()), 0);

    int status = 0;
    ASSERT_EQ(waitpid(child_pid, &status, 0), child_pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
#endif

// Regression: multiple large sub-arenas carved from one parent must all succeed and be
// independently usable. This reproduces the Windows UIContext failure where eager commit
// of earlier sub-arenas (ImportPipeline 3.5 GB, etc.) exhausted pagefile quota so that
// the UIContext VirtualAlloc(MEM_COMMIT) returned NULL.
TEST(AllocatorTest, ArenaSubArenaMultipleLargeSubArenas)
{
    // Use 4 MB total; carve three 1 MB sub-arenas (mirrors FrameArena + PersistentArena
    // + PayloadArena pattern in AppRenderPipeline at a smaller scale).
    MemoryManager manager{};
    manager.Initialize(ZMega(4), {});
    auto*          parent = &(manager.MainArena);

    ArenaAllocator a{}, b{}, c{};
    parent->CreateSubArena(ZMega(1), &a);
    parent->CreateSubArena(ZMega(1), &b);
    parent->CreateSubArena(ZMega(1), &c);

    ASSERT_NE(a.m_memory, nullptr);
    ASSERT_NE(b.m_memory, nullptr);
    ASSERT_NE(c.m_memory, nullptr);

    // Each sub-arena must allocate independently.
    int* pa = reinterpret_cast<int*>(a.Allocate(sizeof(int)));
    int* pb = reinterpret_cast<int*>(b.Allocate(sizeof(int)));
    int* pc = reinterpret_cast<int*>(c.Allocate(sizeof(int)));
    ASSERT_NE(pa, nullptr);
    *pa = 1;
    ASSERT_NE(pb, nullptr);
    *pb = 2;
    ASSERT_NE(pc, nullptr);
    *pc = 3;
    EXPECT_EQ(*pa, 1);
    EXPECT_EQ(*pb, 2);
    EXPECT_EQ(*pc, 3);

    // Sub-arena address ranges must not overlap.
    EXPECT_GE((uintptr_t) b.m_memory, (uintptr_t) a.m_memory + ZMega(1));
    EXPECT_GE((uintptr_t) c.m_memory, (uintptr_t) b.m_memory + ZMega(1));

    c.Shutdown();
    b.Shutdown();
    a.Shutdown();
    manager.Shutdown();
}

// Sub-arena m_memory must be page-aligned on every platform — on Windows this keeps
// VirtualAlloc(MEM_COMMIT) boundaries clean; elsewhere it's just a uniform guarantee.
TEST(AllocatorTest, ArenaSubArenaPageAligned)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(4), {});
    auto* parent = &(manager.MainArena);

    // Allocate one byte first to make the next sub-arena start non-trivially aligned.
    (void) parent->Allocate(1);

    ArenaAllocator sub{};
    parent->CreateSubArena(ZMega(1), &sub);

    ASSERT_NE(sub.m_memory, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(sub.m_memory) % parent->m_mem_page_size, 0u);

    sub.Shutdown();
    manager.Shutdown();
}

TEST(AllocatorTest, TempArenaRestoresOffsets)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto arena = &(manager.MainArena);

    arena->Allocate(32);
    size_t saved_current  = arena->m_current_offset;
    size_t saved_previous = arena->m_previous_offset;

    {
        auto scratch = ZGetScratch(arena);
        arena->Allocate(64);
        arena->Allocate(128);
        ZReleaseScratch(scratch);
    }

    EXPECT_EQ(arena->m_current_offset, saved_current);
    EXPECT_EQ(arena->m_previous_offset, saved_previous);
    manager.Shutdown();
}

TEST(AllocatorTest, PoolExhaustion)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto             arena       = &(manager.MainArena);

    constexpr size_t chunk_count = 4;
    PoolAllocator    pool;
    pool.Initialize(arena, sizeof(Foo) * chunk_count, sizeof(Foo));

    for (size_t i = 0; i < chunk_count; ++i)
    {
        EXPECT_NE(pool.Allocate(), nullptr) << "expected valid slot at i=" << i;
    }

    EXPECT_DEATH(pool.Allocate(), "");
    manager.Shutdown();
}

TEST(AllocatorTest, PoolFreeRestoresSlot)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto          arena = &(manager.MainArena);

    PoolAllocator pool;
    pool.Initialize(arena, sizeof(Foo) * 2, sizeof(Foo));

    auto* slot = reinterpret_cast<Foo*>(pool.Allocate());
    ASSERT_NE(slot, nullptr);
    slot->x = 99;
    pool.Free(slot);

    auto* reused = reinterpret_cast<Foo*>(pool.Allocate());
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(reused->x, 0);
    manager.Shutdown();
}

TEST(AllocatorTest, PoolClearResetsAllSlots)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto             arena       = &(manager.MainArena);

    constexpr size_t chunk_count = 8;
    PoolAllocator    pool;
    pool.Initialize(arena, sizeof(Foo) * chunk_count, sizeof(Foo));

    for (size_t i = 0; i < chunk_count; ++i)
    {
        ASSERT_NE(pool.Allocate(), nullptr);
    }

    pool.Clear();

    for (size_t i = 0; i < chunk_count; ++i)
    {
        EXPECT_NE(pool.Allocate(), nullptr) << "slot " << i << " unavailable after Clear";
    }
    manager.Shutdown();
}

TEST(AllocatorTest, PoolChunkSizeAlignedUp)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto             arena     = &(manager.MainArena);

    constexpr size_t raw_chunk = 3;
    constexpr size_t alignment = 8;
    PoolAllocator    pool;
    pool.Initialize(arena, alignment * 16, raw_chunk, alignment);

    EXPECT_EQ(pool.chunk_size, alignment);
    manager.Shutdown();
}

// AllocateNoZero — skip zeroing for large decode buffers
TEST(AllocatorTest, ArenaAllocateNoZeroDoesNotZero)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto*            arena = &(manager.MainArena);

    constexpr size_t sz    = 64;
    auto*            p1    = reinterpret_cast<uint8_t*>(arena->Allocate(sz));
    for (size_t i = 0; i < sz; ++i)
    {
        p1[i] = 0xAB;
    }

    arena->Clear();

    auto* p2 = reinterpret_cast<uint8_t*>(arena->AllocateNoZero(sz));
    ASSERT_NE(p2, nullptr);
    ASSERT_EQ(p1, p2);

    bool any_nonzero = false;
    for (size_t i = 0; i < sz; ++i)
    {
        if (p2[i] != 0)
        {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero) << "AllocateNoZero must not zero memory";

    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateNoZeroAlignment)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto* arena = &(manager.MainArena);

    for (size_t align : {1u, 8u, 16u, 64u})
    {
        arena->Clear();
        void* p = arena->AllocateNoZero(32, align);
        ASSERT_NE(p, nullptr) << "alignment=" << align;
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % align, 0u) << "misaligned for alignment=" << align;
    }
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateNoZeroOOM)
{
    MemoryManager manager{};
    manager.Initialize(128, {});
    auto* arena = &(manager.MainArena);

    EXPECT_EQ(arena->AllocateNoZero(256), nullptr);
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateNoZeroAndAllocateSameCursor)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto*            arena = &(manager.MainArena);

    constexpr size_t sz    = 32;
    void*            a     = arena->AllocateNoZero(sz);
    void*            b     = arena->Allocate(sz);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(reinterpret_cast<uint8_t*>(a) + sz, reinterpret_cast<uint8_t*>(b)) << "AllocateNoZero must advance the cursor identically to Allocate";

    manager.Shutdown();
}

// Alignment precondition — non-power-of-two must assert
TEST(AllocatorTest, ArenaAllocateNonPowerOfTwoDeath)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto* arena = &(manager.MainArena);

    EXPECT_DEATH(arena->Allocate(4, 3), "");
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateNoZeroNonPowerOfTwoDeath)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto* arena = &(manager.MainArena);

    EXPECT_DEATH(arena->AllocateNoZero(4, 3), "");
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocateZeroSizeDeath)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto* arena = &(manager.MainArena);

    EXPECT_DEATH(arena->Allocate(0), "");
    manager.Shutdown();
}

// PoolAllocator — freed chunk must be re-allocated zeroed (placement new regression)
TEST(AllocatorTest, PoolFreedChunkReturnedZeroed)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto*         arena = &(manager.MainArena);

    PoolAllocator pool;
    pool.Initialize(arena, sizeof(Foo) * 1, sizeof(Foo));

    auto* slot = reinterpret_cast<Foo*>(pool.Allocate());
    ASSERT_NE(slot, nullptr);
    slot->x = 0xDEAD;
    slot->y = 3.14f;
    pool.Free(slot);

    auto* reused = reinterpret_cast<uint8_t*>(pool.Allocate());
    ASSERT_NE(reused, nullptr);
    for (size_t i = 0; i < sizeof(Foo); ++i)
    {
        EXPECT_EQ(reused[i], 0u) << "byte " << i << " not zeroed after re-alloc";
    }

    manager.Shutdown();
}

TEST(AllocatorTest, PoolFreeListIntegrityAfterMultipleFreeCycles)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto* arena = &(manager.MainArena);

    // Use a chunk large enough to hold PoolFreeNode (pointer = 8 bytes on 64-bit).
    struct Slot
    {
        uint64_t a = 0;
        uint64_t b = 0;
    }; // 16 bytes — well above minimum
    constexpr int N = 16;
    PoolAllocator pool;
    pool.Initialize(arena, sizeof(Slot) * N, sizeof(Slot));

    Slot* ptrs[N];
    for (int i = 0; i < N; ++i)
    {
        ptrs[i] = reinterpret_cast<Slot*>(pool.Allocate());
        ASSERT_NE(ptrs[i], nullptr);
        ptrs[i]->a = static_cast<uint64_t>(i + 1);
    }
    for (int i = N - 1; i >= 0; --i)
    {
        pool.Free(ptrs[i]);
    }

    for (int i = 0; i < N; ++i)
    {
        auto* p = reinterpret_cast<Slot*>(pool.Allocate());
        EXPECT_NE(p, nullptr) << "slot " << i << " unavailable after free cycle";
        if (p)
        {
            EXPECT_EQ(p->a, 0u) << "slot " << i << " not zeroed on re-alloc";
            EXPECT_EQ(p->b, 0u) << "slot " << i << " not zeroed on re-alloc";
        }
    }
    manager.Shutdown();
}

TEST(AllocatorTest, PoolAllocateFreeAllocateManyTimes)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto*         arena = &(manager.MainArena);

    PoolAllocator pool;
    pool.Initialize(arena, sizeof(int) * 4, sizeof(int));

    for (int round = 0; round < 1000; ++round)
    {
        auto* p = reinterpret_cast<int*>(pool.Allocate());
        ASSERT_NE(p, nullptr) << "round=" << round;
        EXPECT_EQ(*p, 0) << "not zeroed at round=" << round;
        *p = round;
        pool.Free(p);
    }
    manager.Shutdown();
}

TEST(AllocatorTest, StandaloneArenaDetectsNativePageSizeWhenRequested)
{
    ArenaAllocator arena;
    arena.Initialize(ZKilo(64), 0, "AllocatorTest/NativePageSize");
    ASSERT_NE(arena.m_memory, nullptr);
    EXPECT_GT(arena.m_mem_page_size, 0u);
    EXPECT_EQ(arena.m_mem_page_size & (arena.m_mem_page_size - 1), 0u);
    EXPECT_EQ(arena.m_reserved_size % arena.m_mem_page_size, 0u);
    auto* first = static_cast<uint8_t*>(arena.TryResize(nullptr, 0, 1, 1));
    ASSERT_NE(first, nullptr);
    *first       = 17;
    auto* second = static_cast<uint8_t*>(arena.TryResize(nullptr, 0, arena.m_mem_page_size, 1));
    ASSERT_NE(second, nullptr);
    second[arena.m_mem_page_size - 1] = 29;
    EXPECT_EQ(*first, 17);
    EXPECT_EQ(second[arena.m_mem_page_size - 1], 29);
}

TEST(AllocatorTest, CheckedPoolRejectsInvalidLayoutWithoutChangingArenaOrPool)
{
    ArenaAllocator arena = {};
    arena.Initialize(ZKilo(4), 0, "CheckedPoolTest");
    PoolAllocator pool = {};
    ASSERT_TRUE(pool.TryInitialize(&arena, 128, 16));
    auto* const memory = pool.memory;
    auto* const head   = pool.head;
    const auto  offset = arena.m_current_offset;
    EXPECT_FALSE(pool.TryInitialize(nullptr, 128, 16));
    EXPECT_FALSE(pool.TryInitialize(&arena, 128, 16, 0));
    EXPECT_FALSE(pool.TryInitialize(&arena, 128, 16, 3));
    EXPECT_FALSE(pool.TryInitialize(&arena, 128, 0));
    EXPECT_FALSE(pool.TryInitialize(&arena, 8, 16));
    EXPECT_FALSE(pool.TryInitialize(&arena, 128, SIZE_MAX));
    EXPECT_FALSE(pool.TryInitialize(&arena, ZMega(1), 16));
    EXPECT_EQ(arena.m_current_offset, offset);
    EXPECT_EQ(pool.memory, memory);
    EXPECT_EQ(pool.head, head);
    EXPECT_EQ(pool.chunk_size, 16u);
}

TEST(AllocatorTest, CheckedPoolPromotesByteAlignmentForFreeListPointers)
{
    ArenaAllocator arena = {};
    arena.Initialize(ZKilo(4), 0, "CheckedPoolAlignmentTest");
    ASSERT_NE(arena.Allocate(1, 1), nullptr);
    PoolAllocator pool = {};
    ASSERT_TRUE(pool.TryInitialize(&arena, 64, 9, 1));
    EXPECT_EQ(pool.chunk_size % alignof(PoolFreeNode), 0u);
    while (pool.head)
    {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(pool.Allocate()) % alignof(PoolFreeNode), 0u);
    }
    pool.Clear();
    EXPECT_NE(pool.head, nullptr);
}

// Double-free detection — debug builds only
#ifndef NDEBUG
TEST(AllocatorTest, PoolDoubleFreeDeath)
{
    MemoryManager manager{};
    manager.Initialize(ZKilo(4), {});
    auto*         arena = &(manager.MainArena);

    PoolAllocator pool;
    pool.Initialize(arena, sizeof(Foo) * 4, sizeof(Foo));

    auto* slot = reinterpret_cast<Foo*>(pool.Allocate());
    ASSERT_NE(slot, nullptr);
    pool.Free(slot);

    EXPECT_DEATH(pool.Free(slot), "");
    manager.Shutdown();
}
#endif
