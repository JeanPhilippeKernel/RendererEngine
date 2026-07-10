#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashSet.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <unordered_set>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class UnorderedHashSetTest : public ::testing::Test
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

TEST_F(UnorderedHashSetTest, InitialState)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 10);
    EXPECT_EQ(set.size(), 0);
    EXPECT_EQ(set.capacity(), 10);
    EXPECT_TRUE(set.empty());
}

TEST_F(UnorderedHashSetTest, InsertAndContains)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(2);
    EXPECT_TRUE(set.contains(1));
    EXPECT_TRUE(set.contains(2));
    EXPECT_FALSE(set.contains(3));
}

TEST_F(UnorderedHashSetTest, InsertDuplicateDoesNotGrow)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(1);
    EXPECT_EQ(set.size(), 1);
}

TEST_F(UnorderedHashSetTest, Remove)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.insert(2);
    EXPECT_EQ(set.size(), 2);
    set.remove(1);
    EXPECT_EQ(set.size(), 1);
    EXPECT_FALSE(set.contains(1));
    EXPECT_TRUE(set.contains(2));
}

TEST_F(UnorderedHashSetTest, RemoveNonExistentIsNoOp)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(1);
    set.remove(99);
    EXPECT_EQ(set.size(), 1);
    EXPECT_TRUE(set.contains(1));
}

TEST_F(UnorderedHashSetTest, Find)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 10);
    set.insert(42);
    const int* found = set.find(42);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 42);
    EXPECT_EQ(set.find(99), nullptr);
}

TEST_F(UnorderedHashSetTest, Clear)
{
    UnorderedHashSet<int> set;
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

TEST_F(UnorderedHashSetTest, Resize)
{
    UnorderedHashSet<int> set;
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

TEST_F(UnorderedHashSetTest, CollisionHandling)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 2);
    set.insert(1);
    set.insert(3);
    set.insert(5);
    EXPECT_TRUE(set.contains(1));
    EXPECT_TRUE(set.contains(3));
    EXPECT_TRUE(set.contains(5));
}

TEST_F(UnorderedHashSetTest, Iteration)
{
    UnorderedHashSet<int> set;
    set.init(&manager.MainArena, 8);
    set.insert(10);
    set.insert(20);
    set.insert(30);

    std::unordered_set<int> expected = {10, 20, 30};
    for (const int& key : set)
    {
        auto it = expected.find(key);
        ASSERT_NE(it, expected.end());
        expected.erase(it);
    }
    EXPECT_TRUE(expected.empty());
}

TEST_F(UnorderedHashSetTest, StringKeys)
{
    UnorderedHashSet<String> set;
    set.init(&manager.MainArena, 8);

    String a, b, c;
    a.init(&manager.MainArena, "alpha");
    b.init(&manager.MainArena, "beta");
    c.init(&manager.MainArena, "gamma");

    set.insert(a);
    set.insert(b);

    EXPECT_TRUE(set.contains(a));
    EXPECT_TRUE(set.contains(b));
    EXPECT_FALSE(set.contains(c));

    set.remove(a);
    EXPECT_FALSE(set.contains(a));
    EXPECT_EQ(set.size(), 1);
}
