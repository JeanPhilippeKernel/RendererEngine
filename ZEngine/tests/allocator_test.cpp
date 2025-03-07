#include <Core/Memory/Allocator.h>
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
    }

    arena.Shutdown();
}