#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/GeometryPool.h>
#include <ZEngine/Rendering/GeometryStreamingManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Rendering;

// Test-only helper — friend-declared in RenderResourceManager to allow direct
// slot state manipulation without requiring a live Vulkan device.
struct RRMTestHelper
{
    static void SetSlot(RenderResourceManager& rrm, uint32_t idx, uint32_t gen, RenderResourceManager::StreamingState state, bool referenced, bool pinned, const GeometryRegion& region = {})
    {
        if (idx >= RenderResourceManager::MAX_BUFFERS)
            return;
        rrm.m_mesh_slots[idx].Generation      = gen;
        rrm.m_mesh_slots[idx].Data.State      = state;
        rrm.m_mesh_slots[idx].Data.Referenced = referenced;
        rrm.m_mesh_slots[idx].Data.Pinned     = pinned;
        rrm.m_mesh_slots[idx].Data.Region     = region;
        if (idx >= rrm.m_mesh_slot_count)
            rrm.m_mesh_slot_count = idx + 1;
    }

    static RenderResourceManager::StreamingState GetState(const RenderResourceManager& rrm, uint32_t idx)
    {
        return rrm.m_mesh_slots[idx].Data.State;
    }

    static bool GetReferenced(const RenderResourceManager& rrm, uint32_t idx)
    {
        return rrm.m_mesh_slots[idx].Data.Referenced;
    }

    static const GeometryRegion& GetRegion(const RenderResourceManager& rrm, uint32_t idx)
    {
        return rrm.m_mesh_slots[idx].Data.Region;
    }

    static void SetupPool(RenderResourceManager& rrm, ArenaAllocator* arena, VkDeviceSize vtx_cap, VkDeviceSize idx_cap, uint32_t max_free_nodes)
    {
        rrm.m_pool.Initialize(arena, vtx_cap, idx_cap, max_free_nodes);
    }

    static GeometryPool& GetPool(RenderResourceManager& rrm)
    {
        return rrm.m_pool;
    }
};

namespace
{
    constexpr uint64_t ARENA_SIZE = 512 * 1024;

    struct StreamingFixture
    {
        ArenaAllocator           arena{};
        RenderResourceManager    rrm{};
        GeometryStreamingManager mgr{};

        StreamingFixture()
        {
            arena.Initialize(ARENA_SIZE, {});
            RRMTestHelper::SetupPool(rrm, &arena, 4096, 4096, 256);
            mgr.Initialize(nullptr, &rrm);
        }

        ~StreamingFixture()
        {
            mgr.Deinitialize();
            arena.Shutdown();
        }
    };
} // namespace

TEST(GeometryStreamingManager, TickClearsReferencedBitsOnResidentSlots)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/true, false);
    RRMTestHelper::SetSlot(f.rrm, 1, 1, RenderResourceManager::StreamingState::Unloaded, /*referenced=*/true, false);
    f.mgr.Tick(0);
    EXPECT_FALSE(RRMTestHelper::GetReferenced(f.rrm, 0)); // Resident — bit cleared
    EXPECT_TRUE(RRMTestHelper::GetReferenced(f.rrm, 1));  // Unloaded — untouched
}

TEST(GeometryStreamingManager, EvictionSweepSkipsReferencedSlot)
{
    StreamingFixture f;
    GeometryRegion   r0, r1;
    RRMTestHelper::GetPool(f.rrm).Allocate(128, 64, r0);
    RRMTestHelper::GetPool(f.rrm).Allocate(128, 64, r1);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/true, false, r0);
    RRMTestHelper::SetSlot(f.rrm, 1, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/false, false, r1);

    // Force eviction pressure by filling pool above 85%.
    GeometryRegion filler;
    RRMTestHelper::GetPool(f.rrm).Allocate(4096 - 256 - 2, 4096 - 128 - 2, filler);

    f.mgr.Tick(0);
    // Slot 0 (Referenced) gets second-chance — stays Resident (bit cleared).
    // Slot 1 (not referenced) is evicted.
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
    EXPECT_FALSE(RRMTestHelper::GetReferenced(f.rrm, 0)); // second-chance cleared the bit
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 1), RenderResourceManager::StreamingState::Unloaded);
}

