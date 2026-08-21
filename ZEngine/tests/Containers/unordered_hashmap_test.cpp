#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class HashMapTest : public ::testing::Test
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

TEST_F(HashMapTest, InitialState)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    EXPECT_EQ(map.size(), 0u);
    EXPECT_TRUE(map.empty());
    EXPECT_GE(map.capacity(), 10u);
}

TEST_F(HashMapTest, CapacityAlwaysPowerOfTwo)
{
    for (int req : {1, 2, 3, 5, 7, 10, 17, 100, 906})
    {
        UnorderedHashMap<int, int> m;
        m.init(&manager.MainArena, req);
        size_t cap = m.capacity();
        EXPECT_GT(cap, 0u);
        EXPECT_EQ(cap & (cap - 1), 0u) << "capacity " << cap << " is not power-of-2";
    }
}

TEST_F(HashMapTest, MinimumCapacityIs16)
{
    UnorderedHashMap<int, int> m;
    m.init(&manager.MainArena, 1);
    EXPECT_GE(m.capacity(), 16u);
}

TEST_F(HashMapTest, Contains)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
}

TEST_F(HashMapTest, BracketOperator)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map[1] = 10;
    EXPECT_EQ(map[1], 10);
    map[1] = 20;
    EXPECT_EQ(map[1], 20);
    EXPECT_EQ(map[2], 0); // default-inserts
    EXPECT_TRUE(map.contains(2));
}

TEST_F(HashMapTest, Remove)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.insert(2, 20);
    map.remove(1);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_FALSE(map.contains(1));
    EXPECT_TRUE(map.contains(2));
}

TEST_F(HashMapTest, Find)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    int* v = map.find(1);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 10);
    EXPECT_EQ(map.find(2), nullptr);
}

TEST_F(HashMapTest, Clear)
{
    UnorderedHashMap<int, int> map;
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

TEST_F(HashMapTest, OverwriteValue)
{
    UnorderedHashMap<int, String> map;
    map.init(&manager.MainArena, 16);
    String s1;
    s1.init(&manager.MainArena, "first");
    String s2;
    s2.init(&manager.MainArena, "updated");
    map.insert(1, s1);
    map.insert(1, s2);
    EXPECT_STREQ(map[1].c_str(), "updated");
}

TEST_F(HashMapTest, ViewIteration)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(10, 100);
    map.insert(20, 200);
    map.insert(30, 300);

    std::unordered_map<int, int> expected = {
        {10, 100},
        {20, 200},
        {30, 300}
    };
    for (auto [k, v] : map)
    {
        auto it = expected.find(k);
        ASSERT_NE(it, expected.end());
        EXPECT_EQ(v, it->second);
        expected.erase(it);
    }
    EXPECT_TRUE(expected.empty());
}

TEST_F(HashMapTest, ExplicitReserveAndBulkInsert)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    map.reserve(200);

    for (int i = 0; i < 100; ++i)
        map.insert(i, i * 2);

    EXPECT_EQ(map.size(), 100u);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(map.contains(i));
        EXPECT_EQ(*map.find(i), i * 2);
    }
}

// Edge cases

TEST_F(HashMapTest, RemoveNonExistentIsNoop)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(1, 10);
    map.remove(999);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_TRUE(map.contains(1));
}

TEST_F(HashMapTest, RemoveAndReinsertSameKey)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    map.insert(42, 100);
    map.remove(42);
    EXPECT_FALSE(map.contains(42));
    EXPECT_EQ(map.size(), 0u);
    map.insert(42, 200);
    EXPECT_TRUE(map.contains(42));
    EXPECT_EQ(*map.find(42), 200);
    EXPECT_EQ(map.size(), 1u);
}

TEST_F(HashMapTest, TombstoneReuse_RemoveAllThenReinsert)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 64);
    for (int i = 0; i < 20; ++i)
        map.insert(i, i);
    for (int i = 0; i < 20; ++i)
        map.remove(i);
    EXPECT_EQ(map.size(), 0u);

    for (int i = 0; i < 20; ++i)
        map.insert(i + 100, i + 100);

    EXPECT_EQ(map.size(), 20u);
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(*map.find(i + 100), i + 100);
}

