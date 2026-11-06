#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <gtest/gtest.h>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookSceneRootsTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager          Memory      = {};
        ECS::SceneComponentSchemaRegistry    Schemas     = {};
        ECS::Scene                           Source      = {};
        ECS::SceneDocument                   Document    = {};
        Tests::FixedAllocator<ZKilo(256), 1> Parent      = {};
        CookScratchAllocator                 Allocator   = {};
        CookDependencyGraph                  Graph       = {};
        CookLimits                           Limits      = {.MaxWorkingBytes = ZKilo(256) + ZKilo(4)};
        CookDiagnostics                      Diagnostics = {};
        ECS::SceneDocumentSettings           Settings    = {};
        uuids::uuid                          AssetUUID   = uuids::uuid::from_string("00000000-0000-0000-0000-000000000003").value();

        void                                 SetUp() override
        {
            Memory.Initialize(ZMega(128), {});
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas));
            Source.Initialize(&Memory.MainArena);
            // Keep the same slab; reserve separate ledger headroom for fixed
            // graph-finalization state rather than waiving that charge.
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Allocator.Initialize(Parent, ZKilo(256), CookMemoryCategory::CatalogGraph).Succeeded());
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            Settings.UUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
        }
        void TearDown() override
        {
            Graph.Reset();
            EXPECT_EQ(Allocator.OutstandingAllocations(), 0u);
            Allocator.Release();
            Source.Shutdown();
        }
        ECS::EntityID Entity(cstring uuid = "00000000-0000-0000-0000-000000000002")
        {
            const auto entity = Source.CreateEntity();
            Source.AddComponent<ECS::Components::UUIDComponent>(entity, {uuids::uuid::from_string(uuid).value()});
            return entity;
        }
        void Capture()
        {
            ASSERT_TRUE(ECS::SceneDocument::Capture(Source, Settings, Schemas, &Document, nullptr));
        }
        VFS::VFSResult<uint32_t> Add(const ECS::SceneComponentSchemaRegistry& schemas)
        {
            return Graph.AddScene(Document, schemas, VFS::VFSRelativePath::Parse("scenes/main.yaml").Value(), &Diagnostics);
        }
        ECS::SceneComponentSchemaRegistry Override(ECS::SceneReference& reference)
        {
            const auto*                       mesh   = Schemas.FindByKey("MeshComponent");
            ECS::SceneComponentSchemaRegistry result = {};
            result.Initialize(&Memory.MainArena);
            const ECS::SceneComponentSchemaDesc descriptor = {
                .Key = mesh->Key, .Version = mesh->Version, .RuntimeType = mesh->RuntimeType, .Fields = mesh->Fields, .FieldCount = mesh->FieldCount, .Codecs = mesh->Codecs, .References = {.Context = &reference, .Enumerate = [](void* context, const ECS::SceneValue&, ECS::SceneReferenceVisitorFn visitor, void* state, ECS::SceneDiagnostics*) { return visitor(state, *static_cast<ECS::SceneReference*>(context)); }, .Remap = mesh->References.Remap}
            };
            EXPECT_TRUE(result.Register(descriptor, nullptr));
            return result;
        }
    };

    TEST_F(CookSceneRootsTest, DocumentRootsPreserveTypedOccurrencesAndInactiveSkyWithoutRetainingSceneStorage)
    {
        const auto first = Entity(), second = Entity("00000000-0000-0000-0000-000000000004");
        const auto material_uuid    = uuids::uuid::from_string("00000000-0000-0000-0000-000000000005").value();
        const auto environment_uuid = uuids::uuid::from_string("00000000-0000-0000-0000-000000000006").value();
        Source.AddComponent<ECS::Components::MeshComponent>(first, {AssetUUID});
        Source.AddComponent<ECS::Components::MaterialComponent>(first, {material_uuid});
        Source.AddComponent<ECS::Components::MeshComponent>(second, {AssetUUID});
        Source.AddComponent<ECS::Components::ParentComponent>(second, {first});
        Settings.Sky.EnvironmentMap = environment_uuid;
        Settings.Sky.Mode           = Rendering::Scenes::SkyMode::Atmosphere;
        Capture();
        const auto scene = Add(Schemas);
        ASSERT_TRUE(scene.Succeeded());
        Document = {};
        Source.DestroyEntity(second);
        Source.DestroyEntity(first);
        EXPECT_EQ(Graph.SceneCount(), 1u);
        EXPECT_EQ(Graph.NodeCount(), 3u); // Three kinds, duplicate mesh occurrence deduplicated.
        EXPECT_EQ(Graph.OccurrenceCount(), 4u);
        EXPECT_EQ(Graph.ReferenceCount(), 5u); // Includes the entity reference.
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        for (uint32_t i = 0; i < Graph.NodeCount(); ++i)
        {
            const auto          kind       = Graph.Node(i)->Identity.Kind;
            CookAssetInspection inspection = {};
            ASSERT_TRUE(inspection.Assign(kind == CookAssetKind::Mesh ? "zemesh-v1-le64" : kind == CookAssetKind::Material ? "zematerial-v1-json" : "zenvmap-v2-le64", {}, {}, {}, Allocator).Succeeded());
            ASSERT_TRUE(Graph.SetInspection(i, 1, inspection).Succeeded());
        }
        ASSERT_TRUE(Graph.Finalize(&Diagnostics).Succeeded());
        EXPECT_EQ(Graph.SceneRoots(scene.Value()).size(), 3u);
        CookDiagnostic provenance = {};
        ASSERT_TRUE(Graph.Describe(Graph.Find({CookAssetKind::EnvironmentMap, environment_uuid}), CookDiagnosticCode::InvalidAsset, "invalid", &provenance).Succeeded());
        EXPECT_TRUE(provenance.EntityUUID.is_nil());
        EXPECT_STREQ(provenance.SchemaKey, "SceneSettings");
        EXPECT_STREQ(provenance.FieldKey, "sky.environment");
        EXPECT_EQ(Diagnostics.ErrorCount, 0u);
    }

    TEST_F(CookSceneRootsTest, EntityOccurrencesConsumeTheSamePrededuplicationBudgetAsAssetsAndDependencyEdges)
    {
        const auto parent = Entity("00000000-0000-0000-0000-000000000004");
        const auto entity = Entity();
        Source.AddComponent<ECS::Components::MeshComponent>(entity, {AssetUUID});
        Source.AddComponent<ECS::Components::ParentComponent>(entity, {parent});
        Capture();
        Graph.Reset();
        Limits.MaxReferenceOccurrences = 2;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        ASSERT_TRUE(Add(Schemas).Succeeded());
        EXPECT_EQ(Graph.ReferenceCount(), 2u);
        EXPECT_EQ(Graph.OccurrenceCount(), 1u);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        const CookDependency edge = {
            {CookAssetKind::Material, AssetUUID},
            "material", {}
        };
        CookAssetInspection inspection = {};
        ASSERT_TRUE(inspection.Assign("zemesh-v1-le64", {}, {}, {&edge, 1}, Allocator).Succeeded());
        EXPECT_EQ(Graph.SetInspection(0, 1, inspection).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.ReferenceCount(), 2u);
        EXPECT_EQ(Graph.EdgeCount(), 0u);
    }

    TEST_F(CookSceneRootsTest, FailedEnumerationRollsBackNewSceneIdentitiesAndCountsButPreservesExistingRoots)
    {
        const auto existing = Graph.AddScene(Settings.UUID, VFS::VFSRelativePath::Parse("scenes/existing.yaml").Value());
        ASSERT_TRUE(existing.Succeeded());
        const CookAssetIdentity old = {CookAssetKind::Texture, AssetUUID};
        ASSERT_TRUE(Graph.AddRoot(existing.Value(), old, {}, "Existing", "texture").Succeeded());
        Settings.UUID    = uuids::uuid::from_string("00000000-0000-0000-0000-000000000005").value();
        const auto first = Entity(), second = Entity("00000000-0000-0000-0000-000000000004");
        Source.AddComponent<ECS::Components::MeshComponent>(first, {AssetUUID});
        Source.AddComponent<ECS::Components::MeshComponent>(second, {AssetUUID});
        Capture();
        // Invalid descriptors are checked on every callback, including nils.
        ECS::SceneReference reference = {ECS::SceneReferenceKind::Asset, {}, nullptr, "mesh"};
        const auto          schemas   = Override(reference);
        EXPECT_EQ(Add(schemas).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Graph.SceneCount(), 1u);
        EXPECT_EQ(Graph.NodeCount(), 1u);
        EXPECT_EQ(Graph.ReferenceCount(), 1u);
        EXPECT_EQ(Graph.OccurrenceCount(), 1u);
        EXPECT_NE(Graph.Find(old), CookGraphInvalidIndex);
        EXPECT_EQ(Graph.Find({CookAssetKind::Mesh, AssetUUID}), CookGraphInvalidIndex);
        ASSERT_TRUE(Add(Schemas).Succeeded());
        EXPECT_EQ(Graph.NodeCount(), 2u);
        EXPECT_EQ(Graph.ReferenceCount(), 3u);
    }

    TEST_F(CookSceneRootsTest, NilFallbacksStillRequireTypedDescriptorsAndConsumeOccurrenceBudget)
    {
        Source.AddComponent<ECS::Components::MeshComponent>(Entity(), {AssetUUID});
        Capture();
        ECS::SceneReference reference = {ECS::SceneReferenceKind::Asset, {}, "mesh_uuid", "mesh"};
        const auto          schemas   = Override(reference);
        ASSERT_TRUE(Add(schemas).Succeeded());
        EXPECT_EQ(Graph.NodeCount(), 0u);
        EXPECT_EQ(Graph.ReferenceCount(), 1u);
        EXPECT_EQ(Graph.OccurrenceCount(), 0u);
        ASSERT_TRUE(Graph.BeginDiscovery().Succeeded());
        ASSERT_TRUE(Graph.Finalize(nullptr).Succeeded());
        Graph.Reset();
        Limits.MaxReferenceOccurrences = 0;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        EXPECT_EQ(Add(schemas).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.SceneCount(), 0u);
        EXPECT_EQ(Graph.ReferenceCount(), 0u);
        Limits.MaxReferenceOccurrences = 2;
        for (cstring key : {static_cast<cstring>(nullptr), "", "texture2"})
        {
            Graph.Reset();
            ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
            reference.AssetKindKey = key;
            EXPECT_EQ(Add(schemas).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(Graph.SceneCount(), 0u);
            EXPECT_EQ(Graph.NodeCount(), 0u);
        }
    }

    TEST_F(CookSceneRootsTest, OccurrenceLimitFailureAfterPartialEnumerationRestoresAllLogicalState)
    {
        const auto first = Entity(), second = Entity("00000000-0000-0000-0000-000000000004");
        Source.AddComponent<ECS::Components::MeshComponent>(first, {AssetUUID});
        Source.AddComponent<ECS::Components::MeshComponent>(second, {AssetUUID});
        Capture();
        Graph.Reset();
        Limits.MaxReferenceOccurrences = 1;
        ASSERT_TRUE(Graph.Initialize(Allocator, Limits).Succeeded());
        EXPECT_EQ(Add(Schemas).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Graph.SceneCount(), 0u);
        EXPECT_EQ(Graph.NodeCount(), 0u);
        EXPECT_EQ(Graph.OccurrenceCount(), 0u);
        EXPECT_EQ(Graph.ReferenceCount(), 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].EntityUUID, uuids::uuid::from_string("00000000-0000-0000-0000-000000000004").value());
    }
} // namespace ZEngine::Cook
