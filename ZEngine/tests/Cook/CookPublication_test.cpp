#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <nlohmann/json.hpp>
#include "../TemporaryDirectory.h"
#include "CookPublicationTestSupport.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSPath PackagePath(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }
        VFS::VFSRelativePath PackageRelative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }
        uuids::uuid PackageUUID(uint8_t id)
        {
            uint8_t bytes[16] = {};
            bytes[15]         = id;
            return uuids::uuid(bytes, bytes + sizeof(bytes));
        }

        // Only a lease-state probe; real capture/seal/publication integration
        // lives in CookSceneDocumentTest with the frozen-library provider.
        struct PublicationInputs final : ICookInputSource, Detail::ICookInputSession
        {
            VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease* output, CookDiagnostics*) override
            {
                return BindLease(*this, output);
            }
            VFS::VFSResult<void> EnumerateCatalog(CookCatalogVisitorFn, void*, CookDiagnostics*) override
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            VFS::VFSResult<void> Capture(const VFS::VFSRelativePath&, CookCapturedFile*, CookDiagnostics*) override
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            VFS::VFSResult<void> Seal(CookDiagnostics*) override
            {
                return VFS::VFSResult<void>::Ok();
            }
            void Release() noexcept override {}
        };

        struct PackageAllocator final : ICookAllocator
        {
            struct FixedRequest
            {
                uint64_t Bytes      = 0;
                uint64_t LiveBefore = 0;
            };
            ICookAllocator*                                 Inner        = nullptr;
            uint32_t                                        Calls        = 0;
            uint32_t                                        FailAt       = UINT32_MAX;
            VFS::VFSError                                   Failure      = VFS::VFSError::OutOfMemory;
            VFS::VFSError                                   FixedFailure = VFS::VFSError::OK;
            uint32_t                                        ChargeCalls  = 0;
            uint32_t                                        FixedFailAt  = UINT32_MAX;
            Core::Containers::FixedArray<FixedRequest, 256> Requests     = {};
            VFS::VFSResult<void>                            TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                const uint32_t index = ChargeCalls++;
                if (index < Requests.size())
                    Requests[index] = {bytes, Inner->Statistics().LiveBytes};
                return category == CookMemoryCategory::StagingManifest && FixedFailure != VFS::VFSError::OK && (FixedFailAt == UINT32_MAX || FixedFailAt == ChargeCalls) ? VFS::VFSResult<void>::Fail(FixedFailure) : Inner->TryCharge(bytes, category, output);
            }
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return ++Calls == FailAt ? VFS::VFSResult<void*>::Fail(Failure) : Inner->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Inner->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Inner->Statistics();
            }
        };

        struct PackageProducer
        {
            Core::Containers::ArrayView<const uint8_t> Bytes             = {};
            uint32_t                                   Calls             = 0;
            size_t                                     Chunk             = SIZE_MAX;
            VFS::VFSError                              Error             = VFS::VFSError::OK;
            bool                                       IgnoreSinkFailure = false;

            static VFS::VFSResult<void>                Run(void* context, ICookByteSink& sink)
            {
                auto& producer = *static_cast<PackageProducer*>(context);
                ++producer.Calls;
                for (size_t offset = 0; offset < producer.Bytes.size();)
                {
                    const size_t count   = std::min(producer.Bytes.size() - offset, producer.Chunk);
                    const auto   written = sink.Write(producer.Bytes.subspan(offset, count));
                    if (written.Failed() && !producer.IgnoreSinkFailure)
                        return written;
                    offset += count;
                }
                return producer.Error == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(producer.Error);
            }
        };
    } // namespace

    class CookPublicationTest : public testing::TestWithParam<bool>
    {
    protected:
        ZEngine::Tests::TemporaryDirectory   Directory         = {};
        Core::Memory::MemoryManager          Memory            = {};
        VFS::VFSMemoryBackend                MemoryBackend     = {};
        VFS::VFSDiskBackend                  Disk              = {};
        Tests::PublicationFaults             Faults            = {};
        Tests::PublicationBackend            Backend           = {};
        VFS::VFSContext                      Context           = {};
        VFS::VFSPublicationRootLease         Root              = {};
        Tests::FixedAllocator<ZKilo(256), 1> Parent            = {};
        CookScratchAllocator                 Scratch           = {};
        PackageAllocator                     Allocator         = {};
        CookLimits                           Limits            = {.MaxWorkingBytes = ZKilo(512)};
        CookDependencyGraph                  Graph             = {};
        PublicationInputs                    Inputs            = {};
        CookInputLease                       Lease             = {};
        CookRecipeKeys                       Recipe            = {.Target = CookTarget::DesktopVulkanLE64};
        CookPublication                      Publisher         = {};
        CookDiagnostics                      Diagnostics       = {};
        CookPublicationResult                Result            = {};
        uint8_t                              Stream[4096]      = {};
        uint32_t                             Workspace[8]      = {};
        uint8_t                              TextureBytes[128] = {};
        uint8_t                              MaterialBytes[64] = {};
        uint8_t                              SceneBytes[32]    = {};
        PackageProducer                      Texture           = {};
        PackageProducer                      Material          = {};
        PackageProducer                      Scene             = {};
        uint32_t                             TextureIndex      = 0;
        uint32_t                             MaterialIndex     = 0;

        void                                 SetUp() override
        {
            Memory.Initialize(ZMega(4), {});
            if (GetParam())
            {
                std::error_code error = {};
                ASSERT_TRUE(Directory.Create("zengine_cook_publication", error));
                const auto native = Directory.Path().generic_u8string();
                Disk.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &Memory.MainArena);
                Backend.Inner = &Disk;
            }
            else
            {
                ASSERT_TRUE(MemoryBackend.InitializeOwned(ZMega(2), 256).Succeeded());
                Backend.Inner = &MemoryBackend;
            }
            ASSERT_TRUE(Backend.Inner->CreateDir(PackagePath("/output")).Succeeded());
            Backend.Provider         = Helpers::Ref<Tests::PublicationProvider>(new Tests::PublicationProvider());
            Backend.Provider->Inner  = Backend.Inner->RootProvider();
            Backend.Provider->Faults = &Faults;
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(Context.AcquirePublicationRoot(PackagePath("/output"), &Root).Succeeded());
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZKilo(256), CookMemoryCategory::CatalogGraph).Succeeded());
            Allocator.Inner = &Scratch;
            ASSERT_TRUE(Inputs.Acquire({}, &Lease, nullptr).Succeeded());
            ASSERT_TRUE(Lease.Seal(nullptr).Succeeded());
            ASSERT_TRUE(ComputeCookSettingsKey(Recipe.Target, &Recipe.SettingsKey).Succeeded());
            std::fill(std::begin(TextureBytes), std::end(TextureBytes), 1);
            std::fill(std::begin(MaterialBytes), std::end(MaterialBytes), 2);
            std::fill(std::begin(SceneBytes), std::end(SceneBytes), 3);
            Texture.Bytes  = TextureBytes;
            Material.Bytes = MaterialBytes;
            Scene.Bytes    = SceneBytes;
            Build();
        }
        void TearDown() override
        {
            Faults.RemoveLock      = VFS::VFSError::OK;
            Faults.RemoveTemporary = VFS::VFSError::OK;
            (void) Publisher.Reset();
            EXPECT_EQ(Faults.LiveFiles, 0u);
            Lease.Reset();
            Graph.Reset();
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, Parent.Frees);
            Root.Reset();
            Context.Shutdown();
        }
        void Build(bool shared = false)
        {
            Graph.Reset();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            ASSERT_TRUE(Graph.AddScene(PackageUUID(100), PackageRelative("scenes/main.yaml")).Succeeded());
            ASSERT_TRUE(Graph.AddRoot(0, {CookAssetKind::Material, PackageUUID(2)}, {}, "Test", "material").Succeeded());
            if (shared)
                ASSERT_TRUE(Graph.AddRoot(0, {CookAssetKind::Texture, PackageUUID(3)}, {}, "Test", "texture").Succeeded());
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            MaterialIndex             = Graph.Find({CookAssetKind::Material, PackageUUID(2)});
            const CookDependency edge = {
                {CookAssetKind::Texture, PackageUUID(1)},
                "albedo"
            };
            ASSERT_TRUE(Graph
                            .SetInspection(
                                MaterialIndex,
                                1,
                                CookAssetInspectionView{
                                "zematerial-v1-json", {},
                                 {},
                                 {&edge, 1}
            })
                            .Succeeded());
            TextureIndex = Graph.Find(edge.Identity);
            ASSERT_TRUE(Graph.SetInspection(TextureIndex, 1, CookAssetInspectionView{"image-png"}).Succeeded());
            if (shared)
                ASSERT_TRUE(Graph.SetInspection(Graph.Find({CookAssetKind::Texture, PackageUUID(3)}), 1, CookAssetInspectionView{"image-png"}).Succeeded());
            ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        }
        CookPublicationInput Input(bool force = false)
        {
            return {&Root, &Lease, &Graph, Recipe, &Limits, force};
        }
        void Start(bool force = false)
        {
            ASSERT_TRUE(Publisher.Initialize(Allocator, Input(force), Stream, &Diagnostics).Succeeded());
        }
        void Encode()
        {
            ASSERT_TRUE(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics).Succeeded());
            ASSERT_TRUE(Publisher.EncodeAsset(MaterialIndex, PackageProducer::Run, &Material, &Diagnostics).Succeeded());
            ASSERT_TRUE(Publisher.EncodeScene(0, {}, PackageProducer::Run, &Scene, &Diagnostics).Succeeded());
        }
        void FirstPackage()
        {
            Start();
            Encode();
            ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
            ASSERT_TRUE(Result.Published);
            ASSERT_TRUE(Publisher.Reset(&Diagnostics).Succeeded());
            Faults = {};
        }
        std::string Manifest()
        {
            const auto opened = Backend.Inner->Open(PackagePath("/output/manifest.json"), VFS::VFSOpenFlags::Read);
            EXPECT_TRUE(opened.Succeeded());
            if (opened.Failed())
                return {};
            const auto  size = opened.Value()->Size().Value();
            std::string bytes(static_cast<size_t>(size), '\0');
            EXPECT_EQ(opened.Value()->ReadAll({reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()}).Value(), size);
            Backend.Inner->Close(opened.Value());
            return bytes;
        }
        void ReplaceManifest(const std::string& bytes)
        {
            const auto opened = Backend.Inner->Open(PackagePath("/output/manifest.json"), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Truncate);
            ASSERT_TRUE(opened.Succeeded());
            ASSERT_EQ(opened.Value()->Write({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, 0).Value(), bytes.size());
            Backend.Inner->Close(opened.Value());
        }
    };

    TEST_P(CookPublicationTest, FirstPackagePublishesVerifiedObjectsAndCanonicalManifestWithoutAdditionalCookAllocations)
    {
        Start();
        const auto allocations = Allocator.Calls;
        Texture.Chunk = Material.Chunk = Scene.Chunk = 1;
        Encode();
        EXPECT_EQ(Faults.Writes, 3u); // Buffer tiny producer writes, not one syscall per byte.
        EXPECT_FALSE(Backend.Inner->Exists(PackagePath("/output/manifest.json")));
        Allocator.FailAt = Allocator.Calls + 1;
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_EQ(Allocator.Calls, allocations);
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Result.ObjectCount, 3u);
        EXPECT_EQ(Result.ReferencedObjectBytes, sizeof(TextureBytes) + sizeof(MaterialBytes) + sizeof(SceneBytes));
        EXPECT_EQ(Faults.ManifestRenames, 1u);
        EXPECT_EQ(Faults.ObjectRenames, 3u);
        EXPECT_EQ(Faults.WritesAfterCommit, 0u);
        EXPECT_EQ(Faults.Writes, 4u);
        EXPECT_EQ(Publisher.StagedBytes(), Result.ReferencedObjectBytes + Result.Manifest.Bytes);
        const auto text = Manifest();
        EXPECT_EQ(text.back(), '\n');
        EXPECT_EQ(text.size(), Result.Manifest.Bytes);
        Core::Hash::SHA256 hash   = {};
        Digest256          digest = {};
        ASSERT_TRUE(hash.Update({reinterpret_cast<const uint8_t*>(text.data()), text.size()}).Succeeded());
        ASSERT_TRUE(hash.Finalize(&digest).Succeeded());
        EXPECT_EQ(Result.Manifest.Digest, digest);
        EXPECT_EQ(Texture.Calls + Material.Calls + Scene.Calls, 3u);
        ASSERT_TRUE(Publisher.Reset(&Diagnostics).Succeeded());
        EXPECT_EQ(Faults.LockRemovals, 1u);
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    TEST_P(CookPublicationTest, ObjectVerificationResourceRefusalPoisonsPublicationAndPreservesThePreviousPackage)
    {
        FirstPackage();
        const auto original  = Manifest();
        const auto previous  = Result;
        TextureBytes[0]     ^= 4;
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            Start(true);
            const auto live        = Parent.Statistics().LiveBytes;
            const auto calls       = Allocator.Calls;
            Diagnostics            = {};
            Allocator.FixedFailure = error;
            Allocator.FixedFailAt  = Allocator.ChargeCalls + 2; // Encode state, then staged-byte verification.
            EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics).Error(), error);
            EXPECT_EQ(Faults.ObjectRenames, 0u);
            EXPECT_EQ(Faults.ManifestRenames, 0u);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            EXPECT_EQ(Publisher.StagedBytes(), 0u);
            EXPECT_EQ(Parent.Statistics().LiveBytes, live);
            EXPECT_EQ(Allocator.Calls, calls);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
            Allocator.FixedFailure = VFS::VFSError::OK;
            const auto writes      = Faults.Writes;
            EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, nullptr).Error(), error);
            EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, nullptr).Error(), error);
            EXPECT_EQ(Faults.Writes, writes);
            EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
            EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
            EXPECT_EQ(Result.Published, previous.Published);
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, ManifestVerificationResourceRefusalHappensBeforeCommitAndPreservesTheCallerResult)
    {
        FirstPackage();
        const auto original  = Manifest();
        const auto previous  = Result;
        TextureBytes[0]     ^= 4;
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            Start(true);
            Encode();
            const auto staged      = Publisher.StagedBytes();
            const auto live        = Parent.Statistics().LiveBytes;
            const auto calls       = Allocator.Calls;
            Diagnostics            = {};
            Allocator.FixedFailure = error;
            Allocator.FixedFailAt  = Allocator.ChargeCalls + 6; // Result, comparison/writer, staging/writer, verification.
            EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Error(), error);
            EXPECT_EQ(Faults.ManifestRenames, 0u);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            EXPECT_EQ(Publisher.StagedBytes(), staged);
            EXPECT_EQ(Parent.Statistics().LiveBytes, live);
            EXPECT_EQ(Allocator.Calls, calls);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
            Allocator.FixedFailure = VFS::VFSError::OK;
            EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, nullptr).Error(), error);
            EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
            EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
            EXPECT_EQ(Result.Published, previous.Published);
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, PrincipalPhaseRefusalPrecedesIOAndPreservesThePackageWithoutAdditionalAllocations)
    {
        FirstPackage();
        const auto original  = Manifest();
        TextureBytes[0]     ^= 4;
        for (uint32_t phase = 0; phase < 5; ++phase)
        {
            for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
            {
                SCOPED_TRACE(phase);
                if (phase)
                {
                    Start(phase != 3);
                    if (phase >= 2)
                        Encode();
                }
                const auto previous    = Result;
                const auto live        = Parent.Statistics().LiveBytes;
                const auto allocations = Allocator.Calls;
                const auto readers     = Faults.ReaderOpens;
                const auto writers     = Faults.WriterOpens;
                const auto staged      = Publisher.StagedBytes();
                const auto produced    = Texture.Calls + Material.Calls + Scene.Calls;
                Diagnostics            = {};
                Allocator.FixedFailure = error;
                Allocator.FixedFailAt  = Allocator.ChargeCalls + (phase == 4 ? 4 : phase >= 2 ? phase - 1 : phase == 1 ? 2 : 1); // Asset key precedes staging.
                const auto refused     = phase == 0 ? Publisher.Initialize(Allocator, Input(), Stream, &Diagnostics) : phase == 1 ? Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics) : Publisher.PublishManifest(Workspace, &Result, &Diagnostics);
                EXPECT_EQ(refused.Error(), error);
                EXPECT_EQ(Faults.ReaderOpens, readers + (phase == 4 ? 1 : 0));
                EXPECT_EQ(Faults.WriterOpens, writers);
                EXPECT_EQ(Faults.LiveFiles, 0u);
                EXPECT_EQ(Publisher.StagedBytes(), staged);
                EXPECT_EQ(Parent.Statistics().LiveBytes, live);
                EXPECT_EQ(Allocator.Calls, allocations);
                EXPECT_EQ(Texture.Calls + Material.Calls + Scene.Calls, produced);
                EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
                EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
                EXPECT_EQ(Result.ObjectCount, previous.ObjectCount);
                EXPECT_EQ(Result.ReferencedObjectBytes, previous.ReferencedObjectBytes);
                EXPECT_EQ(Result.Published, previous.Published);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
                Allocator.FixedFailure = VFS::VFSError::OK;
                if (phase)
                    EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, nullptr).Error(), error);
                else
                    EXPECT_FALSE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
                EXPECT_EQ(Manifest(), original);
                ASSERT_TRUE(Publisher.Reset().Succeeded());
                Faults = {};
            }
        }
    }

    TEST_P(CookPublicationTest, ObjectStagingAndVerificationUseDistinctNestedChargesAtExactAndOneByteShortBudgets)
    {
        FirstPackage();
        const auto original  = Manifest();
        TextureBytes[0]     ^= 4;
        Start(true);
        const auto first = Allocator.ChargeCalls;
        ASSERT_TRUE(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics).Succeeded());
        ASSERT_EQ(Allocator.ChargeCalls, first + 4);
        ASSERT_LT(first + 3, Allocator.Requests.size());
        const auto key      = Allocator.Requests[first].Bytes;
        const auto own      = Allocator.Requests[first + 1].Bytes;
        const auto verify   = Allocator.Requests[first + 2].Bytes;
        const auto required = own + verify;
        EXPECT_EQ(key, CookFingerprint::FixedWorkingBytes() + sizeof(CookMemoryCharge));
        EXPECT_LT(key, own - 1);
        EXPECT_EQ(Allocator.Requests[first + 3].Bytes, verify);
        EXPECT_EQ(Allocator.Requests[first + 1].LiveBefore, Allocator.Requests[first].LiveBefore);
        EXPECT_EQ(Allocator.Requests[first + 2].LiveBefore, Allocator.Requests[first + 1].LiveBefore + own);
        EXPECT_EQ(Allocator.Requests[first + 3].LiveBefore, Allocator.Requests[first + 1].LiveBefore + own);
        const auto expected = Publisher.AssetResults()[TextureIndex].Object;
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        Faults = {};
        for (const uint64_t available : {own - 1, required - 1, required})
        {
            Start(true);
            const auto live = Parent.Statistics().LiveBytes;
            ASSERT_LE(live + required, Limits.MaxWorkingBytes);
            CookMemoryCharge blocker = {};
            ASSERT_TRUE(Parent.Ledger.TryCharge(Limits.MaxWorkingBytes - live - available, CookMemoryCategory::Codecs, &blocker).Succeeded());
            const auto calls    = Allocator.Calls;
            const auto writers  = Faults.WriterOpens;
            const auto readers  = Faults.ReaderOpens;
            const auto produced = Texture.Calls;
            const auto result   = Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics);
            EXPECT_EQ(Parent.Statistics().LiveBytes, live + blocker.Bytes());
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            if (available < required)
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(Faults.ReaderOpens, readers);
                EXPECT_EQ(Faults.WriterOpens, writers + (available < own ? 0 : 1));
                EXPECT_EQ(Texture.Calls, produced + (available < own ? 0 : 1));
                EXPECT_EQ(Faults.ObjectRenames, 0u);
                EXPECT_EQ(Publisher.StagedBytes(), 0u);
            }
            else
            {
                EXPECT_TRUE(result.Succeeded());
                EXPECT_EQ(Publisher.AssetResults()[TextureIndex].Object, expected);
                EXPECT_EQ(Faults.ReaderOpens, readers + 2);
                EXPECT_EQ(Faults.WriterOpens, writers + 1);
            }
            blocker.Release();
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, CanonicalNoOpReservesResultComparisonAndWriterStateAndFitsExactlyWithoutStaging)
    {
        FirstPackage();
        const auto original   = Manifest();
        Limits.MaxStagedBytes = 0;
        Start();
        Encode();
        const auto first = Allocator.ChargeCalls;
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        ASSERT_FALSE(Result.Published);
        ASSERT_EQ(Allocator.ChargeCalls, first + 3);
        ASSERT_LT(first + 2, Allocator.Requests.size());
        const auto own        = Allocator.Requests[first].Bytes;
        const auto comparison = Allocator.Requests[first + 1].Bytes;
        const auto required   = own + comparison + Allocator.Requests[first + 2].Bytes;
        EXPECT_EQ(Allocator.Requests[first + 1].LiveBefore, Allocator.Requests[first].LiveBefore + own);
        EXPECT_EQ(Allocator.Requests[first + 2].LiveBefore, Allocator.Requests[first].LiveBefore + own + comparison);
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        Faults = {};
        for (const uint64_t available : {required - 1, required})
        {
            Start();
            Encode();
            const auto previous = Result;
            const auto live     = Parent.Statistics().LiveBytes;
            ASSERT_LE(live + required, Limits.MaxWorkingBytes);
            CookMemoryCharge blocker = {};
            ASSERT_TRUE(Parent.Ledger.TryCharge(Limits.MaxWorkingBytes - live - available, CookMemoryCategory::Codecs, &blocker).Succeeded());
            const auto calls   = Allocator.Calls;
            const auto readers = Faults.ReaderOpens;
            const auto result  = Publisher.PublishManifest(Workspace, &Result, &Diagnostics);
            EXPECT_EQ(Parent.Statistics().LiveBytes, live + blocker.Bytes());
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            EXPECT_EQ(Faults.WriterOpens, 0u);
            EXPECT_EQ(Faults.Writes, 0u);
            EXPECT_EQ(Faults.ManifestRenames, 0u);
            EXPECT_EQ(Publisher.StagedBytes(), 0u);
            if (available < required)
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(Faults.ReaderOpens, readers + 1);
                EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
                EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
                EXPECT_EQ(Result.Published, previous.Published);
            }
            else
            {
                EXPECT_TRUE(result.Succeeded());
                EXPECT_EQ(Faults.ReaderOpens, readers + 1);
                EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
                EXPECT_FALSE(Result.Published);
            }
            blocker.Release();
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, ManifestWriterRefusalPreservesNoOpAndStagedPackagesAndUsesResourceDiagnostics)
    {
        FirstPackage();
        const auto original = Manifest();
        for (const bool changed : {false, true})
        {
            if (changed)
                TextureBytes[0] ^= 4;
            for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
            {
                Start(changed);
                Encode();
                const auto previous    = Result;
                const auto live        = Parent.Statistics().LiveBytes;
                const auto allocations = Allocator.Calls;
                const auto staged      = Publisher.StagedBytes();
                const auto reads       = Faults.ReaderOpens;
                const auto writes      = Faults.Writes;
                const auto opens       = Faults.WriterOpens;
                Diagnostics            = {};
                Allocator.FixedFailure = error;
                Allocator.FixedFailAt  = Allocator.ChargeCalls + (changed ? 5 : 3);
                EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Error(), error);
                EXPECT_EQ(Faults.ReaderOpens, reads + 1);
                EXPECT_EQ(Faults.WriterOpens, opens + (changed ? 1 : 0));
                EXPECT_EQ(Faults.Writes, writes);
                EXPECT_EQ(Faults.ManifestRenames, 0u);
                EXPECT_EQ(Faults.LiveFiles, 0u);
                EXPECT_EQ(Publisher.StagedBytes(), staged);
                EXPECT_EQ(Parent.Statistics().LiveBytes, live);
                EXPECT_EQ(Allocator.Calls, allocations);
                EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
                EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
                EXPECT_EQ(Result.ObjectCount, previous.ObjectCount);
                EXPECT_EQ(Result.ReferencedObjectBytes, previous.ReferencedObjectBytes);
                EXPECT_EQ(Result.Published, previous.Published);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
                Allocator.FixedFailure = VFS::VFSError::OK;
                EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, nullptr).Error(), error);
                EXPECT_EQ(Manifest(), original);
                ASSERT_TRUE(Publisher.Reset().Succeeded());
                Faults = {};
            }
        }
    }

    TEST_P(CookPublicationTest, NullProducerFailsBeforeStagingChargesOrIOAfterCheckingReuse)
    {
        Start(true);
        const auto charges     = Allocator.ChargeCalls;
        const auto readers     = Faults.ReaderOpens;
        const auto writers     = Faults.WriterOpens;
        Allocator.FixedFailure = VFS::VFSError::Unsupported;
        EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, nullptr, nullptr, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.ChargeCalls, charges + 1);
        EXPECT_EQ(Allocator.Requests[charges].Bytes, CookFingerprint::FixedWorkingBytes() + sizeof(CookMemoryCharge));
        EXPECT_EQ(Faults.ReaderOpens, readers);
        EXPECT_EQ(Faults.WriterOpens, writers);
        EXPECT_EQ(Faults.LiveFiles, 0u);
    }

    TEST_P(CookPublicationTest, VerifiedCanonicalNoOpWritesNothingAndNeedsNoStagingBudget)
    {
        FirstPackage();
        const auto original   = Manifest();
        Limits.MaxStagedBytes = 0;
        Start();
        Encode();
        EXPECT_EQ(Texture.Calls + Material.Calls + Scene.Calls, 3u);
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Faults.WriterOpens, 0u);
        EXPECT_EQ(Faults.Writes, 0u);
        EXPECT_EQ(Faults.ManifestRenames, 0u);
        EXPECT_EQ(Publisher.StagedBytes(), 0u);
        EXPECT_EQ(Manifest(), original);
    }

    TEST_P(CookPublicationTest, NoncanonicalValidLedgerIsRepublishedCanonicallyWithoutReencodingObjects)
    {
        FirstPackage();
        const auto canonical = Manifest();
        ReplaceManifest(nlohmann::json::parse(canonical).dump(2));
        Start();
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Manifest(), canonical);
        EXPECT_EQ(Faults.ObjectRenames, 0u);
        EXPECT_EQ(Faults.ManifestRenames, 1u);
        EXPECT_EQ(Texture.Calls + Material.Calls + Scene.Calls, 3u);
    }

    TEST_P(CookPublicationTest, ExclusiveWriterContentionAndUnsealedInputsNeverDeleteForeignLocks)
    {
        Start();
        CookPublication other            = {};
        uint8_t         other_stream[32] = {};
        EXPECT_EQ(other.Initialize(Allocator, Input(), other_stream, &Diagnostics).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
        EXPECT_EQ(Faults.LockRemovals, 0u);
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::OutputBusy);
        EXPECT_EQ(Publisher.Initialize(Allocator, Input(), Stream, nullptr).Error(), VFS::VFSError::InvalidPath);
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, nullptr).Succeeded());
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        CookInputLease unsealed = {};
        ASSERT_TRUE(Inputs.Acquire({}, &unsealed, nullptr).Succeeded());
        auto input   = Input();
        input.Inputs = &unsealed;
        EXPECT_EQ(other.Initialize(Allocator, input, other_stream, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
    }

    TEST_P(CookPublicationTest, EveryObjectIOFailurePreservesTheOldPackageAndClosesEveryHandle)
    {
        FirstPackage();
        const auto original = Manifest();
        for (uint32_t failure = 0; failure < 9; ++failure)
        {
            SCOPED_TRACE(failure);
            Start(true);
            Faults.Write         = failure == 0 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OK;
            Faults.Flush         = failure == 1 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.CloseWriter   = failure == 2 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.Read          = failure == 3 ? VFS::VFSError::NotFound : VFS::VFSError::OK;
            Faults.CloseReader   = failure == 4 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.Stat          = failure == 5 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OK;
            Faults.RenameObject  = failure == 6 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.CorruptWrites = failure == 7;
            Faults.OpenWriter    = failure == 8 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OK;
            EXPECT_TRUE(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, nullptr).Failed());
            EXPECT_EQ(Faults.LiveFiles, 0u);
            const auto before = Result.Manifest.Digest;
            EXPECT_TRUE(Publisher.PublishManifest(Workspace, &Result, nullptr).Failed());
            EXPECT_EQ(Result.Manifest.Digest, before);
            EXPECT_EQ(Manifest(), original);
            EXPECT_EQ(Faults.ManifestRenames, 0u);
            EXPECT_EQ(Faults.ObjectRenames, failure == 6 ? 1u : 0u);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, EveryManifestStagingFailurePreservesTheOldManifestAndCallerResult)
    {
        FirstPackage();
        const auto original       = Manifest();
        Recipe.SchemaKey.Bytes[0] = 1; // New canonical ledger, so comparison must differ.
        for (uint32_t failure = 0; failure < 9; ++failure)
        {
            SCOPED_TRACE(failure);
            Start();
            Encode();
            Faults.ReadStagingOnly = true;
            Faults.Write           = failure == 0 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OK;
            Faults.Flush           = failure == 1 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.CloseWriter     = failure == 2 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.Read            = failure == 3 ? VFS::VFSError::NotFound : VFS::VFSError::OK;
            Faults.CloseReader     = failure == 4 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.RenameManifest  = failure == 5 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.CorruptWrites   = failure == 6;
            Faults.OpenWriter      = failure == 7 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OK;
            Faults.Stat            = failure == 8 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            const auto previous    = Result;
            EXPECT_TRUE(Publisher.PublishManifest(Workspace, &Result, nullptr).Failed());
            EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
            EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
            EXPECT_EQ(Result.Published, previous.Published);
            EXPECT_EQ(Manifest(), original);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, CheckedManifestComparisonErrorsAreNotTreatedAsCacheMisses)
    {
        FirstPackage();
        const auto original = Manifest();
        for (uint32_t failure = 0; failure < 3; ++failure)
        {
            Start();
            Encode();
            Faults.OpenManifest = failure == 0 ? VFS::VFSError::NotFound : VFS::VFSError::OK;
            Faults.Read         = failure == 1 ? VFS::VFSError::NotFound : VFS::VFSError::OK;
            Faults.CloseReader  = failure == 2 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            EXPECT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Failed());
            EXPECT_TRUE(Diagnostics.HasErrors());
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Stage, CookStage::Publish);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Source.View(), CookManifestName);
            EXPECT_EQ(Faults.WriterOpens, 0u);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, ProducerFailureAndIgnoredSinkFailureCannotInstallOrCommitPartialBytes)
    {
        FirstPackage();
        const auto original = Manifest();
        Start(true);
        Texture.Error = VFS::VFSError::Cancelled;
        EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, nullptr).Error(), VFS::VFSError::Cancelled);
        EXPECT_EQ(Publisher.StagedBytes(), 0u);
        EXPECT_EQ(Faults.ObjectRenames, 0u);
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        Texture.Error             = VFS::VFSError::OK;
        Texture.IgnoreSinkFailure = true;
        // Keep the old manifest within its read limits; fail only the new
        // sink's run-wide staging reservation, even if the producer ignores it.
        Limits.MaxStagedBytes     = sizeof(TextureBytes) - 1;
        Start(true);
        EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_TRUE(Publisher.PublishManifest(Workspace, &Result, nullptr).Failed());
        EXPECT_EQ(Manifest(), original);
    }

    TEST_P(CookPublicationTest, GlobalStagingCapIncludesNewObjectsAndManifestAndDuplicateStagesReleaseTheirCharge)
    {
        FirstPackage();
        const auto original   = Manifest();
        Limits.MaxStagedBytes = sizeof(TextureBytes);
        Start(true);
        ASSERT_TRUE(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, nullptr).Succeeded());
        EXPECT_EQ(Publisher.StagedBytes(), 0u); // Existing identical immutable object.
        ASSERT_TRUE(Publisher.EncodeAsset(MaterialIndex, PackageProducer::Run, &Material, nullptr).Succeeded());
        ASSERT_TRUE(Publisher.EncodeScene(0, {}, PackageProducer::Run, &Scene, nullptr).Succeeded());
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, nullptr).Succeeded());
        EXPECT_FALSE(Result.Published);
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        Recipe.SchemaKey.Bytes[0] = 9;
        Start(true);
        Encode();
        EXPECT_EQ(Publisher.PublishManifest(Workspace, &Result, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Manifest(), original);
    }

    TEST_P(CookPublicationTest, MalformedFutureAndUnreadableManifestsFailClosedWithoutStaging)
    {
        FirstPackage();
        const auto original = Manifest();
        for (const std::string text : {std::string("{"), std::string("{\"manifest_version\":2}"), std::string("{\"manifest_version\":1,\"manifest_version\":1}")})
        {
            ReplaceManifest(text);
            EXPECT_TRUE(Publisher.Initialize(Allocator, Input(), Stream, nullptr).Failed());
            EXPECT_EQ(Manifest(), text);
            EXPECT_FALSE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
            EXPECT_EQ(Faults.WriterOpens, 0u);
        }
        ReplaceManifest(original);
        Faults.OpenManifest = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(Publisher.Initialize(Allocator, Input(), Stream, nullptr).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Manifest(), original);
        EXPECT_FALSE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
    }

    TEST_P(CookPublicationTest, RebuildableKnownV1ShapeNeverAuthorizesCleanupOfUntrustedPaths)
    {
        FirstPackage();
        auto json                           = nlohmann::json::parse(Manifest());
        json["scenes"][0]["object"]["path"] = "untrusted/sentinel";
        ReplaceManifest(json.dump());
        Start();
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Faults.ObjectRenames, 3u); // Full rebuild, immutable collisions verified.
        EXPECT_EQ(Faults.TemporaryRemovals, 3u);
    }

    TEST_P(CookPublicationTest, PreviousLedgerReadStatOpenAndCloseErrorsPreserveThePackageAndReleaseOnlyTheOwnedLock)
    {
        FirstPackage();
        const auto original = Manifest();
        for (uint32_t failure = 0; failure < 4; ++failure)
        {
            SCOPED_TRACE(failure);
            Faults.OpenManifest = failure == 0 ? VFS::VFSError::PermissionDenied : VFS::VFSError::OK;
            Faults.Read         = failure == 1 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.Stat         = failure == 2 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            Faults.CloseReader  = failure == 3 ? VFS::VFSError::IOError : VFS::VFSError::OK;
            EXPECT_EQ(Publisher.Initialize(Allocator, Input(), Stream, &Diagnostics).Error(), failure ? VFS::VFSError::IOError : VFS::VFSError::PermissionDenied);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            EXPECT_EQ(Faults.Writes, 0u);
            EXPECT_EQ(Faults.LockRemovals, 1u);
            EXPECT_FALSE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
            EXPECT_EQ(Manifest(), original);
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Stage, CookStage::Setup);
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, EveryInitializationAllocationRefusalReleasesItsLockAndPreservesThePackage)
    {
        FirstPackage();
        const auto original = Manifest();
        Allocator.Calls     = 0;
        Start();
        const uint32_t calls = Allocator.Calls;
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        const auto outstanding = Scratch.OutstandingAllocations();
        for (uint32_t fail = 1; fail <= calls; ++fail)
        {
            SCOPED_TRACE(fail);
            Allocator.Calls  = 0;
            Allocator.FailAt = fail;
            EXPECT_EQ(Publisher.Initialize(Allocator, Input(), Stream, nullptr).Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(Scratch.OutstandingAllocations(), outstanding);
            EXPECT_FALSE(Root.Exists(PackageRelative(CookWriterLockName)).Value());
            EXPECT_EQ(Manifest(), original);
        }
        Allocator.FailAt = UINT32_MAX;
    }

    TEST_P(CookPublicationTest, SharedPathsCountOnceAndRetainedRootSurvivesContextShutdown)
    {
        Build(true);
        Start();
        const auto extra = Graph.Find({CookAssetKind::Texture, PackageUUID(3)});
        ASSERT_TRUE(Publisher.EncodeAsset(extra, PackageProducer::Run, &Texture, nullptr).Succeeded());
        Context.Shutdown();
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_EQ(Result.ObjectCount, 3u);
        EXPECT_EQ(Result.ReferencedObjectBytes, sizeof(TextureBytes) + sizeof(MaterialBytes) + sizeof(SceneBytes));
        EXPECT_EQ(Publisher.StagedBytes(), Result.ReferencedObjectBytes + Result.Manifest.Bytes);
        EXPECT_EQ(Faults.TemporaryRemovals, 1u);
        EXPECT_EQ(Faults.WritesAfterCommit, 0u);
    }

    TEST_P(CookPublicationTest, PostcommitLockCleanupIsOnlyAWarningAndNeverChangesThePublishedResult)
    {
        Start();
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        const auto digest = Result.Manifest.Digest;
        Faults.RemoveLock = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(Publisher.Reset(&Diagnostics).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Result.Manifest.Digest, digest);
        EXPECT_FALSE(Diagnostics.HasErrors());
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::CleanupWarning);
        EXPECT_EQ(Faults.LockRemovals, 1u);
        EXPECT_TRUE(Publisher.Reset().Succeeded());
        EXPECT_EQ(Faults.LockRemovals, 1u);
        Faults.RemoveLock = VFS::VFSError::OK;
        ASSERT_TRUE(Root.Remove(PackageRelative(CookWriterLockName)).Succeeded()); // Explicit fixture/operator recovery.
    }

    TEST_P(CookPublicationTest, ShortReadsAndWritesPreserveExactBytesAndCanonicalNoOp)
    {
        Faults.MaxWriteChunk = 3;
        Faults.MaxReadChunk  = 5;
        Start();
        Texture.Chunk = Material.Chunk = Scene.Chunk = 7;
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        const auto original = Manifest();
        EXPECT_GT(Faults.Writes, 4u);
        ASSERT_TRUE(Publisher.Reset(&Diagnostics).Succeeded());
        Faults              = {};
        Faults.MaxReadChunk = 1;
        Start();
        Encode();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Faults.Writes, 0u);
        EXPECT_EQ(Texture.Calls + Material.Calls + Scene.Calls, 3u);
        EXPECT_EQ(Manifest(), original);
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    TEST_P(CookPublicationTest, MissingCachedObjectRegeneratesWithoutRepublishingAnUnchangedManifest)
    {
        FirstPackage();
        const auto original = Manifest();
        Start();
        Encode();
        const auto object = Publisher.AssetResults()[TextureIndex].Object;
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        ASSERT_TRUE(Root.Remove(object.Path).Succeeded());
        Faults = {};
        Start();
        Encode();
        EXPECT_EQ(Texture.Calls, 2u);
        EXPECT_EQ(Material.Calls, 1u);
        EXPECT_EQ(Scene.Calls, 1u);
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Faults.ObjectRenames, 1u);
        EXPECT_EQ(Faults.ManifestRenames, 0u);
        EXPECT_EQ(Manifest(), original);
    }

    TEST_P(CookPublicationTest, ExistingCorruptionFailsReuseAndForcedCollisionWithoutOverwritingBytes)
    {
        FirstPackage();
        const auto original = Manifest();
        Start();
        Encode();
        const auto object = Publisher.AssetResults()[TextureIndex].Object;
        ASSERT_TRUE(Publisher.Reset().Succeeded());
        const uint8_t changed = TextureBytes[0] ^ 1;
        const auto    opened  = Backend.Inner->Open(PackagePath("/output").Append(object.Path.CStr()).Value(), VFS::VFSOpenFlags::Write);
        ASSERT_TRUE(opened.Succeeded());
        ASSERT_EQ(opened.Value()->Write({&changed, 1}, 0).Value(), 1u);
        Backend.Inner->Close(opened.Value());
        for (const bool force : {false, true})
        {
            SCOPED_TRACE(force);
            Faults = {};
            Start(force);
            EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(Faults.ObjectRenames, force ? 1u : 0u);
            EXPECT_EQ(Faults.LiveFiles, 0u);
            const auto reader = Root.Open(object.Path, VFS::VFSOpenFlags::Read);
            ASSERT_TRUE(reader.Succeeded());
            uint8_t byte = 0;
            ASSERT_EQ(reader.Value()->Read({&byte, 1}, 0).Value(), 1u);
            EXPECT_EQ(byte, changed);
            ASSERT_TRUE(Root.CloseChecked(reader.Value()).Succeeded());
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
        }
        EXPECT_EQ(Texture.Calls, 2u); // Only the forced run invokes the producer.
    }

    TEST_P(CookPublicationTest, FailedTemporaryRemovalKeepsItsChargeWarnsAndNeverRetries)
    {
        FirstPackage();
        const auto original = Manifest();
        Start(true);
        Faults.RemoveTemporary = VFS::VFSError::PermissionDenied;
        ASSERT_TRUE(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics).Succeeded());
        EXPECT_EQ(Publisher.StagedBytes(), sizeof(TextureBytes));
        EXPECT_EQ(Faults.TemporaryRemovals, 1u);
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::CleanupWarning);
        EXPECT_FALSE(Diagnostics.HasErrors());
        EXPECT_EQ(Manifest(), original);
        ASSERT_TRUE(Publisher.Reset(&Diagnostics).Succeeded());
        EXPECT_EQ(Faults.TemporaryRemovals, 1u);
        EXPECT_EQ(Faults.LiveFiles, 0u);
    }

    TEST_P(CookPublicationTest, InstalledBytesAreVerifiedAgainAfterRenameBeforeAnyManifestCanCommit)
    {
        Start();
        Faults.CorruptInstalled = true;
        EXPECT_EQ(Publisher.EncodeAsset(TextureIndex, PackageProducer::Run, &Texture, &Diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Faults.ObjectRenames, 1u);
        EXPECT_EQ(Faults.ManifestRenames, 0u);
        EXPECT_EQ(Faults.LiveFiles, 0u);
        EXPECT_EQ(Publisher.StagedBytes(), sizeof(TextureBytes)); // Installed orphan is not deleted.
        EXPECT_TRUE(Publisher.PublishManifest(Workspace, &Result, nullptr).Failed());
        EXPECT_EQ(Result.Manifest.Bytes, 0u);
        EXPECT_FALSE(Root.Exists(PackageRelative(CookManifestName)).Value());
    }

    TEST_P(CookPublicationTest, MissingResultsInvalidWorkspaceAndNullOutputNeverCommit)
    {
        FirstPackage();
        const auto original = Manifest();
        for (uint32_t failure = 0; failure < 4; ++failure)
        {
            SCOPED_TRACE(failure);
            Start();
            if (failure)
                Encode();
            const auto previous    = Result;
            const auto workspace   = failure == 2 ? Core::Containers::ArrayView<uint32_t>{nullptr, 3} : failure == 3 ? Core::Containers::ArrayView<uint32_t>{Workspace, 2} : Core::Containers::ArrayView<uint32_t>{Workspace};
            const auto charges     = Allocator.ChargeCalls;
            Allocator.FixedFailure = VFS::VFSError::Unsupported;
            EXPECT_TRUE(Publisher.PublishManifest(workspace, failure == 1 ? nullptr : &Result, nullptr).Failed());
            EXPECT_EQ(Allocator.ChargeCalls, charges);
            Allocator.FixedFailure = VFS::VFSError::OK;
            EXPECT_EQ(Result.Manifest.Digest, previous.Manifest.Digest);
            EXPECT_EQ(Result.Manifest.Bytes, previous.Manifest.Bytes);
            EXPECT_EQ(Result.Published, previous.Published);
            EXPECT_EQ(Faults.ManifestRenames, 0u);
            EXPECT_EQ(Manifest(), original);
            ASSERT_TRUE(Publisher.Reset().Succeeded());
            Faults = {};
        }
    }

    TEST_P(CookPublicationTest, HeldOpenManifestAndOwnedOldSnapshotRemainConsistentAcrossPublication)
    {
        FirstPackage();
        const auto   old_bytes = Manifest();
        CookManifest snapshot  = {};
        ASSERT_EQ(snapshot.Read({reinterpret_cast<const uint8_t*>(old_bytes.data()), old_bytes.size()}, Recipe.Target, Limits, Allocator, &Diagnostics).Value(), CookManifestReadStatus::Valid);
        VFS::VFSReadRootLease reader = {};
        ASSERT_TRUE(Context.AcquireReadRoot(PackagePath("/output"), &reader).Succeeded());
        const auto held = reader.Open(PackageRelative(CookManifestName));
        ASSERT_TRUE(held.Succeeded());
        TextureBytes[0] ^= 4;
        Start(true);
        Encode();
        const auto old_object = snapshot.FindAsset({CookAssetKind::Texture, PackageUUID(1)})->Result.Object;
        EXPECT_NE(Publisher.AssetResults()[TextureIndex].Object, old_object);
        Context.Shutdown();
        ASSERT_TRUE(Publisher.PublishManifest(Workspace, &Result, &Diagnostics).Succeeded());
        ASSERT_TRUE(Result.Published);
        ASSERT_TRUE(Publisher.Reset(&Diagnostics).Succeeded());
        std::string captured(old_bytes.size(), '\0');
        ASSERT_EQ(held.Value()->ReadAll({reinterpret_cast<uint8_t*>(captured.data()), captured.size()}).Value(), captured.size());
        EXPECT_EQ(captured, old_bytes);
        ASSERT_TRUE(reader.CloseChecked(held.Value()).Succeeded());
        ASSERT_TRUE(VerifyCookObject(CookObjectReader(reader), old_object, "png", Stream, Allocator).Value());
        const auto new_bytes = Manifest();
        EXPECT_NE(new_bytes, old_bytes);
        CookManifest fresh = {};
        ASSERT_EQ(fresh.Read({reinterpret_cast<const uint8_t*>(new_bytes.data()), new_bytes.size()}, Recipe.Target, Limits, Allocator, &Diagnostics).Value(), CookManifestReadStatus::Valid);
        EXPECT_NE(fresh.FindAsset({CookAssetKind::Texture, PackageUUID(1)})->Result.Object, old_object);
        EXPECT_EQ(snapshot.FindAsset({CookAssetKind::Texture, PackageUUID(1)})->Result.Object, old_object);
        EXPECT_EQ(Faults.WritesAfterCommit, 0u);
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    INSTANTIATE_TEST_SUITE_P(MemoryAndDisk, CookPublicationTest, testing::Bool());
#else
    INSTANTIATE_TEST_SUITE_P(MemoryOnly, CookPublicationTest, testing::Values(false));
#endif
} // namespace ZEngine::Cook
