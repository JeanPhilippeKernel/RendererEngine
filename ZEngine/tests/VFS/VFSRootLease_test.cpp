#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <barrier>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <type_traits>
#include "../TemporaryDirectory.h"
#if defined(_WIN32)
#include <ZEngine/Core/VFS/VFSNativeStat.h>
#include <winioctl.h>
#endif

namespace ZEngine::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSPath RootPath(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }
        VFS::VFSRelativePath Relative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }

        template <typename T>
        concept CanRename = requires(T& root, const VFS::VFSRelativePath& path) { root.Rename(path, path); };

        template <typename T>
        concept CanWriteOpen = requires(T& root, const VFS::VFSRelativePath& path) { root.Open(path, VFS::VFSOpenFlags::Write); };

        template <typename T>
        concept CanRemove = requires(T& root, const VFS::VFSRelativePath& path) { root.Remove(path); };

        template <typename T>
        concept CanCreate = requires(T& root, const VFS::VFSRelativePath& path) { root.CreateDir(path); };

        template <typename T>
        concept CanAcquirePublicationChild = requires(T& root, const VFS::VFSRelativePath& path, VFS::VFSPublicationRootLease* child) { root.AcquireChildRoot(path, child); };

        template <typename T>
        concept CanAcquireScratchChild = requires(T& root, const VFS::VFSRelativePath& path, VFS::VFSScratchRootLease* child) { root.AcquireChildRoot(path, child); };

        static_assert(!std::is_copy_constructible_v<VFS::VFSPublicationRootLease>);
        static_assert(!std::is_default_constructible_v<VFS::VFSRootLease>);
        static_assert(!std::is_move_assignable_v<VFS::VFSRootLease>);
        static_assert(!std::is_copy_assignable_v<VFS::VFSScratchRootLease>);
        static_assert(std::is_nothrow_move_constructible_v<VFS::VFSPublicationRootLease>);
        static_assert(std::is_nothrow_move_assignable_v<VFS::VFSScratchRootLease>);
        static_assert(!CanRename<VFS::VFSScratchRootLease>);
        static_assert(CanRename<VFS::VFSPublicationRootLease>);
        static_assert(!CanWriteOpen<VFS::VFSReadRootLease>);
        static_assert(!CanRemove<VFS::VFSReadRootLease>);
        static_assert(!CanCreate<VFS::VFSReadRootLease>);
        static_assert(!CanRename<VFS::VFSReadRootLease>);
        static_assert(!std::is_convertible_v<VFS::VFSReadRootLease*, VFS::VFSRootLease*>);
        static_assert(!std::is_copy_constructible_v<VFS::VFSReadRootLease>);
        static_assert(std::is_nothrow_move_constructible_v<VFS::VFSReadRootLease>);
        static_assert(CanAcquirePublicationChild<VFS::VFSPublicationRootLease>);
        static_assert(!CanAcquirePublicationChild<VFS::VFSScratchRootLease>);
        static_assert(!CanAcquirePublicationChild<VFS::VFSReadRootLease>);
        static_assert(CanAcquireScratchChild<VFS::VFSScratchRootLease>);
        static_assert(!CanAcquireScratchChild<VFS::VFSReadRootLease>);

        struct VisitProbe
        {
            uint64_t                    Seen    = 0;
            VFS::VFSRootEntry           Last    = {};
            VFS::VFSError               Failure = VFS::VFSError::OK;

            static VFS::VFSResult<void> Visit(void* context, const VFS::VFSRootEntry& entry)
            {
                auto& probe = *static_cast<VisitProbe*>(context);
                ++probe.Seen;
                probe.Last = entry;
                EXPECT_TRUE(entry.Path.IsValid());
                EXPECT_TRUE(entry.Identity.IsValid());
                return probe.Failure == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(probe.Failure);
            }
        };

        constexpr uint8_t OldContent[] = {1, 2, 3};
        constexpr uint8_t NewContent[] = {4, 5, 6, 7, 8};

        void              ExpectBytes(VFS::VFSRootLease& root, const VFS::VFSRelativePath& path, Core::Containers::ArrayView<const uint8_t> expected)
        {
            const auto open = root.Open(path, VFS::VFSOpenFlags::Read);
            ASSERT_TRUE(open.Succeeded()) << static_cast<uint32_t>(open.Error());
            uint8_t    bytes[32] = {};
            const auto read      = open.Value()->ReadAll({bytes, sizeof(bytes)});
            const auto close     = root.CloseChecked(open.Value());
            ASSERT_TRUE(read.Succeeded());
            ASSERT_EQ(read.Value(), expected.size());
            EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), expected.data(), expected.size(), expected.size()), 0);
            EXPECT_TRUE(close.Succeeded());
        }

#if defined(_WIN32)
        // Junctions exercise native reparse rejection without requiring the
        // symlink privilege or enabling Developer Mode on the test machine.
        DWORD CreateJunction(const std::filesystem::path& link, const std::filesystem::path& target)
        {
            std::error_code error = {};
            if (!std::filesystem::create_directory(link, error))
            {
                return error ? static_cast<DWORD>(error.value()) : ERROR_ALREADY_EXISTS;
            }
            const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return GetLastError();
            }
            const auto native = std::filesystem::absolute(target, error).native();
            if (error)
            {
                (void) CloseHandle(handle);
                return static_cast<DWORD>(error.value());
            }
            struct JunctionBuffer
            {
                DWORD   Tag              = IO_REPARSE_TAG_MOUNT_POINT;
                WORD    DataLength       = 0;
                WORD    Reserved         = 0;
                WORD    SubstituteOffset = 0;
                WORD    SubstituteLength = 0;
                WORD    PrintOffset      = 0;
                WORD    PrintLength      = 0;
                wchar_t Path[4096]       = {};
            } buffer                        = {};
            constexpr wchar_t prefix[]      = L"\\??\\";
            constexpr size_t  prefix_length = std::size(prefix) - 1;
            if (native.size() > std::size(buffer.Path) - prefix_length - 2)
            {
                (void) CloseHandle(handle);
                return ERROR_FILENAME_EXCED_RANGE;
            }
            (void) Helpers::secure_memcpy(buffer.Path, sizeof(buffer.Path), prefix, prefix_length * sizeof(wchar_t));
            (void) Helpers::secure_memcpy(buffer.Path + prefix_length, sizeof(buffer.Path) - prefix_length * sizeof(wchar_t), native.data(), native.size() * sizeof(wchar_t));
            buffer.SubstituteLength = static_cast<WORD>((prefix_length + native.size()) * sizeof(wchar_t));
            buffer.PrintOffset      = buffer.SubstituteLength + sizeof(wchar_t);
            buffer.DataLength       = static_cast<WORD>(8 + buffer.PrintOffset + sizeof(wchar_t));
            DWORD      returned     = 0;
            const auto result       = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &buffer, 8 + buffer.DataLength, nullptr, 0, &returned, nullptr) ? ERROR_SUCCESS : GetLastError();
            (void) CloseHandle(handle);
            return result;
        }
