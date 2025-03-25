#include <Core/Containers/HashMap.h>
#include <Core/Containers/Strings.h>
#include <gtest/gtest.h>
#include <string>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class HashMapTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        allocator.Initialize(2000);
    }
    void TearDown() override
    {
        allocator.Shutdown();
    }
    ArenaAllocator allocator;
};

TEST_F(HashMapTest, InitialState)
{
    HashMap<int, int> array;
    array.init(&allocator, 10, 0);
    EXPECT_EQ(array.size(), 0);
    EXPECT_EQ(array.capacity(), 10);
    EXPECT_TRUE(array.empty());
}

TEST_F(HashMapTest, Contains)
{
    HashMap<int, int> map;
    map.init(&allocator, 10, 0);
    map.insert(1, 10);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
}

TEST_F(HashMapTest, BracketOperator)
{
    HashMap<int, int> map;
    map.init(&allocator, 10, 0);
    map[1] = 10;
    EXPECT_EQ(map[1], 10);

    map[1] = 20;
    EXPECT_EQ(map[1], 20);

    EXPECT_EQ(map[2], 0);
    EXPECT_TRUE(map.contains(2));
}

TEST_F(HashMapTest, Remove)
{
    HashMap<int, int> map;
    map.init(&allocator, 10, 0);
    map.insert(1, 10);
    map.insert(2, 20);
    EXPECT_EQ(map.size(), 2);
    map.remove(1);
    EXPECT_EQ(map.size(), 1);
    EXPECT_FALSE(map.contains(1));
    EXPECT_TRUE(map.contains(2));
}

TEST_F(HashMapTest, Find)
{
    HashMap<int, int> map;
    map.init(&allocator, 10, 0);
    map.insert(1, 10);
    int* value = map.find(1);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 10);
    int* non_existent = map.find(2);
    EXPECT_EQ(non_existent, nullptr);
}

TEST_F(HashMapTest, Clear)
{
    HashMap<int, int> map;
    map.init(&allocator, 10, 0);

    // Insert multiple elements
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    EXPECT_EQ(map.size(), 3);

    map.clear();

    EXPECT_EQ(map.size(), 0);
    EXPECT_TRUE(map.empty());

    EXPECT_FALSE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
    EXPECT_FALSE(map.contains(3));
}

TEST_F(HashMapTest, Resize)
{
    HashMap<int, int> map;
    map.init(&allocator, 2, 0);

    for (int i = 0; i < 10; ++i)
    {
        map.insert(i, i * 10);
    }

    EXPECT_EQ(map.size(), 10);

    for (int i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(map.contains(i));
        EXPECT_EQ(map[i], i * 10);
    }

    EXPECT_GT(map.capacity(), 2);
}

TEST_F(HashMapTest, OverwriteValue)
{
    HashMap<int, String> map;
    map.init(&allocator, 10, 0);

    String str1;
    str1.init(&allocator, "first");

    String str2;
    str2.init(&allocator, "updated");

    map.insert(1, str1);
    EXPECT_STREQ(map[1].c_str(), str1.c_str());

    map.insert(1, str2);
    EXPECT_STREQ(map[1].c_str(), str2.c_str());
}

TEST_F(HashMapTest, CollisionHandling)
{
    HashMap<int, int> map;
    map.init(&allocator, 2, 0);

    map.insert(1, 10);
    map.insert(3, 30);
    map.insert(5, 50);

    EXPECT_TRUE(map.contains(1));
    EXPECT_TRUE(map.contains(3));
    EXPECT_TRUE(map.contains(5));

    EXPECT_EQ(map[1], 10);
    EXPECT_EQ(map[3], 30);
    EXPECT_EQ(map[5], 50);
}