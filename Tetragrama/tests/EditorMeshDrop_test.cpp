#include <Tetragrama/Editor.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <gtest/gtest.h>
#include <thread>

using namespace Tetragrama;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class EditorMeshDropTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    Memory        = {};
    std::unique_ptr<Editor>                 App           = {};
    std::unique_ptr<ZEngine::EngineContext> Engine        = {};
    Scene                                   World         = {};
    ActorManager                            Actors        = {};
    EditorScene                             Owner         = {};
    VFSMemoryBackend                        Backend       = {};
    VFSContext                              VFS           = {};
    SceneDiagnostics                        Diagnostics   = {};
    ZEngine::Importers::AssetMesh           Mesh          = {};
    ZEngine::Importers::AssetNodeHierarchy  Hierarchy     = {};
    EntityID                                Original      = {};
    const uuids::uuid                       MeshID        = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const VFSPath                           Path          = VFSPath::Parse("/meshes/triangle.zemesh").Value();
    void*                                   QueuedContext = nullptr;
    void (*QueuedTask)(void*)                             = nullptr;
    uint32_t Submissions                                  = 0;
    uint32_t Binds                                        = 0;
    uint32_t ExistingDraws                                = 1;
    bool     AcceptSubmission                             = true;
    bool     BindSucceeds                                 = true;
    bool     IncomingLoaded                               = true;

    void     SetUp() override
    {
        Memory.Initialize(ZMega(512), {});
        App    = std::make_unique<Editor>();
        Engine = std::make_unique<ZEngine::EngineContext>();
        ASSERT_TRUE(App->MeshDrop.Initialize(Memory.MainArena));
        World.Initialize(&Memory.MainArena);
        Actors.Initialize(&Memory.MainArena, World);
        Backend.Initialize(&Memory.MainArena);
        VFS.Initialize(&Memory.MainArena);
        ASSERT_TRUE(VFS.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
        Engine->Scene        = &World;
        Engine->ActorManager = &Actors;
        Engine->VFS          = &VFS;
        Owner.Name           = "Drop test";
        Owner.UUID           = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        Owner.Instances.init(&Memory.MainArena, 4);
        App->CurrentScene = &Owner;
        ASSERT_TRUE(App->Session.Initialize(&Memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        ASSERT_TRUE(App->Session.BindScene(&World));
        ASSERT_TRUE(Owner.AttachSession(App->Session, *Engine));
        App->Session.EndMutationPhase();
        Original = World.CreateEntity();
        World.AddComponent<UUIDComponent>(Original, {*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")});
        World.AddComponent<TransformComponent>(Original, {});

        Mesh.MeshUUID = MeshID;
        Mesh.Vertices.init(&Memory.MainArena, 24, 24);
        Mesh.Vertices[8]  = 1.f;
        Mesh.Vertices[17] = 1.f;
        Mesh.Indices.init(&Memory.MainArena, 3);
        Mesh.Indices.push(0);
        Mesh.Indices.push(1);
        Mesh.Indices.push(2);
        Mesh.SubMeshes.init(&Memory.MainArena, 1);
        Mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        Hierarchy.MeshUUID          = MeshID;
        Hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
        Hierarchy.Hierarchies.init(&Memory.MainArena, 1);
        Hierarchy.LocalTransforms.init(&Memory.MainArena, 1);
        Hierarchy.GlobalTransforms.init(&Memory.MainArena, 1);
        Hierarchy.Names.init(&Memory.MainArena, 0);
        Hierarchy.MaterialNames.init(&Memory.MainArena, 0);
        Hierarchy.NodeNames.init(&Memory.MainArena, 16);
        Hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
        Hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
        ZEngine::Importers::AddNode(Hierarchy, -1, 0);
        Hierarchy.NodeMeshes.insert(0, 0);
        ZEngine::Importers::AssetCodec::ImportConfiguration config = {};
        config.VFS                                                 = &VFS;
        config.OutputAssetsPath.init(&Memory.MainArena, "/meshes");
        config.OutputAssetFile.init(&Memory.MainArena, "triangle.zemesh");
        ASSERT_TRUE(ZEngine::Importers::AssetCodec::SerializeMeshAssetFile(&Memory.MainArena, Mesh, Hierarchy, config).Succeeded());
    }
    void TearDown() override
    {
        App->MeshDrop.Cancel();
        RunQueued();
        App->MeshDrop.ShutdownAfterWorkersJoined();
        App->Session.Shutdown();
        Owner.Session     = nullptr;
        App->CurrentScene = nullptr;
        Actors.Shutdown();
        World.Shutdown();
        VFS.Shutdown();
    }
    EditorMeshDropServices Services()
    {
        return {
            .Context = this,
            .Submit =
                [](void* context, void* request, void (*task)(void*)) {
                    auto& fixture = *static_cast<EditorMeshDropTest*>(context);
                    ++fixture.Submissions;
                    if (!fixture.AcceptSubmission)
                    {
                        return false;
                    }
                    EXPECT_EQ(fixture.QueuedTask, nullptr);
                    fixture.QueuedContext = request;
                    fixture.QueuedTask    = task;
                    return true;
                },
            .BindMesh =
                [](void* context, ZEngine::Importers::AssetMesh& mesh, ZEngine::Importers::AssetNodeHierarchy& hierarchy, ZEngine::Core::Memory::ArenaAllocator*) {
                    auto& fixture = *static_cast<EditorMeshDropTest*>(context);
                    ++fixture.Binds;
                    EXPECT_TRUE(fixture.App->Session.InMutationPhase());
                    EXPECT_EQ(mesh.MeshUUID, fixture.MeshID);
                    EXPECT_EQ(hierarchy.MeshUUID, mesh.MeshUUID);
                    EXPECT_EQ(mesh.Vertices.size(), 24u);
                    return fixture.BindSucceeds;
                },
            .DrawCount =
                [](void* context, const uuids::uuid&, uint32_t* out) {
                    auto& fixture = *static_cast<EditorMeshDropTest*>(context);
                    *out          = fixture.ExistingDraws;
                    return fixture.IncomingLoaded;
                },
        };
    }
    void RunQueued()
    {
        if (!QueuedTask)
        {
            return;
        }
        const auto task    = QueuedTask;
        auto*      context = QueuedContext;
        QueuedTask         = nullptr;
        QueuedContext      = nullptr;
        task(context);
    }
    bool Process(bool editable = true)
    {
        EXPECT_TRUE(App->Session.BeginMutationPhase(editable));
        const bool result = App->MeshDrop.Process(*Engine, &Owner, App->Session, &Diagnostics, Services());
        App->Session.EndMutationPhase();
        return result;
    }
    void Start()
    {
        ASSERT_TRUE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
        ASSERT_TRUE(Process());
        ASSERT_NE(QueuedTask, nullptr);
    }
    void Preview(float x, bool commit = false)
    {
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        const auto target = App->Session.TargetFor(Original);
        ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = x;
        ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
        if (commit)
        {
            ASSERT_TRUE(App->Session.Commit());
        }
        App->Session.EndMutationPhase();
    }
};

TEST_F(EditorMeshDropTest, CompletionCreatesOneSaveableIdentityAndRestoresPreviewBeforeInvalidatingHistory)
{
    Preview(2.f, true);
    Start();
    Preview(5.f);
    RunQueued();
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_TRUE(App->Session.Active());
    ASSERT_TRUE(Process());
    EXPECT_FALSE(App->MeshDrop.Pending());
    EXPECT_EQ(Binds, 1u);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Original)->Position.x, 2.f);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(Actors.Count(), 1u);
    ASSERT_EQ(Owner.Instances.size(), 1u);
    Actors.ForEach([&](auto, const Actor* actor) {
        const auto* identity = actor->GetComponent<UUIDComponent>();
        ASSERT_NE(identity, nullptr);
        EXPECT_FALSE(identity->Value.is_nil());
        EXPECT_EQ(actor->GetComponent<MeshComponent>()->MeshUUID, MeshID);
        EXPECT_EQ(actor->GetComponent<MeshComponent>()->RenderInstanceId, Owner.Instances[0].Id);
        EXPECT_STREQ(actor->GetComponent<NameComponent>()->Value, "triangle");
    });
    SceneComponentSchemaRegistry schemas = {};
    schemas.Initialize(&Memory.MainArena);
    ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(schemas, &Diagnostics));
    SceneDocument document;
    ASSERT_TRUE(SceneDocument::Capture(World, {.UUID = Owner.UUID, .Name = Owner.Name}, schemas, &document, &Diagnostics));
    EXPECT_EQ(document.EntityCount(), 2u);
    EXPECT_TRUE(Process());
    EXPECT_EQ(Actors.Count(), 1u);
}

TEST_F(EditorMeshDropTest, ASceneEpochChangeCannotRedirectACompletedDrop)
{
    Start();
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(nullptr));
    ASSERT_TRUE(App->Session.BindScene(&World));
    ASSERT_TRUE(Owner.AttachSession(App->Session, *Engine));
    App->Session.EndMutationPhase();
    RunQueued();
    EXPECT_FALSE(Process());
    EXPECT_FALSE(App->MeshDrop.Pending());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_EQ(World.AliveCount(), 1u);
}

