#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <gtest/gtest.h>
#include <tlsf.h>
#include <atomic>
#include <mutex>
#include <thread>

using namespace ZEngine::Core::Memory;

namespace
{
    struct Env
    {
        MemoryManager Manager;
        Env()
        {
            Manager.Initialize(ZMega(4), {});
        }
        ~Env()
        {
            Manager.Shutdown();
        }
        ArenaAllocator* Arena()
        {
            return &Manager.MainArena;
        }
    };
} // namespace

TEST(TLSFSlabTest, InitAndShutdown)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));
    EXPECT_NE(slab.Backing, nullptr);
    EXPECT_NE(slab.Pool, nullptr);
    EXPECT_GT(slab.Overhead(), 0u);
    slab.Shutdown();
    EXPECT_EQ(slab.Pool, nullptr);
    EXPECT_EQ(slab.Backing, nullptr);
}

TEST(TLSFSlabTest, CheckedInitRejectsInvalidSizesWithoutConsumingBacking)
{
    Env        env    = {};
    TLSFSlab   slab   = {};
    const auto offset = env.Arena()->m_current_offset;
    EXPECT_FALSE(slab.TryInit(nullptr, ZKilo(64)));
    EXPECT_FALSE(slab.TryInit(env.Arena(), 0));
    EXPECT_FALSE(slab.TryInit(env.Arena(), 1));
    EXPECT_FALSE(slab.TryInit(env.Arena(), SIZE_MAX));
    EXPECT_FALSE(slab.TryInit(env.Arena(), ZMega(8)));
    EXPECT_EQ(env.Arena()->m_current_offset, offset);
    EXPECT_EQ(slab.Pool, nullptr);
    EXPECT_EQ(slab.Backing, nullptr);
    ASSERT_TRUE(slab.TryInit(env.Arena(), ZKilo(64)));
    const auto initialized_offset = env.Arena()->m_current_offset;
    const auto pool               = slab.Pool;
    EXPECT_FALSE(slab.TryInit(env.Arena(), ZKilo(64)));
    EXPECT_EQ(env.Arena()->m_current_offset, initialized_offset);
    EXPECT_EQ(slab.Pool, pool);
    slab.Shutdown();
}

TEST(TLSFSlabTest, CheckedResizeFailurePreservesAllocationAndFreedSpaceIsReusable)
{
    Env      env  = {};
    TLSFSlab slab = {};
    ASSERT_TRUE(slab.TryInit(env.Arena(), ZKilo(64)));
    auto* const allocation = static_cast<uint8_t*>(slab.TryRealloc(nullptr, 256));
    ASSERT_NE(allocation, nullptr);
    allocation[0]   = 19;
    allocation[255] = 37;
    EXPECT_EQ(slab.TryRealloc(allocation, ZMega(1)), nullptr);
    EXPECT_EQ(slab.TryRealloc(allocation, SIZE_MAX), nullptr);
    EXPECT_EQ(allocation[0], 19);
    EXPECT_EQ(allocation[255], 37);
    slab.Free(allocation);
    auto* const reused = slab.TryRealloc(nullptr, 256);
    EXPECT_EQ(reused, allocation);
    slab.Free(reused);
    slab.Shutdown();
}

TEST(TLSFSlabTest, AllocReturnsAligned)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));

    void* p = slab.Alloc(64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 8u, 0u) << "default alignment";

    slab.Shutdown();
}

TEST(TLSFSlabTest, FootprintBoundsIncludeMinimumBlocksHeadersAndUnsplittableTails)
{
    Env      env  = {};
    TLSFSlab slab = {};
    ASSERT_TRUE(slab.TryInit(env.Arena(), ZKilo(64)));
    EXPECT_EQ(TLSFSlab::AllocationFootprintBound(0, 16), 0u);
    EXPECT_EQ(TLSFSlab::AllocationFootprintBound(1, 0), 0u);
    EXPECT_EQ(TLSFSlab::AllocationFootprintBound(1, 3), 0u);
    EXPECT_EQ(TLSFSlab::AllocationFootprintBound(SIZE_MAX, 16), 0u);
    EXPECT_EQ(TLSFSlab::AllocationFootprintBound(1, size_t(1) << (sizeof(size_t) * 8 - 1)), 0u);
    // Compare real pinned TLSF block sizes, not a second sizing model.
    for (size_t neighbor : {size_t(1), size_t(25), size_t(127), size_t(1023)})
    {
        void* held = slab.TryAllocate(neighbor, 16);
        ASSERT_NE(held, nullptr);
        for (size_t alignment : {size_t(1), size_t(8), size_t(16), size_t(64), size_t(4096)})
        {
            for (size_t bytes : {size_t(1), size_t(24), size_t(31), size_t(128), size_t(4097), size_t(58000)})
            {
                const auto bound = TLSFSlab::AllocationFootprintBound(bytes, alignment);
                EXPECT_GE(bound, bytes);
                void* block = slab.TryAllocate(bytes, alignment);
                if (!block)
                {
                    continue;
                }
                EXPECT_LE(tlsf_block_size(block) + tlsf_alloc_overhead(), bound);
                slab.Free(block);
            }
        }
        slab.Free(held);
    }
    slab.Shutdown();
}

