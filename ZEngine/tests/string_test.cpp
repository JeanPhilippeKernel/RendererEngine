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
    EXPECT_TRUE(str1.IsEmpty());
    EXPECT_EQ(str1.Length(), 0);
    EXPECT_EQ(str1.Capacity(), 0);
    EXPECT_STREQ(str1.CStr(), "");

    const char* str = "Hello, World!";
    String      str2(str, &arena);
    EXPECT_FALSE(str2.IsEmpty());
    EXPECT_EQ(str2.Length(), strlen(str));
    EXPECT_STREQ(str2.CStr(), str);

    String str3(nullptr, &arena);
    EXPECT_TRUE(str3.IsEmpty());
    EXPECT_EQ(str3.Length(), 0);
    EXPECT_STREQ(str3.CStr(), "");
}

TEST_F(StringTest, CopyConstructorAndAssignment)
{
    String original1("Test String", &arena);
    String copy1(original1);
    EXPECT_EQ(copy1.Length(), original1.Length());
    EXPECT_STREQ(copy1.CStr(), original1.CStr());
    EXPECT_GE(copy1.Capacity(), copy1.Length() + 1);

    String original2("Test String", &arena);
    String copy2(&arena);
    copy2 = original2;
    EXPECT_EQ(copy2.Length(), original2.Length());
    EXPECT_STREQ(copy2.CStr(), original2.CStr());

    const char* testStr = "Hello, World!";
    String      str(&arena);
    str = testStr;
    EXPECT_EQ(str.Length(), strlen(testStr));
    EXPECT_STREQ(str.CStr(), testStr);
}

TEST_F(StringTest, AppendString)
{
    String str1("Hello", &arena);
    String str2(", World!", &arena);
    str1.Append(str2);
    EXPECT_STREQ(str1.CStr(), "Hello, World!");
    EXPECT_EQ(str1.Length(), 13);

    str1.Append(", NO");
    EXPECT_STREQ(str1.CStr(), "Hello, World!, NO");
    EXPECT_EQ(str1.Length(), 17);

    String str3("", &arena);
    str1.Append(str3);
    EXPECT_STREQ(str1.CStr(), "Hello, World!, NO");
    EXPECT_EQ(str1.Length(), 17);
}

TEST_F(StringTest, Substring)
{
    String str("Hello, World!", &arena);

    String sub1 = str.Substring(0, 5);
    EXPECT_STREQ(sub1.CStr(), "Hello");

    String sub2 = str.Substring(7, 5);
    EXPECT_STREQ(sub2.CStr(), "World");

    String sub3 = str.Substring(7, 100);
    EXPECT_STREQ(sub3.CStr(), "World!");

    String sub4 = str.Substring(100, 5);
    EXPECT_TRUE(sub4.IsEmpty());
}

TEST_F(StringTest, Clear)
{
    String str("Hello, World!", &arena);
    EXPECT_FALSE(str.IsEmpty());

    str.Clear();

    EXPECT_TRUE(str.IsEmpty());
    EXPECT_EQ(str.Length(), 0);
    EXPECT_STREQ(str.CStr(), "");

    EXPECT_GT(str.Capacity(), 0);
}

TEST_F(StringTest, Reserve)
{
    String str(&arena);

    str.Reserve(50);
    EXPECT_GE(str.Capacity(), 50);
    EXPECT_TRUE(str.IsEmpty());

    str                        = "Hello";
    size_t capacityAfterAssign = str.Capacity();

    str.Reserve(100);
    EXPECT_GE(str.Capacity(), 100);
    EXPECT_STREQ(str.CStr(), "Hello");

    str.Reserve(10);
    EXPECT_GE(str.Capacity(), 100);
}

TEST_F(StringTest, AccessOperator)
{
    String str("Hello", &arena);

    EXPECT_EQ(str[0], 'H');
    EXPECT_EQ(str[4], 'o');

    str[1] = 'a';
    EXPECT_STREQ(str.CStr(), "Hallo");
}

// Test equality operators
TEST_F(StringTest, EqualityOperators)
{
    String str1("Hello", &arena);
    String str2("Hello", &arena);
    String str3("World", &arena);

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
