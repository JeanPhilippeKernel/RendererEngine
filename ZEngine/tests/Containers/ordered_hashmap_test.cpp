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
        manager.Initialize(ZMega(4), {});
    }
    void TearDown() override
    {
        manager.Shutdown();
    }
    MemoryManager manager;
};

// Basic API

TEST_F(OrderedHashMapTest, InitialState)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    EXPECT_EQ(map.size(), 0u);
    EXPECT_TRUE(map.empty());
    EXPECT_GE(map.capacity(), 16u);
}

TEST_F(OrderedHashMapTest, CapacityAlwaysPowerOfTwo)
{
    for (int req : {1, 2, 3, 5, 7, 10, 17, 100, 906})
    {
        HashMap<int, int> m;
        m.init(&manager.MainArena, req);
        size_t cap = m.capacity();
        EXPECT_EQ(cap & (cap - 1), 0u) << "capacity " << cap << " is not power-of-2";
    }
}

TEST_F(OrderedHashMapTest, Contains)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
}

TEST_F(OrderedHashMapTest, BracketOperator)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
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
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.insert(2, 20);
    map.remove(1);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_FALSE(map.contains(1));
    EXPECT_TRUE(map.contains(2));
}

TEST_F(OrderedHashMapTest, Find)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    ASSERT_NE(map.find(1), nullptr);
    EXPECT_EQ(*map.find(1), 10);
    EXPECT_EQ(map.find(2), nullptr);
}

TEST_F(OrderedHashMapTest, Clear)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.clear();
    EXPECT_EQ(map.size(), 0u);
    EXPECT_TRUE(map.empty());
    EXPECT_FALSE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
    EXPECT_FALSE(map.contains(3));
}

TEST_F(OrderedHashMapTest, OverwriteValue)
{
    HashMap<int, String> map;
    map.init(&manager.MainArena, 16);
    String s1;
    s1.init(&manager.MainArena, "first");
    String s2;
    s2.init(&manager.MainArena, "updated");
    map.insert(1, s1);
    map.insert(1, s2);
    EXPECT_STREQ(map[1].c_str(), "updated");
}

TEST_F(OrderedHashMapTest, ExplicitReserveAndBulkInsert)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    map.reserve(200);
    for (int i = 0; i < 100; ++i)
        map.insert(i, i * 3);
    EXPECT_EQ(map.size(), 100u);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(*map.find(i), i * 3);
}

// Insertion order

TEST_F(OrderedHashMapTest, InsertionOrderPreserved)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(10, 100);
    map.insert(20, 200);
    map.insert(30, 300);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 20);
    EXPECT_EQ(keys[2], 30);
}

TEST_F(OrderedHashMapTest, InsertionOrderAfterRemoveMiddle)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.remove(2);
    map.insert(4, 40);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 3);
    EXPECT_EQ(keys[2], 4);
}

TEST_F(OrderedHashMapTest, RemoveHead_OrderCorrect)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.remove(1);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], 2);
    EXPECT_EQ(keys[1], 3);
}

TEST_F(OrderedHashMapTest, RemoveTail_OrderCorrect)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.remove(3);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 2);
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
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 2);
    EXPECT_EQ(keys[2], 3);
    EXPECT_EQ(*map.find(2), 999);
}

TEST_F(OrderedHashMapTest, InsertionOrderPreservedAfterReserve)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    for (int i = 0; i < 20; ++i)
        map.insert(i, i * 10);
    map.reserve(256);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 20u);
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(keys[i], i);
}

TEST_F(OrderedHashMapTest, ReinsertDeletedKeyAppendsToTail)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.remove(1);
    map.insert(1, 100);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 2);
    EXPECT_EQ(keys[1], 3);
    EXPECT_EQ(keys[2], 1); // appended at tail
    EXPECT_EQ(*map.find(1), 100);
}

TEST_F(OrderedHashMapTest, ClearThenRebuildOrder)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    for (int i = 0; i < 5; ++i)
        map.insert(i, i);
    map.clear();
    for (int i = 10; i < 15; ++i)
        map.insert(i, i * 2);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(keys[i], 10 + i);
}

