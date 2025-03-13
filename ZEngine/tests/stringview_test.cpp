#include <Core/Container/StringView.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Container;

TEST(StringViewTest, DefaultConstructor)
{
    StringView sv;
    EXPECT_EQ(sv.size(), 0);
    EXPECT_TRUE(sv.empty());
    EXPECT_STREQ(sv.data(), "");
}

TEST(StringViewTest, CStringConstructor)
{
    const char* test_str = "Hello, World!";
    StringView  sv(test_str);
    EXPECT_EQ(sv.size(), 13);
    EXPECT_FALSE(sv.empty());
    EXPECT_EQ(sv.data(), test_str);
    EXPECT_STREQ(sv.data(), test_str);
}

TEST(StringViewTest, CopyConstructor)
{
    StringView original("Test String");
    StringView copy(original);
    EXPECT_EQ(copy.size(), original.size());
    EXPECT_EQ(copy.data(), original.data());
}

TEST(StringViewTest, LengthCalculation)
{
    EXPECT_EQ(StringView::len(""), 0);
    EXPECT_EQ(StringView::len("a"), 1);
    EXPECT_EQ(StringView::len("abc"), 3);
    EXPECT_EQ(StringView::len("Hello, World!"), 13);
}

TEST(StringViewTest, Iterators)
{
    StringView sv("abc");

    auto       it = sv.begin();
    EXPECT_EQ(*it, 'a');

    EXPECT_EQ(*(it++), 'a');
    EXPECT_EQ(*it, 'b');
    EXPECT_EQ(*(++it), 'c');
    EXPECT_EQ(++it, sv.end());

    std::string result;
    for (char c : sv)
    {
        result += c;
    }
    EXPECT_EQ(result, "abc");
}

TEST(StringViewTest, DataAndSize)
{
    const char* test_str = "Test String";
    StringView  sv(test_str);
    EXPECT_EQ(sv.data(), test_str);
    EXPECT_EQ(sv.size(), 11);
}

TEST(StringViewTest, Empty)
{
    EXPECT_TRUE(StringView().empty());
    EXPECT_TRUE(StringView("").empty());
    EXPECT_FALSE(StringView("a").empty());
}

TEST(StringViewTest, IndexOperator)
{
    StringView sv("abcdef");
    EXPECT_EQ(sv[0], 'a');
    EXPECT_EQ(sv[3], 'd');
    EXPECT_EQ(sv[5], 'f');
}

TEST(StringViewTest, At)
{
    StringView sv("abcdef");
    EXPECT_EQ(sv.at(0), 'a');
    EXPECT_EQ(sv.at(3), 'd');
    EXPECT_EQ(sv.at(5), 'f');

    // Note: The current implementation doesn't check bounds
}

TEST(StringViewTest, EqualityOperator)
{
    StringView sv1("test");
    StringView sv2("test");
    StringView sv3("different");
    StringView sv4("");

    EXPECT_TRUE(sv1 == sv2);
    EXPECT_FALSE(sv1 == sv3);
    EXPECT_FALSE(sv1 == sv4);
    EXPECT_TRUE(StringView() == StringView(""));
}

TEST(StringViewTest, InequalityOperator)
{
    StringView sv1("test");
    StringView sv2("test");
    StringView sv3("different");

    EXPECT_FALSE(sv1 != sv2);
    EXPECT_TRUE(sv1 != sv3);
}