#endif
    } // namespace

    TEST(VFSRootLeaseTest, EmptyLeasesFailClosedAndRejectInvalidPaths)
    {
        VFS::VFSPublicationRootLease root = {};
        EXPECT_FALSE(root.IsValid());
        EXPECT_EQ(root.QueryPublicationCapabilities().BackendIdentity, 0u);
        EXPECT_EQ(root.Open({}, VFS::VFSOpenFlags::Read).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.Stat({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.Exists({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.CreateDir({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.CreateDirExclusive({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.Remove({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.Rename({}, Relative("file")).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.RenameNoReplace(Relative("file"), {}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.Open(Relative("file"), VFS::VFSOpenFlags::Read).Error(), VFS::VFSError::Unsupported);
        EXPECT_TRUE(root.CloseChecked(nullptr).Succeeded());
        VFS::VFSPublicationRootLease child = {};
        EXPECT_EQ(root.AcquireChildRoot({}, &child).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.AcquireChildRoot(Relative("child"), nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(root.AcquireChildRoot(Relative("child"), &child).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(child.IsValid());
        root.Reset();
    }

    TEST(VFSRootLeaseTest, UnsupportedBackendCannotManufactureCapabilities)
    {
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        backend.Initialize(&memory.MainArena);
        VFS::VFSContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease publication = {};
        VFS::VFSScratchRootLease     scratch     = {};
        EXPECT_EQ(context.AcquirePublicationRoot(VFS::VFSPath::Root(), &publication).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(context.AcquireScratchRoot(VFS::VFSPath::Root(), &scratch).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(publication.IsValid());
        EXPECT_FALSE(scratch.IsValid());
        context.Shutdown();
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    class VFSDiskRootLeaseTest : public testing::Test
    {
    protected:
        TemporaryDirectory           Directory = {};
        Core::Memory::MemoryManager  Memory    = {};
        VFS::VFSDiskBackend          Disk      = {};
        VFS::VFSContext              Context   = {};
        VFS::VFSPublicationRootLease Root      = {};

        void                         SetUp() override
        {
            std::error_code error = {};
            ASSERT_TRUE(Directory.Create("zengine_rooted_vfs", error));
            ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "output", error));
            Memory.Initialize(ZMega(8), {});
            const auto native = Directory.Path().generic_u8string();
            Disk.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &Memory.MainArena);
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Disk, RootPath("/assets"), 0).Succeeded());
            const auto acquire = Context.AcquirePublicationRoot(RootPath("/assets/output"), &Root);
            ASSERT_TRUE(acquire.Succeeded()) << static_cast<uint32_t>(acquire.Error());
        }

        void TearDown() override
        {
            Root.Reset();
            Context.Shutdown();
        }
    };

    TEST_F(VFSDiskRootLeaseTest, CapturesAllPublicationGuaranteesAndStableIdentity)
    {
        const auto capabilities = Root.QueryPublicationCapabilities();
        EXPECT_NE(capabilities.BackendIdentity, 0u);
        EXPECT_TRUE(VFS::HasGuarantees(capabilities.Guarantees, VFS::VFS_PUBLICATION_GUARANTEES));
        VFS::VFSPublicationRootLease second = {};
        ASSERT_TRUE(Context.AcquirePublicationRoot(RootPath("/assets/output"), &second).Succeeded());
        EXPECT_EQ(second.QueryPublicationCapabilities().BackendIdentity, capabilities.BackendIdentity);
    }

    TEST_F(VFSDiskRootLeaseTest, StreamingStageRetainsPinnedRoutingAndDoesNotReplaceAnExistingObject)
    {
        const auto directory = Relative("objects");
        ASSERT_TRUE(Root.CreateDirExclusive(directory).Succeeded());
        const auto final = Relative("objects/digest.object");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, final, OldContent, VFS::AtomicWriteMode::NoReplace).Succeeded());
        VFS::VFSRootAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Root, final).Succeeded());
        ASSERT_TRUE(writer.Write(NewContent).Succeeded());
        const auto temporary = writer.TemporaryPath();
        Context.Shutdown();
        ASSERT_TRUE(writer.Seal().Succeeded());
        ExpectBytes(Root, temporary, NewContent);
        EXPECT_EQ(writer.Commit(final, VFS::AtomicWriteMode::NoReplace).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(writer.OwnsTemporary());
        ExpectBytes(Root, final, OldContent);
        ASSERT_TRUE(writer.Reset().Succeeded());
        EXPECT_FALSE(Root.Exists(temporary).Value());
        ExpectBytes(Root, final, OldContent);
    }

    TEST_F(VFSDiskRootLeaseTest, AcquisitionFailurePreservesExistingLeaseAndCreatesNothing)
    {
        const auto identity = Root.QueryPublicationCapabilities().BackendIdentity;
        EXPECT_EQ(Context.AcquirePublicationRoot(RootPath("/missing"), &Root).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Root.QueryPublicationCapabilities().BackendIdentity, identity);
        VFS::VFSPublicationRootLease empty = {};
        EXPECT_EQ(Context.AcquirePublicationRoot({}, &empty).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Context.AcquirePublicationRoot(RootPath("/assets/not-created"), &empty).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Context.AcquirePublicationRoot(RootPath("/assets/output"), nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(empty.IsValid());
        EXPECT_FALSE(std::filesystem::exists(Directory.Path() / "not-created"));
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "output"));
    }

    TEST_F(VFSDiskRootLeaseTest, ChildPublicationRootPreservesAuthorityAndPinsAfterParentRelease)
    {
        const auto path = Relative("desktop-vulkan-le64");
        ASSERT_TRUE(Root.CreateDirExclusive(path).Succeeded());
        const auto                   identity = Root.Identity(path).Value();
        VFS::VFSPublicationRootLease child    = {};
        ASSERT_TRUE(Root.AcquireChildRoot(path, &child).Succeeded());
        EXPECT_EQ(child.RootIdentity().Value(), identity);
        EXPECT_EQ(child.QueryPublicationCapabilities().BackendIdentity, Root.QueryPublicationCapabilities().BackendIdentity);
        EXPECT_TRUE(Root.Overlaps(child).Value());
        Root.Reset();
        EXPECT_EQ(Context.Unmount(RootPath("/assets")).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Context.Mount(&Disk, RootPath("/assets/output/desktop-vulkan-le64"), 1).Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(VFS::WriteFileAtomically(child, Relative("manifest.json"), OldContent).Succeeded());
        ExpectBytes(child, Relative("manifest.json"), OldContent);
        child.Reset();
        EXPECT_TRUE(Context.Unmount(RootPath("/assets")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, ChildAcquisitionNeverCreatesDirectoriesOrReplacesAnExistingLease)
    {
        VFS::VFSPublicationRootLease child = {};
        EXPECT_EQ(Root.AcquireChildRoot(Relative("missing/target"), &child).Error(), VFS::VFSError::NotFound);
        EXPECT_FALSE(child.IsValid());
        EXPECT_FALSE(std::filesystem::exists(Directory.Path() / "output/missing"));
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("file"), OldContent).Succeeded());
        EXPECT_EQ(Root.AcquireChildRoot(Relative("file"), &child).Error(), VFS::VFSError::NotADirectory);
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("target")).Succeeded());
        ASSERT_TRUE(Root.AcquireChildRoot(Relative("target"), &child).Succeeded());
        const auto identity = child.RootIdentity().Value();
        EXPECT_EQ(Root.AcquireChildRoot(Relative("missing"), &child).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Root.AcquireChildRoot(Relative("target"), &Root).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(child.RootIdentity().Value(), identity);
    }

    TEST_F(VFSDiskRootLeaseTest, ChildAcquisitionUsesRetainedAnchorAfterPathReplacementAndContextShutdown)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("target")).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("target/manifest.json"), OldContent).Succeeded());
        const auto      identity = Root.Identity(Relative("target")).Value();
        std::error_code error    = {};
        std::filesystem::rename(Directory.Path() / "output", Directory.Path() / "retained-output", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(std::filesystem::create_directories(Directory.Path() / "output/target", error));
        VFS::VFSPublicationRootLease replacement = {};
        ASSERT_TRUE(Context.AcquirePublicationRoot(RootPath("/assets/output/target"), &replacement).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(replacement, Relative("manifest.json"), NewContent).Succeeded());
        replacement.Reset();
        Context.Shutdown();
        Disk.RootProvider()->Retire();
        VFS::VFSPublicationRootLease child = {};
        ASSERT_TRUE(Root.AcquireChildRoot(Relative("target"), &child).Succeeded());
        Root.Reset();
        EXPECT_EQ(child.RootIdentity().Value(), identity);
        ExpectBytes(child, Relative("manifest.json"), OldContent);
        ASSERT_TRUE(VFS::WriteFileAtomically(child, Relative("manifest.json"), NewContent).Succeeded());
        ExpectBytes(child, Relative("manifest.json"), NewContent);
    }

#if !defined(_WIN32)
    TEST_F(VFSDiskRootLeaseTest, ChildAcquisitionRejectsSymlinkLeavesAndAncestors)
    {
        std::error_code error = {};
        ASSERT_TRUE(std::filesystem::create_directories(Directory.Path() / "outside/target", error));
        std::filesystem::create_directory_symlink(Directory.Path() / "outside", Directory.Path() / "output/link", error);
        ASSERT_FALSE(error);
        VFS::VFSPublicationRootLease child = {};
        EXPECT_TRUE(Root.AcquireChildRoot(Relative("link"), &child).Failed());
        EXPECT_TRUE(Root.AcquireChildRoot(Relative("link/target"), &child).Failed());
        EXPECT_FALSE(child.IsValid());
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "outside/target"));
    }
#endif

    TEST_F(VFSDiskRootLeaseTest, ChildReadRootRetainsReadOnlyAccessAfterParentRelease)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("target")).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("target/asset"), OldContent).Succeeded());
        VFS::VFSReadRootLease parent = {}, child = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &parent).Succeeded());
        ASSERT_TRUE(parent.AcquireChildRoot(Relative("target"), &child).Succeeded());
        parent.Reset();
        const auto file = child.Open(Relative("asset"));
        ASSERT_TRUE(file.Succeeded());
        EXPECT_FALSE(file.Value()->Write(NewContent, 0).Succeeded());
        EXPECT_TRUE(child.CloseChecked(file.Value()).Succeeded());
        auto authority = Disk.RootProvider()->Acquire(RootPath("/output"), VFS::VFSRootAccess::ReadOnly);
        ASSERT_TRUE(authority.Succeeded());
        auto nested = authority.Value()->AcquireChild(Relative("target"));
        ASSERT_TRUE(nested.Succeeded());
        EXPECT_EQ(nested.Value()->Open(Relative("asset"), VFS::VFSOpenFlags::Write).Error(), VFS::VFSError::PermissionDenied);
    }

    TEST_F(VFSDiskRootLeaseTest, AtomicWritesUseOwnedSiblingsAndNoReplacePreservesOldBytes)
    {
        const auto file = Relative("manifest.json");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, file, {OldContent, sizeof(OldContent)}).Succeeded());
        EXPECT_EQ(VFS::WriteFileAtomically(Root, file, {NewContent, sizeof(NewContent)}, VFS::AtomicWriteMode::NoReplace).Error(), VFS::VFSError::AlreadyExists);
        ExpectBytes(Root, file, {OldContent, sizeof(OldContent)});
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, file, {NewContent, sizeof(NewContent)}).Succeeded());
        ExpectBytes(Root, file, {NewContent, sizeof(NewContent)});
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Directory.Path() / "output"), std::filesystem::directory_iterator{}), 1);
    }

    TEST_F(VFSDiskRootLeaseTest, ReplaceCompatibleReaderRetainsOldObject)
    {
        const auto file = Relative("manifest.json");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, file, {OldContent, sizeof(OldContent)}).Succeeded());
        const auto open = Root.Open(file, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
        ASSERT_TRUE(open.Succeeded());
        const auto replace  = VFS::WriteFileAtomically(Root, file, {NewContent, sizeof(NewContent)});
        uint8_t    bytes[8] = {};
        const auto read     = open.Value()->ReadAll({bytes, sizeof(bytes)});
        const auto close    = Root.CloseChecked(open.Value());
        ASSERT_TRUE(replace.Succeeded());
        ASSERT_TRUE(read.Succeeded());
        EXPECT_EQ(read.Value(), sizeof(OldContent));
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), OldContent, sizeof(OldContent), sizeof(OldContent)), 0);
        EXPECT_TRUE(close.Succeeded());
        ExpectBytes(Root, file, {NewContent, sizeof(NewContent)});
    }

    TEST_F(VFSDiskRootLeaseTest, ExclusiveNamesAndStrictOpenModes)
    {
        ASSERT_TRUE(Root.CreateDir(Relative("nested/objects")).Succeeded());
        EXPECT_TRUE(Root.Stat(Relative("nested/objects")).Value().IsDirectory);
        EXPECT_EQ(Root.CreateDirExclusive(Relative("nested")).Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("lock")).Succeeded());
        EXPECT_EQ(Root.CreateDirExclusive(Relative("lock")).Error(), VFS::VFSError::AlreadyExists);
        const auto file   = Relative("nested/objects/first");
        const auto flags  = VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Exclusive;
        const auto opened = Root.Open(file, flags);
        ASSERT_TRUE(opened.Succeeded());
        EXPECT_EQ(Root.Open(file, flags).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(Root.CloseChecked(opened.Value()).Succeeded());
        EXPECT_EQ(Root.Open(file, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::AllowReplace).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Root.Open(file, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::Truncate).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Root.Open(Relative("not-created"), VFS::VFSOpenFlags::Write).Error(), VFS::VFSError::NotFound);
        ASSERT_TRUE(Root.RenameNoReplace(file, Relative("nested/objects/second")).Succeeded());
        EXPECT_FALSE(Root.Exists(file).Value());
        ASSERT_TRUE(Root.Remove(Relative("nested/objects/second")).Succeeded());
        ASSERT_TRUE(Root.Remove(Relative("nested/objects")).Succeeded());
        ASSERT_TRUE(Root.Remove(Relative("nested")).Succeeded());
        ASSERT_TRUE(Root.Remove(Relative("lock")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, ActivePinsRejectOverlappingMountMutationsButAllowUnrelatedMounts)
    {
        EXPECT_EQ(Context.Unmount(RootPath("/assets")).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Context.Mount(&Disk, RootPath("/assets/output/child"), 10).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Context.Mount(&Disk, VFS::VFSPath::Root(), 0).Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(Context.Mount(&Disk, RootPath("/unrelated"), 10).Succeeded());
        ASSERT_TRUE(Context.Unmount(RootPath("/unrelated")).Succeeded());
        Root.Reset();
        EXPECT_TRUE(Context.Unmount(RootPath("/assets")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, AcquisitionNeverBypassesExistingDescendantMount)
    {
        Root.Reset();
        ASSERT_TRUE(Context.Mount(&Disk, RootPath("/assets/output/nested"), 10).Succeeded());
        EXPECT_EQ(Context.AcquirePublicationRoot(RootPath("/assets/output"), &Root).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(Root.IsValid());
        EXPECT_TRUE(Context.Unmount(RootPath("/assets/output/nested")).Succeeded());
        EXPECT_TRUE(Context.AcquirePublicationRoot(RootPath("/assets/output"), &Root).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, RootRenameAndPathReplacementCannotRerouteLease)
    {
        std::error_code error = {};
        std::filesystem::rename(Directory.Path() / "output", Directory.Path() / "original", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "output", error));
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("manifest.json"), {NewContent, sizeof(NewContent)}).Succeeded());
        EXPECT_TRUE(std::filesystem::exists(Directory.Path() / "original/manifest.json"));
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "output"));
        EXPECT_TRUE(Root.Remove(Relative("manifest.json")).Succeeded());
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "original"));
    }

