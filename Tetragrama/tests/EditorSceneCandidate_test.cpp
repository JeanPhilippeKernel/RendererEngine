#include <Tetragrama/EditorSceneCandidate.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <gtest/gtest.h>
#include <array>

using namespace Tetragrama;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class EditorSceneCandidateTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    Memory             = {};
    std::unique_ptr<ZEngine::EngineContext> Engine             = {};
    Scene                                   Source             = {};
    SceneComponentSchemaRegistry            Schemas            = {};
    SceneDiagnostics                        Diagnostics        = {};
    SceneDocumentSettings                   Settings           = {};
    std::unique_ptr<EditorSceneCandidate>   Candidate          = {};
    VFSPath                                 Path               = VFSPath::Parse("/Scenes/loaded.zescene").Value();
    uint32_t                                ResolveCalls       = 0;
    uint32_t                                Draws              = 1;
    bool                                    AssetExists        = true;
    bool                                    FailWithDiagnostic = false;

    static uuids::uuid                      Identity(uint32_t index)
    {
        std::array<uint8_t, 16> bytes = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x47, 0x88, 0x99, 0xaa, 0xbb, 0xcc};
        for (uint32_t i = 0; i < 4; ++i)
        {
            bytes[12 + i] = static_cast<uint8_t>(index >> (i * 8));
        }
        return uuids::uuid(bytes);
    }
    void SetUp() override
    {
        Memory.Initialize(ZMega(192), {});
        Engine = std::make_unique<ZEngine::EngineContext>();
        Memory.MainArena.CreateSubArena(ZMega(32), &Engine->EditorSceneLoadArenaA, "CandidateTest/A");
        Memory.MainArena.CreateSubArena(ZMega(32), &Engine->EditorSceneLoadArenaB, "CandidateTest/B");
        Source.Initialize(&Memory.MainArena);
        Engine->Scene = &Source;
        Schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
        Settings.UUID = Identity(100000);
        Settings.Name = "Owned candidate name";
    }
    void TearDown() override
    {
        Candidate.reset();
        Source.Shutdown();
        Engine.reset();
    }
    EntityID AddEntity(uint32_t index, bool mesh = false)
    {
        const auto entity = Source.CreateEntity();
        Source.AddComponent<UUIDComponent>(entity, {Identity(index)});
        TransformComponent transform = {};
        transform.Position           = {float(index), 2.f, 3.f};
        Source.AddComponent<TransformComponent>(entity, transform);
        if (mesh)
        {
            Source.AddComponent<MeshComponent>(entity, {Identity(500000), 12345});
        }
        return entity;
    }
    EditorSceneAssetResolver Resolver()
    {
        return {this, [](void* context, const uuids::uuid& uuid, uint32_t* out_draws, SceneDiagnostics* diagnostics) {
                    auto& fixture = *static_cast<EditorSceneCandidateTest*>(context);
                    ++fixture.ResolveCalls;
                    EXPECT_FALSE(uuid.is_nil());
                    if (fixture.FailWithDiagnostic)
                    {
                        SceneDiagnosticError(diagnostics, "Asset lookup failed.");
                        return false;
                    }
                    *out_draws = fixture.Draws;
                    return fixture.AssetExists;
                }};
    }
    bool Capture(SceneDocument* document)
    {
        return SceneDocument::Capture(Source, Settings, Schemas, document, &Diagnostics);
    }
    bool Prepare(SceneDocument&& document, const EditorSceneCandidateLimits& limits = {})
    {
        return EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, Resolver(), &Candidate, &Diagnostics, limits);
    }
    bool AnySlotBusy() const
    {
        return Engine->EditorSceneLoadArenaAInUse.value.load() || Engine->EditorSceneLoadArenaBInUse.value.load();
    }
};

TEST_F(EditorSceneCandidateTest, EmptyDocumentHasOwnedMetadataAndNoDefaultContent)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    ASSERT_NE(Candidate, nullptr);
    EXPECT_EQ(Candidate->World().AliveCount(), 0u);
    EXPECT_EQ(Candidate->Actors().Count(), 0u);
    EXPECT_EQ(Candidate->Render().Instances.size(), 0u);
    EXPECT_EQ(Candidate->DrawCount(), 0u);
    EXPECT_STREQ(Candidate->Render().Name, Settings.Name.c_str());
    EXPECT_EQ(Candidate->Render().UUID, Settings.UUID);
    EXPECT_EQ(Candidate->Render().SourcePath, Path);
    EXPECT_FALSE(Candidate->Render().SourceReadOnly);
    EXPECT_FALSE(Candidate->Render().IsDirty());
    EXPECT_EQ(document.Settings(), nullptr);
    EXPECT_EQ(ResolveCalls, 0u);
    EXPECT_EQ(Engine->Scene, &Source);
    EXPECT_EQ(Engine->ActorManager, nullptr);
}

