#include <ZEngine/Core/Serialization/CanonicalJSONWriter.h>
#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <limits>

namespace ZEngine::Core::Serialization
{
    namespace
    {
        struct JSONSink
        {
            char                        Bytes[1024] = {};
            size_t                      Count       = 0;
            uint32_t                    Calls       = 0;
            uint32_t                    FailAt      = UINT32_MAX;

            static VFS::VFSResult<void> Write(void* context, Containers::ArrayView<const uint8_t> bytes)
            {
                auto& self = *static_cast<JSONSink*>(context);
                if (++self.Calls == self.FailAt)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied);
                }
                if (bytes.size() > sizeof(self.Bytes) - self.Count)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                if (bytes.size())
                {
                    Helpers::secure_memcpy(self.Bytes + self.Count, sizeof(self.Bytes) - self.Count, bytes.data(), bytes.size());
                }
                self.Count += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }

            Containers::StringView Text() const
            {
                return {Bytes, Count};
            }
        };
    } // namespace

    TEST(CanonicalJSONWriterTest, EscapesControlsQuotesAndBackslashesWithoutEscapingSlashOrUnicode)
    {
        JSONSink            sink = {};
        CanonicalJSONWriter writer(JSONSink::Write, &sink);
        constexpr char      text[] = {'a', '\0', '\x1f', '\n', '"', '\\', '/', '\xc3', '\xa9'};
        ASSERT_TRUE(writer.String({text, sizeof(text)}));
        EXPECT_TRUE(
            sink.Text() == R"("a\u0000\u001f\u000a\"\\/)"
                           "\xc3\xa9"
                           "\"");
        JSONReader             reader({reinterpret_cast<const uint8_t*>(sink.Bytes), sink.Count});
        char                   decoded[16] = {};
        Containers::StringView value       = {};
        ASSERT_TRUE(reader.String(decoded, &value));
        EXPECT_TRUE(reader.Finish());
        ASSERT_EQ(value.size(), sizeof(text));
        EXPECT_EQ(Helpers::secure_memcmp(value.data(), value.size(), text, sizeof(text), sizeof(text)), 0);
    }

    TEST(CanonicalJSONWriterTest, EveryControlUsesItsLowercaseSixByteEscape)
    {
        constexpr cstring hex = "0123456789abcdef";
        for (uint8_t code = 0; code < 32; ++code)
        {
            JSONSink            sink = {};
            CanonicalJSONWriter writer(JSONSink::Write, &sink);
            const char          byte = static_cast<char>(code);
            ASSERT_TRUE(writer.String({&byte, 1}));
            char expected[] = {'"', '\\', 'u', '0', '0', hex[code >> 4], hex[code & 15], '"'};
            EXPECT_EQ(sink.Count, sizeof(expected));
            EXPECT_EQ(Helpers::secure_memcmp(sink.Bytes, sizeof(sink.Bytes), expected, sizeof(expected), sizeof(expected)), 0);
        }
    }

    TEST(CanonicalJSONWriterTest, EmptyStringsAndSchemaDirectedPunctuationHaveNoWhitespaceOrHiddenNewline)
    {
        JSONSink            sink = {};
        CanonicalJSONWriter writer(JSONSink::Write, &sink);
        EXPECT_TRUE(writer.Raw("{\"a\":"));
        EXPECT_TRUE(writer.String({}));
        EXPECT_TRUE(writer.Raw(",\"b\":"));
        EXPECT_TRUE(writer.UInt64(UINT64_MAX));
        EXPECT_TRUE(writer.Raw(",\"c\":"));
        EXPECT_TRUE(writer.Boolean(true));
        EXPECT_TRUE(writer.Raw(",\"d\":"));
        EXPECT_TRUE(writer.Boolean(false));
        EXPECT_TRUE(writer.Raw("}\n"));
        EXPECT_TRUE(sink.Text() == "{\"a\":\"\",\"b\":18446744073709551615,\"c\":true,\"d\":false}\n");
        EXPECT_EQ(writer.Error(), VFS::VFSError::OK);
    }

    TEST(CanonicalJSONWriterTest, FloatGoldensNormalizeSignedZeroExponentsAndTiesToEven)
    {
        struct FloatCase
        {
            float   Value = 0.f;
            cstring Text  = nullptr;
        };
        const FloatCase cases[] = {
            {                                     0.f,              "0"},
            {                                    -0.f,              "0"},
            {                                     1.f,   "1.00000000e0"},
            {                                     .5f,  "5.00000000e-1"},
            {                                   -10.f,  "-1.00000000e1"},
            {                            1.001953125f,   "1.00195312e0"},
            {                            1.005859375f,   "1.00585938e0"},
            {std::numeric_limits<float>::denorm_min(), "1.40129846e-45"},
            {       std::numeric_limits<float>::max(),  "3.40282347e38"},
        };
        for (const auto& item : cases)
        {
            SCOPED_TRACE(item.Text);
            JSONSink            sink = {};
            CanonicalJSONWriter writer(JSONSink::Write, &sink);
            ASSERT_TRUE(writer.Float32(item.Value));
            EXPECT_TRUE(sink.Text() == item.Text);
            EXPECT_EQ(sink.Calls, 1u);
        }
    }

    TEST(CanonicalJSONWriterTest, SampledFiniteIEEE754ValuesRoundTripThroughTheSharedReader)
    {
        uint32_t state = 0x12345678;
        for (uint32_t i = 0; i < 16384; ++i)
        {
            state             = state * 1664525u + 1013904223u;
            const float value = std::bit_cast<float>(state);
            if (!std::isfinite(value))
            {
                continue;
            }
            JSONSink            sink = {};
            CanonicalJSONWriter writer(JSONSink::Write, &sink);
            ASSERT_TRUE(writer.Float32(value));
            JSONReader reader({reinterpret_cast<const uint8_t*>(sink.Bytes), sink.Count});
            float      decoded = 0.f;
            ASSERT_TRUE(reader.Float32(&decoded)) << sink.Bytes;
            EXPECT_TRUE(reader.Finish());
            EXPECT_EQ(std::bit_cast<uint32_t>(decoded), value == 0.f ? 0u : state);
        }
    }

    TEST(CanonicalJSONWriterTest, UUIDAndHexStringsShareInlineScratchWithEscapesAndNumbers)
    {
        JSONSink            sink = {};
        CanonicalJSONWriter writer(JSONSink::Write, &sink);
        const auto          uuid    = uuids::uuid::from_string("00112233-4455-6677-8899-aabbccddeeff").value();
        const uint8_t       bytes[] = {0, 1, 0x7f, 0x80, 0xfe, 0xff};
        ASSERT_TRUE(writer.Raw("["));
        ASSERT_TRUE(writer.UUID(uuid));
        ASSERT_TRUE(writer.Raw(","));
        ASSERT_TRUE(writer.HexString(bytes));
        ASSERT_TRUE(writer.Raw(","));
        ASSERT_TRUE(writer.String("\n\"\\"));
        ASSERT_TRUE(writer.Raw(","));
        ASSERT_TRUE(writer.UInt64(UINT64_MAX));
        ASSERT_TRUE(writer.Raw(","));
        ASSERT_TRUE(writer.Float32(.5f));
        ASSERT_TRUE(writer.Raw(","));
        ASSERT_TRUE(writer.UUID({}));
        ASSERT_TRUE(writer.Raw(","));
        ASSERT_TRUE(writer.HexString({}));
        ASSERT_TRUE(writer.Raw("]"));
        EXPECT_EQ(sink.Text(), Containers::StringView(R"(["00112233-4455-6677-8899-aabbccddeeff","00017f80feff","\u000a\"\\",18446744073709551615,5.00000000e-1,"00000000-0000-0000-0000-000000000000",""])"));
    }

    TEST(CanonicalJSONWriterTest, HexStreamsAcrossInlineChunkBoundariesWithExactLowercaseBytes)
    {
        Containers::FixedArray<uint8_t, 256> bytes = {};
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<uint8_t>(i);
        }
        constexpr cstring hex = "0123456789abcdef";
        for (size_t count : {0u, 1u, 31u, 32u, 33u, 127u, 256u})
        {
            JSONSink            sink = {};
            CanonicalJSONWriter writer(JSONSink::Write, &sink);
            ASSERT_TRUE(writer.HexString({bytes.data(), count}));
            ASSERT_EQ(sink.Count, count * 2 + 2);
            EXPECT_EQ(sink.Bytes[0], '"');
            EXPECT_EQ(sink.Bytes[sink.Count - 1], '"');
            for (size_t i = 0; i < count; ++i)
            {
                EXPECT_EQ(sink.Bytes[1 + i * 2], hex[bytes[i] >> 4]);
                EXPECT_EQ(sink.Bytes[2 + i * 2], hex[bytes[i] & 15]);
            }
            EXPECT_EQ(sink.Calls, 2 + (count + 31) / 32);
        }
        JSONSink            sink = {};
        CanonicalJSONWriter writer(JSONSink::Write, &sink);
        EXPECT_FALSE(writer.HexString({nullptr, 1}));
        EXPECT_EQ(writer.Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST(CanonicalJSONWriterTest, SameWriterSinkReentryFailsBeforeClobberingBorrowedFormattingBytes)
    {
        struct ReentrantSink
        {
            CanonicalJSONWriter*        Writer    = nullptr;
            uint32_t                    Operation = 0;
            uint32_t                    Calls     = 0;
            bool                        Nested    = true;
            bool                        Unchanged = false;
            static VFS::VFSResult<void> Write(void* context, Containers::ArrayView<const uint8_t> bytes)
            {
                auto& self = *static_cast<ReentrantSink*>(context);
                ++self.Calls;
                Containers::FixedArray<uint8_t, 64> before = {};
                if (bytes.size() > before.size())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                Helpers::secure_memcpy(before.data(), before.size(), bytes.data(), bytes.size());
                const uint8_t hex[] = {0xff};
                switch (self.Operation)
                {
                    case 0:
                        self.Nested = self.Writer->Raw("true");
                        break;
                    case 1:
                        self.Nested = self.Writer->String("\n");
                        break;
                    case 2:
                        self.Nested = self.Writer->UInt64(7);
                        break;
                    case 3:
                        self.Nested = self.Writer->Float32(.5f);
                        break;
                    case 4:
                        self.Nested = self.Writer->Boolean(true);
                        break;
                    case 5:
                        self.Nested = self.Writer->UUID({});
                        break;
                    case 6:
                        self.Nested = self.Writer->HexString(hex);
                        break;
                    default:
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
                }
                self.Unchanged = Helpers::secure_memcmp(before.data(), before.size(), bytes.data(), bytes.size(), bytes.size()) == 0;
                // A callback cannot hide the nested refusal by returning success.
                return VFS::VFSResult<void>::Ok();
            }
        };
        for (uint32_t operation = 0; operation < 7; ++operation)
        {
            ReentrantSink       sink = {.Operation = operation};
            CanonicalJSONWriter writer(ReentrantSink::Write, &sink);
            sink.Writer = &writer;
            EXPECT_FALSE(writer.UInt64(UINT64_MAX));
            EXPECT_FALSE(sink.Nested);
            EXPECT_TRUE(sink.Unchanged);
            EXPECT_EQ(sink.Calls, 1u);
            EXPECT_EQ(writer.Error(), VFS::VFSError::InvalidPath);
            EXPECT_FALSE(writer.Float32(1.f));
            EXPECT_EQ(sink.Calls, 1u);
        }
    }

    TEST(CanonicalJSONWriterTest, SinkCallbacksMayUseAnotherWriterWithoutCorruptingEitherBuffer)
    {
        struct SiblingSink
        {
            JSONSink                    Primary = {};
            JSONSink                    Sibling = {};
            static VFS::VFSResult<void> Write(void* context, Containers::ArrayView<const uint8_t> bytes)
            {
                auto&               self = *static_cast<SiblingSink*>(context);
                CanonicalJSONWriter sibling(JSONSink::Write, &self.Sibling);
                if (!sibling.Float32(.5f))
                {
                    return VFS::VFSResult<void>::Fail(sibling.Error());
                }
                return JSONSink::Write(&self.Primary, bytes);
            }
        };
        SiblingSink         sink = {};
        CanonicalJSONWriter writer(SiblingSink::Write, &sink);
        ASSERT_TRUE(writer.UInt64(UINT64_MAX));
        EXPECT_EQ(writer.Error(), VFS::VFSError::OK);
        EXPECT_EQ(sink.Primary.Text(), Containers::StringView("18446744073709551615"));
        EXPECT_EQ(sink.Sibling.Text(), Containers::StringView("5.00000000e-1"));
    }

    TEST(CanonicalJSONWriterTest, InvalidUnicodeOrNumbersFailBeforeWritingAndStayFailed)
    {
        for (uint32_t invalid = 0; invalid < 5; ++invalid)
        {
            JSONSink            sink = {};
            CanonicalJSONWriter writer(JSONSink::Write, &sink);
            if (invalid == 0)
            {
                EXPECT_FALSE(writer.String({"\xc0\x80", 2}));
            }
            if (invalid == 1)
            {
                EXPECT_FALSE(writer.String({"\xed\xa0\x80", 3}));
            }
            if (invalid == 2)
            {
                EXPECT_FALSE(writer.Float32(std::numeric_limits<float>::quiet_NaN()));
            }
            if (invalid == 3)
            {
                EXPECT_FALSE(writer.Float32(std::numeric_limits<float>::infinity()));
            }
            if (invalid == 4)
            {
                EXPECT_FALSE(writer.Float32(-std::numeric_limits<float>::infinity()));
            }
            EXPECT_EQ(writer.Error(), VFS::VFSError::Corrupted);
            EXPECT_FALSE(writer.String("valid"));
            EXPECT_FALSE(writer.UInt64(1));
            EXPECT_FALSE(writer.Boolean(true));
            EXPECT_EQ(sink.Count, 0u);
            EXPECT_EQ(sink.Calls, 0u);
        }
    }

    TEST(CanonicalJSONWriterTest, InvalidSinkAndNullStorageAreRejectedWithoutCallingTheSink)
    {
        CanonicalJSONWriter invalid(nullptr);
        EXPECT_EQ(invalid.Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(invalid.Raw("null"));
        JSONSink            sink = {};
        CanonicalJSONWriter writer(JSONSink::Write, &sink);
        EXPECT_FALSE(writer.String({nullptr, 1}));
        EXPECT_EQ(writer.Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST(CanonicalJSONWriterTest, SinkErrorsRemainStickyAndNeverPermitFurtherCallbacks)
    {
        for (uint32_t fail_at = 1; fail_at <= 4; ++fail_at)
        {
            JSONSink sink = {};
            sink.FailAt   = fail_at;
            CanonicalJSONWriter writer(JSONSink::Write, &sink);
            EXPECT_FALSE(writer.String("ab\ncd"));
            EXPECT_EQ(writer.Error(), VFS::VFSError::PermissionDenied);
            const auto calls = sink.Calls;
            EXPECT_FALSE(writer.Raw("null"));
            EXPECT_FALSE(writer.Float32(1.f));
            EXPECT_EQ(sink.Calls, calls);
        }
    }
} // namespace ZEngine::Core::Serialization