#if !defined(_WIN32)
    TEST_F(VFSDiskRootLeaseTest, SymlinkComponentsAndLeafTargetsAreNeverFollowed)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("safe")).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("safe/sentinel"), {OldContent, sizeof(OldContent)}).Succeeded());
        std::error_code error = {};
        std::filesystem::create_directory_symlink(Directory.Path() / "output/safe", Directory.Path() / "output/alias", error);
        ASSERT_FALSE(error);
        std::filesystem::create_symlink(Directory.Path() / "output/safe/sentinel", Directory.Path() / "output/link", error);
        ASSERT_FALSE(error);
        EXPECT_TRUE(Root.Open(Relative("alias/sentinel"), VFS::VFSOpenFlags::Read).Failed());
        EXPECT_TRUE(Root.CreateDir(Relative("alias/newdir")).Failed());
        EXPECT_TRUE(Root.Remove(Relative("alias/sentinel")).Failed());
        EXPECT_EQ(Root.Open(Relative("link"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Truncate).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Stat(Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Exists(Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Remove(Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Rename(Relative("safe/sentinel"), Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Rename(Relative("link"), Relative("renamed")).Error(), VFS::VFSError::PermissionDenied);
        VFS::VFSPublicationRootLease alias = {};
        EXPECT_TRUE(Context.AcquirePublicationRoot(RootPath("/assets/output/alias"), &alias).Failed());
        EXPECT_FALSE(alias.IsValid());
        ExpectBytes(Root, Relative("safe/sentinel"), {OldContent, sizeof(OldContent)});
        EXPECT_FALSE(std::filesystem::exists(Directory.Path() / "output/safe/newdir"));
    }
#endif

    TEST_F(VFSDiskRootLeaseTest, ScratchUsesSamePinDomainWithoutPublicationAuthority)
    {
        VFS::VFSScratchRootLease scratch = {};
        ASSERT_TRUE(Context.AcquireScratchRoot(RootPath("/assets/output"), &scratch).Succeeded());
        ASSERT_TRUE(scratch.CreateDirExclusive(Relative("run")).Succeeded());
        Root.Reset();
        EXPECT_EQ(Context.Unmount(RootPath("/assets")).Error(), VFS::VFSError::AlreadyExists);
        const auto open = scratch.Open(Relative("run/capture"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Exclusive);
        ASSERT_TRUE(open.Succeeded());
        EXPECT_TRUE(open.Value()->Write({NewContent, sizeof(NewContent)}, 0).Succeeded());
        EXPECT_TRUE(open.Value()->Flush().Succeeded());
        EXPECT_TRUE(scratch.CloseChecked(open.Value()).Succeeded());
        ExpectBytes(scratch, Relative("run/capture"), {NewContent, sizeof(NewContent)});
        EXPECT_TRUE(scratch.Remove(Relative("run/capture")).Succeeded());
        EXPECT_TRUE(scratch.Remove(Relative("run")).Succeeded());
        scratch.Reset();
        EXPECT_TRUE(Context.Unmount(RootPath("/assets")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, ReadOnlyBackendCannotAcquireWritingAuthority)
    {
        VFS::VFSDiskBackend read_only = {};
        const auto          native    = Directory.Path().generic_u8string();
        read_only.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read, &Memory.MainArena);
        ASSERT_TRUE(Context.Mount(&read_only, RootPath("/readonly"), 0).Succeeded());
        VFS::VFSPublicationRootLease publication = {};
        VFS::VFSScratchRootLease     scratch     = {};
        EXPECT_EQ(Context.AcquirePublicationRoot(RootPath("/readonly/output"), &publication).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Context.AcquireScratchRoot(RootPath("/readonly/output"), &scratch).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(publication.IsValid());
        EXPECT_FALSE(scratch.IsValid());
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "output"));
    }

    TEST_F(VFSDiskRootLeaseTest, ReadOnlyBackendAcquiresReadRootAndHonorsExactEntryLimit)
    {
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("asset"), {OldContent, sizeof(OldContent)}).Succeeded());
        VFS::VFSDiskBackend backend = {};
        const auto          native  = Directory.Path().generic_u8string();
        backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read, &Memory.MainArena);
        ASSERT_TRUE(Context.Mount(&backend, RootPath("/readonly"), 0).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/readonly/output"), &read).Succeeded());
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        EXPECT_EQ(read.VisitRoot(0, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Seen, 0u);
        ASSERT_TRUE(read.VisitRoot(1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 1u);
        EXPECT_EQ(probe.Last.Path, Relative("asset"));
        EXPECT_EQ(probe.Last.Stat.SizeBytes, sizeof(OldContent));
        EXPECT_EQ(read.Identity(Relative("asset")).Value(), probe.Last.Identity);
        EXPECT_EQ(Context.Unmount(RootPath("/readonly")).Error(), VFS::VFSError::AlreadyExists);
        const auto open = read.Open(Relative("asset"));
        ASSERT_TRUE(open.Succeeded());
        EXPECT_EQ(open.Value()->Identity().Value(), probe.Last.Identity);
        EXPECT_TRUE(read.CloseChecked(open.Value()).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, EnumerationIsBoundedAcrossBatchesAndVisitsHaveIndependentOffsets)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("nested")).Succeeded());
        constexpr uint32_t count = 320;
        for (uint32_t i = 0; i < count; ++i)
        {
            char path[64] = {};
            ASSERT_GT(std::snprintf(path, sizeof(path), "nested/asset-%04u", i), 0);
            const auto open = Root.Open(Relative(path), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Exclusive);
            ASSERT_TRUE(open.Succeeded());
            ASSERT_TRUE(Root.CloseChecked(open.Value()).Succeeded());
        }
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        ASSERT_TRUE(read.VisitDirectory(Relative("nested"), count, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, count);
        EXPECT_EQ(probe.Last.Path.ComponentCount(), 2u);
        probe = {};
        ASSERT_TRUE(read.VisitDirectory(Relative("nested"), count, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, count);
        probe = {};
        EXPECT_EQ(read.VisitDirectory(Relative("nested"), count - 1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Seen, count - 1);
        probe = {.Failure = VFS::VFSError::Corrupted};
        EXPECT_EQ(read.VisitDirectory(Relative("nested"), count, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(probe.Seen, 1u);
    }

    TEST_F(VFSDiskRootLeaseTest, EnumerationRejectsBadBuffersDirectoriesAndMissingVisitors)
    {
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("file"), {OldContent, sizeof(OldContent)}).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        EXPECT_EQ(read.VisitRoot(1, {}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer) - 1}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer)}, nullptr, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitDirectory({}, 1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.VisitDirectory(Relative("missing"), 1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(read.VisitDirectory(Relative("file"), 1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::NotADirectory);
        EXPECT_EQ(probe.Seen, 0u);
    }

    TEST_F(VFSDiskRootLeaseTest, EmptyDirectoryAcceptsZeroEntryLimit)
    {
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        EXPECT_TRUE(read.VisitRoot(0, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 0u);
    }

    TEST_F(VFSDiskRootLeaseTest, OpenHandleMetadataUsesNativeSharingAndIdentitySurvivesReplacement)
    {
        const auto path = Relative("asset");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, {OldContent, sizeof(OldContent)}).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        const auto open = read.Open(path);
        ASSERT_TRUE(open.Succeeded());
        const auto old_identity = open.Value()->Identity();
        ASSERT_TRUE(old_identity.Succeeded());
        EXPECT_EQ(read.Identity(path).Value(), old_identity.Value());
        std::error_code error = {};
        std::filesystem::resize_file(Directory.Path() / "output/asset", 17, error);
#if defined(_WIN32)
        // Replace-compatible readers intentionally do not share writes. A
        // writer cannot modify a captured file through another open handle.
        EXPECT_TRUE(error);
        constexpr uint64_t expected_size = sizeof(OldContent);
#else
        EXPECT_FALSE(error);
        constexpr uint64_t expected_size = 17;
#endif
        EXPECT_EQ(open.Value()->Size().Value(), expected_size);
        EXPECT_EQ(open.Value()->Stat().Value().SizeBytes, expected_size);
        EXPECT_TRUE(VFS::WriteFileAtomically(Root, path, {NewContent, sizeof(NewContent)}).Succeeded());
        EXPECT_NE(read.Identity(path).Value(), old_identity.Value());
        EXPECT_EQ(open.Value()->Identity().Value(), old_identity.Value());
        EXPECT_EQ(open.Value()->Size().Value(), expected_size);
        EXPECT_TRUE(read.CloseChecked(open.Value()).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, UnicodeEntriesPreserveNamesAndNativeIdentity)
    {
        const auto path = Relative(reinterpret_cast<cstring>(u8"café_東京_🚀.asset"));
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, OldContent).Succeeded());
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        ASSERT_TRUE(read.VisitRoot(1, buffer, VisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 1u);
        EXPECT_EQ(probe.Last.Path, path);
        const auto opened = read.Open(path);
        ASSERT_TRUE(opened.Succeeded());
        const auto identity = opened.Value()->Identity();
        ASSERT_TRUE(identity.Succeeded());
        EXPECT_EQ(identity.Value(), probe.Last.Identity);
#if defined(_WIN32)
        const auto native_path = Directory.Path() / "output" / reinterpret_cast<const char8_t*>(path.CStr());
        const auto handle      = CreateFileW(native_path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        ASSERT_NE(handle, INVALID_HANDLE_VALUE);
        FILE_ID_INFO native  = {};
        const auto   queried = GetFileInformationByHandleEx(handle, FileIdInfo, &native, sizeof(native));
        EXPECT_TRUE(CloseHandle(handle));
        ASSERT_TRUE(queried);
        EXPECT_EQ(identity.Value().Volume, native.VolumeSerialNumber);
        uint64_t words[2] = {};
        static_assert(sizeof(words) == sizeof(native.FileId.Identifier));
        ASSERT_EQ(Helpers::secure_memcpy(words, sizeof(words), native.FileId.Identifier, sizeof(native.FileId.Identifier)), Helpers::MEMORY_OP_SUCCESS);
        EXPECT_EQ(identity.Value().ObjectLow, words[0]);
        EXPECT_EQ(identity.Value().ObjectHigh, words[1]);
#endif
        EXPECT_TRUE(read.CloseChecked(opened.Value()).Succeeded());
    }

#if defined(_WIN32)
    TEST_F(VFSDiskRootLeaseTest, InvalidNativeMetadataHandlesCannotReuseAStaleLastError)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        EXPECT_EQ(VFS::Detail::NativeIdentity(INVALID_HANDLE_VALUE).Error(), VFS::VFSError::IOError);
        SetLastError(ERROR_FILE_NOT_FOUND);
        EXPECT_EQ(VFS::Detail::NativeStat(INVALID_HANDLE_VALUE).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(VFS::Detail::NativeIdentity(nullptr).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(VFS::Detail::NativeStat(nullptr).Error(), VFS::VFSError::IOError);
    }

    TEST_F(VFSDiskRootLeaseTest, OrdinaryReaderBlocksReplacementUntilClosed)
    {
        const auto path = Relative("manifest.json");
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, OldContent).Succeeded());
        const auto opened = Root.Open(path, VFS::VFSOpenFlags::Read);
        ASSERT_TRUE(opened.Succeeded());
        EXPECT_EQ(VFS::WriteFileAtomically(Root, path, NewContent).Error(), VFS::VFSError::PermissionDenied);
        ExpectBytes(Root, path, OldContent);
        EXPECT_TRUE(Root.CloseChecked(opened.Value()).Succeeded());
        EXPECT_TRUE(VFS::WriteFileAtomically(Root, path, NewContent).Succeeded());
        ExpectBytes(Root, path, NewContent);
    }

    TEST_F(VFSDiskRootLeaseTest, RootedAndOrdinaryAppendIgnoreTheExplicitWriteOffset)
    {
        for (bool rooted : {false, true})
        {
            const auto path = Relative(rooted ? "rooted" : "ordinary");
            ASSERT_TRUE(VFS::WriteFileAtomically(Root, path, OldContent).Succeeded());
            const auto flags  = VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Append;
            const auto opened = rooted ? Root.Open(path, flags) : Disk.Open(RootPath("/output/ordinary"), flags);
            ASSERT_TRUE(opened.Succeeded());
            const auto written = opened.Value()->Write(NewContent, 0);
            EXPECT_TRUE(written.Succeeded());
            if (written.Succeeded())
            {
                EXPECT_EQ(written.Value(), sizeof(NewContent));
            }
            if (rooted)
            {
                EXPECT_TRUE(Root.CloseChecked(opened.Value()).Succeeded());
            }
            else
            {
                Disk.Close(opened.Value());
            }
            const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
            ExpectBytes(Root, path, expected);
        }
    }

    TEST_F(VFSDiskRootLeaseTest, JunctionLeavesAndAncestorsNeverGrantAuthorityOutsideRoot)
    {
        std::error_code error = {};
        ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "outside", error));
        VFS::VFSPublicationRootLease outside = {};
        ASSERT_TRUE(Context.AcquirePublicationRoot(RootPath("/assets/outside"), &outside).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(outside, Relative("sentinel"), OldContent).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("source"), NewContent).Succeeded());
        const auto visible = Directory.Path() / "output/link";
        const auto hidden  = Directory.Path() / "output/hidden-link";
        ASSERT_EQ(CreateJunction(visible, Directory.Path() / "outside"), ERROR_SUCCESS);
        VFS::VFSPublicationRootLease child = {};
        EXPECT_EQ(Root.AcquireChildRoot(Relative("link"), &child).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.AcquireChildRoot(Relative("link/nested"), &child).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_FALSE(child.IsValid());
        EXPECT_EQ(Root.Open(Relative("link"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Truncate).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Stat(Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Identity(Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Remove(Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.CreateDir(Relative("link/newdir")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Rename(Relative("source"), Relative("link")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Root.Rename(Relative("link"), Relative("renamed")).Error(), VFS::VFSError::PermissionDenied);
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        EXPECT_EQ(read.VisitRoot(8, buffer, VisitProbe::Visit, &probe).Error(), VFS::VFSError::PermissionDenied);
        std::barrier start(2);
        bool         rename_succeeded = true;
        std::thread  retarget([&] {
            start.arrive_and_wait();
            std::error_code thread_error = {};
            for (uint32_t i = 0; i < 256; ++i)
            {
                std::filesystem::rename(visible, hidden, thread_error);
                if (!thread_error)
                {
                    std::filesystem::rename(hidden, visible, thread_error);
                }
                if (thread_error)
                {
                    rename_succeeded = false;
                    break;
                }
            }
        });
        start.arrive_and_wait();
        for (uint32_t i = 0; i < 256; ++i)
        {
            const auto opened = Root.Open(Relative("link/sentinel"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Truncate);
            EXPECT_TRUE(opened.Failed());
            if (opened.Succeeded())
            {
                (void) Root.CloseChecked(opened.Value());
            }
            else
            {
                EXPECT_TRUE(opened.Error() == VFS::VFSError::NotFound || opened.Error() == VFS::VFSError::PermissionDenied);
            }
        }
        retarget.join();
        EXPECT_TRUE(rename_succeeded);
        ExpectBytes(outside, Relative("sentinel"), OldContent);
        ExpectBytes(Root, Relative("source"), NewContent);
        EXPECT_FALSE(std::filesystem::exists(Directory.Path() / "outside/newdir"));
    }
#endif

#if !defined(_WIN32)
    TEST_F(VFSDiskRootLeaseTest, ReadRootRejectsSymlinksSpecialFilesAndNonportableNames)
    {
        VFS::VFSReadRootLease read = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &read).Succeeded());
        uint8_t         buffer[ZKilo(4)] = {};
        VisitProbe      probe            = {};
        std::error_code error            = {};
        const auto      alias            = Directory.Path() / "output/alias";
        std::filesystem::create_directory_symlink(Directory.Path(), alias, error);
        ASSERT_FALSE(error);
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(read.Identity(Relative("alias")).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_TRUE(read.VisitDirectory(Relative("alias"), 10, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Failed());
        ASSERT_TRUE(std::filesystem::remove(alias, error));
        const auto fifo = Directory.Path() / "output/fifo";
        ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
        EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::NotAFile);
        ASSERT_TRUE(std::filesystem::remove(fifo, error));
        for (cstring name : {"bad\\name", "bad:name", "bad\xff"})
        {
            const auto native_path = Directory.Path() / "output" / name;
            const int  descriptor  = ::open(native_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
            if (descriptor < 0 && static_cast<uint8_t>(name[3]) == 0xff)
            {
                // APFS rejects malformed UTF-8 at creation. On filesystems that
                // permit these bytes the enumeration parser must reject them.
                EXPECT_TRUE(errno == EILSEQ || errno == EINVAL) << errno;
                continue;
            }
            ASSERT_GE(descriptor, 0) << errno;
            ASSERT_EQ(::close(descriptor), 0);
            EXPECT_EQ(read.VisitRoot(1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Error(), VFS::VFSError::InvalidPath);
            ASSERT_TRUE(std::filesystem::remove(native_path, error));
        }
        EXPECT_EQ(probe.Seen, 0u);
    }
#endif

    TEST_F(VFSDiskRootLeaseTest, PhysicalOverlapUsesNativeAncestryAcrossSeparateBackends)
    {
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("child")).Succeeded());
        ASSERT_TRUE(Root.CreateDirExclusive(Relative("other")).Succeeded());
        VFS::VFSReadRootLease parent = {}, child = {}, sibling = {}, same = {};
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output"), &parent).Succeeded());
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output/child"), &child).Succeeded());
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/assets/output/other"), &sibling).Succeeded());
        EXPECT_TRUE(parent.Overlaps(child).Value());
        EXPECT_TRUE(child.Overlaps(parent).Value());
        EXPECT_FALSE(child.Overlaps(sibling).Value());
        VFS::VFSDiskBackend separate = {};
        const auto          native   = (Directory.Path() / "output/child").generic_u8string();
        separate.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read, &Memory.MainArena);
        ASSERT_TRUE(Context.Mount(&separate, RootPath("/separate"), 0).Succeeded());
        ASSERT_TRUE(Context.AcquireReadRoot(RootPath("/separate"), &same).Succeeded());
        EXPECT_EQ(same.RootIdentity().Value(), child.RootIdentity().Value());
        EXPECT_TRUE(same.Overlaps(child).Value());
        EXPECT_TRUE(parent.Overlaps(same).Value());
        EXPECT_TRUE(same.Overlaps(Root).Value());
        EXPECT_FALSE(same.Overlaps(sibling).Value());
    }

    TEST_F(VFSDiskRootLeaseTest, ReadLeaseSurvivesFacadeArenaDestructionAndRootPathReplacement)
    {
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("asset"), {OldContent, sizeof(OldContent)}).Succeeded());
        VFS::VFSReadRootLease retained = {};
        {
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(2), {});
            VFS::VFSDiskBackend backend = {};
            const auto          native  = Directory.Path().generic_u8string();
            backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read, &memory.MainArena);
            VFS::VFSContext context = {};
            context.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(context.AcquireReadRoot(RootPath("/output"), &retained).Succeeded());
            context.Shutdown();
            memory.Shutdown();
        }
        const auto      identity = retained.RootIdentity().Value();
        std::error_code error    = {};
        std::filesystem::rename(Directory.Path() / "output", Directory.Path() / "original", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "output", error));
        EXPECT_EQ(retained.RootIdentity().Value(), identity);
        uint8_t    buffer[ZKilo(4)] = {};
        VisitProbe probe            = {};
        ASSERT_TRUE(retained.VisitRoot(1, {buffer, sizeof(buffer)}, VisitProbe::Visit, &probe).Succeeded());
        EXPECT_EQ(probe.Seen, 1u);
        VFS::VFSReadRootLease moved(std::move(retained));
        EXPECT_FALSE(retained.IsValid());
        const auto open = moved.Open(Relative("asset"));
        ASSERT_TRUE(open.Succeeded());
        EXPECT_EQ(open.Value()->Size().Value(), sizeof(OldContent));
        EXPECT_TRUE(moved.CloseChecked(open.Value()).Succeeded());
    }

#if !defined(_WIN32)
    TEST_F(VFSDiskRootLeaseTest, ConcurrentSymlinkRetargetingCannotReadOrTruncateOutsideRoot)
    {
        std::error_code error = {};
        ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "outside", error));
        VFS::VFSPublicationRootLease outside = {};
        ASSERT_TRUE(Context.AcquirePublicationRoot(RootPath("/assets/outside"), &outside).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(outside, Relative("sentinel"), {OldContent, sizeof(OldContent)}).Succeeded());
        const auto visible = Directory.Path() / "output/link";
        const auto hidden  = Directory.Path() / "output/hidden-link";
        std::filesystem::create_symlink(Directory.Path() / "outside/sentinel", visible, error);
        ASSERT_FALSE(error);
        std::barrier start(2);
        bool         rename_succeeded = true;
        std::thread  retarget([&] {
            start.arrive_and_wait();
            std::error_code thread_error = {};
            for (uint32_t i = 0; i < 256; ++i)
            {
                std::filesystem::rename(visible, hidden, thread_error);
                if (thread_error)
                {
                    rename_succeeded = false;
                    break;
                }
                std::filesystem::rename(hidden, visible, thread_error);
                if (thread_error)
                {
                    rename_succeeded = false;
                    break;
                }
            }
        });
        start.arrive_and_wait();
        for (uint32_t i = 0; i < 256; ++i)
        {
            const auto open = Root.Open(Relative("link"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Truncate);
            EXPECT_TRUE(open.Failed());
            if (open.Succeeded())
            {
                (void) Root.CloseChecked(open.Value());
            }
            else
            {
                EXPECT_TRUE(open.Error() == VFS::VFSError::NotFound || open.Error() == VFS::VFSError::PermissionDenied);
            }
        }
        retarget.join();
        EXPECT_TRUE(rename_succeeded);
        ExpectBytes(outside, Relative("sentinel"), {OldContent, sizeof(OldContent)});
        EXPECT_EQ(VFS::WriteFileAtomically(Root, Relative("link"), {NewContent, sizeof(NewContent)}).Error(), VFS::VFSError::PermissionDenied);
        ExpectBytes(outside, Relative("sentinel"), {OldContent, sizeof(OldContent)});
    }
#endif

    TEST_F(VFSDiskRootLeaseTest, MoveTransfersPinExactlyOnce)
    {
        VFS::VFSPublicationRootLease moved(std::move(Root));
        EXPECT_FALSE(Root.IsValid());
        EXPECT_TRUE(moved.IsValid());
        EXPECT_EQ(Context.Unmount(RootPath("/assets")).Error(), VFS::VFSError::AlreadyExists);
        Root = std::move(moved);
        EXPECT_FALSE(moved.IsValid());
        moved.Reset();
        EXPECT_TRUE(VFS::WriteFileAtomically(Root, Relative("object"), {OldContent, sizeof(OldContent)}).Succeeded());
        Root.Reset();
        EXPECT_TRUE(Context.Unmount(RootPath("/assets")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, WrapperPoolIsBoundedAndRejectsForeignClose)
    {
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("object"), {OldContent, sizeof(OldContent)}).Succeeded());
        Core::Containers::FixedArray<VFS::IVFSFile*, 32> handles = {};
        for (auto& handle : handles)
        {
            const auto open = Root.Open(Relative("object"), VFS::VFSOpenFlags::Read);
            ASSERT_TRUE(open.Succeeded());
            handle = open.Value();
        }
        EXPECT_EQ(Root.Open(Relative("not-created"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_FALSE(Root.Exists(Relative("not-created")).Value());
        VFS::VFSScratchRootLease other = {};
        ASSERT_TRUE(Context.AcquireScratchRoot(RootPath("/assets/output"), &other).Succeeded());
        EXPECT_EQ(other.CloseChecked(handles[0]).Error(), VFS::VFSError::InvalidPath);
        for (auto* handle : handles)
        {
            EXPECT_TRUE(Root.CloseChecked(handle).Succeeded());
        }
        EXPECT_EQ(Root.CloseChecked(handles[0]).Error(), VFS::VFSError::InvalidPath);
        const auto again = Root.Open(Relative("object"), VFS::VFSOpenFlags::Read);
        ASSERT_TRUE(again.Succeeded());
        EXPECT_TRUE(Root.CloseChecked(again.Value()).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, PinCapacityFailureIsRecoverable)
    {
        Core::Containers::FixedArray<VFS::VFSScratchRootLease, 63> pins = {};
        for (auto& pin : pins)
        {
            ASSERT_TRUE(Context.AcquireScratchRoot(RootPath("/assets/output"), &pin).Succeeded());
        }
        VFS::VFSScratchRootLease overflow = {};
        EXPECT_EQ(Context.AcquireScratchRoot(RootPath("/assets/output"), &overflow).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_FALSE(overflow.IsValid());
        pins[0].Reset();
        EXPECT_TRUE(Context.AcquireScratchRoot(RootPath("/assets/output"), &overflow).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, ShutdownDefersPinnedMappingAndNativeState)
    {
        Context.Shutdown();
        VFS::VFSScratchRootLease denied = {};
        EXPECT_TRUE(Context.AcquireScratchRoot(RootPath("/assets/output"), &denied).Failed());
        EXPECT_EQ(Context.Mount(&Disk, RootPath("/assets"), 0).Error(), VFS::VFSError::Unsupported);
        ASSERT_TRUE(VFS::WriteFileAtomically(Root, Relative("object"), {NewContent, sizeof(NewContent)}).Succeeded());
        ExpectBytes(Root, Relative("object"), {NewContent, sizeof(NewContent)});
        EXPECT_TRUE(Root.Remove(Relative("object")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, OperationsSurviveBackendContextAndArenaDestruction)
    {
        VFS::VFSPublicationRootLease retained = {};
        {
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(2), {});
            VFS::VFSDiskBackend backend = {};
            const auto          native  = Directory.Path().generic_u8string();
            backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &memory.MainArena);
            VFS::VFSContext context = {};
            context.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(context.AcquirePublicationRoot(RootPath("/output"), &retained).Succeeded());
            context.Shutdown();
            memory.Shutdown();
        }
        ASSERT_TRUE(VFS::WriteFileAtomically(retained, Relative("object"), {NewContent, sizeof(NewContent)}).Succeeded());
        ExpectBytes(retained, Relative("object"), {NewContent, sizeof(NewContent)});
        EXPECT_TRUE(retained.Remove(Relative("object")).Succeeded());
    }

    TEST_F(VFSDiskRootLeaseTest, RetiredBackendCannotBeReacquiredThroughDanglingFacade)
    {
        VFS::VFSPublicationRootLease        retained = {};
        Helpers::Ref<VFS::IVFSRootProvider> observer = {};
        VFS::VFSContext                     context  = {};
        context.Initialize(&Memory.MainArena);
        {
            VFS::VFSDiskBackend backend = {};
            const auto          native  = Directory.Path().generic_u8string();
            backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &Memory.MainArena);
            observer = backend.RootProvider();
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(context.AcquirePublicationRoot(RootPath("/output"), &retained).Succeeded());
        }
        VFS::VFSScratchRootLease denied = {};
        EXPECT_EQ(context.AcquireScratchRoot(RootPath("/output"), &denied).Error(), VFS::VFSError::Unsupported);
        EXPECT_TRUE(VFS::WriteFileAtomically(retained, Relative("object"), {OldContent, sizeof(OldContent)}).Succeeded());
        context.Shutdown();
        retained.Reset();
        EXPECT_EQ(observer->RefCount(), 1); // Closed mapping releases its provider after the last pin.
    }

    TEST_F(VFSDiskRootLeaseTest, AcquireAndUnmountRaceHasOnlyPinnedOrUnmountedOutcomes)
    {
        for (uint32_t iteration = 0; iteration < 32; ++iteration)
        {
            Root.Reset();
            std::barrier         start(2);
            VFS::VFSResult<void> acquired  = VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
            VFS::VFSResult<void> unmounted = VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
            std::thread          reader([&] {
                start.arrive_and_wait();
                acquired = Context.AcquirePublicationRoot(RootPath("/assets/output"), &Root);
            });
            start.arrive_and_wait();
            unmounted = Context.Unmount(RootPath("/assets"));
            reader.join();
            if (acquired.Succeeded())
            {
                EXPECT_EQ(unmounted.Error(), VFS::VFSError::AlreadyExists);
                EXPECT_TRUE(VFS::WriteFileAtomically(Root, Relative("race"), {OldContent, sizeof(OldContent)}).Succeeded());
            }
            else
            {
                EXPECT_EQ(acquired.Error(), VFS::VFSError::NotFound);
                EXPECT_TRUE(unmounted.Succeeded());
                ASSERT_TRUE(Context.Mount(&Disk, RootPath("/assets"), 0).Succeeded());
            }
        }
    }
#endif
} // namespace ZEngine::Tests
