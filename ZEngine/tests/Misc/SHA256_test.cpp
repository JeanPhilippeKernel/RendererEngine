#include <ZEngine/Core/Hash/SHA256.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace ZEngine::Core::Hash
{
    namespace
    {
        struct HashVector
        {
            cstring Input    = nullptr;
            cstring Expected = nullptr;
        };

        constexpr HashVector KnownAnswers[] = {
            {                                                                                                                "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {                                                                                                             "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {                                                        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
            {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
            {                                                                                                            "{}\n", "ca3d163bab055381827226140568f3bef7eaac187cebd76878e0b63e9e442356"},
        };
    } // namespace

    TEST(SHA256Test, PublishedKnownAnswersAcrossStreamingChunkSizes)
    {
        static_assert(sizeof(Digest256) == 32);
        // Pinned compression has simultaneous ah[8] and w[16] arrays outside
        // the stream object. The query is constexpr and adds no retained state.
        static_assert(SHA256::FixedWorkingBytes() == sizeof(SHA256) + 96);
        static_assert(!std::is_copy_constructible_v<SHA256>);
        static_assert(!std::is_move_constructible_v<SHA256>);
        constexpr size_t chunk_sizes[] = {1, 3, 55, 63, 64, 65, 127};
        for (const auto& vector : KnownAnswers)
        {
            for (const size_t chunk_size : chunk_sizes)
            {
                SCOPED_TRACE(vector.Input);
                SHA256       stream = {};
                const size_t length = Helpers::secure_strlen(vector.Input);
                for (size_t offset = 0; offset < length; offset += std::min(chunk_size, length - offset))
                {
                    const size_t count = std::min(chunk_size, length - offset);
                    ASSERT_TRUE(stream.Update({reinterpret_cast<const uint8_t*>(vector.Input) + offset, count}).Succeeded());
                }
                Digest256 digest = {};
                ASSERT_TRUE(stream.Finalize(&digest).Succeeded());
                EXPECT_EQ(stream.BytesWritten(), length);
                EXPECT_STREQ(digest.Hex().data(), vector.Expected);
            }
        }
    }

    TEST(SHA256Test, MillionAUsesRepeatedFixedBuffer)
    {
        uint8_t buffer[1000] = {};
        ASSERT_EQ(Helpers::secure_memset(buffer, 'a', sizeof(buffer), sizeof(buffer)), Helpers::MEMORY_OP_SUCCESS);
        SHA256 stream = {};
        for (uint32_t i = 0; i < 1000; ++i)
            ASSERT_TRUE(stream.Update(buffer).Succeeded());
        Digest256 digest = {};
        ASSERT_TRUE(stream.Finalize(&digest).Succeeded());
        EXPECT_STREQ(digest.Hex().data(), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    TEST(SHA256Test, ResetDiscardsPartialChunksAndPriorDigestsWithoutRequiringClearedScratch)
    {
        SHA256                               stream = {};
        Containers::FixedArray<uint8_t, 129> stale  = {};
        for (auto& byte : stale)
            byte = 0xa5;
        for (const size_t previous_length : {0u, 1u, 55u, 56u, 63u, 64u, 65u, 127u, 128u, 129u})
        {
            for (bool finalize_previous : {false, true})
            {
                for (const auto& vector : KnownAnswers)
                {
                    stream.Reset();
                    ASSERT_TRUE(stream.Update({stale.data(), previous_length}).Succeeded());
                    Digest256 previous = {};
                    if (finalize_previous)
                        ASSERT_TRUE(stream.Finalize(&previous).Succeeded());
                    stream.Reset();
                    EXPECT_EQ(stream.BytesWritten(), 0u);
                    const auto length = Helpers::secure_strlen(vector.Input);
                    // Both fragmented and direct compression paths after reuse.
                    for (size_t offset = 0; offset < length;)
                    {
                        const size_t count = std::min(size_t(17), length - offset);
                        ASSERT_TRUE(stream.Update({reinterpret_cast<const uint8_t*>(vector.Input) + offset, count}).Succeeded());
                        offset += count;
                    }
                    Digest256 actual = {};
                    ASSERT_TRUE(stream.Finalize(&actual).Succeeded());
                    EXPECT_STREQ(actual.Hex().data(), vector.Expected) << previous_length << "/" << finalize_previous;
                    stream.Reset();
                    ASSERT_TRUE(stream.Update({reinterpret_cast<const uint8_t*>(vector.Input), length}).Succeeded());
                    ASSERT_TRUE(stream.Finalize(&actual).Succeeded());
                    EXPECT_STREQ(actual.Hex().data(), vector.Expected);
                }
            }
        }
    }

    TEST(SHA256Test, PaddingBoundariesMatchIndependentReference)
    {
        struct BoundaryVector
        {
            size_t  Length   = 0;
            cstring Expected = nullptr;
        };
        // Independently generated with Ruby Digest::SHA256, not the dependency.
        constexpr BoundaryVector vectors[] = {
            { 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
            { 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
            { 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
            { 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
            { 65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
            {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
            {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
            {129, "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae"},
        };
        uint8_t buffer[129] = {};
        ASSERT_EQ(Helpers::secure_memset(buffer, 'a', sizeof(buffer), sizeof(buffer)), Helpers::MEMORY_OP_SUCCESS);
        for (const auto& vector : vectors)
        {
            for (size_t split = 0; split <= vector.Length; ++split)
            {
                SHA256 stream = {};
                ASSERT_TRUE(stream.Update({buffer, split}).Succeeded());
                ASSERT_TRUE(stream.Update({buffer + split, vector.Length - split}).Succeeded());
                Digest256 digest = {};
                ASSERT_TRUE(stream.Finalize(&digest).Succeeded());
                EXPECT_STREQ(digest.Hex().data(), vector.Expected) << "length " << vector.Length << ", split " << split;
            }
        }
    }

    TEST(SHA256Test, InvalidRequestsAndLengthOverflowPreserveStreamAndOutput)
    {
        SHA256    stream = {};
        Digest256 output = {};
        EXPECT_EQ(stream.Finalize(nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(stream.Update({nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        const uint8_t byte = 'a';
        EXPECT_EQ(stream.Update({&byte, std::numeric_limits<uint64_t>::max() / 8 + 1}).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(stream.BytesWritten(), 0u);
        ASSERT_TRUE(stream.Update({}).Succeeded());
        ASSERT_TRUE(stream.Finalize(&output).Succeeded());
        EXPECT_STREQ(output.Hex().data(), KnownAnswers[0].Expected);
        Digest256 unchanged = {};
        EXPECT_EQ(stream.Finalize(&unchanged).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(unchanged, Digest256{});
        EXPECT_EQ(stream.Update({&byte, 1}).Error(), VFS::VFSError::InvalidPath);
        stream.Reset();
        EXPECT_EQ(stream.BytesWritten(), 0u);
        ASSERT_TRUE(stream.Finalize(&output).Succeeded());
        EXPECT_STREQ(output.Hex().data(), KnownAnswers[0].Expected);
    }

    TEST(SHA256Test, DigestParsingAcceptsOnlyExactLowercaseHexadecimalBytes)
    {
        const auto parsed = Digest256::ParseHex(KnownAnswers[1].Expected);
        ASSERT_TRUE(parsed.Succeeded());
        EXPECT_EQ(parsed.Value().Bytes[0], 0xba);
        EXPECT_EQ(parsed.Value().Bytes[31], 0xad);
        EXPECT_STREQ(parsed.Value().Hex().data(), KnownAnswers[1].Expected);
        EXPECT_EQ(Digest256::ParseHex({}).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Digest256::ParseHex({nullptr, 64}).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Digest256::ParseHex({KnownAnswers[1].Expected, 63}).Error(), VFS::VFSError::Corrupted);
        char text[66] = {};
        ASSERT_EQ(Helpers::secure_memcpy(text, sizeof(text), KnownAnswers[1].Expected, 64), Helpers::MEMORY_OP_SUCCESS);
        text[64] = '\n';
        EXPECT_EQ(Digest256::ParseHex({text, 65}).Error(), VFS::VFSError::Corrupted);
        for (const char invalid : {'B', 'g', ' ', '\n', '\0'})
        {
            text[0] = invalid;
            EXPECT_EQ(Digest256::ParseHex({text, 64}).Error(), VFS::VFSError::Corrupted);
        }
    }
} // namespace ZEngine::Core::Hash
