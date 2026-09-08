#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/GeometryPool.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Rendering;

namespace
{
    // Provide enough arena memory for the PoolAllocator's backing slab.
    // 4096 nodes * sizeof(FreeNode) = 4096 * 24 ≈ 96 KB; round up to 512 KB.
    constexpr uint64_t ARENA_SIZE     = 512 * 1024;
    constexpr uint32_t MAX_FREE_NODES = 4096;

    struct PoolFixture
    {
        ArenaAllocator arena{};
        GeometryPool   pool{};

        PoolFixture()
        {
            arena.Initialize(ARENA_SIZE, {});
            pool.Initialize(&arena, /*vtx*/ 1024, /*idx*/ 1024, MAX_FREE_NODES);
        }

        ~PoolFixture()
        {
            arena.Shutdown();
        }
    };
} // namespace

TEST(GeometryPool, FreshPoolIsEmpty)
{
    PoolFixture f;
    EXPECT_EQ(f.pool.VtxUsed, 0u);
    EXPECT_EQ(f.pool.IdxUsed, 0u);
    EXPECT_EQ(f.pool.VtxCursor, 0u);
    EXPECT_EQ(f.pool.IdxCursor, 0u);
    EXPECT_EQ(f.pool.FreeVtxHead, nullptr);
    EXPECT_EQ(f.pool.FreeIdxHead, nullptr);
    EXPECT_FLOAT_EQ(f.pool.FragmentationRatio(), 0.f);
}

TEST(GeometryPool, AllocateAdvancesCursors)
{
    PoolFixture    f;
    GeometryRegion r;
    ASSERT_TRUE(f.pool.Allocate(100, 50, r));
    EXPECT_EQ(r.VtxByteOffset, 0u);
    EXPECT_EQ(r.VtxByteSize, 100u);
    EXPECT_EQ(r.IdxByteOffset, 0u);
    EXPECT_EQ(r.IdxByteSize, 50u);
    EXPECT_EQ(f.pool.VtxCursor, 100u);
    EXPECT_EQ(f.pool.IdxCursor, 50u);
    EXPECT_EQ(f.pool.VtxUsed, 100u);
    EXPECT_EQ(f.pool.IdxUsed, 50u);
}

TEST(GeometryPool, SecondAllocAppendsAfterFirst)
{
    PoolFixture    f;
    GeometryRegion a, b;
    ASSERT_TRUE(f.pool.Allocate(100, 50, a));
    ASSERT_TRUE(f.pool.Allocate(200, 80, b));
    EXPECT_EQ(b.VtxByteOffset, 100u);
    EXPECT_EQ(b.IdxByteOffset, 50u);
    EXPECT_EQ(f.pool.VtxCursor, 300u);
    EXPECT_EQ(f.pool.IdxCursor, 130u);
}

TEST(GeometryPool, AllocateFailsWhenFull)
{
    PoolFixture    f;
    GeometryRegion r;
    ASSERT_TRUE(f.pool.Allocate(1024, 1024, r)); // exactly fills both axes
    GeometryRegion overflow;
    EXPECT_FALSE(f.pool.Allocate(1, 1, overflow));
}

TEST(GeometryPool, AllocateFailsWhenOnlyOneAxisFull)
{
    PoolFixture    f;
    GeometryRegion r;
    ASSERT_TRUE(f.pool.Allocate(1024, 1, r)); // vtx full, idx has space
    GeometryRegion overflow;
    EXPECT_FALSE(f.pool.Allocate(1, 1, overflow)); // vtx cursor exhausted
    // idx cursor must not have advanced (rollback)
    EXPECT_EQ(f.pool.IdxCursor, 1u);
}

TEST(GeometryPool, FreeRecordsHolesAsFragmentation)
{
    PoolFixture    f;
    GeometryRegion r;
    ASSERT_TRUE(f.pool.Allocate(100, 50, r));
    EXPECT_FLOAT_EQ(f.pool.FragmentationRatio(), 0.f);
    f.pool.Free(r);
    // 150 freed bytes / 2048 total capacity
    float expected = 150.f / 2048.f;
    EXPECT_NEAR(f.pool.FragmentationRatio(), expected, 1e-5f);
    EXPECT_EQ(f.pool.VtxUsed, 0u);
    EXPECT_EQ(f.pool.IdxUsed, 0u);
}

