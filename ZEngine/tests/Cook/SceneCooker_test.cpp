#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookCallerMemory.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookPublication.h>
#include <ZEngine/Cook/CookRegistryMemory.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/CookVFSServiceMemory.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <memory>
#include <string>
#include "../TemporaryDirectory.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        constexpr uint8_t CookPixel[] = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00, 0x03, 0x03, 0x02, 0x00, 0xef, 0x9a, 0xde, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
        };

        VFS::VFSPath Path(cstring text)
        {
            return VFS::VFSPath::Parse(text).Value();
        }

        VFS::VFSRelativePath Relative(cstring text)
        {
            return VFS::VFSRelativePath::Parse(text).Value();
        }

        Core::Containers::ArrayView<const uint8_t> Bytes(const std::string& text)
        {
            return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        }

        struct RunAllocator final : ICookAllocator
        {
            ICookAllocator*      Parent = nullptr;
            size_t               Calls  = 0;
            size_t               FailAt = SIZE_MAX;

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
            VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override
            {
                return Parent->AllocationFootprint(bytes, alignment, category);
            }
            VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                const auto parent = Parent->MemoryRequirements();
                if (parent.Failed())
                    return parent;
                if (parent.Value().StateBytes > UINT64_MAX - sizeof(*this))
                    return VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(VFS::VFSError::SizeLimitExceeded);
                return VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({parent.Value().StateBytes + sizeof(*this), parent.Value().PoolBytes, parent.Value().IncludesAllocations});
            }
            const ICookAllocator* ParentAllocator() const noexcept override
            {
                return Parent;
            }
        };

        struct CookSink
        {
            std::string                 Data = {};
            static VFS::VFSResult<void> Write(void* state, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                static_cast<CookSink*>(state)->Data.append(reinterpret_cast<cstring>(bytes.data()), bytes.size());
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct PreparationInputProbe final : ICookInputSource
        {
            mutable uint32_t              Queries            = 0;
            uint32_t                      Acquisitions       = 0;
            mutable const ICookAllocator* Working            = nullptr;
            mutable uint64_t              QueryDiagnostics   = 0;
            uint64_t                      AcquireDiagnostics = 0;

            VFS::VFSResult<uint64_t>      FixedWorkingBytes(const ICookAllocator& allocator) const noexcept override
            {
                ++Queries;
                Working          = &allocator;
                QueryDiagnostics = allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::DiagnosticsResult)];
                return VFS::VFSResult<uint64_t>::Ok(sizeof(*this));
            }
            VFS::VFSResult<void> AcquireRoots(CookInputLease*, CookDiagnostics*) override
            {
                ++Acquisitions;
                AcquireDiagnostics = Working->Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::DiagnosticsResult)];
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                return AcquireRoots(lease, diagnostics);
            }
        };

        struct AdapterProbe final : ICookCallbackStorage
        {
            explicit AdapterProbe(CookAdapterFns original) noexcept : Original(original) {}
            CookAdapterFns   Original    = {};
            mutable uint32_t Inspections = 0;
            mutable uint32_t Encodes     = 0;
            VFS::VFSError    EncodeError = VFS::VFSError::OK;

            bool             Contains(const void* context) const noexcept override
            {
                return context == this;
            }
            VFS::VFSResult<CookServiceMemoryRequirements> MemoryRequirements(const SceneCookContext&, const ICookAllocator&) const noexcept override
            {
                // This test probe has no retained graph beyond its inline state;
                // the wrapped built-in's context must remain null.
                return Original.Context ? VFS::VFSResult<CookServiceMemoryRequirements>::Fail(VFS::VFSError::Unsupported) : VFS::VFSResult<CookServiceMemoryRequirements>::Ok({.StateBytes = sizeof(*this)});
            }

            CookAdapterFns Functions()
            {
                return {
                    Original.Kind,
                    Original.Revision,
                    this,
                    [](const void* state, const CookAssetInput& input, ICookAllocator& allocator, CookAssetInspection* output, CookDiagnostics* diagnostics) {
                        const auto& probe = *static_cast<const AdapterProbe*>(state);
                        ++probe.Inspections;
                        return probe.Original.Inspect(probe.Original.Context, input, allocator, output, diagnostics);
                    },
                    [](const void* state, const CookAssetInput& input, Core::Containers::ArrayView<const CookResolvedDependency> dependencies, CookTarget target, ICookByteSink& sink, ICookAllocator& allocator, CookDiagnostics* diagnostics) {
                        const auto& probe = *static_cast<const AdapterProbe*>(state);
                        ++probe.Encodes;
                        return probe.EncodeError == VFS::VFSError::OK ? probe.Original.Encode(probe.Original.Context, input, dependencies, target, sink, allocator, diagnostics) : VFS::VFSResult<void>::Fail(probe.EncodeError);
                    }};
            }
        };
    } // namespace

    class SceneCookerTest : public testing::TestWithParam<bool>
    {
    protected:
        Core::Memory::MemoryManager        Memory              = {};
        ZEngine::Tests::TemporaryDirectory Directory           = {};
        VFS::VFSDiskBackend                Disk                = {};
        VFS::VFSMemoryBackend              InMemory            = {};
        size_t                             BackendStorageBytes = ZMega(8);
        uint32_t                           BackendMaxNodes     = 512;
        VFS::IVFSBackend*                  Backend             = nullptr;
        VFS::VFSContext                    Files               = {};
        FixedAllocator<ZMega(4), 1>        Parent              = {};
        CookScratchAllocator               Backing             = {};
        RunAllocator                       Allocator           = {};
        CookFrozenLibraryOwner             Owner               = {};
        CookAdapterRegistry                Adapters            = {};
        AdapterProbe                       MaterialAdapter{CookMaterialAdapter()};
        ECS::SceneComponentSchemaRegistry  Schemas      = {};
        ECS::Scene                         Source       = {};
        ECS::EntityID                      Entity       = ECS::INVALID_ENTITY;
        uuids::uuid                        SceneUUID    = uuids::uuid::from_string("10000000-0000-0000-0000-000000000001").value();
        uuids::uuid                        MaterialUUID = uuids::uuid::from_string("20000000-0000-0000-0000-000000000002").value();
        uuids::uuid                        TextureUUID  = uuids::uuid::from_string("30000000-0000-0000-0000-000000000003").value();
        VFS::VFSPath                       Sources[1]   = {Path("/scenes/main.scene")};
        SceneCookRequest                   Request      = {};
        SceneCookContext                   Context      = {};
        SceneCookResult                    Result       = {};
        CookDiagnostics                    Diagnostics  = {};

        void                               SetUp() override
        {
            Memory.Initialize(ZMega(128), {});
            if (GetParam())
            {
                std::error_code error = {};
                ASSERT_TRUE(Directory.Create("zengine_scene_cooker", error));
                const auto native = Directory.Path().generic_u8string();
                Disk.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write | VFS::VFSBackendCaps::List, &Memory.MainArena);
                Backend = &Disk;
            }
            else
            {
                ASSERT_TRUE(InMemory.InitializeOwned(BackendStorageBytes, BackendMaxNodes).Succeeded());
                Backend = &InMemory;
            }
            Files.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Files.Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
            for (cstring path : {"/scenes", "/assets", "/scratch", "/output"})
                ASSERT_TRUE(Files.CreateDir(Path(path)).Succeeded());
            // The slab occupies one retained block. Fixed parser working state
            // lives outside that slab and needs independent ledger headroom.
            ASSERT_TRUE(Parent.Ledger.Initialize(ZMega(8)).Succeeded());
            ASSERT_TRUE(Backing.Initialize(Parent, ZMega(4), CookMemoryCategory::Codecs).Succeeded());
            Allocator.Parent = &Backing;
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas));
            Source.Initialize(&Memory.MainArena);
            Entity = Source.CreateEntity();
            Source.AddComponent<ECS::Components::UUIDComponent>(Entity, {uuids::uuid::from_string("40000000-0000-0000-0000-000000000004").value()});
            Source.AddComponent<ECS::Components::NameComponent>(Entity, {});
            Source.AddComponent<ECS::Components::MaterialComponent>(Entity, {MaterialUUID});
            SaveScene();
            SaveMaterial();
            const CookFrozenRootRequest roots[] = {
                {"scenes", &Files, Path("/scenes")},
                {"assets", &Files, Path("/assets")},
            };
            ASSERT_TRUE(Owner.Initialize(Allocator, roots).Succeeded());
            const CookAdapterFns functions[] = {CookEnvironmentAdapter(), MaterialAdapter.Functions(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(Adapters.Initialize(functions).Succeeded());
            Request = {.Sources = Sources, .OutputRoot = Path("/output"), .Target = CookTarget::DesktopVulkanLE64};
            Context = {
                .Output = &Files, .Scratch = &Files, .ScratchRoot = Path("/scratch"), .Schemas = &Schemas, .Adapters = &Adapters, .Allocator = &Allocator, .Limits = {.MaxWorkingBytes = ZMega(32), .SceneLimits = {.MaxDecodedBytes = ZKilo(128)}},
                                    .Callbacks = &MaterialAdapter
            };
        }

        void TearDown() override
        {
            EXPECT_TRUE(Owner.Reset().Succeeded());
            EXPECT_EQ(Backing.OutstandingAllocations(), 0u);
            Backing.Release();
            EXPECT_EQ(Parent.Statistics().LiveBytes, 0u);
            Source.Shutdown();
            Files.Shutdown();
        }

        void Write(const VFS::VFSPath& path, Core::Containers::ArrayView<const uint8_t> bytes)
        {
            const auto file = Files.Open(path, VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Truncate);
            ASSERT_TRUE(file.Succeeded());
            EXPECT_EQ(file.Value()->Write(bytes, 0).Value(), bytes.size());
            EXPECT_TRUE(Files.CloseChecked(file.Value()).Succeeded());
        }

        void Write(cstring path, const std::string& text)
        {
            Write(Path(path), Bytes(text));
        }

        std::string Read(const VFS::VFSPath& path)
        {
            const auto file = Files.Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
            EXPECT_TRUE(file.Succeeded());
            if (file.Failed())
                return {};
            const auto size = file.Value()->Size();
            EXPECT_TRUE(size.Succeeded());
            std::string bytes(static_cast<size_t>(size.Value()), '\0');
            EXPECT_EQ(file.Value()->ReadAll({reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()}).Value(), bytes.size());
            EXPECT_TRUE(Files.CloseChecked(file.Value()).Succeeded());
            return bytes;
        }

        void SaveScene(cstring path = "/scenes/main.scene")
        {
            ECS::SceneDocument document = {};
            ASSERT_TRUE(ECS::SceneDocument::Capture(Source, {.UUID = SceneUUID, .Name = "Cook scene"}, Schemas, &document, nullptr));
            std::string yaml = {};
            ASSERT_TRUE(document.Emit(&yaml, nullptr));
            Write(path, yaml);
        }

        void SaveMaterial(bool texture = false)
        {
            Importers::AssetCodec::MaterialAssetDocument material       = {.UUID = MaterialUUID};
            material.Name[0]                                            = 'M';
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
            if (texture)
            {
                material.Textures[0].UUID = TextureUUID;
                ASSERT_EQ(Helpers::secure_strcpy(material.Textures[0].Path, sizeof(material.Textures[0].Path), "assets/pixel.png"), Helpers::MEMORY_OP_SUCCESS);
                paths[0] = Relative("assets/pixel.png");
            }
            CookSink sink = {};
            ASSERT_TRUE(Importers::AssetCodec::WriteCanonicalMaterialAsset(material, paths, CookSink::Write, &sink).Succeeded());
            Write("/assets/material.zematerial", sink.Data);
            Write("/assets/material.zematerial.meta", R"({"uuid":"20000000-0000-0000-0000-000000000002","importer":"MaterialImporter","settings":[]})");
        }

        void AddTexture()
        {
            Write(Path("/assets/pixel.png"), CookPixel);
            Write("/assets/pixel.png.meta", R"({"uuid":"30000000-0000-0000-0000-000000000003","importer":"TextureImporter","settings":[]})");
            SaveMaterial(true);
        }

        VFS::VFSResult<void> Cook()
        {
            CookFrozenInputSource input(Owner);
            auto                  context = Context;
            context.Inputs                = &input;
            const auto live               = Backing.OutstandingAllocations();
            const auto result             = SceneCooker::Cook(Request, context, &Result, &Diagnostics);
            EXPECT_EQ(Backing.OutstandingAllocations(), live);
            return result;
        }

        void ExpectSetupRefusal(SceneCookContext context, VFS::VFSError error, CookDiagnosticCode code)
        {
            CookFrozenInputSource input(Owner);
            context.Inputs     = &input;
            Result.ObjectCount = 77;
            const auto calls   = Allocator.Calls;
            EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), error);
            EXPECT_EQ(Result.ObjectCount, 77u);
            EXPECT_EQ(Allocator.Calls, calls);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
            EXPECT_EQ(Diagnostics.Entries[0].Code, code);
            EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
            ExpectScratchEmpty();
            CookLibraryMutationLease mutation = {};
            EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        }

        void ExpectScratchEmpty()
        {
            VFS::VFSReadRootLease scratch = {};
            ASSERT_TRUE(Files.AcquireReadRoot(Path("/scratch"), &scratch).Succeeded());
            uint8_t buffer[ZKilo(4)] = {};
            size_t  count            = 0;
            ASSERT_TRUE(scratch
                            .VisitRoot(
                                64,
                                buffer,
                                [](void* state, const VFS::VFSRootEntry&) {
                                    ++*static_cast<size_t*>(state);
                                    return VFS::VFSResult<void>::Ok();
                                },
                                &count)
                            .Succeeded());
            EXPECT_EQ(count, 0u);
        }
    };

    TEST_P(SceneCookerTest, PublishesACompletePackageThenIndependentlyCapturedVerifiedNoOp)
    {
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Result.SceneCount, 1u);
        EXPECT_EQ(Result.AssetCount, 1u);
        EXPECT_EQ(Result.ObjectCount, 2u);
        EXPECT_GT(Result.PeakWorkingBytes, 0u);
        EXPECT_LE(Result.PeakWorkingBytes, Context.Limits.MaxWorkingBytes);
        EXPECT_EQ(Result.ManifestPath, Path("/output/desktop-vulkan-le64/manifest.json"));
        const auto         committed = Read(Result.ManifestPath);
        Core::Hash::SHA256 hash      = {};
        Digest256          digest    = {};
        ASSERT_TRUE(hash.Update(Bytes(committed)).Succeeded());
        ASSERT_TRUE(hash.Finalize(&digest).Succeeded());
        EXPECT_EQ(digest, Result.ManifestDigest);
        CookManifest manifest = {};
        ASSERT_EQ(manifest.Read(Bytes(committed), Request.Target, Context.Limits, Allocator, &Diagnostics).Value(), CookManifestReadStatus::Valid);
        ASSERT_EQ(manifest.Scenes().size(), 1u);
        EXPECT_EQ(manifest.Scenes()[0].UUID, SceneUUID);
        const auto         binary   = Read(Result.ManifestPath.Parent() / manifest.Scenes()[0].Result.Object.Path.CStr());
        ECS::SceneDocument document = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Decode(Bytes(binary), Schemas, &document, nullptr, Context.Limits.SceneLimits));
        ASSERT_NE(document.Settings(), nullptr);
        EXPECT_EQ(document.Settings()->UUID, SceneUUID);
        manifest.Reset();
        const auto first = Result;
        EXPECT_EQ(MaterialAdapter.Encodes, 1u);
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, first.ManifestDigest);
        EXPECT_EQ(Result.ReferencedObjectBytes, first.ReferencedObjectBytes);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        EXPECT_FALSE(Diagnostics.HasErrors());
        EXPECT_EQ(MaterialAdapter.Inspections, 2u);
        EXPECT_EQ(MaterialAdapter.Encodes, 1u);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, CategoryPeaksAreInlineBoundedNonsemanticAndPreservedOnFailure)
    {
        EXPECT_EQ(Result.PeakWorkingBytesByCategory, CookMemoryStatistics{}.PeakByCategory);
        ASSERT_TRUE(Cook().Succeeded());
        const auto original  = Result;
        const auto committed = Read(Result.ManifestPath);
        for (uint64_t peak : Result.PeakWorkingBytesByCategory)
            EXPECT_LE(peak, Result.PeakWorkingBytes);
        for (auto category : {CookMemoryCategory::Captures, CookMemoryCategory::Codecs, CookMemoryCategory::StagingManifest, CookMemoryCategory::DiagnosticsResult})
            EXPECT_GT(Result.PeakWorkingBytesByCategory[static_cast<size_t>(category)], 0u);
        const auto budget              = Context.Limits.MaxWorkingBytes;
        Context.Limits.MaxWorkingBytes = 1;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Result.PeakWorkingBytesByCategory, original.PeakWorkingBytesByCategory);
        EXPECT_EQ(Result.PeakWorkingBytes, original.PeakWorkingBytes);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        Context.Limits.MaxWorkingBytes = budget;
        for (auto& peak : Result.PeakWorkingBytesByCategory)
            peak = UINT64_MAX;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, original.ManifestDigest);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        for (uint64_t peak : Result.PeakWorkingBytesByCategory)
            EXPECT_LE(peak, Result.PeakWorkingBytes);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, ResolvesDependencyResultsLeafFirstAndRewritesMaterialTexturePaths)
    {
        AddTexture();
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_EQ(Result.AssetCount, 2u);
        EXPECT_EQ(Result.ObjectCount, 3u);
        const auto   bytes    = Read(Result.ManifestPath);
        CookManifest manifest = {};
        ASSERT_TRUE(manifest.Read(Bytes(bytes), Request.Target, Context.Limits, Allocator, &Diagnostics).Succeeded());
        const auto* texture  = manifest.FindAsset({CookAssetKind::Texture, TextureUUID});
        const auto* material = manifest.FindAsset({CookAssetKind::Material, MaterialUUID});
        ASSERT_NE(texture, nullptr);
        ASSERT_NE(material, nullptr);
        const auto                                   encoded = Read(Result.ManifestPath.Parent() / material->Result.Object.Path.CStr());
        Importers::AssetCodec::MaterialAssetDocument decoded = {};
        ASSERT_TRUE(Importers::AssetCodec::ReadMaterialAssetDocument(Bytes(encoded), &decoded, MaterialUUID).Succeeded());
        EXPECT_STREQ(decoded.Textures[0].Path, texture->Result.Object.Path.CStr());
        EXPECT_NE(decoded.Textures[0].Path, std::string("assets/pixel.png"));
        manifest.Reset();
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, InvalidRequestsFailBeforeAcquiringInputsAndPreserveThePreviousResult)
    {
        CookFrozenInputSource input(Owner);
        auto                  context = Context;
        context.Inputs                = &input;
        Result.ObjectCount            = 99;
        Diagnostics.Add({.Code = CookDiagnosticCode::IOFailure});
        auto request    = Request;
        request.Sources = {};
        EXPECT_EQ(SceneCooker::Cook(request, context, &Result, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidRequest);
        EXPECT_EQ(Result.ObjectCount, 99u);
        context.Limits.MaxWorkingBytes = 0;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidBudget);
        context        = Context;
        context.Inputs = &input;
        request        = Request;
        request.Target = CookTarget::Invalid;
        EXPECT_EQ(SceneCooker::Cook(request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedTarget);
        EXPECT_EQ(Result.ObjectCount, 99u);
        EXPECT_EQ(Allocator.Calls, 1u); // Only the preconfigured frozen root table.
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, PathBoundsRejectBeforeServiceQueriesEvenWhenTheWorkingBudgetIsInsufficient)
    {
        PreparationInputProbe probe    = {};
        auto                  context  = Context;
        context.Inputs                 = &probe;
        context.Limits.MaxWorkingBytes = 1;
        const size_t target_bytes      = Helpers::secure_strlen(CookTargetKey(Request.Target));
        const size_t manifest_bytes    = Helpers::secure_strlen(CookManifestName);
        const size_t calls             = Allocator.Calls;
        Result.ObjectCount             = 77;
        for (const bool target_overflow : {false, true})
        {
            std::string raw(1, '/');
            raw.append(VFS::VFS_MAX_PATH - (target_overflow ? 1 + target_bytes : 2 + target_bytes + manifest_bytes) - 1, 'a');
            auto       request = Request;
            const auto root    = VFS::VFSPath::Parse(raw.c_str());
            ASSERT_TRUE(root.Succeeded());
            request.OutputRoot = root.Value();
            const auto target  = request.OutputRoot.Append(CookTargetKey(request.Target));
            EXPECT_EQ(target.Failed(), target_overflow);
            if (target.Succeeded())
                EXPECT_TRUE(target.Value().Append(CookManifestName).Failed());
            EXPECT_EQ(SceneCooker::Cook(request, context, &Result, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidPath);
            EXPECT_EQ(Result.ObjectCount, 77u);
        }
        for (const size_t count : {VFS::VFS_MAX_COMPONENTS - 2, VFS::VFS_MAX_COMPONENTS - 1})
        {
            std::string raw = {};
            for (size_t i = 0; i < count; ++i)
                raw += "/a";
            auto       request = Request;
            const auto root    = VFS::VFSPath::Parse(raw.c_str());
            ASSERT_TRUE(root.Succeeded());
            request.OutputRoot = root.Value();
            EXPECT_EQ(SceneCooker::Cook(request, context, &Result, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidPath);
        }
        EXPECT_EQ(probe.Queries, 0u);
        EXPECT_EQ(probe.Acquisitions, 0u);
        EXPECT_EQ(Allocator.Calls, calls);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, ValidPathBoundariesPrepareWithoutAllocatingBeforeInputAcquisition)
    {
        PreparationInputProbe probe   = {};
        auto                  context = Context;
        context.Inputs                = &probe;
        const size_t calls            = Allocator.Calls;
        const auto   live             = Backing.OutstandingAllocations();
        const size_t suffix_bytes     = 2 + Helpers::secure_strlen(CookTargetKey(Request.Target)) + Helpers::secure_strlen(CookManifestName);
        std::string  longest(1, '/');
        longest.append(VFS::VFS_MAX_PATH - suffix_bytes - 2, 'a');
        std::string deepest = {};
        for (size_t i = 0; i < VFS::VFS_MAX_COMPONENTS - 3; ++i)
            deepest += "/a";
        for (cstring text : {"/", "/output/\xc3\xa9", longest.c_str(), deepest.c_str()})
        {
            auto       request = Request;
            const auto root    = VFS::VFSPath::Parse(text);
            ASSERT_TRUE(root.Succeeded());
            request.OutputRoot = root.Value();
            probe.Queries      = 0;
            probe.Acquisitions = 0;
            Result.ObjectCount = 77;
            EXPECT_EQ(SceneCooker::Cook(request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
            EXPECT_EQ(probe.Queries, 1u);
            EXPECT_EQ(probe.Acquisitions, 1u);
            EXPECT_EQ(Result.ObjectCount, 77u);
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Backing.OutstandingAllocations(), live);
        }
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, PathPreparationHonorsAnExactReservationBoundaryBeforeProviderQueriesOrAllocations)
    {
        PreparationInputProbe probe   = {};
        auto                  context = Context;
        context.Inputs                = &probe;
        const auto calls              = Allocator.Calls;
        const auto live               = Backing.OutstandingAllocations();
        uint64_t   low = 1, high = context.Limits.MaxWorkingBytes;
        Result.ObjectCount = 77;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        ASSERT_EQ(probe.Queries, 1u);
        while (high - low > 1)
        {
            context.Limits.MaxWorkingBytes = low + (high - low) / 2;
            probe.Queries                  = 0;
            probe.Acquisitions             = 0;
            const auto result              = SceneCooker::Cook(Request, context, &Result, &Diagnostics);
            if (probe.Queries)
                high = context.Limits.MaxWorkingBytes;
            else
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(probe.Acquisitions, 0u);
                low = context.Limits.MaxWorkingBytes;
            }
            EXPECT_EQ(Result.ObjectCount, 77u);
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Backing.OutstandingAllocations(), live);
        }
        context.Limits.MaxWorkingBytes = high - 1;
        probe.Queries                  = 0;
        probe.Acquisitions             = 0;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Queries, 0u);
        EXPECT_EQ(probe.Acquisitions, 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_STREQ(Diagnostics.Entries[0].Message, "The working budget cannot prepare target/manifest paths.");
        context.Limits.MaxWorkingBytes = high;
        EXPECT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Failed());
        EXPECT_EQ(probe.Queries, 1u);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, ExecutionStateHasAnExactAllocationFreeReservationBeforeInputAcquisition)
    {
        PreparationInputProbe probe   = {};
        auto                  context = Context;
        context.Inputs                = &probe;
        const auto calls              = Allocator.Calls;
        const auto live               = Backing.OutstandingAllocations();
        uint64_t   low = 1, high = context.Limits.MaxWorkingBytes;
        Result.ObjectCount = 77;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        ASSERT_EQ(probe.Acquisitions, 1u);
        while (high - low > 1)
        {
            context.Limits.MaxWorkingBytes = low + (high - low) / 2;
            probe.Acquisitions             = 0;
            const auto result              = SceneCooker::Cook(Request, context, &Result, &Diagnostics);
            if (probe.Acquisitions)
                high = context.Limits.MaxWorkingBytes;
            else
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                low = context.Limits.MaxWorkingBytes;
            }
            EXPECT_EQ(Result.ObjectCount, 77u);
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Backing.OutstandingAllocations(), live);
        }
        context.Limits.MaxWorkingBytes = high - 1;
        probe.Acquisitions             = 0;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Acquisitions, 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_STREQ(Diagnostics.Entries[0].Message, "The working budget cannot hold execution state.");
        context.Limits.MaxWorkingBytes = high;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(probe.Acquisitions, 1u);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, SetupQueryResultsAndNativeScratchDrainBeforeInputAcquisitionWithoutAllocating)
    {
        PreparationInputProbe probe   = {};
        auto                  context = Context;
        context.Inputs                = &probe;
        const auto registry           = QueryCookRegistryMemory(context);
        const auto callbacks          = QueryCookCallbackMemory(context, Allocator);
        const auto caller             = QueryCookCallerMemory(Request, context, &Result, &Diagnostics);
        ASSERT_TRUE(registry.Succeeded());
        ASSERT_TRUE(callbacks.Succeeded());
        ASSERT_TRUE(caller.Succeeded());
        const uint64_t other   = std::max(Detail::CookNativeServiceMemory::CallerQueryWorkingBytes(), Detail::CookNativeServiceMemory::ServiceQueryWorkingBytes());
        const uint64_t scratch = Detail::CookNativeServiceMemory::ProviderQueryWorkingBytes();
        ASSERT_GT(scratch, other); // The native provider chain, not either projection, sets this bound.
        const uint64_t reservation = sizeof(VFS::VFSResult<uint64_t>) + sizeof(VFS::VFSResult<CookServiceMemoryRequirements>) + scratch + sizeof(size_t) + sizeof(CookMemoryCharge);
        const auto     calls       = Allocator.Calls;
        const auto     live        = Backing.OutstandingAllocations();
        Result.ObjectCount         = 77;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        ASSERT_EQ(probe.Queries, 1u);
        ASSERT_EQ(probe.Acquisitions, 1u);
        EXPECT_EQ(probe.AcquireDiagnostics + reservation, probe.QueryDiagnostics + registry.Value().StateBytes + callbacks.Value().StateBytes + caller.Value());
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, OverlappingDomainsAreRejectedBeforeDirectoryCreation)
    {
        Context.ScratchRoot = Path("/output");
        EXPECT_EQ(Cook().Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        Context.ScratchRoot = Path("/assets");
        EXPECT_EQ(Cook().Error(), VFS::VFSError::InvalidPath);
        Request.OutputRoot  = Path("/scenes");
        Context.ScratchRoot = Path("/scratch");
        EXPECT_EQ(Cook().Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(Files.Exists(Path("/scenes/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, DuplicateScenePathsAndUUIDsNeverCommitOrLeakScratch)
    {
        ASSERT_TRUE(Cook().Succeeded());
        const auto         original    = Result;
        const auto         committed   = Read(Result.ManifestPath);
        const VFS::VFSPath duplicate[] = {Sources[0], Sources[0]};
        Request.Sources                = duplicate;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Result.ManifestDigest, original.ManifestDigest);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        SaveScene("/scenes/duplicate.scene");
        const VFS::VFSPath identities[] = {Sources[0], Path("/scenes/duplicate.scene")};
        Request.Sources                 = identities;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Result.ManifestDigest, original.ManifestDigest);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, EveryPublicRunAllocationRefusalPreservesTheCommittedPackageAndDrainsStorage)
    {
        AddTexture();
        ASSERT_TRUE(Cook().Succeeded());
        const auto committed = Read(Result.ManifestPath);
        const auto original  = Result;
        Request.ForceRebuild = true;
        Allocator.Calls      = 0;
        ASSERT_TRUE(Cook().Succeeded());
        const auto calls = Allocator.Calls;
        ASSERT_GT(calls, 10u);
        for (size_t failure = 1; failure <= calls; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            Result           = original;
            EXPECT_EQ(Cook().Error(), VFS::VFSError::OutOfMemory);
            EXPECT_TRUE(Diagnostics.HasErrors());
            EXPECT_EQ(Result.ManifestDigest, original.ManifestDigest);
            EXPECT_EQ(Result.Published, original.Published);
            EXPECT_EQ(Read(Result.ManifestPath), committed);
            ExpectScratchEmpty();
        }
        Allocator.FailAt = SIZE_MAX;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
    }

    TEST_P(SceneCookerTest, CompleteSceneBatchesShareAssetsAndAreCanonicalAcrossRequestOrderAndSufficientBudgets)
    {
        const auto original_uuid = SceneUUID;
        SceneUUID                = uuids::uuid::from_string("10000000-0000-0000-0000-000000000005").value();
        SaveScene("/scenes/second.scene");
        VFS::VFSPath sources[] = {Path("/scenes/second.scene"), Sources[0]};
        Request.Sources        = sources;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_EQ(Result.SceneCount, 2u);
        EXPECT_EQ(Result.AssetCount, 1u);
        EXPECT_EQ(Result.ObjectCount, 3u);
        const auto   committed = Read(Result.ManifestPath);
        CookManifest manifest  = {};
        ASSERT_TRUE(manifest.Read(Bytes(committed), Request.Target, Context.Limits, Allocator, &Diagnostics).Succeeded());
        ASSERT_EQ(manifest.Scenes().size(), 2u);
        EXPECT_EQ(manifest.Scenes()[0].UUID, original_uuid);
        EXPECT_EQ(manifest.Scenes()[1].UUID, SceneUUID);
        manifest.Reset();
        std::swap(sources[0], sources[1]);
        Context.Limits.MaxWorkingBytes = ZMega(64);
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, CachedObjectsSkipProducersAndFailedForcedEncodingPreservesThePublishedPackage)
    {
        ASSERT_TRUE(Cook().Succeeded());
        const auto committed        = Read(Result.ManifestPath);
        const auto first            = Result;
        MaterialAdapter.EncodeError = VFS::VFSError::IOError;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(MaterialAdapter.Encodes, 1u);
        Request.ForceRebuild = true;
        Result               = first;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::IOError);
        EXPECT_EQ(MaterialAdapter.Encodes, 2u);
        EXPECT_EQ(Result.ManifestDigest, first.ManifestDigest);
        EXPECT_EQ(Result.Published, first.Published);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        ASSERT_TRUE(Diagnostics.HasErrors());
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Stage, CookStage::Encode);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, ExistingWriterLocksAreNeverStolenOrRemoved)
    {
        ASSERT_TRUE(Cook().Succeeded());
        const auto first     = Result;
        const auto committed = Read(Result.ManifestPath);
        const auto lock      = Result.ManifestPath.Parent() / CookWriterLockName;
        ASSERT_TRUE(Files.CreateDirExclusive(lock).Succeeded());
        Write(lock / "foreign-owner", Bytes(std::string("preserve")));
        EXPECT_EQ(Cook().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Result.ManifestDigest, first.ManifestDigest);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        EXPECT_EQ(Read(lock / "foreign-owner"), "preserve");
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::OutputBusy);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, CorruptImmutableObjectsFailBothReuseAndForcedRebuildWithoutRepair)
    {
        ASSERT_TRUE(Cook().Succeeded());
        const auto   first     = Result;
        const auto   committed = Read(Result.ManifestPath);
        CookManifest manifest  = {};
        ASSERT_TRUE(manifest.Read(Bytes(committed), Request.Target, Context.Limits, Allocator, &Diagnostics).Succeeded());
        ASSERT_EQ(manifest.Assets().size(), 1u);
        const auto object = Result.ManifestPath.Parent() / manifest.Assets()[0].Result.Object.Path.CStr();
        manifest.Reset();
        Write(object, Bytes(std::string("corrupt")));
        for (bool force : {false, true})
        {
            Request.ForceRebuild = force;
            EXPECT_EQ(Cook().Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(Result.ManifestDigest, first.ManifestDigest);
            EXPECT_EQ(Read(Result.ManifestPath), committed);
            EXPECT_EQ(Read(object), "corrupt");
            ExpectScratchEmpty();
        }
    }

    TEST_P(SceneCookerTest, PostcommitProviderCleanupFailureIsWarningOnlyAndStillReleasesAllRunStorage)
    {
        struct CleanupSource final : ICookInputSource
        {
            CookFrozenInputSource Inner;
            uint32_t              Releases = 0;

            explicit CleanupSource(CookFrozenLibraryOwner& owner) : Inner(owner) {}
            VFS::VFSResult<void> AcquireRoots(CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                return Inner.AcquireRoots(lease, diagnostics);
            }
            VFS::VFSResult<void> Acquire(const CookCaptureContext& context, CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                return Inner.Acquire(context, lease, diagnostics);
            }
            VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator& covered) const noexcept override
            {
                const auto bytes = Inner.FixedWorkingBytes(covered);
                return bytes.Succeeded() ? VFS::VFSResult<uint64_t>::Ok(bytes.Value() + sizeof(*this) - sizeof(Inner)) : bytes;
            }
            VFS::VFSResult<void> ReleaseStorage() noexcept override
            {
                ++Releases;
                const auto released = Inner.ReleaseStorage();
                EXPECT_TRUE(released.Succeeded());
                return VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
            }
        } input(Owner);
        auto context    = Context;
        context.Inputs  = &input;
        const auto live = Backing.OutstandingAllocations();
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(input.Releases, 1u);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        EXPECT_FALSE(Diagnostics.HasErrors());
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Cleanup);
        EXPECT_EQ(Diagnostics.Entries[0].Severity, CookSeverity::Warning);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::CleanupWarning);
        EXPECT_FALSE(Read(Result.ManifestPath).empty());
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, PublicRunNeverRemovesUnownedScratchEntries)
    {
        Write("/scratch/foreign.tmp", "unowned");
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_EQ(Read(Path("/scratch/foreign.tmp")), "unowned");
        ASSERT_TRUE(Files.Remove(Path("/scratch/foreign.tmp")).Succeeded());
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, InsufficientWorkingBudgetFailsCleanlyWithoutChangingTheOldResult)
    {
        Result.ObjectCount             = 77;
        Context.Limits.MaxWorkingBytes = 1;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Result.ObjectCount, 77u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, UnknownBackingFootprintFailsBeforeRootAcquisitionOrOutputMutation)
    {
        struct UnknownAllocator final : ICookAllocator
        {
            size_t                                          Calls      = 0;
            bool                                            KnownState = false;
            VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                return KnownState ? VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({}) : VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(VFS::VFSError::Unsupported);
            }
            VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t, CookMemoryCategory) const noexcept override
            {
                return KnownState ? VFS::VFSResult<size_t>::Fail(VFS::VFSError::Unsupported) : VFS::VFSResult<size_t>::Ok(bytes);
            }
            VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ++Calls;
                return VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory);
            }
            void                 Free(void*) noexcept override {}
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
        } unknown          = {};
        Context.Allocator  = &unknown;
        Result.ObjectCount = 77;
        for (bool known_state : {false, true})
        {
            SCOPED_TRACE(known_state);
            unknown.KnownState = known_state;
            EXPECT_EQ(Cook().Error(), VFS::VFSError::Unsupported);
            EXPECT_EQ(Result.ObjectCount, 77u);
            EXPECT_EQ(unknown.Calls, 0u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedBackend);
            EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
            ExpectScratchEmpty();
            CookLibraryMutationLease mutation = {};
            EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        }
    }

    TEST_P(SceneCookerTest, UnaffordableIdleBackingPoolFailsBeforeAnyRootAcquisitionOrOutputMutation)
    {
        const auto requirements = Allocator.MemoryRequirements();
        ASSERT_TRUE(requirements.Succeeded());
        ASSERT_TRUE(requirements.Value().IncludesAllocations);
        Context.Limits.MaxWorkingBytes = requirements.Value().PoolBytes;
        const auto calls               = Allocator.Calls;
        Result.ObjectCount             = 77;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(Allocator.Calls, calls);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
        CookLibraryMutationLease mutation = {};
        EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST_P(SceneCookerTest, SeparatePreownedNativeBindingsAddTheirFullFootprintWithoutChangingPackageBytes)
    {
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded()); // Compare two previous-manifest reads.
        const auto             baseline = Result;
        CookPageAllocator      native   = {};
        CookFrozenLibraryOwner owner    = {};
        ASSERT_TRUE(native.Initialize(ZMega(4)).Succeeded());
        const CookFrozenRootRequest roots[] = {
            {"scenes", &Files, Path("/scenes")},
            {"assets", &Files, Path("/assets")}
        };
        ASSERT_TRUE(owner.Initialize(native, roots).Succeeded());
        CookFrozenInputSource input(owner);
        auto                  context = Context;
        context.Inputs                = &input;
        const auto actual             = native.Statistics().LiveBytes;
        const auto live               = Backing.OutstandingAllocations();
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes + actual);
        EXPECT_EQ(native.AllocationCount(), 1u);
        EXPECT_EQ(native.Statistics().LiveBytes, actual);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, SeparatePreownedPoolBudgetRefusalPrecedesInputExclusionAndOutputMutation)
    {
        CookPageAllocator      native = {};
        CookScratchAllocator   pool   = {};
        CookFrozenLibraryOwner owner  = {};
        ASSERT_TRUE(native.Initialize(ZMega(16)).Succeeded());
        ASSERT_TRUE(pool.Initialize(native, ZMega(8), CookMemoryCategory::Captures).Succeeded());
        const CookFrozenRootRequest roots[] = {
            {"scenes", &Files, Path("/scenes")},
            {"assets", &Files, Path("/assets")}
        };
        ASSERT_TRUE(owner.Initialize(pool, roots).Succeeded());
        CookFrozenInputSource input(owner);
        auto                  context  = Context;
        context.Inputs                 = &input;
        context.Limits.MaxWorkingBytes = ZMega(8);
        Result.ObjectCount             = 77;
        const auto calls               = Allocator.Calls;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(native.AllocationCount(), 1u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
        CookLibraryMutationLease mutation = {};
        EXPECT_TRUE(owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST_P(SceneCookerTest, BorrowedSchemaBackingWithoutAnOwnerFootprintFailsBeforeRootsOrOutputMutation)
    {
        alignas(DEFAULT_ALIGNMENT) Core::Containers::FixedArray<uint8_t, ZKilo(64)> storage = {};
        Core::Memory::ArenaAllocator                                                arena   = {};
        ASSERT_TRUE(arena.InitializeBorrowed(storage.data(), storage.size()));
        ECS::SceneComponentSchemaRegistry schemas = {};
        schemas.Initialize(&arena);
        auto context      = Context;
        context.Schemas   = &schemas;
        const auto offset = arena.m_current_offset;
        ExpectSetupRefusal(context, VFS::VFSError::Unsupported, CookDiagnosticCode::UnsupportedBackend);
        EXPECT_EQ(arena.m_current_offset, offset);
    }

    TEST_P(SceneCookerTest, UndeclaredOrUnknownCallbackStorageFailsBeforeInputRootsOrAdapterExecution)
    {
        auto context      = Context;
        context.Callbacks = nullptr;
        ExpectSetupRefusal(context, VFS::VFSError::Unsupported, CookDiagnosticCode::UnsupportedBackend);
        struct Unknown final : ICookCallbackStorage
        {
            bool Contains(const void*) const noexcept override
            {
                return true;
            }
        } unknown         = {};
        context.Callbacks = &unknown;
        ExpectSetupRefusal(context, VFS::VFSError::Unsupported, CookDiagnosticCode::UnsupportedBackend);
        EXPECT_EQ(MaterialAdapter.Inspections, 0u);
        EXPECT_EQ(MaterialAdapter.Encodes, 0u);
    }

    TEST_P(SceneCookerTest, UnaffordableCallbackArenaFailsBeforeInputExclusionOrAdapterExecution)
    {
        Core::Memory::ArenaAllocator arena = {};
        arena.Initialize(ZMega(16), 0, "UnaffordableCallbackStorage");
        ASSERT_NE(arena.AllocateNoZero(ZMega(8), 16), nullptr);
        auto* memory = arena.Allocate(sizeof(AdapterProbe), alignof(AdapterProbe));
        ASSERT_NE(memory, nullptr);
        auto*                    probe = new (memory) AdapterProbe(CookMaterialAdapter());
        CookArenaCallbackStorage owner(arena);
        CookAdapterRegistry      adapters  = {};
        const CookAdapterFns     entries[] = {CookEnvironmentAdapter(), probe->Functions(), CookMeshAdapter(), CookTextureAdapter()};
        ASSERT_TRUE(adapters.Initialize(entries).Succeeded());
        auto context                   = Context;
        context.Adapters               = &adapters;
        context.Callbacks              = &owner;
        context.Limits.MaxWorkingBytes = ZMega(8);
        ExpectSetupRefusal(context, VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded);
        EXPECT_EQ(probe->Inspections, 0u);
        EXPECT_EQ(probe->Encodes, 0u);
        probe->~AdapterProbe();
    }

    TEST_P(SceneCookerTest, NativeCallbackBackingAddsItsExactFootprintWithoutChangingPackageBytes)
    {
        const auto original = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(original.Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded()); // Include the same fixed parser phase.
        const auto                   baseline = Result;
        Core::Memory::ArenaAllocator arena    = {};
        arena.Initialize(ZMega(2), 0, "MeasuredCallbackStorage");
        auto* memory = arena.Allocate(sizeof(AdapterProbe), alignof(AdapterProbe));
        ASSERT_NE(memory, nullptr);
        auto*                    probe = new (memory) AdapterProbe(CookMaterialAdapter());
        CookArenaCallbackStorage owner(arena);
        CookAdapterRegistry      adapters  = {};
        const CookAdapterFns     entries[] = {CookEnvironmentAdapter(), probe->Functions(), CookMeshAdapter(), CookTextureAdapter()};
        ASSERT_TRUE(adapters.Initialize(entries).Succeeded());
        auto context      = Context;
        context.Adapters  = &adapters;
        context.Callbacks = &owner;
        const auto native = QueryCookCallbackMemory(context, Allocator);
        ASSERT_TRUE(native.Succeeded());
        CookFrozenInputSource input(Owner);
        context.Inputs = &input;
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes + native.Value().StateBytes + native.Value().StorageBytes - original.Value().StateBytes - original.Value().StorageBytes);
        EXPECT_EQ(probe->Inspections, 1u);
        EXPECT_EQ(probe->Encodes, 0u);
        ExpectScratchEmpty();
        probe->~AdapterProbe();
    }

    TEST_P(SceneCookerTest, NativeArenaResidentServicesUseAnExactCombinedChargeWithoutChangingThePackage)
    {
        const auto original_registry  = QueryCookRegistryMemory(Context);
        const auto original_callbacks = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(original_registry.Succeeded());
        ASSERT_TRUE(original_callbacks.Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded()); // Compare matching no-op read phases.
        const auto                   baseline = Result;
        Core::Memory::ArenaAllocator arena    = {};
        arena.Initialize(ZMega(2), 0, "ResidentCookServices");
        void* schema_memory  = arena.Allocate(sizeof(ECS::SceneComponentSchemaRegistry), alignof(ECS::SceneComponentSchemaRegistry));
        void* adapter_memory = arena.Allocate(sizeof(CookAdapterRegistry), alignof(CookAdapterRegistry));
        void* owner_memory   = arena.Allocate(sizeof(CookArenaCallbackStorage), alignof(CookArenaCallbackStorage));
        void* probe_memory   = arena.Allocate(sizeof(AdapterProbe), alignof(AdapterProbe));
        ASSERT_NE(schema_memory, nullptr);
        ASSERT_NE(adapter_memory, nullptr);
        ASSERT_NE(owner_memory, nullptr);
        ASSERT_NE(probe_memory, nullptr);
        auto*                schemas   = new (schema_memory) ECS::SceneComponentSchemaRegistry(std::move(Schemas));
        auto*                adapters  = new (adapter_memory) CookAdapterRegistry{};
        auto*                owner     = new (owner_memory) CookArenaCallbackStorage(arena);
        auto*                probe     = new (probe_memory) AdapterProbe(CookMaterialAdapter());
        const CookAdapterFns entries[] = {CookEnvironmentAdapter(), probe->Functions(), CookMeshAdapter(), CookTextureAdapter()};
        ASSERT_TRUE(adapters->Initialize(entries).Succeeded());
        auto context         = Context;
        context.Schemas      = schemas;
        context.Adapters     = adapters;
        context.Callbacks    = owner;
        const auto registry  = QueryCookRegistryMemory(context);
        const auto callbacks = QueryCookCallbackMemory(context, Allocator);
        ASSERT_TRUE(registry.Succeeded());
        ASSERT_TRUE(callbacks.Succeeded());
        CookFrozenInputSource input(Owner);
        context.Inputs = &input;
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes + registry.Value().StateBytes + registry.Value().StorageBytes + callbacks.Value().StateBytes + callbacks.Value().StorageBytes - original_registry.Value().StateBytes - original_registry.Value().StorageBytes - original_callbacks.Value().StateBytes - original_callbacks.Value().StorageBytes);
        EXPECT_EQ(probe->Inspections, 1u);
        EXPECT_EQ(probe->Encodes, 0u);
        ExpectScratchEmpty();
        probe->~AdapterProbe();
        owner->~CookArenaCallbackStorage();
        adapters->~CookAdapterRegistry();
        schemas->~SceneComponentSchemaRegistry();
    }

    TEST_P(SceneCookerTest, NativeArenaResidentCallerValuesReduceTheExactChargeWithoutChangingThePackage)
    {
        struct CallerValues
        {
            SceneCookRequest Request     = {};
            SceneCookContext Context     = {};
            VFS::VFSPath     Sources[1]  = {};
            SceneCookResult  Result      = {};
            CookDiagnostics  Diagnostics = {};
        };
        // Commit the resident storage before the baseline: both runs retain
        // exactly the same registry pages, differing only in caller-value charge.
        void* storage = Memory.MainArena.Allocate(sizeof(CallerValues), alignof(CallerValues));
        ASSERT_NE(storage, nullptr);
        auto* values            = new (storage) CallerValues{};
        values->Sources[0]      = Sources[0];
        values->Request         = Request;
        values->Request.Sources = values->Sources;
        values->Context         = Context;
        const auto original     = QueryCookCallerMemory(Request, Context, &Result, &Diagnostics);
        const auto resident     = QueryCookCallerMemory(values->Request, values->Context, &values->Result, &values->Diagnostics);
        ASSERT_TRUE(original.Succeeded());
        ASSERT_TRUE(resident.Succeeded());
        ASSERT_EQ(resident.Value(), 0u);
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        const auto            baseline = Result;
        const auto            live     = Backing.OutstandingAllocations();
        CookFrozenInputSource input(Owner);
        values->Context.Inputs = &input;
        ASSERT_TRUE(SceneCooker::Cook(values->Request, values->Context, &values->Result, &values->Diagnostics).Succeeded());
        EXPECT_FALSE(values->Result.Published);
        EXPECT_EQ(values->Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(values->Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(values->Result.PeakWorkingBytes, baseline.PeakWorkingBytes - original.Value());
        EXPECT_EQ(Diagnostics.Count, 0u);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
        values->~CallerValues();
    }

    TEST_P(SceneCookerTest, CallerValueBudgetRefusalPrecedesInputAcquisitionAndOutputMutation)
    {
        struct PaddingProvider final : ICookInputSource
        {
            uint64_t                 Limit          = 0;
            uint64_t                 LaterServices  = 0;
            uint64_t                 CallerHeadroom = 0;
            size_t                   Acquisitions   = 0;

            VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator& allocator) const noexcept override
            {
                // Conservative test profile leaves exactly one byte too little
                // at the caller-value seam, independently of platform sizeof.
                const auto live = allocator.Statistics().LiveBytes;
                if (live > Limit || LaterServices > Limit - live || CallerHeadroom > Limit - live - LaterServices)
                    return VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::SizeLimitExceeded);
                return VFS::VFSResult<uint64_t>::Ok(Limit - live - LaterServices - CallerHeadroom);
            }
            VFS::VFSResult<void> AcquireRoots(CookInputLease*, CookDiagnostics*) override
            {
                ++Acquisitions;
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                return AcquireRoots(lease, diagnostics);
            }
        };
        const auto registry  = QueryCookRegistryMemory(Context);
        const auto callbacks = QueryCookCallbackMemory(Context, Allocator);
        const auto caller    = QueryCookCallerMemory(Request, Context, &Result, &Diagnostics);
        ASSERT_TRUE(registry.Succeeded());
        ASSERT_TRUE(callbacks.Succeeded());
        ASSERT_TRUE(caller.Succeeded());
        ASSERT_GT(caller.Value(), 0u);
        PaddingProvider provider = {};
        provider.Limit           = Context.Limits.MaxWorkingBytes;
        const auto facades       = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(facades.Succeeded());
        provider.LaterServices  = registry.Value().StateBytes + registry.Value().StorageBytes + callbacks.Value().StateBytes + callbacks.Value().StorageBytes + facades.Value().StateBytes + facades.Value().StorageBytes;
        provider.CallerHeadroom = caller.Value() - 1;
        auto context            = Context;
        context.Inputs          = &provider;
        Result.ObjectCount      = 77;
        const auto calls        = Allocator.Calls;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(provider.Acquisitions, 0u);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(MaterialAdapter.Inspections, 0u);
        EXPECT_EQ(MaterialAdapter.Encodes, 0u);
        EXPECT_EQ(Result.ObjectCount, 77u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
        CookLibraryMutationLease mutation = {};
        EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST_P(SceneCookerTest, NativeArenaResidentVFSFacadeReducesTheExactChargeWithoutChangingThePackage)
    {
        VFS::VFSContext external = {};
        external.Initialize(&Memory.MainArena);
        ASSERT_TRUE(external.Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
        void* storage = Memory.MainArena.Allocate(sizeof(VFS::VFSContext), alignof(VFS::VFSContext));
        ASSERT_NE(storage, nullptr);
        auto* resident = new (storage) VFS::VFSContext{};
        resident->Initialize(&Memory.MainArena);
        ASSERT_TRUE(resident->Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
        // Both facades and their arena storage exist before either run. Their
        // independent mapping domains have equal capacity and the same backend.
        Context.Output      = &external;
        Context.Scratch     = &external;
        const auto original = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(original.Succeeded());
        ASSERT_EQ(original.Value().StateBytes, sizeof(external));
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        const auto baseline  = Result;
        const auto live      = Backing.OutstandingAllocations();
        auto       context   = Context;
        context.Output       = resident;
        context.Scratch      = resident;
        const auto relocated = QueryCookVFSServiceMemory(context);
        ASSERT_TRUE(relocated.Succeeded());
        EXPECT_EQ(relocated.Value().StateBytes, 0u);
        EXPECT_EQ(relocated.Value().StorageBytes, 0u);
        CookFrozenInputSource input(Owner);
        context.Inputs = &input;
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes - sizeof(external));
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
        Context.Output  = &Files;
        Context.Scratch = &Files;
        resident->Shutdown();
        resident->~VFSContext();
        external.Shutdown();
    }

    TEST_P(SceneCookerTest, RegistryCallbackVFSAndCallerChargesReuseOneSetupSnapshot)
    {
        struct ProfiledContext final : VFS::VFSContext
        {
            const Core::Memory::ArenaAllocator*    Arena   = nullptr;
            mutable uint32_t                       Queries = 0;

            VFS::VFSResult<VFS::VFSContextStorage> NativeStorage() const noexcept override
            {
                ++Queries;
                return VFS::VFSResult<VFS::VFSContextStorage>::Ok({this, sizeof(*this), Arena});
            }
        } output = {}, scratch = {};
        output.Arena = scratch.Arena = &Memory.MainArena;
        output.Initialize(&Memory.MainArena);
        scratch.Initialize(&Memory.MainArena);
        ASSERT_TRUE(output.Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
        ASSERT_TRUE(scratch.Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
        void* storage = Memory.MainArena.Allocate(sizeof(AdapterProbe), alignof(AdapterProbe));
        ASSERT_NE(storage, nullptr);
        auto*                    probe = new (storage) AdapterProbe(CookMaterialAdapter());
        CookArenaCallbackStorage native_owner(Memory.MainArena);
        CookAdapterRegistry      native_adapters = {};
        const CookAdapterFns     entries[]       = {CookEnvironmentAdapter(), probe->Functions(), CookMeshAdapter(), CookTextureAdapter()};
        ASSERT_TRUE(native_adapters.Initialize(entries).Succeeded());
        Digest256 baseline = {};
        for (const bool native : {false, true})
        {
            for (const bool shared : {true, false})
            {
                output.Queries = scratch.Queries = 0;
                auto context                     = Context;
                context.Output                   = &output;
                context.Scratch                  = shared ? &output : &scratch;
                if (native)
                {
                    context.Adapters  = &native_adapters;
                    context.Callbacks = &native_owner;
                }
                CookFrozenInputSource input(Owner);
                context.Inputs = &input;
                ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
                EXPECT_EQ(output.Queries, 1u);
                EXPECT_EQ(scratch.Queries, shared ? 0u : 1u);
                if (!native && shared)
                    baseline = Result.ManifestDigest;
                else
                {
                    EXPECT_EQ(Result.ManifestDigest, baseline);
                    EXPECT_FALSE(Result.Published);
                }
                ExpectScratchEmpty();
            }
        }
        EXPECT_EQ(probe->Inspections, 2u);
        EXPECT_EQ(probe->Encodes, 0u);
        probe->~AdapterProbe();
        output.Shutdown();
        scratch.Shutdown();
    }

    TEST_P(SceneCookerTest, NativeArenaResidentWorkingAllocatorReducesExactStateChargeWithoutChangingPackage)
    {
        CookScratchAllocator external = {};
        void*                storage  = Memory.MainArena.Allocate(sizeof(CookScratchAllocator), alignof(CookScratchAllocator));
        ASSERT_NE(storage, nullptr);
        std::unique_ptr<CookScratchAllocator, void (*)(CookScratchAllocator*)> resident(new (storage) CookScratchAllocator{}, [](CookScratchAllocator* allocator) { std::destroy_at(allocator); });
        // Both objects and pools exist before either cook; only the selected
        // allocator's fixed object residence differs. Parent idle backing stays
        // charged, even though these child workspaces share that same pool.
        ASSERT_TRUE(external.Initialize(Backing, ZKilo(512), CookMemoryCategory::Codecs).Succeeded());
        ASSERT_TRUE(resident->Initialize(Backing, ZKilo(512), CookMemoryCategory::Codecs).Succeeded());
        Context.Allocator = &external;
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        const auto baseline = Result;
        const auto live     = Backing.OutstandingAllocations();
        Context.Allocator   = resident.get();
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes - sizeof(external));
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        EXPECT_EQ(resident->OutstandingAllocations(), 0u);
        ExpectScratchEmpty();
        Context.Allocator = &Allocator;
    }

    TEST_P(SceneCookerTest, NativeArenaResidentFrozenInputStateReducesExactChargeWithoutChangingPackage)
    {
        void* owner_storage    = Memory.MainArena.Allocate(sizeof(CookFrozenLibraryOwner), alignof(CookFrozenLibraryOwner));
        void* source_storage   = Memory.MainArena.Allocate(sizeof(CookFrozenInputSource), alignof(CookFrozenInputSource));
        void* combined_storage = Memory.MainArena.Allocate(sizeof(CookFrozenInputSource), alignof(CookFrozenInputSource));
        ASSERT_NE(owner_storage, nullptr);
        ASSERT_NE(source_storage, nullptr);
        ASSERT_NE(combined_storage, nullptr);
        std::unique_ptr<CookFrozenLibraryOwner, void (*)(CookFrozenLibraryOwner*)> owner(new (owner_storage) CookFrozenLibraryOwner{}, [](CookFrozenLibraryOwner* value) { std::destroy_at(value); });
        const CookFrozenRootRequest                                                roots[] = {
            {"assets", &Files, Path("/assets")},
            {"scenes", &Files, Path("/scenes")}
        };
        ASSERT_TRUE(owner->Initialize(Allocator, roots).Succeeded());
        std::unique_ptr<CookFrozenInputSource, void (*)(CookFrozenInputSource*)> source(new (source_storage) CookFrozenInputSource{Owner}, [](CookFrozenInputSource* value) { std::destroy_at(value); });
        std::unique_ptr<CookFrozenInputSource, void (*)(CookFrozenInputSource*)> combined(new (combined_storage) CookFrozenInputSource{*owner}, [](CookFrozenInputSource* value) { std::destroy_at(value); });
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        const auto baseline = Result;
        const auto manifest = Read(Path("/output/desktop-vulkan-le64/manifest.json"));
        const auto live     = Backing.OutstandingAllocations();
        auto       context  = Context;
        context.Inputs      = source.get();
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes - sizeof(*source));
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        context.Inputs = combined.get();
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes - sizeof(*source) - sizeof(*owner));
        EXPECT_EQ(Read(Path("/output/desktop-vulkan-le64/manifest.json")), manifest);
        EXPECT_EQ(Backing.OutstandingAllocations(), live);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, CoveredFrozenProviderStillChargesIndependentBindingMappingBeforeInputAcquisition)
    {
        void* page_storage   = Memory.MainArena.Allocate(sizeof(CookPageAllocator), alignof(CookPageAllocator));
        void* owner_storage  = Memory.MainArena.Allocate(sizeof(CookFrozenLibraryOwner), alignof(CookFrozenLibraryOwner));
        void* source_storage = Memory.MainArena.Allocate(sizeof(CookFrozenInputSource), alignof(CookFrozenInputSource));
        ASSERT_NE(page_storage, nullptr);
        ASSERT_NE(owner_storage, nullptr);
        ASSERT_NE(source_storage, nullptr);
        std::unique_ptr<CookPageAllocator, void (*)(CookPageAllocator*)> page(new (page_storage) CookPageAllocator{}, [](CookPageAllocator* value) { std::destroy_at(value); });
        ASSERT_TRUE(page->Initialize(ZMega(4)).Succeeded());
        std::unique_ptr<CookFrozenLibraryOwner, void (*)(CookFrozenLibraryOwner*)> owner(new (owner_storage) CookFrozenLibraryOwner{}, [](CookFrozenLibraryOwner* value) { std::destroy_at(value); });
        const CookFrozenRootRequest                                                roots[] = {
            {"assets", &Files, Path("/assets")},
            {"scenes", &Files, Path("/scenes")}
        };
        ASSERT_TRUE(owner->Initialize(*page, roots).Succeeded());
        std::unique_ptr<CookFrozenInputSource, void (*)(CookFrozenInputSource*)> source(new (source_storage) CookFrozenInputSource{*owner}, [](CookFrozenInputSource* value) { std::destroy_at(value); });
        struct ObservedInput                                                     final : ICookInputSource
        {
            CookFrozenInputSource*   Source       = nullptr;
            mutable uint64_t         Before       = 0;
            mutable uint64_t         Required     = 0;
            size_t                   Acquisitions = 0;
            bool                     InspectOnly  = true;

            VFS::VFSResult<uint64_t> UncoveredFixedWorkingBytes(const ICookAllocator& allocator, const ICookMemoryCoverage& coverage) const noexcept override
            {
                const auto native = Source->UncoveredFixedWorkingBytes(allocator, coverage);
                if (native.Failed())
                    return native;
                Before   = allocator.Statistics().LiveBytes;
                Required = native.Value() + sizeof(*this);
                return InspectOnly ? VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::Unsupported) : VFS::VFSResult<uint64_t>::Ok(Required);
            }
            VFS::VFSResult<void> AcquireRoots(CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                ++Acquisitions;
                return Source->AcquireRoots(lease, diagnostics);
            }
            VFS::VFSResult<void> Acquire(const CookCaptureContext& context, CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                ++Acquisitions;
                return Source->Acquire(context, lease, diagnostics);
            }
            VFS::VFSResult<void> ReleaseStorage() noexcept override
            {
                return Source->ReleaseStorage();
            }
        } probe            = {};
        probe.Source       = source.get();
        auto context       = Context;
        context.Inputs     = &probe;
        Result.ObjectCount = 77;
        const auto calls   = Allocator.Calls;
        const auto mapping = page->Statistics().LiveBytes - sizeof(*page);
        ASSERT_GT(mapping, 0u);
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        ASSERT_EQ(probe.Required, mapping + sizeof(probe));
        probe.InspectOnly              = false;
        context.Limits.MaxWorkingBytes = probe.Before + probe.Required - 1;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(probe.Acquisitions, 0u);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(page->AllocationCount(), 1u);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(MaterialAdapter.Inspections, 0u);
        EXPECT_EQ(MaterialAdapter.Encodes, 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
        {
            CookLibraryMutationLease mutation = {};
            ASSERT_TRUE(owner->TryAcquireMutation(&mutation).Succeeded());
        }
        context.Limits.MaxWorkingBytes = Context.Limits.MaxWorkingBytes;
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_EQ(probe.Acquisitions, 1u);
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(page->AllocationCount(), 1u);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, CoveredAllocatorStateStillRequiresEveryNativeServiceChargeBeforeInputAcquisition)
    {
        struct PaddingProvider final : ICookInputSource
        {
            uint64_t                 Limit            = 0;
            uint64_t                 RegistryHeadroom = 0;
            size_t                   Acquisitions     = 0;

            VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator& allocator) const noexcept override
            {
                const auto live = allocator.Statistics().LiveBytes;
                if (live > Limit || RegistryHeadroom > Limit - live)
                    return VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::SizeLimitExceeded);
                return VFS::VFSResult<uint64_t>::Ok(Limit - live - RegistryHeadroom);
            }
            VFS::VFSResult<void> AcquireRoots(CookInputLease*, CookDiagnostics*) override
            {
                ++Acquisitions;
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease* lease, CookDiagnostics* diagnostics) override
            {
                return AcquireRoots(lease, diagnostics);
            }
        } provider    = {};
        void* storage = Memory.MainArena.Allocate(sizeof(CookPageAllocator), alignof(CookPageAllocator));
        ASSERT_NE(storage, nullptr);
        std::unique_ptr<CookPageAllocator, void (*)(CookPageAllocator*)> resident(new (storage) CookPageAllocator{}, [](CookPageAllocator* allocator) { std::destroy_at(allocator); });
        ASSERT_TRUE(resident->Initialize(ZMega(4)).Succeeded());
        auto context        = Context;
        context.Inputs      = &provider;
        context.Allocator   = resident.get();
        const auto registry = QueryCookRegistryMemory(context);
        ASSERT_TRUE(registry.Succeeded());
        const auto required = registry.Value().StateBytes + registry.Value().StorageBytes;
        ASSERT_GT(required, 0u);
        provider.Limit            = context.Limits.MaxWorkingBytes;
        provider.RegistryHeadroom = required - 1;
        Result.ObjectCount        = 77;
        EXPECT_EQ(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(provider.Acquisitions, 0u);
        EXPECT_EQ(resident->AllocationCount(), 0u);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(MaterialAdapter.Inspections, 0u);
        EXPECT_EQ(MaterialAdapter.Encodes, 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Stage, CookStage::Setup);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        EXPECT_FALSE(Files.Exists(Path("/output/desktop-vulkan-le64")).Value());
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerTest, UnknownOrUnaffordableVFSFacadeStorageFailsBeforeInputAcquisitionOrOutputMutation)
    {
        struct UnknownContext final : VFS::VFSContext
        {
        } unknown = {};
        unknown.Initialize(&Memory.MainArena);
        ASSERT_TRUE(unknown.Mount(Backend, VFS::VFSPath::Root(), 0).Succeeded());
        auto context   = Context;
        context.Output = &unknown;
        ExpectSetupRefusal(context, VFS::VFSError::Unsupported, CookDiagnosticCode::UnsupportedBackend);
        const auto publication = Files.BeginImportPublication();
        ExpectSetupRefusal(Context, VFS::VFSError::Unsupported, CookDiagnosticCode::UnsupportedBackend);
        Files.EndImportPublication(publication);
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(64), 0, "UnaffordableVFSFacade");
        VFS::VFSContext files = {};
        files.Initialize(&separate);
        ASSERT_NE(separate.AllocateNoZero(ZMega(32), DEFAULT_ALIGNMENT), nullptr);
        context.Output  = &files;
        context.Scratch = &files;
        ExpectSetupRefusal(context, VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded);
        EXPECT_EQ(MaterialAdapter.Inspections, 0u);
        EXPECT_EQ(MaterialAdapter.Encodes, 0u);
    }

    TEST_P(SceneCookerTest, PreownedRegistryCommittedStorageMustFitBeforeInputExclusionOrOutputMutation)
    {
        Core::Memory::ArenaAllocator arena = {};
        arena.Initialize(ZMega(16), 0, "UnaffordableRegistryStorage");
        ECS::SceneComponentSchemaRegistry schemas = {};
        schemas.Initialize(&arena);
        ASSERT_NE(arena.AllocateNoZero(ZMega(8), 16), nullptr);
        auto context                   = Context;
        context.Schemas                = &schemas;
        context.Limits.MaxWorkingBytes = ZMega(8);
        ExpectSetupRefusal(context, VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded);
    }

    TEST_P(SceneCookerTest, UninitializedOptionalMigrationRegistryFailsBeforeAnyInputWork)
    {
        ECS::SceneMigrationRegistry migrations = {};
        auto                        context    = Context;
        context.Migrations                     = &migrations;
        ExpectSetupRefusal(context, VFS::VFSError::InvalidPath, CookDiagnosticCode::InvalidRequest);
    }

    TEST_P(SceneCookerTest, IndependentMigrationRegistryAddsItsBackingChargeWithoutChangingPackageBytes)
    {
        const auto original = QueryCookRegistryMemory(Context);
        ASSERT_TRUE(original.Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded()); // Compare matching no-op read phases.
        const auto                   baseline = Result;
        Core::Memory::ArenaAllocator arena    = {};
        arena.Initialize(ZMega(1), 0, "OptionalMigrations");
        ECS::SceneMigrationRegistry migrations = {};
        ASSERT_TRUE(migrations.Initialize(&arena));
        auto context        = Context;
        context.Migrations  = &migrations;
        const auto extended = QueryCookRegistryMemory(context);
        ASSERT_TRUE(extended.Succeeded());
        CookFrozenInputSource input(Owner);
        context.Inputs = &input;
        ASSERT_TRUE(SceneCooker::Cook(Request, context, &Result, &Diagnostics).Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, baseline.ManifestDigest);
        EXPECT_EQ(Result.ObjectCount, baseline.ObjectCount);
        EXPECT_EQ(Result.PeakWorkingBytes, baseline.PeakWorkingBytes + extended.Value().StateBytes - original.Value().StateBytes + extended.Value().StorageBytes - original.Value().StorageBytes);
        ExpectScratchEmpty();
    }

    class SceneCookerResourceTest : public SceneCookerTest
    {
    protected:
        static constexpr uint32_t                                     SceneCapacity = CookLimits{}.MaxScenes;
        Core::Containers::FixedArray<VFS::VFSPath, SceneCapacity + 1> Batch         = {};
        Core::Containers::FixedArray<uuids::uuid, SceneCapacity>      Identities    = {};

        void                                                          SetUp() override
        {
            // Fixture capacity, not a cooker preset. Keep the same reclaimable
            // 4 MiB working slab while allowing 256 source/capture/object files.
            BackendStorageBytes = ZMega(32);
            BackendMaxNodes     = 2048;
            SceneCookerTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            Context.Limits.MaxWorkingBytes = ZMega(128);
        }

        void GenerateBatch(uint32_t count, uint32_t first_id = 1)
        {
            ASSERT_LE(count, SceneCapacity);
            for (uint32_t index = 0; index < count; ++index)
            {
                char uuid[37] = {}, path[64] = {};
                ASSERT_EQ(std::snprintf(uuid, sizeof(uuid), "10000000-0000-0000-0000-%012x", first_id + index), 36);
                Identities[index] = uuids::uuid::from_string(uuid).value();
                SceneUUID         = Identities[index];
                if (!index)
                    Batch[index] = Sources[0];
                else
                {
                    const auto length = std::snprintf(path, sizeof(path), "/scenes/batch%03u.scene", index);
                    ASSERT_GT(length, 0);
                    ASSERT_LT(static_cast<size_t>(length), sizeof(path));
                    Batch[index] = Path(path);
                }
                SaveScene(Batch[index].CStr());
                ASSERT_FALSE(HasFatalFailure());
            }
            Batch[count]    = Path("/scenes/unrequested.scene");
            Request.Sources = {Batch.data(), count};
        }

        void VerifyPackage(const SceneCookResult& result, uint32_t count)
        {
            const auto   committed = Read(result.ManifestPath);
            CookManifest manifest  = {};
            const auto   read      = manifest.Read(Bytes(committed), Request.Target, Context.Limits, Allocator, &Diagnostics);
            ASSERT_TRUE(read.Succeeded());
            ASSERT_EQ(read.Value(), CookManifestReadStatus::Valid);
            ASSERT_EQ(manifest.Scenes().size(), count);
            ASSERT_EQ(manifest.Assets().size(), result.AssetCount);
            const auto verify = [&](const CookObjectDescriptor& object) {
                const auto bytes = Read(result.ManifestPath.Parent() / object.Path.CStr());
                EXPECT_EQ(bytes.size(), object.Size);
                Core::Hash::SHA256 hash   = {};
                Digest256          digest = {};
                EXPECT_TRUE(hash.Update(Bytes(bytes)).Succeeded());
                EXPECT_TRUE(hash.Finalize(&digest).Succeeded());
                EXPECT_EQ(digest, object.Digest);
                return bytes;
            };
            uint64_t total = 0;
            for (const auto& asset : manifest.Assets())
            {
                (void) verify(asset.Result.Object);
                total += asset.Result.Object.Size;
            }
            for (uint32_t index = 0; index < count; ++index)
            {
                const auto& scene = manifest.Scenes()[index];
                EXPECT_EQ(scene.UUID, Identities[index]);
                const auto roots = manifest.Roots(index);
                ASSERT_EQ(roots.size(), 1u);
                EXPECT_EQ(roots[0].Kind, CookAssetKind::Material);
                EXPECT_EQ(roots[0].UUID, MaterialUUID);
                const auto         binary   = verify(scene.Result.Object);
                ECS::SceneDocument document = {};
                ASSERT_TRUE(ECS::BinarySceneCodec::Decode(Bytes(binary), Schemas, &document, nullptr, Context.Limits.SceneLimits));
                ASSERT_NE(document.Settings(), nullptr);
                EXPECT_EQ(document.Settings()->UUID, scene.UUID);
                EXPECT_EQ(document.EntityCount(), 1u);
                total += scene.Result.Object.Size;
            }
            EXPECT_EQ(total, result.ReferencedObjectBytes);
        }

        void ExpectPreserved(const SceneCookResult& previous, const std::string& committed)
        {
            EXPECT_EQ(Result.ManifestPath, previous.ManifestPath);
            EXPECT_EQ(Result.ManifestDigest, previous.ManifestDigest);
            EXPECT_EQ(Result.SceneCount, previous.SceneCount);
            EXPECT_EQ(Result.AssetCount, previous.AssetCount);
            EXPECT_EQ(Result.ObjectCount, previous.ObjectCount);
            EXPECT_EQ(Result.ReferencedObjectBytes, previous.ReferencedObjectBytes);
            EXPECT_EQ(Result.PeakWorkingBytes, previous.PeakWorkingBytes);
            EXPECT_EQ(Result.PeakWorkingBytesByCategory, previous.PeakWorkingBytesByCategory);
            EXPECT_EQ(Result.Published, previous.Published);
            EXPECT_EQ(Read(previous.ManifestPath), committed);
            VFS::VFSReadRootLease root = {};
            ASSERT_TRUE(Files.AcquireReadRoot(previous.ManifestPath.Parent(), &root).Succeeded());
            Core::Containers::FixedArray<uint8_t, ZKilo(4)> buffer = {};
            const auto                                      visit  = [](void*, const VFS::VFSRootEntry& entry) {
                const auto path = entry.Path.View();
                EXPECT_NE(path, CookWriterLockName);
                EXPECT_FALSE(path.size() >= 4 && Core::Containers::StringView(path.data() + path.size() - 4, 4) == ".tmp");
                return VFS::VFSResult<void>::Ok();
            };
            ASSERT_TRUE(root.VisitRoot(1024, buffer, visit, nullptr).Succeeded());
            const auto objects = Relative("objects");
            ASSERT_TRUE(root.VisitDirectory(objects, 1024, buffer, visit, nullptr).Succeeded());
            ExpectScratchEmpty();
            CookLibraryMutationLease mutation = {};
            EXPECT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        }
    };

    TEST_P(SceneCookerResourceTest, ActualSceneCountCapPublishesOneSharedAssetAndEveryBinaryThenRejectsOverflowWithoutInputWork)
    {
        ASSERT_EQ(SceneCapacity, 256u);
        ASSERT_TRUE(Cook().Succeeded());
        const auto previous  = Result;
        const auto committed = Read(Result.ManifestPath);
        GenerateBatch(SceneCapacity);
        ASSERT_FALSE(HasFatalFailure());
        const auto calls = Allocator.Calls;
        --Context.Limits.MaxScenes;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Allocator.Calls, calls);
        ExpectPreserved(previous, committed);
        ++Context.Limits.MaxScenes;
        Request.Sources = {Batch.data(), Batch.size()};
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Allocator.Calls, calls);
        ExpectPreserved(previous, committed);
        Request.Sources = {Batch.data(), SceneCapacity};
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Result.SceneCount, SceneCapacity);
        EXPECT_EQ(Result.AssetCount, 1u);
        EXPECT_EQ(Result.ObjectCount, SceneCapacity + 1);
        VerifyPackage(Result, SceneCapacity);
        ASSERT_FALSE(HasFatalFailure());
        const auto full = Result;
        std::printf("Maximum scene batch (%s): %u scenes, %llu referenced bytes, %llu modeled working bytes\n", GetParam() ? "disk" : "memory", Result.SceneCount, static_cast<unsigned long long>(Result.ReferencedObjectBytes), static_cast<unsigned long long>(Result.PeakWorkingBytes));
        const auto full_manifest = Read(Result.ManifestPath);
        std::reverse(Batch.begin(), Batch.begin() + SceneCapacity);
        Context.Limits.MaxWorkingBytes *= 2;
        Request.ForceRebuild            = true;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, full.ManifestDigest);
        EXPECT_EQ(Result.ReferencedObjectBytes, full.ReferencedObjectBytes);
        EXPECT_EQ(Read(Result.ManifestPath), full_manifest);
        VerifyPackage(Result, SceneCapacity);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerResourceTest, AggregateCaptureAndStagingCapsAreExactPreserveOldPackagesAndRecoverInstalledOrphans)
    {
        constexpr uint32_t                               count     = 8;
        const VFS::VFSPath                               outputs[] = {Path("/output/reference"), Path("/output/short"), Path("/output/exact")};
        Core::Containers::FixedArray<SceneCookResult, 3> previous  = {};
        for (size_t index = 0; index < previous.size(); ++index)
        {
            ASSERT_TRUE(Files.CreateDir(outputs[index]).Succeeded());
            Request.OutputRoot = outputs[index];
            ASSERT_TRUE(Cook().Succeeded());
            previous[index] = Result;
        }
        const auto old_manifest = Read(Result.ManifestPath);
        // All new scene UUIDs, plus a changed material/new texture: every new
        // referenced object is absent from each root's old one-scene package.
        GenerateBatch(count, 1024);
        ASSERT_FALSE(HasFatalFailure());
        AddTexture();
        uint64_t captured = 0;
        for (uint32_t index = 0; index < count; ++index)
            captured += Read(Batch[index]).size();
        for (cstring path : {"/assets/material.zematerial", "/assets/material.zematerial.meta", "/assets/pixel.png", "/assets/pixel.png.meta"})
            captured += Read(Path(path)).size();
        Context.Limits.MaxCapturedBytes = captured;
        Request.OutputRoot              = outputs[0];
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_TRUE(Result.Published);
        const auto     reference = Result;
        const auto     canonical = Read(Result.ManifestPath);
        const uint64_t staged    = Result.ReferencedObjectBytes + canonical.size();
        VerifyPackage(Result, count);
        ASSERT_FALSE(HasFatalFailure());
        std::printf("Aggregate batch (%s): %llu captured bytes, %llu staged bytes, %llu modeled working bytes\n", GetParam() ? "disk" : "memory", static_cast<unsigned long long>(captured), static_cast<unsigned long long>(staged), static_cast<unsigned long long>(reference.PeakWorkingBytes));

        Request.OutputRoot = outputs[1];
        Result             = previous[1];
        --Context.Limits.MaxCapturedBytes;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_TRUE(Diagnostics.HasErrors());
        ExpectPreserved(previous[1], old_manifest);
        ++Context.Limits.MaxCapturedBytes;
        Context.Limits.MaxStagedBytes = staged - 1;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_TRUE(Diagnostics.HasErrors());
        ASSERT_GT(Diagnostics.Count, 0u);
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Stage, CookStage::Publish);
        ExpectPreserved(previous[1], old_manifest);

        // Separate cold root: the failed run's newly installed orphan objects
        // cannot accidentally make this exact-total staging test cheaper.
        Request.OutputRoot            = outputs[2];
        Result                        = previous[2];
        Context.Limits.MaxStagedBytes = staged;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, reference.ManifestDigest);
        EXPECT_EQ(Read(Result.ManifestPath), canonical);
        VerifyPackage(Result, count);
        ASSERT_FALSE(HasFatalFailure());

        // The failed root has all new immutable objects but still its old
        // manifest. Forced verification discards duplicate stages, so only the
        // new manifest needs retained staging capacity on recovery.
        Request.OutputRoot            = outputs[1];
        Request.ForceRebuild          = true;
        Result                        = previous[1];
        Context.Limits.MaxStagedBytes = canonical.size();
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_TRUE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, reference.ManifestDigest);
        EXPECT_EQ(Read(Result.ManifestPath), canonical);
        VerifyPackage(Result, count);
        ExpectScratchEmpty();
    }

    TEST_P(SceneCookerResourceTest, AggregateWorkingBudgetHasAReproducibleOneByteBoundaryWithoutChangingPackageBytes)
    {
        GenerateBatch(8);
        ASSERT_FALSE(HasFatalFailure());
        AddTexture();
        ASSERT_TRUE(Cook().Succeeded());
        const auto committed = Read(Result.ManifestPath);
        Request.ForceRebuild = true;
        // Warm the same forced, already-installed generation. The stream may
        // adapt to headroom, so a previous high-budget peak is NOT assumed to be
        // the minimum sufficient run budget. Find and verify the actual boundary.
        ASSERT_TRUE(Cook().Succeeded());
        ASSERT_TRUE(Cook().Succeeded());
        const auto previous = Result;
        uint64_t   low = 0, high = Context.Limits.MaxWorkingBytes;
        uint32_t   attempts = 0;
        while (high - low > 1)
        {
            const auto limit = low + (high - low) / 2;
            SCOPED_TRACE(limit);
            Context.Limits.MaxWorkingBytes = limit;
            Result                         = previous;
            const auto cooked              = Cook();
            ++attempts;
            if (cooked.Succeeded())
            {
                EXPECT_FALSE(Result.Published);
                EXPECT_EQ(Result.ManifestDigest, previous.ManifestDigest);
                EXPECT_LE(Result.PeakWorkingBytes, limit);
                EXPECT_EQ(Read(Result.ManifestPath), committed);
                high = limit;
            }
            else
            {
                ASSERT_EQ(cooked.Error(), VFS::VFSError::SizeLimitExceeded);
                ExpectPreserved(previous, committed);
                low = limit;
            }
            ASSERT_FALSE(HasFatalFailure());
        }
        ASSERT_GT(high, 1u);
        EXPECT_LE(attempts, 27u);
        Context.Limits.MaxWorkingBytes = high - 1;
        Result                         = previous;
        EXPECT_EQ(Cook().Error(), VFS::VFSError::SizeLimitExceeded);
        ExpectPreserved(previous, committed);
        Context.Limits.MaxWorkingBytes = high;
        ASSERT_TRUE(Cook().Succeeded());
        EXPECT_FALSE(Result.Published);
        EXPECT_EQ(Result.ManifestDigest, previous.ManifestDigest);
        EXPECT_LE(Result.PeakWorkingBytes, high);
        EXPECT_EQ(Read(Result.ManifestPath), committed);
        VerifyPackage(Result, 8);
        ExpectScratchEmpty();
        std::printf("Aggregate working boundary (%s): %llu accepted, %llu refused, %llu modeled peak bytes\n", GetParam() ? "disk" : "memory", static_cast<unsigned long long>(high), static_cast<unsigned long long>(high - 1), static_cast<unsigned long long>(Result.PeakWorkingBytes));
    }

    INSTANTIATE_TEST_SUITE_P(MemoryAndDisk, SceneCookerTest, testing::Bool());
    INSTANTIATE_TEST_SUITE_P(MemoryAndDisk, SceneCookerResourceTest, testing::Bool());
} // namespace ZEngine::Cook::Tests
