#include <Core/Containers/HashMap.h>
#include <Core/Containers/Strings.h>
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
        allocator.Initialize(2000);
    }
    void TearDown() override
    {
        allocator.Shutdown();
    }
    ArenaAllocator allocator;
};

TEST_F(OrderedHashMapTest, InitialState)
{
    HashMap<int, int> map;
    map.init(&allocator, 10);
    EXPECT_EQ(map.size(), 0);
    EXPECT_EQ(map.capacity(), 10);
    EXPECT_TRUE(map.empty());
}

TEST_F(OrderedHashMapTest, Contains)
{
    HashMap<int, int> map;
    map.init(&allocator, 10);
    map.insert(1, 10);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(2));
}

TEST_F(OrderedHashMapTest, BracketOperator)
{
    HashMap<int, int> map;
    map.init(&allocator, 10);
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
    map.init(&allocator, 10);
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
    map.init(&allocator, 10);
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
    map.init(&allocator, 10);

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
    map.init(&allocator, 2);

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
    map.init(&allocator, 10);

    String str1;
    str1.init(&allocator, "first");

    String str2;
    str2.init(&allocator, "updated");

    map.insert(1, str1);
    EXPECT_STREQ(map[1].c_str(), str1.c_str());

    map.insert(1, str2);
    EXPECT_STREQ(map[1].c_str(), str2.c_str());
}

TEST_F(OrderedHashMapTest, CollisionHandling)
{
    HashMap<int, int> map;
    map.init(&allocator, 2);

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

// --- Insertion-order tests (not applicable to UnorderedHashMap) ---

TEST_F(OrderedHashMapTest, InsertionOrderPreserved)
{
    HashMap<int, int> map;
    map.init(&allocator, 16);

    map.insert(10, 100);
    map.insert(20, 200);
    map.insert(30, 300);

    std::vector<int> keys;
    for (auto [key, value] : map)
        keys.push_back(key);

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 10);
    EXPECT_EQ(keys[1], 20);
    EXPECT_EQ(keys[2], 30);
}

TEST_F(OrderedHashMapTest, InsertionOrderAfterRemove)
{
    HashMap<int, int> map;
    map.init(&allocator, 16);

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);
    map.remove(2);
    map.insert(4, 40);

    std::vector<int> keys;
    for (auto [key, value] : map)
        keys.push_back(key);

    // 2 was removed; 4 appended at end; remaining order: 1, 3, 4
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 3);
    EXPECT_EQ(keys[2], 4);
}

TEST_F(OrderedHashMapTest, InsertionOrderPreservedAfterResize)
{
    HashMap<int, int> map;
    map.init(&allocator, 2);

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
    map.init(&allocator, 16);

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    // Updating an existing key must not move it in the order sequence.
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

TEST_F(OrderedHashMapTest, IterationAfterClearAndReinsert)
{
    HashMap<int, int> map;
    map.init(&allocator, 16);

    map.insert(1, 10);
    map.insert(2, 20);
    map.clear();

    map.insert(3, 30);
    map.insert(4, 40);

    std::vector<int> keys;
    for (auto [key, value] : map)
        keys.push_back(key);

    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], 3);
    EXPECT_EQ(keys[1], 4);
}

TEST_F(OrderedHashMapTest, UserDefinedStructViewIterations)
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

    HashMap<Person, String> map;
    map.init(&allocator, 8);

    String str1;
    str1.init(&allocator, "Alice");
    String str2;
    str2.init(&allocator, "Bob");
    String str3;
    str3.init(&allocator, "Carol");
    String str4;
    str4.init(&allocator, "Engineer");
    String str5;
    str5.init(&allocator, "Designer");
    String str6;
    str6.init(&allocator, "Artist");

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

    // Verify insertion order: alice, bob, carol
    std::vector<Person> key_order;
    for (auto [key, value] : map)
        key_order.push_back(key);

    ASSERT_EQ(key_order.size(), 3u);
    EXPECT_EQ(key_order[0], alice);
    EXPECT_EQ(key_order[1], bob);
    EXPECT_EQ(key_order[2], carol);
}