TEST(GeometryPool, FreedRegionIsReusedByNextAlloc)
{
    PoolFixture    f;
    GeometryRegion a, b;
    ASSERT_TRUE(f.pool.Allocate(100, 50, a));
    ASSERT_TRUE(f.pool.Allocate(200, 80, b));
    f.pool.Free(a); // frees [0, 100) vtx and [0, 50) idx
    GeometryRegion c;
    ASSERT_TRUE(f.pool.Allocate(100, 50, c));
    // Should reuse the freed region, not the cursor.
    EXPECT_EQ(c.VtxByteOffset, 0u);
    EXPECT_EQ(c.IdxByteOffset, 0u);
    EXPECT_FLOAT_EQ(f.pool.FragmentationRatio(), 0.f);
}

TEST(GeometryPool, PartialReuseLeavesSmallerFreeNode)
{
    PoolFixture    f;
    GeometryRegion a;
    ASSERT_TRUE(f.pool.Allocate(200, 100, a));
    f.pool.Free(a); // [0,200) vtx, [0,100) idx free
    GeometryRegion b;
    ASSERT_TRUE(f.pool.Allocate(50, 40, b)); // fits in free region
    EXPECT_EQ(b.VtxByteOffset, 0u);
    EXPECT_EQ(b.IdxByteOffset, 0u);
    // Remainder [50,200) vtx and [40,100) idx should still be free
    EXPECT_NE(f.pool.FreeVtxHead, nullptr);
    EXPECT_EQ(f.pool.FreeVtxHead->Offset, 50u);
    EXPECT_EQ(f.pool.FreeVtxHead->Size, 150u);
    EXPECT_NE(f.pool.FreeIdxHead, nullptr);
    EXPECT_EQ(f.pool.FreeIdxHead->Offset, 40u);
    EXPECT_EQ(f.pool.FreeIdxHead->Size, 60u);
}

TEST(GeometryPool, AdjacentFreedRegionsMerge)
{
    PoolFixture    f;
    GeometryRegion a, b;
    ASSERT_TRUE(f.pool.Allocate(100, 50, a));
    ASSERT_TRUE(f.pool.Allocate(100, 50, b));
    f.pool.Free(a);
    f.pool.Free(b); // b starts where a ended — should merge
    EXPECT_EQ(f.pool.FreeVtxHead->Offset, 0u);
    EXPECT_EQ(f.pool.FreeVtxHead->Size, 200u);
    EXPECT_EQ(f.pool.FreeVtxHead->Next, nullptr); // merged into one node
    EXPECT_EQ(f.pool.FreeIdxHead->Offset, 0u);
    EXPECT_EQ(f.pool.FreeIdxHead->Size, 100u);
    EXPECT_EQ(f.pool.FreeIdxHead->Next, nullptr);
}

TEST(GeometryPool, ResetClearsAllState)
{
    PoolFixture    f;
    GeometryRegion r;
    ASSERT_TRUE(f.pool.Allocate(100, 50, r));
    f.pool.Free(r);
    f.pool.Reset();
    EXPECT_EQ(f.pool.VtxCursor, 0u);
    EXPECT_EQ(f.pool.IdxCursor, 0u);
    EXPECT_EQ(f.pool.VtxUsed, 0u);
    EXPECT_EQ(f.pool.IdxUsed, 0u);
    EXPECT_EQ(f.pool.FreeVtxHead, nullptr);
    EXPECT_EQ(f.pool.FreeIdxHead, nullptr);
    EXPECT_FLOAT_EQ(f.pool.FragmentationRatio(), 0.f);
    // Pool should be fully functional after reset.
    GeometryRegion r2;
    EXPECT_TRUE(f.pool.Allocate(100, 50, r2));
    EXPECT_EQ(r2.VtxByteOffset, 0u);
}

TEST(GeometryPool, FragmentationRatioZeroWhenNoFreeNodes)
{
    PoolFixture    f;
    GeometryRegion r;
    ASSERT_TRUE(f.pool.Allocate(512, 512, r));
    EXPECT_FLOAT_EQ(f.pool.FragmentationRatio(), 0.f);
}

TEST(GeometryPool, MultipleAllocsAndFreesPreserveUsedCount)
{
    PoolFixture    f;
    GeometryRegion regions[8];
    for (int i = 0; i < 8; ++i)
        ASSERT_TRUE(f.pool.Allocate(64, 32, regions[i]));
    EXPECT_EQ(f.pool.VtxUsed, 8u * 64u);
    EXPECT_EQ(f.pool.IdxUsed, 8u * 32u);
    for (int i = 0; i < 4; ++i)
        f.pool.Free(regions[i]);
    EXPECT_EQ(f.pool.VtxUsed, 4u * 64u);
    EXPECT_EQ(f.pool.IdxUsed, 4u * 32u);
}