TEST_F(EditorMeshDropTest, FailedBindingPreservesActivePreviewAndHistory)
{
    Preview(2.f, true);
    Start();
    Preview(5.f);
    BindSucceeds = false;
    RunQueued();
    EXPECT_FALSE(Process());
    EXPECT_TRUE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Original)->Position.x, 5.f);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_TRUE(Owner.Instances.empty());
}

TEST_F(EditorMeshDropTest, SaturatedWorkerSubmissionHasNoInlineFallbackAndCanBeRetried)
{
    AcceptSubmission = false;
    ASSERT_TRUE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(Process());
    EXPECT_EQ(QueuedTask, nullptr);
    EXPECT_EQ(Binds, 0u);
    EXPECT_FALSE(App->MeshDrop.Pending());
    AcceptSubmission = true;
    Start();
    RunQueued();
    EXPECT_TRUE(Process());
    EXPECT_EQ(Actors.Count(), 1u);
}

TEST_F(EditorMeshDropTest, CancellationRetainsQueuedOwnershipUntilCompletion)
{
    Start();
    App->MeshDrop.Cancel();
    EXPECT_TRUE(App->MeshDrop.Pending());
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    RunQueued();
    EXPECT_TRUE(Process());
    EXPECT_FALSE(App->MeshDrop.Pending());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
}

