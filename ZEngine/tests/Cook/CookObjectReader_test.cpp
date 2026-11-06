#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <filesystem>
#include "../TemporaryDirectory.h"
#if defined(__APPLE__) || defined(__linux__)
#include <sys/stat.h>
#endif
#include "CookObjectTestSupport.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct ObjectBudgetAllocator final : ICookAllocator
        {
            CookMemoryLedger     Ledger      = {};
            VFS::VFSError        Failure     = VFS::VFSError::OK;
            uint64_t             Requested   = 0;
            uint32_t             ChargeCalls = 0;
            uint32_t             Allocations = 0;

            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++ChargeCalls;
                Requested = bytes;
                EXPECT_EQ(category, CookMemoryCategory::StagingManifest);
                return Failure == VFS::VFSError::OK ? Ledger.TryCharge(bytes, category, output) : Failure == VFS::VFSError::Unsupported ? ICookAllocator::TryCharge(bytes, category, output) : VFS::VFSResult<void>::Fail(Failure);
            }
            VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ++Allocations;
                ADD_FAILURE() << "Object verification must not allocate.";
                return VFS::VFSResult<void*>::Fail(VFS::VFSError::Unsupported);
            }
            void Free(void*) noexcept override
            {
                ADD_FAILURE() << "Object verification owns no allocated block.";
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Ledger.Statistics();
            }
        };
    } // namespace

    class CookObjectReaderTest : public testing::Test
    {
    protected:
        ObjectBudgetAllocator Allocator  = {};
        Tests::ObjectStore    Store      = {};
        uint8_t               Stream[17] = {};
        CookObjectDescriptor  Object     = {};

        void                  SetUp() override
        {
            ASSERT_TRUE(Allocator.Ledger.Initialize(ZKilo(4)).Succeeded());
            uint8_t bytes[128] = {};
            for (uint32_t i = 0; i < sizeof(bytes); ++i)
            {
                bytes[i] = static_cast<uint8_t>(i);
            }
            Object = Store.Entries[Store.Add(bytes, "png")].Object;
        }
        void TearDown() override
        {
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            EXPECT_EQ(Allocator.Allocations, 0u);
        }
        VFS::VFSResult<bool> Verify()
        {
            return VerifyCookObject(Store.Reader(), Object, "png", Stream, Allocator);
        }
    };

    TEST_F(CookObjectReaderTest, StreamsExactBytesAndShortReadsUsingTheCallerBuffer)
    {
        for (const size_t limit : {size_t(1), size_t(7), SIZE_MAX})
        {
            auto& file        = Store.Entries[0].File;
            file.ReadLimit    = limit;
            const auto result = Verify();
            ASSERT_TRUE(result.Succeeded());
            EXPECT_TRUE(result.Value());
            EXPECT_EQ(file.Stats, 2u);
        }
        ASSERT_TRUE(VerifyCookObject(Store.Reader(), Object, "png", {Stream, 1}, Allocator).Value());
        EXPECT_EQ(Store.Entries[0].File.Closes, 4u);
    }

    TEST_F(CookObjectReaderTest, OnlyAnOpenNotFoundIsAMiss)
    {
        auto& entry        = Store.Entries[0];
        entry.OpenError    = VFS::VFSError::NotFound;
        const auto missing = Verify();
        ASSERT_TRUE(missing.Succeeded());
        EXPECT_FALSE(missing.Value());
        EXPECT_EQ(entry.File.Closes, 0u);
        for (const auto error : {VFS::VFSError::IOError, VFS::VFSError::PermissionDenied, VFS::VFSError::Unsupported, VFS::VFSError::NotADirectory, VFS::VFSError::NotAFile})
        {
            entry.OpenError = error;
            EXPECT_EQ(Verify().Error(), error == VFS::VFSError::NotAFile ? VFS::VFSError::Corrupted : error);
        }
        EXPECT_EQ(entry.File.Closes, 0u);
    }

    TEST_F(CookObjectReaderTest, EveryOpenedFailureIsCheckedClosedAndNeverBecomesAMiss)
    {
        auto& file = Store.Entries[0].File;
        for (const auto error : {VFS::VFSError::NotFound, VFS::VFSError::IOError, VFS::VFSError::PermissionDenied, VFS::VFSError::Cancelled})
        {
            file.ReadFailureAt = 0;
            file.ReadError     = error;
            EXPECT_EQ(Verify().Error(), error);
            file.ReadFailureAt = UINT64_MAX;
            file.StatError     = error;
            file.StatFailureAt = 1;
            EXPECT_EQ(Verify().Error(), error);
            file.StatFailureAt = 2;
            EXPECT_EQ(Verify().Error(), error);
            file.StatError  = VFS::VFSError::OK;
            file.CloseError = error;
            EXPECT_EQ(Verify().Error(), error);
            file.CloseError = VFS::VFSError::OK;
        }
        EXPECT_EQ(file.Closes, 16u);
    }

    TEST_F(CookObjectReaderTest, CorruptionCannotBeHiddenByAnOtherwiseSuccessfulRead)
    {
        auto& entry     = Store.Entries[0];
        auto& file      = entry.File;
        entry.Bytes[0] ^= 1;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        entry.Bytes[0] ^= 1;
        file.EOFAt      = Object.Size - 1;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        file.EOFAt      = UINT64_MAX;
        file.Overreport = true;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        file.Overreport  = false;
        file.ChangedSize = Object.Size + 1;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        file.ChangedSize = UINT64_MAX;
        file.Directory   = true;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        file.Directory        = false;
        file.ChangedDirectory = true;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(file.Closes, 6u);
    }

    TEST_F(CookObjectReaderTest, DetectsSizeMismatchAndTrailingBytesEvenIfStatMatches)
    {
        auto& file   = Store.Entries[0].File;
        auto  object = Object;
        --object.Size;
        EXPECT_EQ(VerifyCookObject(Store.Reader(), object, "png", Stream, Allocator).Error(), VFS::VFSError::Corrupted);
        // First stat now reports the smaller descriptor's size, but the handle
        // exposes another byte at expected EOF. No second stat is needed.
        file.Stats       = 1;
        file.ChangedSize = object.Size;
        EXPECT_EQ(VerifyCookObjectFile(file, object, Stream, Allocator).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(file.Stats, 2u);
    }

    TEST_F(CookObjectReaderTest, ChecksEOFReadFailuresAndPreservesPrimaryFailureOverCloseError)
    {
        auto& file         = Store.Entries[0].File;
        file.ReadFailureAt = Object.Size;
        file.ReadError     = VFS::VFSError::NotFound;
        file.CloseError    = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::NotFound);
        file.ReadFailureAt = 0;
        file.ReadError     = VFS::VFSError::Corrupted;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(file.Closes, 2u);
    }

    TEST_F(CookObjectReaderTest, RejectsInvalidAuthorityWorkspaceAndSpellingBeforeOpen)
    {
        Allocator.Failure = VFS::VFSError::Unsupported;
        auto reader       = Store.Reader();
        EXPECT_EQ(VerifyCookObject({}, Object, "png", Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookObject(reader, Object, "png", {}, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookObject(reader, Object, "png", {nullptr, 1}, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookObject(reader, Object, "jpg", Stream, Allocator).Error(), VFS::VFSError::Corrupted);
        auto changed             = Object;
        changed.Digest.Bytes[0] ^= 1;
        EXPECT_EQ(VerifyCookObject(reader, changed, "png", Stream, Allocator).Error(), VFS::VFSError::Corrupted);
        changed      = Object;
        changed.Path = {};
        EXPECT_EQ(VerifyCookObject(reader, changed, "png", Stream, Allocator).Error(), VFS::VFSError::Corrupted);
        changed      = Object;
        changed.Size = 0;
        EXPECT_EQ(VerifyCookObject(reader, changed, "png", Stream, Allocator).Error(), VFS::VFSError::Corrupted);
        reader.CloseChecked = nullptr;
        EXPECT_EQ(VerifyCookObject(reader, Object, "png", Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Store.Opens, 0u);
        EXPECT_EQ(Allocator.ChargeCalls, 0u);
    }

    TEST_F(CookObjectReaderTest, SuccessfulOpenWithoutAHandleFailsClosed)
    {
        Store.Entries[0].NullHandle = true;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Store.Entries[0].File.Closes, 0u);
    }

    TEST_F(CookObjectReaderTest, StagedVerificationUsesTheActualTemporaryPathRatherThanTheFinalDescriptorPath)
    {
        const auto temporary            = VFS::VFSRelativePath::Parse("objects/staged.tmp").Value();
        Store.Entries[0].Object.Path    = temporary;
        Store.Entries[0].File.ReadLimit = 3;
        ASSERT_TRUE(VerifyCookStagedObject(Store.Reader(), temporary, Object, Stream, Allocator).Succeeded());
        EXPECT_EQ(Store.Entries[0].File.Closes, 1u);
        // A missing stage is an error, never an incremental-reuse miss.
        EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), Object.Path, Object, Stream, Allocator).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Store.Entries[0].File.Closes, 1u);
    }

    TEST_F(CookObjectReaderTest, StagedVerificationChecksActualBytesAndCloseAndRejectsInvalidArgumentsBeforeOpen)
    {
        const auto verify      = [&]() { return VerifyCookStagedObject(Store.Reader(), Object.Path, Object, Stream, Allocator); };
        auto&      entry       = Store.Entries[0];
        entry.Bytes[0]        ^= 1;
        entry.File.CloseError  = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(verify().Error(), VFS::VFSError::Corrupted);
        entry.Bytes[0] ^= 1;
        EXPECT_EQ(verify().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(entry.File.Closes, 2u);
        entry.NullHandle = true;
        EXPECT_EQ(verify().Error(), VFS::VFSError::IOError);
        const auto opens   = Store.Opens;
        const auto charges = Allocator.ChargeCalls;
        Allocator.Failure  = VFS::VFSError::Unsupported;
        EXPECT_EQ(VerifyCookStagedObject({}, Object.Path, Object, Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), {}, Object, Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), Object.Path, Object, {}, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), Object.Path, Object, {nullptr, 1}, Allocator).Error(), VFS::VFSError::InvalidPath);
        auto invalid = Object;
        invalid.Size = 0;
        EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), Object.Path, invalid, Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        invalid.Size = ZGiga(16) + uint64_t(1);
        EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), Object.Path, invalid, Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Store.Opens, opens);
        EXPECT_EQ(Allocator.ChargeCalls, charges);
    }

    TEST_F(CookObjectReaderTest, EveryEntryPointChargesExactPrincipalStateAndRefusesOneByteBelowBeforeIO)
    {
        ASSERT_TRUE(Verify().Value());
        const uint64_t required = Allocator.Requested;
        ASSERT_GE(required, Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(CookMemoryCharge));
        EXPECT_EQ(Allocator.Statistics().PeakBytes, required);
        for (uint32_t entry = 0; entry < 3; ++entry)
        {
            const auto verify = [&]() {
                if (entry == 0)
                {
                    return VerifyCookObjectFile(Store.Entries[0].File, Object, Stream, Allocator);
                }
                if (entry == 1)
                {
                    return VerifyCookStagedObject(Store.Reader(), Object.Path, Object, Stream, Allocator);
                }
                const auto result = Verify();
                EXPECT_TRUE(result.Failed() || result.Value());
                return result.Failed() ? VFS::VFSResult<void>::Fail(result.Error()) : VFS::VFSResult<void>::Ok();
            };
            for (const uint64_t budget : {required - 1, required})
            {
                ASSERT_TRUE(Allocator.Ledger.Initialize(budget).Succeeded());
                const auto opens   = Store.Opens;
                const auto reads   = Store.Entries[0].File.Reads;
                const auto stats   = Store.Entries[0].File.Stats;
                const auto closes  = Store.Entries[0].File.Closes;
                const auto charges = Allocator.ChargeCalls;
                const auto result  = verify();
                EXPECT_EQ(Allocator.ChargeCalls, charges + 1);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
                if (budget < required)
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                    EXPECT_EQ(Store.Opens, opens);
                    EXPECT_EQ(Store.Entries[0].File.Reads, reads);
                    EXPECT_EQ(Store.Entries[0].File.Stats, stats);
                    EXPECT_EQ(Store.Entries[0].File.Closes, closes);
                    EXPECT_EQ(Allocator.Statistics().PeakBytes, 0u);
                }
                else
                {
                    EXPECT_TRUE(result.Succeeded());
                    EXPECT_EQ(Store.Opens, opens + (entry == 0 ? 0 : 1));
                    EXPECT_GT(Store.Entries[0].File.Reads, reads);
                    EXPECT_EQ(Store.Entries[0].File.Stats, entry == 0 ? stats + 2 : 2);
                    EXPECT_EQ(Store.Entries[0].File.Closes, closes + (entry == 0 ? 0 : 1));
                    EXPECT_EQ(Allocator.Statistics().PeakBytes, required);
                }
            }
        }
    }

    TEST_F(CookObjectReaderTest, UnknownAndFailedFixedContractsNeverOpenStatOrRead)
    {
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            Allocator.Failure = error;
            EXPECT_EQ(Verify().Error(), error);
            EXPECT_EQ(VerifyCookObjectFile(Store.Entries[0].File, Object, Stream, Allocator).Error(), error);
            EXPECT_EQ(VerifyCookStagedObject(Store.Reader(), Object.Path, Object, Stream, Allocator).Error(), error);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
        EXPECT_EQ(Allocator.ChargeCalls, 9u);
        EXPECT_EQ(Store.Opens, 0u);
        EXPECT_EQ(Store.Entries[0].File.Stats, 0u);
        EXPECT_EQ(Store.Entries[0].File.Reads, 0u);
        EXPECT_EQ(Store.Entries[0].File.Closes, 0u);
    }

    TEST_F(CookObjectReaderTest, ReservationIsHeldThroughOpenAndCheckedCloseWithoutANestedCharge)
    {
        ASSERT_TRUE(Verify().Value());
        const uint64_t required = Allocator.Requested;
        ASSERT_TRUE(Allocator.Ledger.Initialize(required).Succeeded());
        struct ObservedReader
        {
            Tests::ObjectStore*    Store     = nullptr;
            ObjectBudgetAllocator* Allocator = nullptr;
            uint64_t               Required  = 0;
            uint32_t               Opens     = 0;
            uint32_t               Closes    = 0;
        } observed                     = {&Store, &Allocator, required};
        const CookObjectReadFns reader = {
            &observed,
            [](void* context, const VFS::VFSRelativePath& path) {
                auto& state = *static_cast<ObservedReader*>(context);
                ++state.Opens;
                EXPECT_EQ(state.Allocator->Statistics().LiveBytes, state.Required);
                EXPECT_EQ(state.Allocator->Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::StagingManifest)], state.Required);
                const auto inner = state.Store->Reader();
                return inner.Open(inner.Context, path);
            },
            [](void* context, VFS::IVFSFile* file) {
                auto& state = *static_cast<ObservedReader*>(context);
                ++state.Closes;
                EXPECT_EQ(state.Allocator->Statistics().LiveBytes, state.Required);
                const auto inner = state.Store->Reader();
                return inner.CloseChecked(inner.Context, file);
            }};
        const auto calls = Allocator.ChargeCalls;
        EXPECT_TRUE(VerifyCookObject(reader, Object, "png", Stream, Allocator).Value());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        Store.Entries[0].File.ReadFailureAt = 0;
        Store.Entries[0].File.CloseError    = VFS::VFSError::IOError;
        EXPECT_EQ(VerifyCookStagedObject(reader, Object.Path, Object, Stream, Allocator).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        Store.Entries[0].OpenError = VFS::VFSError::NotFound;
        EXPECT_FALSE(VerifyCookObject(reader, Object, "png", Stream, Allocator).Value());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(Allocator.ChargeCalls, calls + 3);
        EXPECT_EQ(observed.Opens, 3u);
        EXPECT_EQ(observed.Closes, 2u);
        EXPECT_EQ(Allocator.Statistics().PeakBytes, required);
    }

    TEST_F(CookObjectReaderTest, DirectFilePreflightDoesNotQueryAnUnknownFixedContract)
    {
        Allocator.Failure = VFS::VFSError::Unsupported;
        auto& file        = Store.Entries[0].File;
        EXPECT_EQ(VerifyCookObjectFile(file, Object, {}, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(VerifyCookObjectFile(file, Object, {nullptr, 1}, Allocator).Error(), VFS::VFSError::InvalidPath);
        auto invalid = Object;
        invalid.Size = 0;
        EXPECT_EQ(VerifyCookObjectFile(file, invalid, Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        invalid.Size = ZGiga(16) + uint64_t(1);
        EXPECT_EQ(VerifyCookObjectFile(file, invalid, Stream, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.ChargeCalls, 0u);
        EXPECT_EQ(file.Stats, 0u);
        EXPECT_EQ(file.Reads, 0u);
    }

    TEST_F(CookObjectReaderTest, RetainedOuterStateRemainsChargedAndAllVerificationFailuresDrainOnlyTheirOwnCharge)
    {
        ASSERT_TRUE(Verify().Value());
        const uint64_t required = Allocator.Requested;
        ASSERT_TRUE(Allocator.Ledger.Initialize(required + 64).Succeeded());
        CookMemoryCharge outer = {};
        ASSERT_TRUE(Allocator.Ledger.TryCharge(64, CookMemoryCategory::Captures, &outer).Succeeded());
        EXPECT_TRUE(Verify().Value());
        auto& file         = Store.Entries[0].File;
        file.ReadFailureAt = 0;
        file.CloseError    = VFS::VFSError::IOError;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 64u);
        EXPECT_EQ(Allocator.Statistics().PeakBytes, required + 64);
        EXPECT_EQ(Allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::StagingManifest)], 0u);
        file.ReadFailureAt = UINT64_MAX;
        EXPECT_EQ(Verify().Error(), VFS::VFSError::IOError);
        Store.Entries[0].OpenError = VFS::VFSError::NotFound;
        ASSERT_FALSE(Verify().Value());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 64u);
        EXPECT_EQ(file.Closes, 4u);
    }

    TEST_F(CookObjectReaderTest, ReadAllRejectsInvalidBuffersAndOverreportedCountsWithoutAdvancing)
    {
        auto& file = Store.Entries[0].File;
        EXPECT_EQ(file.ReadAll({nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        ASSERT_EQ(file.ReadAll({}).Value(), 0u);
        EXPECT_EQ(file.Reads, 0u);
        file.Overreport = true;
        EXPECT_EQ(file.ReadAll(Stream).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(file.Reads, 1u);
        file.Overreport = false;
        file.ReadLimit  = 2;
        ASSERT_EQ(file.ReadAll(Stream).Value(), sizeof(Stream));
        for (size_t i = 0; i < sizeof(Stream); ++i)
        {
            EXPECT_EQ(Stream[i], i);
        }
    }

    TEST(CookObjectRootReaderTest, MemoryPublicationAndReadRootsRetainRoutingAfterContextShutdown)
    {
        Tests::FixedAllocator<ZKilo(1), 1> Allocator = {};
        Core::Memory::MemoryManager        memory    = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSMemoryBackend backend = {};
        ASSERT_TRUE(backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        const auto root_path = VFS::VFSPath::Parse("/output").Value();
        ASSERT_TRUE(backend.CreateDir(root_path).Succeeded());
        VFS::VFSContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease root = {};
        VFS::VFSReadRootLease        read = {};
        ASSERT_TRUE(context.AcquirePublicationRoot(root_path, &root).Succeeded());
        ASSERT_TRUE(context.AcquireReadRoot(root_path, &read).Succeeded());
        Tests::ObjectStore store   = {};
        constexpr uint8_t  bytes[] = {1, 2, 3, 4, 5};
        const auto         object  = store.Entries[store.Add(bytes, "png")].Object;
        ASSERT_TRUE(root.CreateDirExclusive(VFS::VFSRelativePath::Parse("objects").Value()).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(root, object.Path, bytes, VFS::AtomicWriteMode::NoReplace).Succeeded());
        context.Shutdown();
        uint8_t stream[2] = {};
        ASSERT_TRUE(VerifyCookObject(CookObjectReader(root), object, "png", stream, Allocator).Value());
        ASSERT_TRUE(VerifyCookObject(CookObjectReader(read), object, "png", stream, Allocator).Value());
    }

#if defined(__APPLE__) || defined(__linux__)
    TEST(CookObjectRootReaderTest, DiskRootsVerifyBytesAndRejectSymlinksAndSpecialFiles)
    {
        Tests::FixedAllocator<ZKilo(1), 1> Allocator = {};
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("zengine_cook_object_reader", error));
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSDiskBackend backend = {};
        const auto          native  = directory.Path().generic_u8string();
        backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &memory.MainArena);
        VFS::VFSContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease root = {};
        VFS::VFSReadRootLease        read = {};
        ASSERT_TRUE(context.AcquirePublicationRoot(VFS::VFSPath::Root(), &root).Succeeded());
        ASSERT_TRUE(context.AcquireReadRoot(VFS::VFSPath::Root(), &read).Succeeded());
        Tests::ObjectStore store   = {};
        constexpr uint8_t  bytes[] = {6, 7, 8, 9};
        const auto         object  = store.Entries[store.Add(bytes, "png")].Object;
        ASSERT_TRUE(root.CreateDirExclusive(VFS::VFSRelativePath::Parse("objects").Value()).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(root, object.Path, bytes, VFS::AtomicWriteMode::NoReplace).Succeeded());
        context.Shutdown();
        uint8_t stream[3] = {};
        ASSERT_TRUE(VerifyCookObject(CookObjectReader(root), object, "png", stream, Allocator).Value());
        ASSERT_TRUE(VerifyCookObject(CookObjectReader(read), object, "png", stream, Allocator).Value());
        ASSERT_TRUE(root.RenameNoReplace(object.Path, VFS::VFSRelativePath::Parse("saved").Value()).Succeeded());
        std::filesystem::create_symlink(directory.Path() / "saved", directory.Path() / object.Path.CStr(), error);
        ASSERT_FALSE(error);
        EXPECT_TRUE(VerifyCookObject(CookObjectReader(root), object, "png", stream, Allocator).Failed());
        EXPECT_TRUE(VerifyCookObject(CookObjectReader(read), object, "png", stream, Allocator).Failed());
        ASSERT_TRUE(std::filesystem::remove(directory.Path() / object.Path.CStr(), error));
        ASSERT_TRUE(root.CreateDirExclusive(object.Path).Succeeded());
        EXPECT_EQ(VerifyCookObject(CookObjectReader(root), object, "png", stream, Allocator).Error(), VFS::VFSError::Corrupted);
        ASSERT_TRUE(root.Remove(object.Path).Succeeded());
        ASSERT_EQ(::mkfifo((directory.Path() / object.Path.CStr()).c_str(), 0600), 0);
        EXPECT_TRUE(VerifyCookObject(CookObjectReader(root), object, "png", stream, Allocator).Failed());
        EXPECT_TRUE(VerifyCookObject(CookObjectReader(read), object, "png", stream, Allocator).Failed());
    }
#endif
} // namespace ZEngine::Cook
