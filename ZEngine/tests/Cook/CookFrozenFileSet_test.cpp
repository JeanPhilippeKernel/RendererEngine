#include <ZEngine/Cook/CookFrozenFileSet.h>
#include <ZEngine/Cook/CookLimits.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include "../TemporaryDirectory.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSPath FileSetPath(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }
        VFS::VFSRelativePath FileSetRelative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }
        constexpr uint8_t FileSetOld[] = {1, 2, 3, 4};
        constexpr uint8_t FileSetNew[] = {5, 6, 7, 8};
    } // namespace

    class CookFrozenFileSetTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager  Memory           = {};
        VFS::VFSMemoryBackend        Backend          = {};
        VFS::VFSContext              Context          = {};
        FixedAllocator<ZKilo(64), 4> Allocator        = {};
        CookFrozenLibraryOwner       Owner            = {};
        CookFrozenLibraryReadLease   Read             = {};
        VFS::VFSScratchRootLease     Scratch          = {};
        CookLimits                   Limits           = {.MaxWorkingBytes = ZKilo(64)};
        CookCaptureStore             Store            = {};
        CookFrozenFileSet            Files            = {};
        uint8_t                      Buffer[ZKilo(4)] = {};

        void                         SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(2), 128).Succeeded());
            for (cstring path : {"/assets/nested", "/scenes", "/scratch"})
            {
                ASSERT_TRUE(Backend.CreateDir(FileSetPath(path)).Succeeded());
            }
            for (cstring path : {"/assets/a.png", "/assets/nested/b.zemesh", "/scenes/level.scene"})
            {
                ASSERT_TRUE(Backend.WriteFile(FileSetPath(path), FileSetOld).Succeeded());
            }
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            const CookFrozenRootRequest roots[] = {
                {.Key = "scenes", .Context = &Context, .Path = FileSetPath("/scenes")},
                {.Key = "assets", .Context = &Context, .Path = FileSetPath("/assets")},
            };
            ASSERT_TRUE(Owner.Initialize(Allocator, roots).Succeeded());
            ASSERT_TRUE(Owner.TryAcquireRead(&Read).Succeeded());
            ASSERT_TRUE(Context.AcquireScratchRoot(FileSetPath("/scratch"), &Scratch).Succeeded());
            ASSERT_TRUE(Read.RequireDisjoint(Scratch).Succeeded());
            ASSERT_TRUE(Scratch.CreateDirExclusive(FileSetRelative("run")).Succeeded());
            const CookCaptureContext capture = {.Scratch = &Scratch, .ScratchRun = FileSetRelative("run"), .Allocator = &Allocator, .Limits = &Limits};
            ASSERT_TRUE(Store.Initialize(capture, Buffer).Succeeded());
        }

        void TearDown() override
        {
            EXPECT_EQ(Store.OutstandingCaptures(), 0u);
            Files.Reset();
            EXPECT_TRUE(Scratch.Remove(FileSetRelative("run")).Succeeded());
            Scratch.Reset();
            Read.Reset();
            EXPECT_TRUE(Owner.Reset().Succeeded());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            Context.Shutdown();
        }

        void Build(size_t maximum_records = 128)
        {
            ASSERT_TRUE(Files.Build(Allocator, Read, maximum_records, Buffer).Succeeded());
        }
    };

    TEST_F(CookFrozenFileSetTest, BuildsSortedStationaryIndexWithExactLogicalPathsAndNoSecondVerificationIndex)
    {
        Build();
        ASSERT_EQ(Files.Records().size(), 6u);
        for (size_t i = 1; i < Files.Records().size(); ++i)
        {
            EXPECT_TRUE(Files.Records()[i - 1].Path < Files.Records()[i].Path);
        }
        const auto* file = Files.Find(FileSetRelative("assets/nested/b.zemesh"));
        ASSERT_NE(file, nullptr);
        EXPECT_EQ(file->Stat.SizeBytes, sizeof(FileSetOld));
        EXPECT_FALSE(file->Stat.IsDirectory);
        EXPECT_EQ(file->Identity, Read.Identity(file->Path).Value());
        EXPECT_TRUE(Files.Find(FileSetRelative("assets"))->Stat.IsDirectory);
        EXPECT_EQ(Files.Find(FileSetRelative("assets/absent")), nullptr);
        EXPECT_EQ(Files.Find(FileSetRelative("assets-other/a.png")), nullptr);
        const auto allocations = Allocator.Allocations;
        EXPECT_TRUE(Files.Verify(Read, Store, Buffer).Succeeded());
        EXPECT_EQ(Files.Find(file->Path), file);
        EXPECT_EQ(Allocator.Allocations, allocations);
    }

    TEST_F(CookFrozenFileSetTest, EntryLimitIsEnforcedBeforeRetainingExtraRecordAndFailureLeavesEmptyIndex)
    {
        EXPECT_EQ(Files.Build(Allocator, Read, 1, Buffer).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_TRUE(Files.Records().empty());
        const auto live_before = Allocator.Statistics().LiveBytes;
        EXPECT_EQ(Files.Build(Allocator, Read, 5, Buffer).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_TRUE(Files.Records().empty());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live_before);
        Build(6);
        EXPECT_EQ(Files.Records().size(), 6u);
        EXPECT_TRUE(Files.Verify(Read, Store, Buffer).Succeeded());
    }

    TEST_F(CookFrozenFileSetTest, InitialAndGrowthAllocationFailuresReleasePendingStorage)
    {
        const auto live_before = Allocator.Statistics().LiveBytes;
        Allocator.Failure      = VFS::VFSError::OutOfMemory;
        EXPECT_EQ(Files.Build(Allocator, Read, 128, Buffer).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_TRUE(Files.Records().empty());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live_before);
        Allocator.Failure = VFS::VFSError::OK;
        for (uint32_t i = 0; i < 20; ++i)
        {
            char path[] = "/assets/file-00";
            path[13]    = static_cast<char>('0' + i / 10);
            path[14]    = static_cast<char>('0' + i % 10);
            ASSERT_TRUE(Backend.WriteFile(FileSetPath(path), FileSetOld).Succeeded());
        }
        struct FailGrowth final : ICookAllocator
        {
            ICookAllocator*      Parent = nullptr;
            uint32_t             Calls  = 0;
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                return Parent->TryCharge(bytes, category, output);
            }
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return ++Calls == 2 ? VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory) : Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
        } failing      = {};
        failing.Parent = &Allocator;
        EXPECT_EQ(Files.Build(failing, Read, 128, Buffer).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(failing.Calls, 2u);
        EXPECT_TRUE(Files.Records().empty());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live_before);
        Build();
        EXPECT_EQ(Files.Records().size(), 26u);
    }

    TEST_F(CookFrozenFileSetTest, OneSortReservationRefusesBeforeDiscoveryAndRetriesAtTheExactPayloadPlusStateBudget)
    {
        ChargeTrackingAllocator allocator = {};
        allocator.Parent                  = &Allocator;
        CookFrozenFileSet files           = {};
        const auto        retained        = Allocator.Statistics().LiveBytes;
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            allocator.Failure = error;
            const auto calls  = allocator.ChargeCalls;
            EXPECT_EQ(files.Build(allocator, Read, 128, Buffer).Error(), error);
            EXPECT_EQ(allocator.ChargeCalls, calls + 1);
            EXPECT_EQ(allocator.AllocationCalls, 0u);
            EXPECT_TRUE(files.Records().empty());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, retained);
        }
        allocator.Failure = VFS::VFSError::OK;
        const auto fixed  = allocator.Bytes;
        ASSERT_TRUE(files.Build(allocator, Read, 128, Buffer).Succeeded());
        const auto payload = Allocator.Statistics().LiveBytes - retained;
        files.Reset();
        for (const uint64_t available : {payload + fixed - 1, payload + fixed})
        {
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(Allocator.TryCharge(ZKilo(64) - retained - available, CookMemoryCategory::CatalogGraph, &occupied).Succeeded());
            const auto before = Allocator.Statistics().LiveBytes;
            const auto calls  = allocator.ChargeCalls;
            const auto result = files.Build(allocator, Read, 128, Buffer);
            EXPECT_EQ(result.Succeeded(), available == payload + fixed);
            EXPECT_EQ(allocator.ChargeCalls, calls + 1);
            if (result.Failed())
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_TRUE(files.Records().empty());
            }
            else
            {
                ASSERT_EQ(files.Records().size(), 6u);
                ASSERT_NE(files.Find(FileSetRelative("assets/a.png")), nullptr);
                const auto records = files.Records();
                for (size_t i = 1; i < records.size(); ++i)
                {
                    EXPECT_TRUE(records[i - 1].Path < records[i].Path);
                }
            }
            files.Reset();
            EXPECT_EQ(Allocator.Statistics().LiveBytes, before);
        }
    }

    TEST_F(CookFrozenFileSetTest, DeepDiscoveryReusesOneBufferAndGrowthIsAmortized)
    {
        for (uint32_t i = 0; i < 20; ++i)
        {
            char path[] = "/assets/file-00";
            path[13]    = static_cast<char>('0' + i / 10);
            path[14]    = static_cast<char>('0' + i % 10);
            ASSERT_TRUE(Backend.WriteFile(FileSetPath(path), FileSetOld).Succeeded());
        }
        auto directory = FileSetPath("/assets");
        for (uint32_t depth = 0; depth < 28; ++depth)
        {
            const auto next = directory.Append("deep");
            ASSERT_TRUE(next.Succeeded());
            directory = next.Value();
            ASSERT_TRUE(Backend.CreateDir(directory).Succeeded());
        }
        const auto leaf = directory.Append("leaf.png");
        ASSERT_TRUE(leaf.Succeeded());
        ASSERT_TRUE(Backend.WriteFile(leaf.Value(), FileSetOld).Succeeded());
        const auto allocations = Allocator.Allocations;
        Build();
        EXPECT_EQ(Files.Records().size(), 55u);
        EXPECT_EQ(Allocator.Allocations - allocations, 3u); // 16 -> 32 -> 64 records.
        const auto after_build = Allocator.Allocations;
        EXPECT_TRUE(Files.Verify(Read, Store, Buffer).Succeeded());
        EXPECT_EQ(Allocator.Allocations, after_build);
    }

    TEST_F(CookFrozenFileSetTest, CapturesHaveReceiptsAndRemainReadableAfterInputExclusionAndIndexEnd)
    {
        Build();
        CookCapturedFile captured    = {};
        const auto       path        = FileSetRelative("assets/a.png");
        const auto       allocations = Allocator.Allocations;
        ASSERT_TRUE(Files.Capture(Read, Store, path, 32, &captured).Succeeded());
        EXPECT_TRUE(Files.Find(path)->Captured);
        EXPECT_EQ(Files.Find(path)->CaptureDigest, captured.Digest());
        EXPECT_TRUE(Files.Verify(Read, Store, Buffer).Succeeded());
        EXPECT_EQ(Allocator.Allocations, allocations);
        Files.Reset();
        Read.Reset();
        CookLibraryMutationLease mutation = {};
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetNew).Succeeded());
        mutation.Reset();
        uint8_t    bytes[8] = {};
        const auto read     = captured.Read(0, bytes);
        ASSERT_TRUE(read.Succeeded());
        EXPECT_EQ(read.Value(), sizeof(FileSetOld));
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), FileSetOld, sizeof(FileSetOld), sizeof(FileSetOld)), 0);
    }

    TEST_F(CookFrozenFileSetTest, SameSizeArtifactAndSidecarChangesAreDetectedByReceiptDigests)
    {
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png.meta"), FileSetOld).Succeeded());
        Build();
        CookCapturedFile artifact = {}, metadata = {};
        ASSERT_TRUE(Files.Capture(Read, Store, FileSetRelative("assets/a.png"), 32, &artifact).Succeeded());
        ASSERT_TRUE(Files.Capture(Read, Store, FileSetRelative("assets/a.png.meta"), 32, &metadata).Succeeded());
        const auto before = Read.Identity(FileSetRelative("assets/a.png")).Value();
        // Intentionally bypass the owner to exercise detection of a violated
        // cooperating-writer contract; this is not a supported live editor cook.
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetNew).Succeeded());
        EXPECT_EQ(before, Read.Identity(FileSetRelative("assets/a.png")).Value());
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::Corrupted);
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetOld).Succeeded());
        ASSERT_TRUE(Files.Verify(Read, Store, Buffer).Succeeded());
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png.meta"), FileSetNew).Succeeded());
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::Corrupted);
    }

    TEST_F(CookFrozenFileSetTest, FailedRecaptureNeverSubstitutesChangedBytesOrModifiesExistingOutput)
    {
        Build();
        const auto       path     = FileSetRelative("assets/a.png");
        CookCapturedFile captured = {};
        ASSERT_TRUE(Files.Capture(Read, Store, path, 32, &captured).Succeeded());
        const auto digest = captured.Digest();
        EXPECT_EQ(Files.Capture(Read, Store, path, 3, &captured).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(captured.Digest(), digest);
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetNew).Succeeded());
        EXPECT_EQ(Files.Capture(Read, Store, path, 32, &captured).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(captured.Digest(), digest);
        EXPECT_EQ(Files.Find(path)->CaptureDigest, digest);
        EXPECT_EQ(Store.OutstandingCaptures(), 1u);
        EXPECT_EQ(Store.LiveBytes(), sizeof(FileSetOld));
    }

    TEST_F(CookFrozenFileSetTest, NamespaceAdditionRemovalAndReplacementInvalidateSnapshot)
    {
        Build();
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/new.png"), FileSetOld).Succeeded());
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::Corrupted);
        ASSERT_TRUE(Backend.Remove(FileSetPath("/assets/new.png")).Succeeded());
        ASSERT_TRUE(Files.Verify(Read, Store, Buffer).Succeeded());
        ASSERT_TRUE(Backend.Remove(FileSetPath("/assets/a.png")).Succeeded());
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::Corrupted);
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetOld).Succeeded());
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::Corrupted);
    }

    TEST_F(CookFrozenFileSetTest, ChangedSourceIdentityBeforeCaptureFailsWithoutExposingCapture)
    {
        Build();
        ASSERT_TRUE(Backend.Remove(FileSetPath("/assets/a.png")).Succeeded());
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetOld).Succeeded());
        CookCapturedFile captured = {};
        EXPECT_EQ(Files.Capture(Read, Store, FileSetRelative("assets/a.png"), 32, &captured).Error(), VFS::VFSError::Corrupted);
        EXPECT_FALSE(captured.IsValid());
        EXPECT_EQ(Store.OutstandingCaptures(), 0u);
    }

    TEST_F(CookFrozenFileSetTest, IdenticalContentAndMetadataCannotHideIdentityChangesOrReplaceAnExistingCapture)
    {
        Build();
        const auto       path     = FileSetRelative("assets/a.png");
        CookCapturedFile captured = {};
        ASSERT_TRUE(Files.Capture(Read, Store, path, 32, &captured).Succeeded());
        const auto identity = Files.Find(path)->Identity;
        const auto stat     = Files.Find(path)->Stat;
        const auto digest   = captured.Digest();
        ASSERT_TRUE(Backend.Remove(FileSetPath("/assets/a.png")).Succeeded());
        ASSERT_TRUE(Backend.WriteFile(FileSetPath("/assets/a.png"), FileSetOld).Succeeded());
        ASSERT_NE(Read.Identity(path).Value(), identity);
        const auto replacement = Read.Stat(path);
        ASSERT_TRUE(replacement.Succeeded());
        ASSERT_EQ(replacement.Value().SizeBytes, stat.SizeBytes);
        ASSERT_EQ(replacement.Value().MTimeNs, stat.MTimeNs);
        ASSERT_EQ(replacement.Value().IsDirectory, stat.IsDirectory);
        const auto live        = Allocator.Statistics().LiveBytes;
        const auto allocations = Allocator.Allocations;
        EXPECT_EQ(Files.Capture(Read, Store, path, 32, &captured).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(captured.Digest(), digest);
        EXPECT_EQ(Files.Find(path)->CaptureDigest, digest);
        EXPECT_EQ(Files.Find(path)->Identity, identity);
        EXPECT_EQ(Store.OutstandingCaptures(), 1u);
        EXPECT_EQ(Store.LiveBytes(), sizeof(FileSetOld));
        auto failure_source = FileSetRelative("sentinel/unchanged");
        EXPECT_EQ(Files.Verify(Read, Store, Buffer, &failure_source).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(failure_source, path);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        EXPECT_EQ(Allocator.Allocations, allocations);
        uint8_t bytes[sizeof(FileSetOld)] = {};
        ASSERT_EQ(captured.Read(0, bytes).Value(), sizeof(bytes));
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), FileSetOld, sizeof(FileSetOld), sizeof(bytes)), 0);
    }

    TEST_F(CookFrozenFileSetTest, PriorGenerationAndOtherOwnerRootsCannotValidateReceiptSet)
    {
        Build();
        const auto captured_generation = Read.Generation();
        Read.Reset();
        CookLibraryMutationLease mutation = {};
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        mutation.Reset();
        ASSERT_TRUE(Owner.TryAcquireRead(&Read).Succeeded());
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::Corrupted);

        VFS::VFSMemoryBackend other_backend = {};
        ASSERT_TRUE(other_backend.InitializeOwned(ZMega(1), 16).Succeeded());
        ASSERT_TRUE(other_backend.CreateDir(FileSetPath("/assets")).Succeeded());
        ASSERT_TRUE(other_backend.CreateDir(FileSetPath("/scenes")).Succeeded());
        VFS::VFSContext other_context = {};
        other_context.Initialize(&Memory.MainArena);
        ASSERT_TRUE(other_context.Mount(&other_backend, VFS::VFSPath::Root(), 0).Succeeded());
        CookFrozenLibraryOwner      other_owner = {};
        const CookFrozenRootRequest roots[]     = {
            {.Key = "assets", .Context = &other_context, .Path = FileSetPath("/assets")},
            {.Key = "scenes", .Context = &other_context, .Path = FileSetPath("/scenes")},
        };
        ASSERT_TRUE(other_owner.Initialize(Allocator, roots).Succeeded());
        CookFrozenLibraryReadLease other_read = {};
        ASSERT_TRUE(other_owner.TryAcquireRead(&other_read).Succeeded());
        EXPECT_EQ(other_read.Generation(), captured_generation);
        EXPECT_EQ(Files.Verify(other_read, Store, Buffer).Error(), VFS::VFSError::Corrupted);
        other_context.Shutdown();
    }

    TEST_F(CookFrozenFileSetTest, InvalidCaptureAndVerificationArgumentsFailClosed)
    {
        CookCapturedFile captured = {};
        EXPECT_EQ(Files.Capture(Read, Store, FileSetRelative("assets/a.png"), 32, &captured).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Files.Verify(Read, Store, Buffer).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Files.Build(Allocator, Read, 128, {}).Error(), VFS::VFSError::InvalidPath);
        Build();
        EXPECT_EQ(Files.Build(Allocator, Read, 128, Buffer).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Files.Capture(Read, Store, FileSetRelative("assets/a.png"), 32, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Files.Capture(Read, Store, FileSetRelative("assets"), 32, &captured).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Files.Capture(Read, Store, FileSetRelative("assets/nested"), 32, &captured).Error(), VFS::VFSError::NotAFile);
        EXPECT_EQ(Files.Capture(Read, Store, FileSetRelative("assets/missing"), 32, &captured).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Files.Verify(Read, Store, {}).Error(), VFS::VFSError::InvalidPath);
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    TEST(CookFrozenFileSetNativeTest, PhysicalAliasesAcrossRootsAreRejectedWithoutAnAdditionalRetainedIndex)
    {
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("cook-frozen-aliases", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "assets", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "other", error));
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSDiskBackend backend = {};
        const auto          native  = directory.Path().generic_u8string();
        backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &memory.MainArena);
        VFS::VFSContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(context, FileSetPath("/assets/a.png"), FileSetOld).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(context, FileSetPath("/other/equal.png"), FileSetOld).Succeeded());
        FixedAllocator<ZKilo(64), 4> allocator = {};
        CookFrozenLibraryOwner       owner     = {};
        const CookFrozenRootRequest  roots[]   = {
            {.Key = "assets", .Context = &context, .Path = FileSetPath("/assets")},
            { .Key = "other", .Context = &context,  .Path = FileSetPath("/other")},
        };
        ASSERT_TRUE(owner.Initialize(allocator, roots).Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(owner.TryAcquireRead(&read).Succeeded());
        uint8_t           buffer[ZKilo(4)]   = {};
        CookFrozenFileSet files              = {};
        const auto        live_before        = allocator.Statistics().LiveBytes;
        const auto        allocations_before = allocator.Allocations;
        // Equal content in distinct physical files is valid; it is not an alias.
        ASSERT_TRUE(files.Build(allocator, read, 16, buffer).Succeeded());
        EXPECT_EQ(allocator.Allocations - allocations_before, 1u);
        EXPECT_EQ(files.Records().size(), 4u);
        files.Reset();
        std::filesystem::create_hard_link(directory.Path() / "assets/a.png", directory.Path() / "other/alias.png", error);
        ASSERT_FALSE(error) << error.message();
        EXPECT_EQ(files.Build(allocator, read, 16, buffer).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(files.Records().empty());
        EXPECT_EQ(allocator.Statistics().LiveBytes, live_before);
        read.Reset();
        EXPECT_TRUE(owner.Reset().Succeeded());
        context.Shutdown();
    }

    TEST(CookFrozenFileSetNativeTest, StableMetadataCannotHideChangedSourceBytes)
    {
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("cook-frozen-file-set", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "assets", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "scratch", error));
        Core::Memory::MemoryManager memory = {};
        memory.Initialize(ZMega(2), {});
        VFS::VFSDiskBackend backend = {};
        const auto          native  = directory.Path().generic_u8string();
        backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &memory.MainArena);
        VFS::VFSContext context = {};
        context.Initialize(&memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        ASSERT_TRUE(VFS::WriteFileAtomically(context, FileSetPath("/assets/a.png"), FileSetOld).Succeeded());
        FixedAllocator<ZKilo(64), 4> allocator = {};
        CookFrozenLibraryOwner       owner     = {};
        const CookFrozenRootRequest  roots[]   = {
            {.Key = "assets", .Context = &context, .Path = FileSetPath("/assets")}
        };
        ASSERT_TRUE(owner.Initialize(allocator, roots).Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(owner.TryAcquireRead(&read).Succeeded());
        VFS::VFSScratchRootLease scratch = {};
        ASSERT_TRUE(context.AcquireScratchRoot(FileSetPath("/scratch"), &scratch).Succeeded());
        ASSERT_TRUE(read.RequireDisjoint(scratch).Succeeded());
        ASSERT_TRUE(scratch.CreateDirExclusive(FileSetRelative("run")).Succeeded());
        const CookLimits         limits           = {.MaxWorkingBytes = ZKilo(64)};
        const CookCaptureContext capture          = {.Scratch = &scratch, .ScratchRun = FileSetRelative("run"), .Allocator = &allocator, .Limits = &limits};
        uint8_t                  buffer[ZKilo(4)] = {};
        CookCaptureStore         store            = {};
        ASSERT_TRUE(store.Initialize(capture, buffer).Succeeded());
        CookFrozenFileSet files = {};
        ASSERT_TRUE(files.Build(allocator, read, 16, buffer).Succeeded());
        CookCapturedFile captured = {};
        const auto       logical  = FileSetRelative("assets/a.png");
        ASSERT_TRUE(files.Capture(read, store, logical, 32, &captured).Succeeded());
        ASSERT_TRUE(files.Verify(read, store, buffer).Succeeded());
        const auto path      = directory.Path() / "assets/a.png";
        const auto timestamp = std::filesystem::last_write_time(path, error);
        ASSERT_FALSE(error);
        const auto write = context.Open(FileSetPath("/assets/a.png"), VFS::VFSOpenFlags::Write);
        ASSERT_TRUE(write.Succeeded());
        const auto written = write.Value()->Write(FileSetNew, 0);
        const auto closed  = context.CloseChecked(write.Value());
        ASSERT_TRUE(written.Succeeded());
        ASSERT_EQ(written.Value(), sizeof(FileSetNew));
        ASSERT_TRUE(closed.Succeeded());
        std::filesystem::last_write_time(path, timestamp, error);
        ASSERT_FALSE(error);
        EXPECT_EQ(read.Identity(logical).Value(), files.Find(logical)->Identity);
        EXPECT_EQ(read.Stat(logical).Value().MTimeNs, files.Find(logical)->Stat.MTimeNs);
        EXPECT_EQ(files.Verify(read, store, buffer).Error(), VFS::VFSError::Corrupted);
        uint8_t bytes[8] = {};
        ASSERT_TRUE(captured.Read(0, bytes).Succeeded());
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), FileSetOld, sizeof(FileSetOld), sizeof(FileSetOld)), 0);
        EXPECT_TRUE(captured.Reset().Succeeded());
        EXPECT_TRUE(scratch.Remove(FileSetRelative("run")).Succeeded());
        context.Shutdown();
    }
#endif
} // namespace ZEngine::Cook::Tests