TEST_F(EditorSceneCandidateTest, LoadedAuthoredGridInitializesTheCompleteOrientedRuntimeSnapshot)
{
    Settings.Grid.Enabled        = false;
    Settings.Grid.BaseSpacing    = .3f;
    Settings.Grid.Origin         = {2.f, 8.f, 4.f};
    Settings.Grid.Orientation    = {.5f, .5f, .5f, .5f};
    Settings.Grid.MajorLineEvery = 7;
    Settings.Grid.Labels         = SceneGridLabels::UV;
    Settings.Grid.FadeRadius     = 1500.f;
    Settings.Grid.FadeStrength   = 1.5f;
    Settings.Grid.LineWidth      = 3.5f;
    Settings.Grid.MaxLOD         = 9;
    Settings.Grid.MinorColor     = {.1f, .2f, .3f, .4f};
    Settings.Grid.MajorColor     = {.2f, .3f, .4f, .5f};
    Settings.Grid.UColor         = {.3f, .4f, .5f, .6f};
    Settings.Grid.VColor         = {.4f, .5f, .6f, .7f};
    SceneDocument document       = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    const auto& render = Candidate->Render();
    EXPECT_TRUE(EqualSceneGridSettings(render.AuthoredGrid, Settings.Grid));
    EXPECT_FALSE(render.Grid.Enabled);
    EXPECT_FLOAT_EQ(render.Grid.CellSize, .3f);
    EXPECT_FLOAT_EQ(render.Grid.Origin.x, 2.f);
    EXPECT_FLOAT_EQ(render.Grid.Origin.y, 8.f);
    EXPECT_FLOAT_EQ(render.Grid.Origin.z, 4.f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.x, .5f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.y, .5f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.z, .5f);
    EXPECT_FLOAT_EQ(render.Grid.Orientation.w, .5f);
    EXPECT_EQ(render.Grid.MajorLineEvery, 7u);
    EXPECT_EQ(render.Grid.Labels, uint32_t(SceneGridLabels::UV));
    EXPECT_FLOAT_EQ(render.Grid.FadeRadius, 1500.f);
    EXPECT_FLOAT_EQ(render.Grid.FadeStrength, 1.5f);
    EXPECT_FLOAT_EQ(render.Grid.LineWidth, 3.5f);
    EXPECT_EQ(render.Grid.MaxLOD, 9);
    for (uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_FLOAT_EQ(render.Grid.ColorThin[i], Settings.Grid.MinorColor[i]);
        EXPECT_FLOAT_EQ(render.Grid.ColorThick[i], Settings.Grid.MajorColor[i]);
        EXPECT_FLOAT_EQ(render.Grid.ColorUAxis[i], Settings.Grid.UColor[i]);
        EXPECT_FLOAT_EQ(render.Grid.ColorVAxis[i], Settings.Grid.VColor[i]);
    }
    EXPECT_FALSE(Candidate->Render().IsDirty());
}

TEST_F(EditorSceneCandidateTest, MeshDrawCountRejectsScannerSlotZeroAliasesAndIncompleteRanges)
{
    ZEngine::Managers::AssetManager   manager  = {};
    ZEngine::Core::VFS::AssetRegistry registry = {};
    registry.Initialize(&Memory.MainArena);
    manager.Registry = &registry;
    manager.Meshes.init(&Memory.MainArena, 1);
    auto& mesh    = manager.Meshes.push_use({});
    mesh.MeshUUID = Identity(100);
    mesh.Vertices.init(&Memory.MainArena, 24, 24);
    mesh.Indices.init(&Memory.MainArena, 3, 3);
    mesh.SubMeshes.init(&Memory.MainArena, 1);
    mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3});
    registry.RegisterLoaded(mesh.MeshUUID, ZEngine::Managers::AssetType::MESH, {}, {}, ZEngine::Managers::AssetManager::CreateHandle(0, ZEngine::Managers::AssetType::MESH));
    registry.Register(Identity(101), ZEngine::Managers::AssetType::MESH, {}, {});
    uint32_t draws = 99;
    EXPECT_EQ(manager.GetMeshAsset(Identity(101)), nullptr);
    EXPECT_FALSE(manager.TryGetMeshDrawCount(Identity(101), &draws));
    EXPECT_EQ(draws, 99u);
    EXPECT_FALSE(manager.TryGetMeshDrawCount({}, &draws));
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, nullptr));
    ASSERT_TRUE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
    EXPECT_EQ(draws, 1u);
    manager.Meshes[0].SubMeshes[0].IndexOffset = UINT32_MAX;
    draws                                      = 99;
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
    EXPECT_EQ(draws, 99u);
    manager.Meshes[0].SubMeshes[0].IndexOffset = 0;
    manager.Meshes[0].SubMeshes[0].VertexCount = 4;
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
    manager.Meshes[0].SubMeshes[0].VertexCount = 3;
    manager.Meshes[0].Vertices.m_size          = 23;
    EXPECT_FALSE(manager.TryGetMeshDrawCount(mesh.MeshUUID, &draws));
}

