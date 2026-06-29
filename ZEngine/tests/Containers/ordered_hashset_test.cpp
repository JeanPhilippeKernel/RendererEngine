#include <Core/Containers/HashSet.h>
#include <Core/Containers/Strings.h>
#include <Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <vector>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class OrderedHashSetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manager.Initialize({.BufferSize = 2000});
    }
    void TearDown() override
    {
        manager.Shutdown();
    }
    MemoryManager manager;
};

TEST_F(OrderedHashSetTest, InitialState)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    EXPECT_EQ(set.size(), 0);
    EXPECT_EQ(set.capacity(), 10);
    EXPECT_TRUE(set.empty());
}

TEST_F(OrderedHashSetTest, InsertAndContains)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(2);
    EXPECT_TRUE(set.contains(1));
    EXPECT_TRUE(set.contains(2));
    EXPECT_FALSE(set.contains(3));
}

TEST_F(OrderedHashSetTest, InsertDuplicateDoesNotGrow)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(1);
    EXPECT_EQ(set.size(), 1);
}

TEST_F(OrderedHashSetTest, Remove)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(2);
    EXPECT_EQ(set.size(), 2);
    set.remove(1);
    EXPECT_EQ(set.size(), 1);
    EXPECT_FALSE(set.contains(1));
    EXPECT_TRUE(set.contains(2));
}

TEST_F(OrderedHashSetTest, RemoveNonExistentIsNoOp)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.remove(99);
    EXPECT_EQ(set.size(), 1);
    EXPECT_TRUE(set.contains(1));
}

TEST_F(OrderedHashSetTest, Find)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(42);
    const int* found = set.find(42);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 42);
    EXPECT_EQ(set.find(99), nullptr);
}

TEST_F(OrderedHashSetTest, Clear)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(2);
    set.insert(3);
    EXPECT_EQ(set.size(), 3);
    set.clear();
    EXPECT_EQ(set.size(), 0);
    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(set.contains(1));
    EXPECT_FALSE(set.contains(2));
    EXPECT_FALSE(set.contains(3));
}

TEST_F(OrderedHashSetTest, Resize)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 2);
    for (int i = 0; i < 10; ++i)
    {
        set.insert(i);
    }
    EXPECT_EQ(set.size(), 10);
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(set.contains(i));
    }
    EXPECT_GT(set.capacity(), 2);
}

TEST_F(OrderedHashSetTest, CollisionHandling)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 2);
    set.insert(1);
    set.insert(3);
    set.insert(5);
    EXPECT_TRUE(set.contains(1));
    EXPECT_TRUE(set.contains(3));
    EXPECT_TRUE(set.contains(5));
}

// --- Insertion-order tests ---

TEST_F(OrderedHashSetTest, InsertionOrderPreserved)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.insert(30);
    set.insert(10);
    set.insert(20);

    std::vector<int> keys;
    for (const int& key : set)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 30);
    EXPECT_EQ(keys[1], 10);
    EXPECT_EQ(keys[2], 20);
}

TEST_F(OrderedHashSetTest, InsertionOrderAfterRemove)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.insert(1);
    set.insert(2);
    set.insert(3);
    set.remove(2);
    set.insert(4);

    std::vector<int> keys;
    for (const int& key : set)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 3);
    EXPECT_EQ(keys[2], 4);
}

TEST_F(OrderedHashSetTest, InsertionOrderPreservedAfterResize)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 2);
    for (int i = 0; i < 10; ++i)
    {
        set.insert(i);
    }

    std::vector<int> keys;
    for (const int& key : set)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 10u);
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(keys[i], i);
    }
}

TEST_F(OrderedHashSetTest, DuplicateInsertDoesNotChangeOrder)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.insert(1);
    set.insert(2);
    set.insert(3);
    set.insert(2);

    std::vector<int> keys;
    for (const int& key : set)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 2);
    EXPECT_EQ(keys[2], 3);
}

// --- sort_keys() tests ---

TEST_F(OrderedHashSetTest, SortKeysAscending)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.insert(30);
    set.insert(10);
    set.insert(20);

    set.sort_keys();

    std::vector<int> keys;
    for (const int& key : set)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 20);
    EXPECT_EQ(keys[2], 30);
}

TEST_F(OrderedHashSetTest, SortKeysDoesNotAffectLookup)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.insert(30);
    set.insert(10);
    set.insert(20);

    set.sort_keys();

    EXPECT_TRUE(set.contains(10));
    EXPECT_TRUE(set.contains(20));
    EXPECT_TRUE(set.contains(30));
    EXPECT_NE(set.find(10), nullptr);
    EXPECT_NE(set.find(20), nullptr);
    EXPECT_NE(set.find(30), nullptr);
}

TEST_F(OrderedHashSetTest, SortKeysOnEmptySet)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.sort_keys();
    EXPECT_TRUE(set.empty());
}

TEST_F(OrderedHashSetTest, InsertAfterSortAppendsTail)
{
    HashSet<int> set;
    set.init(&manager.MainArena, 16);
    set.insert(30);
    set.insert(10);
    set.sort_keys();

    set.insert(20);

    std::vector<int> keys;
    for (const int& key : set)
    {
        keys.push_back(key);
    }

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 30);
    EXPECT_EQ(keys[2], 20);
}
