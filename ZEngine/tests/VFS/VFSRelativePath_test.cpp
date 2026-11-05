#include <ZEngine/Core/VFS/VFSRelativePath.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <type_traits>

using ZEngine::Core::VFS::VFSRelativePath;

namespace
{
    namespace VFS     = ZEngine::Core::VFS;
    namespace Helpers = ZEngine::Helpers;

    static_assert(std::is_trivially_copyable_v<VFSRelativePath>);
    static_assert(sizeof(VFSRelativePath) <= VFS::VFS_MAX_PATH + 8);

    TEST(VFSRelativePathTest, DefaultIsInvalidAndOwnsNoStorageOutsideTheValue)
    {
        const VFSRelativePath path = {};
        EXPECT_FALSE(path.IsValid());
        EXPECT_EQ(path.Length(), 0u);
        EXPECT_EQ(path.ComponentCount(), 0u);
        EXPECT_STREQ(path.CStr(), "");
        EXPECT_TRUE(path.View().empty());
        EXPECT_TRUE(path.Parent().Failed());
        EXPECT_TRUE(path.Append("file").Failed());
        EXPECT_TRUE(path.Sibling("file").Failed());
    }

    TEST(VFSRelativePathTest, PreservesExactSpellingAndOwnsItsBytes)
    {
        char       raw[]  = "objects/Texture.PNG";
        const auto parsed = VFSRelativePath::Parse(raw);
        ASSERT_TRUE(parsed.Succeeded());
        raw[0] = 'X';
        EXPECT_STREQ(parsed.Value().CStr(), "objects/Texture.PNG");
        EXPECT_EQ(parsed.Value().Length(), sizeof(raw) - 1);
        EXPECT_EQ(parsed.Value().ComponentCount(), 2u);
        EXPECT_EQ(parsed.Value().View().data(), parsed.Value().CStr());
        EXPECT_EQ(parsed.Value().View().size(), parsed.Value().Length());
    }

    TEST(VFSRelativePathTest, RejectsRawAliasesInsteadOfNormalizingThem)
    {
        constexpr cstring invalid[] = {"", "/", "/manifest.json", "//server/file", "\\\\server\\file", "C:/file", "C:\\file", "objects\\file", "objects//file", "objects/./file", "objects/../manifest.json", "../file", "./file", ".", "..", "objects/", "a:b", "file\n", "file\r\n", "file\t", "a/\x7f"};
        for (cstring raw : invalid)
        {
            SCOPED_TRACE(raw);
            const auto parsed = VFSRelativePath::Parse(raw);
            EXPECT_EQ(parsed.Error(), VFS::VFSError::InvalidPath);
            EXPECT_EQ(VFSRelativePath::Validate(raw).Error(), parsed.Error());
        }
    }

    TEST(VFSRelativePathTest, ParseIntoMatchesOwnedParseAndClearsReusedTrailingStorage)
    {
        VFSRelativePath output                     = {};
        char            longest[VFS::VFS_MAX_PATH] = {};
        std::fill(longest, longest + sizeof(longest) - 1, 'a');
        ASSERT_TRUE(VFSRelativePath::ParseInto({longest, sizeof(longest) - 1}, &output).Succeeded());
        constexpr cstring names[] = {"objects/Texture.PNG", "scenes/\xc3\xa9.yaml", "a/b", "x"};
        for (cstring name : names)
        {
            const auto expected = VFSRelativePath::Parse(name);
            ASSERT_TRUE(expected.Succeeded());
            ASSERT_TRUE(VFSRelativePath::ParseInto({name, Helpers::secure_strlen(name)}, &output).Succeeded());
            EXPECT_EQ(output, expected.Value());
            EXPECT_EQ(output.ComponentCount(), expected.Value().ComponentCount());
            for (size_t i = output.Length(); i < VFS::VFS_MAX_PATH; ++i)
            {
                EXPECT_EQ(output.CStr()[i], '\0') << i;
            }
        }
    }

