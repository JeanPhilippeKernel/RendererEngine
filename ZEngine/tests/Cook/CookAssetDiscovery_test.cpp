#include <ZEngine/Cook/CookAssetDiscovery.h>
#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        CookAssetIdentity DiscoveryIdentity(uint32_t value, CookAssetKind kind)
        {
            char text[37] = {};
            std::snprintf(text, sizeof(text), "00000000-0000-0000-0000-%012x", value);
            return {kind, uuids::uuid::from_string(text).value()};
        }

        VFS::VFSRelativePath Relative(cstring text)
        {
            return VFS::VFSRelativePath::Parse(text).Value();
        }

        VFS::VFSPath Absolute(cstring text)
        {
            return VFS::VFSPath::Parse(text).Value();
        }

        struct DiscoveryAllocator final : ICookAllocator
        {
            ICookAllocator*       Parent  = nullptr;
            uint32_t              Calls   = 0;
            uint32_t              FailAt  = UINT32_MAX;
            VFS::VFSError         Failure = VFS::VFSError::OutOfMemory;

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return ++Calls == FailAt ? VFS::VFSResult<void*>::Fail(Failure) : Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                return Parent->TryCharge(bytes, category, output);
            }
            VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                return Parent->MemoryRequirements();
            }
        };

        struct DiscoverySink final : ICookByteSink
        {
            std::string          Bytes = {};
            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                Bytes.append(reinterpret_cast<cstring>(bytes.data()), bytes.size());
                return VFS::VFSResult<void>::Ok();
            }
            static VFS::VFSResult<void> Emit(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                return static_cast<DiscoverySink*>(context)->Write(bytes);
            }
        };

        struct DiscoveryCleanup
        {
            CookAssetDiscovery* Owner = nullptr;
            ~DiscoveryCleanup()
            {
                if (Owner)
                    Owner->Reset();
            }
        };
    } // namespace

    class CookAssetDiscoveryTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager        Memory      = {};
        VFS::VFSMemoryBackend              Backend     = {};
        VFS::VFSContext                    Context     = {};
        Tests::FixedAllocator<ZMega(4), 1> Parent      = {};
        CookScratchAllocator               Backing     = {};
        CookBudgetAllocator                Run         = {};
        DiscoveryAllocator                 Allocator   = {};
        CookFrozenLibraryOwner             Owner       = {};
        VFS::VFSScratchRootLease           Scratch     = {};
        CookDependencyGraph                Graph       = {};
        CookAssetDiscovery                 Discovery   = {};
        CookAdapterRegistry                Adapters    = {};
        CookLimits                         Limits      = {.MaxWorkingBytes = ZMega(4)};
        CookDiagnostics                    Diagnostics = {};

        void                               SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(8), 512).Succeeded());
            for (cstring path : {"/assets", "/scenes", "/scratch"})
                ASSERT_TRUE(Backend.CreateDir(Absolute(path)).Succeeded());
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(Backing.Initialize(Parent, ZMega(4), CookMemoryCategory::CatalogGraph).Succeeded());
            const auto backing = Backing.MemoryRequirements();
            ASSERT_TRUE(backing.Succeeded());
            // Principal codec stack state is external to the retained 4 MiB
            // slab, not another child allocation inside it.
            Limits.MaxWorkingBytes = backing.Value().StateBytes + backing.Value().PoolBytes + sizeof(Run) + ZKilo(64);
            ASSERT_TRUE(Run.Initialize(Backing, Limits.MaxWorkingBytes).Succeeded());
            Allocator.Parent                    = &Run;
            const CookFrozenRootRequest roots[] = {
                {.Key = "scenes", .Context = &Context, .Path = Absolute("/scenes")},
                {.Key = "assets", .Context = &Context, .Path = Absolute("/assets")},
            };
            ASSERT_TRUE(Owner.Initialize(Run, roots).Succeeded());
            ASSERT_TRUE(Context.AcquireScratchRoot(Absolute("/scratch"), &Scratch).Succeeded());
            ASSERT_TRUE(Scratch.CreateDirExclusive(Relative("run")).Succeeded());
            ASSERT_TRUE(Graph.Initialize(Run, Limits).Succeeded());
            ASSERT_TRUE(Discovery.Initialize(Allocator, Limits).Succeeded());
            const CookAdapterFns adapters[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(Adapters.Initialize(adapters).Succeeded());
        }
        void TearDown() override
        {
            Discovery.Reset();
            Graph.Reset();
            EXPECT_TRUE(Scratch.Remove(Relative("run")).Succeeded());
            Scratch.Reset();
            EXPECT_TRUE(Owner.Reset().Succeeded());
            EXPECT_EQ(Run.OutstandingAllocations(), 0u);
            Run.Release();
            EXPECT_EQ(Backing.OutstandingAllocations(), 0u);
            Backing.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
            Context.Shutdown();
        }
        void Write(cstring path, Core::Containers::StringView bytes)
        {
            ASSERT_TRUE(Backend.WriteFile(Absolute(path), {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}).Succeeded());
        }
        void Texture(uint32_t id = 2)
        {
            char path[64] = {}, metadata[192] = {};
            std::snprintf(path, sizeof(path), "/assets/%u.tga", id);
            // One uncompressed 24-bit BGR pixel; no image-writing dependency.
            constexpr uint8_t bytes[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 17, 33, 255};
            ASSERT_TRUE(Backend.WriteFile(Absolute(path), bytes).Succeeded());
            std::snprintf(path, sizeof(path), "/assets/%u.tga.meta", id);
            std::snprintf(metadata, sizeof(metadata), R"({"uuid":"00000000-0000-0000-0000-%012x","importer":"TextureImporter","settings":[]})", id);
            Write(path, metadata);
        }
        void Material(cstring texture_path = "assets/2.tga")
        {
            Codec::MaterialAssetDocument document                       = {};
            document.UUID                                               = DiscoveryIdentity(1, CookAssetKind::Material).UUID;
            document.Name[0]                                            = 'M';
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
            for (size_t i = 0; i < 2; ++i)
            {
                document.Textures[i].UUID = DiscoveryIdentity(2, CookAssetKind::Texture).UUID;
                Helpers::secure_strcpy(document.Textures[i].Path, sizeof(document.Textures[i].Path), texture_path);
                paths[i] = Relative(texture_path);
            }
            DiscoverySink sink = {};
            ASSERT_TRUE(Codec::WriteCanonicalMaterialAsset(document, paths, DiscoverySink::Emit, &sink).Succeeded());
            Write("/assets/material.zematerial", {sink.Bytes.data(), sink.Bytes.size()});
            Write("/assets/material.zematerial.meta", R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"MaterialImporter","settings":[]})");
        }
        uint32_t Scene(uint32_t id = 100)
        {
            char path[64] = {};
            std::snprintf(path, sizeof(path), "scenes/%u.yaml", id);
            const auto result = Graph.AddScene(DiscoveryIdentity(id, CookAssetKind::Mesh).UUID, Relative(path));
            EXPECT_TRUE(result.Succeeded());
            return result.Succeeded() ? result.Value() : UINT32_MAX;
        }
        void Root(uint32_t scene, uint32_t id = 1, CookAssetKind kind = CookAssetKind::Material)
        {
            ASSERT_TRUE(Graph.AddRoot(scene, DiscoveryIdentity(id, kind), DiscoveryIdentity(200, CookAssetKind::Mesh).UUID, "MaterialComponent", "material_uuid").Succeeded());
        }
        CookCaptureContext CaptureContext()
        {
            return {.Scratch = &Scratch, .ScratchRun = Relative("run"), .Allocator = &Run, .Limits = &Limits};
        }
    };

    TEST_F(CookAssetDiscoveryTest, SharedReachableAssetsAreInspectedOnceAndRemainEncodableAfterSealAndSourceTeardown)
    {
        Texture();
        Material();
        Write("/assets/unused.png", "invalid-but-unreachable");
        Write("/assets/unused.png.meta", R"({"uuid":"00000000-0000-0000-0000-000000000099","importer":"TextureImporter","settings":[]})");
        Root(Scene());
        Root(Scene(101));
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        ASSERT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Succeeded());
        EXPECT_EQ(lease.State(), CookInputState::Active); // Orchestrator must seal.
        EXPECT_TRUE(Graph.IsFinalized());
        EXPECT_EQ(Graph.NodeCount(), 2u);
        EXPECT_EQ(Graph.EdgeCount(), 2u);
        EXPECT_EQ(Discovery.Count(), 2u);
        EXPECT_EQ(Allocator.Calls, 1u); // No per-asset capture owner allocation.
        const auto material_index = Graph.Find(DiscoveryIdentity(1, CookAssetKind::Material));
        const auto texture_index  = Graph.Find(DiscoveryIdentity(2, CookAssetKind::Texture));
        EXPECT_EQ(Graph.LeafFirstNodes()[0], texture_index);
        EXPECT_EQ(Graph.LeafFirstNodes()[1], material_index);
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        lease.Reset();
        Write("/assets/2.tga", "replacement");
        Write("/assets/material.zematerial", "replacement");
        Context.Shutdown();

        VFS::StrictMetaFileData metadata = {};
        CookAssetInput          input    = {};
        ASSERT_TRUE(Discovery.ReadInput(texture_index, Run, &metadata, &input).Succeeded());
        const auto*   stable_artifact = input.Artifact;
        DiscoverySink texture         = {};
        const auto*   texture_adapter = Adapters.Find(CookAssetKind::Texture);
        ASSERT_TRUE(texture_adapter->Encode(nullptr, input, {}, CookTarget::DesktopVulkanLE64, texture, Run, &Diagnostics).Succeeded());
        EXPECT_EQ(texture.Bytes.size(), 21u);
        ASSERT_TRUE(Discovery.ReadInput(material_index, Run, &metadata, &input).Succeeded());
        const auto             edges       = Graph.Dependencies(material_index);
        CookResolvedDependency resolved[2] = {};
        for (size_t i = 0; i < 2; ++i)
            resolved[i] = {.Slot = Graph.Slot(edges[i]), .Identity = Graph.Node(texture_index)->Identity, .Format = "image-tga", .ObjectPath = Relative("objects/0000000000000000000000000000000000000000000000000000000000000000.tga"), .ObjectSize = 21};
        DiscoverySink material         = {};
        const auto*   material_adapter = Adapters.Find(CookAssetKind::Material);
        ASSERT_TRUE(material_adapter->Encode(nullptr, input, resolved, CookTarget::DesktopVulkanLE64, material, Run, &Diagnostics).Succeeded());
        EXPECT_NE(material.Bytes.find("objects/0000000000000000000000000000000000000000000000000000000000000000.tga"), std::string::npos);
        ASSERT_TRUE(Discovery.ReadInput(texture_index, Run, &metadata, &input).Succeeded());
        EXPECT_EQ(input.Artifact, stable_artifact);
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
        Discovery.Reset(); // Captures must die before the provider/store.
    }

    TEST_F(CookAssetDiscoveryTest, LazyCaptureBlocksDoNotRelocateOwnersOrAllocatePerIdentity)
    {
        const auto scene = Scene();
        for (uint32_t id = 1; id <= 65; ++id)
        {
            Texture(id);
            Root(scene, id, CookAssetKind::Texture);
        }
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        ASSERT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Succeeded());
        EXPECT_EQ(Discovery.Count(), 65u);
        EXPECT_EQ(Allocator.Calls, 3u);
        VFS::StrictMetaFileData metadata = {};
        CookAssetInput          first = {}, last = {};
        ASSERT_TRUE(Discovery.ReadInput(0, Run, &metadata, &first).Succeeded());
        const auto* original = first.Artifact;
        ASSERT_TRUE(Discovery.ReadInput(64, Run, &metadata, &last).Succeeded());
        ASSERT_TRUE(Discovery.ReadInput(0, Run, &metadata, &first).Succeeded());
        EXPECT_EQ(first.Artifact, original);
        EXPECT_NE(last.Artifact, original);
        EXPECT_EQ(Parent.Allocations, 1u);
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        Discovery.Reset();
    }

    TEST_F(CookAssetDiscoveryTest, UUIDPathMismatchIsRejectedBeforeAnyDependencyEdgesAreCommitted)
    {
        Texture();
        Material("assets/wrong.tga");
        Root(Scene());
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        EXPECT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Failed());
        EXPECT_FALSE(Graph.IsFinalized());
        EXPECT_EQ(Graph.NodeCount(), 1u);
        EXPECT_EQ(Graph.EdgeCount(), 0u);
        EXPECT_EQ(Discovery.Count(), 0u);
        ASSERT_GT(Diagnostics.Count, 0u);
        const auto& entry = Diagnostics.Entries[Diagnostics.Count - 1];
        EXPECT_EQ(entry.SceneUUID, DiscoveryIdentity(100, CookAssetKind::Mesh).UUID);
        EXPECT_EQ(entry.EntityUUID, DiscoveryIdentity(200, CookAssetKind::Mesh).UUID);
        EXPECT_STREQ(entry.SchemaKey, "MaterialComponent");
        EXPECT_EQ(entry.ChainCount, 2u);
        EXPECT_EQ(entry.Chain[0], DiscoveryIdentity(1, CookAssetKind::Material));
        EXPECT_EQ(entry.Chain[1], DiscoveryIdentity(2, CookAssetKind::Texture));
        EXPECT_EQ(entry.Asset, DiscoveryIdentity(2, CookAssetKind::Texture));
        EXPECT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, nullptr).Failed());
        Discovery.Reset();
    }

    TEST_F(CookAssetDiscoveryTest, MissingRootIsReportedWithSceneEntityAndFieldProvenance)
    {
        Root(Scene());
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        EXPECT_EQ(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Error(), VFS::VFSError::NotFound);
        const auto& entry = Diagnostics.Entries[Diagnostics.Count - 1];
        EXPECT_EQ(entry.Code, CookDiagnosticCode::MissingDependency);
        EXPECT_STREQ(entry.FieldKey, "material_uuid");
        EXPECT_EQ(entry.Asset, DiscoveryIdentity(1, CookAssetKind::Material));
        Discovery.Reset();
    }

    TEST_F(CookAssetDiscoveryTest, EmptyScenesRequireNoCaptureBlockAndReadInputPreservesBothOutputs)
    {
        Scene();
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        VFS::StrictMetaFileData metadata = {};
        metadata.Data.SourceHash         = 123;
        CookAssetInput input             = {.Identity = DiscoveryIdentity(7, CookAssetKind::Texture)};
        EXPECT_TRUE(Discovery.ReadInput(0, Run, &metadata, &input).Failed());
        ASSERT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Succeeded());
        EXPECT_EQ(Allocator.Calls, 0u);
        EXPECT_EQ(Discovery.Count(), 0u);
        EXPECT_TRUE(Discovery.ReadInput(0, Run, &metadata, &input).Failed());
        EXPECT_EQ(metadata.Data.SourceHash, 123u);
        EXPECT_EQ(input.Identity, DiscoveryIdentity(7, CookAssetKind::Texture));
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
    }

    TEST_F(CookAssetDiscoveryTest, CaptureBlockBudgetRefusalIsTerminalAndAllCapturedFilesAreReleasedOnReset)
    {
        const auto scene = Scene();
        for (uint32_t id = 1; id <= 33; ++id)
        {
            Texture(id);
            Root(scene, id, CookAssetKind::Texture);
        }
        Allocator.FailAt  = 2;
        Allocator.Failure = VFS::VFSError::SizeLimitExceeded;
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        const auto initial = Run.OutstandingAllocations();
        EXPECT_EQ(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_GT(Run.OutstandingAllocations(), initial);
        EXPECT_EQ(Discovery.Count(), 0u);
        EXPECT_FALSE(Graph.IsFinalized());
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::BudgetExceeded);
        Discovery.Reset();
        // Graph never grew here (all identities were authored roots), and disk
        // captures own no heap; resetting discovery releases exactly its block.
        EXPECT_EQ(Run.OutstandingAllocations(), initial);
        EXPECT_TRUE(lease.Seal(&Diagnostics).Succeeded());
    }

    TEST_F(CookAssetDiscoveryTest, ReadInputAllocationRefusalPreservesBothOutputsAfterSealing)
    {
        Texture();
        Root(Scene(), 2, CookAssetKind::Texture);
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        ASSERT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Succeeded());
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        DiscoveryAllocator failing       = {};
        failing.Parent                   = &Run;
        failing.FailAt                   = 1;
        VFS::StrictMetaFileData metadata = {};
        metadata.Data.SourceHash         = 7;
        CookAssetInput input             = {.Identity = DiscoveryIdentity(9, CookAssetKind::Mesh)};
        EXPECT_EQ(Discovery.ReadInput(0, failing, &metadata, &input).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(metadata.Data.SourceHash, 7u);
        EXPECT_EQ(input.Identity, DiscoveryIdentity(9, CookAssetKind::Mesh));
        EXPECT_TRUE(Discovery.ReadInput(0, Run, &metadata, &input).Succeeded());
        Discovery.Reset();
    }

    TEST_F(CookAssetDiscoveryTest, EveryScratchAllocationFailureAbortsDiscoveryWithoutLeakingInspectionOrWorkspace)
    {
        Texture();
        Material();
        uint32_t attempts = 0;
        for (uint32_t failure = 0; failure <= attempts; ++failure)
        {
            SCOPED_TRACE(failure);
            Discovery.Reset();
            Graph.Reset();
            Diagnostics.Clear();
            ASSERT_TRUE(Graph.Initialize(Run, Limits).Succeeded());
            ASSERT_TRUE(Discovery.Initialize(Allocator, Limits).Succeeded());
            Root(Scene());
            CookFrozenInputSource source(Owner);
            DiscoveryCleanup      cleanup{&Discovery};
            CookInputLease        lease = {};
            ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
            DiscoveryAllocator scratch = {};
            scratch.Parent             = &Run;
            scratch.FailAt             = failure ? failure : UINT32_MAX;
            const auto initial         = Run.OutstandingAllocations();
            const auto result          = Discovery.Discover(Graph, lease, Adapters, scratch, &Diagnostics);
            if (!failure)
            {
                ASSERT_TRUE(result.Succeeded());
                attempts = scratch.Calls;
                ASSERT_GT(attempts, 3u);
            }
            else
            {
                ASSERT_TRUE(result.Failed());
                EXPECT_EQ(result.Error(), VFS::VFSError::OutOfMemory);
                EXPECT_EQ(Discovery.Count(), 0u);
                EXPECT_FALSE(Graph.IsFinalized());
                EXPECT_TRUE(Diagnostics.HasErrors());
            }
            Discovery.Reset();
            EXPECT_EQ(Run.OutstandingAllocations(), initial);
            ASSERT_TRUE(lease.Seal(nullptr).Succeeded());
        }
    }

    TEST_F(CookAssetDiscoveryTest, ResetRetainsCleanupErrorsWithoutRetryingAgainstLiveSourceOrLosingOwners)
    {
        Texture();
        Root(Scene(), 2, CookAssetKind::Texture);
        CookFrozenInputSource source(Owner);
        DiscoveryCleanup      cleanup{&Discovery};
        CookInputLease        lease = {};
        ASSERT_TRUE(source.Acquire(CaptureContext(), &lease, &Diagnostics).Succeeded());
        ASSERT_TRUE(Discovery.Discover(Graph, lease, Adapters, Run, &Diagnostics).Succeeded());
        ASSERT_TRUE(lease.Seal(&Diagnostics).Succeeded());
        const auto listed = Backend.List(&Memory.MainArena, Absolute("/scratch/run"));
        ASSERT_TRUE(listed.Succeeded());
        ASSERT_EQ(listed.Value().size(), 2u);
        const auto path   = listed.Value()[0].Path;
        // Simulate an external file user denying removal. No source-generation
        // mutation, fake input session, or scratch-pin lifetime violation.
        const auto writer = Backend.Open(path, VFS::VFSOpenFlags::Write);
        ASSERT_TRUE(writer.Succeeded());
        Discovery.Reset();
        EXPECT_EQ(Discovery.Count(), 0u);
        EXPECT_EQ(Discovery.CleanupError(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(source.CleanupError(), VFS::VFSError::AlreadyExists);
        Backend.Close(writer.Value());
        Discovery.Reset();
        EXPECT_TRUE(Backend.Exists(path)); // No hidden retry on a later Reset.
        EXPECT_EQ(Discovery.CleanupError(), VFS::VFSError::AlreadyExists);
        ASSERT_TRUE(Backend.Remove(path).Succeeded());
        ASSERT_TRUE(Discovery.Initialize(Allocator, Limits).Succeeded());
        EXPECT_EQ(Discovery.CleanupError(), VFS::VFSError::OK);
    }
} // namespace ZEngine::Cook
