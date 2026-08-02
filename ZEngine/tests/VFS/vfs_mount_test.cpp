#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <gtest/gtest.h>
#include <cstdint>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

namespace
{
    VFSPath P(const char* s)
    {
        return VFSPath::Parse(s).Value();
    }

    struct MockBackend : IVFSBackend
    {
        const char*    Name;
        VFSBackendCaps Caps;
        VFSPath        LastOpen;

        explicit MockBackend(const char* name, VFSBackendCaps caps = VFSBackendCaps::Read) : Name(name), Caps(caps) {}

        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags) override
        {
            LastOpen = path;
            return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
        }
        void                   Close(IVFSFile*) override {}
        VFSResult<VFSFileStat> Stat(const VFSPath&) const override
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Unsupported);
        }
        bool Exists(const VFSPath&) const override
        {
            return false;
        }
        VFSResult<Array<VFSDirEntry>> List(ArenaAllocator*, const VFSPath&) const override
        {
            return VFSResult<Array<VFSDirEntry>>::Fail(VFSError::Unsupported);
        }
        cstring BackendType() const override
        {
            return Name;
        }
        VFSBackendCaps Capabilities() const override
        {
            return Caps;
        }
    };
} // namespace

class VFSMountTableTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_manager.Initialize(ZKilo(256), {});
        m_table.Initialize(&m_manager.MainArena, 8);
    }
    void TearDown() override
    {
        m_manager.Shutdown();
    }

    MemoryManager m_manager{};
    VFSMountTable m_table;
};

TEST_F(VFSMountTableTest, MountAndResolveSingle)
{
    MockBackend assets("assets");
    ASSERT_TRUE(m_table.Mount(&assets, P("/assets"), 0).Succeeded());
    EXPECT_EQ(m_table.Count(), 1u);

    VFSResult<ResolveResult> r = m_table.Resolve(P("/assets/textures/foo.png"));
    ASSERT_TRUE(r.Succeeded());
    EXPECT_EQ(r.Value().Backend, &assets);
    EXPECT_TRUE(r.Value().RelativePath == P("/textures/foo.png"));
}

TEST_F(VFSMountTableTest, ResolveExactMatchGivesRoot)
{
    MockBackend assets("assets");
    ASSERT_TRUE(m_table.Mount(&assets, P("/assets"), 0).Succeeded());

    VFSResult<ResolveResult> r = m_table.Resolve(P("/assets"));
    ASSERT_TRUE(r.Succeeded());
    EXPECT_TRUE(r.Value().RelativePath.IsRoot());
}

TEST_F(VFSMountTableTest, PrefixBoundaryDoesNotFalseMatch)
{
    MockBackend assets("assets");
    ASSERT_TRUE(m_table.Mount(&assets, P("/assets"), 0).Succeeded());

    EXPECT_TRUE(m_table.Resolve(P("/assetstore/secret.png")).Failed());
}

TEST_F(VFSMountTableTest, ResolveNoMountFails)
{
    EXPECT_EQ(m_table.Resolve(P("/nothing/here")).Error(), VFSError::NotFound);
}

TEST_F(VFSMountTableTest, DuplicateMountRejected)
{
    MockBackend a("a");
    MockBackend b("b");
    ASSERT_TRUE(m_table.Mount(&a, P("/x"), 0).Succeeded());
    EXPECT_EQ(m_table.Mount(&b, P("/x"), 0).Error(), VFSError::AlreadyExists);
}

TEST_F(VFSMountTableTest, HigherPriorityWinsRegardlessOfInsertionOrder)
{
    MockBackend base("base");
    MockBackend overlay("overlay");

    ASSERT_TRUE(m_table.Mount(&overlay, P("/assets"), 10).Succeeded());
    ASSERT_TRUE(m_table.Mount(&base, VFSPath::Root(), 0).Succeeded());

    VFSResult<ResolveResult> r = m_table.Resolve(P("/assets/x.png"));
    ASSERT_TRUE(r.Succeeded());
    EXPECT_EQ(r.Value().Backend, &overlay);
}

TEST_F(VFSMountTableTest, ResolveAllReturnsAllMatchesInPriorityOrder)
{
    MockBackend base("base");
    MockBackend overlay("overlay");
    ASSERT_TRUE(m_table.Mount(&base, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(m_table.Mount(&overlay, P("/assets"), 10).Succeeded());

    Array<ResolveResult> out;
    out.init(&m_manager.MainArena, 4);

    ASSERT_TRUE(m_table.ResolveAll(P("/assets/x.png"), out).Succeeded());
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].Backend, &overlay); // higher priority first
    EXPECT_EQ(out[1].Backend, &base);
    EXPECT_TRUE(out[0].RelativePath == P("/x.png"));
    EXPECT_TRUE(out[1].RelativePath == P("/assets/x.png"));
}

TEST_F(VFSMountTableTest, UnmountRemovesMount)
{
    MockBackend a("a");
    ASSERT_TRUE(m_table.Mount(&a, P("/x"), 0).Succeeded());
    ASSERT_EQ(m_table.Count(), 1u);

    ASSERT_TRUE(m_table.Unmount(P("/x")).Succeeded());
    EXPECT_EQ(m_table.Count(), 0u);
    EXPECT_TRUE(m_table.Resolve(P("/x/y")).Failed());
}

TEST_F(VFSMountTableTest, UnmountMissingFails)
{
    EXPECT_EQ(m_table.Unmount(P("/nope")).Error(), VFSError::NotFound);
}

class VFSContextTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_manager.Initialize(ZKilo(256), {});
        m_ctx.Initialize(&m_manager.MainArena, 8);
    }
    void TearDown() override
    {
        m_manager.Shutdown();
    }

    MemoryManager m_manager{};
    VFSContext    m_ctx;
};

TEST_F(VFSContextTest, OpenForwardsStrippedPathToBackend)
{
    MockBackend assets("assets");
    ASSERT_TRUE(m_ctx.Mount(&assets, P("/assets"), 0).Succeeded());

    (void) m_ctx.Open(P("/assets/textures/foo.png"), VFSOpenFlags::Read);
    EXPECT_TRUE(assets.LastOpen == P("/textures/foo.png"));
}

TEST_F(VFSContextTest, OpenUnmountedPathFailsNotFound)
{
    VFSResult<IVFSFile*> r = m_ctx.Open(P("/unmounted/file"), VFSOpenFlags::Read);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::NotFound);
}

TEST_F(VFSContextTest, WriteOpsRequireWritableBackend)
{
    MockBackend read_only("ro", VFSBackendCaps::Read);
    ASSERT_TRUE(m_ctx.Mount(&read_only, P("/ro"), 0).Succeeded());

    EXPECT_EQ(m_ctx.CreateDir(P("/ro/newdir")).Error(), VFSError::PermissionDenied);
}
