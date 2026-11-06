#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookSort.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        CookAssetIdentity Identity(uint32_t number, CookAssetKind kind = CookAssetKind::Mesh)
        {
            char text[37] = {};
            std::snprintf(text, sizeof(text), "00000000-0000-0000-0000-%012x", number);
            return {kind, uuids::uuid::from_string(text).value()};
        }

        cstring Format(CookAssetKind kind)
        {
            switch (kind)
            {
                case CookAssetKind::Mesh:
                    return "zemesh-v1-le64";
                case CookAssetKind::Material:
                    return "zematerial-v1-json";
                case CookAssetKind::Texture:
                    return "image-png";
                case CookAssetKind::EnvironmentMap:
                    return "zenvmap-v2-le64";
                default:
                    return nullptr;
            }
        }

        struct GraphAllocator final : ICookAllocator
        {
            ICookAllocator*      Parent        = nullptr;
            uint32_t             Calls         = 0;
            uint32_t             FailAt        = UINT32_MAX;
            size_t               FirstRequest  = 0;
            uint32_t             ChargeCalls   = 0;
            uint64_t             ChargedBytes  = 0;
            VFS::VFSError        ChargeFailure = VFS::VFSError::OK;
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++ChargeCalls;
                ChargedBytes = bytes;
                EXPECT_EQ(category, CookMemoryCategory::CatalogGraph);
                return ChargeFailure == VFS::VFSError::OK ? Parent->TryCharge(bytes, category, output) : VFS::VFSResult<void>::Fail(ChargeFailure);
            }
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                if (!Calls)
                {
                    FirstRequest = bytes;
                }
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
        };
    } // namespace

    class CookDependencyGraphTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZMega(8), 1> Parent    = {};
        CookScratchAllocator               Scratch   = {};
        GraphAllocator                     Allocator = {};
        CookDependencyGraph                Graph     = {};
        CookLimits                         Limits    = {.MaxWorkingBytes = ZMega(8) + ZKilo(4)};

        void                               SetUp() override
        {
            // The retained slab and fixed helper storage have distinct charges.
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZMega(8), CookMemoryCategory::CatalogGraph).Succeeded());
            Allocator.Parent = &Scratch;
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        }
        void TearDown() override
        {
            Graph.Reset();
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
            EXPECT_EQ(Parent.Allocations, 1u);
            EXPECT_EQ(Parent.Frees, 1u);
        }
        uint32_t Scene(uint32_t id = 1)
        {
            const auto path  = std::string("scenes/") + std::to_string(id) + ".yaml";
            const auto added = Graph.AddScene(Identity(id).UUID, VFS::VFSRelativePath::Parse(path.c_str()).Value());
            EXPECT_TRUE(added.Succeeded());
            return added.Succeeded() ? added.Value() : UINT32_MAX;
        }
        void Root(uint32_t scene, const CookAssetIdentity& identity, uint32_t entity = 100)
        {
            ASSERT_TRUE(Graph.AddRoot(scene, identity, Identity(entity).UUID, "MeshComponent", "mesh_uuid").Succeeded());
        }
        void Inspect(const CookAssetIdentity& identity, Core::Containers::ArrayView<const CookDependency> dependencies = {})
        {
            const uint32_t index = Graph.Find(identity);
            ASSERT_NE(index, CookGraphInvalidIndex);
            CookAssetInspection inspection = {};
            Digest256           local = {}, metadata = {};
            local.Bytes[0]    = 1;
            metadata.Bytes[0] = 2;
            ASSERT_TRUE(inspection.Assign(Format(identity.Kind), local, metadata, dependencies, Scratch).Succeeded());
            ASSERT_TRUE(Graph.SetInspection(index, 1, inspection).Succeeded());
        }
        std::vector<CookAssetIdentity> Identities(Core::Containers::ArrayView<const uint32_t> nodes)
        {
            std::vector<CookAssetIdentity> result = {};
            for (uint32_t index : nodes)
            {
                result.push_back(Graph.Node(index)->Identity);
            }
            return result;
        }
    };

    TEST_F(CookDependencyGraphTest, InitializeDoesNotReserveHardCapsAndEmptySceneClosureNeedsNoScratch)
    {
        EXPECT_EQ(Allocator.Calls, 0u);
        const auto scene = Scene();
        EXPECT_LT(Allocator.FirstRequest, ZKilo(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const auto calls        = Allocator.Calls;
        Allocator.ChargeFailure = VFS::VFSError::Unsupported;
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        EXPECT_EQ(Allocator.ChargeCalls, 0u);
        Core::Containers::ArrayView<const uint32_t> closure = {};
        ASSERT_TRUE(Graph.SceneClosure(scene, {}, &closure).Succeeded());
        EXPECT_TRUE(closure.empty());
        EXPECT_TRUE(Graph.SceneRoots(scene).empty());
        EXPECT_TRUE(Graph.CanonicalNodes().empty());
        EXPECT_EQ(Allocator.Calls, calls);
    }

    TEST_F(CookDependencyGraphTest, FinalizationSharesOneSortQueueReservationAndRetriesAtItsExactBudget)
    {
        const auto scene = Scene();
        const auto mesh = Identity(3), material = Identity(2, CookAssetKind::Material), texture = Identity(1, CookAssetKind::Texture);
        Root(scene, mesh, 102);
        Root(scene, material, 103);
        Root(scene, mesh, 101);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency edges[] = {
            {texture, "albedo", {}}
        };
        Inspect(mesh, edges);
        Inspect(material);
        Inspect(texture);
        const auto         calls    = Allocator.Calls;
        const auto         retained = Parent.Statistics().LiveBytes;
        constexpr uint64_t fixed    = sizeof(Detail::CookSortState<uint32_t>) + sizeof(CookMemoryCharge);
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            Allocator.ChargeFailure = error;
            const auto charges      = Allocator.ChargeCalls;
            EXPECT_EQ(Graph.Finalize(nullptr).Error(), error);
            EXPECT_EQ(Allocator.ChargeCalls, charges + 1);
            EXPECT_EQ(Allocator.ChargedBytes, fixed);
            EXPECT_EQ(Parent.Statistics().LiveBytes, retained);
            EXPECT_FALSE(Graph.IsFinalized());
            EXPECT_TRUE(Graph.CanonicalNodes().empty());
            EXPECT_TRUE(Graph.LeafFirstNodes().empty());
            EXPECT_EQ(Graph.Scene(scene)->RootCount, 0u);
            EXPECT_EQ(Graph.NodeCount(), 3u);
            EXPECT_EQ(Graph.EdgeCount(), 1u);
            EXPECT_EQ(Graph.OccurrenceCount(), 3u);
        }
        Allocator.ChargeFailure = VFS::VFSError::OK;
        for (const uint64_t available : {fixed - 1, fixed})
        {
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(Parent.TryCharge(ZKilo(4) - available, CookMemoryCategory::CatalogGraph, &occupied).Succeeded());
            const auto before  = Parent.Statistics().LiveBytes;
            const auto charges = Allocator.ChargeCalls;
            const auto result  = Graph.Finalize(nullptr);
            EXPECT_EQ(result.Succeeded(), available == fixed);
            if (result.Failed())
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
            }
            EXPECT_EQ(Allocator.ChargeCalls, charges + 1);
            EXPECT_EQ(Parent.Statistics().LiveBytes, before);
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(Graph.IsFinalized(), available == fixed);
        }
        EXPECT_EQ(Identities(Graph.CanonicalNodes()), (std::vector<CookAssetIdentity>{material, mesh, texture}));
        EXPECT_EQ(Identities(Graph.LeafFirstNodes()), (std::vector<CookAssetIdentity>{material, texture, mesh}));
        EXPECT_EQ(Identities(Graph.SceneRoots(scene)), (std::vector<CookAssetIdentity>{material, mesh}));
        EXPECT_EQ(Parent.Statistics().LiveBytes, retained);
    }

    TEST_F(CookDependencyGraphTest, InvalidStateAndUninspectedNodesRefuseBeforeQueryingTheSortBudget)
    {
        Allocator.ChargeFailure = VFS::VFSError::Unsupported;
        EXPECT_EQ(Graph.Finalize(nullptr).Error(), VFS::VFSError::InvalidPath);
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        EXPECT_EQ(Graph.Finalize(nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.ChargeCalls, 0u);
        Inspect(Identity(1));
        EXPECT_EQ(Graph.Finalize(nullptr).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Allocator.ChargeCalls, 1u);
        EXPECT_FALSE(Graph.IsFinalized());
        Allocator.ChargeFailure = VFS::VFSError::OK;
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        EXPECT_EQ(Allocator.ChargeCalls, 2u);
    }

    TEST_F(CookDependencyGraphTest, SharedNodesKeepDistinctSlotsOccurrencesAndPerSceneRoots)
    {
        const auto first = Scene(1), second = Scene(2);
        const auto mesh = Identity(3), material = Identity(2, CookAssetKind::Material), texture = Identity(4, CookAssetKind::Texture), sky = Identity(1, CookAssetKind::EnvironmentMap);
        Root(first, mesh, 101);
        Root(first, mesh, 102);
        Root(second, material, 103);
        Root(second, sky, 104);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency mesh_edges[] = {
            {material, "submeshes/0/material", {}}
        };
        Inspect(mesh, mesh_edges);
        char                 path[]           = "textures/a.png";
        const CookDependency material_edges[] = {
            {texture, "albedo", path},
            {texture, "normal", path}
        };
        Inspect(material, material_edges);
        path[0] = 'X';
        Inspect(texture);
        Inspect(sky);
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        EXPECT_EQ(Graph.NodeCount(), 4u);
        EXPECT_EQ(Graph.EdgeCount(), 3u);
        EXPECT_EQ(Graph.OccurrenceCount(), 4u);
        EXPECT_EQ(Graph.SceneRoots(first).size(), 1u);
        EXPECT_EQ(Graph.SceneRoots(second).size(), 2u);
        EXPECT_EQ(Graph.Occurrence(0)->EntityUUID, Identity(101).UUID);
        EXPECT_EQ(Graph.Occurrence(1)->EntityUUID, Identity(102).UUID);
        const auto edges = Graph.Dependencies(Graph.Find(material));
        ASSERT_EQ(edges.size(), 2u);
        EXPECT_EQ(Graph.Slot(edges[0]), "albedo");
        EXPECT_EQ(Graph.Slot(edges[1]), "normal");
        EXPECT_EQ(Graph.OriginalSource(edges[0]), "textures/a.png");
        EXPECT_EQ(edges[0].Child, edges[1].Child);
        EXPECT_EQ(Identities(Graph.LeafFirstNodes()), (std::vector<CookAssetIdentity>{sky, texture, material, mesh}));
        EXPECT_EQ(Identities(Graph.CanonicalNodes()), (std::vector<CookAssetIdentity>{sky, material, mesh, texture}));
        uint32_t                                    workspace[8] = {};
        Core::Containers::ArrayView<const uint32_t> closure      = {};
        ASSERT_TRUE(Graph.SceneClosure(first, workspace, &closure).Succeeded());
        EXPECT_EQ(Identities(closure), (std::vector<CookAssetIdentity>{material, mesh, texture}));
        ASSERT_TRUE(Graph.SceneClosure(second, workspace, &closure).Succeeded());
        EXPECT_EQ(Identities(closure), (std::vector<CookAssetIdentity>{sky, material, texture}));
        CookDiagnostic diagnostic = {};
        ASSERT_TRUE(Graph.Describe(Graph.Find(texture), CookDiagnosticCode::InvalidAsset, "bad texture", &diagnostic).Succeeded());
        EXPECT_EQ(diagnostic.SceneUUID, Identity(2).UUID);
        EXPECT_EQ(diagnostic.EntityUUID, Identity(103).UUID);
        EXPECT_STREQ(diagnostic.SchemaKey, "MeshComponent");
        EXPECT_STREQ(diagnostic.FieldKey, "mesh_uuid");
    }

    TEST_F(CookDependencyGraphTest, DescribeReusesOutputAndClearsPreviousProvenanceWithoutAllocating)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency dependency = {Identity(2), "child", {}};
        Inspect(Identity(1), {&dependency, 1});
        Inspect(Identity(2));
        const auto     allocations = Allocator.Calls, charges = Allocator.ChargeCalls;
        CookDiagnostic diagnostic = {.Severity = CookSeverity::Warning, .Stage = CookStage::Cleanup, .Underlying = VFS::VFSError::IOError};
        for (auto& identity : diagnostic.Chain)
        {
            identity = Identity(99);
        }
        diagnostic.ChainCount     = 32;
        diagnostic.ChainTruncated = true;
        ASSERT_TRUE(diagnostic.SetMessage("old explanatory text"));
        ASSERT_TRUE(Graph.Describe(Graph.Find(Identity(2)), CookDiagnosticCode::InvalidAsset, diagnostic.Message + 4, &diagnostic).Succeeded());
        EXPECT_STREQ(diagnostic.Message, "explanatory text");
        EXPECT_EQ(diagnostic.Severity, CookSeverity::Error);
        EXPECT_EQ(diagnostic.Stage, CookStage::Discover);
        EXPECT_EQ(diagnostic.Code, CookDiagnosticCode::InvalidAsset);
        EXPECT_EQ(diagnostic.Underlying, VFS::VFSError::OK);
        EXPECT_EQ(diagnostic.ChainCount, 2u);
        EXPECT_FALSE(diagnostic.ChainTruncated);
        EXPECT_EQ(diagnostic.Chain[0], Identity(1));
        EXPECT_EQ(diagnostic.Chain[1], Identity(2));
        for (size_t i = 2; i < diagnostic.Chain.size(); ++i)
        {
            EXPECT_EQ(diagnostic.Chain[i], CookAssetIdentity{});
        }
        // Text may also alias a field that the new description overwrites.
        ASSERT_TRUE(Graph.Describe(Graph.Find(Identity(1)), CookDiagnosticCode::MissingDependency, diagnostic.FieldKey, &diagnostic).Succeeded());
        EXPECT_STREQ(diagnostic.Message, "mesh_uuid");
        EXPECT_EQ(diagnostic.Asset, Identity(1));
        EXPECT_EQ(diagnostic.SceneUUID, Identity(1).UUID);
        EXPECT_EQ(diagnostic.EntityUUID, Identity(100).UUID);
        EXPECT_EQ(diagnostic.Source.View(), "scenes/1.yaml");
        EXPECT_EQ(diagnostic.ChainCount, 1u);
        EXPECT_EQ(diagnostic.Chain[1], CookAssetIdentity{});
        EXPECT_EQ(Allocator.Calls, allocations);
        EXPECT_EQ(Allocator.ChargeCalls, charges);
    }

    TEST_F(CookDependencyGraphTest, DescribeFailuresPreserveEveryOutputByte)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        CookDiagnostic diagnostic = {};
        ASSERT_TRUE(Graph.Describe(Graph.Find(Identity(1)), CookDiagnosticCode::InvalidAsset, "original", &diagnostic).Succeeded());
        const std::string before(reinterpret_cast<const char*>(&diagnostic), sizeof(diagnostic));
        const char        invalid[] = {'b', 'a', 'd', static_cast<char>(0xff), '\0'};
        EXPECT_EQ(Graph.Describe(Graph.Find(Identity(1)), CookDiagnosticCode::MissingDependency, invalid, &diagnostic).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(&diagnostic), sizeof(diagnostic)), before);
        EXPECT_EQ(Graph.Describe(CookGraphInvalidIndex, CookDiagnosticCode::MissingDependency, "replacement", &diagnostic).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(&diagnostic), sizeof(diagnostic)), before);
        EXPECT_EQ(Graph.Describe(Graph.Find(Identity(1)), CookDiagnosticCode::MissingDependency, "replacement", nullptr).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookDependencyGraphTest, DescribeValidatesFullMessageBeforeUtf8BoundaryTruncation)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        CookDiagnostic diagnostic = {};
        std::string    message(254, 'x');
        message += "\xe2\x82\xac";
        ASSERT_TRUE(Graph.Describe(Graph.Find(Identity(1)), CookDiagnosticCode::InvalidAsset, message.c_str(), &diagnostic).Succeeded());
        EXPECT_EQ(std::string(diagnostic.Message), std::string(254, 'x'));
        EXPECT_EQ(diagnostic.Message[255], '\0');
        const std::string before(reinterpret_cast<const char*>(&diagnostic), sizeof(diagnostic));
        message += static_cast<char>(0xff);
        EXPECT_EQ(Graph.Describe(Graph.Find(Identity(1)), CookDiagnosticCode::MissingDependency, message.c_str(), &diagnostic).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(&diagnostic), sizeof(diagnostic)), before);
    }

    TEST_F(CookDependencyGraphTest, CanonicalAndLeafFirstOrdersIgnoreSceneAndInspectionDiscoveryOrder)
    {
        std::vector<CookAssetIdentity> canonical = {}, leaf_first = {};
        std::vector<uint32_t>          permutation(64);
        for (uint32_t i = 0; i < 64; ++i)
        {
            permutation[i] = i + 1;
        }
        std::mt19937 random(42);
        for (uint32_t run = 0; run < 16; ++run)
        {
            Graph.Reset();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            const auto scene = Scene();
            std::shuffle(permutation.begin(), permutation.end(), random);
            for (uint32_t id : permutation)
            {
                Root(scene, Identity(id));
            }
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            std::shuffle(permutation.begin(), permutation.end(), random);
            for (uint32_t id : permutation)
            {
                CookDependency edges[2] = {};
                size_t         count    = 0;
                if (id < 64)
                {
                    edges[count++] = {Identity(id + 1), "a", {}};
                }
                if (id < 63)
                {
                    edges[count++] = {Identity(id + 2), "b", {}};
                }
                Inspect(Identity(id), {edges, count});
            }
            ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
            if (!run)
            {
                canonical  = Identities(Graph.CanonicalNodes());
                leaf_first = Identities(Graph.LeafFirstNodes());
            }
            EXPECT_EQ(Identities(Graph.CanonicalNodes()), canonical);
            EXPECT_EQ(Identities(Graph.LeafFirstNodes()), leaf_first);
            EXPECT_EQ(leaf_first.front(), Identity(64));
            EXPECT_EQ(leaf_first.back(), Identity(1));
        }
    }

    TEST_F(CookDependencyGraphTest, LongChainUsesIterativeOrderingClosureAndBoundedProvenance)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        constexpr uint32_t count = 8192;
        for (uint32_t id = 1; id <= count; ++id)
        {
            const CookDependency next = {Identity(id + 1), "next", {}};
            Inspect(Identity(id), id == count ? Core::Containers::ArrayView<const CookDependency>{} : Core::Containers::ArrayView<const CookDependency>{&next, 1});
        }
        const auto calls = Allocator.Calls;
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        EXPECT_EQ(Allocator.Calls, calls);
        ASSERT_EQ(Graph.LeafFirstNodes().size(), count);
        EXPECT_EQ(Graph.Node(Graph.LeafFirstNodes()[0])->Identity, Identity(count));
        std::vector<uint32_t>                       workspace(count * 2);
        Core::Containers::ArrayView<const uint32_t> closure = {};
        ASSERT_TRUE(Graph.SceneClosure(scene, workspace, &closure).Succeeded());
        EXPECT_EQ(closure.size(), count);
        CookDiagnostic diagnostic = {};
        ASSERT_TRUE(Graph.Describe(Graph.Find(Identity(count)), CookDiagnosticCode::MissingDependency, "missing", &diagnostic).Succeeded());
        EXPECT_TRUE(diagnostic.ChainTruncated);
        EXPECT_EQ(diagnostic.ChainCount, 32u);
        EXPECT_EQ(diagnostic.Chain[0], Identity(1));
        EXPECT_EQ(diagnostic.Chain[31], Identity(count));
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookDependencyGraphTest, CyclesReportActualClosedCycleRatherThanItsAcyclicDependents)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency first = {Identity(5), "next", {}}, second = {Identity(7), "next", {}}, back = {Identity(5), "next", {}};
        Inspect(Identity(1), {&first, 1});
        Inspect(Identity(5), {&second, 1});
        Inspect(Identity(7), {&back, 1});
        CookDiagnostics diagnostics = {};
        EXPECT_EQ(Graph.Finalize(&diagnostics).Error(), VFS::VFSError::Corrupted);
        ASSERT_EQ(diagnostics.Count, 1u);
        const auto& entry = diagnostics.Entries[0];
        EXPECT_EQ(entry.Code, CookDiagnosticCode::DependencyCycle);
        EXPECT_EQ(entry.Asset, Identity(5));
        EXPECT_EQ(entry.SceneUUID, Identity(1).UUID);
        EXPECT_EQ(entry.ChainCount, 3u);
        EXPECT_EQ(entry.Chain[0], Identity(5));
        EXPECT_EQ(entry.Chain[1], Identity(7));
        EXPECT_EQ(entry.Chain[2], Identity(5));
        EXPECT_FALSE(Graph.IsFinalized());
        EXPECT_TRUE(Graph.LeafFirstNodes().empty());
        EXPECT_TRUE(Graph.Finalize(nullptr).Failed());
    }

    TEST_F(CookDependencyGraphTest, SelfCyclesFailWithoutDiagnosticsAllocation)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency edge = {Identity(1), "self", {}};
        Inspect(Identity(1), {&edge, 1});
        const auto calls = Allocator.Calls;
        EXPECT_EQ(Graph.Finalize(nullptr).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Allocator.Calls, calls);
    }

    TEST_F(CookDependencyGraphTest, NodeLimitCountsUniqueIdentitiesNotRepeatedSlotReferences)
    {
        Graph.Reset();
        Limits.MaxAssets = 2;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        std::vector<std::string>    slots(32);
        std::vector<CookDependency> edges(32);
        for (uint32_t i = 0; i < 32; ++i)
        {
            char slot[16] = {};
            std::snprintf(slot, sizeof(slot), "slot/%03u", i);
            slots[i] = slot;
            edges[i] = {
                Identity(2), {slots[i].data(), slots[i].size()},
                 {}
            };
        }
        Inspect(Identity(1), edges);
        Inspect(Identity(2));
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        EXPECT_EQ(Graph.NodeCount(), 2u);
        EXPECT_EQ(Graph.EdgeCount(), 32u);
    }

    TEST_F(CookDependencyGraphTest, RootOccurrencesAndEdgesRespectPrededuplicationLimit)
    {
        Graph.Reset();
        Limits.MaxReferenceOccurrences = 2;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        const auto scene = Scene();
        Root(scene, Identity(1));
        Root(scene, Identity(1), 101);
        EXPECT_EQ(Graph.AddRoot(scene, Identity(1), {}, "MeshComponent", "mesh_uuid").Error(), VFS::VFSError::SizeLimitExceeded);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        CookAssetInspection  inspection = {};
        const CookDependency edge       = {Identity(2), "next", {}};
        ASSERT_TRUE(inspection.Assign("zemesh-v1-le64", {}, {}, {&edge, 1}, Scratch).Succeeded());
        EXPECT_EQ(Graph.SetInspection(0, 1, inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.NodeCount(), 1u);
        EXPECT_EQ(Graph.EdgeCount(), 0u);
        EXPECT_FALSE(Graph.Node(0)->Inspected);
    }

    TEST_F(CookDependencyGraphTest, EveryInspectionGrowthRefusalRestoresLogicalIdentitySetAndAllowsRetry)
    {
        std::vector<std::string>    slots(12);
        std::vector<CookDependency> edges(12);
        for (uint32_t i = 0; i < 12; ++i)
        {
            char slot[16] = {};
            std::snprintf(slot, sizeof(slot), "slot/%03u", i);
            slots[i] = slot;
            edges[i] = {
                Identity(i + 2), {slots[i].data(), slots[i].size()},
                   "textures/a.png"
            };
        }
        CookAssetInspection inspection = {};
        ASSERT_TRUE(inspection.Assign("zemesh-v1-le64", {}, {}, edges, Scratch).Succeeded());
        uint32_t attempts = 0;
        for (uint32_t failure = 0; failure <= attempts; ++failure)
        {
            Graph.Reset();
            Allocator.FailAt = UINT32_MAX;
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            const auto scene = Scene();
            Root(scene, Identity(1));
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            const auto before = Allocator.Calls;
            if (failure)
            {
                Allocator.FailAt = before + failure;
            }
            const auto assigned = Graph.SetInspection(0, 1, inspection);
            if (!failure)
            {
                ASSERT_TRUE(assigned.Succeeded());
                attempts = Allocator.Calls - before;
                ASSERT_GT(attempts, 1u);
                continue;
            }
            SCOPED_TRACE(failure);
            ASSERT_TRUE(assigned.Failed());
            EXPECT_EQ(assigned.Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(Graph.NodeCount(), 1u);
            EXPECT_EQ(Graph.EdgeCount(), 0u);
            EXPECT_FALSE(Graph.Node(0)->Inspected);
            for (uint32_t i = 0; i < 12; ++i)
            {
                EXPECT_EQ(Graph.Find(Identity(i + 2)), CookGraphInvalidIndex);
            }
            Allocator.FailAt = UINT32_MAX;
            ASSERT_TRUE(Graph.SetInspection(0, 1, inspection).Succeeded());
            EXPECT_EQ(Graph.NodeCount(), 13u);
            EXPECT_EQ(Graph.EdgeCount(), 12u);
            EXPECT_EQ(Graph.OriginalSource(Graph.Dependencies(0)[0]), "textures/a.png");
        }
    }

    TEST_F(CookDependencyGraphTest, PartialNodeLimitFailureRollsBackInternedChildrenAndText)
    {
        Graph.Reset();
        Limits.MaxAssets = 2;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency edges[] = {
            {Identity(2), "a", {}},
            {Identity(3), "b", {}}
        };
        CookAssetInspection inspection = {};
        ASSERT_TRUE(inspection.Assign("zemesh-v1-le64", {}, {}, edges, Scratch).Succeeded());
        EXPECT_EQ(Graph.SetInspection(0, 1, inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.NodeCount(), 1u);
        EXPECT_EQ(Graph.EdgeCount(), 0u);
        EXPECT_EQ(Graph.Find(Identity(2)), CookGraphInvalidIndex);
        EXPECT_EQ(Graph.Find(Identity(3)), CookGraphInvalidIndex);
        Inspect(Identity(1), {edges, 1});
        Inspect(Identity(2));
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        EXPECT_EQ(Graph.Slot(Graph.Dependencies(0)[0]), "a");
    }

    TEST_F(CookDependencyGraphTest, CrossKindUUIDCollisionCannotFinalizeAndUsesExistingWorkspace)
    {
        const auto scene    = Scene();
        const auto mesh     = Identity(2, CookAssetKind::Mesh);
        const auto material = Identity(2, CookAssetKind::Material);
        Root(scene, mesh);
        Root(scene, material);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        Inspect(mesh);
        Inspect(material);
        CookDiagnostics diagnostics = {};
        const auto      calls       = Allocator.Calls;
        EXPECT_EQ(Graph.Finalize(&diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_FALSE(Graph.IsFinalized());
        EXPECT_EQ(Allocator.Calls, calls);
        ASSERT_EQ(diagnostics.Count, 1u);
        EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::IdentityConflict);
    }

    TEST_F(CookDependencyGraphTest, BorrowedInspectionsShareValidationAndDoNotAllocateAnInspectionOwner)
    {
        const auto scene = Scene();
        Root(scene, Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const auto calls = Allocator.Calls;
        EXPECT_TRUE(Graph.SetInspection(0, 1, CookAssetInspectionView{.Format = "zemesh-v1-le64"}).Succeeded());
        EXPECT_EQ(Allocator.Calls, calls);
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
    }

    TEST_F(CookDependencyGraphTest, InvalidStatesKindsRevisionsAndClosureScratchPreserveGraph)
    {
        EXPECT_TRUE(Graph.BeginDiscovery().Failed());
        const auto scene = Scene();
        Root(scene, Identity(1));
        EXPECT_TRUE(Graph.AddScene(Identity(1).UUID, VFS::VFSRelativePath::Parse("scenes/other.yaml").Value()).Failed());
        EXPECT_TRUE(Graph.AddRoot(scene, {}, {}, "Schema", "field").Failed());
        EXPECT_TRUE(Graph.AddRoot(scene, Identity(2), {}, "", "field").Failed());
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        EXPECT_TRUE(Graph.AddScene(Identity(2).UUID, VFS::VFSRelativePath::Parse("scenes/2.yaml").Value()).Failed());
        EXPECT_TRUE(Graph.Finalize(nullptr).Failed());
        CookAssetInspection inspection = {};
        EXPECT_TRUE(Graph.SetInspection(0, 1, inspection).Failed());
        ASSERT_TRUE(inspection.Assign("image-png", {}, {}, {}, Scratch).Succeeded());
        EXPECT_EQ(Graph.SetInspection(0, 1, inspection).Error(), VFS::VFSError::Corrupted);
        ASSERT_TRUE(inspection.Assign("zemesh-v1-le64", {}, {}, {}, Scratch).Succeeded());
        EXPECT_EQ(Graph.SetInspection(0, 2, inspection).Error(), VFS::VFSError::Unsupported);
        ASSERT_TRUE(Graph.SetInspection(0, 1, inspection).Succeeded());
        EXPECT_TRUE(Graph.SetInspection(0, 1, inspection).Failed());
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        uint32_t                                    workspace[2] = {7, 7};
        Core::Containers::ArrayView<const uint32_t> closure      = {workspace, 1};
        EXPECT_EQ(Graph.SceneClosure(scene, {workspace, 1}, &closure).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(closure.data(), workspace);
        EXPECT_EQ(closure.size(), 1u);
        EXPECT_EQ(workspace[0], 7u);
        EXPECT_TRUE(Graph.SceneClosure(99, workspace, &closure).Failed());
        EXPECT_TRUE(Graph.SceneClosure(scene, workspace, nullptr).Failed());
    }

    TEST_F(CookDependencyGraphTest, SceneAndEveryRootGrowthFailurePreserveLogicalRecordsAndAllowRetry)
    {
        Allocator.FailAt = Allocator.Calls + 1;
        EXPECT_EQ(Graph.AddScene(Identity(1).UUID, VFS::VFSRelativePath::Parse("scenes/1.yaml").Value()).Error(), VFS::VFSError::OutOfMemory);
        EXPECT_EQ(Graph.SceneCount(), 0u);
        for (uint32_t failure = 1; failure <= 2; ++failure)
        {
            Graph.Reset();
            Allocator.FailAt = UINT32_MAX;
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            const auto scene = Scene();
            Allocator.FailAt = Allocator.Calls + failure;
            EXPECT_EQ(Graph.AddRoot(scene, Identity(2), Identity(100).UUID, "MeshComponent", "mesh_uuid").Error(), VFS::VFSError::OutOfMemory);
            EXPECT_EQ(Graph.SceneCount(), 1u);
            EXPECT_EQ(Graph.NodeCount(), 0u);
            EXPECT_EQ(Graph.OccurrenceCount(), 0u);
            EXPECT_EQ(Graph.ReferenceCount(), 0u);
            Allocator.FailAt = UINT32_MAX;
            Root(scene, Identity(2));
            EXPECT_EQ(Graph.NodeCount(), 1u);
            EXPECT_EQ(Graph.ReferenceCount(), 1u);
        }
    }

    TEST_F(CookDependencyGraphTest, BorrowedRootIdentityEntityAndKeysAreCopiedBeforeGrowthInvalidatesThem)
    {
        const auto scene = Scene();
        for (uint32_t i = 1; i <= 4; ++i)
        {
            Root(scene, Identity(i), 100 + i);
        }
        const auto* node       = Graph.Node(0);
        const auto* occurrence = Graph.Occurrence(0);
        const auto  expected   = *occurrence;
        const auto  calls      = Allocator.Calls;
        ASSERT_TRUE(Graph.AddRoot(scene, node->Identity, occurrence->EntityUUID, occurrence->SchemaKey, occurrence->FieldKey).Succeeded());
        EXPECT_GT(Allocator.Calls, calls);
        ASSERT_EQ(Graph.OccurrenceCount(), 5u);
        EXPECT_EQ(Graph.Occurrence(4)->Node, expected.Node);
        EXPECT_EQ(Graph.Occurrence(4)->EntityUUID, expected.EntityUUID);
        EXPECT_STREQ(Graph.Occurrence(4)->SchemaKey, expected.SchemaKey);
        EXPECT_STREQ(Graph.Occurrence(4)->FieldKey, expected.FieldKey);
        EXPECT_EQ(Graph.NodeCount(), 4u);
    }

    TEST_F(CookDependencyGraphTest, FullDiagnosticBufferStillReportsCyclesAndGlobalIdentityConflicts)
    {
        for (bool conflict : {false, true})
        {
            Graph.Reset();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            const auto scene = Scene();
            Root(scene, Identity(1));
            if (conflict)
            {
                Root(scene, Identity(1, CookAssetKind::Material));
            }
            ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
            const CookDependency self = {Identity(1), "self", {}};
            Inspect(Identity(1), conflict ? Core::Containers::ArrayView<const CookDependency>{} : Core::Containers::ArrayView<const CookDependency>{&self, 1});
            if (conflict)
            {
                Inspect(Identity(1, CookAssetKind::Material));
            }
            CookDiagnostics diagnostics = {};
            for (size_t i = 0; i < diagnostics.Entries.size(); ++i)
            {
                auto* warning = diagnostics.Emplace(CookStage::Setup, CookDiagnosticCode::CacheMiss);
                ASSERT_NE(warning, nullptr);
                ASSERT_TRUE(warning->SetMessage("preserved warning"));
            }
            const auto calls = Allocator.Calls;
            EXPECT_EQ(Graph.Finalize(&diagnostics).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(diagnostics.Count, 64u);
            EXPECT_EQ(diagnostics.ErrorCount, 1u);
            EXPECT_EQ(diagnostics.DroppedEntries, 1u);
            for (const auto& entry : diagnostics.Entries)
            {
                EXPECT_EQ(entry.Code, CookDiagnosticCode::CacheMiss);
                EXPECT_EQ(entry.Severity, CookSeverity::Warning);
                EXPECT_STREQ(entry.Message, "preserved warning");
            }
        }
    }

    TEST_F(CookDependencyGraphTest, LongCycleDiagnosticRetainsItsClosingIdentityAndAllocatesNothing)
    {
        Root(Scene(), Identity(1));
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        for (uint32_t id = 1; id <= 128; ++id)
        {
            const CookDependency edge = {Identity(id == 128 ? 5 : id + 1), "next", {}};
            Inspect(Identity(id), {&edge, 1});
        }
        const auto      calls       = Allocator.Calls;
        CookDiagnostics diagnostics = {};
        EXPECT_EQ(Graph.Finalize(&diagnostics).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Allocator.Calls, calls);
        ASSERT_EQ(diagnostics.Count, 1u);
        const auto& entry = diagnostics.Entries[0];
        EXPECT_EQ(entry.Underlying, VFS::VFSError::Corrupted);
        EXPECT_TRUE(entry.ChainTruncated);
        EXPECT_EQ(entry.ChainCount, 32u);
        EXPECT_EQ(entry.Chain[0], Identity(5));
        EXPECT_EQ(entry.Chain[30], Identity(35));
        EXPECT_EQ(entry.Chain[31], Identity(5));
    }

    TEST_F(CookDependencyGraphTest, ZeroAssetCapsAndInvalidProvenanceDoNotAllocateNodes)
    {
        Graph.Reset();
        Limits.MaxAssets = 0;
        Limits.MaxEdges  = 0;
        Limits.MaxScenes = 1;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        const auto scene = Scene();
        const auto calls = Allocator.Calls;
        EXPECT_EQ(Graph.AddScene(Identity(2).UUID, VFS::VFSRelativePath::Parse("scenes/1.yaml").Value()).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Graph.AddScene(Identity(2).UUID, VFS::VFSRelativePath::Parse("scenes/2.yaml").Value()).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.AddRoot(scene, Identity(1), {}, {"\xff", 1}, "field").Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(Graph.AddRoot(scene, Identity(1), {}, "MeshComponent", "mesh_uuid").Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.NodeCount(), 0u);
        EXPECT_EQ(Graph.OccurrenceCount(), 0u);
        EXPECT_EQ(Graph.ReferenceCount(), 0u);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
    }

    TEST_F(CookDependencyGraphTest, RandomDAGMatchesIndependentReadyScanAndFixedPointClosureOracles)
    {
        constexpr uint32_t    count = 96;
        std::mt19937          random(917);
        std::vector<uint32_t> rank(count), order(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            order[i] = i;
        }
        std::shuffle(order.begin(), order.end(), random);
        for (uint32_t i = 0; i < count; ++i)
        {
            rank[order[i]] = i;
        }
        std::vector<std::vector<uint32_t>> children(count);
        for (uint32_t parent = 0; parent < count; ++parent)
        {
            for (uint32_t child = 0; child < count; ++child)
            {
                if (rank[child] < rank[parent] && random() % 17 == 0)
                {
                    children[parent].push_back(child);
                }
            }
        }
        const auto first = Scene(1), second = Scene(2);
        for (uint32_t id : order)
        {
            Root(id % 2 ? first : second, Identity(id + 1));
        }
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        std::shuffle(order.begin(), order.end(), random);
        for (uint32_t id : order)
        {
            std::vector<std::string>    slots(children[id].size());
            std::vector<CookDependency> edges(children[id].size());
            for (size_t edge = 0; edge < children[id].size(); ++edge)
            {
                char slot[16] = {};
                std::snprintf(slot, sizeof(slot), "slot/%03u", children[id][edge]);
                slots[edge] = slot;
                edges[edge] = {
                    Identity(children[id][edge] + 1), {slots[edge].data(), slots[edge].size()},
                       {}
                };
            }
            Inspect(Identity(id + 1), edges);
        }
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        std::vector<bool>              complete(count, false);
        std::vector<CookAssetIdentity> expected = {};
        for (uint32_t step = 0; step < count; ++step)
        {
            uint32_t ready = count;
            for (uint32_t id = 0; id < count; ++id)
            {
                if (!complete[id] && std::all_of(children[id].begin(), children[id].end(), [&](uint32_t child) { return complete[child]; }))
                {
                    ready = id;
                    break; // Numeric IDs encode canonical UUID-byte order.
                }
            }
            ASSERT_LT(ready, count);
            complete[ready] = true;
            expected.push_back(Identity(ready + 1));
        }
        EXPECT_EQ(Identities(Graph.LeafFirstNodes()), expected);
        for (uint32_t scene : {first, second})
        {
            std::vector<bool> reachable(count, false);
            for (uint32_t id = 0; id < count; ++id)
            {
                reachable[id] = (id % 2 ? first : second) == scene;
            }
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (uint32_t id = 0; id < count; ++id)
                {
                    if (reachable[id])
                    {
                        for (uint32_t child : children[id])
                        {
                            if (!reachable[child])
                            {
                                reachable[child] = true;
                                changed          = true;
                            }
                        }
                    }
                }
            }
            expected.clear();
            for (uint32_t id = 0; id < count; ++id)
            {
                if (reachable[id])
                {
                    expected.push_back(Identity(id + 1));
                }
            }
            uint32_t                                    workspace[count * 2] = {};
            Core::Containers::ArrayView<const uint32_t> closure              = {};
            ASSERT_TRUE(Graph.SceneClosure(scene, workspace, &closure).Succeeded());
            EXPECT_EQ(Identities(closure), expected);
        }
    }
} // namespace ZEngine::Cook
