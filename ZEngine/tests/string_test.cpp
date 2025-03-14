#include <Core/Container/Strings.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Container;
using namespace ZEngine::Core::Memory;

class StringTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        arena.Initialize(200);
    }

    void TearDown() override
    {
        arena.Shutdown();
    }

    ArenaAllocator arena;
};

TEST_F(StringTest, Constructor)
{
    String str1(&arena);
    EXPECT_TRUE(str1.isempty());
    EXPECT_EQ(str1.length(), 0);
    EXPECT_EQ(str1.capacity(), 0);
    EXPECT_STREQ(str1.cstr(), "");

    const char* str = "Hello, World!";
    String      str2(&arena, str);
    EXPECT_FALSE(str2.isempty());
    EXPECT_EQ(str2.length(), strlen(str));
    EXPECT_STREQ(str2.cstr(), str);

    String str3(&arena, nullptr);
    EXPECT_TRUE(str3.isempty());
    EXPECT_EQ(str3.length(), 0);
    EXPECT_STREQ(str3.cstr(), "");
}

TEST_F(StringTest, CopyConstructorAndAssignment)
{
    String original1(&arena, "Test String");
    String copy1(original1);
    EXPECT_EQ(copy1.length(), original1.length());
    EXPECT_STREQ(copy1.cstr(), original1.cstr());
    EXPECT_GE(copy1.capacity(), copy1.length() + 1);

    String original2(&arena, "Test String");
    String copy2(&arena);
    copy2 = original2;
    EXPECT_EQ(copy2.length(), original2.length());
    EXPECT_STREQ(copy2.cstr(), original2.cstr());

    const char* testStr = "Hello, World!";
    String      str(&arena);
    str = testStr;
    EXPECT_EQ(str.length(), strlen(testStr));
    EXPECT_STREQ(str.cstr(), testStr);
}

TEST_F(StringTest, appendString)
{
    String str1(&arena, "Hello");
    String str2(&arena, ", World!");
    str1.append(str2);
    EXPECT_STREQ(str1.cstr(), "Hello, World!");
    EXPECT_EQ(str1.length(), 13);

    str1.append(", NO");
    EXPECT_STREQ(str1.cstr(), "Hello, World!, NO");
    EXPECT_EQ(str1.length(), 17);

    String str3(&arena, "");
    str1.append(str3);
    EXPECT_STREQ(str1.cstr(), "Hello, World!, NO");
    EXPECT_EQ(str1.length(), 17);
}

TEST_F(StringTest, Substring)
{
    String str(&arena, "Hello, World!");

    String sub1 = str.substring(0, 5);
    EXPECT_STREQ(sub1.cstr(), "Hello");

    String sub2 = str.substring(7, 5);
    EXPECT_STREQ(sub2.cstr(), "World");

    String sub3 = str.substring(7, 100);
    EXPECT_STREQ(sub3.cstr(), "World!");

    String sub4 = str.substring(100, 5);
    EXPECT_TRUE(sub4.isempty());
}

TEST_F(StringTest, Clear)
{
    String str(&arena, "Hello, World!");
    EXPECT_FALSE(str.isempty());

    str.clear();

    EXPECT_TRUE(str.isempty());
    EXPECT_EQ(str.length(), 0);
    EXPECT_STREQ(str.cstr(), "");

    EXPECT_GT(str.capacity(), 0);
}

TEST_F(StringTest, Reserve)
{
    String str(&arena);

    str.reserve(50);
    EXPECT_GE(str.capacity(), 50);
    EXPECT_TRUE(str.isempty());

    str                        = "Hello";
    size_t capacityAfterAssign = str.capacity();

    str.reserve(100);
    EXPECT_GE(str.capacity(), 100);
    EXPECT_STREQ(str.cstr(), "Hello");

    str.reserve(10);
    EXPECT_GE(str.capacity(), 100);
}

TEST_F(StringTest, AccessOperator)
{
    String str(&arena, "Hello");

    EXPECT_EQ(str[0], 'H');
    EXPECT_EQ(str[4], 'o');

    str[1] = 'a';
    EXPECT_STREQ(str.cstr(), "Hallo");
}

TEST_F(StringTest, EqualityOperators)
{
    String str1(&arena, "Hello");
    String str2(&arena, "Hello");
    String str3(&arena, "World");

    EXPECT_TRUE(str1 == str2);
    EXPECT_FALSE(str1 == str3);

    EXPECT_FALSE(str1 != str2);
    EXPECT_TRUE(str1 != str3);

    // Test with empty strings
    String empty1(&arena);
    String empty2(&arena);
    EXPECT_TRUE(empty1 == empty2);
    EXPECT_FALSE(empty1 != empty2);
}