    TEST(VFSRelativePathTest, ParseIntoSupportsSelfAndOverlappingSuffixWithoutTemporaryPath)
    {
        VFSRelativePath output = {};
        ASSERT_TRUE(VFSRelativePath::ParseInto({"objects/scenes/\xc3\xa9.yaml", 22}, &output).Succeeded());
        const auto original = output;
        ASSERT_TRUE(VFSRelativePath::ParseInto(output.View(), &output).Succeeded());
        EXPECT_EQ(output, original);
        ASSERT_TRUE(VFSRelativePath::ParseInto({output.CStr() + 8, output.Length() - 8}, &output).Succeeded());
        EXPECT_STREQ(output.CStr(), "scenes/\xc3\xa9.yaml");
        EXPECT_EQ(output.ComponentCount(), 2u);
        ASSERT_TRUE(VFSRelativePath::ParseInto({output.CStr(), 6}, &output).Succeeded());
        EXPECT_STREQ(output.CStr(), "scenes");
        EXPECT_EQ(output.ComponentCount(), 1u);
        EXPECT_EQ(output.CStr()[7], '\0');
    }

    TEST(VFSRelativePathTest, ParseIntoRejectsInvalidInputWithoutChangingAnyDestinationBytes)
    {
        VFSRelativePath output = {};
        ASSERT_TRUE(VFSRelativePath::ParseInto({"objects/retained", 16}, &output).Succeeded());
        const auto previous                    = output;
        const char embedded[]                  = {'a', '\0', 'b'};
        const char malformed[]                 = {'a', '/', '\xc3'};
        char       overlong[VFS::VFS_MAX_PATH] = {};
        std::fill(overlong, overlong + sizeof(overlong), 'a');
        const ZEngine::Core::Containers::StringView invalid[] = {
            {},
            {nullptr, 4},
            {embedded, sizeof(embedded)},
            {malformed, sizeof(malformed)},
            {overlong, sizeof(overlong)},
            {"a/../b", 6},
            {"a//b", 4},
            {output.CStr() + 7, output.Length() - 7}
        };
        for (const auto text : invalid)
        {
            EXPECT_EQ(VFSRelativePath::ParseInto(text, &output).Error(), VFS::VFSError::InvalidPath);
            EXPECT_EQ(Helpers::secure_memcmp(&output, sizeof(output), &previous, sizeof(previous), sizeof(output)), 0);
        }
        EXPECT_EQ(VFSRelativePath::ParseInto(previous.View(), nullptr).Error(), VFS::VFSError::InvalidPath);
    }

    TEST(VFSRelativePathTest, RejectsNullAndEmbeddedNulWithoutReadingBeyondTheView)
    {
        EXPECT_TRUE(VFSRelativePath::Parse(nullptr).Failed());
        EXPECT_TRUE(VFSRelativePath::Parse(nullptr, 4).Failed());
        const char embedded[] = {'a', '\0', 'b'};
        EXPECT_TRUE(VFSRelativePath::Parse(embedded, sizeof(embedded)).Failed());
        const char bounded[] = {'a', '/', 'b', '\\'};
        const auto parsed    = VFSRelativePath::Parse({bounded, 3});
        ASSERT_TRUE(parsed.Succeeded());
        EXPECT_STREQ(parsed.Value().CStr(), "a/b");
        EXPECT_TRUE(VFSRelativePath::Parse(bounded, sizeof(bounded)).Failed());
    }

    TEST(VFSRelativePathTest, PreservesUnicodeIncludingDistinctNormalizationForms)
    {
        constexpr cstring names[] = {"scenes/\xc3\xa9.yaml", "scenes/e\xcc\x81.yaml", "\xe6\x97\xa5\xe6\x9c\xac/\xf0\x9f\x8c\x8d.png", "objects/%2e%2e.png", "a/.../b", "a/\xc2\xa0"};
        for (cstring name : names)
        {
            const auto parsed = VFSRelativePath::Parse(name);
            ASSERT_TRUE(parsed.Succeeded()) << name;
            EXPECT_STREQ(parsed.Value().CStr(), name);
            EXPECT_TRUE(VFSRelativePath::Validate(name).Succeeded());
            EXPECT_TRUE(VFSRelativePath::Validate(parsed.Value().View()).Succeeded());
        }
        EXPECT_NE(VFSRelativePath::Parse(names[0]).Value(), VFSRelativePath::Parse(names[1]).Value());
    }

