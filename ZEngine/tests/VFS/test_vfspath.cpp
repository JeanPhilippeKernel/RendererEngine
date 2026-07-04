#include <Core/VFS/VFSPath.h>
#include <gtest/gtest.h>
#include <string>

using namespace ZEngine::Core::VFS;

static VFSPath MustParse(const char* raw)
{
    VFSResult<VFSPath> r = VFSPath::Parse(raw);
    EXPECT_TRUE(r.Succeeded()) << "expected Parse(\"" << raw << "\") to succeed";
    return r.Value();
}

TEST(VFSPathParse, AlreadyClean)
{
    EXPECT_STREQ(MustParse("/textures/rock.png").CStr(), "/textures/rock.png");
}

TEST(VFSPathParse, AddsLeadingSlash)
{
    EXPECT_STREQ(MustParse("textures/rock.png").CStr(), "/textures/rock.png");
}

TEST(VFSPathParse, ResolvesDot)
{
    EXPECT_STREQ(MustParse("./textures/rock.png").CStr(), "/textures/rock.png");
}

TEST(VFSPathParse, ResolvesDotDot)
{
    EXPECT_STREQ(MustParse("textures/../rock.png").CStr(), "/rock.png");
}

TEST(VFSPathParse, CollapsesSlashes)
{
    EXPECT_STREQ(MustParse("//textures///rock.png").CStr(), "/textures/rock.png");
}

TEST(VFSPathParse, StripsTrailingSlash)
{
    EXPECT_STREQ(MustParse("/textures/rock.png/").CStr(), "/textures/rock.png");
}

TEST(VFSPathParse, Root)
{
    VFSPath p = MustParse("/");
    EXPECT_TRUE(p.IsRoot());
    EXPECT_EQ(p.ComponentCount(), 0u);
}

TEST(VFSPathParse, EmptyFails)
{
    VFSResult<VFSPath> r = VFSPath::Parse("");
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::InvalidPath);
}

TEST(VFSPathParse, RootEscapeFails)
{
    VFSResult<VFSPath> r = VFSPath::Parse("../../escape");
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::InvalidPath);
}

TEST(VFSPathParse, TooLongFails)
{
    std::string        huge(300, 'a');
    VFSResult<VFSPath> r = VFSPath::Parse(huge.c_str());
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::InvalidPath);
}

TEST(VFSPathParse, NullFails)
{
    VFSResult<VFSPath> r = VFSPath::Parse(nullptr);
    EXPECT_TRUE(r.Failed());
}

TEST(VFSPathFromNative, StripsWindowsDrive)
{
    EXPECT_STREQ(VFSPath::FromNative("C:\\Assets\\rock.png").Value().CStr(), "/Assets/rock.png");
}

TEST(VFSPathFromNative, PosixUnchanged)
{
    EXPECT_STREQ(VFSPath::FromNative("/usr/share/assets").Value().CStr(), "/usr/share/assets");
}

TEST(VFSPathAccessors, Filename)
{
    VFSPath p = MustParse("/textures/rock.png");
    EXPECT_TRUE(p.Filename().Equals("rock.png"));
    EXPECT_EQ(p.Filename().Length, 8u);
}

TEST(VFSPathAccessors, Stem)
{
    EXPECT_TRUE(MustParse("/textures/rock.png").Stem().Equals("rock"));
}

TEST(VFSPathAccessors, Extension)
{
    EXPECT_TRUE(MustParse("/textures/rock.png").Extension().Equals(".png"));
}

TEST(VFSPathAccessors, ExtensionMissing)
{
    EXPECT_TRUE(MustParse("/textures/rock").Extension().Empty());
}

TEST(VFSPathAccessors, Parent)
{
    EXPECT_STREQ(MustParse("/textures/rock.png").Parent().CStr(), "/textures");
}

TEST(VFSPathAccessors, ParentOfTopLevelIsRoot)
{
    EXPECT_TRUE(MustParse("/textures").Parent().IsRoot());
}

TEST(VFSPathAccessors, Components)
{
    VFSPath p = MustParse("/textures/rock.png");
    EXPECT_EQ(p.ComponentCount(), 2u);
    EXPECT_TRUE(p.ComponentAt(0).Equals("textures"));
    EXPECT_TRUE(p.ComponentAt(1).Equals("rock.png"));
}

