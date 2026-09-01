#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <gtest/gtest.h>
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
        p[i] = static_cast<uint8_t>(i);

    auto* p2 = reinterpret_cast<uint8_t*>(slab.Realloc(p, 256));
    ASSERT_NE(p2, nullptr);
    for (int i = 0; i < 64; ++i)
        EXPECT_EQ(p2[i], static_cast<uint8_t>(i)) << "Realloc must preserve existing data at byte " << i;

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
        slab.Free(ptrs[i]);

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
