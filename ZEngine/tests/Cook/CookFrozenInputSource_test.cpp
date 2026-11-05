#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookVFSWorkingMemory.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <string>
#include "../TemporaryDirectory.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSPath InputPath(cstring text)
        {
            return VFS::VFSPath::Parse(text).Value();
        }

        VFS::VFSRelativePath InputRelative(cstring text)
        {
            return VFS::VFSRelativePath::Parse(text).Value();
        }

        CookAssetIdentity InputIdentity(CookAssetKind kind, cstring uuid = "00000000-0000-0000-0000-000000000001")
        {
            return {.Kind = kind, .UUID = uuids::uuid::from_string(uuid).value()};
        }

        constexpr cstring InputMetadata  = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"TextureImporter","settings":[],"source_path":"/not-present/original.png","artifact_path":"/assets/a.png"})";
        constexpr cstring SecondMetadata = R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"ImportedAsset","settings":[]})";

        struct InputCatalog
        {
            Core::Containers::FixedArray<CookAssetCatalogEntry, 8> Entries = {};
            uint32_t                                               Count   = 0;

            static VFS::VFSResult<void>                            Visit(void* context, const CookAssetCatalogEntry& entry)
            {
                auto& self = *static_cast<InputCatalog*>(context);
                if (self.Count == self.Entries.size())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                self.Entries[self.Count++] = entry;
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct CapturedMaterialSink final : ICookByteSink
        {
            std::string          Bytes = {};
            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                Bytes.append(reinterpret_cast<cstring>(bytes.data()), bytes.size());
                return VFS::VFSResult<void>::Ok();
            }
            static VFS::VFSResult<void> Emit(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                return static_cast<CapturedMaterialSink*>(context)->Write(bytes);
            }
        };
    } // namespace

    class CookFrozenInputSourceTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager   Memory      = {};
        VFS::VFSMemoryBackend         Backend     = {};
        VFS::VFSContext               Context     = {};
        FixedAllocator<ZKilo(256), 8> Allocator   = {};
        CookFrozenLibraryOwner        Owner       = {};
        VFS::VFSScratchRootLease      Scratch     = {};
        CookLimits                    Limits      = {.MaxWorkingBytes = ZKilo(256)};
        CookDiagnostics               Diagnostics = {};

        void                          SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(4), 128).Succeeded());
            for (cstring path : {"/assets", "/scenes", "/_cache/envmaps", "/scratch"})
            {
                ASSERT_TRUE(Backend.CreateDir(InputPath(path)).Succeeded());
            }
            Write("/assets/a.png", "encoded-image");
            Write("/assets/a.png.meta", InputMetadata);
            Write("/scenes/main.yaml", "saved-scene");
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            const CookFrozenRootRequest roots[] = {
                { .Key = "scenes", .Context = &Context,         .Path = InputPath("/scenes")},
                { .Key = "assets", .Context = &Context,         .Path = InputPath("/assets")},
                {.Key = "envmaps", .Context = &Context, .Path = InputPath("/_cache/envmaps")},
            };
            ASSERT_TRUE(Owner.Initialize(Allocator, roots).Succeeded());
            ASSERT_TRUE(Context.AcquireScratchRoot(InputPath("/scratch"), &Scratch).Succeeded());
            ASSERT_TRUE(Scratch.CreateDirExclusive(InputRelative("run")).Succeeded());
        }

        void TearDown() override
        {
            EXPECT_TRUE(Scratch.Remove(InputRelative("run")).Succeeded());
            Scratch.Reset();
            EXPECT_TRUE(Owner.Reset().Succeeded());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            Context.Shutdown();
        }

        void Write(cstring path, Core::Containers::StringView bytes)
        {
            ASSERT_TRUE(Backend.WriteFile(InputPath(path), {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}).Succeeded());
        }

        CookCaptureContext CaptureContext()
        {
            return {.Scratch = &Scratch, .ScratchRun = InputRelative("run"), .Allocator = &Allocator, .Limits = &Limits};
        }

        void AddEnvironment(cstring metadata)
        {
            Write("/assets/sky.hdr", "environment-source");
            Write("/assets/sky.hdr.meta", metadata);
            Write("/_cache/envmaps/sky.zenvmap", "imported-environment");
        }
    };

    TEST_F(CookFrozenInputSourceTest, CheckedInputRootReservationsShareTheScratchDomainAndDrainBeforeAllocatorTeardown)
    {
        CookBudgetAllocator budget = {};
        ASSERT_TRUE(budget.Initialize(Allocator, ZMega(8)).Succeeded());
        CookVFSWorkingMemory vfs_memory = {};
        ASSERT_TRUE(vfs_memory.Initialize(budget).Succeeded());
        VFS::VFSWorkingMemoryReservation scratch_memory = {};
        ASSERT_TRUE(Scratch.ReserveWorkingMemory(vfs_memory, &scratch_memory).Succeeded());
        const auto            baseline = vfs_memory.ReservedBytes();
        const auto            profile  = Backend.RootProvider()->MemoryRequirements().Value();
        CookFrozenInputSource source(Owner);
        CookInputLease        lease  = {};
        CookLimits            limits = Limits;
        limits.MaxWorkingBytes       = ZMega(8);
        auto context                 = CaptureContext();
        context.Allocator            = &budget;
        context.Limits               = &limits;
        context.WorkingMemory        = &vfs_memory;
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        ASSERT_TRUE(lease.PrepareCapture(context, &Diagnostics).Succeeded());
        EXPECT_EQ(vfs_memory.ReservedBytes(), baseline + 3 * profile.StateBytes);
        EXPECT_EQ(vfs_memory.DomainCount(), 2u);
        EXPECT_EQ(vfs_memory.ReservationCount(), 4u);
        VFS::VFSRelativePath logical = {};
        ASSERT_TRUE(lease.MapContextPath(InputPath("/scenes/main.yaml"), &logical, &Diagnostics).Succeeded());
        CookCapturedFile capture = {};
        ASSERT_TRUE(lease.Capture(logical, &capture, &Diagnostics).Succeeded());
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        lease.Reset();
        EXPECT_EQ(vfs_memory.ReservedBytes(), baseline);
        EXPECT_EQ(source.ReleaseStorage().Error(), VFS::VFSError::AlreadyExists);
        uint8_t byte = 0;
        ASSERT_EQ(capture.Read(0, {&byte, 1}).Value(), 1u);
        EXPECT_EQ(byte, 's');
        ASSERT_TRUE(capture.Reset().Succeeded());
        ASSERT_TRUE(source.ReleaseStorage().Succeeded());
        EXPECT_EQ(budget.OutstandingAllocations(), 0u);
        scratch_memory.Reset();
        EXPECT_EQ(vfs_memory.ReservedBytes(), 0u);
        ASSERT_TRUE(vfs_memory.Reset().Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, PartiallyAcceptedInputRootReservationsAreReleasedByFailedLeaseReset)
    {
        constexpr uint64_t  maximum = ZMega(8);
        CookBudgetAllocator budget  = {};
        ASSERT_TRUE(budget.Initialize(Allocator, maximum).Succeeded());
        CookVFSWorkingMemory vfs_memory = {};
        ASSERT_TRUE(vfs_memory.Initialize(budget).Succeeded());
        VFS::VFSWorkingMemoryReservation probe = {};
        ASSERT_TRUE(Scratch.ReserveWorkingMemory(vfs_memory, &probe).Succeeded());
        const auto first_root_bytes = vfs_memory.ReservedBytes();
        probe.Reset();
        const auto before = budget.Statistics().LiveBytes;
        const auto array  = budget.Allocate(3 * sizeof(VFS::VFSWorkingMemoryReservation), alignof(VFS::VFSWorkingMemoryReservation), CookMemoryCategory::Captures);
        ASSERT_TRUE(array.Succeeded());
        const auto array_bytes = budget.Statistics().LiveBytes - before;
        budget.Free(array.Value());
        CookMemoryCharge blocker = {};
        ASSERT_TRUE(budget.TryCharge(maximum - budget.Statistics().LiveBytes - first_root_bytes - array_bytes, CookMemoryCategory::Codecs, &blocker).Succeeded());
        CookFrozenInputSource source(Owner);
        CookInputLease        lease  = {};
        CookLimits            limits = Limits;
        limits.MaxWorkingBytes       = maximum;
        auto context                 = CaptureContext();
        context.Allocator            = &budget;
        context.Limits               = &limits;
        context.WorkingMemory        = &vfs_memory;
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        EXPECT_EQ(lease.PrepareCapture(context, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(vfs_memory.ReservationCount(), 1u);
        EXPECT_EQ(vfs_memory.ReservedBytes(), first_root_bytes);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        lease.Reset();
        EXPECT_EQ(vfs_memory.ReservationCount(), 0u);
        EXPECT_EQ(vfs_memory.ReservedBytes(), 0u);
        ASSERT_TRUE(source.ReleaseStorage().Succeeded());
        EXPECT_EQ(budget.OutstandingAllocations(), 0u);
        ASSERT_TRUE(vfs_memory.Reset().Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, UnaffordableInputPoolFailsBeforeCatalogOrCaptureFilesAndReleasesFailedRootStorage)
    {
        CookBudgetAllocator budget = {};
        ASSERT_TRUE(budget.Initialize(Allocator, ZMega(1)).Succeeded());
        CookVFSWorkingMemory vfs_memory = {};
        ASSERT_TRUE(vfs_memory.Initialize(budget).Succeeded());
        CookFrozenInputSource source(Owner);
        CookInputLease        lease   = {};
        auto                  context = CaptureContext();
        context.Allocator             = &budget;
        context.WorkingMemory         = &vfs_memory;
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        EXPECT_EQ(lease.PrepareCapture(context, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        EXPECT_EQ(vfs_memory.ReservationCount(), 0u);
        const auto entries = Backend.List(&Memory.MainArena, InputPath("/scratch/run"));
        ASSERT_TRUE(entries.Succeeded());
        EXPECT_TRUE(entries.Value().empty());
        EXPECT_EQ(source.ReleaseStorage().Error(), VFS::VFSError::AlreadyExists);
        lease.Reset();
        ASSERT_TRUE(source.ReleaseStorage().Succeeded());
        EXPECT_EQ(budget.OutstandingAllocations(), 0u);
        ASSERT_TRUE(vfs_memory.Reset().Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, CatalogDiscoveryUsesActualCapacityAndNoPerEntryAllocationOrRawSourceReads)
    {
        Write("/assets/raw.glb", "not-an-imported-asset");
        Write("/assets/b.zemesh", "mesh-payload");
        Write("/assets/b.zemesh.meta", SecondMetadata);
        Write("/assets/c.zematerial", "material-payload");
        Write("/assets/c.zematerial.meta", R"({"uuid":"00000000-0000-0000-0000-000000000003","importer":"MaterialImporter","settings":[]})");
        const auto            allocations = Allocator.Allocations;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        EXPECT_EQ(Allocator.Allocations - allocations, 3u); // stream, discovery array, catalog/index block.
        InputCatalog catalog = {};
        ASSERT_TRUE(lease.EnumerateCatalog(InputCatalog::Visit, &catalog, &Diagnostics).Succeeded());
        ASSERT_EQ(catalog.Count, 3u);
        EXPECT_EQ(catalog.Entries[0].Identity.Kind, CookAssetKind::Material);
        EXPECT_EQ(catalog.Entries[1].Identity.Kind, CookAssetKind::Mesh);
        EXPECT_EQ(catalog.Entries[2].Identity.Kind, CookAssetKind::Texture);
        for (uint32_t i = 1; i < catalog.Count; ++i)
        {
            EXPECT_TRUE(catalog.Entries[i - 1].Identity < catalog.Entries[i].Identity);
        }
        const auto            after_acquire = Allocator.Allocations;
        CookAssetCatalogEntry entry         = {};
        ASSERT_TRUE(lease.FindAsset(InputIdentity(CookAssetKind::Texture), &entry, &Diagnostics).Succeeded());
        EXPECT_EQ(entry.Artifact, InputRelative("assets/a.png"));
        EXPECT_EQ(entry.Metadata, InputRelative("assets/a.png.meta"));
        VFS::VFSRelativePath scene = {};
        ASSERT_TRUE(lease.MapContextPath(InputPath("/scenes/main.yaml"), &scene, &Diagnostics).Succeeded());
        EXPECT_EQ(scene, InputRelative("scenes/main.yaml"));
        CookCapturedFile captured = {};
        ASSERT_TRUE(lease.Capture(scene, &captured, &Diagnostics).Succeeded());
        EXPECT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        EXPECT_EQ(Allocator.Allocations, after_acquire);
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
    }

    TEST_F(CookFrozenInputSourceTest, RootedPreflightHoldsExclusionWithoutAllocatingOrCreatingCaptureStorage)
    {
        ASSERT_TRUE(Scratch.Remove(InputRelative("run")).Succeeded());
        ASSERT_TRUE(Backend.CreateDir(InputPath("/output")).Succeeded());
        VFS::VFSPublicationRootLease output = {};
        ASSERT_TRUE(Context.AcquirePublicationRoot(InputPath("/output"), &output).Succeeded());
        const auto            allocations = Allocator.Allocations;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        EXPECT_EQ(lease.State(), CookInputState::Rooted);
        ASSERT_TRUE(lease.RequireDisjoint(output, &Diagnostics).Succeeded());
        ASSERT_TRUE(lease.RequireDisjoint(Scratch, &Diagnostics).Succeeded());
        ASSERT_FALSE(Scratch.Overlaps(output).Value());
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_FALSE(Scratch.Exists(InputRelative("run")).Value());
        CookLibraryMutationLease mutation = {};
        EXPECT_EQ(Owner.TryAcquireMutation(&mutation).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_TRUE(Owner.Reset().Failed());
        EXPECT_EQ(lease.Seal(nullptr).Error(), VFS::VFSError::InvalidPath);
        // Only after every physical root check may the orchestrator create its
        // owned run directory and ask the same held generation to build captures.
        ASSERT_TRUE(Scratch.CreateDirExclusive(InputRelative("run")).Succeeded());
        ASSERT_TRUE(lease.PrepareCapture(CaptureContext(), &Diagnostics).Succeeded());
        EXPECT_EQ(lease.State(), CookInputState::Active);
        InputCatalog catalog = {};
        ASSERT_TRUE(lease.EnumerateCatalog(InputCatalog::Visit, &catalog, &Diagnostics).Succeeded());
        EXPECT_EQ(catalog.Count, 1u);
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    TEST_F(CookFrozenInputSourceTest, RootedPreflightRejectsEqualAncestorAndDescendantInputOutputRootsWithoutCaptureWrites)
    {
        ASSERT_TRUE(Scratch.Remove(InputRelative("run")).Succeeded());
        ASSERT_TRUE(Backend.CreateDir(InputPath("/assets/nested")).Succeeded());
        const auto            allocations = Allocator.Allocations;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        for (cstring path : {"/assets", "/", "/assets/nested", "/scenes"})
        {
            VFS::VFSPublicationRootLease output = {};
            ASSERT_TRUE(Context.AcquirePublicationRoot(InputPath(path), &output).Succeeded());
            EXPECT_EQ(lease.RequireDisjoint(output, &Diagnostics).Error(), VFS::VFSError::InvalidPath) << path;
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::InvalidRequest);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Underlying, VFS::VFSError::PermissionDenied);
            EXPECT_EQ(lease.State(), CookInputState::Rooted);
        }
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_FALSE(Scratch.Exists(InputRelative("run")).Value());
        lease.Reset();
        CookLibraryMutationLease mutation = {};
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        ASSERT_TRUE(Scratch.CreateDirExclusive(InputRelative("run")).Succeeded()); // Fixture cleanup contract.
    }

    TEST_F(CookFrozenInputSourceTest, AbandonedRootedLeaseNeverBuildsTheCatalogOrPerformsASeal)
    {
        const auto allocations = Allocator.Allocations;
        {
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
            EXPECT_EQ(lease.State(), CookInputState::Rooted);
        }
        EXPECT_EQ(Allocator.Allocations, allocations);
        CookLibraryMutationLease mutation = {};
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    TEST_F(CookFrozenInputSourceTest, FailedCapturePreparationKeepsTheFailedLeaseBoundUntilResetAndNeverRetries)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        auto invalid   = CaptureContext();
        invalid.Limits = nullptr;
        EXPECT_EQ(lease.PrepareCapture(invalid, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        EXPECT_EQ(lease.PrepareCapture(CaptureContext(), nullptr).Error(), VFS::VFSError::InvalidPath);
        CookLibraryMutationLease mutation = {};
        EXPECT_EQ(Owner.TryAcquireMutation(&mutation).Error(), VFS::VFSError::AlreadyExists);
        lease.Reset();
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, OnlyCompleteFixedObjectsAreCoveredAndBindingBackingAlwaysRemainsCharged)
    {
        struct Coverage final : ICookMemoryCoverage
        {
            const void* Source      = nullptr;
            const void* Owner       = nullptr;
            size_t      SourceBytes = 0;
            size_t      OwnerBytes  = 0;

            bool        Contains(const void* object, size_t bytes) const noexcept override
            {
                return (object == Source && bytes <= SourceBytes) || (object == Owner && bytes <= OwnerBytes);
            }
        } coverage = {};
        CookFrozenInputSource source(Owner);
        coverage.Source      = &source;
        coverage.Owner       = &Owner;
        const auto ordinary  = source.FixedWorkingBytes(Allocator).Value();
        const auto live      = Allocator.Statistics().LiveBytes;
        const auto calls     = Allocator.Allocations;
        coverage.SourceBytes = sizeof(source) - 1;
        coverage.OwnerBytes  = sizeof(Owner) - 1;
        EXPECT_EQ(source.UncoveredFixedWorkingBytes(Allocator, coverage).Value(), ordinary);
        coverage.SourceBytes = sizeof(source);
        EXPECT_EQ(source.UncoveredFixedWorkingBytes(Allocator, coverage).Value(), ordinary - sizeof(source));
        coverage.SourceBytes = sizeof(source) - 1;
        coverage.OwnerBytes  = sizeof(Owner);
        EXPECT_EQ(source.UncoveredFixedWorkingBytes(Allocator, coverage).Value(), ordinary - sizeof(Owner));
        coverage.SourceBytes = sizeof(source);
        const auto binding   = source.UncoveredFixedWorkingBytes(Allocator, coverage);
        ASSERT_TRUE(binding.Succeeded());
        EXPECT_EQ(binding.Value(), ordinary - sizeof(source) - sizeof(Owner));
        EXPECT_GT(binding.Value(), 0u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        EXPECT_EQ(Allocator.Allocations, calls);
        CookLibraryMutationLease mutation = {};
        EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, ExplicitStorageReleaseRefusesLiveOwnersAndReturnsEveryRunAllocationBeforeProviderDestruction)
    {
        const auto            before = Allocator.Statistics().LiveBytes;
        CookFrozenInputSource source(Owner);
        EXPECT_EQ(source.FixedWorkingBytes(Allocator).Value(), sizeof(source) + Owner.FixedWorkingBytes(Allocator).Value());
        EXPECT_GT(Owner.FixedWorkingBytes(Allocator).Value(), sizeof(Owner));
        CookInputLease lease = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        EXPECT_EQ(source.ReleaseStorage().Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(lease.PrepareCapture(CaptureContext(), &Diagnostics).Succeeded());
        CookCapturedFile capture = {};
        ASSERT_TRUE(lease.Capture(InputRelative("scenes/main.yaml"), &capture, &Diagnostics).Succeeded());
        const auto live = Allocator.Statistics().LiveBytes;
        EXPECT_GT(live, before);
        EXPECT_EQ(source.ReleaseStorage().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        lease.Reset();
        EXPECT_EQ(source.ReleaseStorage().Error(), VFS::VFSError::AlreadyExists);
        uint8_t bytes[11] = {};
        ASSERT_TRUE(ReadCookCapturedRange(capture, 0, bytes).Succeeded());
        ASSERT_TRUE(capture.Reset().Succeeded());
        ASSERT_TRUE(source.ReleaseStorage().Succeeded());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, before);
        const auto frees = Allocator.Frees;
        ASSERT_TRUE(source.ReleaseStorage().Succeeded());
        EXPECT_EQ(Allocator.Frees, frees);
        EXPECT_TRUE(source.AcquireRoots(&lease, nullptr).Failed()); // One run, not reuse.
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    TEST_F(CookFrozenInputSourceTest, FailedPreparationStorageCanBeReleasedWithTheFailedProviderStillAlive)
    {
        const auto            before = Allocator.Statistics().LiveBytes;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, &Diagnostics).Succeeded());
        auto context       = CaptureContext();
        context.ScratchRun = InputRelative("missing-run");
        EXPECT_EQ(lease.PrepareCapture(context, &Diagnostics).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        EXPECT_GT(Allocator.Statistics().LiveBytes, before); // Allocated stream before store setup failed.
        EXPECT_EQ(source.ReleaseStorage().Error(), VFS::VFSError::AlreadyExists);
        lease.Reset();
        ASSERT_TRUE(source.ReleaseStorage().Succeeded());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, before);
    }

    TEST_F(CookFrozenInputSourceTest, DependencyResolutionChecksOriginalLogicalAndContextSpellingAgainstTypedCatalog)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        for (cstring text : {"assets/a.png", "/assets/a.png", ""})
        {
            const CookDependency  dependency = {InputIdentity(CookAssetKind::Texture), "albedo", text};
            CookAssetCatalogEntry output     = {};
            ASSERT_TRUE(ResolveCookDependency(lease, dependency, &output, &Diagnostics).Succeeded());
            EXPECT_EQ(output.Identity, dependency.Identity);
            EXPECT_EQ(output.Artifact, InputRelative("assets/a.png"));
        }
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
        EXPECT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        CookAssetCatalogEntry prior      = {.Identity = InputIdentity(CookAssetKind::Mesh)};
        const CookDependency  dependency = {InputIdentity(CookAssetKind::Texture), "albedo", "assets/a.png"};
        EXPECT_EQ(ResolveCookDependency(lease, dependency, &prior, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(prior.Identity.Kind, CookAssetKind::Mesh);
    }

    TEST_F(CookFrozenInputSourceTest, DependencyResolutionRejectsUUIDPathDisagreementAndOriginalAliasesWithoutFallback)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        for (cstring text : {"assets/b.png", "/assets/b.png", "assets/./a.png", "/assets/./a.png", "/assets//a.png", "/assets/../assets/a.png", "/assets/a.png/", "C:/assets/a.png", "/not-declared/a.png"})
        {
            const CookDependency  dependency = {InputIdentity(CookAssetKind::Texture), "albedo", text};
            CookAssetCatalogEntry output     = {.Identity = InputIdentity(CookAssetKind::Mesh)};
            Diagnostics.Clear();
            EXPECT_TRUE(ResolveCookDependency(lease, dependency, &output, &Diagnostics).Failed()) << text;
            EXPECT_EQ(output.Identity.Kind, CookAssetKind::Mesh);
            EXPECT_TRUE(Diagnostics.HasErrors());
        }
        const CookDependency  missing = {InputIdentity(CookAssetKind::Texture, "00000000-0000-0000-0000-000000000099"), "albedo", "assets/a.png"};
        CookAssetCatalogEntry output  = {};
        Diagnostics.Clear();
        EXPECT_EQ(ResolveCookDependency(lease, missing, &output, &Diagnostics).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::MissingDependency);
        EXPECT_STREQ(Diagnostics.Entries[Diagnostics.Count - 1].FieldKey, "albedo");
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Asset, missing.Identity);
    }

    TEST_F(CookFrozenInputSourceTest, MaterialEncodingUsesCapturedGenerationAfterSealAndOriginalFacadeTeardown)
    {
        Importers::AssetCodec::MaterialAssetDocument document = {};
        document.UUID                                         = InputIdentity(CookAssetKind::Material, "00000000-0000-0000-0000-000000000002").UUID;
        document.Name[0]                                      = 'M';
        document.Textures[0].UUID                             = InputIdentity(CookAssetKind::Texture).UUID;
        Helpers::secure_memcpy(document.Textures[0].Path, sizeof(document.Textures[0].Path), "assets/a.png", sizeof("assets/a.png"));
        Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
        paths[0]                                                    = InputRelative("assets/a.png");
        CapturedMaterialSink original                               = {};
        ASSERT_TRUE(Importers::AssetCodec::WriteCanonicalMaterialAsset(document, paths, CapturedMaterialSink::Emit, &original).Succeeded());
        Write("/assets/material.zematerial", {original.Bytes.data(), original.Bytes.size()});
        Write("/assets/material.zematerial.meta", SecondMetadata);

        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        CookAssetCatalogEntry material = {};
        ASSERT_TRUE(lease.FindAsset({CookAssetKind::Material, document.UUID}, &material, &Diagnostics).Succeeded());
        CookCapturedFile artifact = {}, sidecar = {};
        ASSERT_TRUE(lease.Capture(material.Artifact, &artifact, &Diagnostics).Succeeded());
        ASSERT_TRUE(lease.Capture(material.Metadata, &sidecar, &Diagnostics).Succeeded());
        uint8_t metadata_bytes[512] = {};
        ASSERT_LE(sidecar.Size(), sizeof(metadata_bytes));
        ASSERT_TRUE(ReadCookCapturedRange(sidecar, 0, {metadata_bytes, static_cast<size_t>(sidecar.Size())}).Succeeded());
        VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(VFS::MetaFileIO::ParseStrict({metadata_bytes, static_cast<size_t>(sidecar.Size())}, &metadata).Succeeded());

        FixedAllocator<ZKilo(64), 1> scratch_parent = {};
        CookScratchAllocator         scratch        = {};
        ASSERT_TRUE(scratch_parent.Ledger.Initialize(ZKilo(128)).Succeeded());
        ASSERT_TRUE(scratch.Initialize(scratch_parent, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        const CookAssetInput   input   = {material.Identity, &artifact, nullptr, &metadata, &Limits, material.Artifact};
        const auto             adapter = CookMaterialAdapter();
        CookResolvedDependency child   = {};
        {
            CookAssetInspection inspection = {};
            ASSERT_TRUE(adapter.Inspect(nullptr, input, scratch, &inspection, &Diagnostics).Succeeded());
            ASSERT_EQ(inspection.Dependencies().size(), 1u);
            CookAssetCatalogEntry texture = {};
            ASSERT_TRUE(ResolveCookDependency(lease, inspection.Dependencies()[0], &texture, &Diagnostics).Succeeded());
            // This fixture tests material/frozen-input lifetime, not the texture
            // adapter or a published package. Descriptor verification is separate.
            child = {"albedo", texture.Identity, {}, "image-png", InputRelative("objects/1111111111111111111111111111111111111111111111111111111111111111.png"), Digest256::ParseHex("1111111111111111111111111111111111111111111111111111111111111111").Value(), 12};
        }
        EXPECT_EQ(scratch.OutstandingAllocations(), 0u);
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        lease.Reset();
        ASSERT_TRUE(Owner.Reset().Succeeded());
        Context.Shutdown();
        Write("/assets/material.zematerial", "changed-original");
        Write("/assets/material.zematerial.meta", "invalid-sidecar");
        Write("/assets/a.png", "changed-texture");
        CapturedMaterialSink cooked = {};
        ASSERT_TRUE(adapter.Encode(nullptr, input, {&child, 1}, CookTarget::DesktopVulkanLE64, cooked, scratch, &Diagnostics).Succeeded());
        Importers::AssetCodec::MaterialAssetDocument decoded = {};
        ASSERT_TRUE(Importers::AssetCodec::ReadMaterialAssetDocument({reinterpret_cast<const uint8_t*>(cooked.Bytes.data()), cooked.Bytes.size()}, &decoded, document.UUID).Succeeded());
        EXPECT_STREQ(decoded.Name, "M");
        EXPECT_STREQ(decoded.Textures[0].Path, child.ObjectPath.CStr());
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
        EXPECT_EQ(scratch_parent.Allocations, 1u);
        EXPECT_EQ(scratch.OutstandingAllocations(), 0u);
    }

    TEST_F(CookFrozenInputSourceTest, UnsupportedTypedTexturesRemainInCatalogAndDoNotInvokeImporters)
    {
        constexpr cstring paths[][2] = {
            {"/assets/b.KTX2", "/assets/b.KTX2.meta"},
            { "/assets/b.pic",  "/assets/b.pic.meta"}
        };
        for (const auto& paths_pair : paths)
        {
            cstring path = paths_pair[0];
            Write(path, "not-validated-until-reachable");
            const auto metadata = InputPath(paths_pair[1]);
            Write(metadata.CStr(), SecondMetadata);
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
            CookAssetCatalogEntry entry = {};
            ASSERT_TRUE(lease.FindAsset(InputIdentity(CookAssetKind::Texture, "00000000-0000-0000-0000-000000000002"), &entry, nullptr).Succeeded());
            EXPECT_EQ(entry.Artifact.View(), Core::Containers::StringView(path + 1));
            EXPECT_TRUE(lease.Seal(nullptr).Succeeded());
            lease.Reset();
            ASSERT_TRUE(Backend.Remove(InputPath(path)).Succeeded());
            ASSERT_TRUE(Backend.Remove(metadata).Succeeded());
        }
    }

    TEST_F(CookFrozenInputSourceTest, MissingMalformedNilOrDuplicateMetadataFailsWithoutRepairingTheLibrary)
    {
        constexpr cstring invalid[] = {"", "{}", R"({"uuid":"00000000-0000-0000-0000-000000000000","importer":"ImportedAsset","settings":[]})", R"({"uuid":"00000000-0000-0000-0000-000000000001","uuid":"00000000-0000-0000-0000-000000000001","importer":"ImportedAsset","settings":[]})"};
        const auto        live      = Allocator.Statistics().LiveBytes;
        for (cstring text : invalid)
        {
            ASSERT_TRUE(Backend.Remove(InputPath("/assets/a.png.meta")).Succeeded());
            if (text[0])
            {
                Write("/assets/a.png.meta", text);
            }
            {
                CookFrozenInputSource source(Owner);
                CookInputLease        lease = {};
                Diagnostics.Clear();
                EXPECT_EQ(source.Acquire(CaptureContext(), &lease, &Diagnostics).Error(), VFS::VFSError::Corrupted);
                EXPECT_FALSE(lease.IsValid());
                ASSERT_GT(Diagnostics.Count, 0u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidMetadata);
                CookLibraryMutationLease mutation = {};
                EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
            }
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
            if (!text[0])
            {
                EXPECT_EQ(Backend.Stat(InputPath("/assets/a.png.meta")).Error(), VFS::VFSError::NotFound);
                Write("/assets/a.png.meta", InputMetadata);
            }
        }
    }

    TEST_F(CookFrozenInputSourceTest, UUIDCollisionsAcrossKindsAreRejected)
    {
        Write("/assets/b.zemesh", "mesh");
        Write("/assets/b.zemesh.meta", InputMetadata);
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        EXPECT_EQ(source.Acquire(CaptureContext(), &lease, &Diagnostics).Error(), VFS::VFSError::Corrupted);
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::IdentityConflict);
        EXPECT_FALSE(lease.IsValid());
    }

    TEST_F(CookFrozenInputSourceTest, CountSidecarAndAggregateCaptureLimitsAreEnforcedDuringAcquisition)
    {
        for (uint32_t limit = 0; limit < 3; ++limit)
        {
            Limits = {.MaxWorkingBytes = ZKilo(256)};
            if (limit == 0)
            {
                Limits.MaxCatalogEntries = 0;
            }
            else if (limit == 1)
            {
                Limits.MaxSidecarBytes = 1;
            }
            else
            {
                Limits.MaxCapturedBytes = 1;
            }
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            Diagnostics.Clear();
            EXPECT_EQ(source.Acquire(CaptureContext(), &lease, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_FALSE(lease.IsValid());
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        }
    }

    TEST_F(CookFrozenInputSourceTest, EveryAcquisitionAllocationFailureReleasesExclusionAndOwnedFiles)
    {
        const auto live = Allocator.Statistics().LiveBytes;
        for (uint32_t fail_at = 1; fail_at <= 3; ++fail_at)
        {
            struct FailingAllocator final : ICookAllocator
            {
                ICookAllocator*      Parent = nullptr;
                uint32_t             Calls  = 0;
                uint32_t             FailAt = 0;
                VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
                {
                    return Parent->TryCharge(bytes, category, output);
                }
                VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
                {
                    return ++Calls == FailAt ? VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory) : Parent->Allocate(bytes, alignment, category);
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
            failing.FailAt = fail_at;
            {
                CookFrozenInputSource source(Owner);
                CookInputLease        lease   = {};
                auto                  capture = CaptureContext();
                capture.Allocator             = &failing;
                EXPECT_EQ(source.Acquire(capture, &lease, &Diagnostics).Error(), VFS::VFSError::OutOfMemory);
                EXPECT_FALSE(lease.IsValid());
                CookLibraryMutationLease mutation = {};
                EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
                EXPECT_TRUE(source.Acquire(capture, &lease, nullptr).Failed());
            }
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        }
    }

    TEST_F(CookFrozenInputSourceTest, WorkingBudgetIsExplicitAndRefusedBeforeInputDiscovery)
    {
        for (const auto bytes : {uint64_t(0), uint64_t(1024)})
        {
            Limits.MaxWorkingBytes            = bytes;
            const auto            allocations = Allocator.Allocations;
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            Diagnostics.Clear();
            EXPECT_EQ(source.Acquire(CaptureContext(), &lease, &Diagnostics).Error(), bytes ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::InvalidPath);
            EXPECT_FALSE(lease.IsValid());
            EXPECT_EQ(Allocator.Allocations, allocations);
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, bytes ? CookDiagnosticCode::BudgetExceeded : CookDiagnosticCode::InvalidBudget);
        }
    }

    TEST_F(CookFrozenInputSourceTest, LargeSidecarUsesOneReusableObservedSizeWorkspaceWithinTheWorkingBudget)
    {
        uint8_t metadata[ZKilo(32)] = {};
        ASSERT_EQ(Helpers::secure_memset(metadata, ' ', sizeof(metadata), sizeof(metadata)), Helpers::MEMORY_OP_SUCCESS);
        const auto prefix = Core::Containers::StringView(InputMetadata);
        ASSERT_EQ(Helpers::secure_memcpy(metadata, sizeof(metadata), prefix.data(), prefix.size()), Helpers::MEMORY_OP_SUCCESS);
        ASSERT_TRUE(Backend.WriteFile(InputPath("/assets/a.png.meta"), metadata).Succeeded());
        Limits.MaxWorkingBytes            = ZKilo(64);
        const auto            allocations = Allocator.Allocations;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        EXPECT_EQ(Allocator.Allocations - allocations, 4u); // one workspace in addition to stream/index/catalog.
        EXPECT_LE(Allocator.Statistics().PeakBytes, Limits.MaxWorkingBytes);
        EXPECT_LT(Allocator.Statistics().LiveBytes, ZKilo(32));
        EXPECT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        EXPECT_EQ(Allocator.Allocations - allocations, 4u);
    }

    TEST_F(CookFrozenInputSourceTest, EnvironmentArtifactMapsDeclaredLogicalAndPersistedContextPathsExactly)
    {
        for (cstring metadata : {R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"envmaps/sky.zenvmap","source_path":"/missing/provenance.hdr"})", R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"/_cache/envmaps/sky.zenvmap","source_path":"/missing/provenance.hdr"})"})
        {
            AddEnvironment(metadata);
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
            CookAssetCatalogEntry entry = {};
            ASSERT_TRUE(lease.FindAsset(InputIdentity(CookAssetKind::EnvironmentMap, "00000000-0000-0000-0000-000000000002"), &entry, nullptr).Succeeded());
            EXPECT_EQ(entry.Artifact, InputRelative("envmaps/sky.zenvmap"));
            EXPECT_EQ(entry.Metadata, InputRelative("assets/sky.hdr.meta"));
            EXPECT_EQ(entry.EnvironmentSource, InputRelative("assets/sky.hdr"));
            EXPECT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        }
    }

    TEST_F(CookFrozenInputSourceTest, InvalidEnvironmentPathsAndMissingProvenanceFieldsNeverNormalizeOrEscape)
    {
        for (cstring metadata : {
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"artifact_path":"envmaps/sky.zenvmap"})",
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"/_cache/envmaps/../envmaps/sky.zenvmap"})",
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"/_cache//envmaps/sky.zenvmap"})",
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"/outside/sky.zenvmap"})",
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"unknown/sky.zenvmap"})",
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"envmaps/missing.zenvmap"})",
             R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"EnvironmentMapImporter","settings":[],"source_hash":0,"artifact_path":"assets/a.png"})"})
        {
            AddEnvironment(metadata);
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            Diagnostics.Clear();
            EXPECT_EQ(source.Acquire(CaptureContext(), &lease, &Diagnostics).Error(), VFS::VFSError::Corrupted);
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidMetadata);
            EXPECT_FALSE(lease.IsValid());
        }
    }

    TEST_F(CookFrozenInputSourceTest, CatalogMetadataChangesAreRecheckedEvenAfterTemporaryCapturesAreReleased)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        Write("/assets/a.png.meta", SecondMetadata);
        EXPECT_EQ(lease.Seal(&Diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InputChanged);
        EXPECT_EQ(Diagnostics.Entries[0].Source, InputRelative("assets/a.png.meta"));
        EXPECT_TRUE(lease.Seal(nullptr).Failed());
        CookLibraryMutationLease mutation = {};
        EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, CatalogMembershipChangesInvalidateSealWithoutAnotherRetainedIndex)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Succeeded());
        const auto allocations = Allocator.Allocations;
        Write("/assets/new.png", "new");
        Write("/assets/new.png.meta", SecondMetadata);
        EXPECT_EQ(lease.Seal(&Diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InputChanged);
        EXPECT_TRUE(Diagnostics.Entries[0].Source.IsValid());
        EXPECT_EQ(Allocator.Allocations, allocations);
    }

    TEST_F(CookFrozenInputSourceTest, FailedCapturePoisonsGenerationAndPreservesExistingOutput)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Succeeded());
        CookCapturedFile captured = {};
        const auto       path     = InputRelative("assets/a.png");
        ASSERT_TRUE(lease.Capture(path, &captured, nullptr).Succeeded());
        const auto digest = captured.Digest();
        Write("/assets/a.png", "changed-image");
        EXPECT_EQ(lease.Capture(path, &captured, &Diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(captured.Digest(), digest);
        Write("/assets/a.png", "encoded-image");
        EXPECT_EQ(lease.Capture(path, &captured, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(lease.Seal(nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        uint8_t bytes[13] = {};
        EXPECT_TRUE(ReadCookCapturedRange(captured, 0, bytes).Succeeded());
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), "encoded-image", 13, 13), 0);
    }

    TEST_F(CookFrozenInputSourceTest, EverySupportedSceneExtensionUsesTheSceneCaptureByteLimit)
    {
        constexpr cstring paths[] = {"/scenes/main.yaml", "/scenes/main.yml", "/scenes/main.scene", "/scenes/main.zescene", "/scenes/main.ZESCENE"};
        for (cstring path : paths)
        {
            Write(path, "saved-scene");
        }
        Limits.SceneLimits.MaxFileBytes = 10;
        ASSERT_GT(Limits.MaxEncodedAssetBytes, 11u);
        for (cstring path : paths)
        {
            CookFrozenInputSource source(Owner);
            CookInputLease        lease = {};
            ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Succeeded());
            VFS::VFSRelativePath logical = {};
            ASSERT_TRUE(lease.MapContextPath(InputPath(path), &logical, nullptr).Succeeded());
            CookCapturedFile captured = {};
            EXPECT_EQ(lease.Capture(logical, &captured, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded) << path;
            EXPECT_FALSE(captured.IsValid());
            ASSERT_GT(Diagnostics.Count, 0u);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::BudgetExceeded);
            EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Source, logical);
            // Provider failures poison this generation. The public lease
            // reaches its terminal state at the explicit Seal boundary.
            EXPECT_EQ(lease.Capture(logical, &captured, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(lease.Seal(nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(lease.State(), CookInputState::Failed);
            Diagnostics.Clear();
        }
    }

    TEST_F(CookFrozenInputSourceTest, UniqueCapturedBytesIncludeReleasedDiscoverySidecarsButNotRepeatedCaptures)
    {
        Limits.MaxCapturedBytes = Core::Containers::StringView(InputMetadata).size() + 13;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Succeeded());
        CookCapturedFile captured = {};
        ASSERT_TRUE(lease.Capture(InputRelative("assets/a.png"), &captured, nullptr).Succeeded());
        ASSERT_TRUE(captured.Reset().Succeeded());
        ASSERT_TRUE(lease.Capture(InputRelative("assets/a.png"), &captured, nullptr).Succeeded());
        EXPECT_EQ(lease.Capture(InputRelative("scenes/main.yaml"), &captured, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(captured.Size(), 13u);
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_EQ(lease.Seal(nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
    }

    TEST_F(CookFrozenInputSourceTest, SealedCapturesOutliveInputLeaseOwnerAndLiveContext)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Succeeded());
        CookCapturedFile captured = {};
        ASSERT_TRUE(lease.Capture(InputRelative("assets/a.png"), &captured, nullptr).Succeeded());
        EXPECT_TRUE(lease.Seal(nullptr).Succeeded());
        EXPECT_EQ(lease.State(), CookInputState::Sealed);
        VFS::VFSRelativePath  mapped = {};
        CookAssetCatalogEntry entry  = {};
        EXPECT_TRUE(lease.MapContextPath(InputPath("/scenes/main.yaml"), &mapped, nullptr).Failed());
        EXPECT_TRUE(lease.FindAsset(InputIdentity(CookAssetKind::Texture), &entry, nullptr).Failed());
        lease.Reset();
        EXPECT_TRUE(Owner.Reset().Succeeded());
        Write("/assets/a.png", "changed-image");
        Context.Shutdown();
        Memory.Shutdown();
        uint8_t bytes[13] = {};
        ASSERT_TRUE(ReadCookCapturedRange(captured, 0, bytes).Succeeded());
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), "encoded-image", 13, 13), 0);
        EXPECT_TRUE(captured.Reset().Succeeded());
        EXPECT_EQ(source.CleanupError(), VFS::VFSError::OK);
    }

    TEST_F(CookFrozenInputSourceTest, ConcurrentReadSessionsExcludeMutationsUntilEverySessionSeals)
    {
        ASSERT_TRUE(Scratch.CreateDirExclusive(InputRelative("second-run")).Succeeded());
        CookFrozenInputSource first(Owner), second(Owner);
        CookInputLease        one = {}, two = {};
        auto                  second_context = CaptureContext();
        second_context.ScratchRun            = InputRelative("second-run");
        ASSERT_TRUE(first.Acquire(CaptureContext(), &one, nullptr).Succeeded());
        ASSERT_TRUE(second.Acquire(second_context, &two, nullptr).Succeeded());
        CookCapturedFile first_capture = {}, second_capture = {};
        ASSERT_TRUE(one.Capture(InputRelative("assets/a.png"), &first_capture, nullptr).Succeeded());
        ASSERT_TRUE(two.Capture(InputRelative("assets/a.png"), &second_capture, nullptr).Succeeded());
        CookLibraryMutationLease mutation = {};
        EXPECT_EQ(Owner.TryAcquireMutation(&mutation).Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(one.Seal(nullptr).Succeeded());
        EXPECT_EQ(Owner.TryAcquireMutation(&mutation).Error(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(two.Seal(nullptr).Succeeded());
        EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        EXPECT_TRUE(first_capture.Reset().Succeeded());
        EXPECT_TRUE(second_capture.Reset().Succeeded());
        EXPECT_TRUE(Scratch.Remove(InputRelative("second-run")).Succeeded());
    }

    TEST_F(CookFrozenInputSourceTest, InvalidContextsBusyOwnerAndReacquisitionNeverReplaceAnExistingLease)
    {
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        EXPECT_TRUE(source.Acquire({}, &lease, nullptr).Failed());
        CookLibraryMutationLease mutation = {};
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        EXPECT_EQ(source.Acquire(CaptureContext(), &lease, &Diagnostics).Error(), VFS::VFSError::AlreadyExists);
        mutation.Reset();
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Succeeded());
        CookFrozenInputSource second(Owner);
        EXPECT_TRUE(second.Acquire(CaptureContext(), &lease, nullptr).Failed());
        EXPECT_EQ(lease.State(), CookInputState::Active);
        EXPECT_TRUE(lease.Seal(nullptr).Succeeded());
        lease.Reset();
        EXPECT_TRUE(source.Acquire(CaptureContext(), &lease, nullptr).Failed());
        EXPECT_FALSE(lease.IsValid());
    }

    TEST_F(CookFrozenInputSourceTest, OverlappingInputScratchIsRejectedBeforeAllocatingOrWritingCaptures)
    {
        VFS::VFSScratchRootLease overlapping = {};
        ASSERT_TRUE(Context.AcquireScratchRoot(InputPath("/assets"), &overlapping).Succeeded());
        auto capture                      = CaptureContext();
        capture.Scratch                   = &overlapping;
        const auto            allocations = Allocator.Allocations;
        CookFrozenInputSource source(Owner);
        CookInputLease        lease = {};
        EXPECT_EQ(source.Acquire(capture, &lease, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(lease.IsValid());
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_EQ(Backend.Stat(InputPath("/assets/run")).Error(), VFS::VFSError::NotFound);
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    TEST(CookFrozenInputSourceNativeTest, SealRehashesReleasedSidecarsAndCapturesSurviveNativeFacadeTeardown)
    {
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("cook-frozen-input", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "assets", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "scratch", error));
        FixedAllocator<ZKilo(128), 4> allocator = {};
        CookFrozenLibraryOwner        owner     = {};
        VFS::VFSScratchRootLease      scratch   = {};
        CookFrozenInputSource         source(owner);
        CookInputLease                lease       = {};
        CookCapturedFile              captured    = {};
        CookDiagnostics               diagnostics = {};
        {
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(2), {});
            VFS::VFSDiskBackend backend = {};
            const auto          native  = directory.Path().generic_u8string();
            backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &memory.MainArena);
            VFS::VFSContext context = {};
            context.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            constexpr cstring original       = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"TextureImporter","settings":[]})";
            constexpr cstring changed        = R"({"uuid":"00000000-0000-0000-0000-000000000002","importer":"TextureImporter","settings":[]})";
            const auto        original_bytes = Core::Containers::StringView(original);
            const auto        changed_bytes  = Core::Containers::StringView(changed);
            ASSERT_EQ(original_bytes.size(), changed_bytes.size());
            ASSERT_TRUE(VFS::WriteFileAtomically(context, InputPath("/assets/a.png.meta"), {reinterpret_cast<const uint8_t*>(original_bytes.data()), original_bytes.size()}).Succeeded());
            constexpr uint8_t payload[] = {3, 1, 4, 1};
            ASSERT_TRUE(VFS::WriteFileAtomically(context, InputPath("/assets/a.png"), payload).Succeeded());
            const CookFrozenRootRequest roots[] = {
                {.Key = "assets", .Context = &context, .Path = InputPath("/assets")}
            };
            ASSERT_TRUE(owner.Initialize(allocator, roots).Succeeded());
            ASSERT_TRUE(context.AcquireScratchRoot(InputPath("/scratch"), &scratch).Succeeded());
            ASSERT_TRUE(source.AcquireRoots(&lease, &diagnostics).Succeeded());
            ASSERT_TRUE(lease.RequireDisjoint(scratch, &diagnostics).Succeeded());
            EXPECT_FALSE(std::filesystem::exists(directory.Path() / "scratch/run"));
            ASSERT_TRUE(scratch.CreateDirExclusive(InputRelative("run")).Succeeded());
            const CookLimits         limits          = {.MaxWorkingBytes = ZKilo(128)};
            const CookCaptureContext capture_context = {.Scratch = &scratch, .ScratchRun = InputRelative("run"), .Allocator = &allocator, .Limits = &limits};
            ASSERT_TRUE(lease.PrepareCapture(capture_context, &diagnostics).Succeeded());
            ASSERT_TRUE(lease.Capture(InputRelative("assets/a.png"), &captured, &diagnostics).Succeeded());
            const auto native_sidecar = directory.Path() / "assets/a.png.meta";
            const auto timestamp      = std::filesystem::last_write_time(native_sidecar, error);
            ASSERT_FALSE(error);
            const auto file = context.Open(InputPath("/assets/a.png.meta"), VFS::VFSOpenFlags::Write);
            ASSERT_TRUE(file.Succeeded());
            const auto written = file.Value()->Write({reinterpret_cast<const uint8_t*>(changed_bytes.data()), changed_bytes.size()}, 0);
            const auto closed  = context.CloseChecked(file.Value());
            ASSERT_TRUE(written.Succeeded());
            ASSERT_EQ(written.Value(), changed_bytes.size());
            ASSERT_TRUE(closed.Succeeded());
            std::filesystem::last_write_time(native_sidecar, timestamp, error);
            ASSERT_FALSE(error);
            EXPECT_EQ(lease.Seal(&diagnostics).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(lease.State(), CookInputState::Failed);
            ASSERT_GT(diagnostics.Count, 0u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::InputChanged);
            EXPECT_EQ(diagnostics.Entries[0].Source, InputRelative("assets/a.png.meta"));
            lease.Reset();
            EXPECT_TRUE(owner.Reset().Succeeded());
            context.Shutdown();
            memory.Shutdown();
        }
        uint8_t bytes[4] = {};
        ASSERT_TRUE(ReadCookCapturedRange(captured, 0, bytes).Succeeded());
        EXPECT_EQ(bytes[0], 3u);
        EXPECT_EQ(bytes[2], 4u);
        EXPECT_TRUE(captured.Reset().Succeeded());
        EXPECT_EQ(source.CleanupError(), VFS::VFSError::OK);
        EXPECT_TRUE(scratch.Remove(InputRelative("run")).Succeeded());
    }
#endif
} // namespace ZEngine::Cook::Tests