TEST_F(EditorSceneCandidateTest, RebuildsRuntimeBindingsAndDerivedHierarchyWithoutActorDuplication)
{
    const auto parent = AddEntity(1);
    const auto child  = AddEntity(2, true);
    Source.AddComponent<ParentComponent>(child, {parent});
    NameComponent name = {};
    ZEngine::Helpers::secure_strncpy(name.Value, sizeof(name.Value), "Placed mesh", 11);
    Source.AddComponent<NameComponent>(child, name);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate->World().AliveCount(), 2u);
    EXPECT_EQ(Candidate->Actors().Count(), 0u);
    ASSERT_EQ(Candidate->Render().Instances.size(), 1u);
    const auto& instance = Candidate->Render().Instances[0];
    EXPECT_STREQ(instance.Name, "Placed mesh");
    EXPECT_EQ(instance.Transform(0, 3), 3.f);
    EXPECT_EQ(instance.Transform(1, 3), 4.f);
    EXPECT_EQ(instance.MeshUUID, Identity(500000));
    Candidate->World().ForEach<MeshComponent>([&](EntityID, const MeshComponent& mesh) {
        EXPECT_EQ(mesh.RenderInstanceId, instance.Id);
        EXPECT_NE(mesh.RenderInstanceId, 12345u);
    });
    EXPECT_EQ(ResolveCalls, 1u);
    EXPECT_EQ(Candidate->DrawCount(), 1u);
    EXPECT_EQ(Source.GetComponent<MeshComponent>(child)->RenderInstanceId, 12345u);
    EXPECT_EQ(Engine->Scene, &Source);
}

TEST_F(EditorSceneCandidateTest, FutureActorsAreBoundToTheCandidateWorld)
{
    AddEntity(1);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    const auto handle = Candidate->Actors().Create();
    ASSERT_TRUE(handle.Valid());
    ASSERT_NE(Candidate->Actors().Access(handle), nullptr);
    EXPECT_EQ(Candidate->World().AliveCount(), 2u);
    EXPECT_EQ(Source.AliveCount(), 1u);
    Candidate->Actors().Destroy(handle);
    EXPECT_EQ(Candidate->World().AliveCount(), 1u);
}

TEST_F(EditorSceneCandidateTest, ResolvesEachUniqueAssetOnceAndCountsEveryPlacedSubmesh)
{
    for (uint32_t i = 1; i <= 100; ++i)
    {
        AddEntity(i, true);
    }
    Draws                  = 3;
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate->DrawCount(), 300u);
    EXPECT_EQ(Candidate->Render().Instances.size(), 100u);
    EXPECT_EQ(ResolveCalls, 1u);
    EXPECT_GE(Candidate->Render().Instances.capacity(), 100u);
}

TEST_F(EditorSceneCandidateTest, MissingAssetPreservesInputAndReleasesTheFailedCandidateSlot)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    AssetExists = false;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate, nullptr);
    ASSERT_NE(document.Settings(), nullptr);
    EXPECT_EQ(document.EntityCount(), 1u);
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Engine->Scene, &Source);
    EXPECT_EQ(Source.AliveCount(), 1u);
    EXPECT_TRUE(Diagnostics.HasErrors());
    AssetExists = true;
    EXPECT_TRUE(Prepare(std::move(document)));
}

TEST_F(EditorSceneCandidateTest, ResolverFailurePreservesInputAndReleasesStorage)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    FailWithDiagnostic = true;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_NE(document.Settings(), nullptr);
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Engine->Scene, &Source);
}

TEST_F(EditorSceneCandidateTest, RejectsMissingResolverAndZeroDrawBinding)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, {}, &Candidate, &Diagnostics));
    EXPECT_FALSE(AnySlotBusy());
    Draws = 0;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RejectsTransformlessMeshInsteadOfSilentlyDroppingIt)
{
    const auto entity = AddEntity(1, true);
    Source.RemoveComponent<TransformComponent>(entity);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_EQ(ResolveCalls, 0u);
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RejectsCompleteInstanceSetBeforeResolvingAssets)
{
    AddEntity(1, true);
    AddEntity(2, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(Prepare(std::move(document), {.MaxInstances = 1}));
    EXPECT_EQ(ResolveCalls, 0u);
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Source.AliveCount(), 2u);
}

