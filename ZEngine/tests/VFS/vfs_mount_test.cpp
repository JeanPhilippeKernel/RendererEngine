#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>

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
        const char*              Name          = nullptr;
        VFSBackendCaps           Caps          = VFSBackendCaps::None;
        VFSPath                  LastOpen      = {};
        uint32_t                 MutationCalls = 0;
        std::vector<VFSDirEntry> Entries;

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
        VFSResult<void> CreateDir(const VFSPath&) override
        {
            ++MutationCalls;
            return VFSResult<void>::Ok();
        }
        VFSResult<void> Remove(const VFSPath&) override
        {
            ++MutationCalls;
            return VFSResult<void>::Ok();
        }
        VFSResult<void> RemoveAll(const VFSPath&) override
        {
            ++MutationCalls;
            return VFSResult<void>::Ok();
        }
        VFSResult<void> Rename(const VFSPath&, const VFSPath&) override
        {
            ++MutationCalls;
            return VFSResult<void>::Ok();
        }
        VFSResult<Array<VFSDirEntry>> List(ArenaAllocator* arena, const VFSPath&) const override
        {
            Array<VFSDirEntry> result;
            result.init(arena, Entries.size() + 1);
            for (const auto& entry : Entries)
                result.push(entry);
            return VFSResult<Array<VFSDirEntry>>::Ok(std::move(result));
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
    EXPECT_EQ(m_ctx.CreateDirExclusive(P("/ro/newdir")).Error(), VFSError::PermissionDenied);
}

