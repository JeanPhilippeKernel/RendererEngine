#include <ZEngine/Cook/CookIncrementalReuse.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookStorageLayout.h>
#include <nlohmann/json.hpp>
#include <rapidhash.h>
#include <cstdio>
#include "CookObjectTestSupport.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        Digest256 ReuseDigest(uint8_t byte)
        {
            Digest256 result = {};
            std::fill(result.Bytes.begin(), result.Bytes.end(), byte);
            return result;
        }

        CookAssetIdentity ReuseIdentity(uint32_t id)
        {
            char text[37] = {};
            std::snprintf(text, sizeof(text), "00000000-0000-4000-8000-%012x", id);
            constexpr CookAssetKind kinds[] = {CookAssetKind::Unknown, CookAssetKind::Texture, CookAssetKind::Material, CookAssetKind::Mesh, CookAssetKind::EnvironmentMap, CookAssetKind::Texture};
            return {id < 6 ? kinds[id] : CookAssetKind::Unknown, uuids::uuid::from_string(text).value()};
        }

        struct ReuseSink final : ICookByteSink
        {
            uint8_t              Bytes[16384] = {};
            size_t               Count        = 0;

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (bytes.size() > sizeof(Bytes) - Count)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                if (bytes.size())
                {
                    (void) Helpers::secure_memcpy(Bytes + Count, sizeof(Bytes) - Count, bytes.data(), bytes.size());
                }
                Count += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
            Core::Containers::ArrayView<const uint8_t> View() const
            {
                return {Bytes, Count};
            }
        };

        struct ReuseAllocator final : ICookAllocator
        {
            ICookAllocator*      Backing      = nullptr;
            VFS::VFSError        Failure      = VFS::VFSError::OK;
            VFS::VFSError        FixedFailure = VFS::VFSError::OK;
            uint32_t             FixedCalls   = 0;
            uint32_t             FixedFailAt  = UINT32_MAX;
            uint32_t             Calls        = 0;
            uint32_t             Frees        = 0;
            bool                 Null         = false;
            bool                 Misalign     = false;

            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++FixedCalls;
                return FixedFailure != VFS::VFSError::OK && (FixedFailAt == UINT32_MAX || FixedCalls == FixedFailAt) ? VFS::VFSResult<void>::Fail(FixedFailure) : Backing->TryCharge(bytes, category, output);
            }

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                ++Calls;
                if (Failure != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<void*>::Fail(Failure);
                }
                if (Null)
                {
                    return VFS::VFSResult<void*>::Ok(nullptr);
                }
                auto result = Backing->Allocate(bytes + (Misalign ? 1 : 0), alignment, category);
                if (result.Succeeded() && Misalign)
                {
                    return VFS::VFSResult<void*>::Ok(static_cast<uint8_t*>(result.Value()) + 1);
                }
                return result;
            }
            void Free(void* pointer) noexcept override
            {
                ++Frees;
                Backing->Free(Misalign ? static_cast<uint8_t*>(pointer) - 1 : pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Backing->Statistics();
            }
        };

        struct GraphVariant
        {
            bool ChangedTexture = false;
            bool OneSlot        = false;
            bool SharedTexture  = false;
            bool Moved          = false;
            bool Empty          = false;
            bool Tga            = false;
        };
    } // namespace

    class CookIncrementalReuseTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(256), 1> Parent            = {};
        CookScratchAllocator                 Allocator         = {};
        ReuseAllocator                       Routed            = {};
        CookLimits                           Limits            = {.MaxWorkingBytes = ZKilo(512)};
        CookRecipeKeys                       Recipe            = {.Target = CookTarget::DesktopVulkanLE64, .SchemaKey = ReuseDigest(0x77)};
        CookDependencyGraph                  Graph             = {};
        CookManifest                         Previous          = {};
        Tests::ObjectStore                   Store             = {};
        CookIncrementalReuse                 Reuse             = {};
        CookAssetResult                      OriginalAssets[5] = {};
        CookSceneResult                      OriginalScenes[2] = {};
        uint32_t                             StoredAssets[6]   = {};
        uint32_t                             StoredScenes[2]   = {};
        uint32_t                             Workspace[16]     = {};
        uint8_t                              Stream[7]         = {};
        ReuseSink                            Original          = {};
        CookManifestOutput                   OriginalOutput    = {};
        CookDiagnostics                      Diagnostics       = {};

        void                                 SetUp() override
        {
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Allocator.Initialize(Parent, ZKilo(256), CookMemoryCategory::CatalogGraph).Succeeded());
            Routed.Backing = &Allocator;
            ASSERT_TRUE(ComputeCookSettingsKey(Recipe.Target, &Recipe.SettingsKey).Succeeded());
            for (uint32_t id = 1; id <= 4; ++id)
            {
                uint8_t bytes[32] = {};
                std::fill(std::begin(bytes), std::end(bytes), static_cast<uint8_t>(id));
                constexpr cstring extensions[] = {nullptr, "png", "zematerial", "zemesh", "zenvmap"};
                StoredAssets[id]               = Store.Add(bytes, extensions[id]);
            }
            StoredAssets[5] = StoredAssets[1];
            for (uint32_t i = 0; i < 2; ++i)
            {
                const uint8_t bytes[] = {0, 1, static_cast<uint8_t>(i)};
                StoredScenes[i]       = Store.Add(bytes, CookSceneObjectExtension);
            }
            Build();
            SavePrevious();
        }

        void TearDown() override
        {
            Reuse.Reset();
            Previous.Reset();
            Graph.Reset();
            EXPECT_EQ(Allocator.OutstandingAllocations(), 0u);
            Allocator.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
        }

        uint32_t Index(uint32_t id) const
        {
            return Graph.Find(ReuseIdentity(id));
        }

        const CookObjectDescriptor& Object(uint32_t id) const
        {
            return Store.Entries[StoredAssets[id]].Object;
        }

        void Build(GraphVariant variant = {})
        {
            Graph.Reset();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            ASSERT_TRUE(Graph.AddScene(ReuseIdentity(100).UUID, VFS::VFSRelativePath::Parse(variant.Moved ? "scenes/moved.yaml" : "scenes/main.yaml").Value()).Succeeded());
            ASSERT_TRUE(Graph.AddScene(ReuseIdentity(101).UUID, VFS::VFSRelativePath::Parse("scenes/sky.yaml").Value()).Succeeded());
            if (!variant.Empty)
            {
                ASSERT_TRUE(Graph.AddRoot(0, ReuseIdentity(3), {}, "Test", "mesh").Succeeded());
                ASSERT_TRUE(Graph.AddRoot(1, ReuseIdentity(4), {}, "Test", "environment").Succeeded());
                if (variant.SharedTexture)
                {
                    ASSERT_TRUE(Graph.AddRoot(0, ReuseIdentity(5), {}, "Test", "texture").Succeeded());
                }
            }
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            if (!variant.Empty)
            {
                const CookDependency mesh = {ReuseIdentity(2), "submeshes/0/material"};
                Inspect(3, "zemesh-v1-le64", {&mesh, 1});
                const CookDependency textures[] = {
                    {ReuseIdentity(1), "albedo"},
                    {ReuseIdentity(1), "normal"}
                };
                Inspect(2, "zematerial-v1-json", {textures, variant.OneSlot ? 1u : 2u});
                Inspect(1, "image-png", {}, variant.ChangedTexture);
                Inspect(4, "zenvmap-v2-le64");
                if (variant.SharedTexture)
                {
                    Inspect(5, variant.Tga ? "image-tga" : "image-png");
                }
            }
            ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        }

        void Inspect(uint32_t id, cstring format, Core::Containers::ArrayView<const CookDependency> dependencies = {}, bool changed = false)
        {
            const CookAssetInspectionView view = {format, ReuseDigest(static_cast<uint8_t>(id + (changed ? 16 : 0))), ReuseDigest(static_cast<uint8_t>(id + 32)), dependencies};
            ASSERT_TRUE(Graph.SetInspection(Index(id), 1, view).Succeeded());
        }

        void SavePrevious()
        {
            Original.Count = 0;
            for (uint32_t node : Graph.LeafFirstNodes())
            {
                auto&          result = OriginalAssets[node];
                const uint32_t id     = static_cast<uint8_t>(Graph.Node(node)->Identity.UUID.as_bytes()[15]);
                ASSERT_TRUE(ComputeCookAssetInputKey(Allocator, Graph, node, Recipe, {OriginalAssets, Graph.NodeCount()}, &result.InputKey).Succeeded());
                result.Object = Object(id);
            }
            for (uint32_t scene = 0; scene < Graph.SceneCount(); ++scene)
            {
                auto& result        = OriginalScenes[scene];
                result.SourceDigest = ReuseDigest(static_cast<uint8_t>(90 + scene));
                result.Object       = Store.Entries[StoredScenes[scene]].Object;
                ASSERT_TRUE(ComputeCookSceneInputKey(Allocator, Graph, scene, result.SourceDigest, Recipe, {OriginalAssets, Graph.NodeCount()}, Workspace, &result.InputKey).Succeeded());
            }
            const CookManifestInput input = {
                &Graph, Recipe, {OriginalAssets, Graph.NodeCount()},
                  OriginalScenes, &Limits
            };
            ASSERT_TRUE(WriteCookManifest(input, Workspace, Original, &OriginalOutput, Allocator).Succeeded());
            ASSERT_EQ(Previous.Read(Original.View(), Recipe.Target, Limits, Allocator, nullptr).Value(), CookManifestReadStatus::Valid);
        }

        void Start(bool force = false, const CookManifest* previous = nullptr)
        {
            const CookReuseInput input = {&Graph, Recipe, previous ? previous : &Previous, Store.Reader(), &Limits, force};
            ASSERT_TRUE(Reuse.Initialize(Routed, input, Stream).Succeeded());
        }

        void ExpectAsset(uint32_t id, CookReuseDecision decision)
        {
            const auto prepared = Reuse.PrepareAsset(Index(id), &Diagnostics);
            ASSERT_TRUE(prepared.Succeeded()) << static_cast<uint32_t>(prepared.Error());
            EXPECT_EQ(prepared.Value(), decision);
        }

        void FinishAssets()
        {
            for (uint32_t node : Graph.LeafFirstNodes())
            {
                const auto prepared = Reuse.PrepareAsset(node, &Diagnostics);
                ASSERT_TRUE(prepared.Succeeded());
                if (prepared.Value() == CookReuseDecision::Encode)
                {
                    const uint32_t id = static_cast<uint8_t>(Graph.Node(node)->Identity.UUID.as_bytes()[15]);
                    ASSERT_TRUE(Reuse.AcceptInstalledAsset(node, Object(id), &Diagnostics).Succeeded());
                }
            }
        }
    };

    TEST_F(CookIncrementalReuseTest, VerifiedUnchangedResultsReproduceTheExactCanonicalManifestWithoutMoreAllocations)
    {
        Start();
        EXPECT_EQ(Routed.Calls, 1u);
        Routed.Failure  = VFS::VFSError::OutOfMemory;
        const auto live = Allocator.Statistics().LiveBytes;
        FinishAssets();
        for (uint32_t i = 0; i < 2; ++i)
        {
            ASSERT_EQ(Reuse.PrepareScene(i, OriginalScenes[i].SourceDigest, &Diagnostics).Value(), CookReuseDecision::Ready);
            EXPECT_TRUE(Reuse.WasSceneReused(i));
        }
        for (uint32_t node = 0; node < Graph.NodeCount(); ++node)
        {
            EXPECT_TRUE(Reuse.WasAssetReused(node));
            EXPECT_EQ(Reuse.AssetResults()[node].Object, OriginalAssets[node].Object);
            EXPECT_EQ(Reuse.AssetResults()[node].InputKey, OriginalAssets[node].InputKey);
        }
        EXPECT_TRUE(Reuse.AllReady());
        EXPECT_EQ(Store.Opens, 6u);
        ReuseSink               sink   = {};
        CookManifestOutput      output = {};
        const CookManifestInput input  = {&Graph, Recipe, Reuse.AssetResults(), Reuse.SceneResults(), &Limits};
        ASSERT_TRUE(WriteCookManifest(input, Workspace, sink, &output, Allocator).Succeeded());
        EXPECT_EQ(sink.Count, Original.Count);
        EXPECT_EQ(Helpers::secure_memcmp(sink.Bytes, sizeof(sink.Bytes), Original.Bytes, sizeof(Original.Bytes), sink.Count), 0);
        EXPECT_EQ(output.Digest, OriginalOutput.Digest);
        EXPECT_EQ(Routed.Calls, 1u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    TEST_F(CookIncrementalReuseTest, FixedKeyAndVerificationRefusalsAreResourceFailuresNotCacheMissesOrReadyResults)
    {
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            for (uint32_t phase : {1u, 2u})
            {
                SCOPED_TRACE(phase);
                Start();
                const auto live     = Parent.Statistics().LiveBytes;
                const auto calls    = Routed.Calls;
                const auto opens    = Store.Opens;
                const auto previous = Reuse.AssetResults()[Index(1)];
                Diagnostics         = {};
                Routed.FixedFailure = error;
                Routed.FixedFailAt  = Routed.FixedCalls + phase;
                EXPECT_EQ(Reuse.PrepareAsset(Index(1), &Diagnostics).Error(), error);
                EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
                EXPECT_FALSE(Reuse.WasAssetReused(Index(1)));
                EXPECT_EQ(Reuse.AssetResults()[Index(1)].Object, previous.Object);
                EXPECT_EQ(Reuse.AssetResults()[Index(1)].InputKey, previous.InputKey);
                EXPECT_EQ(Store.Opens, opens);
                EXPECT_EQ(Parent.Statistics().LiveBytes, live);
                EXPECT_EQ(Routed.Calls, calls);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
                Routed.FixedFailure = VFS::VFSError::OK;
                EXPECT_EQ(Reuse.PrepareAsset(Index(1), nullptr).Value(), CookReuseDecision::Ready);
                EXPECT_EQ(Store.Opens, opens + 1);
                Reuse.Reset();
            }
        }
    }

    TEST_F(CookIncrementalReuseTest, SceneKeyReservationRefusalPreservesStateAndDoesNotOpenCachedObjects)
    {
        Start();
        for (uint32_t id : {1u, 2u, 3u, 4u})
        {
            ExpectAsset(id, CookReuseDecision::Ready);
        }
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            const auto live        = Parent.Statistics().LiveBytes;
            const auto opens       = Store.Opens;
            const auto allocations = Routed.Calls;
            const auto previous    = Reuse.SceneResults()[0];
            Diagnostics            = {};
            Routed.FixedFailure    = error;
            EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, &Diagnostics).Error(), error);
            EXPECT_FALSE(Reuse.IsSceneReady(0));
            EXPECT_EQ(Reuse.SceneResults()[0].InputKey, previous.InputKey);
            EXPECT_EQ(Reuse.SceneResults()[0].SourceDigest, previous.SourceDigest);
            EXPECT_EQ(Reuse.SceneResults()[0].Object, previous.Object);
            EXPECT_EQ(Store.Opens, opens);
            EXPECT_EQ(Parent.Statistics().LiveBytes, live);
            EXPECT_EQ(Routed.Calls, allocations);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
        }
        Routed.FixedFailure = VFS::VFSError::OK;
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
    }

    TEST_F(CookIncrementalReuseTest, MissingChildRegeneratedIdenticallyAllowsParentReuseAndMissesAreNotCached)
    {
        Store.Entries[StoredAssets[1]].OpenError = VFS::VFSError::NotFound;
        Start();
        ExpectAsset(1, CookReuseDecision::Encode);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].Object.Size, 0u);
        EXPECT_EQ(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Error(), VFS::VFSError::NotFound);
        Store.Entries[StoredAssets[1]].OpenError = VFS::VFSError::OK;
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), Object(1), &Diagnostics).Succeeded());
        EXPECT_FALSE(Reuse.WasAssetReused(Index(1)));
        ExpectAsset(2, CookReuseDecision::Ready);
        EXPECT_TRUE(Reuse.WasAssetReused(Index(2)));
        ExpectAsset(3, CookReuseDecision::Ready);
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, &Diagnostics).Value(), CookReuseDecision::Ready);
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 3u);
    }

    TEST_F(CookIncrementalReuseTest, ExistingCorruptionFailsWithoutChangingResultsAndReportsTheExactObjectPath)
    {
        Store.Entries[StoredAssets[1]].Bytes[0] ^= 1;
        Start();
        const auto prepared = Reuse.PrepareAsset(Index(1), &Diagnostics);
        EXPECT_EQ(prepared.Error(), VFS::VFSError::Corrupted);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].InputKey, Digest256{});
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].Object, CookObjectDescriptor{});
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::CorruptObject);
        EXPECT_EQ(Diagnostics.Entries[0].Source, Object(1).Path);
        EXPECT_EQ(Diagnostics.Entries[0].Asset, ReuseIdentity(1));
        EXPECT_EQ(Store.Entries[StoredAssets[1]].File.Closes, 1u);
    }

    TEST_F(CookIncrementalReuseTest, ChangedChildKeyInvalidatesOnlyItsParentsAndSceneNotTheUnrelatedBranch)
    {
        Build({.ChangedTexture = true});
        Start();
        ExpectAsset(4, CookReuseDecision::Ready);
        EXPECT_EQ(Reuse.PrepareScene(1, OriginalScenes[1].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
        for (uint32_t id : {1u, 2u, 3u})
        {
            ExpectAsset(id, CookReuseDecision::Encode);
            ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(id), Object(id), nullptr).Succeeded());
            EXPECT_FALSE(Reuse.WasAssetReused(Index(id)));
        }
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Encode);
        EXPECT_FALSE(Reuse.AllReady());
        ASSERT_TRUE(Reuse.AcceptInstalledScene(0, Store.Entries[StoredScenes[0]].Object, nullptr).Succeeded());
        EXPECT_TRUE(Reuse.AllReady());
    }

    TEST_F(CookIncrementalReuseTest, ChangedChildObjectInvalidatesParentEvenWhenTheChildInputKeyIsUnchanged)
    {
        Store.Entries[StoredAssets[1]].OpenError = VFS::VFSError::NotFound;
        Start();
        ExpectAsset(1, CookReuseDecision::Encode);
        constexpr uint8_t changed[]   = {8, 7, 6, 5, 4};
        const auto        replacement = Store.Entries[Store.Add(changed, "png")].Object;
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), replacement, nullptr).Succeeded());
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].InputKey, OriginalAssets[Index(1)].InputKey);
        ExpectAsset(2, CookReuseDecision::Encode);
        EXPECT_NE(Reuse.AssetResults()[Index(2)].InputKey, OriginalAssets[Index(2)].InputKey);
    }

    TEST_F(CookIncrementalReuseTest, MatchingKeysCannotBypassPreviousEdgeOrChildRecordValidation)
    {
        auto ledger = nlohmann::json::parse(Original.Bytes, Original.Bytes + Original.Count);
        for (auto& asset : ledger["assets"])
        {
            if (asset["kind"] == "material")
            {
                asset["dependencies"][0]["slot"] = "emissive";
            }
        }
        const auto text = ledger.dump();
        ASSERT_EQ(Previous.Read({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, Recipe.Target, Limits, Allocator, nullptr).Value(), CookManifestReadStatus::Valid);
        Start();
        ExpectAsset(1, CookReuseDecision::Ready);
        ExpectAsset(2, CookReuseDecision::Encode);
        // Identical newly encoded bytes do not authorize the scene against an
        // old ledger whose material edges disagree, even if all keys match.
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(2), Object(2), nullptr).Succeeded());
        ExpectAsset(3, CookReuseDecision::Ready);
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Encode);
    }

    TEST_F(CookIncrementalReuseTest, MatchingParentKeyCannotHideAnInconsistentOldChildKey)
    {
        auto ledger = nlohmann::json::parse(Original.Bytes, Original.Bytes + Original.Count);
        for (auto& asset : ledger["assets"])
        {
            if (asset["kind"] == "texture")
            {
                asset["input_key"] = ReuseDigest(0xee).Hex().data();
            }
        }
        const auto text = ledger.dump();
        ASSERT_EQ(Previous.Read({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, Recipe.Target, Limits, Allocator, nullptr).Value(), CookManifestReadStatus::Valid);
        Start();
        ExpectAsset(1, CookReuseDecision::Encode);
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Succeeded());
        ExpectAsset(2, CookReuseDecision::Encode);
        EXPECT_EQ(Reuse.AssetResults()[Index(2)].InputKey, OriginalAssets[Index(2)].InputKey);
    }

    TEST_F(CookIncrementalReuseTest, SchemaChangesInvalidateScenesButNotAssets)
    {
        Recipe.SchemaKey = ReuseDigest(0xaa);
        Start();
        FinishAssets();
        for (uint32_t i = 0; i < Graph.NodeCount(); ++i)
        {
            EXPECT_TRUE(Reuse.WasAssetReused(i));
        }
        for (uint32_t i = 0; i < 2; ++i)
        {
            EXPECT_EQ(Reuse.PrepareScene(i, OriginalScenes[i].SourceDigest, nullptr).Value(), CookReuseDecision::Encode);
        }
        EXPECT_EQ(Store.Opens, 4u);
    }

    TEST_F(CookIncrementalReuseTest, MovingTheLogicalSceneSourceDoesNotInvalidateObjectReuse)
    {
        Build({.Moved = true});
        Start();
        FinishAssets();
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
        ReuseSink          sink   = {};
        CookManifestOutput output = {};
        EXPECT_EQ(Reuse.PrepareScene(1, OriginalScenes[1].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
        ASSERT_TRUE(WriteCookManifest({&Graph, Recipe, Reuse.AssetResults(), Reuse.SceneResults(), &Limits}, Workspace, sink, &output, Allocator).Succeeded());
        EXPECT_NE(output.Digest, OriginalOutput.Digest);
        EXPECT_TRUE(Reuse.WasSceneReused(0));
    }

    TEST_F(CookIncrementalReuseTest, SharedPhysicalObjectsAreVerifiedOnlyOncePerLockedRun)
    {
        Build({.SharedTexture = true});
        SavePrevious();
        Start();
        FinishAssets();
        EXPECT_TRUE(Reuse.WasAssetReused(Index(1)));
        EXPECT_TRUE(Reuse.WasAssetReused(Index(5)));
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 1u);
        EXPECT_EQ(Store.Entries[StoredAssets[1]].File.Closes, 1u);
        EXPECT_EQ(Store.Opens, 4u);
    }

    TEST_F(CookIncrementalReuseTest, OpenAddressCollisionsDoNotAliasDifferentPhysicalObjects)
    {
        // Five assets and two scenes use sixteen slots (load <= 1/2). Find a
        // real collision without assuming a particular platform's hash result.
        uint32_t seen[16] = {};
        std::fill(std::begin(seen), std::end(seen), UINT32_MAX);
        uint8_t first = 0, second = 0;
        bool    found = false;
        for (uint32_t candidate = 0; candidate <= 16; ++candidate)
        {
            const uint8_t      byte   = static_cast<uint8_t>(candidate);
            Core::Hash::SHA256 hash   = {};
            Digest256          digest = {};
            ASSERT_TRUE(hash.Update({&byte, 1}).Succeeded());
            ASSERT_TRUE(hash.Finalize(&digest).Succeeded());
            const uint32_t slot = static_cast<uint32_t>(rapidhash_withSeed(digest.Bytes.data(), digest.Bytes.size(), rapidhash("png", 3))) & 15;
            if (seen[slot] != UINT32_MAX)
            {
                first  = static_cast<uint8_t>(seen[slot]);
                second = byte;
                found  = true;
                break;
            }
            seen[slot] = candidate;
        }
        ASSERT_TRUE(found);
        StoredAssets[1] = Store.Add({&first, 1}, "png");
        StoredAssets[5] = Store.Add({&second, 1}, "png");
        Build({.SharedTexture = true});
        SavePrevious();
        Start();
        ExpectAsset(1, CookReuseDecision::Ready);
        ExpectAsset(5, CookReuseDecision::Ready);
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].Object, Object(1));
        EXPECT_EQ(Reuse.AssetResults()[Index(5)].Object, Object(5));
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 1u);
        EXPECT_EQ(Store.Entries[StoredAssets[5]].Opens, 1u);
        ExpectAsset(1, CookReuseDecision::Ready);
        ExpectAsset(5, CookReuseDecision::Ready);
        EXPECT_EQ(Store.Opens, 2u);
    }

    TEST_F(CookIncrementalReuseTest, IdenticalDigestsWithDifferentExtensionsStillRequireSeparatePhysicalVerification)
    {
        StoredAssets[5] = Store.Add(Store.Entries[StoredAssets[1]].File.Bytes, "tga");
        Build({.SharedTexture = true, .Tga = true});
        SavePrevious();
        Store.Entries[StoredAssets[5]].Bytes[0] ^= 1;
        Start();
        ExpectAsset(1, CookReuseDecision::Ready);
        EXPECT_EQ(Reuse.PrepareAsset(Index(5), nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 1u);
        EXPECT_EQ(Store.Entries[StoredAssets[5]].Opens, 1u);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(5)));
    }

    TEST_F(CookIncrementalReuseTest, AVerifiedPathCannotBeAcceptedWithAConflictingSizeWithoutReopeningIt)
    {
        Build({.SharedTexture = true});
        Start(true);
        ExpectAsset(1, CookReuseDecision::Encode);
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Succeeded());
        ExpectAsset(5, CookReuseDecision::Encode);
        auto conflict = Object(1);
        ++conflict.Size;
        EXPECT_EQ(Reuse.AcceptInstalledAsset(Index(5), conflict, nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(5)));
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 1u);
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(5), Object(1), nullptr).Succeeded());
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 1u);
    }

    TEST_F(CookIncrementalReuseTest, ResetInvalidatesPhysicalVerificationRatherThanTrustingThePreviousRun)
    {
        Start();
        ExpectAsset(1, CookReuseDecision::Ready);
        Reuse.Reset();
        Store.Entries[StoredAssets[1]].Bytes[0] ^= 1;
        Start();
        EXPECT_EQ(Reuse.PrepareAsset(Index(1), nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Store.Entries[StoredAssets[1]].Opens, 2u);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
    }

    TEST_F(CookIncrementalReuseTest, MissingScenesMayBeEncodedButCannotBeReboundOrAcceptedWhileStillMissing)
    {
        auto& scene     = Store.Entries[StoredScenes[0]];
        scene.OpenError = VFS::VFSError::NotFound;
        Start();
        FinishAssets();
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Encode);
        const auto keyed = Reuse.SceneResults()[0];
        EXPECT_EQ(Reuse.AcceptInstalledScene(0, scene.Object, nullptr).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Reuse.PrepareScene(0, ReuseDigest(0x44), nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Reuse.SceneResults()[0].SourceDigest, keyed.SourceDigest);
        EXPECT_EQ(Reuse.SceneResults()[0].InputKey, keyed.InputKey);
        EXPECT_FALSE(Reuse.IsSceneReady(0));
        scene.OpenError = VFS::VFSError::OK;
        ASSERT_TRUE(Reuse.AcceptInstalledScene(0, scene.Object, nullptr).Succeeded());
        EXPECT_FALSE(Reuse.WasSceneReused(0));
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
        auto changed = scene.Object;
        ++changed.Size;
        EXPECT_EQ(Reuse.AcceptInstalledScene(0, changed, nullptr).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookIncrementalReuseTest, CorruptScenesAndCheckedCloseErrorsPreserveTheUnpreparedResult)
    {
        Start();
        FinishAssets();
        auto& scene     = Store.Entries[StoredScenes[0]];
        scene.Bytes[0] ^= 1;
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, &Diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Reuse.SceneResults()[0].SourceDigest, Digest256{});
        EXPECT_EQ(Diagnostics.Entries[0].Source, scene.Object.Path);
        EXPECT_EQ(Diagnostics.Entries[0].SceneUUID, Graph.Scene(0)->UUID);
        scene.Bytes[0]        ^= 1;
        scene.File.CloseError  = VFS::VFSError::IOError;
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Reuse.SceneResults()[0].InputKey, Digest256{});
        EXPECT_FALSE(Reuse.IsSceneReady(0));
        EXPECT_EQ(scene.Opens, 2u);
        scene.File.CloseError = VFS::VFSError::OK;
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
        EXPECT_EQ(scene.Opens, 3u);
    }

    TEST_F(CookIncrementalReuseTest, ANewSceneSourceDigestInvalidatesOnlyThatSceneWithoutOpeningItsOldObject)
    {
        Start();
        FinishAssets();
        EXPECT_EQ(Reuse.PrepareScene(0, ReuseDigest(0x55), nullptr).Value(), CookReuseDecision::Encode);
        EXPECT_NE(Reuse.SceneResults()[0].InputKey, OriginalScenes[0].InputKey);
        EXPECT_EQ(Store.Entries[StoredScenes[0]].Opens, 0u);
        EXPECT_EQ(Reuse.PrepareScene(1, OriginalScenes[1].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
    }

    TEST_F(CookIncrementalReuseTest, ForcedRebuildStillVerifiesInstalledObjectsAndCannotReplaceReadyResults)
    {
        Start(true);
        ExpectAsset(1, CookReuseDecision::Encode);
        EXPECT_EQ(Store.Opens, 0u);
        Store.Entries[StoredAssets[1]].Bytes[0] ^= 1;
        const auto keyed                         = Reuse.AssetResults()[Index(1)].InputKey;
        EXPECT_EQ(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].InputKey, keyed);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
        Store.Entries[StoredAssets[1]].Bytes[0] ^= 1;
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Succeeded());
        EXPECT_TRUE(Reuse.IsAssetReady(Index(1)));
        EXPECT_FALSE(Reuse.WasAssetReused(Index(1)));
        const uint32_t opens = Store.Opens;
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Succeeded());
        EXPECT_EQ(Store.Opens, opens);
        auto different = Object(1);
        ++different.Size;
        EXPECT_EQ(Reuse.AcceptInstalledAsset(Index(1), different, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Reuse.AssetResults()[Index(1)].Object, Object(1));
        FinishAssets();
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Encode);
        ASSERT_TRUE(Reuse.AcceptInstalledScene(0, Store.Entries[StoredScenes[0]].Object, nullptr).Succeeded());
        EXPECT_FALSE(Reuse.WasSceneReused(0));
    }

    TEST_F(CookIncrementalReuseTest, ParentsCannotBecomeReadyBeforeChildrenAndPreparedSceneSourcesCannotChange)
    {
        Start();
        EXPECT_EQ(Reuse.PrepareAsset(Index(2), &Diagnostics).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Reuse.AssetResults()[Index(2)].InputKey, Digest256{});
        EXPECT_EQ(Reuse.SceneResults()[0].SourceDigest, Digest256{});
        EXPECT_EQ(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Error(), VFS::VFSError::InvalidPath);
        FinishAssets();
        EXPECT_EQ(Reuse.PrepareScene(0, OriginalScenes[0].SourceDigest, nullptr).Value(), CookReuseDecision::Ready);
        const auto original = Reuse.SceneResults()[0];
        EXPECT_EQ(Reuse.PrepareScene(0, ReuseDigest(0x33), &Diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Reuse.SceneResults()[0].SourceDigest, original.SourceDigest);
        EXPECT_EQ(Reuse.SceneResults()[0].InputKey, original.InputKey);
        EXPECT_EQ(Reuse.SceneResults()[0].Object, original.Object);
        EXPECT_EQ(Diagnostics.Entries[Diagnostics.Count - 1].Code, CookDiagnosticCode::InputChanged);
        EXPECT_EQ(Reuse.PrepareAsset(UINT32_MAX, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Reuse.PrepareScene(UINT32_MAX, {}, nullptr).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookIncrementalReuseTest, CheckedCloseFailureCannotAuthorizeAResultOrPopulateTheVerifiedCache)
    {
        Start();
        auto& entry           = Store.Entries[StoredAssets[1]];
        entry.File.CloseError = VFS::VFSError::NotFound;
        EXPECT_EQ(Reuse.PrepareAsset(Index(1), &Diagnostics).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::IOFailure);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
        entry.File.CloseError = VFS::VFSError::OK;
        ExpectAsset(1, CookReuseDecision::Ready);
        EXPECT_EQ(entry.Opens, 2u);
    }

    TEST_F(CookIncrementalReuseTest, AllocationRefusalNullAndMisalignmentPreserveAnEmptyOwnerAndFreeRejectedCapacity)
    {
        const auto           live  = Allocator.Statistics().LiveBytes;
        const CookReuseInput input = {&Graph, Recipe, &Previous, Store.Reader(), &Limits};
        for (const auto failure : {VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded, VFS::VFSError::PermissionDenied})
        {
            Routed.Failure = failure;
            EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), failure);
            EXPECT_FALSE(Reuse.AllReady());
            EXPECT_EQ(Reuse.AssetResults().size(), 0u);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        }
        Routed.Failure = VFS::VFSError::OK;
        Routed.Null    = true;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::OutOfMemory);
        Routed.Null     = false;
        Routed.Misalign = true;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Routed.Frees, 1u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        Routed.Misalign = false;
        Start();
        ExpectAsset(1, CookReuseDecision::Ready);
        const auto count = Routed.Calls;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Routed.Calls, count);
        EXPECT_TRUE(Reuse.IsAssetReady(Index(1)));
        Reuse.Reset();
        Reuse.Reset();
        EXPECT_EQ(Routed.Frees, 2u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        EXPECT_FALSE(Reuse.IsAssetReady(Index(1)));
    }

    TEST_F(CookIncrementalReuseTest, EnforcesDescriptorSpellingAndPerKindLimitsBeforeOpening)
    {
        Limits.MaxMaterialBytes = 31;
        Start();
        ExpectAsset(1, CookReuseDecision::Ready);
        EXPECT_EQ(Reuse.PrepareAsset(Index(2), &Diagnostics).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Store.Entries[StoredAssets[2]].Opens, 0u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        Reuse.Reset();
        Limits.MaxMaterialBytes = 32;
        Start(true);
        ExpectAsset(1, CookReuseDecision::Encode);
        EXPECT_EQ(Reuse.AcceptInstalledAsset(Index(1), Object(2), nullptr).Error(), VFS::VFSError::Corrupted);
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(1), Object(1), nullptr).Succeeded());
        ExpectAsset(2, CookReuseDecision::Encode);
        ASSERT_TRUE(Reuse.AcceptInstalledAsset(Index(2), Object(2), nullptr).Succeeded());
    }

    TEST_F(CookIncrementalReuseTest, SceneObjectLimitIsIndependentOfTheAssetLimit)
    {
        Build({.Empty = true});
        Limits.MaxEncodedAssetBytes     = 0;
        Limits.SceneLimits.MaxFileBytes = 3;
        Start(true);
        EXPECT_EQ(Reuse.PrepareScene(0, ReuseDigest(1), nullptr).Value(), CookReuseDecision::Encode);
        ASSERT_TRUE(Reuse.AcceptInstalledScene(0, Store.Entries[StoredScenes[0]].Object, nullptr).Succeeded());
        EXPECT_TRUE(Reuse.IsSceneReady(0));
        EXPECT_FALSE(Reuse.AllReady());
        EXPECT_EQ(Reuse.PrepareScene(1, ReuseDigest(2), nullptr).Value(), CookReuseDecision::Encode);
        ASSERT_TRUE(Reuse.AcceptInstalledScene(1, Store.Entries[StoredScenes[1]].Object, nullptr).Succeeded());
        EXPECT_TRUE(Reuse.AllReady());
    }

    TEST_F(CookIncrementalReuseTest, InvalidSetupFailsBeforeAllocatingAndInvalidPreviousLedgerHasNoReuseAuthority)
    {
        CookReuseInput input = {&Graph, Recipe, &Previous, Store.Reader(), &Limits};
        input.Graph          = nullptr;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::InvalidPath);
        input.Graph           = &Graph;
        input.Recipe.Revision = CookRecipeRevision + 1;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::Unsupported);
        input.Recipe             = Recipe;
        input.Recipe.SettingsKey = {};
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::InvalidPath);
        input.Recipe      = Recipe;
        input.Reader.Open = nullptr;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::InvalidPath);
        input.Reader = Store.Reader();
        EXPECT_EQ(Reuse.Initialize(Routed, input, {}).Error(), VFS::VFSError::InvalidPath);
        Limits.MaxAssets = Graph.NodeCount() - 1;
        EXPECT_EQ(Reuse.Initialize(Routed, input, Stream).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxAssets = 8192;
        EXPECT_EQ(Routed.Calls, 0u);
        CookManifest invalid = {};
        Start(false, &invalid);
        ExpectAsset(1, CookReuseDecision::Encode);
        EXPECT_EQ(Store.Opens, 0u);
    }

    TEST(CookStorageLayoutTest, CheckedAlignmentAndOverflowPreserveThePreviousLayoutAndOffset)
    {
        Detail::CookStorageLayout layout = {};
        size_t                    offset = 100;
        ASSERT_TRUE(layout.Add<uint8_t>(1, &offset));
        EXPECT_EQ(offset, 0u);
        ASSERT_TRUE(layout.Add<uint64_t>(3, &offset));
        EXPECT_EQ(offset, 8u);
        EXPECT_EQ(layout.Size(), 32u);
        EXPECT_FALSE(layout.Add<uint64_t>(SIZE_MAX, &offset));
        EXPECT_EQ(offset, 8u);
        EXPECT_EQ(layout.Size(), 32u);
        EXPECT_FALSE(layout.Add<uint8_t>(1, nullptr));
        ASSERT_TRUE(layout.Add<uint8_t>(SIZE_MAX - layout.Size(), &offset));
        EXPECT_EQ(layout.Size(), SIZE_MAX);
        EXPECT_FALSE(layout.Add<uint64_t>(0, &offset));
        EXPECT_EQ(offset, 32u);
        EXPECT_EQ(layout.Size(), SIZE_MAX);
    }
} // namespace ZEngine::Cook
