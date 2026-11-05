#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <limits>

namespace ZEngine::Core::VFS
{
    namespace
    {
        constexpr cstring Identity    = "11111111-1111-4111-8111-111111111111";
        constexpr cstring MinimalMeta = R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"MeshImporter","settings":[]})";

        VFSResult<void>   ParseMeta(cstring text, StrictMetaFileData* output)
        {
            return MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(text), Helpers::secure_strlen(text)}, output);
        }

        void ExpectBadMeta(cstring text, VFSError error = VFSError::Corrupted)
        {
            StrictMetaFileData output = {};
            output.Data.SourceHash    = 123;
            output.Fields             = 456;
            EXPECT_EQ(ParseMeta(text, &output).Error(), error) << text;
            EXPECT_EQ(output.Data.SourceHash, 123u);
            EXPECT_EQ(output.Fields, 456u);
        }
    } // namespace

    TEST(MetaFileStrictReaderTest, MinimalDocumentOwnsRequiredFieldsAndPreservesOptionalPresence)
    {
        StrictMetaFileData output = {};
        ASSERT_TRUE(ParseMeta(MinimalMeta, &output).Succeeded());
        EXPECT_EQ(uuids::to_string(output.Data.AssetUUID), Identity);
        EXPECT_STREQ(output.Data.ImporterName, "MeshImporter");
        EXPECT_EQ(output.Data.SettingsCount, 0u);
        EXPECT_EQ(output.Data.Status, ImportStatus::Unknown);
        EXPECT_TRUE(output.Has(MetaFileField::UUID));
        EXPECT_TRUE(output.Has(MetaFileField::Importer));
        EXPECT_TRUE(output.Has(MetaFileField::Settings));
        EXPECT_FALSE(output.Has(MetaFileField::SourceHash));
        EXPECT_FALSE(output.Has(MetaFileField::ArtifactPath));
        EXPECT_FALSE(output.Has(MetaFileField::None));
    }

    TEST(MetaFileStrictReaderTest, UnicodeProvenanceAndExactIntegerBoundariesAreOwned)
    {
        constexpr cstring  source = R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Env\u00e9","settings":[{"key":"exposure","value":"1.0"}],"source_path":"C:\\assets\\\uD834\uDD1E.hdr","source_hash":18446744073709551615,"import_time_ns":-9223372036854775808,"artifact_path":"cache/artifact.zenvmap"})";
        StrictMetaFileData output = {};
        ASSERT_TRUE(ParseMeta(source, &output).Succeeded());
        EXPECT_STREQ(output.Data.ImporterName, "Env\xc3\xa9");
        EXPECT_STREQ(output.Data.SourcePath, "C:\\assets\\\xf0\x9d\x84\x9e.hdr");
        EXPECT_EQ(output.Data.SourceHash, std::numeric_limits<uint64_t>::max());
        EXPECT_EQ(output.Data.LastImportTimeNs, std::numeric_limits<int64_t>::min());
        EXPECT_TRUE(output.Has(MetaFileField::SourceHash));
        EXPECT_TRUE(output.Has(MetaFileField::ImportTimeNs));
        ASSERT_EQ(output.Data.SettingsCount, 1u);
        EXPECT_STREQ(output.Data.Settings[0].Key, "exposure");
        EXPECT_STREQ(output.Data.Settings[0].Value, "1.0");
    }

    TEST(MetaFileStrictReaderTest, UnknownDuplicateMissingAndWrongTypedFieldsAreRejected)
    {
        constexpr cstring cases[] = {
            "{}",
            "[]",
            "null",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","settings":[]})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Mesh"})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"","settings":[]})",
            R"({"uuid":null,"importer":"Mesh","settings":[]})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Mesh","settings":{}})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Mesh","settings":[],"unknown":0})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","\u0075uid":"11111111-1111-4111-8111-111111111111","importer":"Mesh","settings":[]})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Mesh","settings":[],"settings":[]})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Mesh","settings":[],})",
            R"({"uuid":"11111111-1111-4111-8111-111111111111","importer":"Mesh\u0000hidden","settings":[]})",
            R"({"uuid\u0000":"11111111-1111-4111-8111-111111111111","importer":"Mesh","settings":[]})",
        };
        for (cstring text : cases)
            ExpectBadMeta(text);
    }

    TEST(MetaFileStrictReaderTest, IdentityMustBeCanonicalAndNonNil)
    {
        constexpr cstring invalid[] = {"00000000-0000-0000-0000-000000000000", "AAAAAAAA-1111-4111-8111-111111111111", "11111111111141118111111111111111", "11111111-1111-4111-8111-11111111111g"};
        for (cstring identity : invalid)
        {
            char text[256] = {};
            ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":[]})", identity), 0);
            ExpectBadMeta(text);
        }
    }

    TEST(MetaFileStrictReaderTest, NumericTypesNeverCoerceOrOverflow)
    {
        constexpr cstring invalid_hash[] = {"-1", "-0", "1.0", "1e0", "18446744073709551616", "\"1\"", "true", "null", "01"};
        for (cstring hash : invalid_hash)
        {
            char text[256] = {};
            ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":[],"source_hash":%s})", Identity, hash), 0);
            ExpectBadMeta(text);
        }
        constexpr cstring invalid_time[] = {"9223372036854775808", "-9223372036854775809", "1.0", "1e0", "\"1\"", "true"};
        for (cstring time : invalid_time)
        {
            char text[256] = {};
            ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":[],"import_time_ns":%s})", Identity, time), 0);
            ExpectBadMeta(text);
        }
    }

    TEST(MetaFileStrictReaderTest, SettingsRequireExactFieldsAndUniqueDecodedKeys)
    {
        constexpr cstring invalid[] = {
            "[null]",
            "[{}]",
            R"([{"key":"a"}])",
            R"([{"key":"a","value":1}])",
            R"([{"key":"a","value":"b","extra":"c"}])",
            R"([{"key":"a","key":"b","value":"c"}])",
            R"([{"key":"a","value":"b"},{"key":"\u0061","value":"c"}])",
            R"([{"key":"a\u0000b","value":"c"}])",
        };
        for (cstring settings : invalid)
        {
            char text[512] = {};
            ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":%s})", Identity, settings), 0);
            ExpectBadMeta(text);
        }
    }

    TEST(MetaFileStrictReaderTest, ExactSettingsCapacityIsAcceptedAndOverflowIsNotTruncated)
    {
        for (uint32_t count : {32u, 33u})
        {
            char   text[4096] = {};
            size_t length     = static_cast<size_t>(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":[)", Identity));
            for (uint32_t i = 0; i < count; ++i)
            {
                const int written = std::snprintf(text + length, sizeof(text) - length, R"(%s{"key":"k%u","value":"v"})", i ? "," : "", i);
                ASSERT_GT(written, 0);
                ASSERT_LT(static_cast<size_t>(written), sizeof(text) - length);
                length += static_cast<size_t>(written);
            }
            ASSERT_GT(std::snprintf(text + length, sizeof(text) - length, "]}"), 0);
            StrictMetaFileData output = {};
            const auto         result = ParseMeta(text, &output);
            if (count == 32)
            {
                ASSERT_TRUE(result.Succeeded());
                EXPECT_EQ(output.Data.SettingsCount, 32u);
                EXPECT_STREQ(output.Data.Settings[31].Key, "k31");
            }
            else
                EXPECT_EQ(result.Error(), VFSError::SizeLimitExceeded);
        }
    }

    TEST(MetaFileStrictReaderTest, FixedStringCapacitiesFailRatherThanTruncate)
    {
        char importer[65] = {};
        ASSERT_EQ(Helpers::secure_memset(importer, 'x', 64, sizeof(importer)), Helpers::MEMORY_OP_SUCCESS);
        char text[512] = {};
        ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"%s","settings":[]})", Identity, importer), 0);
        ExpectBadMeta(text, VFSError::SizeLimitExceeded);
        importer[63] = '\0';
        ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"%s","settings":[]})", Identity, importer), 0);
        StrictMetaFileData output = {};
        ASSERT_TRUE(ParseMeta(text, &output).Succeeded());
        EXPECT_EQ(Helpers::secure_strlen(output.Data.ImporterName), 63u);
        constexpr cstring fields[] = {"source_path", "artifact_path"};
        for (cstring field : fields)
        {
            char path[257] = {};
            ASSERT_EQ(Helpers::secure_memset(path, 'x', 256, sizeof(path)), Helpers::MEMORY_OP_SUCCESS);
            ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":[],"%s":"%s"})", Identity, field, path), 0);
            ExpectBadMeta(text, VFSError::SizeLimitExceeded);
            path[255] = '\0';
            ASSERT_GT(std::snprintf(text, sizeof(text), R"({"uuid":"%s","importer":"Mesh","settings":[],"%s":"%s"})", Identity, field, path), 0);
            EXPECT_TRUE(ParseMeta(text, &output).Succeeded());
        }
    }

    TEST(MetaFileStrictReaderTest, InvalidArgumentsAndFileBudgetAreRejectedWithoutReadingOrChangingOutput)
    {
        StrictMetaFileData output = {};
        output.Fields             = 99;
        EXPECT_EQ(MetaFileIO::ParseStrict({}, nullptr).Error(), VFSError::InvalidPath);
        EXPECT_EQ(MetaFileIO::ParseStrict({nullptr, 1}, &output).Error(), VFSError::InvalidPath);
        const uint8_t byte = 0;
        EXPECT_EQ(MetaFileIO::ParseStrict({&byte, MetaFileIO::MaxFileSize + 1}, &output).Error(), VFSError::SizeLimitExceeded);
        EXPECT_EQ(output.Fields, 99u);
    }
} // namespace ZEngine::Core::VFS
