#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <charconv>
#include <type_traits>

namespace ZEngine::Tests
{
    namespace VFS = Core::VFS;

    static_assert(!std::is_copy_constructible_v<VFS::VFSMemoryBackend>);
    static_assert(!std::is_move_constructible_v<VFS::VFSMemoryBackend>);

    namespace
    {
        VFS::VFSPath MemoryPath(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }

        VFS::VFSRelativePath MemoryRelative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }

        constexpr uint8_t MemoryOld[] = {1, 2, 3};
        constexpr uint8_t MemoryNew[] = {4, 5, 6, 7, 8};

        struct MemoryVisitProbe
        {
            uint64_t                      Seen         = 0;
            VFS::VFSRootEntry             Last         = {};
            VFS::VFSReadRootLease*        ReadRoot     = nullptr;
            VFS::VFSPublicationRootLease* MutationRoot = nullptr;
            VFS::VFSError                 Failure      = VFS::VFSError::OK;

            static VFS::VFSResult<void>   Visit(void* context, const VFS::VFSRootEntry& entry)
            {
                auto& probe = *static_cast<MemoryVisitProbe*>(context);
                ++probe.Seen;
                probe.Last = entry;
                EXPECT_TRUE(entry.Identity.IsValid());
                EXPECT_TRUE(entry.Path.IsValid());
                if (probe.ReadRoot)
                {
                    const auto identity = probe.ReadRoot->Identity(entry.Path);
                    EXPECT_TRUE(identity.Succeeded());
                    if (identity.Succeeded())
                    {
                        EXPECT_EQ(identity.Value(), entry.Identity);
                    }
                    if (!entry.Stat.IsDirectory)
                    {
                        const auto open = probe.ReadRoot->Open(entry.Path);
                        EXPECT_TRUE(open.Succeeded());
                        if (open.Succeeded())
                        {
                            EXPECT_EQ(open.Value()->Identity().Value(), entry.Identity);
                            EXPECT_TRUE(probe.ReadRoot->CloseChecked(open.Value()).Succeeded());
                        }
                    }
                }
                if (probe.MutationRoot)
                {
                    EXPECT_TRUE(probe.MutationRoot->Remove(entry.Path).Succeeded());
                    probe.MutationRoot = nullptr;
                }
                return probe.Failure == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(probe.Failure);
            }
        };

