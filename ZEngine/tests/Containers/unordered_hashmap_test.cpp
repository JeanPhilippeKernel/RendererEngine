#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <string>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class HashMapTest : public ::testing::Test
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

TEST_F(HashMapTest, InitialState)
{
    UnorderedHashMap<int, int> array;
    array.init(&manager.MainArena, 10);
    EXPECT_EQ(array.size(), 0);
    EXPECT_EQ(array.capacity(), 10);
    EXPECT_TRUE(array.empty());
}

TEST_F(HashMapTest, Contains)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map.insert(1, 10);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
}

TEST_F(HashMapTest, BracketOperator)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map[1] = 10;
    EXPECT_EQ(map[1], 10);

    map[1] = 20;
    EXPECT_EQ(map[1], 20);

    EXPECT_EQ(map[2], 0);
    EXPECT_TRUE(map.contains(2));
}

TEST_F(HashMapTest, Remove)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 10);
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
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 10);
    map.insert(1, 10);
    int* value = map.find(1);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 10);
    int* non_existent = map.find(2);
    EXPECT_EQ(non_existent, nullptr);
}

TEST_F(HashMapTest, Clear)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 10);

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
    UnorderedHashMap<int, int> map;
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

TEST_F(HashMapTest, OverwriteValue)
{
    UnorderedHashMap<int, String> map;
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

TEST_F(HashMapTest, CollisionHandling)
{
    UnorderedHashMap<int, int> map;
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

TEST_F(HashMapTest, ViewIteration)
{
    UnorderedHashMap<int, int> map;
    map.init(&manager.MainArena, 8);

    map.insert(10, 100);
    map.insert(20, 200);
    map.insert(30, 300);

    std::unordered_map<int, int> expected = {
        {10, 100},
        {20, 200},
        {30, 300}
    };

    for (auto [key, value] : map)
    {
        auto it = expected.find(key);
        ASSERT_NE(it, expected.end());
        EXPECT_EQ(value, it->second);
        expected.erase(it);
    }

    EXPECT_TRUE(expected.empty());
}

TEST_F(HashMapTest, UserDefinedStructViewIterations)
{
    struct Person
    {
        String name;
        int    age;

        bool   operator==(const Person& other) const
        {
            return name == other.name && age == other.age;
        }
    };

    UnorderedHashMap<Person, String> map;
    map.init(&manager.MainArena, 8);

    String str1;
    str1.init(&manager.MainArena, "Alice");
    String str2;
    str2.init(&manager.MainArena, "Bob");
    String str3;
    str3.init(&manager.MainArena, "Carol");
    String str4;
    str4.init(&manager.MainArena, "Engineer");
    String str5;
    str5.init(&manager.MainArena, "Designer");
    String str6;
    str6.init(&manager.MainArena, "Artist");

    Person alice{str1, 30};
    Person bob{str2, 25};
    Person carol{str3, 28};

    map.insert(alice, str4);
    map.insert(bob, str5);
    map.insert(carol, str6);

    EXPECT_TRUE(map.contains(alice));
    EXPECT_TRUE(map.contains(bob));

    EXPECT_EQ(map[alice], str4);
    EXPECT_EQ(map[bob], str5);

    struct ExpectedEntry
    {
        Person key;
        String value;
        bool   matched = false;
    };

    ExpectedEntry expected[] = {
        {alice, str4},
        {  bob, str5},
        {carol, str6}
    };

    size_t matched_count = 0;

    for (auto [key, value] : map)
    {
        bool found = false;
        for (auto& entry : expected)
        {
            if (!entry.matched && entry.key == key)
            {
                EXPECT_EQ(value, entry.value);
                entry.matched = true;
                found         = true;
                matched_count++;
                break;
            }
        }
        ASSERT_TRUE(found);
    }

    EXPECT_EQ(matched_count, 3);
}