TEST_F(EditorMeshDropTest, PoolShutdownMayAbandonQueuedWorkWithoutLeakingItsContextOrSpawning)
{
    Start();
    // Mirrors a joined pool discarding tasks that never started.
    QueuedTask    = nullptr;
    QueuedContext = nullptr;
    App->MeshDrop.ShutdownAfterWorkersJoined();
    EXPECT_FALSE(App->MeshDrop.Pending());
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    EXPECT_EQ(Actors.Count(), 0u);
}

TEST_F(EditorMeshDropTest, RealWorkerCompletionDoesNotMutateTheWorldUntilMainThreadPolling)
{
    Start();
    std::thread worker([&] { RunQueued(); });
    worker.join();
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_EQ(World.AliveCount(), 1u);
    EXPECT_EQ(Binds, 0u);
    ASSERT_TRUE(Process());
    EXPECT_EQ(Actors.Count(), 1u);
}

TEST_F(EditorMeshDropTest, ReadOnlyModeAndWrongActorWorldRejectBeforeAssetBinding)
{
    Start();
    RunQueued();
    EXPECT_FALSE(Process(false));
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    App->Session.EndMutationPhase();
    Start();
    RunQueued();
    Scene other;
    other.Initialize(&Memory.MainArena);
    ActorManager wrong;
    wrong.Initialize(&Memory.MainArena, other);
    Engine->ActorManager = &wrong;
    EXPECT_FALSE(Process());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(other.AliveCount(), 0u);
    Engine->ActorManager = &Actors;
    wrong.Shutdown();
    other.Shutdown();
}

