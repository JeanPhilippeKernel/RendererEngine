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
        Containers::ArrayView<const uint8_t> JSONBytes(cstring text)
        {
            return {reinterpret_cast<const uint8_t*>(text), Helpers::secure_strlen(text)};
        }
    } // namespace

    TEST(JSONReaderTest, ResetReusesContainerStorageAfterPartialReadsFailuresAndLimitChanges)
    {
        JSONReader             reader(JSONBytes("{\"a\":[1]}"));
        char                   text[8] = {};
        Containers::StringView key     = {};
        for (uint32_t repetition = 0; repetition < 64; ++repetition)
        {
            reader.Reset(JSONBytes("{\"a\":[1]}"));
            ASSERT_TRUE(reader.BeginObject());
            ASSERT_TRUE(reader.Member(text, &key));
            ASSERT_TRUE(reader.BeginArray());
            ASSERT_TRUE(reader.Element());
            // Reset while two containers and one value are still active.
            reader.Reset(JSONBytes("true"));
            bool value = false;
            ASSERT_TRUE(reader.Boolean(&value));
            EXPECT_TRUE(value);
            EXPECT_TRUE(reader.Finish());
            reader.Reset(JSONBytes("[[0]]"), {.MaxDepth = 1});
            ASSERT_TRUE(reader.BeginArray());
            ASSERT_TRUE(reader.Element());
            EXPECT_FALSE(reader.BeginArray());
            EXPECT_EQ(reader.Error(), VFS::VFSError::SizeLimitExceeded);
            reader.Reset(JSONBytes("[]"), {.MaxDepth = 0});
            EXPECT_EQ(reader.Error(), VFS::VFSError::InvalidPath);
            reader.Reset(JSONBytes("\"\\ud800\""));
            EXPECT_FALSE(reader.String(text, &key));
            EXPECT_EQ(reader.Error(), VFS::VFSError::Corrupted);
            reader.Reset(JSONBytes("\"ok\""));
            EXPECT_EQ(reader.Offset(), 0u);
            ASSERT_TRUE(reader.String(text, &key));
            EXPECT_EQ(key, Containers::StringView("ok"));
            EXPECT_TRUE(reader.Finish());
        }
    }

    TEST(JSONReaderTest, TokenDispatchDoesNotConsumeValuesOrHideMalformedInput)
    {
        JSONReader reader(JSONBytes(" \n{\"a\": [1,true,null,\"x\",{}]} "));
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Object);
        EXPECT_EQ(reader.Offset(), 0u);
        ASSERT_TRUE(reader.BeginObject());
        char                   buffer[8] = {};
        Containers::StringView key       = {};
        ASSERT_TRUE(reader.Member(buffer, &key));
        const auto offset = reader.Offset();
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Array);
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Array);
        EXPECT_EQ(reader.Offset(), offset);
        ASSERT_TRUE(reader.BeginArray());
        ASSERT_TRUE(reader.Element());
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Number);
        uint64_t number = 0;
        ASSERT_TRUE(reader.UInt64(&number));
        ASSERT_TRUE(reader.Element());
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Boolean);
        bool boolean = false;
        ASSERT_TRUE(reader.Boolean(&boolean));
        EXPECT_TRUE(boolean);
        ASSERT_TRUE(reader.Element());
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Null);
        ASSERT_TRUE(reader.Null());
        ASSERT_TRUE(reader.Element());
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::String);
        ASSERT_TRUE(reader.String(buffer, &key));
        ASSERT_TRUE(reader.Element());
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Object);
        ASSERT_TRUE(reader.BeginObject());
        EXPECT_FALSE(reader.Member(buffer, &key));
        EXPECT_FALSE(reader.Element());
        EXPECT_FALSE(reader.Member(buffer, &key));
        EXPECT_EQ(reader.PeekValueKind(), JSONValueKind::Invalid);
        EXPECT_TRUE(reader.Finish());
        JSONReader invalid(JSONBytes("!"));
        EXPECT_EQ(invalid.PeekValueKind(), JSONValueKind::Invalid);
        EXPECT_FALSE(invalid.Finish());
        JSONReader truncated(JSONBytes("trueX"));
        EXPECT_EQ(truncated.PeekValueKind(), JSONValueKind::Boolean);
        EXPECT_FALSE(truncated.Boolean(&boolean));
        EXPECT_EQ(truncated.PeekValueKind(), JSONValueKind::Invalid);
    }

    TEST(JSONReaderTest, Float32ConversionPreservesFiniteBitPatternsIncludingSubnormalsAndSignedZero)
    {
        struct FloatCase
        {
            cstring  Text = nullptr;
            uint32_t Bits = 0;
        };
        constexpr FloatCase cases[] = {
            {                                            "0", 0x00000000},
            {                                           "-0", 0x80000000},
            {                                          "0.5", 0x3f000000},
            {                   "1.000000059604644775390625", 0x3f800000},
            {                   "1.000000178813934326171875", 0x3f800002},
            {"1.401298464324817070923729583289916131280e-45", 0x00000001},
            {                              "-1.40129846e-45", 0x80000001},
            {  "3.40282346638528859811704183484516925440e38", 0x7f7fffff},
        };
        for (const auto& item : cases)
        {
            SCOPED_TRACE(item.Text);
            JSONReader reader(JSONBytes(item.Text));
            float      value = 123.f;
            ASSERT_TRUE(reader.Float32(&value));
            EXPECT_TRUE(reader.Finish());
            EXPECT_EQ(std::bit_cast<uint32_t>(value), item.Bits);
        }
    }

    TEST(JSONReaderTest, InvalidFloat32ConversionPreservesCallerValueAndLatchesFailure)
    {
        for (cstring text : {"1e9999", "-1e9999", "3.402824e38", "NaN", "Infinity", "null", "true", "\"1\"", "1.0x", "+1"})
        {
            SCOPED_TRACE(text);
            JSONReader reader(JSONBytes(text));
            float      value = 123.f;
            EXPECT_FALSE(reader.Float32(&value));
            EXPECT_EQ(value, 123.f);
            EXPECT_EQ(reader.Error(), VFS::VFSError::Corrupted);
            EXPECT_FALSE(reader.Finish());
        }
        JSONReader reader(JSONBytes("1"));
        EXPECT_FALSE(reader.Float32(nullptr));
        EXPECT_EQ(reader.Error(), VFS::VFSError::InvalidPath);
    }

    TEST(JSONReaderTest, NestedSchemaDirectedReadConsumesEveryValueAndClosingToken)
    {
        JSONReader             reader(JSONBytes(R"({"a":[0,-1,2.5e+3,true,false,null,"x"],"b":{}})"));
        char                   buffer[16] = {};
        Containers::StringView text       = {};
        ASSERT_TRUE(reader.BeginObject());
        ASSERT_TRUE(reader.Member(buffer, &text));
        EXPECT_TRUE(text == "a");
        ASSERT_TRUE(reader.BeginArray());
        ASSERT_TRUE(reader.Element());
        uint64_t unsigned_value = 99;
        ASSERT_TRUE(reader.UInt64(&unsigned_value));
        EXPECT_EQ(unsigned_value, 0u);
        ASSERT_TRUE(reader.Element());
        int64_t signed_value = 0;
        ASSERT_TRUE(reader.Int64(&signed_value));
        EXPECT_EQ(signed_value, -1);
        ASSERT_TRUE(reader.Element());
        ASSERT_TRUE(reader.Number(&text));
        EXPECT_TRUE(text == "2.5e+3");
        bool flag = false;
        ASSERT_TRUE(reader.Element());
        ASSERT_TRUE(reader.Boolean(&flag));
        EXPECT_TRUE(flag);
        ASSERT_TRUE(reader.Element());
        ASSERT_TRUE(reader.Boolean(&flag));
        EXPECT_FALSE(flag);
        ASSERT_TRUE(reader.Element());
        ASSERT_TRUE(reader.Null());
        ASSERT_TRUE(reader.Element());
        ASSERT_TRUE(reader.String(buffer, &text));
        EXPECT_TRUE(text == "x");
        EXPECT_FALSE(reader.Element());
        ASSERT_TRUE(reader.Member(buffer, &text));
        EXPECT_TRUE(text == "b");
        ASSERT_TRUE(reader.BeginObject());
        EXPECT_FALSE(reader.Member(buffer, &text));
        EXPECT_FALSE(reader.Member(buffer, &text));
        EXPECT_TRUE(reader.Finish());
    }

    TEST(JSONReaderTest, UnicodeEscapesAndLengthDelimitedNulAreDecodedExactly)
    {
        struct StringCase
        {
            cstring Input    = nullptr;
            cstring Expected = nullptr;
            size_t  Length   = 0;
        };
        constexpr StringCase cases[] = {
            {                R"("")",                 "", 0},
            {          R"("\u00e9")",         "\xc3\xa9", 2},
            {    R"("\uD834\uDd1E")", "\xf0\x9d\x84\x9e", 4},
            {        R"("a\u0000b")",             "a\0b", 3},
            {R"("\"\\\/\b\f\n\r\t")",  "\"\\/\b\f\n\r\t", 8},
        };
        for (const auto& test : cases)
        {
            SCOPED_TRACE(test.Input);
            JSONReader             reader(JSONBytes(test.Input));
            char                   buffer[16] = {};
            Containers::StringView value      = {};
            ASSERT_TRUE(reader.String(buffer, &value));
            ASSERT_TRUE(reader.Finish());
            ASSERT_EQ(value.size(), test.Length);
            EXPECT_EQ(Helpers::secure_memcmp(value.data(), value.size(), test.Expected, test.Length, test.Length), 0);
            EXPECT_EQ(buffer[value.size()], '\0');
        }
    }

    TEST(JSONReaderTest, InvalidStringsFailClosed)
    {
        constexpr cstring cases[] = {
            R"("\uD800")",
            R"("\uDC00")",
            R"("\uD800\u0041")",
            R"("\uD800 x")",
            R"("\uD800\xDC00")",
            R"("\uXX00")",
            R"("\u000")",
            R"("\v")",
            "\"unterminated",
            "\"line\nbreak\"",
            "\"\xed\xa0\x80\"",
            "\"\xc0\x80\"",
            "\"\xf4\x90\x80\x80\"",
        };
        for (cstring input : cases)
        {
            SCOPED_TRACE(input);
            JSONReader             reader(JSONBytes(input));
            char                   buffer[64] = {};
            Containers::StringView value("unchanged");
            EXPECT_FALSE(reader.String(buffer, &value));
            EXPECT_EQ(reader.Error(), VFS::VFSError::Corrupted);
            EXPECT_TRUE(value == "unchanged");
            EXPECT_FALSE(reader.Finish());
        }
    }

    TEST(JSONReaderTest, NumberGrammarAndIntegerRangesAreStrict)
    {
        constexpr cstring invalid[] = {"", "01", "-01", "+1", ".5", "1.", "-", "1e", "1e+", "1e-", "1x", "0x1", "NaN", "Infinity", "true"};
        for (cstring input : invalid)
        {
            SCOPED_TRACE(input);
            JSONReader             reader(JSONBytes(input));
            Containers::StringView number = {};
            EXPECT_FALSE(reader.Number(&number));
            EXPECT_EQ(reader.Error(), VFS::VFSError::Corrupted);
        }
        JSONReader unsigned_reader(JSONBytes("18446744073709551615"));
        uint64_t   unsigned_value = 0;
        ASSERT_TRUE(unsigned_reader.UInt64(&unsigned_value));
        EXPECT_EQ(unsigned_value, std::numeric_limits<uint64_t>::max());
        EXPECT_TRUE(unsigned_reader.Finish());
        JSONReader signed_reader(JSONBytes("-9223372036854775808"));
        int64_t    signed_value = 0;
        ASSERT_TRUE(signed_reader.Int64(&signed_value));
        EXPECT_EQ(signed_value, std::numeric_limits<int64_t>::min());
        EXPECT_TRUE(signed_reader.Finish());
        constexpr cstring wrong_unsigned[] = {"18446744073709551616", "-1", "-0", "1.0", "1e0", "\"1\"", "null", "false"};
        for (cstring input : wrong_unsigned)
        {
            JSONReader reader(JSONBytes(input));
            unsigned_value = 17;
            EXPECT_FALSE(reader.UInt64(&unsigned_value)) << input;
            EXPECT_EQ(unsigned_value, 17u);
        }
        constexpr cstring wrong_signed[] = {"9223372036854775808", "-9223372036854775809", "1.0", "1e0"};
        for (cstring input : wrong_signed)
        {
            JSONReader reader(JSONBytes(input));
            signed_value = 17;
            EXPECT_FALSE(reader.Int64(&signed_value)) << input;
            EXPECT_EQ(signed_value, 17);
        }
    }

    TEST(JSONReaderTest, TrailingCommasMissingValuesAndExtraRootsFail)
    {
        constexpr cstring objects[] = {R"({"x":null,})", R"({"x" null})", R"({"x":})", R"({"x":null "y":null})", "{}{}", "{}//comment", "{}\f"};
        for (cstring input : objects)
        {
            JSONReader reader(JSONBytes(input));
            ASSERT_TRUE(reader.BeginObject());
            char                   buffer[16] = {};
            Containers::StringView key        = {};
            while (reader.Member(buffer, &key))
                if (!reader.Null())
                    break;
            EXPECT_FALSE(reader.Finish()) << input;
            EXPECT_EQ(reader.Error(), VFS::VFSError::Corrupted);
        }
        constexpr cstring arrays[] = {"[null,]", "[,null]", "[null null]", "[null", "[truefalse]", "[nullnull]", "[][]"};
        for (cstring input : arrays)
        {
            JSONReader reader(JSONBytes(input));
            ASSERT_TRUE(reader.BeginArray());
            while (reader.Element())
                if (!reader.Null())
                    break;
            EXPECT_FALSE(reader.Finish()) << input;
            EXPECT_EQ(reader.Error(), VFS::VFSError::Corrupted);
        }
    }

    TEST(JSONReaderTest, SkippedValuesCannotBeMistakenForAnEmptyObjectOrArray)
    {
        JSONReader object(JSONBytes(R"({"x":null})"));
        ASSERT_TRUE(object.BeginObject());
        char                   buffer[16] = {};
        Containers::StringView key        = {};
        ASSERT_TRUE(object.Member(buffer, &key));
        EXPECT_FALSE(object.Member(buffer, &key));
        EXPECT_FALSE(object.Finish());
        JSONReader array(JSONBytes("[null]"));
        ASSERT_TRUE(array.BeginArray());
        ASSERT_TRUE(array.Element());
        EXPECT_FALSE(array.Element());
        EXPECT_FALSE(array.Finish());
    }

    TEST(JSONReaderTest, DepthAndDecodedByteLimitsAreEnforcedBeforeGrowth)
    {
        char nested[35] = {};
        for (size_t i = 0; i < 17; ++i)
        {
            nested[i]      = '[';
            nested[17 + i] = ']';
        }
        JSONReader reader(JSONBytes(nested));
        for (uint32_t i = 0; i < 16; ++i)
        {
            ASSERT_TRUE(reader.BeginArray());
            ASSERT_TRUE(reader.Element());
        }
        EXPECT_FALSE(reader.BeginArray());
        EXPECT_EQ(reader.Error(), VFS::VFSError::SizeLimitExceeded);
        char                   buffer[8] = {};
        Containers::StringView text      = {};
        JSONReader             exact(JSONBytes(R"("\u00e9")"), {.MaxStringBytes = 2});
        ASSERT_TRUE(exact.String(buffer, &text));
        EXPECT_TRUE(exact.Finish());
        JSONReader limited(JSONBytes(R"("\u00e9")"), {.MaxStringBytes = 1});
        EXPECT_FALSE(limited.String(buffer, &text));
        EXPECT_EQ(limited.Error(), VFS::VFSError::SizeLimitExceeded);
        JSONReader short_buffer(JSONBytes(R"("abc")"));
        char       too_short[3] = {};
        EXPECT_FALSE(short_buffer.String(too_short, &text));
        EXPECT_EQ(short_buffer.Error(), VFS::VFSError::SizeLimitExceeded);
    }
} // namespace ZEngine::Core::Serialization
