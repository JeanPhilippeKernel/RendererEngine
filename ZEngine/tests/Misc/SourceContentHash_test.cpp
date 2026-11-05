#include <ZEngine/Core/Hash/SourceContentHash.h>
#include <gtest/gtest.h>
#include <rapidhash.h>
#include <algorithm>
#include <vector>

namespace ZEngine::Core::Hash
{
    TEST(SourceContentHashTest, FragmentedUpdatesPreserveExistingImportChunkBoundaries)
    {
        std::vector<uint8_t> bytes(4096 * 3 + 17);
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<uint8_t>(i * 37);
        }
        for (size_t length : {size_t(0), size_t(1), size_t(4095), size_t(4096), size_t(4097), bytes.size()})
        {
            uint64_t expected = 0;
            for (size_t offset = 0; offset < length; offset += 4096)
            {
                expected = rapidhash_withSeed(bytes.data() + offset, std::min(size_t(4096), length - offset), expected);
            }
            for (size_t fragment : {size_t(1), size_t(31), size_t(4095), size_t(4096), size_t(4097), bytes.size()})
            {
                SCOPED_TRACE(length);
                SCOPED_TRACE(fragment);
                SourceContentHash hash = {};
                for (size_t offset = 0; offset < length; offset += fragment)
                {
                    ASSERT_TRUE(hash.Update({bytes.data() + offset, std::min(fragment, length - offset)}).Succeeded());
                }
                uint64_t actual = UINT64_MAX;
                ASSERT_TRUE(hash.Finalize(&actual).Succeeded());
                EXPECT_EQ(actual, expected);
            }
        }
    }

    TEST(SourceContentHashTest, InvalidUpdatesAndFinalizationPreserveActiveStateAndPriorOutput)
    {
        SourceContentHash hash    = {};
        uint8_t           bytes[] = {1, 2, 3};
        EXPECT_TRUE(hash.Update({nullptr, 1}).Failed());
        EXPECT_TRUE(hash.Finalize(nullptr).Failed());
        ASSERT_TRUE(hash.Update(bytes).Succeeded());
        uint64_t value = 0;
        ASSERT_TRUE(hash.Finalize(&value).Succeeded());
        EXPECT_EQ(value, rapidhash_withSeed(bytes, sizeof(bytes), 0));
        const auto previous = value;
        EXPECT_TRUE(hash.Update(bytes).Failed());
        EXPECT_TRUE(hash.Finalize(&value).Failed());
        EXPECT_EQ(value, previous);
    }
} // namespace ZEngine::Core::Hash