TEST_F(EditorMeshDropTest, DrawBudgetFailureIsAtomicAndDoesNotInvalidateHistory)
{
    Preview(2.f, true);
    Owner.Instances.push({.MeshUUID = MeshID, .Id = 1});
    ExistingDraws = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
    Start();
    RunQueued();
    EXPECT_FALSE(Process());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_EQ(Owner.Instances.size(), 1u);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorMeshDropTest, ComponentBudgetFailureHappensBeforeBindingOrHistoryInvalidation)
{
    Preview(2.f, true);
    Start();
    RunQueued();
    const auto capacity           = Memory.MainArena.m_total_size;
    Memory.MainArena.m_total_size = Memory.MainArena.m_current_offset;
    EXPECT_FALSE(Process());
    Memory.MainArena.m_total_size = capacity;
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorMeshDropTest, HierarchyGrowthIsIncludedBeforeBindingOrHistoryInvalidation)
{
    // Warm all component stores so this exercises hierarchy growth, not the
    // much larger fresh sparse-store budget. The other entities are pure ECS.
    World.AddComponent<NameComponent>(Original, {});
    World.AddComponent<MeshComponent>(Original, {});
    World.RemoveComponent<NameComponent>(Original);
    World.RemoveComponent<MeshComponent>(Original);
    while (World.AliveCount() < 1024)
    {
        ASSERT_TRUE(World.CreateEntity().IsValid());
    }
    ASSERT_GT(World.Hierarchy().ReservationMemoryBound(World.AliveCount() + 1), ZKilo(40));
    Preview(2.f, true);
    Start();
    Preview(5.f);
    RunQueued();
    const auto capacity           = Memory.MainArena.m_total_size;
    Memory.MainArena.m_total_size = Memory.MainArena.m_current_offset + ZKilo(40);
    EXPECT_FALSE(Process());
    Memory.MainArena.m_total_size = capacity;
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_EQ(World.AliveCount(), 1024u);
    EXPECT_TRUE(Owner.Instances.empty());
    EXPECT_TRUE(App->Session.Active());
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Original)->Position.x, 5.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorMeshDropTest, LongMeshNamesNeverSplitUtf8AndRemainSerializable)
{
    const char* characters[] = {"\xc3\xa9", "\xe7\x8c\xab", "\xf0\x9f\x94\xa7"};
    auto        previous     = Path;
    for (const char* character : characters)
    {
        for (const bool split : {false, true})
        {
            const size_t      bytes = ZEngine::Helpers::secure_strlen(character);
            const std::string prefix(127 - bytes + (split ? 1 : 0), 'a');
            const auto        label  = prefix + character + "suffix";
            const auto        parsed = VFSPath::Parse(("/meshes/" + label + ".zemesh").c_str());
            ASSERT_TRUE(parsed.Succeeded());
            ASSERT_TRUE(VFS.Rename(previous, parsed.Value()).Succeeded());
            previous = parsed.Value();
            ASSERT_TRUE(App->RequestMeshDrop(previous, App->Session.SceneEpoch()));
            ASSERT_TRUE(Process());
            RunQueued();
            ASSERT_TRUE(Process());
            const auto expected = split ? prefix : prefix + character;
            ASSERT_FALSE(Owner.Instances.empty());
            EXPECT_STREQ(Owner.Instances.back().Name, expected.c_str());
            uint32_t matches = 0;
            World.ForEach<MeshComponent, NameComponent>([&](auto, const MeshComponent& mesh, const NameComponent& name) {
                if (mesh.RenderInstanceId == Owner.Instances.back().Id)
                {
                    ++matches;
                    EXPECT_STREQ(name.Value, expected.c_str());
                }
            });
            EXPECT_EQ(matches, 1u);
        }
    }
    EXPECT_EQ(Actors.Count(), 6u);
    SceneComponentSchemaRegistry schemas = {};
    schemas.Initialize(&Memory.MainArena);
    ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(schemas, &Diagnostics));
    SceneDocument document = {};
    ASSERT_TRUE(SceneDocument::Capture(World, {.UUID = Owner.UUID, .Name = Owner.Name}, schemas, &document, &Diagnostics));
    std::string source = {};
    ASSERT_TRUE(document.Emit(&source, &Diagnostics));
    SceneDocument reloaded = {};
    ASSERT_TRUE(SceneDocument::Parse(source.data(), source.size(), schemas, &reloaded, &Diagnostics));
    EXPECT_EQ(reloaded.EntityCount(), World.AliveCount());
}

TEST_F(EditorMeshDropTest, InstanceReservationFailureDoesNotPublishAnOrphanActor)
{
    Preview(2.f, true);
    Start();
    RunQueued();
    ZEngine::Core::Memory::ArenaAllocator tiny;
    tiny.Initialize(64, 0, "MeshDropTest/TinyInstances");
    ASSERT_NE(tiny.m_memory, nullptr);
    Owner.Instances             = {};
    Owner.Instances.m_allocator = &tiny;
    EXPECT_FALSE(Process());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_TRUE(Owner.Instances.empty());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    Owner.Instances.m_allocator = nullptr;
}