TEST_F(VFSContextTest, ExclusiveDirectoryCreationFailsClosedForUnsupportedBackends)
{
    MockBackend backend("base", VFSBackendCaps::Read | VFSBackendCaps::Write);
    ASSERT_TRUE(m_ctx.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    EXPECT_EQ(m_ctx.CreateDirExclusive(P("/newdir")).Error(), VFSError::Unsupported);
    EXPECT_EQ(m_ctx.CreateDirExclusive(VFSPath{}).Error(), VFSError::InvalidPath);
    EXPECT_EQ(backend.MutationCalls, 0u);
}

TEST_F(VFSContextTest, NoReplaceRenameFailsClosedForReadOnlyCrossMountAndUnsupportedBackends)
{
    MockBackend base("base", VFSBackendCaps::Read | VFSBackendCaps::Write);
    MockBackend read_only("read-only", VFSBackendCaps::Read);
    ASSERT_TRUE(m_ctx.Mount(&base, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(m_ctx.Mount(&read_only, P("/readonly"), 10).Succeeded());
    EXPECT_EQ(m_ctx.RenameNoReplace(P("/file"), P("/other")).Error(), VFSError::Unsupported);
    EXPECT_EQ(m_ctx.RenameNoReplace(P("/readonly/file"), P("/readonly/other")).Error(), VFSError::PermissionDenied);
    EXPECT_EQ(m_ctx.RenameNoReplace(P("/file"), P("/readonly/file")).Error(), VFSError::Unsupported);
    EXPECT_EQ(m_ctx.RenameNoReplace(VFSPath::Root(), P("/other")).Error(), VFSError::InvalidPath);
    EXPECT_EQ(m_ctx.RenameNoReplace(P("/file"), P("/file/child")).Error(), VFSError::InvalidPath);
}

TEST_F(VFSContextTest, MutationsNeverFallThroughAReadOnlyOwningMount)
{
    MockBackend base("base", VFSBackendCaps::Read | VFSBackendCaps::Write);
    MockBackend read_only("read-only", VFSBackendCaps::Read);
    // Specificity, not priority, determines the mount the user sees.
    ASSERT_TRUE(m_ctx.Mount(&base, VFSPath::Root(), 100).Succeeded());
    ASSERT_TRUE(m_ctx.Mount(&read_only, P("/readonly"), 0).Succeeded());
    const auto arena_bytes = m_manager.MainArena.m_current_offset;
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        EXPECT_EQ(m_ctx.CreateDir(P("/readonly/new")).Error(), VFSError::PermissionDenied);
        EXPECT_EQ(m_ctx.CreateDirExclusive(P("/readonly/new")).Error(), VFSError::PermissionDenied);
        EXPECT_EQ(m_ctx.Remove(P("/readonly/file")).Error(), VFSError::PermissionDenied);
        EXPECT_EQ(m_ctx.RemoveAll(P("/readonly/folder")).Error(), VFSError::PermissionDenied);
        EXPECT_EQ(m_ctx.Rename(P("/readonly/file"), P("/other")).Error(), VFSError::PermissionDenied);
        EXPECT_EQ(m_ctx.Rename(P("/file"), P("/readonly/other")).Error(), VFSError::PermissionDenied);
        EXPECT_EQ(m_manager.MainArena.m_current_offset, arena_bytes);
    }
    EXPECT_EQ(base.MutationCalls, 0u);
    EXPECT_EQ(read_only.MutationCalls, 0u);
    EXPECT_EQ(m_ctx.Remove(VFSPath{}).Error(), VFSError::InvalidPath);
    EXPECT_TRUE(m_ctx.CreateDir(P("/writable")).Succeeded());
    EXPECT_TRUE(m_ctx.Remove(P("/writable/file")).Succeeded());
    EXPECT_TRUE(m_ctx.RemoveAll(P("/writable/folder")).Succeeded());
    EXPECT_TRUE(m_ctx.Rename(P("/writable/file"), P("/other")).Succeeded());
    EXPECT_EQ(base.MutationCalls, 4u);
}

TEST(VFSListingTest, DeduplicationPreservesPriorityOrderAndWinningEntryMetadata)
{
    MemoryManager memory;
    memory.Initialize(ZMega(16), {});
    VFSContext vfs;
    vfs.Initialize(&memory.MainArena, 8);
    ArenaAllocator output;
    output.Initialize(ZMega(8), 0);
    MockBackend base("base"), overlay("overlay");
    VFSDirEntry winner = {};
    winner.Path        = P("/shared");
    winner.IsDirectory = true;
    overlay.Entries    = {
        winner, {{}, P("/overlay-only"), false},
         winner
    };
    base.Entries = {
        {{},    P("/assets/shared"), false},
        {{}, P("/assets/base-only"), false},
        {{},    P("/assets/Shared"), false}
    };
    ASSERT_TRUE(vfs.Mount(&base, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(vfs.Mount(&overlay, P("/assets"), 10).Succeeded());
    const size_t before = memory.MainArena.m_current_offset;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        output.Clear();
        auto result = vfs.List(P("/assets"), &output);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.Value().size(), 4u);
        EXPECT_TRUE(result.Value()[0].Path == P("/assets/shared"));
        EXPECT_TRUE(result.Value()[0].IsDirectory);
        EXPECT_TRUE(result.Value()[1].Path == P("/assets/overlay-only"));
        EXPECT_TRUE(result.Value()[2].Path == P("/assets/base-only"));
        EXPECT_TRUE(result.Value()[3].Path == P("/assets/Shared"));
        EXPECT_EQ(memory.MainArena.m_current_offset, before);
    }
}

TEST(VFSListingTest, LargeDuplicateListingSurvivesHashAndOutputGrowth)
{
    MemoryManager memory;
    memory.Initialize(ZMega(64), {});
    VFSContext vfs;
    vfs.Initialize(&memory.MainArena, 8);
    ArenaAllocator output;
    output.Initialize(ZMega(32), 0);
    MockBackend backend("large");
    for (int i = 0; i < 4096; ++i)
    {
        const std::string path = "/Asset_" + std::to_string(i);
        backend.Entries.push_back({{}, P(path.c_str()), false});
    }
    // Keys must survive output growth; probing collisions compare full strings.
    for (int i = 0; i < 4096; ++i)
        backend.Entries.push_back(backend.Entries[i]);
    ASSERT_TRUE(vfs.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    auto result = vfs.List(VFSPath::Root(), &output);
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.Value().size(), 4096u);
    for (size_t i = 0; i < result.Value().size(); ++i)
        EXPECT_TRUE(result.Value()[i].Path == backend.Entries[i].Path);
}
