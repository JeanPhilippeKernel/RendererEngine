#include <Core/Container/Vec.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Container;
using namespace ZEngine::Core::Memory;

class VectorTest : public ::testing::Test
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

TEST_F(VectorTest, Constructors)
{
    Vec<int> vec1(allocator);
    EXPECT_EQ(vec1.Size(), 0);
    EXPECT_EQ(vec1.Capacity(), 0);
    EXPECT_TRUE(vec1.Empty());

    Vec<int> vec2(10, allocator);
    EXPECT_EQ(vec2.Size(), 0);
    EXPECT_GE(vec2.Capacity(), 10);
    EXPECT_TRUE(vec2.Empty());

    Vec<int> vec3({1, 2, 3, 4, 5}, allocator);
    EXPECT_EQ(vec3.Size(), 5);
    EXPECT_GE(vec3.Capacity(), 5);

    for (size_t i = 0; i < vec3.Size(); ++i)
    {
        EXPECT_EQ(vec3[i], i + 1);
    }
}

TEST_F(VectorTest, CopyConstructor)
{
    Vec<int> vec1({1, 2, 3, 4, 5}, allocator);
    Vec<int> vec2(vec1);

    EXPECT_EQ(vec2.Size(), 5);
    EXPECT_GE(vec2.Capacity(), 5);

    for (size_t i = 0; i < vec2.Size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, MoveConstructor)
{
    Vec<int> vec1({1, 2, 3, 4, 5}, allocator);
    size_t   size     = vec1.Size();
    size_t   capacity = vec1.Capacity();

    Vec<int> vec2(std::move(vec1));

    EXPECT_EQ(vec2.Size(), size);
    EXPECT_EQ(vec2.Capacity(), capacity);
    EXPECT_EQ(vec1.Size(), 0);
    EXPECT_EQ(vec1.Capacity(), 0);

    for (size_t i = 0; i < vec2.Size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, CopyAssignment)
{
    Vec<int> vec1({1, 2, 3, 4, 5}, allocator);
    Vec<int> vec2(allocator);

    vec2 = vec1;

    EXPECT_EQ(vec2.Size(), 5);
    EXPECT_GE(vec2.Capacity(), 5);

    for (size_t i = 0; i < vec2.Size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, MoveAssignment)
{
    Vec<int> vec1({1, 2, 3, 4, 5}, allocator);
    Vec<int> vec2(allocator);

    size_t   size     = vec1.Size();
    size_t   capacity = vec1.Capacity();

    vec2              = std::move(vec1);

    EXPECT_EQ(vec2.Size(), size);
    EXPECT_EQ(vec2.Capacity(), capacity);
    EXPECT_EQ(vec1.Size(), 0);
    EXPECT_EQ(vec1.Capacity(), 0);

    for (size_t i = 0; i < vec2.Size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, SubscriptOperator)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);

    for (size_t i = 0; i < vec.Size(); ++i)
    {
        EXPECT_EQ(vec[i], i + 1);
    }
    vec[2] = 10;
    EXPECT_EQ(vec[2], 10);
}

TEST_F(VectorTest, At)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);

    for (size_t i = 0; i < vec.Size(); ++i)
    {
        EXPECT_EQ(vec.At(i), i + 1);
    }

    // Test exception ?
}

TEST_F(VectorTest, FrontAndBack)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);

    EXPECT_EQ(vec.Front(), 1);
    EXPECT_EQ(vec.Back(), 5);

    vec.Front() = 10;
    vec.Back()  = 50;

    EXPECT_EQ(vec.Front(), 10);
    EXPECT_EQ(vec.Back(), 50);
}

TEST_F(VectorTest, Data)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);

    int*     data = vec.Data();
    EXPECT_NE(data, nullptr);

    for (size_t i = 0; i < vec.Size(); ++i)
    {
        EXPECT_EQ(data[i], i + 1);
    }
}

// Test iterators
TEST_F(VectorTest, Iterators)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);

    int      expected = 1;
    for (auto it = vec.Begin(); it != vec.End(); ++it)
    {
        EXPECT_EQ(*it, expected++);
    }
    for (auto it = vec.Begin(); it != vec.End(); ++it)
    {
        *it *= 2;
    }
    for (size_t i = 0; i < vec.Size(); ++i)
    {
        EXPECT_EQ(vec[i], (i + 1) * 2);
    }
}

TEST_F(VectorTest, Reserve)
{
    Vec<int> vec(allocator);

    vec.Reserve(10);
    EXPECT_GE(vec.Capacity(), 10);
    EXPECT_EQ(vec.Size(), 0);

    // Reserve less than capacity should not change capacity
    size_t capacity = vec.Capacity();
    vec.Reserve(5);
    EXPECT_EQ(vec.Capacity(), capacity);
}

TEST_F(VectorTest, PushBack)
{
    Vec<int> vec(allocator);

    size_t   initial_capacity = vec.Capacity();

    for (int i = 0; i < 3; ++i)
    {
        vec.PushBack(i);
        EXPECT_EQ(vec[i], i);
    }

    EXPECT_EQ(vec.Size(), 3);
    if (initial_capacity > 0)
    {
        EXPECT_GE(vec.Capacity(), initial_capacity);
    }
}

TEST_F(VectorTest, PopBack)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);
    size_t   initial_capacity = vec.Capacity();

    for (int i = 0; i < 3; ++i)
    {
        vec.PopBack();
    }

    EXPECT_EQ(vec.Size(), 2);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec.Capacity(), initial_capacity);

    vec.PopBack();
    vec.PopBack();

    EXPECT_EQ(vec.Size(), 0);

    vec.PopBack();
    EXPECT_EQ(vec.Size(), 0);
}

TEST_F(VectorTest, Resize)
{
    Vec<int> vec({1, 2, 3, 4, 5}, allocator);

    vec.Resize(5);
    EXPECT_EQ(vec.Size(), 5);
    EXPECT_GE(vec.Capacity(), 5);

    vec.Resize(2);
    EXPECT_EQ(vec.Size(), 2);
    EXPECT_GE(vec.Capacity(), 5);

    vec.Resize(4, 42);
    EXPECT_EQ(vec.Size(), 4);
    EXPECT_EQ(vec[2], 42);
    EXPECT_EQ(vec[3], 42);
}
