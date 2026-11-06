#include <ZEngine/Cook/CookAssetDiscovery.h>
#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookIncrementalReuse.h>
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookPublication.h>
#include <ZEngine/Cook/CookSceneCaptureSet.h>
#include <ZEngine/Cook/CookSceneCodec.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Cook/CookVFSWorkingMemory.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookSceneDocumentTest : public testing::Test
    {
    protected:
        struct CapturedSource final : ICookCapturedFile
        {
            Core::Containers::ArrayView<const uint8_t> Bytes        = {};
            Digest256                                  SourceDigest = {};
            size_t                                     MaxRead      = SIZE_MAX;
            VFS::VFSError                              Error        = VFS::VFSError::OK;
            bool                                       EarlyEOF     = false;
            bool                                       Overreported = false;
            mutable size_t                             Reads        = 0;

            uint64_t                                   Size() const noexcept override
            {
                return Bytes.size();
            }
            Digest256 Digest() const noexcept override
            {
                return SourceDigest;
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override
            {
                ++Reads;
                if (Error != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<size_t>::Fail(Error);
                }
                if (Overreported)
                {
                    return VFS::VFSResult<size_t>::Ok(output.size() + 1);
                }
                if (EarlyEOF)
                {
                    return VFS::VFSResult<size_t>::Ok(0);
                }
                if (offset > Bytes.size())
                {
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::InvalidPath);
                }
                const auto count = std::min({output.size(), Bytes.size() - static_cast<size_t>(offset), MaxRead});
                if (count)
                {
                    Helpers::secure_memcpy(output.data(), output.size(), Bytes.data() + static_cast<size_t>(offset), count);
                }
                return VFS::VFSResult<size_t>::Ok(count);
            }
        };

        struct SceneSink final : ICookByteSink
        {
            Core::Containers::FixedArray<uint8_t, ZKilo(64)> Bytes = {};
            size_t                                           Count = 0;
            size_t                                           Calls = 0;
            VFS::VFSError                                    Error = VFS::VFSError::OK;
            VFS::VFSResult<void>                             Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                ++Calls;
                if (Error != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<void>::Fail(Error);
                }
                if (bytes.size() > Bytes.size() - Count)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                Helpers::secure_memcpy(Bytes.data() + Count, Bytes.size() - Count, bytes.data(), bytes.size());
                Count += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct CountingAllocator final : ICookAllocator
        {
            struct Block
            {
                void*  Pointer = nullptr;
                size_t Bytes   = 0;
                size_t Call    = 0;
            };
            ICookAllocator*                         Parent               = nullptr;
            Core::Containers::FixedArray<Block, 64> Blocks               = {};
            size_t                                  Calls                = 0;
            size_t                                  FailAt               = 0;
            size_t                                  LastBytes            = 0;
            VFS::VFSError                           Failure              = VFS::VFSError::SizeLimitExceeded;
            size_t                                  DecodedBytes         = 0;
            size_t                                  MigrationAllocations = 0;
            size_t                                  LiveMigrationBlocks  = 0;
            size_t                                  PeakMigrationBlocks  = 0;
            size_t                                  FixedCalls           = 0;
            uint64_t                                FixedBytes           = 0;
            VFS::VFSError                           FixedFailure         = VFS::VFSError::OK;

            VFS::VFSResult<void>                    TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++FixedCalls;
                FixedBytes = bytes;
                return FixedFailure == VFS::VFSError::OK ? Parent->TryCharge(bytes, category, output) : VFS::VFSResult<void>::Fail(FixedFailure);
            }

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                LastBytes = bytes;
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void*>::Fail(Failure);
                }
                const auto result = Parent->Allocate(bytes, alignment, category);
                if (result.Failed())
                {
                    return result;
                }
                for (auto& block : Blocks)
                {
                    if (block.Pointer)
                    {
                        continue;
                    }
                    block = {result.Value(), bytes, Calls};
                    if (bytes == DecodedBytes)
                    {
                        ++MigrationAllocations;
                        PeakMigrationBlocks = std::max(PeakMigrationBlocks, ++LiveMigrationBlocks);
                    }
                    return result;
                }
                Parent->Free(result.Value());
                ADD_FAILURE() << "Test allocation tracking slots exhausted.";
                return VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory);
            }
            void Free(void* pointer) noexcept override
            {
                if (!pointer)
                {
                    return;
                }
                for (auto& block : Blocks)
                {
                    if (block.Pointer != pointer)
                    {
                        continue;
                    }
                    if (block.Bytes == DecodedBytes)
                    {
                        --LiveMigrationBlocks;
                    }
                    block = {};
                    Parent->Free(pointer);
                    return;
                }
                ADD_FAILURE() << "Foreign/duplicate scene allocation release.";
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
            VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                return Parent->MemoryRequirements();
            }
        };

        Core::Memory::MemoryManager        Memory      = {};
        ECS::SceneComponentSchemaRegistry  BuiltIns    = {};
        ECS::SceneComponentSchemaRegistry  Schemas     = {};
        ECS::SceneMigrationRegistry        Migrations  = {};
        ECS::SceneComponentCodecFns        NameCodecs  = {};
        ECS::Scene                         SourceScene = {};
        ECS::SceneDocument                 Original    = {};
        std::string                        Source      = {};
        std::string                        Name        = std::string(ECS::SceneDocument::MaxNameBytes, 'n');
        ECS::SceneDocumentLimits           Limits      = {.MaxDecodedBytes = ZKilo(128)};
        ECS::SceneDiagnostics              Diagnostics = {};
        Tests::FixedAllocator<ZMega(1), 1> Parent      = {};
        CookScratchAllocator               Scratch     = {};
        CookBudgetAllocator                Budget      = {};
        CountingAllocator                  Allocator   = {};
        CookCodecAllocationRoute           Route{Allocator};
        ECS::SceneDocument                 Document  = {};
        uuids::uuid                        SceneUUID = uuids::uuid::from_string("10000000-0000-0000-0000-000000000001").value();

        void                               SetUp() override
        {
            ASSERT_TRUE(Scratch.Initialize(Parent, ZMega(1), CookMemoryCategory::Codecs).Succeeded());
            const auto backing = Scratch.MemoryRequirements();
            ASSERT_TRUE(backing.Succeeded());
            // Keep the same 1 MiB slab; parser fixed state is outside its pool.
            ASSERT_TRUE(Budget.Initialize(Scratch, ZMega(2)).Succeeded());
            Allocator.Parent       = &Budget;
            Allocator.DecodedBytes = ECS::SceneYAML::Storage::InitialRoutedCapacity(Limits.MaxDecodedBytes);
            Memory.Initialize(ZMega(128), {});
            BuiltIns.Initialize(&Memory.MainArena);
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(BuiltIns));
            const auto* schema = BuiltIns.FindByKey("NameComponent");
            ASSERT_NE(schema, nullptr);
            NameCodecs        = schema->Codecs;
            auto codecs       = NameCodecs;
            codecs.Context    = &NameCodecs;
            codecs.DecodeYAML = [](void* context, uint32_t version, const ECS::SceneYAML::Node& value, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics) {
                const auto& original = *static_cast<const ECS::SceneComponentCodecFns*>(context);
                return version == 3 && original.DecodeYAML(original.Context, 1, value, output, diagnostics);
            };
            codecs.DecodeBinary = [](void* context, uint32_t version, ECS::SceneBinaryReader* value, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics) {
                const auto& original = *static_cast<const ECS::SceneComponentCodecFns*>(context);
                return version == 3 && original.DecodeBinary(original.Context, 1, value, output, diagnostics);
            };
            ASSERT_TRUE(Schemas.Register({.Key = schema->Key, .Version = 3, .RuntimeType = schema->RuntimeType, .Fields = schema->Fields, .FieldCount = schema->FieldCount, .Codecs = codecs, .References = schema->References}, &Diagnostics));
            ASSERT_TRUE(Migrations.Initialize(&Memory.MainArena));
            SourceScene.Initialize(&Memory.MainArena);
            const auto entity = SourceScene.CreateEntity();
            SourceScene.AddComponent<ECS::Components::UUIDComponent>(entity, {uuids::uuid::from_string("20000000-0000-0000-0000-000000000002").value()});
            ECS::Components::NameComponent component = {};
            ASSERT_EQ(Helpers::secure_strncpy(component.Value, sizeof(component.Value), "Current", 7), Helpers::MEMORY_OP_SUCCESS);
            SourceScene.AddComponent<ECS::Components::NameComponent>(entity, component);
            ASSERT_TRUE(ECS::SceneDocument::Capture(SourceScene, {.UUID = SceneUUID, .Name = Name}, Schemas, &Original, &Diagnostics, Limits));
            ASSERT_TRUE(Original.Emit(&Source, &Diagnostics));
        }

        void TearDown() override
        {
            Document = {};
            EXPECT_EQ(Allocator.LiveMigrationBlocks, 0u);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
            Budget.Release();
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            SourceScene.Shutdown();
        }

        bool Parse(ECS::SceneDocument* output = nullptr, Core::Memory::IAllocationRoute* route = nullptr, const ECS::SceneMigrationRegistry* migrations = nullptr)
        {
            return ECS::SceneDocument::Parse(Source.data(), Source.size(), Schemas, output ? output : &Document, &Diagnostics, Limits, migrations, route ? route : &Route);
        }

        CapturedSource CaptureSource()
        {
            CapturedSource capture  = {};
            capture.Bytes           = {reinterpret_cast<const uint8_t*>(Source.data()), Source.size()};
            Core::Hash::SHA256 hash = {};
            EXPECT_TRUE(hash.Update(capture.Bytes).Succeeded());
            EXPECT_TRUE(hash.Finalize(&capture.SourceDigest).Succeeded());
            return capture;
        }

        CookSceneInput SceneInput(const ICookCapturedFile& capture, const CookLimits& limits)
        {
            return {&capture, VFS::VFSRelativePath::Parse("scenes/main.scene").Value(), &limits};
        }

        void Legacy(bool document_upgrade = true)
        {
            ECS::SceneYAML::Storage storage = {};
            ASSERT_TRUE(storage.Initialize(ZMega(1)));
            auto root = storage.Destination();
            ASSERT_TRUE(ECS::ReadSceneYAML(Source, Limits, &root, &Diagnostics));
            if (document_upgrade)
            {
                ASSERT_TRUE(storage.Set(root["scene"], "schema_version", storage.UInt(1)));
            }
            ASSERT_TRUE(storage.Set(root["scene"]["entities"][size_t(0)]["components"]["NameComponent"], "schema_version", storage.UInt(1)));
            ECS::SceneYAML::Emitter emitter(Limits.MaxFileBytes);
            emitter << root;
            ASSERT_TRUE(emitter.Output(&Source));
        }

        static bool Upgrade(void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* output, ECS::SceneDiagnostics*)
        {
            auto& storage = *output->Owner();
            auto  result  = storage.Clone(source);
            if (result["scene"].IsDefined() && !storage.Set(result["scene"], "schema_version", storage.UInt(2)))
            {
                return false;
            }
            *output = result;
            return result.IsDefined();
        }

        void RegisterMigrations()
        {
            ASSERT_TRUE(Migrations.RegisterDocumentMigration(1, 2, nullptr, Upgrade, &Diagnostics));
            ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, nullptr, Upgrade, &Diagnostics));
            ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 2, 3, nullptr, Upgrade, &Diagnostics));
        }
    };

    class CookSceneCaptureSetTest : public CookSceneDocumentTest
    {
    protected:
        VFS::VFSMemoryBackend                Backend     = {};
        VFS::VFSContext                      Context     = {};
        CookFrozenLibraryOwner               Owner       = {};
        VFS::VFSScratchRootLease             ScratchRoot = {};
        std::optional<CookFrozenInputSource> Provider    = {};
        CookInputLease                       Lease       = {};
        CookSceneCodec                       Codec{Allocator, Schemas};
        CookSceneCaptureSet                  Captures   = {};
        CookDependencyGraph                  Graph      = {};
        CookLimits                           RunLimits  = {.MaxWorkingBytes = ZMega(1)};
        CookDiagnostics                      CookErrors = {};
        VFS::VFSPath                         Paths[2]   = {};

        static VFS::VFSPath                  Absolute(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }
        static VFS::VFSRelativePath Relative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }
        void Write(const VFS::VFSPath& path, const std::string& bytes)
        {
            ASSERT_TRUE(Backend.WriteFile(path, {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}).Succeeded());
        }
        void SetUp() override
        {
            CookSceneDocumentTest::SetUp();
            RunLimits.SceneLimits = Limits;
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(2), 64).Succeeded());
            ASSERT_TRUE(Backend.CreateDir(Absolute("/scenes")).Succeeded());
            ASSERT_TRUE(Backend.CreateDir(Absolute("/scratch")).Succeeded());
            Paths[0] = Absolute("/scenes/main.scene");
            Paths[1] = Absolute("/scenes/other.scene");
            Write(Paths[0], Source);
            auto       second = Source;
            const auto at     = second.find("10000000-0000-0000-0000-000000000001");
            ASSERT_NE(at, std::string::npos);
            second.replace(at, 36, "40000000-0000-0000-0000-000000000004");
            Write(Paths[1], second);
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            const CookFrozenRootRequest roots[] = {
                {.Key = "scenes", .Context = &Context, .Path = Absolute("/scenes")}
            };
            ASSERT_TRUE(Owner.Initialize(Allocator, roots).Succeeded());
            ASSERT_TRUE(Context.AcquireScratchRoot(Absolute("/scratch"), &ScratchRoot).Succeeded());
            ASSERT_TRUE(ScratchRoot.CreateDirExclusive(Relative("run")).Succeeded());
            Provider.emplace(Owner);
            ASSERT_TRUE(Provider->Acquire({&ScratchRoot, Relative("run"), &Allocator, &RunLimits}, &Lease, &CookErrors).Succeeded());
            ASSERT_TRUE(Graph.Initialize(Allocator, RunLimits).Succeeded());
        }
        void TearDown() override
        {
            EXPECT_TRUE(Captures.Reset().Succeeded());
            Graph.Reset();
            Lease.Reset();
            Provider.reset();
            EXPECT_TRUE(ScratchRoot.Remove(Relative("run")).Succeeded());
            ScratchRoot.Reset();
            EXPECT_TRUE(Owner.Reset().Succeeded());
            Context.Shutdown();
            CookSceneDocumentTest::TearDown();
        }
        VFS::VFSResult<void> Capture(Core::Containers::ArrayView<const VFS::VFSPath> sources)
        {
            return Captures.Capture(Allocator, sources, Lease, Codec, Graph, RunLimits, &CookErrors);
        }
    };

    TEST_F(CookSceneCaptureSetTest, CompleteRequestedSetRetainsStableCapturesButNoParsedDocuments)
    {
        std::swap(Paths[0], Paths[1]); // Sorting paths must not change graph indexes.
        ASSERT_TRUE(Capture(Paths).Succeeded());
        EXPECT_EQ(Captures.Count(), 2u);
        EXPECT_EQ(Graph.SceneCount(), 2u);
        const auto first  = Captures.Input(0).Value();
        const auto second = Captures.Input(1).Value();
        EXPECT_EQ(first.Source, Relative("scenes/other.scene"));
        EXPECT_EQ(second.Source, Relative("scenes/main.scene"));
        EXPECT_EQ(Graph.Scene(0)->Source, first.Source);
        EXPECT_EQ(Graph.Scene(1)->Source, second.Source);
        const auto live  = Budget.Statistics().LiveBytes;
        const auto calls = Allocator.Calls;
        EXPECT_EQ(Capture(Paths).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Captures.Input(0).Value().Capture, first.Capture);
        EXPECT_EQ(Captures.Input(2).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Budget.Statistics().LiveBytes, live);
        ASSERT_TRUE(Lease.Seal(&CookErrors).Succeeded());
        Lease.Reset();
        Write(Paths[0], "replacement");
        Write(Paths[1], "replacement");
        Context.Shutdown();
        ECS::SceneDocument document = {};
        ASSERT_TRUE(Codec.Read(first, &document, nullptr).Succeeded());
        EXPECT_EQ(document.Settings()->UUID, Graph.Scene(0)->UUID);
        document = {};
        ASSERT_TRUE(Codec.Read(second, &document, nullptr).Succeeded());
        EXPECT_EQ(document.Settings()->UUID, SceneUUID);
        document          = {};
        const auto before = Budget.OutstandingAllocations();
        ASSERT_TRUE(Captures.Reset().Succeeded());
        // Exactly one owner/index allocation. The real frozen provider uses
        // rooted scratch captures, not per-scene heap storage or parsed trees.
        EXPECT_EQ(Budget.OutstandingAllocations() + 1, before);
        EXPECT_EQ(Captures.Count(), 0u);
        EXPECT_EQ(Captures.Input(0).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookSceneCaptureSetTest, InputIntoReusesCallerStorageWithoutAllocationAndOutlivesInputExclusion)
    {
        ASSERT_TRUE(Capture(Paths).Succeeded());
        const auto     calls = Allocator.Calls;
        const auto     live  = Budget.Statistics().LiveBytes;
        CookSceneInput input = {};
        for (uint32_t i : {0u, 1u, 0u})
        {
            ASSERT_TRUE(Captures.InputInto(i, &input).Succeeded());
            const auto expected = Captures.Input(i);
            ASSERT_TRUE(expected.Succeeded());
            EXPECT_EQ(input.Capture, expected.Value().Capture);
            EXPECT_EQ(input.Source, expected.Value().Source);
            EXPECT_EQ(input.Limits, expected.Value().Limits);
        }
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Budget.Statistics().LiveBytes, live);
        ASSERT_TRUE(Lease.Seal(&CookErrors).Succeeded());
        Lease.Reset();
        ASSERT_TRUE(Captures.InputInto(1, &input).Succeeded());
        ECS::SceneDocument document = {};
        ASSERT_TRUE(Codec.Read(input, &document, nullptr).Succeeded());
        EXPECT_EQ(document.Settings()->UUID, Graph.Scene(1)->UUID);
    }

    TEST_F(CookSceneCaptureSetTest, InvalidInputIntoPreservesEveryOutputFieldBeforeCaptureAndAfterReset)
    {
        CookLimits     limits           = {};
        CookSceneInput output           = {.Source = Relative("unchanged.scene"), .Limits = &limits};
        const auto     unchanged        = output;
        const auto     expect_unchanged = [&]() {
            EXPECT_EQ(output.Capture, unchanged.Capture);
            EXPECT_EQ(output.Source, unchanged.Source);
            EXPECT_EQ(output.Limits, unchanged.Limits);
        };
        EXPECT_EQ(Captures.InputInto(0, &output).Error(), VFS::VFSError::InvalidPath);
        expect_unchanged();
        ASSERT_TRUE(Capture(Paths).Succeeded());
        const auto calls = Allocator.Calls;
        const auto live  = Budget.Statistics().LiveBytes;
        EXPECT_EQ(Captures.InputInto(0, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Captures.InputInto(Captures.Count(), &output).Error(), VFS::VFSError::InvalidPath);
        expect_unchanged();
        EXPECT_EQ(Captures.InputInto(UINT32_MAX, &output).Error(), VFS::VFSError::InvalidPath);
        expect_unchanged();
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Budget.Statistics().LiveBytes, live);
        ASSERT_TRUE(Captures.Reset().Succeeded());
        EXPECT_EQ(Captures.InputInto(0, &output).Error(), VFS::VFSError::InvalidPath);
        expect_unchanged();
    }

    TEST_F(CookSceneCaptureSetTest, EmptyInvalidAndOverCapRequestsFailBeforeAnyAllocationOrGraphMutation)
    {
        const auto calls = Allocator.Calls;
        EXPECT_EQ(Capture({}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Capture({nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        RunLimits.MaxScenes = 1;
        EXPECT_EQ(Capture(Paths).Error(), VFS::VFSError::SizeLimitExceeded);
        RunLimits.MaxScenes = 2;
        for (const auto invalid : {VFS::VFSPath{}, VFS::VFSPath::Root()})
        {
            EXPECT_EQ(Capture({&invalid, 1}).Error(), VFS::VFSError::InvalidPath);
        }
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Graph.SceneCount(), 0u);
        EXPECT_EQ(Captures.Count(), 0u);
    }

    TEST_F(CookSceneCaptureSetTest, DuplicateNormalizedPathsAreRejectedBeforeTheFirstSceneCapture)
    {
        Paths[1]          = Absolute("/scenes/./main.scene");
        const auto before = Budget.OutstandingAllocations();
        EXPECT_EQ(Capture(Paths).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Graph.SceneCount(), 0u);
        EXPECT_EQ(Captures.Count(), 0u);
        EXPECT_EQ(Budget.OutstandingAllocations(), before + 1); // Table only, no captures/documents.
        ASSERT_GT(CookErrors.Count, 0u);
        EXPECT_EQ(CookErrors.Entries[CookErrors.Count - 1].Code, CookDiagnosticCode::IdentityConflict);
        EXPECT_EQ(CookErrors.Entries[CookErrors.Count - 1].Source, Relative("scenes/main.scene"));
        EXPECT_EQ(Captures.Input(0).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookSceneCaptureSetTest, AllPathsMustMapToDeclaredRootsBeforeAnySceneCapture)
    {
        Paths[1]          = Absolute("/unknown/missing.scene");
        const auto before = Budget.OutstandingAllocations();
        EXPECT_EQ(Capture(Paths).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Graph.SceneCount(), 0u);
        EXPECT_EQ(Budget.OutstandingAllocations(), before + 1);
        EXPECT_EQ(Captures.Count(), 0u);
    }

    TEST_F(CookSceneCaptureSetTest, DifferentFilesWithTheSameSceneUUIDFailWithoutExposingPartialResults)
    {
        Lease.Reset();
        Provider.reset();
        Write(Paths[1], Source);
        Provider.emplace(Owner);
        ASSERT_TRUE(Provider->Acquire({&ScratchRoot, Relative("run"), &Allocator, &RunLimits}, &Lease, &CookErrors).Succeeded());
        EXPECT_EQ(Capture(Paths).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Graph.SceneCount(), 1u); // Partial graph is explicitly discard-only.
        EXPECT_EQ(Captures.Count(), 0u);
        EXPECT_EQ(Captures.Input(0).Error(), VFS::VFSError::InvalidPath);
        ASSERT_GT(CookErrors.Count, 0u);
        EXPECT_EQ(CookErrors.Entries[CookErrors.Count - 1].Code, CookDiagnosticCode::IdentityConflict);
        EXPECT_EQ(CookErrors.Entries[CookErrors.Count - 1].SceneUUID, SceneUUID);
    }

    TEST_F(CookSceneCaptureSetTest, SealedInputsAreRejectedBeforeAllocatingTheOwnerTable)
    {
        ASSERT_TRUE(Lease.Seal(&CookErrors).Succeeded());
        const auto calls = Allocator.Calls;
        EXPECT_EQ(Capture(Paths).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Graph.SceneCount(), 0u);
    }

    TEST_F(CookSceneCaptureSetTest, SortReservationRefusalPrecedesOwnerAllocationAndRetainsRetryableInputs)
    {
        const auto calls    = Allocator.Calls;
        const auto retained = Budget.Statistics().LiveBytes;
        const auto blocks   = Budget.OutstandingAllocations();
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            Allocator.FixedFailure = error;
            const auto fixedCalls  = Allocator.FixedCalls;
            EXPECT_EQ(Capture(Paths).Error(), error);
            EXPECT_EQ(Allocator.FixedCalls, fixedCalls + 1);
            EXPECT_EQ(Allocator.FixedBytes, sizeof(Detail::CookSortState<uint32_t>) + sizeof(CookMemoryCharge));
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Budget.Statistics().LiveBytes, retained);
            EXPECT_EQ(Budget.OutstandingAllocations(), blocks);
            EXPECT_EQ(Captures.Count(), 0u);
            EXPECT_EQ(Graph.SceneCount(), 0u);
            EXPECT_EQ(Lease.State(), CookInputState::Active);
        }
        Allocator.FixedFailure = VFS::VFSError::OK;
        ASSERT_TRUE(Capture(Paths).Succeeded());
        EXPECT_EQ(Captures.Count(), 2u);
        EXPECT_EQ(Graph.SceneCount(), 2u);
    }

    TEST_F(CookSceneCaptureSetTest, OwnerTableAllocationRefusalDoesNotCaptureOrChangeTheGraph)
    {
        const auto before = Budget.OutstandingAllocations();
        for (const auto failure : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::OutOfMemory})
        {
            Allocator.FailAt  = Allocator.Calls + 1;
            Allocator.Failure = failure;
            EXPECT_EQ(Capture(Paths).Error(), failure);
            EXPECT_EQ(Captures.Count(), 0u);
            EXPECT_EQ(Graph.SceneCount(), 0u);
            EXPECT_EQ(Budget.OutstandingAllocations(), before);
            EXPECT_EQ(CookErrors.Entries[CookErrors.Count - 1].Code, failure == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : CookDiagnosticCode::AllocationFailed);
        }
    }

    TEST_F(CookSceneCaptureSetTest, EverySceneSetAllocationRefusalReleasesCapturesAndDiscardsPartialResults)
    {
        Allocator.Calls = 0;
        ASSERT_TRUE(Capture(Paths).Succeeded());
        const auto calls = Allocator.Calls;
        ASSERT_GT(calls, 1u);
        ASSERT_TRUE(Captures.Reset().Succeeded());
        Graph.Reset();
        for (size_t failure = 1; failure <= calls; ++failure)
        {
            SCOPED_TRACE(failure);
            Lease.Reset();
            Provider.reset();
            Provider.emplace(Owner);
            ASSERT_TRUE(Provider->Acquire({&ScratchRoot, Relative("run"), &Allocator, &RunLimits}, &Lease, &CookErrors).Succeeded());
            ASSERT_TRUE(Graph.Initialize(Allocator, RunLimits).Succeeded());
            const auto     before = Budget.OutstandingAllocations();
            CookSceneCodec codec(Allocator, Schemas);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            EXPECT_EQ(Captures.Capture(Allocator, Paths, Lease, codec, Graph, RunLimits, &CookErrors).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Captures.Count(), 0u);
            EXPECT_EQ(Captures.Input(0).Error(), VFS::VFSError::InvalidPath);
            ASSERT_TRUE(Captures.Reset().Succeeded());
            Graph.Reset();
            EXPECT_EQ(Budget.OutstandingAllocations(), before);
            Allocator.FailAt = 0;
        }
    }

    TEST_F(CookSceneDocumentTest, NameAndRecordsShareOneOwnedBlockAndSurviveInputAndSchemaTeardown)
    {
        ASSERT_TRUE(Parse());
        ASSERT_NE(Document.Settings(), nullptr);
        EXPECT_EQ(Document.Settings()->Name, Name);
        EXPECT_EQ(Document.Settings()->UUID, SceneUUID);
        EXPECT_EQ(Document.EntityCount(), 1u);
        EXPECT_FALSE(Document.ReadOnly());
        EXPECT_EQ(Budget.OutstandingAllocations(), 1u);
        const auto name_pointer = reinterpret_cast<uintptr_t>(Document.Settings()->Name.data());
        const auto backing      = reinterpret_cast<uintptr_t>(Parent.Bytes);
        EXPECT_GE(name_pointer, backing);
        EXPECT_LT(name_pointer, backing + sizeof(Parent.Bytes));
        EXPECT_EQ(Document.Settings()->Name.data()[Name.size()], '\0');
        std::fill(Source.begin(), Source.end(), '?');
        Original = {};
        SourceScene.Shutdown();
        Schemas = {};
        Memory.Shutdown();
        EXPECT_EQ(Document.Settings()->Name, Name);
        EXPECT_EQ(Document.EntityCount(), 1u);
        Document = {};
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
    }

    TEST_F(CookSceneDocumentTest, EverySceneAllocationFailurePreservesPreviousDocumentAndReleasesCandidates)
    {
        ASSERT_TRUE(Parse());
        const auto count = Allocator.Calls;
        ASSERT_GT(count, 1u);
        Document = {};
        for (size_t failure = 1; failure <= count; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute route(Allocator);
            const auto*              retained = Original.Settings();
            EXPECT_FALSE(Parse(&Original, &route));
            EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Original.Settings(), retained);
            EXPECT_EQ(Original.Settings()->Name, Name);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
        Allocator.FailAt = 0;
        EXPECT_TRUE(Parse());
    }

    TEST_F(CookSceneDocumentTest, MovesAndReplacementReleaseEachOriginalAllocatorExactlyOnce)
    {
        ASSERT_TRUE(Parse());
        CountingAllocator other = {};
        other.Parent            = &Budget;
        CookCodecAllocationRoute route(other);
        ECS::SceneDocument       replacement = {};
        ASSERT_TRUE(Parse(&replacement, &route));
        EXPECT_EQ(Budget.OutstandingAllocations(), 2u);
        const auto* retained = Document.Settings();
        replacement          = std::move(Document);
        EXPECT_EQ(Document.Settings(), nullptr);
        EXPECT_EQ(replacement.Settings(), retained);
        EXPECT_EQ(Budget.OutstandingAllocations(), 1u);
        for (const auto& block : other.Blocks)
        {
            EXPECT_EQ(block.Pointer, nullptr);
        }
        Document   = std::move(replacement);
        auto* self = &Document;
        Document   = std::move(*self); // Deliberate self-move must preserve ownership.
        EXPECT_EQ(Document.Settings(), retained);
    }

    TEST_F(CookSceneDocumentTest, BinaryRoundTripUsesOneDecodeAllocationAndNameIsIndependentOfInputBytes)
    {
        ASSERT_TRUE(Parse());
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Budget, ZKilo(16), CookMemoryCategory::Codecs).Succeeded());
        Core::Containers::Array<uint8_t> bytes = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, workspace.Arena(), &bytes, &Diagnostics, Limits));
        const auto         before  = Allocator.Calls;
        ECS::SceneDocument decoded = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Decode({bytes.data(), bytes.size()}, Schemas, &decoded, &Diagnostics, Limits, &Route));
        EXPECT_EQ(Allocator.Calls, before + 1);
        EXPECT_EQ(decoded.Settings()->Name, Name);
        EXPECT_EQ(decoded.EntityCount(), Document.EntityCount());
        Core::Containers::Array<uint8_t> repeated = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(decoded, Schemas, workspace.Arena(), &repeated, &Diagnostics, Limits));
        ASSERT_EQ(bytes.size(), repeated.size());
        EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), repeated.begin()));
        std::fill(bytes.begin(), bytes.end(), uint8_t(0));
        EXPECT_EQ(decoded.Settings()->Name, Name);
    }

    TEST_F(CookSceneDocumentTest, DocumentAndComponentMigrationsUseTwoScratchOwnersAndReleaseThemBeforeReturn)
    {
        Legacy();
        RegisterMigrations();
        ASSERT_TRUE(Parse(nullptr, nullptr, &Migrations));
        EXPECT_EQ(Allocator.MigrationAllocations, 4u); // Two per document/component chain, never per step.
        EXPECT_EQ(Allocator.PeakMigrationBlocks, 2u);
        EXPECT_EQ(Allocator.LiveMigrationBlocks, 0u);
        // Source + upgraded values need one extra stable document block, not
        // either migration scratch owner (both are already released).
        EXPECT_EQ(Budget.OutstandingAllocations(), 2u);
        EXPECT_FALSE(Document.ReadOnly());
        EXPECT_EQ(Document.Settings()->Name, Name);
        std::string current = {};
        ASSERT_TRUE(Document.Emit(&current, &Diagnostics));
        EXPECT_EQ(current.find("schema_version\": 1"), std::string::npos);
    }

    TEST_F(CookSceneDocumentTest, EveryMigratingParseAllocationFailurePreservesPreviousDocumentAndReleasesScratch)
    {
        Legacy();
        RegisterMigrations();
        ASSERT_TRUE(Parse(nullptr, nullptr, &Migrations));
        const auto count = Allocator.Calls;
        Document         = {};
        for (size_t failure = 1; failure <= count; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute route(Allocator);
            const auto*              retained = Original.Settings();
            EXPECT_FALSE(Parse(&Original, &route, &Migrations));
            EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Original.Settings(), retained);
            EXPECT_EQ(Allocator.LiveMigrationBlocks, 0u);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, DifferentSufficientDocumentBudgetsProduceIdenticalBinaryBytes)
    {
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Budget, ZKilo(16), CookMemoryCategory::Codecs).Succeeded());
        Core::Containers::Array<uint8_t> first = {};
        ASSERT_TRUE(Parse());
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, workspace.Arena(), &first, &Diagnostics, Limits));
        Document                = {};
        Limits.MaxDecodedBytes *= 2;
        ASSERT_TRUE(Parse());
        Core::Containers::Array<uint8_t> second = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, workspace.Arena(), &second, &Diagnostics, Limits));
        ASSERT_EQ(first.size(), second.size());
        EXPECT_TRUE(std::equal(first.begin(), first.end(), second.begin()));
    }

    TEST_F(CookSceneDocumentTest, DefaultSceneCapDoesNotReserveTheEntireDecodedBudget)
    {
        CookPageAllocator   native = {};
        CookBudgetAllocator budget = {};
        ASSERT_TRUE(native.Initialize(ZMega(1)).Succeeded());
        ASSERT_TRUE(budget.Initialize(native, ZMega(1)).Succeeded());
        CookCodecAllocationRoute route(budget);
        ECS::SceneDocument       document = {};
        Limits                            = {};
        ASSERT_GT(Limits.MaxDecodedBytes, ZMega(1));
        ASSERT_TRUE(Parse(&document, &route));
        EXPECT_EQ(document.Settings()->Name, Name);
        EXPECT_EQ(document.EntityCount(), 1u);
        EXPECT_EQ(budget.OutstandingAllocations(), 1u);
        // Includes actual native page rounding for every parser/tree block,
        // not the synthetic requested-byte fixture or its pre-owned 1 MiB pool.
        EXPECT_LT(budget.Statistics().PeakBytes, ZMega(1));
        EXPECT_LT(native.Statistics().PeakBytes, ZMega(1));
    }

    TEST_F(CookSceneDocumentTest, MultiBlockDocumentsKeepNamesAndBinaryValuesStableAndFailTransactionally)
    {
        Limits = {};
        for (uint32_t index = 0; index < 200; ++index)
        {
            char uuid[37] = {};
            std::snprintf(uuid, sizeof(uuid), "20000000-0000-0000-0000-%012u", index + 100);
            const auto entity = SourceScene.CreateEntity();
            SourceScene.AddComponent<ECS::Components::UUIDComponent>(entity, {uuids::uuid::from_string(uuid).value()});
            ECS::Components::NameComponent name = {};
            ASSERT_EQ(Helpers::secure_strncpy(name.Value, sizeof(name.Value), "Multiblock", 10), Helpers::MEMORY_OP_SUCCESS);
            SourceScene.AddComponent<ECS::Components::NameComponent>(entity, name);
        }
        ASSERT_TRUE(ECS::SceneDocument::Capture(SourceScene, {.UUID = SceneUUID, .Name = Name}, Schemas, &Original, &Diagnostics, Limits));
        ASSERT_TRUE(Original.Emit(&Source, &Diagnostics));
        ASSERT_TRUE(Parse());
        ASSERT_GT(Budget.OutstandingAllocations(), 1u);
        Core::Containers::FixedArray<size_t, 64> tree_allocations = {};
        size_t                                   tree_count       = 0;
        for (const auto& block : Allocator.Blocks)
        {
            if (block.Pointer)
            {
                tree_allocations[tree_count++] = block.Call;
            }
        }
        EXPECT_EQ(Document.EntityCount(), 201u);
        EXPECT_EQ(Document.Settings()->Name, Name);
        std::string emitted = {};
        ASSERT_TRUE(Document.Emit(&emitted, &Diagnostics));
        EXPECT_EQ(emitted, Source);
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Budget, ZKilo(128), CookMemoryCategory::Codecs).Succeeded());
        Core::Containers::Array<uint8_t> bytes = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, workspace.Arena(), &bytes, &Diagnostics, Limits));
        Document                        = {};
        ECS::SceneDocument decoded      = {};
        const auto         decode_start = Allocator.Calls;
        ASSERT_TRUE(ECS::BinarySceneCodec::Decode({bytes.data(), bytes.size()}, Schemas, &decoded, &Diagnostics, Limits, &Route));
        const auto decode_allocations = Allocator.Calls - decode_start;
        EXPECT_EQ(decoded.EntityCount(), 201u);
        EXPECT_EQ(decoded.Settings()->Name, Name);
        Core::Containers::Array<uint8_t> repeated = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(decoded, Schemas, workspace.Arena(), &repeated, &Diagnostics, Limits));
        ASSERT_EQ(bytes.size(), repeated.size());
        EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), repeated.begin()));
        decoded = {};
        for (size_t failure = 1; failure <= decode_allocations; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute route(Allocator);
            const auto*              retained = Original.Settings();
            EXPECT_FALSE(ECS::BinarySceneCodec::Decode({bytes.data(), bytes.size()}, Schemas, &Original, &Diagnostics, Limits, &route));
            EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Original.Settings(), retained);
            EXPECT_EQ(Budget.OutstandingAllocations(), 1u); // Encoded-byte workspace only.
        }
        workspace.Release();
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        for (size_t index = 0; index < tree_count; ++index)
        {
            const auto failure = tree_allocations[index];
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute route(Allocator);
            const auto*              retained = Original.Settings();
            EXPECT_FALSE(Parse(&Original, &route));
            EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Original.Settings(), retained);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, MaximumLengthMigrationChainReusesExactlyTwoBackingBlocks)
    {
        Legacy(false);
        ECS::SceneComponentSchemaRegistry current = {};
        current.Initialize(&Memory.MainArena);
        const auto* schema = BuiltIns.FindByKey("NameComponent");
        auto        codecs = NameCodecs;
        codecs.Context     = &NameCodecs;
        codecs.DecodeYAML  = [](void* context, uint32_t version, const ECS::SceneYAML::Node& value, ECS::SceneValueWriter* output, ECS::SceneDiagnostics* diagnostics) {
            const auto& original = *static_cast<const ECS::SceneComponentCodecFns*>(context);
            return version == 33 && original.DecodeYAML(original.Context, 1, value, output, diagnostics);
        };
        ASSERT_TRUE(current.Register({.Key = schema->Key, .Version = 33, .RuntimeType = schema->RuntimeType, .Fields = schema->Fields, .FieldCount = schema->FieldCount, .Codecs = codecs, .References = schema->References}, &Diagnostics));
        for (uint32_t version = 1; version <= ECS::SceneMigrationRegistry::MaxSteps; ++version)
        {
            ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", version, version + 1, nullptr, Upgrade, &Diagnostics));
        }
        ASSERT_TRUE(ECS::SceneDocument::Parse(Source.data(), Source.size(), current, &Document, &Diagnostics, Limits, &Migrations, &Route));
        EXPECT_EQ(Allocator.MigrationAllocations, 2u);
        EXPECT_EQ(Allocator.PeakMigrationBlocks, 2u);
        EXPECT_EQ(Allocator.LiveMigrationBlocks, 0u);
        EXPECT_EQ(Budget.OutstandingAllocations(), 1u);
        EXPECT_FALSE(Document.ReadOnly());
        EXPECT_EQ(Document.Settings()->Name, Name);
    }

    TEST_F(CookSceneDocumentTest, FailedBinaryDecodeAllocationPreservesThePreviousDocument)
    {
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Budget, ZKilo(16), CookMemoryCategory::Codecs).Succeeded());
        Core::Containers::Array<uint8_t> bytes = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Original, Schemas, workspace.Arena(), &bytes, &Diagnostics, Limits));
        const auto* retained = Original.Settings();
        Allocator.FailAt     = Allocator.Calls + 1;
        EXPECT_FALSE(ECS::BinarySceneCodec::Decode({bytes.data(), bytes.size()}, Schemas, &Original, &Diagnostics, Limits, &Route));
        EXPECT_EQ(Original.Settings(), retained);
        EXPECT_EQ(Route.Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Budget.OutstandingAllocations(), 1u); // Only the caller's output workspace.
    }

    TEST_F(CookSceneDocumentTest, TreeExhaustionIsFallibleAndPreservesThePreviousDocument)
    {
        Limits.MaxDecodedBytes = 1;
        const auto* retained   = Original.Settings();
        EXPECT_FALSE(Parse(&Original));
        EXPECT_TRUE(Diagnostics.HasErrors());
        EXPECT_EQ(Original.Settings(), retained);
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        EXPECT_EQ(Route.Error(), VFS::VFSError::OK); // Rejected inside the explicitly bounded tree, not its allocator.
    }

    TEST_F(CookSceneDocumentTest, InvalidSceneRequestsAreRejectedBeforeQueryingFixedWorkingStorage)
    {
        ECS::SceneComponentSchemaRegistry uninitialized = {};
        Allocator.FixedFailure                          = VFS::VFSError::Unsupported;
        const auto* retained                            = Original.Settings();
        for (bool encode : {false, true})
        {
            for (uint32_t failure = 0; failure < 8; ++failure)
            {
                SCOPED_TRACE(failure);
                SCOPED_TRACE(encode);
                CookLimits limits  = {.MaxWorkingBytes = ZMega(2), .SceneLimits = Limits};
                auto       capture = CaptureSource();
                auto       input   = SceneInput(capture, limits);
                if (failure == 0)
                {
                    input.Capture = nullptr;
                }
                if (failure == 1)
                {
                    input.Limits = nullptr;
                }
                if (failure == 2)
                {
                    input.Source = {};
                }
                if (failure == 3)
                {
                    limits.MaxWorkingBytes = 0;
                }
                if (failure == 4)
                {
                    capture.Bytes = {};
                }
                SceneSink       sink   = {};
                CookSceneOutput output = {.Bytes = 99};
                CookSceneCodec  codec(Allocator, failure == 7 ? uninitialized : Schemas);
                const auto      result = encode ? codec.Encode(input, SceneUUID, failure == 6 ? static_cast<CookTarget>(255) : CookTarget::DesktopVulkanLE64, sink, failure == 5 ? nullptr : &output, nullptr) : codec.Read(input, failure == 5 ? nullptr : &Original, nullptr, failure == 6 ? CookStage::Publish : CookStage::Discover);
                EXPECT_EQ(result.Error(), failure == 4 ? VFS::VFSError::Corrupted : failure == 6 && encode ? VFS::VFSError::Unsupported : VFS::VFSError::InvalidPath);
                EXPECT_EQ(Allocator.FixedCalls, 0u);
                EXPECT_EQ(Allocator.Calls, 0u);
                EXPECT_EQ(capture.Reads, 0u);
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_EQ(output.Bytes, 99u);
                EXPECT_EQ(Original.Settings(), retained);
            }
        }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookSceneDocumentTest, PrincipalSceneRefusalIsStickyAndPreservesCallerValuesBeforeReadOrAllocation)
    {
        CookLimits  limits   = {.MaxWorkingBytes = ZMega(2), .SceneLimits = Limits};
        const auto  retained = Budget.Statistics().LiveBytes;
        const auto* original = Original.Settings();
        for (auto error : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::OutOfMemory, VFS::VFSError::Unsupported})
        {
            for (bool encode : {false, true})
            {
                SCOPED_TRACE(encode);
                auto capture           = CaptureSource();
                Allocator.FixedFailure = error;
                Allocator.FixedCalls = Allocator.Calls = 0;
                CookSceneCodec  codec(Allocator, Schemas);
                SceneSink       sink        = {};
                CookSceneOutput output      = {.UUID = SceneUUID, .Bytes = 99};
                CookDiagnostics diagnostics = {};
                for (size_t attempt = 0; attempt < 2; ++attempt)
                {
                    const auto result = encode ? codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics) : codec.Read(SceneInput(capture, limits), &Original, &diagnostics);
                    EXPECT_EQ(result.Error(), error);
                    EXPECT_EQ(Allocator.FixedCalls, 1u); // First refusal remains sticky.
                    EXPECT_EQ(Allocator.Calls, 0u);
                    EXPECT_EQ(capture.Reads, 0u);
                    EXPECT_EQ(sink.Calls, 0u);
                    EXPECT_EQ(output.UUID, SceneUUID);
                    EXPECT_EQ(output.Bytes, 99u);
                    EXPECT_EQ(output.Digest, Digest256{});
                    EXPECT_EQ(Original.Settings(), original);
                    EXPECT_EQ(Budget.Statistics().LiveBytes, retained);
                    EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
                }
                ASSERT_GT(diagnostics.Count, 0u);
                EXPECT_EQ(diagnostics.Entries[0].Stage, encode ? CookStage::Encode : CookStage::Discover);
                EXPECT_EQ(diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
                EXPECT_GT(Allocator.FixedBytes, sizeof(Core::Hash::SHA256));
            }
        }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookSceneDocumentTest, NestedEncodeAndReadFixedStateUsesOneLedgerOutsideTheRetainedSlab)
    {
        constexpr uint64_t read_bytes   = sizeof(CookWorkspace) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(ECS::SceneDocument) + sizeof(Core::Memory::WorkingMemoryCharge);
        constexpr uint64_t encode_bytes = sizeof(ECS::SceneDocument) + sizeof(CookWorkspace) + sizeof(Core::Containers::Array<uint8_t>) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookSceneOutput) + sizeof(size_t) + sizeof(Core::Memory::WorkingMemoryCharge);
        const auto         retained     = Budget.Statistics().LiveBytes;
        CookLimits         limits       = {.MaxWorkingBytes = ZMega(2), .SceneLimits = Limits};
        for (uint64_t shortfall : {1u, 0u})
        {
            SCOPED_TRACE(shortfall);
            ASSERT_GT(ZMega(2) - retained, encode_bytes + read_bytes);
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(Budget.TryCharge(ZMega(2) - retained - encode_bytes - read_bytes + shortfall, CookMemoryCategory::Codecs, &occupied).Succeeded());
            auto capture         = CaptureSource();
            Allocator.FixedCalls = Allocator.Calls = 0;
            CookSceneCodec  codec(Allocator, Schemas);
            SceneSink       sink   = {};
            CookSceneOutput output = {.UUID = SceneUUID, .Bytes = 99};
            EXPECT_EQ(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Allocator.FixedCalls, 1u); // Inner phases resize that same charge.
            EXPECT_EQ(Allocator.FixedBytes, encode_bytes);
            if (shortfall)
            {
                EXPECT_EQ(capture.Reads, 0u);
                EXPECT_EQ(Allocator.Calls, 0u);
            }
            else
            {
                // Exact space for these two phases reaches parsing, where its
                // distinct YAML fixed state must still be charged separately.
                EXPECT_GT(capture.Reads, 0u);
                EXPECT_GT(Allocator.Calls, 0u);
            }
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(output.UUID, SceneUUID);
            EXPECT_EQ(output.Bytes, 99u);
            EXPECT_EQ(output.Digest, Digest256{});
            EXPECT_EQ(Budget.Statistics().LiveBytes, retained + occupied.Bytes());
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookSceneDocumentTest, PrincipalEncodePeakBoundaryPreservesBytesAndAddsNoNativeAllocation)
    {
        CookLimits      limits   = {.MaxWorkingBytes = ZMega(2), .SceneLimits = Limits};
        auto            capture  = CaptureSource();
        const auto      retained = Budget.Statistics().LiveBytes;
        SceneSink       baseline = {};
        CookSceneOutput expected = {};
        {
            CookSceneCodec codec(Allocator, Schemas);
            ASSERT_TRUE(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, baseline, &expected, nullptr).Succeeded());
        }
        const auto peak = Budget.Statistics().PeakBytes - retained;
        ASSERT_GT(peak, Allocator.FixedBytes);
        ASSERT_EQ(Budget.Statistics().LiveBytes, retained);
        for (uint64_t shortfall : {1u, 0u})
        {
            SCOPED_TRACE(shortfall);
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(Budget.TryCharge(ZMega(2) - retained - peak + shortfall, CookMemoryCategory::Codecs, &occupied).Succeeded());
            SceneSink       sink   = {};
            CookSceneOutput output = {.UUID = SceneUUID, .Bytes = 99};
            CookSceneCodec  codec(Allocator, Schemas);
            EXPECT_EQ(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr).Error(), shortfall ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK);
            if (shortfall)
            {
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_EQ(output.Bytes, 99u);
                EXPECT_EQ(output.Digest, Digest256{});
            }
            else
            {
                EXPECT_EQ(output.UUID, expected.UUID);
                EXPECT_EQ(output.Bytes, expected.Bytes);
                EXPECT_EQ(output.Digest, expected.Digest);
                ASSERT_EQ(sink.Count, baseline.Count);
                EXPECT_TRUE(std::equal(sink.Bytes.begin(), sink.Bytes.begin() + sink.Count, baseline.Bytes.begin()));
            }
            EXPECT_EQ(Budget.Statistics().LiveBytes, retained + occupied.Bytes());
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
            EXPECT_EQ(Parent.Allocations, 1u);
        }
    }

    TEST_F(CookSceneDocumentTest, CapturedSceneOwnsValuesAfterSourceReleaseAndRootsNeedNoRetainedDocument)
    {
        CookLimits limits              = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto       capture             = CaptureSource();
        capture.MaxRead                = 7;
        CookDiagnostics    diagnostics = {};
        CookSceneCodec     codec(Allocator, Schemas);
        ECS::SceneDocument document = {};
        ASSERT_TRUE(codec.Read(SceneInput(capture, limits), &document, &diagnostics).Succeeded());
        EXPECT_GT(capture.Reads, 1u);
        EXPECT_EQ(Budget.OutstandingAllocations(), 1u); // Only document, not source/parser.
        std::fill(Source.begin(), Source.end(), '?');
        EXPECT_EQ(document.Settings()->Name, Name);
        CookDependencyGraph graph = {};
        ASSERT_TRUE(graph.Initialize(Allocator, limits).Succeeded());
        const auto scene = graph.AddScene(document, Schemas, SceneInput(capture, limits).Source, &diagnostics);
        ASSERT_TRUE(scene.Succeeded());
        document = {};
        ASSERT_TRUE(graph.BeginDiscovery().Succeeded());
        ASSERT_TRUE(graph.Finalize(&diagnostics).Succeeded());
        EXPECT_EQ(graph.SceneCount(), 1u);
        EXPECT_EQ(graph.Scene(scene.Value())->UUID, SceneUUID);
        EXPECT_EQ(graph.NodeCount(), 0u);
        EXPECT_FALSE(diagnostics.HasErrors());
    }

    TEST_F(CookSceneDocumentTest, EveryCapturedParseAllocationRefusalPreservesPreviousDocument)
    {
        CookLimits limits  = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto       capture = CaptureSource();
        {
            CookSceneCodec     codec(Allocator, Schemas);
            ECS::SceneDocument document = {};
            ASSERT_TRUE(codec.Read(SceneInput(capture, limits), &document, nullptr).Succeeded());
        }
        const auto calls = Allocator.Calls;
        ASSERT_GT(calls, 2u);
        for (size_t fail = 1; fail <= calls; ++fail)
        {
            SCOPED_TRACE(fail);
            Allocator.Calls  = 0;
            Allocator.FailAt = fail;
            CookSceneCodec  codec(Allocator, Schemas);
            CookDiagnostics diagnostics = {};
            const auto*     retained    = Original.Settings();
            EXPECT_EQ(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Original.Settings(), retained);
            ASSERT_GT(diagnostics.Count, 0u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
            EXPECT_EQ(diagnostics.Entries[0].Source, SceneInput(capture, limits).Source);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, CapturedSceneFormatAndDecodedCapsHaveStructuredBudgetErrors)
    {
        CookLimits base    = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto       capture = CaptureSource();
        for (uint32_t choice = 0; choice < 7; ++choice)
        {
            auto limits = base;
            switch (choice)
            {
                case 0:
                    limits.SceneLimits.MaxFileBytes = Source.size() - 1;
                    break;
                case 1:
                    limits.SceneLimits.MaxNodes = 1;
                    break;
                case 2:
                    limits.SceneLimits.MaxCollectionItems = 1;
                    break;
                case 3:
                    limits.SceneLimits.MaxScalarBytes = 1;
                    break;
                case 4:
                    limits.SceneLimits.MaxDepth = 2;
                    break;
                case 5:
                    limits.SceneLimits.MaxEntities = 0;
                    break;
                case 6:
                    limits.SceneLimits.MaxDecodedBytes = 1;
                    break;
            }
            SCOPED_TRACE(choice);
            CookSceneCodec  codec(Allocator, Schemas);
            CookDiagnostics diagnostics = {};
            const auto*     retained    = Original.Settings();
            EXPECT_EQ(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(Original.Settings(), retained);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, CapturedSyntaxUnsupportedAndMissingMigrationErrorsAreDistinctFromBudgets)
    {
        CookLimits limits = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        {
            auto            capture = CaptureSource();
            CookSceneCodec  codec(Allocator, BuiltIns); // source NameComponent is newer than this registry.
            CookDiagnostics diagnostics = {};
            EXPECT_EQ(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Error(), VFS::VFSError::Unsupported);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedVersion);
        }
        Legacy();
        {
            auto            capture = CaptureSource();
            CookSceneCodec  codec(Allocator, Schemas); // old document without configured migrations.
            CookDiagnostics diagnostics = {};
            EXPECT_EQ(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Error(), VFS::VFSError::Unsupported);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedVersion);
        }
        Source = "[";
        {
            auto            capture = CaptureSource();
            CookSceneCodec  codec(Allocator, Schemas);
            CookDiagnostics diagnostics = {};
            EXPECT_EQ(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Error(), VFS::VFSError::Corrupted);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidScene);
        }
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
    }

    TEST_F(CookSceneDocumentTest, CapturedReadsAndDigestAreCheckedBeforePublishingADocument)
    {
        CookLimits limits = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        for (uint32_t choice = 0; choice < 4; ++choice)
        {
            auto capture = CaptureSource();
            switch (choice)
            {
                case 0:
                    capture.EarlyEOF = true;
                    break;
                case 1:
                    capture.Overreported = true;
                    break;
                case 2:
                    capture.Error = VFS::VFSError::PermissionDenied;
                    break;
                case 3:
                    capture.SourceDigest.Bytes[0] ^= 1;
                    break;
            }
            CookSceneCodec  codec(Allocator, Schemas);
            CookDiagnostics diagnostics = {};
            const auto*     retained    = Original.Settings();
            EXPECT_TRUE(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Failed());
            EXPECT_EQ(Original.Settings(), retained);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, choice == 3 ? CookDiagnosticCode::InputChanged : CookDiagnosticCode::IOFailure);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, MeasuredBinaryOutputUsesExactStorageAndMatchesIndependentDecode)
    {
        CookLimits limits  = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto       capture = CaptureSource();
        capture.MaxRead    = 13;
        CookSceneCodec  codec(Allocator, Schemas);
        CookDiagnostics diagnostics = {};
        SceneSink       sink        = {};
        CookSceneOutput output      = {};
        ASSERT_TRUE(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics).Succeeded());
        EXPECT_EQ(sink.Calls, 1u);
        EXPECT_EQ(output.Bytes, sink.Count);
        EXPECT_EQ(Allocator.LastBytes, sink.Count); // Exact-sized binary allocation, not the file cap.
        EXPECT_EQ(output.UUID, SceneUUID);
        EXPECT_LT(output.Bytes, Limits.MaxFileBytes);
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        Core::Hash::SHA256 hash   = {};
        Digest256          digest = {};
        ASSERT_TRUE(hash.Update({sink.Bytes.data(), sink.Count}).Succeeded());
        ASSERT_TRUE(hash.Finalize(&digest).Succeeded());
        EXPECT_EQ(output.Digest, digest);
        size_t     measured = 0;
        const auto calls    = Allocator.Calls;
        ASSERT_TRUE(ECS::BinarySceneCodec::Measure(Original, Schemas, &measured, &Diagnostics, Limits));
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(measured, sink.Count);
        ECS::SceneDocument decoded = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Decode({sink.Bytes.data(), sink.Count}, Schemas, &decoded, &Diagnostics, Limits, &Route));
        EXPECT_EQ(decoded.Settings()->Name, Name);
        EXPECT_EQ(decoded.Settings()->UUID, SceneUUID);
        EXPECT_EQ(decoded.EntityCount(), 1u);
        std::fill(sink.Bytes.begin(), sink.Bytes.end(), 0);
        EXPECT_EQ(decoded.Settings()->Name, Name);
    }

    TEST_F(CookSceneDocumentTest, EverySceneEncodeAllocationRefusalLeavesSinkAndResultUntouched)
    {
        CookLimits      limits  = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto            capture = CaptureSource();
        SceneSink       sink    = {};
        CookSceneOutput output  = {};
        {
            CookSceneCodec codec(Allocator, Schemas);
            ASSERT_TRUE(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr).Succeeded());
        }
        const auto calls = Allocator.Calls;
        ASSERT_GT(calls, 3u);
        for (size_t fail = 1; fail <= calls; ++fail)
        {
            SCOPED_TRACE(fail);
            Allocator.Calls  = 0;
            Allocator.FailAt = fail;
            CookSceneCodec  codec(Allocator, Schemas);
            CookDiagnostics diagnostics = {};
            sink.Count = sink.Calls = 0;
            output                  = {.UUID = SceneUUID, .Bytes = 99};
            EXPECT_EQ(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(output.Bytes, 99u);
            EXPECT_EQ(output.Digest, Digest256{});
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, BinaryMeasurementPreservesOutputAndReportsFileCapWithoutAllocating)
    {
        size_t measured = 0;
        ASSERT_TRUE(ECS::BinarySceneCodec::Measure(Original, Schemas, &measured, &Diagnostics, Limits));
        auto limits         = Limits;
        limits.MaxFileBytes = measured - 1;
        const auto previous = measured;
        EXPECT_FALSE(ECS::BinarySceneCodec::Measure(Original, Schemas, &measured, &Diagnostics, limits));
        EXPECT_EQ(measured, previous);
        EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::LimitExceeded));
        EXPECT_EQ(Allocator.Calls, 0u);
    }

    TEST_F(CookSceneDocumentTest, NativeAllocationRefusalIsNotMisreportedAsAConfiguredBudget)
    {
        CookLimits limits  = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto       capture = CaptureSource();
        for (size_t failure : {1u, 2u, 3u})
        {
            Allocator.Calls   = 0;
            Allocator.FailAt  = failure;
            Allocator.Failure = VFS::VFSError::OutOfMemory;
            CookSceneCodec  codec(Allocator, Schemas);
            CookDiagnostics diagnostics = {};
            const auto*     retained    = Original.Settings();
            EXPECT_EQ(codec.Read(SceneInput(capture, limits), &Original, &diagnostics).Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(Original.Settings(), retained);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::AllocationFailed);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, EveryMigratingCapturedEncodeAllocationFailureReleasesScratchAndPreservesResult)
    {
        Legacy();
        RegisterMigrations();
        CookLimits      limits  = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto            capture = CaptureSource();
        SceneSink       sink    = {};
        CookSceneOutput output  = {};
        {
            CookSceneCodec codec(Allocator, Schemas, &Migrations);
            ASSERT_TRUE(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, nullptr).Succeeded());
        }
        const auto calls = Allocator.Calls;
        ASSERT_EQ(Allocator.MigrationAllocations, 4u);
        for (size_t failure = 1; failure <= calls; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookSceneCodec  codec(Allocator, Schemas, &Migrations);
            CookDiagnostics diagnostics = {};
            sink.Count = sink.Calls = 0;
            output                  = {.Bytes = 99};
            EXPECT_EQ(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(output.Bytes, 99u);
            ASSERT_EQ(diagnostics.Count, 1u);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
            EXPECT_EQ(Allocator.LiveMigrationBlocks, 0u);
            EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookSceneDocumentTest, SceneEncodingRejectsWrongUUIDTargetAndSinkFailureWithoutPublishingResult)
    {
        CookLimits      limits  = {.MaxWorkingBytes = ZMega(1), .SceneLimits = Limits};
        auto            capture = CaptureSource();
        CookSceneCodec  codec(Allocator, Schemas);
        SceneSink       sink       = {};
        CookSceneOutput output     = {.Bytes = 99};
        const auto      wrong_uuid = uuids::uuid::from_string("90000000-0000-0000-0000-000000000009").value();
        EXPECT_EQ(codec.Encode(SceneInput(capture, limits), wrong_uuid, CookTarget::DesktopVulkanLE64, sink, &output, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(sink.Calls, 0u);
        EXPECT_EQ(output.Bytes, 99u);
        EXPECT_EQ(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::Invalid, sink, &output, nullptr).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(sink.Calls, 0u);
        sink.Error                  = VFS::VFSError::IOError;
        CookDiagnostics diagnostics = {};
        EXPECT_EQ(codec.Encode(SceneInput(capture, limits), SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(output.Bytes, 99u);
        ASSERT_EQ(diagnostics.Count, 1u);
        EXPECT_EQ(diagnostics.Entries[0].Stage, CookStage::Encode);
        EXPECT_EQ(diagnostics.Entries[0].SceneUUID, SceneUUID);
        EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::IOFailure);
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
    }

    TEST_F(CookSceneDocumentTest, CapturedSceneAndDiscoveredAssetsEncodeAfterSealSourceReplacementAndContextTeardown)
    {
        // This integration accounts the complete retained memory-backend pool,
        // not just codec buffers. The actual routed workspace remains 1 MiB.
        Budget.Release();
        ASSERT_TRUE(Budget.Initialize(Scratch, ZMega(8)).Succeeded());
        CookVFSWorkingMemory vfs_memory = {};
        ASSERT_TRUE(vfs_memory.Initialize(Budget).Succeeded());
        const auto* material_schema = BuiltIns.FindByKey("MaterialComponent");
        ASSERT_NE(material_schema, nullptr);
        ASSERT_TRUE(Schemas.Register({.Key = material_schema->Key, .Version = material_schema->Version, .RuntimeType = material_schema->RuntimeType, .Fields = material_schema->Fields, .FieldCount = material_schema->FieldCount, .Codecs = material_schema->Codecs, .References = material_schema->References}, &Diagnostics));
        const auto    material_uuid = uuids::uuid::from_string("30000000-0000-0000-0000-000000000003").value();
        ECS::EntityID entity        = ECS::INVALID_ENTITY;
        SourceScene.ForEachEntity([&](ECS::EntityID value) { entity = value; });
        ASSERT_NE(entity, ECS::INVALID_ENTITY);
        SourceScene.AddComponent<ECS::Components::MaterialComponent>(entity, {material_uuid});
        const auto settings = *Original.Settings();
        ASSERT_TRUE(ECS::SceneDocument::Capture(SourceScene, settings, Schemas, &Original, &Diagnostics, Limits));
        ASSERT_TRUE(Original.Emit(&Source, &Diagnostics));

        CookLimits            limits  = {.MaxWorkingBytes = ZMega(8), .SceneLimits = Limits};
        VFS::VFSMemoryBackend backend = {};
        VFS::VFSContext       context = {};
        ASSERT_TRUE(backend.InitializeOwned(ZMega(2), 64).Succeeded());
        const auto absolute = [](cstring path) { return VFS::VFSPath::Parse(path).Value(); };
        const auto relative = [](cstring path) { return VFS::VFSRelativePath::Parse(path).Value(); };
        const auto write    = [&](cstring path, std::string_view bytes) { return backend.WriteFile(absolute(path), {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}); };
        for (cstring path : {"/scenes", "/assets", "/scratch", "/output"})
        {
            ASSERT_TRUE(backend.CreateDir(absolute(path)).Succeeded());
        }
        ASSERT_TRUE(write("/scenes/main.scene", Source).Succeeded());
        {
            Importers::AssetCodec::MaterialAssetDocument material       = {.UUID = material_uuid};
            material.Name[0]                                            = 'M';
            SceneSink                                             sink  = {};
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
            ASSERT_TRUE(Importers::AssetCodec::WriteCanonicalMaterialAsset(material, paths, [](void* state, Core::Containers::ArrayView<const uint8_t> bytes) { return static_cast<SceneSink*>(state)->Write(bytes); }, &sink).Succeeded());
            ASSERT_TRUE(write("/assets/material.zematerial", {reinterpret_cast<cstring>(sink.Bytes.data()), sink.Count}).Succeeded());
        }
        ASSERT_TRUE(write("/assets/material.zematerial.meta", R"({"uuid":"30000000-0000-0000-0000-000000000003","importer":"MaterialImporter","settings":[]})").Succeeded());
        context.Initialize(&Memory.MainArena);
        ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        VFS::VFSPublicationRootLease output_root = {}, package = {};
        ASSERT_TRUE(context.AcquirePublicationRoot(absolute("/output"), &output_root, &vfs_memory).Succeeded());
        CookFrozenLibraryOwner      owner   = {};
        const CookFrozenRootRequest roots[] = {
            {.Key = "scenes", .Context = &context, .Path = absolute("/scenes")},
            {.Key = "assets", .Context = &context, .Path = absolute("/assets")},
        };
        ASSERT_TRUE(owner.Initialize(Allocator, roots).Succeeded());
        VFS::VFSScratchRootLease scratch = {};
        ASSERT_TRUE(context.AcquireScratchRoot(absolute("/scratch"), &scratch, &vfs_memory).Succeeded());
        CookDiagnostics diagnostics = {};
        {
            CookFrozenInputSource    provider(owner);
            CookSceneCaptureSet      captures        = {};
            CookAssetDiscovery       discovery       = {};
            CookDependencyGraph      graph           = {};
            CookInputLease           lease           = {};
            const CookCaptureContext capture_context = {.Scratch = &scratch, .ScratchRun = relative("run"), .Allocator = &Allocator, .Limits = &limits, .WorkingMemory = &vfs_memory};
            ASSERT_TRUE(provider.AcquireRoots(&lease, &diagnostics).Succeeded());
            ASSERT_TRUE(lease.RequireDisjoint(output_root, &diagnostics).Succeeded());
            ASSERT_TRUE(lease.RequireDisjoint(scratch, &diagnostics).Succeeded());
            const auto overlap = scratch.Overlaps(output_root);
            ASSERT_TRUE(overlap.Succeeded());
            ASSERT_FALSE(overlap.Value());
            const auto target = relative(CookTargetKey(CookTarget::DesktopVulkanLE64));
            ASSERT_TRUE(output_root.CreateDir(target).Succeeded());
            ASSERT_TRUE(output_root.AcquireChildRoot(target, &package).Succeeded());
            ASSERT_TRUE(scratch.CreateDirExclusive(relative("run")).Succeeded());
            ASSERT_TRUE(lease.PrepareCapture(capture_context, &diagnostics).Succeeded());
            CookSceneCodec codec(Allocator, Schemas);
            ASSERT_TRUE(graph.Initialize(Allocator, limits).Succeeded());
            const VFS::VFSPath sources[] = {absolute("/scenes/main.scene")};
            ASSERT_TRUE(captures.Capture(Allocator, sources, lease, codec, graph, limits, &diagnostics).Succeeded());
            const auto           input       = captures.Input(0).Value();
            const auto&          capture     = *input.Capture;
            CookAdapterRegistry  adapters    = {};
            const CookAdapterFns functions[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(adapters.Initialize(functions).Succeeded());
            ASSERT_TRUE(discovery.Initialize(Allocator, limits).Succeeded());
            ASSERT_TRUE(discovery.Discover(graph, lease, adapters, Allocator, &diagnostics).Succeeded());
            EXPECT_EQ(graph.NodeCount(), 1u);
            EXPECT_EQ(graph.SceneRoots(0).size(), 1u);
            CookRecipeKeys recipe          = {};
            uint32_t       schema_order[1] = {};
            ASSERT_EQ(CookSchemaKeyWorkspaceWords(Schemas).Value(), 1u);
            ASSERT_TRUE(ComputeCookRecipeKeys(Allocator, Schemas, CookTarget::DesktopVulkanLE64, schema_order, &recipe).Succeeded());
            ASSERT_TRUE(lease.Seal(&diagnostics).Succeeded());
            uint8_t                    stream[31]        = {};
            CookPublication            publication       = {};
            const CookPublicationInput publication_input = {&package, &lease, &graph, recipe, &limits};
            ASSERT_TRUE(publication.Initialize(Allocator, publication_input, stream, &diagnostics).Succeeded());
            lease.Reset();
            ASSERT_TRUE(write("/scenes/main.scene", "replacement").Succeeded());
            ASSERT_TRUE(write("/assets/material.zematerial", "replacement").Succeeded());
            context.Shutdown();
            SceneSink       sink   = {};
            CookSceneOutput output = {};
            ASSERT_TRUE(codec.Encode(input, SceneUUID, CookTarget::DesktopVulkanLE64, sink, &output, &diagnostics).Succeeded());
            EXPECT_GT(output.Bytes, 0u);
            const auto              node     = graph.Find({CookAssetKind::Material, material_uuid});
            VFS::StrictMetaFileData metadata = {};
            CookAssetInput          asset    = {};
            ASSERT_TRUE(discovery.ReadInput(node, Allocator, &metadata, &asset).Succeeded());
            SceneSink material_sink = {};
            ASSERT_TRUE(adapters.Find(CookAssetKind::Material)->Encode(nullptr, asset, {}, CookTarget::DesktopVulkanLE64, material_sink, Allocator, &diagnostics).Succeeded());
            EXPECT_GT(material_sink.Count, 0u);
            // Bind real post-seal adapter output to leaf/scene recipe keys,
            // without reopening the replaced source or retaining documents.
            CookAssetResult result = {};
            ASSERT_TRUE(ComputeCookAssetInputKey(Allocator, graph, node, recipe, {&result, 1}, &result.InputKey).Succeeded());
            Core::Hash::SHA256 hash            = {};
            Digest256          material_digest = {};
            ASSERT_TRUE(hash.Update({material_sink.Bytes.data(), material_sink.Count}).Succeeded());
            ASSERT_TRUE(hash.Finalize(&material_digest).Succeeded());
            ASSERT_TRUE(MakeCookObjectDescriptor(material_digest, material_sink.Count, "zematerial", &result.Object).Succeeded());
            uint32_t  closure[2] = {};
            Digest256 scene_key  = {};
            ASSERT_TRUE(ComputeCookSceneInputKey(Allocator, graph, 0, capture.Digest(), recipe, {&result, 1}, closure, &scene_key).Succeeded());
            auto changed_source      = capture.Digest();
            changed_source.Bytes[0] ^= 1;
            Digest256 changed_key    = {};
            ASSERT_TRUE(ComputeCookSceneInputKey(Allocator, graph, 0, changed_source, recipe, {&result, 1}, closure, &changed_key).Succeeded());
            EXPECT_NE(scene_key, changed_key);
            {
                CookSceneResult scene_result = {.SourceDigest = capture.Digest(), .InputKey = scene_key};
                ASSERT_TRUE(MakeCookObjectDescriptor(output.Digest, output.Bytes, CookSceneObjectExtension, &scene_result.Object).Succeeded());
                const CookManifestInput manifest_input = {
                    .Graph = &graph, .Recipe = recipe, .Assets = {      &result, 1},
                            .Scenes = {&scene_result, 1},
                            .Limits = &limits
                };
                SceneSink          manifest_sink   = {};
                CookManifestOutput manifest_output = {};
                const auto         calls           = Allocator.Calls;
                ASSERT_TRUE(WriteCookManifest(manifest_input, closure, manifest_sink, &manifest_output, Allocator).Succeeded());
                EXPECT_EQ(Allocator.Calls, calls);
                EXPECT_EQ(manifest_output.Bytes, manifest_sink.Count);
                CookManifest manifest = {};
                const auto   read     = manifest.Read({manifest_sink.Bytes.data(), manifest_sink.Count}, recipe.Target, limits, Allocator, &diagnostics);
                ASSERT_TRUE(read.Succeeded());
                ASSERT_EQ(read.Value(), CookManifestReadStatus::Valid);
                ASSERT_EQ(manifest.Scenes().size(), 1u);
                ASSERT_EQ(manifest.Assets().size(), 1u);
                EXPECT_EQ(manifest.Scenes()[0].Result.InputKey, scene_key);
                EXPECT_EQ(manifest.Scenes()[0].Result.Object.Digest, output.Digest);
                EXPECT_EQ(manifest.Assets()[0].Result.Object.Digest, material_digest);
                EXPECT_EQ(manifest.Roots(0)[0].UUID, material_uuid);
                // Production codecs stream directly through the transaction
                // after sealing, replacement and source-context teardown.
                struct AssetProducer
                {
                    const CookAdapterFns*       Adapter     = nullptr;
                    const CookAssetInput*       Input       = nullptr;
                    ICookAllocator*             Allocator   = nullptr;
                    CookDiagnostics*            Diagnostics = nullptr;
                    uint32_t                    Calls       = 0;

                    static VFS::VFSResult<void> Run(void* state, ICookByteSink& target)
                    {
                        auto& producer = *static_cast<AssetProducer*>(state);
                        ++producer.Calls;
                        return producer.Adapter->Encode(nullptr, *producer.Input, {}, CookTarget::DesktopVulkanLE64, target, *producer.Allocator, producer.Diagnostics);
                    }
                } asset_producer = {adapters.Find(CookAssetKind::Material), &asset, &Allocator, &diagnostics};
                struct SceneProducer
                {
                    CookSceneCodec*             Codec       = nullptr;
                    const CookSceneInput*       Input       = nullptr;
                    uuids::uuid                 UUID        = {};
                    CookDiagnostics*            Diagnostics = nullptr;
                    CookSceneOutput             Output      = {};
                    uint32_t                    Calls       = 0;

                    static VFS::VFSResult<void> Run(void* state, ICookByteSink& target)
                    {
                        auto& producer = *static_cast<SceneProducer*>(state);
                        ++producer.Calls;
                        return producer.Codec->Encode(*producer.Input, producer.UUID, CookTarget::DesktopVulkanLE64, target, &producer.Output, producer.Diagnostics);
                    }
                } scene_producer = {&codec, &input, SceneUUID, &diagnostics};
                ASSERT_TRUE(publication.EncodeAsset(node, AssetProducer::Run, &asset_producer, &diagnostics).Succeeded());
                ASSERT_TRUE(publication.EncodeScene(0, capture.Digest(), SceneProducer::Run, &scene_producer, &diagnostics).Succeeded());
                EXPECT_EQ(publication.AssetResults()[node].Object, result.Object);
                EXPECT_EQ(publication.SceneResults()[0].Object, scene_result.Object);
                EXPECT_EQ(scene_producer.Output.Digest, output.Digest);
                CookPublicationResult published = {};
                ASSERT_TRUE(publication.PublishManifest(closure, &published, &diagnostics).Succeeded());
                EXPECT_TRUE(published.Published);
                EXPECT_EQ(published.ObjectCount, 2u);
                EXPECT_EQ(published.Manifest.Digest, manifest_output.Digest);
                EXPECT_EQ(published.Manifest.Bytes, manifest_output.Bytes);
                const auto committed = package.Open(relative(CookManifestName), VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
                ASSERT_TRUE(committed.Succeeded());
                ASSERT_LE(published.Manifest.Bytes, sink.Bytes.size());
                ASSERT_EQ(committed.Value()->ReadAll({sink.Bytes.data(), static_cast<size_t>(published.Manifest.Bytes)}).Value(), published.Manifest.Bytes);
                EXPECT_EQ(Helpers::secure_memcmp(sink.Bytes.data(), sink.Bytes.size(), manifest_sink.Bytes.data(), manifest_sink.Bytes.size(), static_cast<size_t>(published.Manifest.Bytes)), 0);
                ASSERT_TRUE(package.CloseChecked(committed.Value()).Succeeded());
                ASSERT_TRUE(publication.Reset(&diagnostics).Succeeded());
                // An independent real capture/discovery/seal run proves no-op
                // reuse. Providers are deliberately single-run, not resettable.
                ASSERT_TRUE(write("/scenes/main.scene", Source).Succeeded());
                ASSERT_TRUE(write("/assets/material.zematerial", {reinterpret_cast<cstring>(material_sink.Bytes.data()), material_sink.Count}).Succeeded());
                {
                    CookFrozenInputSource repeat_provider(owner);
                    CookSceneCaptureSet   repeat_captures  = {};
                    CookDependencyGraph   repeat_graph     = {};
                    CookAssetDiscovery    repeat_discovery = {};
                    CookInputLease        repeat_lease     = {};
                    auto                  repeat_context   = capture_context;
                    repeat_context.ScratchRun              = relative("repeat");
                    ASSERT_TRUE(repeat_provider.AcquireRoots(&repeat_lease, &diagnostics).Succeeded());
                    ASSERT_TRUE(repeat_lease.RequireDisjoint(output_root, &diagnostics).Succeeded());
                    ASSERT_TRUE(repeat_lease.RequireDisjoint(scratch, &diagnostics).Succeeded());
                    const auto repeat_overlap = scratch.Overlaps(output_root);
                    ASSERT_TRUE(repeat_overlap.Succeeded());
                    ASSERT_FALSE(repeat_overlap.Value());
                    VFS::VFSPublicationRootLease repeat_package = {};
                    ASSERT_TRUE(output_root.AcquireChildRoot(target, &repeat_package).Succeeded());
                    ASSERT_TRUE(scratch.CreateDirExclusive(relative("repeat")).Succeeded());
                    ASSERT_TRUE(repeat_lease.PrepareCapture(repeat_context, &diagnostics).Succeeded());
                    ASSERT_TRUE(repeat_graph.Initialize(Allocator, limits).Succeeded());
                    ASSERT_TRUE(repeat_captures.Capture(Allocator, sources, repeat_lease, codec, repeat_graph, limits, &diagnostics).Succeeded());
                    const auto repeat_input = repeat_captures.Input(0).Value();
                    ASSERT_TRUE(repeat_discovery.Initialize(Allocator, limits).Succeeded());
                    ASSERT_TRUE(repeat_discovery.Discover(repeat_graph, repeat_lease, adapters, Allocator, &diagnostics).Succeeded());
                    ASSERT_TRUE(repeat_lease.Seal(&diagnostics).Succeeded());
                    const auto              repeat_node     = repeat_graph.Find({CookAssetKind::Material, material_uuid});
                    VFS::StrictMetaFileData repeat_metadata = {};
                    CookAssetInput          repeat_asset    = {};
                    ASSERT_TRUE(repeat_discovery.ReadInput(repeat_node, Allocator, &repeat_metadata, &repeat_asset).Succeeded());
                    asset_producer.Input     = &repeat_asset;
                    scene_producer.Input     = &repeat_input;
                    CookPublication repeated = {};
                    ASSERT_TRUE(repeated.Initialize(Allocator, {&repeat_package, &repeat_lease, &repeat_graph, recipe, &limits}, stream, &diagnostics).Succeeded());
                    repeat_lease.Reset();
                    ASSERT_TRUE(write("/scenes/main.scene", "replacement").Succeeded());
                    ASSERT_TRUE(write("/assets/material.zematerial", "replacement").Succeeded());
                    ASSERT_TRUE(repeated.EncodeAsset(repeat_node, AssetProducer::Run, &asset_producer, &diagnostics).Succeeded());
                    ASSERT_TRUE(repeated.EncodeScene(0, repeat_input.Capture->Digest(), SceneProducer::Run, &scene_producer, &diagnostics).Succeeded());
                    ASSERT_TRUE(repeated.PublishManifest(closure, &published, &diagnostics).Succeeded());
                    EXPECT_FALSE(published.Published);
                    EXPECT_EQ(published.Manifest.Digest, manifest_output.Digest);
                    EXPECT_EQ(repeated.StagedBytes(), 0u);
                    EXPECT_EQ(asset_producer.Calls, 1u);
                    EXPECT_EQ(scene_producer.Calls, 1u);
                    ASSERT_TRUE(repeated.Reset(&diagnostics).Succeeded());
                    repeat_discovery.Reset();
                    ASSERT_EQ(repeat_discovery.CleanupError(), VFS::VFSError::OK);
                    ASSERT_TRUE(repeat_captures.Reset().Succeeded());
                    ASSERT_TRUE(repeat_provider.ReleaseStorage().Succeeded());
                }
                ASSERT_TRUE(scratch.Remove(relative("repeat")).Succeeded());
                // Independent strict ledger plus physically verified reuse.
                CookIncrementalReuse reuse = {};
                ASSERT_TRUE(reuse.Initialize(Allocator, {&graph, recipe, &manifest, CookObjectReader(package), &limits}, stream).Succeeded());
                const auto reuse_calls = Allocator.Calls;
                ASSERT_EQ(reuse.PrepareAsset(node, &diagnostics).Value(), CookReuseDecision::Ready);
                ASSERT_EQ(reuse.PrepareScene(0, capture.Digest(), &diagnostics).Value(), CookReuseDecision::Ready);
                EXPECT_TRUE(reuse.AllReady());
                EXPECT_TRUE(reuse.WasAssetReused(node));
                EXPECT_TRUE(reuse.WasSceneReused(0));
                EXPECT_EQ(reuse.AssetResults()[node].Object, result.Object);
                EXPECT_EQ(reuse.SceneResults()[0].Object, scene_result.Object);
                EXPECT_EQ(Allocator.Calls, reuse_calls);
            }
            EXPECT_FALSE(diagnostics.HasErrors());
            discovery.Reset();
            EXPECT_EQ(discovery.CleanupError(), VFS::VFSError::OK);
            ASSERT_TRUE(captures.Reset().Succeeded());
            ASSERT_TRUE(provider.ReleaseStorage().Succeeded());
        }
        EXPECT_TRUE(scratch.Remove(relative("run")).Succeeded());
        scratch.Reset();
        EXPECT_TRUE(owner.Reset().Succeeded());
        package.Reset();
        output_root.Reset();
        EXPECT_EQ(vfs_memory.ReservedBytes(), 0u);
        ASSERT_TRUE(vfs_memory.Reset().Succeeded());
        EXPECT_EQ(Budget.OutstandingAllocations(), 0u);
    }
} // namespace ZEngine::Cook