TEST(VFSPathAccessors, ComponentOutOfRangeIsEmpty)
{
    EXPECT_TRUE(MustParse("/textures/rock.png").ComponentAt(9).Empty());
}

TEST(VFSPathAccessors, DefaultIsInvalid)
{
    VFSPath p;
    EXPECT_FALSE(p.IsValid());
}

TEST(VFSPathRoot, IsRoot)
{
    EXPECT_TRUE(VFSPath::Root().IsRoot());
}

TEST(VFSPathRoot, ParentOfRootIsRoot)
{
    EXPECT_TRUE(VFSPath::Root().Parent().IsRoot());
}

TEST(VFSPathRoot, ZeroComponents)
{
    EXPECT_EQ(VFSPath::Root().ComponentCount(), 0u);
}

TEST(VFSPathCompose, Append)
{
    VFSResult<VFSPath> r = MustParse("/textures").Append("rock.png");
    ASSERT_TRUE(r.Succeeded());
    EXPECT_STREQ(r.Value().CStr(), "/textures/rock.png");
}

TEST(VFSPathCompose, AppendOntoRoot)
{
    VFSResult<VFSPath> r = VFSPath::Root().Append("rock.png");
    ASSERT_TRUE(r.Succeeded());
    EXPECT_STREQ(r.Value().CStr(), "/rock.png");
}

TEST(VFSPathCompose, AppendResolvesDotDot)
{
    VFSResult<VFSPath> r = MustParse("/a/b").Append("../c");
    ASSERT_TRUE(r.Succeeded());
    EXPECT_STREQ(r.Value().CStr(), "/a/c");
}

TEST(VFSPathCompose, OperatorSlash)
{
    EXPECT_STREQ((MustParse("/textures") / "rock.png").CStr(), "/textures/rock.png");
}

TEST(VFSPathCompose, AppendTooLongFails)
{
    VFSPath            base = MustParse(("/" + std::string(200, 'a')).c_str());
    VFSResult<VFSPath> r    = base.Append(std::string(200, 'b').c_str());
    EXPECT_TRUE(r.Failed());
}

TEST(VFSPathCompare, Equal)
{
    EXPECT_TRUE(MustParse("/a/b") == MustParse("/a/b"));
}

TEST(VFSPathCompare, NotEqual)
{
    EXPECT_TRUE(MustParse("/a/b") != MustParse("/a/c"));
}

TEST(VFSPathCompare, LessThan)
{
    EXPECT_TRUE(MustParse("/a/b") < MustParse("/a/c"));
}

TEST(VFSPathPrefix, ComponentBoundary)
{
    EXPECT_TRUE(MustParse("/textures").IsPrefixOf(MustParse("/textures/rock.png")));
}

TEST(VFSPathPrefix, EqualPathsArePrefix)
{
    EXPECT_TRUE(MustParse("/textures").IsPrefixOf(MustParse("/textures")));
}

TEST(VFSPathPrefix, MidComponentIsNotPrefix)
{
    EXPECT_FALSE(MustParse("/tex").IsPrefixOf(MustParse("/textures/rock.png")));
}

TEST(VFSPathPrefix, RootIsPrefixOfAll)
{
    EXPECT_TRUE(VFSPath::Root().IsPrefixOf(MustParse("/anything")));
}

TEST(VFSPathHash, EqualPathsEqualHash)
{
    EXPECT_EQ(MustParse("/textures/rock.png").Hash(), MustParse("/textures/rock.png").Hash());
}

TEST(VFSPathHash, DifferentPathsDifferentHash)
{
    EXPECT_NE(MustParse("/textures/rock.png").Hash(), MustParse("/textures/wood.png").Hash());
}

TEST(VFSPathHash, EqualAfterNormalization)
{
    EXPECT_EQ(MustParse("//a///b/").Hash(), MustParse("/a/b").Hash());
}

TEST(VFSPathToNative, UsesPlatformSeparator)
{
    char buffer[VFS_MAX_PATH];
    MustParse("/a/b").ToNative(buffer, sizeof(buffer));

    std::string expected = "/a/b";
    for (char& c : expected)
    {
        if (c == '/')
        {
            c = PLATFORM_OS_BACKSLASH;
        }
    }
    EXPECT_STREQ(buffer, expected.c_str());
}