    TEST(VFSRelativePathTest, RejectsMalformedUtf8AndUnicodeControls)
    {
        constexpr cstring invalid[] = {"a/\x80", "a/\xc0\xaf", "a/\xc1\xbf", "a/\xc2", "a/\xe0\x80\xaf", "a/\xed\xa0\x80", "a/\xed\xbf\xbf", "a/\xf0\x80\x80\xaf", "a/\xf4\x90\x80\x80", "a/\xf5\x80\x80\x80", "a/\xe2\x82", "a/\xe2\x28\xa1", "a/\xc2\x80", "a/\xc2\x9f"};
        for (cstring raw : invalid)
        {
            EXPECT_TRUE(VFSRelativePath::Parse(raw).Failed());
            EXPECT_EQ(VFSRelativePath::Validate(raw).Error(), VFS::VFSError::InvalidPath);
        }
    }

    TEST(VFSRelativePathTest, EnforcesUtf8ByteLengthNotCharacterCount)
    {
        char raw[VFS::VFS_MAX_PATH + 1] = {};
        for (size_t i = 0; i < VFS::VFS_MAX_PATH; ++i)
        {
            raw[i] = 'a';
        }
        const auto maximum = VFSRelativePath::Parse(raw, VFS::VFS_MAX_PATH - 1);
        ASSERT_TRUE(maximum.Succeeded());
        EXPECT_EQ(maximum.Value().Length(), 255u);
        EXPECT_EQ(maximum.Value().CStr()[255], '\0');
        EXPECT_TRUE(VFSRelativePath::Validate(raw, VFS::VFS_MAX_PATH - 1).Succeeded());
        EXPECT_TRUE(VFSRelativePath::Validate(raw, VFS::VFS_MAX_PATH).Failed());
        EXPECT_TRUE(VFSRelativePath::Validate(raw).Failed());
        EXPECT_TRUE(VFSRelativePath::Parse(raw, VFS::VFS_MAX_PATH).Failed());
        EXPECT_TRUE(VFSRelativePath::Parse(raw).Failed());
        raw[253] = '\xc3';
        raw[254] = '\xa9';
        EXPECT_TRUE(VFSRelativePath::Parse(raw, 255).Succeeded());
        EXPECT_TRUE(VFSRelativePath::Validate(raw, 255).Succeeded());
        raw[254] = '\xc3';
        raw[255] = '\xa9';
        EXPECT_TRUE(VFSRelativePath::Parse(raw, 256).Failed());
        EXPECT_TRUE(VFSRelativePath::Validate(raw, 256).Failed());
    }

    TEST(VFSRelativePathTest, AcceptsExactly32ComponentsAndRejects33)
    {
        char raw[66] = {};
        for (size_t i = 0; i < 33; ++i)
        {
            raw[i * 2]     = 'a';
            raw[i * 2 + 1] = '/';
        }
        const auto maximum = VFSRelativePath::Parse(raw, 63);
        ASSERT_TRUE(maximum.Succeeded());
        EXPECT_EQ(maximum.Value().ComponentCount(), 32u);
        EXPECT_TRUE(VFSRelativePath::Validate(raw, 63).Succeeded());
        EXPECT_TRUE(VFSRelativePath::Validate(raw, 65).Failed());
        EXPECT_TRUE(VFSRelativePath::Parse(raw, 65).Failed());
        EXPECT_TRUE(maximum.Value().Append("b").Failed());
        EXPECT_TRUE(maximum.Value().Sibling("b").Succeeded());
    }

    TEST(VFSRelativePathTest, BuildsValidatedParentsChildrenAndSiblings)
    {
        const auto file = VFSRelativePath::Parse("objects/asset.png");
        ASSERT_TRUE(file.Succeeded());
        const auto parent = file.Value().Parent();
        ASSERT_TRUE(parent.Succeeded());
        EXPECT_STREQ(parent.Value().CStr(), "objects");
        EXPECT_TRUE(parent.Value().Parent().Failed());
        const auto child = parent.Value().Append("nested/asset.png");
        ASSERT_TRUE(child.Succeeded());
        EXPECT_STREQ(child.Value().CStr(), "objects/nested/asset.png");
        const auto sibling = file.Value().Sibling(".temporary");
        ASSERT_TRUE(sibling.Succeeded());
        EXPECT_STREQ(sibling.Value().CStr(), "objects/.temporary");
        const auto manifest = VFSRelativePath::Parse("manifest.json");
        ASSERT_TRUE(manifest.Succeeded());
        const auto temporary = manifest.Value().Sibling(".temporary");
        ASSERT_TRUE(temporary.Succeeded());
        EXPECT_STREQ(temporary.Value().CStr(), ".temporary");
        EXPECT_TRUE(file.Value().Sibling("other/file").Failed());
        EXPECT_TRUE(file.Value().Sibling("../file").Failed());
        EXPECT_TRUE(file.Value().Append("/absolute").Failed());
        EXPECT_TRUE(file.Value().Append(VFSRelativePath{}).Failed());
    }