TEST(TLSFSlabTest, FootprintBoundIncludesAnActuallyUnsplittableTail)
{
    Env          env        = {};
    TLSFSlab     slab       = {};
    const size_t free_bytes = ZKilo(64);
    ASSERT_TRUE(slab.TryInit(env.Arena(), tlsf_size() + tlsf_pool_overhead() + free_bytes));
    const size_t requested = free_bytes - tlsf_align_size();
    const auto   bound     = TLSFSlab::AllocationFootprintBound(requested, tlsf_align_size());
    void*        block     = slab.TryAllocate(requested, tlsf_align_size());
    ASSERT_NE(block, nullptr);
    EXPECT_GT(tlsf_block_size(block), requested);
    EXPECT_LE(tlsf_block_size(block) + tlsf_alloc_overhead(), bound);
    slab.Free(block);
    slab.Shutdown();
}

TEST(TLSFSlabTest, AllocFreeAlloc)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));

    void* p1 = slab.Alloc(ZKilo(16));
    ASSERT_NE(p1, nullptr);
    slab.Free(p1);

    void* p2 = slab.Alloc(ZKilo(14));
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p2, p1) << "TLSF must reuse freed block via coalesce";

    slab.Shutdown();
}

TEST(TLSFSlabTest, FreeNullIsNoop)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));

    EXPECT_NO_FATAL_FAILURE(slab.Free(nullptr));

    slab.Shutdown();
}

TEST(TLSFSlabTest, ReallocNullDelegatesToAlloc)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));

    void* p = slab.Realloc(nullptr, 128);
    EXPECT_NE(p, nullptr);

    slab.Shutdown();
}

TEST(TLSFSlabTest, ReallocGrows)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));

    auto* p = reinterpret_cast<uint8_t*>(slab.Alloc(64));
    ASSERT_NE(p, nullptr);
    for (int i = 0; i < 64; ++i)
    {
        p[i] = static_cast<uint8_t>(i);
    }

    auto* p2 = reinterpret_cast<uint8_t*>(slab.Realloc(p, 256));
    ASSERT_NE(p2, nullptr);
    for (int i = 0; i < 64; ++i)
    {
        EXPECT_EQ(p2[i], static_cast<uint8_t>(i)) << "Realloc must preserve existing data at byte " << i;
    }

    slab.Shutdown();
}

TEST(TLSFSlabTest, ManyAllocsAndFrees)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(2));

    static constexpr int N = 64;
    void*                ptrs[N];
    for (int i = 0; i < N; ++i)
    {
        ptrs[i] = slab.Alloc(static_cast<size_t>(128 + i * 16));
        ASSERT_NE(ptrs[i], nullptr) << "alloc " << i;
    }
    for (int i = N - 1; i >= 0; --i)
    {
        slab.Free(ptrs[i]);
    }

    void* big = slab.Alloc(ZKilo(512));
    EXPECT_NE(big, nullptr) << "after freeing all blocks TLSF must coalesce into one large free block";

    slab.Shutdown();
}

TEST(TLSFSlabTest, ArenaBackingNotFreedOnShutdown)
{
    Env    env;
    size_t offset_before = env.Arena()->m_current_offset;

    {
        TLSFSlab slab{};
        slab.Init(env.Arena(), ZMega(1));
        slab.Shutdown();
    }

    size_t offset_after = env.Arena()->m_current_offset;
    EXPECT_GT(offset_after, offset_before) << "arena cursor must have advanced for the backing memory";
    EXPECT_NE(env.Arena()->m_memory, nullptr) << "arena must still be valid after slab shutdown";
}

TEST(TLSFSlabTest, ExhaustionAsserts)
{
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(1));

    EXPECT_DEATH(slab.Alloc(ZMega(2)), "");

    slab.Shutdown();
}

TEST(TLSFSlabTest, ConcurrentAllocFreeNoDataRace)
{
    // Simulates issue #690: worker thread allocates, render thread frees.
    // Uses a bounded SPSC ring buffer for safe pointer handoff.
    // If the spinlock is absent, TLSF internal state would be corrupted.
    Env      env;
    TLSFSlab slab{};
    slab.Init(env.Arena(), ZMega(4));

    constexpr int     ROUNDS   = 500;
    constexpr int     RING_CAP = 32;
    void*             ring[RING_CAP]{};
    std::atomic<int>  head{0}; // written by worker (producer)
    std::atomic<int>  tail{0}; // written by render (consumer)

    std::atomic<bool> go{false};

    std::thread       worker([&] {
        while (!go.load(std::memory_order_acquire))
        {
        }
        for (int i = 0; i < ROUNDS; ++i)
        {
            // Wait until there is space in the ring
            while ((head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire)) >= RING_CAP)
            {
            }

            void* p                                               = slab.Alloc(128);
            ring[head.load(std::memory_order_relaxed) % RING_CAP] = p;
            head.fetch_add(1, std::memory_order_release);
        }
    });

    std::thread       render([&] {
        while (!go.load(std::memory_order_acquire))
        {
        }
        for (int i = 0; i < ROUNDS; ++i)
        {
            // Wait until there is an item to consume
            while (tail.load(std::memory_order_relaxed) >= head.load(std::memory_order_acquire))
            {
            }

            void* p = ring[tail.load(std::memory_order_relaxed) % RING_CAP];
            slab.Free(p);
            tail.fetch_add(1, std::memory_order_release);
        }
    });

    go.store(true, std::memory_order_release);
    worker.join();
    render.join();

    // All allocations have been freed — slab should be in a clean state
    void* big = slab.Alloc(ZKilo(512));
    EXPECT_NE(big, nullptr) << "slab should be reusable after all concurrent frees";

    slab.Shutdown();
}
