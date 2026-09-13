#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/SerializerCommonHelper.h>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace ZEngine::Helpers
{
    TEST(SerializerCommonHelperTest, ReadsStringsLargerThanLegacyStackBuffer)
    {
        Core::Memory::MemoryManager manager = {};
        manager.Initialize(ZMega(2), {});

        const std::string text(DEFAULT_STR_BUFFER * 4, 'x');
        std::stringstream  stream(std::ios::in | std::ios::out | std::ios::binary);

        Core::Containers::String input = {};
        input.init(&manager.MainArena, text.c_str());
        WriteBinaryString(stream, input);
        stream.seekg(0);

        Core::Containers::String output = {};
        ASSERT_TRUE(ReadBinaryString(&manager.MainArena, stream, output));
        EXPECT_EQ(output.size(), text.size());
        EXPECT_STREQ(output.c_str(), text.c_str());
    }

    TEST(SerializerCommonHelperTest, ReadsCStringIntoArenaOwnedString)
    {
        Core::Memory::MemoryManager manager = {};
        manager.Initialize(ZMega(2), {});

        const std::string text(DEFAULT_STR_BUFFER * 4, 'y');
        std::stringstream  stream(std::ios::in | std::ios::out | std::ios::binary);

        WriteBinaryString(stream, text.c_str());
        stream.seekg(0);

        Core::Containers::String output = {};
        ASSERT_TRUE(ReadBinaryCString(&manager.MainArena, stream, output));
        EXPECT_EQ(output.size(), text.size());
        EXPECT_STREQ(output.c_str(), text.c_str());
    }
} // namespace ZEngine::Helpers