TEST(GeometryStreamingManager, EvictionSweepFreesPoolRegion)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/false, false, r);

    VkDeviceSize   used_before = RRMTestHelper::GetPool(f.rrm).VtxUsed;

    // Force eviction pressure.
    GeometryRegion filler;
    RRMTestHelper::GetPool(f.rrm).Allocate(4096 - 200 - 2, 4096 - 100 - 2, filler);

    f.mgr.Tick(0);

    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
    EXPECT_EQ(RRMTestHelper::GetRegion(f.rrm, 0).VtxByteSize, 0u);                      // region zeroed
    EXPECT_LT(RRMTestHelper::GetPool(f.rrm).VtxUsed, used_before + filler.VtxByteSize); // bytes returned
}

TEST(GeometryStreamingManager, EvictionSweepSkipsPinnedSlots)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(128, 64, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/false, /*pinned=*/true, r);

    GeometryRegion filler;
    RRMTestHelper::GetPool(f.rrm).Allocate(4096 - 128 - 2, 4096 - 64 - 2, filler);

    f.mgr.Tick(0);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
}

TEST(GeometryStreamingManager, RequestEvictFreesRegionAndSetsUnloaded)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, r);
    BufferHandle h{0, 1};

    VkDeviceSize vtx_used_before = RRMTestHelper::GetPool(f.rrm).VtxUsed;
    f.mgr.RequestEvict(h);

    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
    EXPECT_EQ(RRMTestHelper::GetRegion(f.rrm, 0).VtxByteSize, 0u);
    EXPECT_LT(RRMTestHelper::GetPool(f.rrm).VtxUsed, vtx_used_before);
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnInvalidHandle)
{
    StreamingFixture f;
    BufferHandle     invalid{};
    EXPECT_NO_FATAL_FAILURE(f.mgr.RequestEvict(invalid));
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnPinnedSlot)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(100, 50, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, /*pinned=*/true, r);
    BufferHandle h{0, 1};
    f.mgr.RequestEvict(h);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnUnloadedSlot)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
    BufferHandle h{0, 1};
    f.mgr.RequestEvict(h);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnStaleHandle)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(100, 50, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false, r); // gen=2
    BufferHandle stale{0, 1};                                                                              // gen=1 — stale
    f.mgr.RequestEvict(stale);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
}

TEST(GeometryStreamingManager, FragmentationAboveThresholdSetsCompactionRequest)
{
    StreamingFixture f;
    GeometryRegion   r;
    // Allocate then free enough to push fragmentation above 0.30.
    RRMTestHelper::GetPool(f.rrm).Allocate(2000, 2000, r);
    RRMTestHelper::GetPool(f.rrm).Free(r);

    EXPECT_FALSE(f.mgr.IsCompactionRequested());
    f.mgr.Tick(0);
    EXPECT_TRUE(f.mgr.IsCompactionRequested());
}

TEST(GeometryStreamingManager, ClearCompactionRequestResetsFlag)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(2000, 2000, r);
    RRMTestHelper::GetPool(f.rrm).Free(r);
    f.mgr.Tick(0);
    ASSERT_TRUE(f.mgr.IsCompactionRequested());
    f.mgr.ClearCompactionRequest();
    EXPECT_FALSE(f.mgr.IsCompactionRequested());
}

TEST(GeometryStreamingManager, RequestLoadQueueOverflowReturnsFalse)
{
    // SPSCQueue<T, N> holds at most N-1 items (head==tail = empty sentinel).
    // kLoadQueueCapacity=256 → effective capacity 255.
    StreamingFixture f;
    StreamRequest    req{};
    req.Handle         = BufferHandle{0, 1};
    uint32_t succeeded = 0;
    for (uint32_t i = 0; i < 256; ++i)
    {
        if (f.mgr.RequestLoad(req))
            ++succeeded;
        else
            break;
    }
    EXPECT_EQ(succeeded, 255u);           // N-1 items fit
    EXPECT_FALSE(f.mgr.RequestLoad(req)); // next push must fail
}
