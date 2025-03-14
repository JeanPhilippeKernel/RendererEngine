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
    EXPECT_EQ(vec1.size(), 0);
    EXPECT_EQ(vec1.capacity(), 0);
    EXPECT_TRUE(vec1.empty());

    Vec<int> vec2(allocator, 10);
    EXPECT_EQ(vec2.size(), 0);
    EXPECT_GE(vec2.capacity(), 10);
    EXPECT_TRUE(vec2.empty());

    Vec<int> vec3(allocator, {1, 2, 3, 4, 5});
    EXPECT_EQ(vec3.size(), 5);
    EXPECT_GE(vec3.capacity(), 5);

    for (size_t i = 0; i < vec3.size(); ++i)
    {
        EXPECT_EQ(vec3[i], i + 1);
    }
}

TEST_F(VectorTest, CopyConstructor)
{
    Vec<int> vec1(allocator, {1, 2, 3, 4, 5});
    Vec<int> vec2(vec1);

    EXPECT_EQ(vec2.size(), 5);
    EXPECT_GE(vec2.capacity(), 5);

    for (size_t i = 0; i < vec2.size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, MoveConstructor)
{
    Vec<int> vec1(allocator, {1, 2, 3, 4, 5});
    size_t   size     = vec1.size();
    size_t   capacity = vec1.capacity();

    Vec<int> vec2(std::move(vec1));

    EXPECT_EQ(vec2.size(), size);
    EXPECT_EQ(vec2.capacity(), capacity);
    EXPECT_EQ(vec1.size(), 0);
    EXPECT_EQ(vec1.capacity(), 0);

    for (size_t i = 0; i < vec2.size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, CopyAssignment)
{
    Vec<int> vec1(allocator, {1, 2, 3, 4, 5});
    Vec<int> vec2(allocator);

    vec2 = vec1;

    EXPECT_EQ(vec2.size(), 5);
    EXPECT_GE(vec2.capacity(), 5);

    for (size_t i = 0; i < vec2.size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, MoveAssignment)
{
    Vec<int> vec1(allocator, {1, 2, 3, 4, 5});
    Vec<int> vec2(allocator);

    size_t   size     = vec1.size();
    size_t   capacity = vec1.capacity();

    vec2              = std::move(vec1);

    EXPECT_EQ(vec2.size(), size);
    EXPECT_EQ(vec2.capacity(), capacity);
    EXPECT_EQ(vec1.size(), 0);
    EXPECT_EQ(vec1.capacity(), 0);

    for (size_t i = 0; i < vec2.size(); ++i)
    {
        EXPECT_EQ(vec2[i], i + 1);
    }
}

TEST_F(VectorTest, SubscriptOperator)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});

    for (size_t i = 0; i < vec.size(); ++i)
    {
        EXPECT_EQ(vec[i], i + 1);
    }
    vec[2] = 10;
    EXPECT_EQ(vec[2], 10);
}

TEST_F(VectorTest, At)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});

    for (size_t i = 0; i < vec.size(); ++i)
    {
        EXPECT_EQ(vec.at(i), i + 1);
    }

    // Test exception ?
}

TEST_F(VectorTest, FrontAndBack)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});

    EXPECT_EQ(vec.front(), 1);
    EXPECT_EQ(vec.back(), 5);

    vec.front() = 10;
    vec.back()  = 50;

    EXPECT_EQ(vec.front(), 10);
    EXPECT_EQ(vec.back(), 50);
}

TEST_F(VectorTest, data)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});

    int*     data = vec.data();
    EXPECT_NE(data, nullptr);

    for (size_t i = 0; i < vec.size(); ++i)
    {
        EXPECT_EQ(data[i], i + 1);
    }
}

// Test iterators
TEST_F(VectorTest, Iterators)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});

    int      expected = 1;
    for (auto it = vec.begin(); it != vec.end(); ++it)
    {
        EXPECT_EQ(*it, expected++);
    }
    for (auto it = vec.begin(); it != vec.end(); ++it)
    {
        *it *= 2;
    }
    for (size_t i = 0; i < vec.size(); ++i)
    {
        EXPECT_EQ(vec[i], (i + 1) * 2);
    }
}

TEST_F(VectorTest, reserve)
{
    Vec<int> vec(allocator);

    vec.reserve(10);
    EXPECT_GE(vec.capacity(), 10);
    EXPECT_EQ(vec.size(), 0);

    // reserve less than capacity should not change capacity
    size_t capacity = vec.capacity();
    vec.reserve(5);
    EXPECT_EQ(vec.capacity(), capacity);
}

TEST_F(VectorTest, PushBack)
{
    Vec<int> vec(allocator);

    size_t   initial_capacity = vec.capacity();

    for (int i = 0; i < 3; ++i)
    {
        vec.pushback(i);
        EXPECT_EQ(vec[i], i);
    }

    EXPECT_EQ(vec.size(), 3);
    if (initial_capacity > 0)
    {
        EXPECT_GE(vec.capacity(), initial_capacity);
    }
}

TEST_F(VectorTest, popback)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});
    size_t   initial_capacity = vec.capacity();

    for (int i = 0; i < 3; ++i)
    {
        vec.popback();
    }

    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec.capacity(), initial_capacity);

    vec.popback();
    vec.popback();

    EXPECT_EQ(vec.size(), 0);

    vec.popback();
    EXPECT_EQ(vec.size(), 0);
}

TEST_F(VectorTest, resize)
{
    Vec<int> vec(allocator, {1, 2, 3, 4, 5});

    vec.resize(5);
    EXPECT_EQ(vec.size(), 5);
    EXPECT_GE(vec.capacity(), 5);

    vec.resize(2);
    EXPECT_EQ(vec.size(), 2);
    EXPECT_GE(vec.capacity(), 5);

    vec.resize(4, 42);
    EXPECT_EQ(vec.size(), 4);
    EXPECT_EQ(vec[2], 42);
    EXPECT_EQ(vec[3], 42);
}
