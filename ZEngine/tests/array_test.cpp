#include <Core/Container/Array.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Container;
using namespace ZEngine::Core::Memory;

class ArrayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        allocator.Initialize(200);
    }

    void TearDown() override
    {
        allocator.Shutdown();
    }

    ArenaAllocator allocator;
};

TEST_F(ArrayTest, InitialState)
{
    Array<int> array(allocator, 10, 0);

    EXPECT_EQ(array.size(), 0);
    EXPECT_EQ(array.capacity(), 10);
    EXPECT_TRUE(array.empty());
}

TEST_F(ArrayTest, PushBack)
{
    Array<int> array(allocator, 4, 0);

    array.push(1);
    array.push(2);
    array.push(3);

    EXPECT_EQ(array.size(), 3);
    EXPECT_FALSE(array.empty());
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);
    EXPECT_EQ(array[2], 3);
}

TEST_F(ArrayTest, AutoResize)
{
    Array<int> array(allocator, 2, 0);

    EXPECT_EQ(array.capacity(), 2);

    array.push(1);
    array.push(2);
    array.push(3); // Should trigger resize

    EXPECT_EQ(array.size(), 3);
    EXPECT_EQ(array.capacity(), 4); // Should double from 2 to 4
    EXPECT_EQ(array[2], 3);
}

TEST_F(ArrayTest, PopBack)
{
    Array<int> array(allocator, 4, 0);

    array.push(1);
    array.push(2);
    array.push(3);

    EXPECT_EQ(array.size(), 3);

    array.pop();
    EXPECT_EQ(array.size(), 2);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);

    array.pop();
    array.pop();
    EXPECT_EQ(array.size(), 0);
    EXPECT_TRUE(array.empty());
}

TEST_F(ArrayTest, Clear)
{
    Array<int> array(allocator, 4, 0);

    array.push(1);
    array.push(2);
    array.push(3);

    EXPECT_EQ(array.size(), 3);

    array.clear();

    EXPECT_EQ(array.size(), 0);
    EXPECT_TRUE(array.empty());
    EXPECT_EQ(array.capacity(), 4); // Capacity should remain unchanged
}

TEST_F(ArrayTest, Reserve)
{
    Array<int> array(allocator, 4, 0);

    EXPECT_EQ(array.capacity(), 4);

    array.reserve(8);
    EXPECT_EQ(array.capacity(), 8);

    // Reserve to a smaller capacity should not change capacity
    array.reserve(6);
    EXPECT_EQ(array.capacity(), 8);
}

TEST_F(ArrayTest, FrontAndBack)
{
    Array<int> array(allocator, 4, 0);

    array.push(10);
    EXPECT_EQ(array.front(), 10);
    EXPECT_EQ(array.back(), 10);

    array.push(20);
    array.push(30);

    EXPECT_EQ(array.front(), 10);
    EXPECT_EQ(array.back(), 30);
}