    TEST(VFSRelativePathTest, AppendingChecksTheCompletePathBudget)
    {
        char raw[255] = {};
        for (size_t i = 0; i < 254; ++i)
        {
            raw[i] = 'a';
        }
        const auto prefix = VFSRelativePath::Parse(raw);
        ASSERT_TRUE(prefix.Succeeded());
        EXPECT_TRUE(prefix.Value().Append("b").Failed());
        raw[253]           = '\0';
        const auto shorter = VFSRelativePath::Parse(raw);
        ASSERT_TRUE(shorter.Succeeded());
        EXPECT_EQ(shorter.Value().Append("b").Value().Length(), 255u);
        EXPECT_EQ(prefix.Value().Sibling("b").Value().Length(), 1u);
    }

    TEST(VFSRelativePathTest, ComparesUnsignedUtf8BytesWithoutCaseFolding)
    {
        const auto a       = VFSRelativePath::Parse("a").Value();
        const auto b       = VFSRelativePath::Parse("b").Value();
        const auto longer  = VFSRelativePath::Parse("aa").Value();
        const auto unicode = VFSRelativePath::Parse("\xc3\xa9").Value();
        EXPECT_EQ(a, a);
        EXPECT_NE(a, b);
        EXPECT_LT(a, b);
        EXPECT_LT(a, longer);
        EXPECT_LT(b, unicode);
        EXPECT_NE(a, VFSRelativePath::Parse("A").Value());
    }

    TEST(VFSRelativePathTest, ValidationOnlyMatchesOwningParserForEveryByte)
    {
        for (uint32_t byte = 0; byte <= 255; ++byte)
        {
            SCOPED_TRACE(byte);
            const char text[] = {'a', '/', static_cast<char>(byte), '\0'};
            EXPECT_EQ(VFSRelativePath::Validate(text, 3).Error(), VFSRelativePath::Parse(text, 3).Error());
            EXPECT_EQ(VFSRelativePath::Validate({text, 3}).Error(), VFSRelativePath::Parse(text, 3).Error());
            EXPECT_EQ(VFSRelativePath::Validate(text).Error(), VFSRelativePath::Parse(text).Error());
            EXPECT_EQ(VFSRelativePath::Validate(text + 2, 1).Error(), VFSRelativePath::Parse(text + 2, 1).Error());
        }
    }

    TEST(VFSRelativePathTest, ValidationOnlyChecksBoundedPrefixesWithoutChangingBytes)
    {
        constexpr char text[] = {'a', '/', '\xc3', '\xa9', '/', 'b', '\0', '.', '.', '\\', ':'};
        for (size_t length = 0; length <= sizeof(text); ++length)
        {
            SCOPED_TRACE(length);
            char input[sizeof(text)] = {};
            ASSERT_EQ(ZEngine::Helpers::secure_memcpy(input, sizeof(input), text, sizeof(text)), ZEngine::Helpers::MEMORY_OP_SUCCESS);
            const auto parsed = VFSRelativePath::Parse(input, length);
            EXPECT_EQ(VFSRelativePath::Validate(input, length).Error(), parsed.Error());
            EXPECT_EQ(VFSRelativePath::Validate({input, length}).Error(), parsed.Error());
            EXPECT_EQ(ZEngine::Helpers::secure_memcmp(input, sizeof(input), text, sizeof(text), sizeof(text)), 0);
        }
    }

    TEST(VFSRelativePathTest, ValidationOnlyRejectsInvalidLengthsBeforeReadingBytes)
    {
        EXPECT_EQ(VFSRelativePath::Validate(nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VFSRelativePath::Validate(nullptr, 4).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VFSRelativePath::Validate(ZEngine::Core::Containers::StringView{}).Error(), VFS::VFSError::InvalidPath);
        constexpr char byte = 'a';
        EXPECT_EQ(VFSRelativePath::Validate(&byte, 0).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VFSRelativePath::Validate(&byte, VFS::VFS_MAX_PATH).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VFSRelativePath::Validate(&byte, std::numeric_limits<size_t>::max()).Error(), VFS::VFSError::InvalidPath);
        EXPECT_TRUE(VFSRelativePath::Validate(&byte, 1).Succeeded());
    }
} // namespace