TEST_F(EditorMeshDropTest, CorruptSourceDoesNotCreateAnEntityOrInvalidateHistory)
{
    Preview(2.f, true);
    const uint8_t invalid[] = {1, 2, 3};
    ASSERT_TRUE(WriteFileAtomically(VFS, Path, {invalid, sizeof(invalid)}).Succeeded());
    Start();
    RunQueued();
    EXPECT_FALSE(Process());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
}

TEST_F(EditorMeshDropTest, RepeatedCancelledLoadsReuseTheSameImportWorkspace)
{
    const auto used = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 100; ++i)
    {
        Start();
        RunQueued();
        App->MeshDrop.Cancel();
        ASSERT_TRUE(Process());
        EXPECT_FALSE(App->MeshDrop.Pending());
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, used);
    EXPECT_EQ(Binds, 0u);
}

TEST_F(EditorMeshDropTest, RequestsAreBoundedAndCompletionCannotApplyOutsideMutation)
{
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch() + 1));
    EXPECT_FALSE(App->RequestMeshDrop(VFSPath::Parse("/mesh.fbx").Value(), App->Session.SceneEpoch()));
    Start();
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    RunQueued();
    EXPECT_FALSE(App->MeshDrop.Process(*Engine, &Owner, App->Session, &Diagnostics, Services()));
    EXPECT_TRUE(App->MeshDrop.Pending());
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_TRUE(Process());
}

TEST_F(EditorMeshDropTest, ImportedPlacementCopiesNameAndUsesCheckedSaveableSpawnAtMutationPoint)
{
    IncomingLoaded = false;
    Preview(2.f, true);
    char name[32] = "Captured actor";
    ASSERT_TRUE(App->RequestImportedMesh(*Engine, Path, name, Owner.UUID, App->Session.SceneEpoch()));
    ZEngine::Helpers::secure_strcpy(name, sizeof(name), "Changed by caller");
    EXPECT_EQ(Actors.Count(), 0u);
    ASSERT_TRUE(Process());
    Preview(5.f);
    RunQueued();
    EXPECT_TRUE(App->Session.Active());
    EXPECT_EQ(Binds, 0u);
    ASSERT_TRUE(Process());
    EXPECT_FALSE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Original)->Position.x, 2.f);
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_EQ(Owner.Instances.size(), 1u);
    EXPECT_STREQ(Owner.Instances[0].Name, "Captured actor");
    Actors.ForEach([&](auto, const Actor* actor) {
        ASSERT_NE(actor->GetComponent<UUIDComponent>(), nullptr);
        EXPECT_FALSE(actor->GetComponent<UUIDComponent>()->Value.is_nil());
        EXPECT_STREQ(actor->GetComponent<NameComponent>()->Value, "Captured actor");
    });
    // Ordinary placement of the same mesh remains supported and clears the
    // imported name/new-binding policy from the previous request.
    IncomingLoaded = true;
    Start();
    RunQueued();
    ASSERT_TRUE(Process());
    ASSERT_EQ(Owner.Instances.size(), 2u);
    EXPECT_STREQ(Owner.Instances.back().Name, "triangle");
}

TEST_F(EditorMeshDropTest, ImportedPlacementNeverSilentlyReusesStaleRecookedGeometry)
{
    Preview(2.f, true);
    ASSERT_TRUE(App->RequestImportedMesh(*Engine, Path, "Reimport", Owner.UUID, App->Session.SceneEpoch()));
    ASSERT_TRUE(Process());
    Preview(5.f);
    RunQueued();
    EXPECT_FALSE(Process());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
    EXPECT_TRUE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Original)->Position.x, 5.f);
}

TEST_F(EditorMeshDropTest, ImportedRequestRejectsWrongOriginWorldReadOnlyThreadAndNamesWithoutTakingSlot)
{
    const auto epoch = App->Session.SceneEpoch();
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", {}, epoch));
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", MeshID, epoch));
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, epoch + 1));
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, std::string(256, 'x'), Owner.UUID, epoch));
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, std::string_view("a\0b", 3), Owner.UUID, epoch));
    Owner.SourceReadOnly = true;
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, epoch));
    Owner.SourceReadOnly = false;
    Engine->Scene        = nullptr;
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, epoch));
    Engine->Scene = &World;
    Engine->VFS   = nullptr;
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, epoch));
    Engine->VFS          = &VFS;
    bool        accepted = true;
    std::thread worker([&] { accepted = App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, epoch); });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_FALSE(App->MeshDrop.Pending());
    EXPECT_EQ(Submissions, 0u);
}

