#include <ZEngine/Core/Containers/HashMap.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class OrderedHashMapTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manager.Initialize(2000, {});
    }
    void TearDown() override
    {
        manager.Shutdown();
    }
    MemoryManager manager;
};

TEST_F(OrderedHashMapTest, InitialState)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    EXPECT_EQ(map.size(), 0);
    EXPECT_EQ(map.capacity(), 10);
    EXPECT_TRUE(map.empty());
}

TEST_F(OrderedHashMapTest, Contains)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map.insert(1, 10);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
}

TEST_F(OrderedHashMapTest, BracketOperator)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map[1] = 10;
    EXPECT_EQ(map[1], 10);

    map[1] = 20;
    EXPECT_EQ(map[1], 20);

    EXPECT_EQ(map[2], 0);
    EXPECT_TRUE(map.contains(2));
}

TEST_F(OrderedHashMapTest, Remove)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map.insert(1, 10);
    map.insert(2, 20);
    EXPECT_EQ(map.size(), 2);
    map.remove(1);
    EXPECT_EQ(map.size(), 1);
    EXPECT_FALSE(map.contains(1));
    EXPECT_TRUE(map.contains(2));
}

TEST_F(OrderedHashMapTest, Find)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map.insert(1, 10);
    int* value = map.find(1);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 10);
    int* non_existent = map.find(2);
    EXPECT_EQ(non_existent, nullptr);
}

TEST_F(OrderedHashMapTest, Clear)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);

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

TEST_F(OrderedHashMapTest, Resize)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 2);

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

TEST_F(OrderedHashMapTest, OverwriteValue)
{
    HashMap<int, String> map;
    map.init(&manager.MainArena, 10);

    String str1;
    str1.init(&manager.MainArena, "first");

    String str2;
    str2.init(&manager.MainArena, "updated");

    map.insert(1, str1);
    EXPECT_STREQ(map[1].c_str(), str1.c_str());

    map.insert(1, str2);
    EXPECT_STREQ(map[1].c_str(), str2.c_str());
}

TEST_F(OrderedHashMapTest, CollisionHandling)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 2);

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

// --- Insertion-order tests ---

TEST_F(OrderedHashMapTest, InsertionOrderPreserved)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);

    map.insert(10, 100);
    map.insert(20, 200);
    map.insert(30, 300);

    std::vector<int> keys;
    for (auto [key, value] : map)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 20);
    EXPECT_EQ(keys[2], 30);
}

TEST_F(OrderedHashMapTest, InsertionOrderAfterRemove)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.remove(2);
    map.insert(4, 40);

    std::vector<int> keys;
    for (auto [key, value] : map)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 3);
    EXPECT_EQ(keys[2], 4);
}

TEST_F(OrderedHashMapTest, InsertionOrderPreservedAfterResize)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 2);

    for (int i = 0; i < 10; ++i)
        map.insert(i, i * 10);

    std::vector<int> keys;
    for (auto [key, value] : map)
        keys.push_back(key);

    ASSERT_EQ(keys.size(), 10u);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(keys[i], i);
}

TEST_F(OrderedHashMapTest, UpdateDoesNotChangeOrder)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.insert(2, 999);

    std::vector<int> keys;
    for (auto [key, value] : map)
        keys.push_back(key);

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 2);
    EXPECT_EQ(keys[2], 3);
    EXPECT_EQ(map[2], 999);
}

// --- sort_keys() tests ---

TEST_F(OrderedHashMapTest, SortKeysAscending)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(30, 300);
    map.insert(10, 100);
    map.insert(20, 200);

    map.sort_keys();

    std::vector<int> keys;
    for (auto [key, value] : map)
        keys.push_back(key);

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 20);
    EXPECT_EQ(keys[2], 30);
}

TEST_F(OrderedHashMapTest, SortKeysPreservesValues)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(3, 300);
    map.insert(1, 100);
    map.insert(2, 200);

    map.sort_keys();

    std::vector<std::pair<int, int>> entries;
    for (auto [key, value] : map)
    {
        entries.push_back({key, value});
    }

    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].first, 1);
    EXPECT_EQ(entries[0].second, 100);
    EXPECT_EQ(entries[1].first, 2);
    EXPECT_EQ(entries[1].second, 200);
    EXPECT_EQ(entries[2].first, 3);
    EXPECT_EQ(entries[2].second, 300);
}

TEST_F(OrderedHashMapTest, SortKeysDoesNotAffectLookup)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(30, 300);
    map.insert(10, 100);
    map.insert(20, 200);

    map.sort_keys();

    EXPECT_EQ(*map.find(10), 100);
    EXPECT_EQ(*map.find(20), 200);
    EXPECT_EQ(*map.find(30), 300);
}

TEST_F(OrderedHashMapTest, SortKeysOnEmptyMap)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.sort_keys();
    EXPECT_TRUE(map.empty());
}

TEST_F(OrderedHashMapTest, InsertAfterSortAppendsTail)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(30, 300);
    map.insert(10, 100);
    map.sort_keys(); // order: 10, 30

    map.insert(20, 200); // appended after sort

    std::vector<int> keys;
    for (auto [key, value] : map)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 30);
    EXPECT_EQ(keys[2], 20);
}
