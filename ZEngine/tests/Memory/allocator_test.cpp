#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;

TEST(AllocatorTest, ArenaInit)
{
    MemoryManager manager{};
    manager.Initialize(200, {});
    manager.Shutdown();
}

TEST(AllocatorTest, ArenaAllocate)
{
    MemoryManager manager{};
    manager.Initialize(200, {});
    auto arena = manager.MainArena;

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

    int* intPtr    = ZPushArray(&(manager.MainArena), int, 1);
    auto structPtr = ZPushStruct(&(manager.MainArena), Foo);

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
        auto fooPtr  = ZPushStruct(arena, Foo);
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

        auto fooPtr  = ZPushDynamicArray(&pool, Foo);
        auto fooPtr1 = ZPushDynamicArray(&pool, Foo);
        auto fooPtr2 = ZPushDynamicArray(&pool, Foo);
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

    int* val = reinterpret_cast<int*>(sub.Allocate(sizeof(int)));
    ASSERT_NE(val, nullptr);
    *val = 77;
    EXPECT_EQ(*val, 77);

    sub.Shutdown();
    EXPECT_EQ(sub.m_memory, nullptr);

    void* after = parent->Allocate(sizeof(int));
    EXPECT_NE(after, nullptr);

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
        EXPECT_NE(pool.Allocate(), nullptr) << "expected valid slot at i=" << i;

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
        ASSERT_NE(pool.Allocate(), nullptr);

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
        p1[i] = 0xAB;

    arena->Clear();

    auto* p2 = reinterpret_cast<uint8_t*>(arena->AllocateNoZero(sz));
    ASSERT_NE(p2, nullptr);
    ASSERT_EQ(p1, p2);

    bool any_nonzero = false;
    for (size_t i = 0; i < sz; ++i)
        if (p2[i] != 0)
        {
            any_nonzero = true;
            break;
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
        EXPECT_EQ(reused[i], 0u) << "byte " << i << " not zeroed after re-alloc";

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
        pool.Free(ptrs[i]);

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
