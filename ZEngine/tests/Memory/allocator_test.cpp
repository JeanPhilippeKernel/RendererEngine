#include <Core/Memory/Allocator.h>
#include <Core/Memory/MemoryManager.h>
#include <Helpers/MemoryOperations.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;

TEST(AllocatorTest, ArenaInit)
{
    ArenaAllocator arena{};
    arena.Initialize(200);
    arena.Shutdown();
}

TEST(AllocatorTest, ArenaAllocate)
{
    ArenaAllocator arena{};
    arena.Initialize(200);

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

    arena.Shutdown();
}

TEST(AllocatorTest, ArenaStruct)
{
    struct Foo
    {
        int   x = 0;
        float y = 0.f;
        void  Func() {}
    };

    ArenaAllocator a{};
    a.Initialize(200);

    auto fooPtr = (Foo*) a.Allocate(sizeof(Foo));
    fooPtr->x   = 23;
    fooPtr->y   = 100.f;

    a.Shutdown();
    fooPtr = nullptr;
}

TEST(AllocatorTest, ArenaMemoryManager)
{
    MemoryManager manager{};

    manager.Initialize(MemoryConfiguration{.DefaultSize = ZKilo(8)});

    struct Foo
    {
        int   x = 0;
        float y = 0.f;
        void  Func() {}
    };

    int* intPtr    = ZPushArray(&(manager.Allocator), int, 1);
    auto structPtr = ZPushStruct(&(manager.Allocator), Foo);

    *intPtr        = 12;
    structPtr->x   = 12;
    structPtr->y   = 798.0f;

    char* str      = ZPushString(&(manager.Allocator), 12);
    Helpers::secure_memmove(str, 12, "hello", 5);

    EXPECT_EQ(*intPtr, 12);
    EXPECT_EQ(structPtr->x, 12);
    EXPECT_EQ(structPtr->y, 798.0f);
    EXPECT_STREQ(str, "hello");

    manager.Shutdowm();
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
    manager.Initialize({.DefaultSize = ZKilo(10)});
    auto arena = &(manager.Allocator);
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
    manager.Shutdowm();
}

TEST(AllocatorTest, ArenaMemoryPool)
{
    MemoryManager manager{};
    manager.Initialize({.DefaultSize = ZKilo(10)});
    auto arena = &(manager.Allocator);
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
    manager.Shutdowm();
}