        void ExpectMemoryBytes(VFS::VFSRootLease& root, const VFS::VFSRelativePath& path, Core::Containers::ArrayView<const uint8_t> expected)
        {
            const auto file = root.Open(path, VFS::VFSOpenFlags::Read);
            ASSERT_TRUE(file.Succeeded());
            uint8_t    bytes[32] = {};
            const auto read      = file.Value()->ReadAll({bytes, sizeof(bytes)});
            const auto close     = root.CloseChecked(file.Value());
            ASSERT_TRUE(read.Succeeded());
            ASSERT_TRUE(close.Succeeded());
            ASSERT_EQ(read.Value(), expected.size());
            EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), expected.data(), expected.size(), expected.size()), 0);
        }
    } // namespace

    class VFSMemoryRootLeaseTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager  Memory  = {};
        VFS::VFSMemoryBackend        Backend = {};
        VFS::VFSContext              Context = {};
        VFS::VFSPublicationRootLease Root    = {};

        void                         SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZKilo(512), 64).Succeeded());
            ASSERT_TRUE(Backend.CreateDir(MemoryPath("/output")).Succeeded());
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(Context.AcquirePublicationRoot(MemoryPath("/output"), &Root).Succeeded());
        }

        void TearDown() override
        {
            Root.Reset();
            Context.Shutdown();
        }
    };

    TEST_F(VFSMemoryRootLeaseTest, AtomicPublicationAndNoReplacePreserveOldBytes)
    {
        EXPECT_TRUE(VFS::HasGuarantees(Root.QueryPublicationCapabilities().Guarantees, VFS::VFS_PUBLICATION_GUARANTEES));
        const auto path = MemoryRelative("manifest.json");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryOld).Succeeded());
        EXPECT_EQ(VFS::WriteFileAtomically(Root, path, MemoryNew, VFS::AtomicWriteMode::NoReplace).Error(), VFS::VFSError::AlreadyExists);
        ExpectMemoryBytes(Root, path, MemoryOld);
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryNew).Succeeded());
        ExpectMemoryBytes(Root, path, MemoryNew);
        const auto entries = Backend.List(&Memory.MainArena, MemoryPath("/output"));
        ASSERT_TRUE(entries.Succeeded());
        EXPECT_EQ(entries.Value().size(), 1u);
    }

    TEST_F(VFSMemoryRootLeaseTest, ChildPublicationRootRetainsPhysicalAndMountPinsAfterParentRelease)
    {
        const auto path = MemoryRelative("desktop-vulkan-le64");
        ASSERT_TRUE(Root.CreateDirExclusive(path).Succeeded());
        const auto                   identity = Root.Identity(path).Value();
        VFS::VFSPublicationRootLease child    = {};
        ASSERT_TRUE(Root.AcquireChildRoot(path, &child).Succeeded());
        EXPECT_EQ(child.RootIdentity().Value(), identity);
        EXPECT_EQ(child.QueryPublicationCapabilities().BackendIdentity, Root.QueryPublicationCapabilities().BackendIdentity);
        EXPECT_TRUE(Root.Overlaps(child).Value());
        Root.Reset();
        EXPECT_EQ(Context.Unmount(VFS::VFSPath::Root()).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Context.Mount(&Backend, MemoryPath("/output/desktop-vulkan-le64"), 1).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Backend.Rename(MemoryPath("/output"), MemoryPath("/moved")).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Backend.Remove(MemoryPath("/output/desktop-vulkan-le64")).Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(VFS::WriteFileAtomically(child, MemoryRelative("manifest.json"), MemoryOld).Succeeded());
        ExpectMemoryBytes(child, MemoryRelative("manifest.json"), MemoryOld);
        child.Reset();
        EXPECT_TRUE(Context.Unmount(VFS::VFSPath::Root()).Succeeded());
        EXPECT_TRUE(Backend.Rename(MemoryPath("/output"), MemoryPath("/moved")).Succeeded());
    }

    TEST_F(VFSMemoryRootLeaseTest, ChildAcquisitionNeverCreatesDirectoriesOrReplacesAnExistingLease)
    {
        VFS::VFSPublicationRootLease child = {};
        EXPECT_EQ(Root.AcquireChildRoot(MemoryRelative("missing/target"), &child).Error(), VFS::VFSError::NotFound);
        EXPECT_FALSE(Backend.Exists(MemoryPath("/output/missing")));
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("file"), MemoryOld).Succeeded());
        EXPECT_EQ(Root.AcquireChildRoot(MemoryRelative("file"), &child).Error(), VFS::VFSError::NotADirectory);
        EXPECT_FALSE(child.IsValid());
        ASSERT_TRUE(Root.CreateDirExclusive(MemoryRelative("target")).Succeeded());
        ASSERT_TRUE(Root.AcquireChildRoot(MemoryRelative("target"), &child).Succeeded());
        const auto identity = child.RootIdentity().Value();
        EXPECT_EQ(Root.AcquireChildRoot(MemoryRelative("missing"), &child).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Root.AcquireChildRoot(MemoryRelative("target"), &Root).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(child.RootIdentity().Value(), identity);
    }

    TEST_F(VFSMemoryRootLeaseTest, ChildAcquisitionSurvivesContextShutdownAndProviderRetirement)
    {
        ASSERT_TRUE(Root.CreateDir(MemoryRelative("target/nested")).Succeeded());
        const auto identity = Root.Identity(MemoryRelative("target/nested")).Value();
        Context.Shutdown();
        Backend.RootProvider()->Retire();
        VFS::VFSPublicationRootLease child = {}, nested = {};
        ASSERT_TRUE(Root.AcquireChildRoot(MemoryRelative("target"), &child).Succeeded());
        Root.Reset();
        ASSERT_TRUE(child.AcquireChildRoot(MemoryRelative("nested"), &nested).Succeeded());
        child.Reset();
        EXPECT_EQ(nested.RootIdentity().Value(), identity);
        ASSERT_TRUE(VFS::WriteFileAtomically(nested, MemoryRelative("manifest.json"), MemoryNew).Succeeded());
        ExpectMemoryBytes(nested, MemoryRelative("manifest.json"), MemoryNew);
    }

    TEST(VFSMemoryRootLeaseLifetimeTest, ChildAcquisitionSurvivesBackendAndContextDestruction)
    {
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSPublicationRootLease parent = {}, child = {};
        {
            VFS::VFSMemoryBackend backend = {};
            VFS::VFSContext       context = {};
            ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
            ASSERT_TRUE(backend.CreateDir(MemoryPath("/output/target")).Succeeded());
            context.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(context.AcquirePublicationRoot(MemoryPath("/output"), &parent).Succeeded());
            context.Shutdown();
        }
        ASSERT_TRUE(parent.AcquireChildRoot(MemoryRelative("target"), &child).Succeeded());
        parent.Reset();
        ASSERT_TRUE(VFS::WriteFileAtomically(child, MemoryRelative("manifest.json"), MemoryNew).Succeeded());
        ExpectMemoryBytes(child, MemoryRelative("manifest.json"), MemoryNew);
    }

    TEST_F(VFSMemoryRootLeaseTest, ChildAcquisitionHonorsPinLimitAndReleasesFailedAndSuccessfulSlots)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(MemoryRelative("target")).Succeeded());
        Core::Containers::FixedArray<VFS::VFSPublicationRootLease, 63> children = {};
        for (auto& child : children)
        {
            ASSERT_TRUE(Root.AcquireChildRoot(MemoryRelative("target"), &child).Succeeded());
        }
        VFS::VFSPublicationRootLease overflow = {};
        EXPECT_EQ(Root.AcquireChildRoot(MemoryRelative("target"), &overflow).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_FALSE(overflow.IsValid());
        children[0].Reset();
        EXPECT_EQ(Root.AcquireChildRoot(MemoryRelative("missing"), &overflow).Error(), VFS::VFSError::NotFound);
        ASSERT_TRUE(Root.AcquireChildRoot(MemoryRelative("target"), &overflow).Succeeded());
        for (auto& child : children)
        {
            child.Reset();
        }
        Root.Reset();
        EXPECT_EQ(Context.Unmount(VFS::VFSPath::Root()).Error(), VFS::VFSError::AlreadyExists);
        overflow.Reset();
        EXPECT_TRUE(Context.Unmount(VFS::VFSPath::Root()).Succeeded());
    }

    TEST_F(VFSMemoryRootLeaseTest, ChildReadRootRetainsReadOnlyAuthority)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(MemoryRelative("target")).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("target/asset"), MemoryOld).Succeeded());
        VFS::VFSReadRootLease parent = {}, child = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &parent).Succeeded());
        ASSERT_TRUE(parent.AcquireChildRoot(MemoryRelative("target"), &child).Succeeded());
        parent.Reset();
        const auto file = child.Open(MemoryRelative("asset"));
        ASSERT_TRUE(file.Succeeded());
        EXPECT_TRUE(file.Value()->Write(MemoryNew, 0).Failed());
        ASSERT_TRUE(child.CloseChecked(file.Value()).Succeeded());
        auto authority = Backend.RootProvider()->Acquire(MemoryPath("/output"), VFS::VFSRootAccess::ReadOnly);
        ASSERT_TRUE(authority.Succeeded());
        auto nested = authority.Value()->AcquireChild(MemoryRelative("target"));
        ASSERT_TRUE(nested.Succeeded());
        EXPECT_EQ(nested.Value()->Open(MemoryRelative("asset"), VFS::VFSOpenFlags::Write).Error(), VFS::VFSError::PermissionDenied);
    }

    TEST_F(VFSMemoryRootLeaseTest, StreamingStageSurvivesContextShutdownAndCommitsOnlyAfterInspection)
    {
        const auto directory = MemoryRelative("objects");
        ASSERT_TRUE(Root.CreateDirExclusive(directory).Succeeded());
        VFS::VFSRootAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Root, MemoryRelative("objects/pending")).Succeeded());
        ASSERT_TRUE(writer.Write(MemoryOld).Succeeded());
        const auto temporary = writer.TemporaryPath();
        Context.Shutdown();
        ASSERT_TRUE(writer.Write(MemoryNew).Succeeded());
        EXPECT_EQ(writer.BytesWritten(), sizeof(MemoryOld) + sizeof(MemoryNew));
        ASSERT_TRUE(writer.Seal().Succeeded());
        constexpr uint8_t combined[] = {1, 2, 3, 4, 5, 6, 7, 8};
        ExpectMemoryBytes(Root, temporary, combined);
        const auto final = MemoryRelative("objects/digest.object");
        EXPECT_FALSE(Root.Exists(final).Value());
        EXPECT_EQ(writer.Commit(MemoryRelative("digest.object"), VFS::AtomicWriteMode::NoReplace).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(writer.Commit(final, VFS::AtomicWriteMode::NoReplace).Succeeded());
        ASSERT_TRUE(writer.Reset().Succeeded());
        EXPECT_FALSE(Root.Exists(temporary).Value());
        ExpectMemoryBytes(Root, final, combined);
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootEnumeratesWithExactEntryLimitAndReentrantReads)
    {
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &read).Succeeded());
        uint8_t          buffer[ZKilo(4)] = {};
        MemoryVisitProbe probe            = {.ReadRoot = &read};
        EXPECT_TRUE(read.VisitRoot(0, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("asset"), MemoryOld).Succeeded());
        EXPECT_EQ(read.VisitRoot(0, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Seen, 0u);
        ASSERT_TRUE(read.VisitRoot(1, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 1u);
        EXPECT_EQ(probe.Last.Path, MemoryRelative("asset"));
        EXPECT_EQ(probe.Last.Stat.SizeBytes, sizeof(MemoryOld));
        EXPECT_EQ(probe.Last.Identity, read.Identity(MemoryRelative("asset")).Value());
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootDoesNotExposeWritableAuthorityEvenThroughProvider)
    {
        auto root = Backend.RootProvider()->Acquire(MemoryPath("/output"), VFS::VFSRootAccess::ReadOnly);
        ASSERT_TRUE(root.Succeeded());
        EXPECT_TRUE(VFS::HasGuarantees(root.Value()->Capabilities().Guarantees, VFS::VFS_READ_GUARANTEES));
        const auto path = MemoryRelative("file");
        EXPECT_EQ(root.Value()->Open(path, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(root.Value()->CreateDir(path, false).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(root.Value()->Remove(path).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(root.Value()->Rename(path, path, true).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_FALSE(Backend.Exists(MemoryPath("/output/file")));
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootEnumerationUsesBatchesWithoutSharedCursor)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(MemoryRelative("nested")).Succeeded());
        constexpr uint32_t count = 40;
        for (uint32_t i = 0; i < count; ++i)
        {
            char path[] = "nested/00";
            path[7]     = static_cast<char>('0' + i / 10);
            path[8]     = static_cast<char>('0' + i % 10);
            ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative(path), MemoryOld).Succeeded());
        }
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &read).Succeeded());
        uint8_t                                    storage[ZKilo(4) + 1] = {};
        // Deliberately unaligned: implementations must not cast caller bytes to
        // an aligned VFSRootEntry array.
        const Core::Containers::ArrayView<uint8_t> buffer                = {storage + 1, sizeof(storage) - 1};
        MemoryVisitProbe                           probe                 = {.ReadRoot = &read};
        ASSERT_TRUE(read.VisitDirectory(MemoryRelative("nested"), count, buffer, MemoryVisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, count);
        EXPECT_EQ(probe.Last.Path.ComponentCount(), 2u);
        probe = {};
        ASSERT_TRUE(read.VisitDirectory(MemoryRelative("nested"), count, buffer, MemoryVisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, count);
        probe = {};
        EXPECT_EQ(read.VisitDirectory(MemoryRelative("nested"), count - 1, buffer, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Seen, count - 1);
        probe = {.Failure = VFS::VFSError::Cancelled};
        EXPECT_EQ(read.VisitDirectory(MemoryRelative("nested"), count, buffer, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::Cancelled);
        EXPECT_EQ(probe.Seen, 1u);
    }

    TEST_F(VFSMemoryRootLeaseTest, NamespaceMutationDuringCallbackInvalidatesVisitWithoutDeadlock)
    {
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("asset"), MemoryOld).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &read).Succeeded());
        uint8_t          buffer[ZKilo(4)] = {};
        MemoryVisitProbe probe            = {.ReadRoot = &read, .MutationRoot = &Root};
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(probe.Seen, 1u);
        probe = {};
        EXPECT_TRUE(read.VisitRoot(0, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 0u);
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootSnapshotRetainsIdentityWhilePoolReusesRemovedNodes)
    {
        const auto path = MemoryRelative("asset");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryOld).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &read).Succeeded());
        const auto open = read.Open(path);
        ASSERT_TRUE(open.Succeeded());
        const auto identity = open.Value()->Identity().Value();
        EXPECT_EQ(identity, read.Identity(path).Value());
        for (uint32_t i = 0; i < 128; ++i)
        {
            ASSERT_TRUE(Root.Remove(path).Succeeded());
            ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryNew).Succeeded());
            EXPECT_NE(read.Identity(path).Value(), identity);
        }
        EXPECT_EQ(open.Value()->Identity().Value(), identity);
        uint8_t    bytes[8] = {};
        const auto copied   = open.Value()->ReadAll(bytes);
        EXPECT_TRUE(copied.Succeeded());
        EXPECT_EQ(copied.Value(), sizeof(MemoryOld));
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), MemoryOld, sizeof(MemoryOld), sizeof(MemoryOld)), 0);
        EXPECT_TRUE(read.CloseChecked(open.Value()).Succeeded());
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootRejectsMissingDirectoriesInvalidPathsAndSmallBuffers)
    {
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("file"), MemoryOld).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &read).Succeeded());
        uint8_t          buffer[ZKilo(4)] = {};
        MemoryVisitProbe probe            = {};
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer) - 1}, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer)}, nullptr, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitDirectory({}, 1, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitDirectory(MemoryRelative("missing"), 1, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(read.VisitDirectory(MemoryRelative("file"), 1, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Error(), VFS::VFSError::NotADirectory);
        EXPECT_EQ(probe.Seen, 0u);
        EXPECT_EQ(read.Identity(MemoryRelative("missing")).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(read.Identity({}).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootOverlapUsesStorageIdentityAndRootBoundaries)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(MemoryRelative("child")).Succeeded());
        ASSERT_TRUE(Root.CreateDirExclusive(MemoryRelative("child-other")).Succeeded());
        VFS::VFSReadRootLease parent = {}, child = {}, sibling = {}, whole = {}, separate = {};
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output"), &parent).Succeeded());
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output/child"), &child).Succeeded());
        ASSERT_TRUE(Context.AcquireReadRoot(MemoryPath("/output/child-other"), &sibling).Succeeded());
        ASSERT_TRUE(Context.AcquireReadRoot(VFS::VFSPath::Root(), &whole).Succeeded());
        EXPECT_TRUE(parent.Overlaps(child).Value());
        EXPECT_TRUE(child.Overlaps(parent).Value());
        EXPECT_TRUE(child.Overlaps(Root).Value());
        EXPECT_FALSE(child.Overlaps(sibling).Value());
        EXPECT_TRUE(whole.Overlaps(parent).Value());
        EXPECT_TRUE(whole.RootIdentity().Value().IsValid());
        EXPECT_NE(whole.RootIdentity().Value(), parent.RootIdentity().Value());
        VFS::VFSMemoryBackend other = {};
        ASSERT_TRUE(other.InitializeOwned(ZKilo(256), 16).Succeeded());
        auto direct = other.RootProvider()->Acquire(VFS::VFSPath::Root(), VFS::VFSRootAccess::ReadOnly);
        ASSERT_TRUE(direct.Succeeded());
        auto own = Backend.RootProvider()->Acquire(VFS::VFSPath::Root(), VFS::VFSRootAccess::ReadOnly);
        ASSERT_TRUE(own.Succeeded());
        EXPECT_FALSE(own.Value()->Overlaps(*direct.Value()).Value());
        EXPECT_NE(own.Value()->RootIdentity().Value(), direct.Value()->RootIdentity().Value());
    }

    TEST_F(VFSMemoryRootLeaseTest, ReadRootSurvivesBackendContextAndExternalArenaDestruction)
    {
        VFS::VFSReadRootLease retained = {};
        VFS::VFSFileIdentity  identity = {};
        {
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(2), {});
            VFS::VFSMemoryBackend backend = {};
            ASSERT_TRUE(backend.InitializeOwned(ZKilo(256), 16).Succeeded());
            ASSERT_TRUE(backend.WriteFile(MemoryPath("/asset"), MemoryOld).Succeeded());
            VFS::VFSContext context = {};
            context.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(context.AcquireReadRoot(VFS::VFSPath::Root(), &retained).Succeeded());
            identity = retained.RootIdentity().Value();
            context.Shutdown();
            memory.Shutdown();
        }
        EXPECT_EQ(retained.RootIdentity().Value(), identity);
        uint8_t          buffer[ZKilo(4)] = {};
        MemoryVisitProbe probe            = {.ReadRoot = &retained};
        EXPECT_TRUE(retained.VisitRoot(1, {buffer, sizeof(buffer)}, MemoryVisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 1u);
        EXPECT_EQ(probe.Last.Path, MemoryRelative("asset"));
    }

    TEST_F(VFSMemoryRootLeaseTest, SnapshotSurvivesReplaceAndRemovalWithoutHoldingTreeLock)
    {
        const auto path = MemoryRelative("manifest.json");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryOld).Succeeded());
        const auto file = Root.Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
        ASSERT_TRUE(file.Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryNew).Succeeded());
        ASSERT_TRUE(Root.Remove(path).Succeeded());
        uint8_t    bytes[8] = {};
        const auto read     = file.Value()->ReadAll({bytes, sizeof(bytes)});
        ASSERT_TRUE(read.Succeeded());
        EXPECT_EQ(read.Value(), sizeof(MemoryOld));
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), MemoryOld, sizeof(MemoryOld), sizeof(MemoryOld)), 0);
        EXPECT_EQ(file.Value()->Path(), MemoryPath("/output/manifest.json"));
        EXPECT_TRUE(Root.CloseChecked(file.Value()).Succeeded());
    }

    TEST_F(VFSMemoryRootLeaseTest, ThousandsOfPublicationsReuseBoundedStorage)
    {
        static constexpr uint8_t payload[4096] = {42};
        const auto               path          = MemoryRelative("manifest.json");
        for (uint32_t i = 0; i < 2048; ++i)
        {
            ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, payload).Succeeded()) << "publication " << i;
            if (i % 32 == 0)
            {
                ASSERT_TRUE(Root.Remove(path).Succeeded());
            }
        }
        EXPECT_EQ(Root.Stat(path).Value().SizeBytes, sizeof(payload));
    }

    TEST_F(VFSMemoryRootLeaseTest, SnapshotExhaustionRecoversWithoutLeakingWrappersOrBytes)
    {
        static constexpr uint8_t payload[ZKilo(32)] = {17};
        const auto               path               = MemoryRelative("image.png");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, payload).Succeeded());
        Core::Containers::FixedArray<VFS::IVFSFile*, 32> snapshots = {};
        size_t                                           count     = 0;
        for (auto& file : snapshots)
        {
            const auto open = Root.Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
            if (open.Failed())
            {
                EXPECT_EQ(open.Error(), VFS::VFSError::OutOfMemory);
                break;
            }
            file = open.Value();
            ++count;
        }
        ASSERT_GT(count, 0u);
        ASSERT_LT(count, snapshots.size()); // Byte exhaustion, not the 32-wrapper limit.
        for (auto* file : snapshots)
        {
            if (file)
            {
                EXPECT_TRUE(Root.CloseChecked(file).Succeeded());
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            const auto open = Root.Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
            ASSERT_TRUE(open.Succeeded());
            snapshots[i] = open.Value();
        }
        for (size_t i = 0; i < count; ++i)
        {
            EXPECT_TRUE(Root.CloseChecked(snapshots[i]).Succeeded());
        }
    }

    TEST_F(VFSMemoryRootLeaseTest, WriteFailurePreservesExistingBytesAndDoesNotPublishMissingFile)
    {
        static constexpr uint8_t oversized[ZMega(1)] = {};
        ASSERT_TRUE(Backend.WriteFile(MemoryPath("/output/existing"), MemoryOld).Succeeded());
        EXPECT_EQ(Backend.WriteFile(MemoryPath("/output/existing"), oversized).Error(), VFS::VFSError::OutOfMemory);
        ExpectMemoryBytes(Root, MemoryRelative("existing"), MemoryOld);
        EXPECT_EQ(Backend.WriteFile(MemoryPath("/output/missing"), oversized).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_FALSE(Backend.Exists(MemoryPath("/output/missing")));
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("recovered"), MemoryNew).Succeeded());
    }

    TEST_F(VFSMemoryRootLeaseTest, OutstandingWriterRejectsNamespaceChangesAndPreservesUntruncatedBytes)
    {
        ASSERT_TRUE(Root.CreateDir(MemoryRelative("nested")).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("nested/file"), MemoryOld).Succeeded());
        const auto file = Root.Open(MemoryRelative("nested/file"), VFS::VFSOpenFlags::Write);
        ASSERT_TRUE(file.Succeeded());
        EXPECT_EQ(Root.Remove(MemoryRelative("nested/file")).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Root.Rename(MemoryRelative("nested"), MemoryRelative("moved")).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Backend.WriteFile(MemoryPath("/output/nested/file"), MemoryNew).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(Root.CloseChecked(file.Value()).Succeeded());
        ASSERT_TRUE(Root.Rename(MemoryRelative("nested"), MemoryRelative("moved")).Succeeded());
        ExpectMemoryBytes(Root, MemoryRelative("moved/file"), MemoryOld);
    }

    TEST_F(VFSMemoryRootLeaseTest, WrapperLimitRecoversAndRejectsForeignClose)
    {
        const auto path = MemoryRelative("file");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, MemoryOld).Succeeded());
        Core::Containers::FixedArray<VFS::IVFSFile*, 32> files = {};
        for (auto& file : files)
        {
            const auto open = Root.Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
            ASSERT_TRUE(open.Succeeded());
            file = open.Value();
        }
        EXPECT_EQ(Root.Open(MemoryRelative("not-created"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Exclusive).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_FALSE(Root.Exists(MemoryRelative("not-created")).Value());
        VFS::VFSPublicationRootLease other = {};
        ASSERT_TRUE(Context.AcquirePublicationRoot(MemoryPath("/output"), &other).Succeeded());
        EXPECT_EQ(other.CloseChecked(files[0]).Error(), VFS::VFSError::InvalidPath);
        for (auto* file : files)
        {
            EXPECT_TRUE(Root.CloseChecked(file).Succeeded());
        }
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("not-created"), MemoryNew).Succeeded());
    }

    TEST_F(VFSMemoryRootLeaseTest, FailedReinitializationPreservesStateAndSuccessfulReplacementRetiresOnlyAcquisition)
    {
        const auto identity = Root.QueryPublicationCapabilities().BackendIdentity;
        EXPECT_EQ(Backend.InitializeOwned(0, 64).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Backend.InitializeOwned(16, 64).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(Backend.RootProvider()->BackendIdentity(), identity);
        ASSERT_TRUE(Backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        EXPECT_NE(Backend.RootProvider()->BackendIdentity(), identity);
        VFS::VFSPublicationRootLease retired = {};
        EXPECT_EQ(Context.AcquirePublicationRoot(MemoryPath("/output"), &retired).Error(), VFS::VFSError::Unsupported);
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, MemoryRelative("still-live"), MemoryNew).Succeeded());
        ExpectMemoryBytes(Root, MemoryRelative("still-live"), MemoryNew);
    }

    TEST_F(VFSMemoryRootLeaseTest, EveryPinRetainsAnchorUntilLastRelease)
    {
        VFS::VFSScratchRootLease second = {};
        ASSERT_TRUE(Context.AcquireScratchRoot(MemoryPath("/output"), &second).Succeeded());
        EXPECT_EQ(Backend.Rename(MemoryPath("/output"), MemoryPath("/moved")).Error(), VFS::VFSError::AlreadyExists);
        Root.Reset();
        EXPECT_EQ(Backend.Remove(MemoryPath("/output")).Error(), VFS::VFSError::AlreadyExists);
        second.Reset();
        ASSERT_TRUE(Backend.Rename(MemoryPath("/output"), MemoryPath("/moved")).Succeeded());
        ASSERT_TRUE(Backend.Remove(MemoryPath("/moved")).Succeeded());
    }

    TEST(VFSMemoryRootLifetimeTest, OwnedStorageSurvivesFacadeContextAndExternalArena)
    {
        VFS::VFSPublicationRootLease publication = {};
        VFS::VFSScratchRootLease     scratch     = {};
        {
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(2), {});
            VFS::VFSMemoryBackend backend = {};
            ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
            ASSERT_TRUE(backend.CreateDir(MemoryPath("/output")).Succeeded());
            ASSERT_TRUE(backend.CreateDir(MemoryPath("/scratch")).Succeeded());
            VFS::VFSContext context = {};
            context.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(context.AcquirePublicationRoot(MemoryPath("/output"), &publication).Succeeded());
            ASSERT_TRUE(context.AcquireScratchRoot(MemoryPath("/scratch"), &scratch).Succeeded());
            context.Shutdown();
        }
        ASSERT_TRUE(VFS::WriteFileAtomically(publication, MemoryRelative("manifest.json"), MemoryOld).Succeeded());
        ExpectMemoryBytes(publication, MemoryRelative("manifest.json"), MemoryOld);
        publication.Reset();
        ASSERT_TRUE(scratch.CreateDirExclusive(MemoryRelative("run")).Succeeded());
        ASSERT_TRUE(scratch.Remove(MemoryRelative("run")).Succeeded());
        scratch.Reset();
    }

    TEST(VFSMemoryRootLifetimeTest, PinRegistryIsBoundedSharedAndReusable)
    {
        VFS::VFSMemoryBackend backend = {};
        ASSERT_TRUE(backend.InitializeOwned(ZMega(1), 128).Succeeded());
        const auto                                                                provider = backend.RootProvider();
        Core::Containers::FixedArray<Helpers::Scope<VFS::IVFSRootOperations>, 64> roots    = {};
        for (uint32_t i = 0; i < roots.size() + 1; ++i)
        {
            char name[16] = {'/'};
            ASSERT_EQ(std::to_chars(name + 1, name + sizeof(name) - 1, i).ec, std::errc{});
            const auto path = MemoryPath(name);
            ASSERT_TRUE(backend.CreateDir(path).Succeeded());
            auto root = provider->Acquire(path, VFS::VFSRootAccess::Publication);
            if (i == roots.size())
            {
                EXPECT_EQ(root.Error(), VFS::VFSError::OutOfMemory);
            }
            else
            {
                ASSERT_TRUE(root.Succeeded());
                roots[i] = std::move(root.Value());
            }
        }
        auto duplicate = provider->Acquire(MemoryPath("/0"), VFS::VFSRootAccess::Publication);
        ASSERT_TRUE(duplicate.Succeeded());
        roots[0].reset();
        EXPECT_EQ(backend.Remove(MemoryPath("/0")).Error(), VFS::VFSError::AlreadyExists);
        duplicate.Value().reset();
        EXPECT_TRUE(backend.Remove(MemoryPath("/0")).Succeeded());
        EXPECT_TRUE(provider->Acquire(MemoryPath("/64"), VFS::VFSRootAccess::Publication).Succeeded());
    }
} // namespace ZEngine::Tests