TEST_F(EditorMeshDropTest, BusyImportPlacementPreservesTheOriginalRequest)
{
    IncomingLoaded = false;
    ASSERT_TRUE(App->RequestImportedMesh(*Engine, Path, "First", Owner.UUID, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "Second", Owner.UUID, App->Session.SceneEpoch()));
    ASSERT_TRUE(Process());
    RunQueued();
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "Third", Owner.UUID, App->Session.SceneEpoch()));
    ASSERT_TRUE(Process());
    ASSERT_EQ(Owner.Instances.size(), 1u);
    EXPECT_STREQ(Owner.Instances[0].Name, "First");
}

TEST_F(EditorMeshDropTest, ImportedPlacementCannotCrossVFSReplacementBeforeOrAfterWorkerLaunch)
{
    VFSContext other = {};
    IncomingLoaded   = false;
    for (bool launch : {false, true})
    {
        ASSERT_TRUE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, App->Session.SceneEpoch()));
        if (launch)
        {
            ASSERT_TRUE(Process());
            RunQueued();
        }
        Engine->VFS = &other;
        EXPECT_FALSE(Process());
        EXPECT_FALSE(App->MeshDrop.Pending());
        Engine->VFS = &VFS;
    }
    EXPECT_EQ(Submissions, 1u);
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
}

TEST_F(EditorMeshDropTest, ForcedCloseRejectsPlacementBeforeAndAfterWorkerLaunch)
{
    IncomingLoaded = false;
    for (bool launch : {false, true})
    {
        ASSERT_TRUE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, App->Session.SceneEpoch()));
        if (launch)
        {
            ASSERT_TRUE(Process());
            RunQueued();
        }
        Engine->CloseRequested.value.store(true);
        EXPECT_FALSE(Process());
        EXPECT_FALSE(App->MeshDrop.Pending());
        EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, App->Session.SceneEpoch()));
        Engine->CloseRequested.value.store(false);
    }
    EXPECT_EQ(Submissions, 1u);
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
}

TEST_F(EditorMeshDropTest, PendingEditorCloseRejectsBothImportedAndCookedPlacement)
{
    Start();
    RunQueued();
    EXPECT_FALSE(App->OnCloseRequested());
    EXPECT_TRUE(Process());
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->MeshDrop.Pending());
}

TEST_F(EditorMeshDropTest, PendingSceneOpenRejectsBothImportedAndCookedPlacement)
{
    Start();
    RunQueued();
    ASSERT_TRUE(App->RequestSceneOpen(VFSPath::Parse("/next.zescene").Value(), App->Session.SceneEpoch()));
    EXPECT_TRUE(Process());
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->MeshDrop.Pending());
}

TEST_F(EditorMeshDropTest, NewSceneCancelsPlacementAlreadyBeingDecoded)
{
    Start();
    ASSERT_TRUE(App->RequestNewScene());
    RunQueued();
    EXPECT_TRUE(Process());
    EXPECT_FALSE(App->RequestImportedMesh(*Engine, Path, "actor", Owner.UUID, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->RequestMeshDrop(Path, App->Session.SceneEpoch()));
    EXPECT_FALSE(App->MeshDrop.Pending());
    EXPECT_EQ(Binds, 0u);
    EXPECT_EQ(Actors.Count(), 0u);
}

TEST_F(EditorMeshDropTest, ExplicitActorNamesAreCopiedAndTruncatedAtUtf8Boundaries)
{
    for (const std::string character : {"\xc3\xa9", "\xe7\x8c\xab", "\xf0\x9f\x94\xa7"})
    {
        for (bool split : {false, true})
        {
            const std::string prefix(127 - character.size() + (split ? 1 : 0), 'a');
            std::string       name = prefix + character + "suffix";
            ASSERT_TRUE(App->MeshDrop.Request(Path, App->Session.SceneEpoch(), name, &VFS));
            name.clear();
            ASSERT_TRUE(Process());
            RunQueued();
            ASSERT_TRUE(Process());
            ASSERT_FALSE(Owner.Instances.empty());
            const std::string expected = split ? prefix : prefix + character;
            EXPECT_STREQ(Owner.Instances.back().Name, expected.c_str());
        }
    }
}