TEST_F(HashMapTest, DuplicateInsertDoesNotChangeSize)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(7, 70);
    map.insert(7, 71);
    map.insert(7, 72);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(*map.find(7), 72);
}

TEST_F(HashMapTest, FindKeyReturnsStablePointer)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    map.insert(5, 50);
    const int* kp = map.find_key(5);
    ASSERT_NE(kp, nullptr);
    EXPECT_EQ(*kp, 5);
    EXPECT_EQ(map.find_key(999), nullptr);
}

TEST_F(HashMapTest, ClearThenReuseMap)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    for (int i = 0; i < 10; ++i)
        map.insert(i, i);
    map.clear();
    for (int i = 100; i < 110; ++i)
        map.insert(i, i * 2);
    EXPECT_EQ(map.size(), 10u);
    for (int i = 100; i < 110; ++i)
        EXPECT_EQ(*map.find(i), i * 2);
    for (int i = 0; i < 10; ++i)
        EXPECT_FALSE(map.contains(i));
}

TEST_F(HashMapTest, IteratorOnEmptyMap)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 16);
    int count = 0;
    for (auto [k, v] : map)
        ++count;
    EXPECT_EQ(count, 0);
}

TEST_F(HashMapTest, IteratorSkipsDeletedSlots)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 32);
    for (int i = 0; i < 10; ++i)
        map.insert(i, i);
    for (int i = 0; i < 10; i += 2)
        map.remove(i);

    std::unordered_set<int> seen;
    for (auto [k, v] : map)
    {
        seen.insert(k);
        EXPECT_EQ(v, k);
    }
    EXPECT_EQ(seen.size(), 5u);
    for (int i = 1; i < 10; i += 2)
        EXPECT_TRUE(seen.count(i));
}

TEST_F(HashMapTest, CStringKeyContentEquality)
{
    UnorderedHashMap<const char*, int> map;
    map.init(&manager.MainArena, 16);

    char a[] = "hello";
    char b[] = "hello"; // same content, different pointer
    map.insert(a, 42);
    EXPECT_TRUE(map.contains(b));
    EXPECT_EQ(*map.find(b), 42);
}

TEST_F(HashMapTest, LargeBatch_906Entries)
{
    UnorderedHashMap<uint32_t, uint32_t> map;
    map.init(&manager.MainArena, 906 * 2 + 16);

    for (uint32_t i = 0; i < 906; ++i)
        map.insert(i, i * 3);

    EXPECT_EQ(map.size(), 906u);
    for (uint32_t i = 0; i < 906; ++i)
    {
        EXPECT_TRUE(map.contains(i));
        EXPECT_EQ(*map.find(i), i * 3);
    }
}

TEST_F(HashMapTest, CollisionCluster_AllHashToSameBucket)
{
    // keys 0, 16, 32, 48 all hash to bucket 0 (mod 64 with capacity=64)
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 128);

    const int stride = 64;
    for (int i = 0; i < 20; ++i)
        map.insert(i * stride, i);

    for (int i = 0; i < 20; ++i)
    {
        EXPECT_TRUE(map.contains(i * stride));
        EXPECT_EQ(*map.find(i * stride), i);
    }
}

TEST_F(HashMapTest, UserDefinedStructKey)
{
    struct Point
    {
        int  x, y;
        bool operator==(const Point& o) const
        {
            return x == o.x && y == o.y;
        }
    };
    UnorderedHashMap<Point, int> map;
    map.init(&manager.MainArena, 32);
    map.insert({1, 2}, 12);
    map.insert({3, 4}, 34);
    EXPECT_EQ(*map.find({1, 2}), 12);
    EXPECT_EQ(*map.find({3, 4}), 34);
    EXPECT_EQ(map.find({5, 6}), nullptr);
}

TEST_F(HashMapTest, RemoveHeadOfCollisionChain)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 64);
    // keys 0 and 64 both hash to bucket 0 (assuming capacity=64)
    map.insert(0, 1000);
    map.insert(64, 6400);
    map.remove(0);
    EXPECT_FALSE(map.contains(0));
    EXPECT_TRUE(map.contains(64));
    EXPECT_EQ(*map.find(64), 6400);
}
