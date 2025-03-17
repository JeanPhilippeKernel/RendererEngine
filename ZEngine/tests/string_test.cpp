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

TEST_F(StringTest, DefaultConstructor)
{
    String str1;
    str1.init(&arena);
    EXPECT_EQ(str1.size(), 0);
    EXPECT_TRUE(str1.empty());
    EXPECT_STREQ(str1.c_str(), "");
    EXPECT_GE(str1.capacity(), 16);

    String str2;
    str2.init(&arena, nullptr);

    EXPECT_EQ(str2.size(), 0);
    EXPECT_TRUE(str2.empty());
    EXPECT_STREQ(str2.c_str(), "");
    EXPECT_GE(str2.capacity(), 16);
}

TEST_F(StringTest, AppendCString)
{
    String str;
    str.init(&arena);

    str.append("Hello");
    EXPECT_STREQ(str.c_str(), "Hello");
    EXPECT_EQ(str.size(), 5);

    str.append(", World!");
    EXPECT_STREQ(str.c_str(), "Hello, World!");
    EXPECT_EQ(str.size(), 13);
}

TEST_F(StringTest, AppendString)
{
    String str1;
    str1.init(&arena, "Hello");
    String str2;
    str2.init(&arena, ", World!");

    str1.append(str2);
    EXPECT_STREQ(str1.c_str(), "Hello, World!");
    EXPECT_EQ(str1.size(), 13);
}

TEST_F(StringTest, AppendChar)
{
    String str;
    str.init(&arena, "Hello");

    str.append('!');
    EXPECT_STREQ(str.c_str(), "Hello!");
    EXPECT_EQ(str.size(), 6);

    str.append(' ');
    str.append('W');
    EXPECT_STREQ(str.c_str(), "Hello! W");
    EXPECT_EQ(str.size(), 8);
}

TEST_F(StringTest, ElementAccess)
{
    String str;
    str.init(&arena, "Hello");

    EXPECT_EQ(str[0], 'H');
    EXPECT_EQ(str[1], 'e');
    EXPECT_EQ(str[4], 'o');

    str[0] = 'J';
    EXPECT_STREQ(str.c_str(), "Jello");
}

TEST_F(StringTest, Clear)
{
    String str;
    str.init(&arena, "Hello, World!");

    EXPECT_FALSE(str.empty());
    EXPECT_EQ(str.size(), 13);

    str.clear();

    EXPECT_TRUE(str.empty());
    EXPECT_EQ(str.size(), 0);
    EXPECT_STREQ(str.c_str(), "");

    size_t capacity = str.capacity();
    str.clear();
    EXPECT_EQ(str.capacity(), capacity);
}

TEST_F(StringTest, Reserve)
{
    String str;
    str.init(&arena);

    str.reserve(50);
    EXPECT_GE(str.capacity(), 50);

    str.append("Hello, World!");
    EXPECT_STREQ(str.c_str(), "Hello, World!");
    EXPECT_GE(str.capacity(), 50);

    size_t capacity = str.capacity();
    str.reserve(30);
    EXPECT_EQ(str.capacity(), capacity);
}

TEST_F(StringTest, StringViewDefaultConstructor)
{
    StringView view;

    EXPECT_EQ(view.size(), 0);
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.data(), nullptr);
}

TEST_F(StringTest, StringViewFromCString)
{
    const char* text = "Hello, World!";
    StringView  view(text);

    EXPECT_EQ(view.size(), 13);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.data(), text);
}

TEST_F(StringTest, StringViewFromString)
{
    String str;
    str.init(&arena, "Hello, World!");
    StringView view(str);

    EXPECT_EQ(view.size(), 13);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.data(), str.c_str());
}

TEST_F(StringTest, StringViewSubstring)
{
    const char* text = "Hello, World!";
    StringView  view(text, 5);

    EXPECT_EQ(view.size(), 5);
    EXPECT_FALSE(view.empty());

    for (size_t i = 0; i < view.size(); ++i)
    {
        EXPECT_EQ(view[i], text[i]);
    }
}

TEST_F(StringTest, StringViewSet)
{
    StringView  view;
    const char* text = "Hello, World!";

    view.set(text);
    EXPECT_EQ(view.size(), 13);
    EXPECT_EQ(view.data(), text);

    String str;
    str.init(&arena, "Another string");
    view.set(str);
    EXPECT_EQ(view.size(), 14);
    EXPECT_EQ(view.data(), str.c_str());

    view.set("Short", 5);
    EXPECT_EQ(view.size(), 5);
    EXPECT_STREQ(view.data(), "Short");
}

TEST_F(StringTest, StringViewElementAccess)
{
    StringView view("Hello");

    EXPECT_EQ(view[0], 'H');
    EXPECT_EQ(view[1], 'e');
    EXPECT_EQ(view[4], 'o');
}
