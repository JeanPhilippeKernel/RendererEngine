#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/InitializerList.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

class InitializerListTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manager.Initialize({.BufferSize = 512});
    }

    void TearDown() override
    {
        manager.Shutdown();
    }
    MemoryManager manager;
};

TEST_F(InitializerListTest, WithArray)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3, 4));

    InitializerList<int> list(array.data(), array.size());

    EXPECT_EQ(list.size(), 4);
    EXPECT_EQ(list.data()[0], 1);
    EXPECT_EQ(*(list.begin() + 2), 3);

    int total = 0;
    for (int val : list)
    {
        total += val;
    }

    EXPECT_EQ(total, 10);
}

TEST_F(InitializerListTest, WithStrings)
{
    Array<String> str_array;
    str_array.init(&manager.MainArena, 3);
    str_array.init(&manager.MainArena, 3, make_initializer_list(&manager.MainArena, String(), String(), String()));

    str_array[0].init(&manager.MainArena, "Alpha");
    str_array[1].init(&manager.MainArena, "Beta");
    str_array[2].init(&manager.MainArena, "Gamma");

    InitializerList<String> list(str_array.data(), str_array.size());

    EXPECT_EQ(list.size(), 3);
    EXPECT_STREQ(list.data()[0].c_str(), "Alpha");
    EXPECT_STREQ(list.data()[2].c_str(), "Gamma");

    std::string result;
    for (const auto& str : list)
        result += str.c_str();

    EXPECT_EQ(result, "AlphaBetaGamma");
}

TEST_F(InitializerListTest, EmptyFromArray)
{
    Array<int> empty_array;
    empty_array.init(&manager.MainArena, 0);

    InitializerList<int> list(empty_array.data(), empty_array.size());

    EXPECT_EQ(list.size(), 0);
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.begin(), list.end());
}