TEST_F(EditorSceneCandidateTest, RejectsSubmeshBudgetAndMultiplicationOverflowWithoutTruncation)
{
    AddEntity(1, true);
    AddEntity(2, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    Draws = 3;
    EXPECT_FALSE(Prepare(std::move(document), {.MaxDrawCommands = 5}));
    EXPECT_FALSE(AnySlotBusy());
    Draws = UINT32_MAX;
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_NE(document.Settings(), nullptr);
}

TEST_F(EditorSceneCandidateTest, AcceptsExactDrawBudgetButCannotRaiseTheGPUCap)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    Draws = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
    EXPECT_FALSE(Prepare(std::move(document), {.MaxDrawCommands = Draws + 1}));
    EXPECT_FALSE(AnySlotBusy());
    ASSERT_TRUE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate->DrawCount(), Draws);
}

TEST_F(EditorSceneCandidateTest, TwoOwnersCoexistAndAFailedThirdLeavesBothIntact)
{
    AddEntity(1, true);
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    auto first = std::move(Candidate);
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    auto* second = Candidate.get();
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_EQ(Candidate.get(), second);
    EXPECT_EQ(first->World().AliveCount(), 1u);
    EXPECT_EQ(second->World().AliveCount(), 1u);
    EXPECT_TRUE(Engine->EditorSceneLoadArenaAInUse.value.load());
    EXPECT_TRUE(Engine->EditorSceneLoadArenaBInUse.value.load());
    first.reset();
    EXPECT_TRUE(Prepare(std::move(document)));
    EXPECT_TRUE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RepeatedReplacementReusesBothBoundedSlots)
{
    AddEntity(1, true);
    std::array<size_t, 2> high_water = {};
    for (uint32_t iteration = 0; iteration < 100; ++iteration)
    {
        SceneDocument document = {};
        ASSERT_TRUE(Capture(&document));
        ASSERT_TRUE(Prepare(std::move(document)));
        const size_t slot   = Candidate->Render().LocalArena == &Engine->EditorSceneLoadArenaA ? 0 : 1;
        const auto   offset = Candidate->Render().LocalArena->m_current_offset;
        if (high_water[slot])
        {
            EXPECT_EQ(offset, high_water[slot]);
        }
        high_water[slot] = offset;
        EXPECT_STREQ(Candidate->Render().Name, "Owned candidate name");
    }
    EXPECT_NE(high_water[0], 0u);
    EXPECT_NE(high_water[1], 0u);
    Candidate.reset();
    EXPECT_FALSE(AnySlotBusy());
    EXPECT_EQ(Engine->EditorSceneLoadArenaA.m_current_offset, Engine->EditorSceneLoadArenaA.m_initial_current_offset);
    EXPECT_EQ(Engine->EditorSceneLoadArenaB.m_current_offset, Engine->EditorSceneLoadArenaB.m_initial_current_offset);
}

TEST_F(EditorSceneCandidateTest, RejectsInvalidInputsBeforeClaimingStorage)
{
    SceneDocument document = {};
    EXPECT_FALSE(Prepare(std::move(document)));
    ASSERT_TRUE(Capture(&document));
    EXPECT_FALSE(EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, Path, {}, nullptr, &Diagnostics));
    const auto invalid = VFSPath::Parse("/cache/hidden.zescene").Value();
    EXPECT_FALSE(EditorSceneCandidate::Prepare(*Engine, std::move(document), Schemas, invalid, {}, &Candidate, &Diagnostics));
    EXPECT_NE(document.Settings(), nullptr);
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, RejectsUnavailableAndUndersizedSlotsWithoutCrashing)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    Engine->EditorSceneLoadArenaA.Shutdown();
    Engine->EditorSceneLoadArenaB.Shutdown();
    EXPECT_FALSE(Prepare(std::move(document)));
    Memory.MainArena.CreateSubArena(ZMega(1), &Engine->EditorSceneLoadArenaA, "CandidateTest/Small");
    EXPECT_FALSE(Prepare(std::move(document)));
    EXPECT_FALSE(AnySlotBusy());
}

TEST_F(EditorSceneCandidateTest, InitializedOwnerCannotClaimASecondSlot)
{
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Prepare(std::move(document)));
    auto* original = Candidate->Render().LocalArena;
    EXPECT_FALSE(Candidate->Render().InitializeDeserialized(*Engine));
    EXPECT_EQ(Candidate->Render().LocalArena, original);
    EXPECT_FALSE(Engine->EditorSceneLoadArenaBInUse.value.load());
}