TEST_F(OrderedHashMapTest, IteratorOnEmptyMap)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    int count = 0;
    for (auto [k, v] : map)
        ++count;
    EXPECT_EQ(count, 0);
}

// sort_keys

TEST_F(OrderedHashMapTest, SortKeysAscending)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(30, 300);
    map.insert(10, 100);
    map.insert(20, 200);
    map.sort_keys();
    std::vector<std::pair<int, int>> entries;
    for (auto [k, v] : map)
        entries.push_back({k, v});
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].first, 10);
    EXPECT_EQ(entries[0].second, 100);
    EXPECT_EQ(entries[1].first, 20);
    EXPECT_EQ(entries[1].second, 200);
    EXPECT_EQ(entries[2].first, 30);
    EXPECT_EQ(entries[2].second, 300);
}

TEST_F(OrderedHashMapTest, SortKeysPreservesLookup)
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

TEST_F(OrderedHashMapTest, SortKeysIdempotent)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(3, 30);
    map.insert(1, 10);
    map.insert(2, 20);
    map.sort_keys();
    map.sort_keys();
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 2);
    EXPECT_EQ(keys[2], 3);
}

TEST_F(OrderedHashMapTest, SortKeysSingleEntry)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(42, 420);
    map.sort_keys();
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], 42);
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
    map.init(&manager.MainArena, 32);
    map.insert(30, 300);
    map.insert(10, 100);
    map.sort_keys(); // order: 10, 30
    map.insert(20, 200);
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 30);
    EXPECT_EQ(keys[2], 20); // appended, not sorted
}

TEST_F(OrderedHashMapTest, SortAfterRemoveAndReinsert)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    map.insert(5, 50);
    map.insert(3, 30);
    map.insert(4, 40);
    map.remove(3);
    map.insert(1, 10); // order: 5, 4, 1
    map.sort_keys();
    std::vector<int> keys;
    for (auto [k, v] : map)
        keys.push_back(k);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 4);
    EXPECT_EQ(keys[2], 5);
}

// Edge cases

TEST_F(OrderedHashMapTest, RemoveNonExistentIsNoop)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.remove(999);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_TRUE(map.contains(1));
}

TEST_F(OrderedHashMapTest, DuplicateInsertDoesNotChangeSize)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(7, 70);
    map.insert(7, 71);
    map.insert(7, 72);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(*map.find(7), 72);
}

TEST_F(OrderedHashMapTest, LargeBatch_906Entries)
{
    HashMap<uint32_t, uint32_t> map;
    map.init(&manager.MainArena, 906 * 2 + 16);
    for (uint32_t i = 0; i < 906; ++i)
        map.insert(i, i * 5);
    EXPECT_EQ(map.size(), 906u);

    // Check all present
    for (uint32_t i = 0; i < 906; ++i)
        EXPECT_EQ(*map.find(i), i * 5);

    // Check insertion order
    uint32_t expected = 0;
    for (auto [k, v] : map)
    {
        EXPECT_EQ(k, expected);
        ++expected;
    }
    EXPECT_EQ(expected, 906u);
}

TEST_F(OrderedHashMapTest, CStringKeyContentEquality)
{
    HashMap<const char*, int> map;
    map.init(&manager.MainArena, 16);
    char a[] = "hello";
    char b[] = "hello";
    map.insert(a, 99);
    EXPECT_TRUE(map.contains(b));
    EXPECT_EQ(*map.find(b), 99);
}

TEST_F(OrderedHashMapTest, FindKeyReturnsStablePointer)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(5, 50);
    const int* kp = map.find_key(5);
    ASSERT_NE(kp, nullptr);
    EXPECT_EQ(*kp, 5);
    EXPECT_EQ(map.find_key(999), nullptr);
}

TEST_F(OrderedHashMapTest, RemoveAllEntriesAndIteratorIsEmpty)
{
    HashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    for (int i = 0; i < 10; ++i)
        map.insert(i, i);
    for (int i = 0; i < 10; ++i)
        map.remove(i);
    int count = 0;
    for (auto [k, v] : map)
        ++count;
    EXPECT_EQ(count, 0);
    EXPECT_EQ(map.size(), 0u);
    // Reinsertion works after removing all
    map.insert(42, 420);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(*map.find(42), 420);
}
