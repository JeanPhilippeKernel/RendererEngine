#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include "CookManifestGolden.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        Digest256 Repeated(uint8_t byte)
        {
            Digest256 digest = {};
            std::fill(digest.Bytes.begin(), digest.Bytes.end(), byte);
            return digest;
        }

        uuids::uuid ManifestUUID(uint32_t value)
        {
            char text[37] = {};
            std::snprintf(text, sizeof(text), "00000000-0000-4000-8000-%012x", value);
            return uuids::uuid::from_string(text).value();
        }

        size_t FindText(Core::Containers::StringView text, Core::Containers::StringView match)
        {
            const auto found = std::search(text.begin(), text.end(), match.begin(), match.end());
            return found == text.end() ? SIZE_MAX : static_cast<size_t>(found - text.begin());
        }

        struct ManifestSink final : ICookByteSink
        {
            char                 Bytes[16384] = {};
            size_t               Count        = 0;
            uint32_t             Calls        = 0;
            uint32_t             FailAt       = UINT32_MAX;
            ICookAllocator*      Budget       = nullptr;
            uint64_t             MinLiveBytes = UINT64_MAX;
            uint64_t             MaxLiveBytes = 0;

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (Budget)
                {
                    const auto live = Budget->Statistics().LiveBytes;
                    MinLiveBytes    = std::min(MinLiveBytes, live);
                    MaxLiveBytes    = std::max(MaxLiveBytes, live);
                }
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied);
                }
                if (bytes.size() > sizeof(Bytes) - Count)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                if (bytes.size())
                {
                    Helpers::secure_memcpy(Bytes + Count, sizeof(Bytes) - Count, bytes.data(), bytes.size());
                }
                Count += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }

            Core::Containers::StringView Text() const
            {
                return {Bytes, Count};
            }
        };

        struct ManifestAllocator final : ICookAllocator
        {
            ICookAllocator*      Backing     = nullptr;
            VFS::VFSError        Failure     = VFS::VFSError::OK;
            uint32_t             ChargeCalls = 0;
            uint64_t             Bytes       = 0;
            CookMemoryCategory   Category    = CookMemoryCategory::Count;

            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++ChargeCalls;
                Bytes    = bytes;
                Category = category;
                return Failure == VFS::VFSError::OK ? Backing->TryCharge(bytes, category, output) : VFS::VFSResult<void>::Fail(Failure);
            }
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Backing->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Backing->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Backing->Statistics();
            }
        };
    } // namespace

    class CookManifestWriterTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(64), 3> Allocator       = {};
        ManifestAllocator                   WriterAllocator = {};
        CookLimits                          Limits          = {.MaxWorkingBytes = ZKilo(64)};
        CookDependencyGraph                 Graph           = {};
        CookAssetResult                     Assets[12]      = {};
        CookSceneResult                     Scenes[3]       = {};
        uint32_t                            Workspace[16]   = {};
        CookManifestInput                   Input           = {};

        void                                SetUp() override
        {
            WriterAllocator.Backing = &Allocator;
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            Input.Graph  = &Graph;
            Input.Limits = &Limits;
            Input.Recipe = {.Target = CookTarget::DesktopVulkanLE64, .SchemaKey = Repeated(0x44)};
            ASSERT_TRUE(ComputeCookSettingsKey(Input.Recipe.Target, &Input.Recipe.SettingsKey).Succeeded());
        }

        void AddScene(uint32_t uuid = 1, Core::Containers::StringView source = "scenes/main.yaml")
        {
            const auto path = VFS::VFSRelativePath::Parse(source);
            ASSERT_TRUE(path.Succeeded());
            const auto added = Graph.AddScene(ManifestUUID(uuid), path.Value());
            ASSERT_TRUE(added.Succeeded());
            auto& result        = Scenes[added.Value()];
            result.SourceDigest = Repeated(0x33);
            result.InputKey     = Repeated(0x11);
            ASSERT_TRUE(MakeCookObjectDescriptor(Repeated(0x22), 256, CookSceneObjectExtension, &result.Object).Succeeded());
            Input.Scenes = {Scenes, Graph.SceneCount()};
        }

        void FinalizeEmpty()
        {
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        }

        void AddAsset(uint32_t scene, CookAssetKind kind, uint32_t uuid)
        {
            ASSERT_TRUE(Graph.AddRoot(scene, {kind, ManifestUUID(uuid)}, {}, "Mesh", "mesh").Succeeded());
        }

        void Inspect(uint32_t index, cstring format_key, Core::Containers::ArrayView<const CookDependency> dependencies = {})
        {
            CookAssetInspection inspection = {};
            ASSERT_TRUE(inspection.Assign(format_key, {}, {}, dependencies, Allocator).Succeeded());
            ASSERT_TRUE(Graph.SetInspection(index, 1, inspection).Succeeded());
            const auto* format = FindCookAssetFormat(format_key);
            ASSERT_NE(format, nullptr);
            auto& result    = Assets[index];
            result.InputKey = Repeated(static_cast<uint8_t>(0x60 + index));
            ASSERT_TRUE(MakeCookObjectDescriptor(Repeated(static_cast<uint8_t>(0x70 + index)), 128, format->Extension, &result.Object).Succeeded());
        }

        void CompleteAssets()
        {
            ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
            Input.Assets = {Assets, Graph.NodeCount()};
        }
    };

    TEST_F(CookManifestWriterTest, ExactReviewedGoldenAndDigestWithoutAllocationsOrInputMutation)
    {
        AddScene();
        FinalizeEmpty();
        const auto         allocations = Allocator.Allocations;
        const auto         bytes       = Allocator.Statistics().LiveBytes;
        ManifestSink       sink        = {};
        CookManifestOutput output      = {};
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Succeeded());
        EXPECT_EQ(sink.Text(), Tests::ManifestGolden);
        EXPECT_EQ(output.Bytes, Tests::ManifestGolden.size());
        Core::Hash::SHA256 hash = {};
        ASSERT_TRUE(hash.Update({reinterpret_cast<const uint8_t*>(sink.Bytes), sink.Count}).Succeeded());
        Digest256 expected = {};
        ASSERT_TRUE(hash.Finalize(&expected).Succeeded());
        EXPECT_EQ(output.Digest, expected);
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, bytes);
        EXPECT_EQ(Graph.Scene(0)->UUID, ManifestUUID(1));
        EXPECT_EQ(Scenes[0].InputKey, Repeated(0x11));
    }

    TEST_F(CookManifestWriterTest, SceneOrderDoesNotChangeCanonicalBytesOrResultAssociation)
    {
        AddScene(9, "scenes/z.yaml");
        AddScene(1, "scenes/a.yaml");
        Scenes[1].SourceDigest = Repeated(0x55);
        FinalizeEmpty();
        ManifestSink       sink   = {};
        CookManifestOutput output = {};
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Succeeded());
        EXPECT_LT(FindText(sink.Text(), "scenes/a.yaml"), FindText(sink.Text(), "scenes/z.yaml"));
        EXPECT_LT(FindText(sink.Text(), "5555555555"), FindText(sink.Text(), "3333333333"));
        EXPECT_EQ(Graph.Scene(0)->UUID, ManifestUUID(9));
        EXPECT_EQ(Scenes[0].SourceDigest, Repeated(0x33));
        const auto first_digest = output.Digest;
        Graph.Reset();
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        AddScene(1, "scenes/a.yaml");
        Scenes[0].SourceDigest = Repeated(0x55);
        AddScene(9, "scenes/z.yaml");
        FinalizeEmpty();
        ManifestSink reordered = {};
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, reordered, &output, WriterAllocator).Succeeded());
        EXPECT_EQ(reordered.Text(), sink.Text());
        EXPECT_EQ(output.Digest, first_digest);
    }

    TEST_F(CookManifestWriterTest, CanonicalAssetsRootsAndMultipleSlotsKeepTheirGraphAssociations)
    {
        AddScene();
        AddAsset(0, CookAssetKind::Mesh, 4);
        AddAsset(0, CookAssetKind::Texture, 2);
        AddAsset(0, CookAssetKind::Mesh, 4);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency mesh_dependencies[] = {
            {{CookAssetKind::Material, ManifestUUID(3)}, "submeshes/0/material"}
        };
        Inspect(0, "zemesh-v1-le64", mesh_dependencies);
        Inspect(1, "image-png");
        const CookDependency material_dependencies[] = {
            {{CookAssetKind::Texture, ManifestUUID(2)}, "albedo"},
            {{CookAssetKind::Texture, ManifestUUID(2)}, "normal"},
        };
        Inspect(2, "zematerial-v1-json", material_dependencies);
        CompleteAssets();
        ManifestSink       sink        = {};
        CookManifestOutput output      = {};
        const auto         allocations = Allocator.Allocations;
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Succeeded());
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_LT(FindText(sink.Text(), "\"format\":\"zematerial"), FindText(sink.Text(), "\"format\":\"zemesh"));
        EXPECT_LT(FindText(sink.Text(), "\"format\":\"zemesh"), FindText(sink.Text(), "\"format\":\"image-png"));
        EXPECT_LT(FindText(sink.Text(), "\"slot\":\"albedo\""), FindText(sink.Text(), "\"slot\":\"normal\""));
        EXPECT_NE(FindText(sink.Text(), "\"roots\":[{\"kind\":\"mesh\",\"uuid\":\"00000000-0000-4000-8000-000000000004\"},{\"kind\":\"texture\",\"uuid\":\"00000000-0000-4000-8000-000000000002\"}]"), SIZE_MAX);
    }

    TEST_F(CookManifestWriterTest, SharedObjectPathsMustHaveConsistentSizesBeforeAnyWrite)
    {
        AddScene();
        AddScene(2, "scenes/second.yaml");
        FinalizeEmpty();
        ManifestSink       sink   = {};
        CookManifestOutput output = {.Digest = Repeated(0xaa), .Bytes = 999};
        Scenes[1].Object.Size     = 257;
        const auto live           = Allocator.Statistics().LiveBytes;
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(WriterAllocator.ChargeCalls, 1u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        EXPECT_EQ(sink.Calls, 0u);
        EXPECT_EQ(output.Bytes, 999u);
        EXPECT_EQ(output.Digest, Repeated(0xaa));
        Scenes[1].Object.Size = 256;
        EXPECT_TRUE(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Succeeded());
    }

    TEST_F(CookManifestWriterTest, AssetAndSceneDescriptorValidationPrecedesAllWrites)
    {
        AddScene();
        AddAsset(0, CookAssetKind::Texture, 2);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        Inspect(0, "image-png");
        CompleteAssets();
        ManifestSink       sink   = {};
        CookManifestOutput output = {.Digest = Repeated(0xaa), .Bytes = 999};
        const auto         object = Assets[0].Object;
        Assets[0].Object.Digest   = Repeated(0x12);
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Corrupted);
        Assets[0].Object = object;
        const auto scene = Scenes[0].Object;
        ASSERT_TRUE(MakeCookObjectDescriptor(scene.Digest, scene.Size, "png", &Scenes[0].Object).Succeeded());
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Corrupted);
        Scenes[0].Object            = scene;
        Limits.MaxEncodedAssetBytes = 127;
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxEncodedAssetBytes     = 512;
        Limits.SceneLimits.MaxFileBytes = 255;
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(sink.Calls, 0u);
        EXPECT_EQ(output.Digest, Repeated(0xaa));
        EXPECT_EQ(output.Bytes, 999u);
    }

    TEST_F(CookManifestWriterTest, WireSlotsRejectWrongParentChildKindsAndNoncanonicalSubmeshIndexes)
    {
        const Core::Containers::StringView slots[] = {"submeshes/01/material", "submeshes/-1/material", "submeshes/4294967296/material", "submeshes//material", "normal"};
        for (const auto slot : slots)
        {
            SCOPED_TRACE(slot.data());
            Graph.Reset();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            AddScene();
            AddAsset(0, CookAssetKind::Mesh, 4);
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            const CookDependency dependency = {
                {CookAssetKind::Material, ManifestUUID(3)},
                slot
            };
            Inspect(0, "zemesh-v1-le64", {&dependency, 1});
            Inspect(1, "zematerial-v1-json");
            CompleteAssets();
            ManifestSink       sink   = {};
            CookManifestOutput output = {};
            EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(sink.Calls, 0u);
        }
    }

    TEST_F(CookManifestWriterTest, ManifestLimitIsMeasuredExactlyBeforeWritingAndSupportsExactFit)
    {
        AddScene();
        FinalizeEmpty();
        ManifestSink       sink        = {};
        CookManifestOutput output      = {.Digest = Repeated(0xaa), .Bytes = 999};
        const auto         live        = Allocator.Statistics().LiveBytes;
        const auto         allocations = Allocator.Allocations;
        for (uint64_t limit : {uint64_t{0}, uint64_t{1}, uint64_t{Tests::ManifestGolden.size() - 1}})
        {
            Limits.MaxManifestBytes = limit;
            EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(output.Digest, Repeated(0xaa));
            EXPECT_EQ(output.Bytes, 999u);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
            EXPECT_EQ(Allocator.Allocations, allocations);
        }
        Limits.MaxManifestBytes = Tests::ManifestGolden.size();
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Succeeded());
        EXPECT_EQ(sink.Text(), Tests::ManifestGolden);
    }

    TEST_F(CookManifestWriterTest, EverySinkFailurePreservesTheCallerResultAndPropagatesIOError)
    {
        AddScene();
        FinalizeEmpty();
        ManifestSink       successful = {};
        CookManifestOutput output     = {};
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, successful, &output, WriterAllocator).Succeeded());
        const auto live        = Allocator.Statistics().LiveBytes;
        const auto allocations = Allocator.Allocations;
        const auto fixed       = WriterAllocator.Bytes;
        for (uint32_t call = 1; call <= successful.Calls; ++call)
        {
            ManifestSink failing = {};
            failing.FailAt       = call;
            failing.Budget       = &Allocator;
            output               = {.Digest = Repeated(0xaa), .Bytes = 999};
            EXPECT_EQ(WriteCookManifest(Input, Workspace, failing, &output, WriterAllocator).Error(), VFS::VFSError::PermissionDenied);
            EXPECT_EQ(output.Digest, Repeated(0xaa));
            EXPECT_EQ(output.Bytes, 999u);
            EXPECT_EQ(failing.Calls, call);
            EXPECT_EQ(failing.MinLiveBytes, live + fixed);
            EXPECT_EQ(failing.MaxLiveBytes, live + fixed);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
            EXPECT_EQ(Allocator.Allocations, allocations);
        }
    }

    TEST_F(CookManifestWriterTest, FixedStateRefusalPrecedesAllWritesAndPreservesOutputWithoutAllocations)
    {
        AddScene();
        FinalizeEmpty();
        const auto live        = Allocator.Statistics().LiveBytes;
        const auto allocations = Allocator.Allocations;
        for (auto& word : Workspace)
        {
            word = UINT32_MAX;
        }
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            ManifestSink       sink   = {};
            CookManifestOutput output = {.Digest = Repeated(0xaa), .Bytes = 999};
            const auto         calls  = WriterAllocator.ChargeCalls;
            WriterAllocator.Failure   = error;
            EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), error);
            EXPECT_EQ(WriterAllocator.ChargeCalls, calls + 1);
            EXPECT_EQ(WriterAllocator.Category, CookMemoryCategory::StagingManifest);
            EXPECT_GT(WriterAllocator.Bytes, sizeof(Core::Hash::SHA256));
            EXPECT_EQ(sink.Calls, 0u);
            EXPECT_EQ(output.Digest, Repeated(0xaa));
            EXPECT_EQ(output.Bytes, 999u);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
            EXPECT_EQ(Allocator.Allocations, allocations);
            for (const auto word : Workspace)
            {
                EXPECT_EQ(word, UINT32_MAX);
            }
        }
    }

    TEST_F(CookManifestWriterTest, ExactFixedStateBudgetRemainsChargedThroughEmissionAndDrainsOnAllExits)
    {
        AddScene();
        FinalizeEmpty();
        ManifestSink       measured = {};
        CookManifestOutput expected = {};
        ASSERT_TRUE(WriteCookManifest(Input, Workspace, measured, &expected, WriterAllocator).Succeeded());
        const auto fixed       = WriterAllocator.Bytes;
        const auto live        = Allocator.Statistics().LiveBytes;
        const auto allocations = Allocator.Allocations;
        ASSERT_GT(fixed, 0u);
        ASSERT_LE(live + fixed, Limits.MaxWorkingBytes);
        for (const uint64_t available : {fixed - 1, fixed})
        {
            CookMemoryCharge blocker = {};
            ASSERT_TRUE(Allocator.Ledger.TryCharge(Limits.MaxWorkingBytes - live - available, CookMemoryCategory::Codecs, &blocker).Succeeded());
            ManifestSink       sink   = {};
            CookManifestOutput output = {.Digest = Repeated(0xaa), .Bytes = 999};
            sink.Budget               = &Allocator;
            const auto baseline       = Allocator.Statistics().LiveBytes;
            const auto status         = WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator);
            if (available < fixed)
            {
                EXPECT_EQ(status.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(sink.Calls, 0u);
                EXPECT_EQ(output.Digest, Repeated(0xaa));
                EXPECT_EQ(output.Bytes, 999u);
            }
            else
            {
                EXPECT_TRUE(status.Succeeded());
                EXPECT_EQ(sink.Text(), Tests::ManifestGolden);
                EXPECT_EQ(output.Digest, expected.Digest);
                EXPECT_EQ(output.Bytes, expected.Bytes);
                EXPECT_EQ(sink.MinLiveBytes, baseline + fixed);
                EXPECT_EQ(sink.MaxLiveBytes, baseline + fixed);
            }
            EXPECT_EQ(Allocator.Statistics().LiveBytes, baseline);
            EXPECT_EQ(Allocator.Allocations, allocations);
            blocker.Release();
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        }
    }

    TEST_F(CookManifestWriterTest, InvalidSetupAndWorkspaceCannotWriteOrChangeResults)
    {
        AddScene();
        FinalizeEmpty();
        ManifestSink       sink   = {};
        CookManifestOutput output = {.Digest = Repeated(0xaa), .Bytes = 999};
        const auto         good   = Input;
        Input.Graph               = nullptr;
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::InvalidPath);
        Input        = good;
        Input.Scenes = {nullptr, 1};
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::InvalidPath);
        Input        = good;
        Input.Assets = {Assets, 1};
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::InvalidPath);
        Input                 = good;
        Input.Recipe.Revision = 2;
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Unsupported);
        Input               = good;
        Input.Recipe.Target = CookTarget::Invalid;
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Unsupported);
        Input                    = good;
        Input.Recipe.SettingsKey = {};
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, &output, WriterAllocator).Error(), VFS::VFSError::Corrupted);
        Input = good;
        EXPECT_EQ(WriteCookManifest(Input, {}, sink, &output, WriterAllocator).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(WriteCookManifest(Input, {nullptr, 1}, sink, &output, WriterAllocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(WriteCookManifest(Input, Workspace, sink, nullptr, WriterAllocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(WriterAllocator.ChargeCalls, 0u);
        EXPECT_EQ(sink.Calls, 0u);
        EXPECT_EQ(output.Digest, Repeated(0xaa));
        EXPECT_EQ(output.Bytes, 999u);
    }
} // namespace ZEngine::Cook
