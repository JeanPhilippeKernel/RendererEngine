#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include "../TemporaryDirectory.h"

namespace ZEngine::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct PublicationFaultFile final : VFS::IVFSFile
        {
            VFS::IVFSFile*         File           = nullptr;
            size_t                 MaxChunk       = std::numeric_limits<size_t>::max();
            bool                   FailWrite      = false;
            bool                   FailFlush      = false;
            bool                   FailClose      = false;
            bool                   OversizedWrite = false;
            uint32_t               Writes         = 0;
            uint32_t               Flushes        = 0;
            uint32_t               Closes         = 0;
            uint32_t               FailWriteAt    = 0;
            size_t                 LargestRequest = 0;
            uint64_t               LastOffset     = 0;

            VFS::VFSResult<size_t> Read(Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) override
            {
                return File->Read(bytes, offset);
            }
            VFS::VFSResult<size_t> Write(Core::Containers::ArrayView<const uint8_t> bytes, uint64_t offset) override
            {
                ++Writes;
                LargestRequest = std::max(LargestRequest, bytes.size());
                LastOffset     = offset;
                if (FailWrite || Writes == FailWriteAt)
                {
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
                }
                if (OversizedWrite)
                {
                    return VFS::VFSResult<size_t>::Ok(bytes.size() + 1);
                }
                return File->Write({bytes.data(), std::min(MaxChunk, bytes.size())}, offset);
            }
            VFS::VFSResult<uint64_t> Size() const override
            {
                return File->Size();
            }
            VFS::VFSResult<VFS::VFSFileStat> Stat() const override
            {
                return File->Stat();
            }
            VFS::VFSResult<void> Flush() override
            {
                ++Flushes;
                return FailFlush ? VFS::VFSResult<void>::Fail(VFS::VFSError::IOError) : File->Flush();
            }
            const VFS::VFSPath& Path() const override
            {
                return File->Path();
            }
            VFS::VFSResult<void> Close() override
            {
                ++Closes;
                const auto result = File->Close();
                return FailClose ? VFS::VFSResult<void>::Fail(VFS::VFSError::IOError) : result;
            }
        };

        struct PublicationFaultContext final : VFS::VFSContext
        {
            PublicationFaultFile           File                   = {};
            VFS::VFSPath                   Temporary              = {};
            VFS::VFSError                  OpenError              = VFS::VFSError::OK;
            bool                           CreateUnownedTemporary = false;
            bool                           FailRename             = false;
            bool                           SawNonExclusiveWrite   = false;
            uint32_t                       Opens                  = 0;
            uint32_t                       Removes                = 0;
            uint32_t                       Renames                = 0;
            uint32_t                       NoReplaceRenames       = 0;
            uint32_t                       Releases               = 0;
            VFS::VFSError                  RemoveError            = VFS::VFSError::OK;
            bool                           NullOpen               = false;

            VFS::VFSResult<VFS::IVFSFile*> Open(const VFS::VFSPath& path, VFS::VFSOpenFlags flags) override
            {
                ++Opens;
                SawNonExclusiveWrite |= !VFS::HasFlag(flags, VFS::VFSOpenFlags::Exclusive) || VFS::HasFlag(flags, VFS::VFSOpenFlags::Truncate);
                Temporary             = path;
                if (CreateUnownedTemporary)
                {
                    const auto created = VFSContext::Open(path, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Exclusive);
                    if (created.Failed())
                    {
                        return created;
                    }
                    constexpr uint8_t sentinel[] = {9, 8, 7};
                    (void) created.Value()->Write({sentinel, sizeof(sentinel)}, 0);
                    VFSContext::Close(created.Value());
                }
                if (OpenError != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<VFS::IVFSFile*>::Fail(OpenError);
                }
                if (NullOpen)
                {
                    return VFS::VFSResult<VFS::IVFSFile*>::Ok(nullptr);
                }
                const auto opened = VFSContext::Open(path, flags);
                if (opened.Failed())
                {
                    return opened;
                }
                File.File = opened.Value();
                return VFS::VFSResult<VFS::IVFSFile*>::Ok(&File);
            }
            void Close(VFS::IVFSFile* file) override
            {
                ++Releases;
                VFSContext::Close(file == &File ? File.File : file);
                File.File = nullptr;
            }
            VFS::VFSResult<void> Remove(const VFS::VFSPath& path) override
            {
                ++Removes;
                return RemoveError != VFS::VFSError::OK ? VFS::VFSResult<void>::Fail(RemoveError) : VFSContext::Remove(path);
            }
            VFS::VFSResult<void> Rename(const VFS::VFSPath& from, const VFS::VFSPath& to) override
            {
                ++Renames;
                return FailRename ? VFS::VFSResult<void>::Fail(VFS::VFSError::IOError) : VFSContext::Rename(from, to);
            }
            VFS::VFSResult<void> RenameNoReplace(const VFS::VFSPath& from, const VFS::VFSPath& to) override
            {
                ++NoReplaceRenames;
                return FailRename ? VFS::VFSResult<void>::Fail(VFS::VFSError::IOError) : VFSContext::RenameNoReplace(from, to);
            }
        };
    } // namespace

    class VFSFileIOTest : public testing::TestWithParam<bool>
    {
    protected:
        Core::Memory::MemoryManager Memory        = {};
        TemporaryDirectory          Directory     = {};
        VFS::VFSDiskBackend         Disk          = {};
        VFS::VFSMemoryBackend       MemoryBackend = {};
        PublicationFaultContext     Context       = {};
        VFS::IVFSBackend*           Backend       = nullptr;
        VFS::VFSPath                Destination   = VFS::VFSPath::Parse("/manifest.json").Value();
        static constexpr uint8_t    OldBytes[]    = {1, 2, 3};
        static constexpr uint8_t    NewBytes[]    = {4, 5, 6, 7, 8};

        void                        SetUp() override
        {
            Memory.Initialize(ZMega(16), {});
            if (GetParam())
            {
                std::error_code error = {};
                ASSERT_TRUE(Directory.Create("zengine_atomic_write", error));
                const auto root = Directory.Path().generic_u8string();
                Disk.Initialize(reinterpret_cast<cstring>(root.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &Memory.MainArena);
                Backend = &Disk;
            }
            else
            {
                MemoryBackend.Initialize(&Memory.MainArena);
                Backend = &MemoryBackend;
            }
            Context.Initialize(&Memory.MainArena, 4);
            ASSERT_TRUE(Context.Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
        }
        void TearDown() override
        {
            Context.Shutdown();
        }

        void PutOriginal()
        {
            const auto open = Backend->Open(Destination, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Truncate);
            ASSERT_TRUE(open.Succeeded());
            ASSERT_TRUE(open.Value()->Write({OldBytes, sizeof(OldBytes)}, 0).Succeeded());
            Backend->Close(open.Value());
        }
        void ExpectBytes(const VFS::VFSPath& path, Core::Containers::ArrayView<const uint8_t> expected)
        {
            const auto open = Backend->Open(path, VFS::VFSOpenFlags::Read);
            ASSERT_TRUE(open.Succeeded());
            uint8_t    bytes[16] = {};
            const auto read      = open.Value()->ReadAll({bytes, sizeof(bytes)});
            Backend->Close(open.Value());
            ASSERT_TRUE(read.Succeeded());
            ASSERT_EQ(read.Value(), expected.size());
            EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), expected.data(), expected.size(), expected.size()), 0);
        }
        VFS::VFSResult<void> Publish(VFS::AtomicWriteMode mode = VFS::AtomicWriteMode::Replace)
        {
            return VFS::WriteFileAtomically(Context, Destination, {NewBytes, sizeof(NewBytes)}, mode);
        }
    };

    TEST_P(VFSFileIOTest, ReplacementCompletesShortWritesBeforeCheckedCloseAndRename)
    {
        PutOriginal();
        Context.File.MaxChunk = 2;
        ASSERT_TRUE(Publish().Succeeded());
        ExpectBytes(Destination, {NewBytes, sizeof(NewBytes)});
        EXPECT_EQ(Context.File.Writes, 3u);
        EXPECT_EQ(Context.File.Flushes, 1u);
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Releases, 1u);
        EXPECT_EQ(Context.Renames, 1u);
        EXPECT_EQ(Context.NoReplaceRenames, 0u);
        EXPECT_FALSE(Context.SawNonExclusiveWrite);
        EXPECT_FALSE(Backend->Exists(Context.Temporary));
    }

    TEST_P(VFSFileIOTest, NoReplacePreservesAnExistingObjectAndRemovesOnlyItsOwnTemporary)
    {
        PutOriginal();
        EXPECT_EQ(Publish(VFS::AtomicWriteMode::NoReplace).Error(), VFS::VFSError::AlreadyExists);
        ExpectBytes(Destination, {OldBytes, sizeof(OldBytes)});
        EXPECT_EQ(Context.Renames, 0u);
        EXPECT_EQ(Context.NoReplaceRenames, 1u);
        EXPECT_EQ(Context.Removes, 1u);
        EXPECT_FALSE(Backend->Exists(Context.Temporary));
    }

    TEST_P(VFSFileIOTest, NoReplaceInstallsAnAbsentObject)
    {
        ASSERT_TRUE(Publish(VFS::AtomicWriteMode::NoReplace).Succeeded());
        ExpectBytes(Destination, {NewBytes, sizeof(NewBytes)});
        EXPECT_EQ(Context.Renames, 0u);
        EXPECT_EQ(Context.NoReplaceRenames, 1u);
    }

    TEST_P(VFSFileIOTest, EveryPrecommitFailurePreservesOldDestinationAndClosesTheWrapper)
    {
        PutOriginal();
        for (uint32_t failure = 0; failure < 6; ++failure)
        {
            SCOPED_TRACE(failure);
            Context.File                = {};
            Context.File.FailWrite      = failure == 0;
            Context.File.MaxChunk       = failure == 1 ? 0 : std::numeric_limits<size_t>::max();
            Context.File.OversizedWrite = failure == 2;
            Context.File.FailFlush      = failure == 3;
            Context.File.FailClose      = failure == 4;
            Context.FailRename          = failure == 5;
            const auto result           = Publish();
            EXPECT_EQ(result.Error(), failure == 0 ? VFS::VFSError::PermissionDenied : VFS::VFSError::IOError);
            EXPECT_EQ(Context.File.Closes, 1u);
            EXPECT_EQ(Context.File.Flushes, failure < 3 ? 0u : 1u);
            EXPECT_EQ(Context.Releases, failure + 1);
            EXPECT_FALSE(Backend->Exists(Context.Temporary));
            ExpectBytes(Destination, {OldBytes, sizeof(OldBytes)});
        }
        EXPECT_EQ(Context.Renames, 1u);
    }

    TEST_P(VFSFileIOTest, FailedOpenNeverRemovesAnUnownedTemporary)
    {
        PutOriginal();
        Context.CreateUnownedTemporary = true;
        Context.OpenError              = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(Publish().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Context.Removes, 0u);
        EXPECT_EQ(Context.Releases, 0u);
        EXPECT_EQ(Context.Renames, 0u);
        constexpr uint8_t sentinel[] = {9, 8, 7};
        ExpectBytes(Context.Temporary, {sentinel, sizeof(sentinel)});
        ExpectBytes(Destination, {OldBytes, sizeof(OldBytes)});
    }

    TEST_P(VFSFileIOTest, ExclusiveCollisionsAreBoundedAndNeverDeleted)
    {
        Context.CreateUnownedTemporary = true;
        Context.OpenError              = VFS::VFSError::AlreadyExists;
        EXPECT_EQ(Publish().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Context.Opens, 8u);
        EXPECT_EQ(Context.Removes, 0u);
        EXPECT_EQ(Context.Releases, 0u);
        EXPECT_EQ(Context.Renames, 0u);
        EXPECT_FALSE(Backend->Exists(Destination));
        constexpr uint8_t sentinel[] = {9, 8, 7};
        ExpectBytes(Context.Temporary, {sentinel, sizeof(sentinel)});
    }

    TEST_P(VFSFileIOTest, InvalidModeAndNullDataFailBeforeAnyWrite)
    {
        EXPECT_EQ(Publish(static_cast<VFS::AtomicWriteMode>(255)).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(VFS::WriteFileAtomically(Context, Destination, {nullptr, 1}).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Context.Opens, 0u);
        EXPECT_TRUE(Context.CloseChecked(nullptr).Succeeded());
        EXPECT_EQ(Context.Releases, 0u);
    }

    TEST_P(VFSFileIOTest, StreamingWriterDefersPublicationAndCanChooseItsFinalSiblingAfterSealing)
    {
        PutOriginal();
        VFS::VFSContextAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        const auto temporary = writer.TemporaryPath();
        EXPECT_TRUE(writer.OwnsTemporary());
        EXPECT_FALSE(writer.IsSealed());
        EXPECT_FALSE(writer.IsCommitted());
        EXPECT_EQ(writer.Commit(Destination).Error(), VFS::VFSError::InvalidPath);
        Context.File.MaxChunk = 1;
        ASSERT_TRUE(writer.Write({NewBytes, 2}).Succeeded());
        ASSERT_TRUE(writer.Write({NewBytes + 2, sizeof(NewBytes) - 2}).Succeeded());
        EXPECT_EQ(writer.BytesWritten(), sizeof(NewBytes));
        EXPECT_EQ(Context.File.LastOffset, sizeof(NewBytes) - 1);
        EXPECT_EQ(writer.Open(Context, Destination).Error(), VFS::VFSError::InvalidPath);
        ExpectBytes(Destination, OldBytes);
        ASSERT_TRUE(writer.Seal().Succeeded());
        ASSERT_TRUE(writer.Seal().Succeeded());
        EXPECT_EQ(Context.File.Flushes, 1u);
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_TRUE(writer.IsSealed());
        EXPECT_TRUE(writer.OwnsTemporary());
        ExpectBytes(temporary, NewBytes);
        ExpectBytes(Destination, OldBytes);
        EXPECT_EQ(writer.Write(NewBytes).Error(), VFS::VFSError::InvalidPath);
        const auto final = VFS::VFSPath::Parse("/digest.object").Value();
        ASSERT_TRUE(writer.Commit(final, VFS::AtomicWriteMode::NoReplace).Succeeded());
        EXPECT_TRUE(writer.IsCommitted());
        EXPECT_FALSE(writer.OwnsTemporary());
        EXPECT_FALSE(Backend->Exists(temporary));
        EXPECT_EQ(writer.Commit(final).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(writer.Reset().Succeeded());
        ASSERT_TRUE(writer.Reset().Succeeded());
        EXPECT_EQ(Context.Removes, 0u);
        ExpectBytes(final, NewBytes);
        ExpectBytes(Destination, OldBytes);
    }

    TEST_P(VFSFileIOTest, StreamingChunksRemainBoundedAndEmptyStreamsCanBePublished)
    {
        VFS::VFSContextAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        Core::Containers::FixedArray<uint8_t, ZKilo(128) + 3> bytes = {};
        ASSERT_TRUE(writer.Write(bytes).Succeeded());
        EXPECT_EQ(writer.BytesWritten(), bytes.size());
        EXPECT_EQ(Context.File.LargestRequest, ZKilo(64));
        EXPECT_EQ(Context.File.Writes, 3u);
        ASSERT_TRUE(writer.Seal().Succeeded());
        ASSERT_TRUE(writer.Reset().Succeeded());
        EXPECT_FALSE(Backend->Exists(Destination));
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        EXPECT_TRUE(writer.Write({nullptr, 0}).Succeeded());
        ASSERT_TRUE(writer.Seal().Succeeded());
        ASSERT_TRUE(writer.Commit(Destination).Succeeded());
        EXPECT_EQ(Backend->Stat(Destination).Value().SizeBytes, 0u);
    }

    TEST_P(VFSFileIOTest, PartialStreamingFailurePoisonsTheStageButStillClosesAndCleansItOnce)
    {
        PutOriginal();
        VFS::VFSContextAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        const auto temporary     = writer.TemporaryPath();
        Context.File.MaxChunk    = 2;
        Context.File.FailWriteAt = 2;
        EXPECT_EQ(writer.Write(NewBytes).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(writer.BytesWritten(), 2u);
        EXPECT_EQ(writer.Write(NewBytes).Error(), VFS::VFSError::PermissionDenied);
        Context.File.FailClose = true;
        EXPECT_EQ(writer.Seal().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(writer.Seal().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(writer.Commit(Destination).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Context.File.Flushes, 0u);
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Renames, 0u);
        EXPECT_EQ(writer.Reset().Error(), VFS::VFSError::IOError);
        EXPECT_EQ(writer.CleanupError(), VFS::VFSError::IOError);
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Removes, 1u);
        EXPECT_FALSE(Backend->Exists(temporary));
        ExpectBytes(Destination, OldBytes);
    }

    TEST_P(VFSFileIOTest, InvalidStreamingDataPoisonsTheStageBeforeItCanBeCommitted)
    {
        VFS::VFSContextAtomicFileWriter writer = {};
        EXPECT_EQ(writer.Write(NewBytes).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(writer.Seal().Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(writer.Commit(Destination).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        EXPECT_EQ(writer.Write({nullptr, 1}).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(writer.Seal().Error(), VFS::VFSError::IOError);
        EXPECT_EQ(writer.Commit(Destination).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Context.File.Writes, 0u);
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Renames, 0u);
    }

    TEST_P(VFSFileIOTest, StreamingCommitRejectsOtherDirectoriesAndRetainsTheOwnedStageOnRenameFailure)
    {
        PutOriginal();
        VFS::VFSContextAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        ASSERT_TRUE(writer.Write(NewBytes).Succeeded());
        ASSERT_TRUE(writer.Seal().Succeeded());
        EXPECT_EQ(writer.Commit(VFS::VFSPath::Parse("/other/manifest.json").Value()).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(writer.Commit(writer.TemporaryPath()).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(writer.Commit(VFS::VFSPath::Root()).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(writer.Commit({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(writer.Commit(Destination, static_cast<VFS::AtomicWriteMode>(255)).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Context.Renames, 0u);
        EXPECT_EQ(writer.Commit(Destination, VFS::AtomicWriteMode::NoReplace).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(writer.OwnsTemporary());
        EXPECT_TRUE(writer.IsSealed());
        ExpectBytes(writer.TemporaryPath(), NewBytes);
        ExpectBytes(Destination, OldBytes);
        Context.FailRename = true;
        EXPECT_EQ(writer.Commit(Destination).Error(), VFS::VFSError::IOError);
        EXPECT_TRUE(writer.OwnsTemporary());
        EXPECT_EQ(Context.Removes, 0u);
        Context.FailRename = false;
        ASSERT_TRUE(writer.Commit(Destination).Succeeded());
        ExpectBytes(Destination, NewBytes);
    }

    TEST_P(VFSFileIOTest, StreamingCleanupFailureIsObservableAndNeverRetriedAfterAuthorityIsReleased)
    {
        PutOriginal();
        VFS::VFSPath orphan = {};
        {
            VFS::VFSContextAtomicFileWriter writer = {};
            ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
            orphan = writer.TemporaryPath();
            ASSERT_TRUE(writer.Write(NewBytes).Succeeded());
            Context.File.FailClose = true;
            Context.RemoveError    = VFS::VFSError::PermissionDenied;
            EXPECT_EQ(writer.Reset().Error(), VFS::VFSError::IOError);
            EXPECT_EQ(writer.CleanupError(), VFS::VFSError::IOError);
            EXPECT_FALSE(writer.OwnsTemporary());
            EXPECT_FALSE(writer.TemporaryPath().IsValid());
            EXPECT_EQ(writer.Reset().Error(), VFS::VFSError::IOError);
        }
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Removes, 1u);
        EXPECT_TRUE(Backend->Exists(orphan));
        ExpectBytes(Destination, OldBytes);
        ExpectBytes(orphan, NewBytes);
    }

    TEST_P(VFSFileIOTest, StreamingRemovalFailureReportsItsNativeErrorWithoutDeletingTheDestination)
    {
        PutOriginal();
        VFS::VFSContextAtomicFileWriter writer = {};
        ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
        ASSERT_TRUE(writer.Write(NewBytes).Succeeded());
        ASSERT_TRUE(writer.Seal().Succeeded());
        const auto temporary = writer.TemporaryPath();
        Context.RemoveError  = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(writer.Reset().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(writer.CleanupError(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Context.Removes, 1u);
        ExpectBytes(temporary, NewBytes);
        ExpectBytes(Destination, OldBytes);
    }

    TEST_P(VFSFileIOTest, StreamingDestructorAbortsOnlyItsOwnedUncommittedTemporary)
    {
        PutOriginal();
        VFS::VFSPath temporary = {};
        {
            VFS::VFSContextAtomicFileWriter writer = {};
            ASSERT_TRUE(writer.Open(Context, Destination).Succeeded());
            temporary = writer.TemporaryPath();
            ASSERT_TRUE(writer.Write(NewBytes).Succeeded());
        }
        EXPECT_FALSE(Backend->Exists(temporary));
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Removes, 1u);
        ExpectBytes(Destination, OldBytes);
    }

    TEST_P(VFSFileIOTest, SuccessfulOpenWithoutAWrapperNeverGrantsCleanupAuthority)
    {
        Context.NullOpen = true;
        EXPECT_EQ(Publish().Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Context.Opens, 1u);
        EXPECT_EQ(Context.Removes, 0u);
        EXPECT_EQ(Context.Renames, 0u);
        EXPECT_EQ(Context.Releases, 0u);
    }

    TEST_P(VFSFileIOTest, AllowReplaceIsReadOnlyAndUnsupportedBackendsFailClosed)
    {
        PutOriginal();
        const auto replace = VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace;
        for (const auto flag : {VFS::VFSOpenFlags::Write, VFS::VFSOpenFlags::Create, VFS::VFSOpenFlags::Append, VFS::VFSOpenFlags::Truncate, VFS::VFSOpenFlags::Exclusive})
        {
            EXPECT_EQ(Backend->Open(Destination, replace | flag).Error(), VFS::VFSError::Unsupported);
        }
        EXPECT_EQ(Backend->Open(Destination, VFS::VFSOpenFlags::AllowReplace).Error(), VFS::VFSError::Unsupported);
        if (!GetParam())
        {
            EXPECT_EQ(Backend->Open(Destination, replace).Error(), VFS::VFSError::Unsupported);
        }
        ExpectBytes(Destination, {OldBytes, sizeof(OldBytes)});
    }

    TEST_P(VFSFileIOTest, EmbeddedIdentityReadPropagatesCheckedCloseAndPreservesEarlierReadErrors)
    {
        const auto     path       = VFS::VFSPath::Parse("/asset.zematerial").Value();
        constexpr char material[] = "{\"uuid\":\"11111111-1111-4111-8111-111111111111\"}";
        const auto     open       = Backend->Open(path, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create);
        ASSERT_TRUE(open.Succeeded());
        EXPECT_TRUE(open.Value()->Write({reinterpret_cast<const uint8_t*>(material), sizeof(material) - 1}, 0).Succeeded());
        Backend->Close(open.Value());
        Context.File.FailClose = true;
        EXPECT_EQ(VFS::ReadEmbeddedAssetUUID(Context, path).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Context.File.Closes, 1u);
        EXPECT_EQ(Context.Releases, 1u);
        const auto truncated = Backend->Open(path, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Truncate);
        ASSERT_TRUE(truncated.Succeeded());
        Backend->Close(truncated.Value());
        EXPECT_EQ(VFS::ReadEmbeddedAssetUUID(Context, path).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Context.File.Closes, 2u);
        EXPECT_EQ(Context.Releases, 2u);
    }

    TEST_P(VFSFileIOTest, DiskReplaceCompatibleReaderRetainsOldBytesAcrossPublication)
    {
        if (!GetParam())
        {
            GTEST_SKIP() << "Borrowed-memory mode intentionally does not support snapshots; owned-memory coverage is in VFSMemoryRootLeaseTest.";
        }
        ASSERT_TRUE(Backend->CreateDir(VFS::VFSPath::Parse("/nested").Value()).Succeeded());
        for (const auto name : {"/manifest.json", reinterpret_cast<cstring>(u8"/nested/資料_𠮷.json")})
        {
            SCOPED_TRACE(name);
            Destination = VFS::VFSPath::Parse(name).Value();
            PutOriginal();
            const auto open = Backend->Open(Destination, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
            ASSERT_TRUE(open.Succeeded());
            const auto published = Publish();
            ASSERT_TRUE(published.Succeeded()) << static_cast<uint32_t>(published.Error());
            const auto next = Backend->Open(Destination, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
            ASSERT_TRUE(next.Succeeded());
            const auto republished = VFS::WriteFileAtomically(Context, Destination, OldBytes);
            ASSERT_TRUE(republished.Succeeded()) << static_cast<uint32_t>(republished.Error());

            // Read both retained generations only after the second commit.
            uint8_t    bytes[16] = {};
            const auto read      = open.Value()->ReadAll(bytes);
            const auto close     = open.Value()->Close();
            EXPECT_TRUE(open.Value()->Close().Succeeded());
            EXPECT_EQ(open.Value()->Read(bytes, 0).Error(), VFS::VFSError::IOError);
            Backend->Close(open.Value());
            EXPECT_TRUE(close.Succeeded());
            ASSERT_TRUE(read.Succeeded());
            EXPECT_EQ(read.Value(), sizeof(OldBytes));
            EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), OldBytes, sizeof(OldBytes), sizeof(OldBytes)), 0);
            const auto next_read = next.Value()->ReadAll(bytes);
            Backend->Close(next.Value());
            ASSERT_TRUE(next_read.Succeeded());
            EXPECT_EQ(next_read.Value(), sizeof(NewBytes));
            EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), NewBytes, sizeof(NewBytes), sizeof(NewBytes)), 0);
            ExpectBytes(Destination, OldBytes);
        }
    }

    INSTANTIATE_TEST_SUITE_P(MemoryAndDisk, VFSFileIOTest, testing::Bool());
} // namespace ZEngine::Tests
