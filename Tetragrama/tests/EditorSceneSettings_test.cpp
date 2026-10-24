#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <thread>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using ZEngine::Rendering::Scenes::SkyConfig;

class EditorSceneSettingsTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    Memory      = {};
    Scene                                   World       = {};
    EditorScene                             Owner       = {};
    std::unique_ptr<Editor>                 App         = {};
    std::unique_ptr<ZEngine::EngineContext> Engine      = {};
    SceneDiagnostics                        Diagnostics = {};
    EntityID                                Entity      = {};

    virtual EditorSessionConfig             SessionConfig() const
    {
        return {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096};
    }

    void SetUp() override
    {
        Memory.Initialize(ZMega(64), {});
        World.Initialize(&Memory.MainArena);
        Entity = World.CreateEntity();
        World.AddComponent<UUIDComponent>(Entity, {*uuids::uuid::from_string("22222222-2222-4222-8222-222222222222")});
        World.AddComponent<TransformComponent>(Entity, {});
        App = std::make_unique<Editor>();
        ASSERT_TRUE(App->Session.Initialize(&Memory.MainArena, SessionConfig()));
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        ASSERT_TRUE(App->Session.BindScene(&World));
        App->Session.EndMutationPhase();
        Owner.UUID        = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        Owner.Session     = &App->Session;
        App->CurrentScene = &Owner;
        Engine            = std::make_unique<ZEngine::EngineContext>();
        Engine->Scene     = &World;
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        ASSERT_TRUE(Owner.AttachSession(App->Session, *Engine));
        App->Session.EndMutationPhase();
    }

    void TearDown() override
    {
        App->OnClosing();
        App->CurrentScene = nullptr;
        World.Shutdown();
    }

    bool Process(bool drain = true, bool editable = true)
    {
        if (!App->Session.BeginMutationPhase(editable))
        {
            return false;
        }
        const bool result = (!drain || App->Session.ProcessRequests()) && App->ProcessSceneSettings(*Engine, &Diagnostics);
        App->Session.EndMutationPhase();
        return result;
    }

    void Edit(float x)
    {
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        const auto target = App->Session.TargetFor(Entity);
        ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = x;
        ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
        ASSERT_TRUE(App->Session.Commit());
        App->Session.EndMutationPhase();
    }

    uuids::uuid MakeLight()
    {
        if (!World.HasComponent<LightComponent>(Entity))
        {
            World.AddComponent<LightComponent>(Entity, {});
        }
        return World.GetComponent<UUIDComponent>(Entity)->Value;
    }
};

class EditorSceneSettingsSmallHistoryTest : public EditorSceneSettingsTest
{
protected:
    EditorSessionConfig SessionConfig() const override
    {
        return {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 192};
    }
};

TEST_F(EditorSceneSettingsTest, GridSelectionIntentChecksBoundOwnerAndThreadWithoutRequiringEditability)
{
    Owner.SourceReadOnly = true;
    ASSERT_TRUE(App->RequestSceneGridSelection());
    EXPECT_EQ(App->Session.Selection().ActiveSceneTarget(), SceneTarget::None);
    EXPECT_EQ(App->Session.PendingRequestCount(), 1u);
    ASSERT_TRUE(Process(true, false));
    EXPECT_EQ(App->Session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
    EXPECT_FALSE(App->Session.Editable());
    EXPECT_FALSE(App->Session.Dirty());
    bool        accepted = true;
    std::thread worker([&] { accepted = App->RequestSceneGridSelection(); });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_EQ(App->Session.PendingRequestCount(), 0u);

    EditorScene wrong = {};
    wrong.UUID        = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    wrong.Session     = &App->Session;
    App->CurrentScene = &wrong;
    EXPECT_FALSE(App->RequestSceneGridSelection());
    App->CurrentScene = nullptr;
    EXPECT_FALSE(App->RequestSceneGridSelection());
    App->CurrentScene = &Owner;
    Engine->Scene     = nullptr;
    EXPECT_FALSE(App->RequestSceneGridSelection());
    Engine->Scene = &World;
    EXPECT_EQ(App->Session.PendingRequestCount(), 0u);
}

TEST_F(EditorSceneSettingsTest, GridSelectionIntentRetiresWithTheEpochAndRejectsPendingSceneChanges)
{
    ASSERT_TRUE(App->RequestSceneGridSelection());
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(&World));
    EXPECT_EQ(App->Session.PendingRequestCount(), 0u);
    App->Session.EndMutationPhase();
    EXPECT_FALSE(App->RequestSceneGridSelection()); // no settings owner in the new epoch
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(Owner.AttachSession(App->Session, *Engine));
    App->Session.EndMutationPhase();
    ASSERT_TRUE(App->RequestNewScene());
    EXPECT_FALSE(App->RequestSceneGridSelection());
    EXPECT_EQ(App->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(App->Session.Selection().ActiveSceneTarget(), SceneTarget::None);
}

TEST_F(EditorSceneSettingsTest, GridSelectionQueuePressureRejectsWithoutMutationAndRepeatedSelectionReusesStorage)
{
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Select(Entity));
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&value, 1}));
    App->Session.EndMutationPhase();
    const auto offset = Memory.MainArena.m_current_offset;
    uint32_t   queued = 0;
    while (queued < 1024 && App->RequestSceneGridSelection())
    {
        ++queued;
    }
    ASSERT_GT(queued, 0u);
    ASSERT_LT(queued, 1024u);
    EXPECT_EQ(App->Session.PendingRequestCount(), queued);
    EXPECT_TRUE(App->Session.Active());
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_EQ(App->Session.Selection().ActiveUUID(), target.UUID);
    ASSERT_TRUE(Process());
    const auto revision = App->Session.Selection().Revision();
    for (uint32_t i = 0; i < 2000; ++i)
    {
        ASSERT_TRUE(App->RequestSceneGridSelection());
        ASSERT_TRUE(Process());
    }
    EXPECT_EQ(App->Session.Selection().Revision(), revision);
    EXPECT_EQ(App->Session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FALSE(App->Session.Dirty());
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
}

TEST_F(EditorSceneSettingsTest, GridFocusResolvesOnlyExplicitSelectionIncludingReadonlyScenesWithoutAnActor)
{
    using ZEngine::Core::Maths::Vec3f;
    Vec3f output = {91.f, 92.f, 93.f};
    EXPECT_FALSE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
    auto grid        = Owner.AuthoredGrid;
    grid.Origin      = {4.f, -7.f, 9.f};
    grid.Orientation = {.5f, -.5f, .5f, .5f};
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(App->RequestSceneGridSelection());
    ASSERT_TRUE(Process());
    ASSERT_TRUE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, grid.Origin);
    EXPECT_EQ(Engine->ActorManager, nullptr);
    Owner.SourceReadOnly = true;
    ASSERT_TRUE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, grid.Origin);
    ASSERT_TRUE(App->Session.BeginMutationPhase(false));
    ASSERT_TRUE(App->Session.Select(Entity));
    App->Session.EndMutationPhase();
    output = {91.f, 92.f, 93.f};
    EXPECT_FALSE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
}

TEST_F(EditorSceneSettingsTest, InvalidGridFocusOwnerEpochWorldOrThreadPreservesThePreviousOutput)
{
    using ZEngine::Core::Maths::Vec3f;
    ASSERT_TRUE(App->RequestSceneGridSelection());
    ASSERT_TRUE(Process());
    Vec3f       output = {91.f, 92.f, 93.f};
    EditorScene wrong  = {};
    wrong.Session      = &App->Session;
    wrong.UUID         = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    EXPECT_FALSE(wrong.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
    bool        accepted = true;
    std::thread worker([&] { accepted = Owner.TryGetSelectedGridOrigin(output); });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
    const auto before           = Owner.AuthoredGrid;
    Owner.AuthoredGrid.Origin.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
    Owner.AuthoredGrid = before;
    Engine->Scene      = nullptr;
    EXPECT_FALSE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
    Engine->Scene = &World;
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BindScene(&World));
    App->Session.EndMutationPhase();
    EXPECT_FALSE(Owner.TryGetSelectedGridOrigin(output));
    EXPECT_EQ(output, (Vec3f{91.f, 92.f, 93.f}));
}

TEST_F(EditorSceneSettingsTest, CoalescesCopiedValuesWithoutWritingOrAllocatingUntilExclusivePhase)
{
    Edit(4.f);
    const auto before   = Owner.Sky;
    const auto revision = App->Session.DocumentRevision();
    const auto external = Owner.ExternalRevision();
    const auto bytes    = Memory.MainArena.m_current_offset;
    for (uint32_t i = 1; i <= 2000; ++i)
    {
        auto sky                 = App->SceneSkyForUI();
        sky.EnvironmentIntensity = float(i);
        ASSERT_TRUE(App->RequestSceneSkySettings(sky));
        auto grid     = App->SceneGridForUI();
        grid.Origin.y = float(i);
        ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    }
    EXPECT_EQ(Owner.Sky, before);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 0.f);
    EXPECT_FLOAT_EQ(App->SceneSkyForUI().EnvironmentIntensity, 2000.f);
    EXPECT_FLOAT_EQ(App->SceneGridForUI().Origin.y, 2000.f);
    EXPECT_TRUE(App->Session.CanUndo());
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
    EXPECT_EQ(Owner.ExternalRevision(), external);
    EXPECT_EQ(Memory.MainArena.m_current_offset, bytes);
    ASSERT_TRUE(Process());
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 2000.f);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 2000.f);
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.y, 2000.f);
    EXPECT_TRUE(App->Session.CanUndo()); // Grid remains undoable even beside a legacy Sky edit.
    EXPECT_EQ(App->Session.DocumentRevision(), revision + 2);
    EXPECT_EQ(Owner.ExternalRevision(), external + 1);
    EXPECT_TRUE(Owner.IsDirty());
    EXPECT_EQ(Memory.MainArena.m_current_offset, bytes);
}

TEST_F(EditorSceneSettingsTest, ReturningBothPagesToOriginalPreservesHistoryAndRevisions)
{
    Edit(4.f);
    const auto revision    = App->Session.DocumentRevision();
    const auto external    = Owner.ExternalRevision();
    auto       sky         = Owner.Sky;
    sky.Sphere.ShowSunDisc = false;
    auto grid              = Owner.AuthoredGrid;
    grid.Enabled           = false;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(App->RequestSceneSkySettings(Owner.Sky));
    ASSERT_TRUE(App->RequestSceneGridSettings(Owner.AuthoredGrid));
    ASSERT_TRUE(Process());
    EXPECT_TRUE(App->Session.CanUndo());
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
    EXPECT_EQ(Owner.ExternalRevision(), external);
}

TEST_F(EditorSceneSettingsTest, GridOnlyChangesShareEntityHistoryAndUndoDoesNotLeaveStickyExternalDirtiness)
{
    Edit(4.f);
    const auto original = Owner.AuthoredGrid;
    const auto revision = App->Session.DocumentRevision(), external = Owner.ExternalRevision();
    auto       grid     = original;
    grid.Origin         = {4.f, 7.f, 9.f};
    grid.Orientation    = {.2f, -.3f, .4f, .5f};
    grid.BaseSpacing    = .5f;
    grid.MajorLineEvery = 7;
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(Process());
    const auto actual = Owner.AuthoredGrid;
    EXPECT_EQ(App->Session.History().UndoCount(), 2u);
    EXPECT_EQ(App->Session.DocumentRevision(), revision + 1);
    EXPECT_EQ(Owner.ExternalRevision(), external);
    EXPECT_FALSE(Owner.Dirty.value.load());
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Undo());
    EXPECT_TRUE(EqualSceneGridSettings(Owner.AuthoredGrid, original));
    SceneGridSettings runtime = {};
    ASSERT_TRUE(ZEngine::Rendering::Scenes::ReadSceneGridRenderSnapshot(Owner.Grid, runtime));
    EXPECT_TRUE(EqualSceneGridSettings(runtime, original));
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.y, 0.f);
    EXPECT_FLOAT_EQ(Owner.Grid.CellSize, original.BaseSpacing);
    ASSERT_TRUE(App->Session.Undo());
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 0.f);
    EXPECT_FALSE(Owner.IsDirty());
    ASSERT_TRUE(App->Session.Redo());
    ASSERT_TRUE(App->Session.Redo());
    EXPECT_TRUE(EqualSceneGridSettings(Owner.AuthoredGrid, actual));
    ASSERT_TRUE(ZEngine::Rendering::Scenes::ReadSceneGridRenderSnapshot(Owner.Grid, runtime));
    EXPECT_TRUE(EqualSceneGridSettings(runtime, actual));
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.y, 7.f);
    EXPECT_FLOAT_EQ(Owner.Grid.CellSize, .5f);
    ASSERT_TRUE(App->Session.MarkSaved());
    ASSERT_TRUE(App->Session.Undo());
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(App->Session.Redo());
    EXPECT_FALSE(Owner.IsDirty());
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneSettingsTest, CoalescedSkyAndGridStillGiveGridOneUndoableSharedOperation)
{
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    auto grid                = Owner.AuthoredGrid;
    grid.Origin.y            = 7.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(Process());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_EQ(Owner.ExternalRevision(), 1u);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.Undo());
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 0.f);
    EXPECT_EQ(Owner.Sky, sky); // Sky retains its existing untracked edit semantics.
    EXPECT_TRUE(Owner.IsDirty());
    ASSERT_TRUE(App->Session.Redo());
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 7.f);
    EXPECT_EQ(Owner.ExternalRevision(), 1u);
    App->Session.EndMutationPhase();
}

TEST_F(EditorSceneSettingsSmallHistoryTest, CompoundBudgetFailurePreservesBothPagesAndUnrelatedLivePreview)
{
    Edit(4.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    App->Session.EndMutationPhase();
    const auto revision      = App->Session.DocumentRevision();
    auto       sky           = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    auto grid                = Owner.AuthoredGrid;
    grid.Origin.y            = 7.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    EXPECT_FALSE(Process());
    EXPECT_TRUE(App->Session.Active());
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 1.f);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 0.f);
    EXPECT_EQ(Owner.ExternalRevision(), 0u);
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
}

TEST_F(EditorSceneSettingsTest, GridOwnerRegistrationFailureCannotRetargetExistingSceneMutationHooks)
{
    EditorScene replacement = {};
    replacement.UUID        = Owner.UUID;
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    EXPECT_FALSE(replacement.AttachSession(App->Session, *Engine));
    EXPECT_EQ(replacement.Session, nullptr);
    App->Session.EndMutationPhase();
    auto grid     = Owner.AuthoredGrid;
    grid.Origin.y = 3.f;
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    ASSERT_TRUE(Process());
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 3.f);
    EXPECT_FLOAT_EQ(replacement.AuthoredGrid.Origin.y, 0.f);
}

TEST_F(EditorSceneSettingsTest, GridSceneReadOnlyTransitionRestoresOnlyTheOwnedPreviewAndPreservesHistory)
{
    Edit(4.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    ASSERT_TRUE(App->Session.BeginGridSettings());
    auto grid     = Owner.AuthoredGrid;
    grid.Origin.y = 3.f;
    ASSERT_TRUE(App->Session.PreviewGridSettings(grid));
    App->Session.EndMutationPhase();
    Owner.SourceReadOnly = true;
    ASSERT_TRUE(Process());
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 0.f);
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.y, 0.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_EQ(Owner.ExternalRevision(), 0u);
}

TEST_F(EditorSceneSettingsTest, OutsidePhaseAndEarlierSessionRequestsRetainSettingsUntilDrained)
{
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    EXPECT_FALSE(App->ProcessSceneSettings(*Engine));
    ASSERT_TRUE(App->Session.RequestSelection(Entity));
    EXPECT_FALSE(Process(false));
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 1.f);
    EXPECT_FLOAT_EQ(App->SceneSkyForUI().EnvironmentIntensity, 3.f);
    ASSERT_TRUE(Process());
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 3.f);
    EXPECT_EQ(App->Session.Selection().ActiveUUID(), World.GetComponent<UUIDComponent>(Entity)->Value);
}

TEST_F(EditorSceneSettingsTest, InvalidNumbersAndEnumsNeverClearHistoryOrReplaceAcceptedIntent)
{
    Edit(4.f);
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    sky.EnvironmentIntensity = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(App->RequestSceneSkySettings(sky));
    sky      = Owner.Sky;
    sky.Mode = static_cast<ZEngine::Rendering::Scenes::SkyMode>(255);
    EXPECT_FALSE(App->RequestSceneSkySettings(sky));
    auto grid        = Owner.AuthoredGrid;
    grid.BaseSpacing = 0.f;
    EXPECT_FALSE(App->RequestSceneGridSettings(grid));
    grid             = Owner.AuthoredGrid;
    grid.Orientation = {0.f, 0.f, 0.f, 0.f};
    EXPECT_FALSE(App->RequestSceneGridSettings(grid));
    grid        = Owner.AuthoredGrid;
    grid.MaxLOD = 17;
    EXPECT_FALSE(App->RequestSceneGridSettings(grid));
    grid           = Owner.AuthoredGrid;
    grid.VColor[3] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(App->RequestSceneGridSettings(grid));
    EXPECT_TRUE(App->Session.CanUndo());
    EXPECT_FLOAT_EQ(App->SceneSkyForUI().EnvironmentIntensity, 3.f);
    ASSERT_TRUE(Process());
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 3.f);
}

TEST_F(EditorSceneSettingsTest, StaleGridRejectsBothPagesAtomicallyWithoutCancellingPreviewOrHistory)
{
    Edit(4.f);
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    auto grid                = Owner.AuthoredGrid;
    grid.Origin.y            = 7.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    Owner.AuthoredGrid.BaseSpacing += 0.000001f;
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    const auto revision = App->Session.DocumentRevision();
    EXPECT_FALSE(App->ProcessSceneSettings(*Engine, &Diagnostics));
    EXPECT_TRUE(App->Session.Active());
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 9.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 1u);
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 1.f);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.y, 0.f);
    EXPECT_GT(Diagnostics.Count, 0u);
    App->Session.Cancel();
    App->Session.EndMutationPhase();
    EXPECT_EQ(App->SceneSkyForUI(), Owner.Sky);
    EXPECT_TRUE(Process()); // Rejected intent is consumed, not retried forever.
}

TEST_F(EditorSceneSettingsTest, SuccessfulSettingsRestorePreviewBeforeInvalidatingIncompatibleHistory)
{
    Edit(4.f);
    ASSERT_TRUE(App->Session.BeginMutationPhase());
    const auto target = App->Session.TargetFor(Entity);
    ASSERT_TRUE(App->Session.BeginTransform({&target, 1}));
    LocalTransformValue preview = {};
    preview.Position.x          = 9.f;
    ASSERT_TRUE(App->Session.PreviewTransforms({&preview, 1}));
    App->Session.EndMutationPhase();
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(Process());
    EXPECT_FALSE(App->Session.Active());
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 4.f);
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 3.f);
    EXPECT_EQ(App->Session.History().UndoCount(), 0u);
    EXPECT_EQ(App->Session.History().BytesUsed(), 0u);
}

TEST_F(EditorSceneSettingsTest, OwnerEpochWorldReadOnlyAndForcedCloseAreRevalidatedBeforeWrites)
{
    Edit(4.f);
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    for (uint32_t fault = 0; fault < 6; ++fault)
    {
        SCOPED_TRACE(fault);
        ASSERT_TRUE(App->RequestSceneSkySettings(sky));
        const auto identity = Owner.UUID;
        if (fault == 0)
        {
            Owner.UUID = {};
        }
        if (fault == 1)
        {
            Owner.SourceReadOnly = true;
        }
        if (fault == 2)
        {
            Engine->Scene = nullptr;
        }
        if (fault == 3)
        {
            Engine->CloseRequested.value.store(true);
        }
        if (fault == 4)
        {
            ASSERT_TRUE(App->Session.BeginMutationPhase());
            ASSERT_TRUE(App->Session.BindScene(&World));
            App->Session.EndMutationPhase();
        }
        EXPECT_FALSE(Process(true, fault != 5));
        EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 1.f);
        EXPECT_EQ(Owner.ExternalRevision(), 0u);
        EXPECT_EQ(App->SceneSkyForUI(), Owner.Sky);
        Owner.UUID           = identity;
        Owner.SourceReadOnly = false;
        Engine->Scene        = &World;
        Engine->CloseRequested.value.store(false);
        ASSERT_TRUE(App->Session.BeginMutationPhase());
        App->Session.EndMutationPhase();
    }
}

TEST_F(EditorSceneSettingsTest, WrongThreadCannotQueueReadProcessOrDiscardOwnerIntent)
{
    auto sky                 = Owner.Sky;
    sky.EnvironmentIntensity = 3.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    std::thread worker([&] {
        EXPECT_FALSE(App->RequestSceneSkySettings(sky));
        EXPECT_FALSE(App->RequestSceneGridSettings({}));
        EXPECT_EQ(App->SceneSkyForUI(), SkyConfig{});
        EXPECT_FALSE(App->ProcessSceneSettings(*Engine));
        App->DiscardSceneSettings();
    });
    worker.join();
    EXPECT_FLOAT_EQ(App->SceneSkyForUI().EnvironmentIntensity, 3.f);
    ASSERT_TRUE(Process());
    EXPECT_FLOAT_EQ(Owner.Sky.EnvironmentIntensity, 3.f);
}

TEST_F(EditorSceneSettingsTest, PureECSPrimaryLightIsAcceptedAndClearingIsSupported)
{
    auto sky                  = Owner.Sky;
    sky.PrimaryCelestialLight = MakeLight();
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    EXPECT_TRUE(Owner.Sky.PrimaryCelestialLight.is_nil());
    ASSERT_TRUE(Process());
    EXPECT_EQ(Owner.Sky.PrimaryCelestialLight, sky.PrimaryCelestialLight);
    sky.PrimaryCelestialLight = {};
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(Process());
    EXPECT_TRUE(Owner.Sky.PrimaryCelestialLight.is_nil());
}

TEST_F(EditorSceneSettingsTest, NewPrimaryMustResolveUniquelyToDirectionalLight)
{
    auto sky                  = Owner.Sky;
    sky.PrimaryCelestialLight = World.GetComponent<UUIDComponent>(Entity)->Value;
    EXPECT_FALSE(App->RequestSceneSkySettings(sky));
    World.AddComponent<LightComponent>(Entity, {.LightType = LightComponent::Type::Point});
    EXPECT_FALSE(App->RequestSceneSkySettings(sky));
    World.GetComponent<LightComponent>(Entity)->LightType = LightComponent::Type::Directional;
    const auto duplicate                                  = World.CreateEntity();
    World.AddComponent<UUIDComponent>(duplicate, {sky.PrimaryCelestialLight});
    EXPECT_FALSE(App->RequestSceneSkySettings(sky));
    EXPECT_TRUE(Process());
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneSettingsTest, DeletedRecycledRetypedAndDuplicatedPrimaryInvalidateTheWholeRequest)
{
    for (uint32_t fault = 0; fault < 4; ++fault)
    {
        SCOPED_TRACE(fault);
        auto sky                  = Owner.Sky;
        sky.PrimaryCelestialLight = MakeLight();
        ASSERT_TRUE(App->RequestSceneSkySettings(sky));
        auto grid    = Owner.AuthoredGrid;
        grid.Enabled = false;
        ASSERT_TRUE(App->RequestSceneGridSettings(grid));
        EntityID duplicate = {};
        if (fault == 0 || fault == 1)
        {
            World.DestroyEntity(Entity);
            Entity = World.CreateEntity();
            World.AddComponent<UUIDComponent>(Entity, {fault == 1 ? sky.PrimaryCelestialLight : *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")});
            World.AddComponent<TransformComponent>(Entity, {});
            World.AddComponent<LightComponent>(Entity, {});
        }
        if (fault == 2)
        {
            World.GetComponent<LightComponent>(Entity)->LightType = LightComponent::Type::Point;
        }
        if (fault == 3)
        {
            duplicate = World.CreateEntity();
            World.AddComponent<UUIDComponent>(duplicate, {sky.PrimaryCelestialLight});
        }
        sky.EnvironmentIntensity = 5.f;
        EXPECT_FALSE(App->RequestSceneSkySettings(sky)); // Coalescing cannot recapture another generation.
        EXPECT_FALSE(Process());
        EXPECT_TRUE(Owner.Sky.PrimaryCelestialLight.is_nil());
        EXPECT_TRUE(Owner.AuthoredGrid.Enabled);
        EXPECT_EQ(Owner.ExternalRevision(), 0u);
        if (duplicate.IsValid())
        {
            World.DestroyEntity(duplicate);
        }
        World.GetComponent<LightComponent>(Entity)->LightType = LightComponent::Type::Directional;
    }
}

TEST_F(EditorSceneSettingsTest, ExistingMissingPrimaryReferenceSurvivesUnrelatedSettingsForDiagnostics)
{
    Owner.Sky.PrimaryCelestialLight = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    auto sky                        = Owner.Sky;
    sky.EnvironmentIntensity        = 3.f;
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    ASSERT_TRUE(Process());
    EXPECT_EQ(Owner.Sky, sky);
}

TEST_F(EditorSceneSettingsTest, PrimaryWithoutTransformIsRejectedAtCaptureAndAfterComponentRemoval)
{
    auto sky                  = Owner.Sky;
    sky.PrimaryCelestialLight = MakeLight();
    World.RemoveComponent<TransformComponent>(Entity);
    EXPECT_FALSE(App->RequestSceneSkySettings(sky));
    World.AddComponent<TransformComponent>(Entity, {});
    ASSERT_TRUE(App->RequestSceneSkySettings(sky));
    World.RemoveComponent<TransformComponent>(Entity);
    EXPECT_FALSE(Process());
    EXPECT_TRUE(Owner.Sky.PrimaryCelestialLight.is_nil());
    EXPECT_FALSE(Owner.IsDirty());
}

TEST_F(EditorSceneSettingsTest, GridCanonicalizationAndRuntimeAdapterPreserveHiddenFields)
{
    auto grid           = Owner.AuthoredGrid;
    grid.Orientation    = {0.f, 0.f, 0.f, -2.f};
    grid.Origin         = {4.f, 7.f, 9.f};
    grid.BaseSpacing    = .5f;
    grid.MajorLineEvery = 7;
    grid.Labels         = SceneGridLabels::UV;
    grid.MinorColor     = {.1f, .2f, .3f, .4f};
    grid.MajorColor     = {.2f, .3f, .4f, .5f};
    grid.UColor         = {.3f, .4f, .5f, .6f};
    grid.VColor         = {.4f, .5f, .6f, .7f};
    grid.MaxLOD         = 16;
    grid.FadeRadius     = 1200.f;
    grid.FadeStrength   = 1.25f;
    grid.LineWidth      = 3.f;
    ASSERT_TRUE(App->RequestSceneGridSettings(grid));
    EXPECT_FLOAT_EQ(App->SceneGridForUI().Orientation.w, 1.f);
    ASSERT_TRUE(Process());
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.x, 4.f);
    EXPECT_FLOAT_EQ(Owner.AuthoredGrid.Origin.z, 9.f);
    EXPECT_EQ(Owner.AuthoredGrid.MajorLineEvery, 7u);
    EXPECT_EQ(Owner.AuthoredGrid.Labels, SceneGridLabels::UV);
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.x, 4.f);
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.z, 9.f);
    EXPECT_FLOAT_EQ(Owner.Grid.Orientation.w, 1.f);
    EXPECT_EQ(Owner.Grid.MajorLineEvery, 7u);
    EXPECT_EQ(Owner.Grid.Labels, uint32_t(SceneGridLabels::UV));
    EXPECT_FLOAT_EQ(Owner.Grid.Origin.y, 7.f);
    EXPECT_FLOAT_EQ(Owner.Grid.CellSize, .5f);
    EXPECT_FLOAT_EQ(Owner.Grid.FadeRadius, 1200.f);
    EXPECT_FLOAT_EQ(Owner.Grid.FadeStrength, 1.25f);
    EXPECT_FLOAT_EQ(Owner.Grid.LineWidth, 3.f);
    EXPECT_EQ(Owner.Grid.MaxLOD, 16);
    for (uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_FLOAT_EQ(Owner.Grid.ColorThin[i], grid.MinorColor[i]);
        EXPECT_FLOAT_EQ(Owner.Grid.ColorThick[i], grid.MajorColor[i]);
        EXPECT_FLOAT_EQ(Owner.Grid.ColorUAxis[i], grid.UColor[i]);
        EXPECT_FLOAT_EQ(Owner.Grid.ColorVAxis[i], grid.VColor[i]);
    }
    for (auto& dirty : Owner.GridDirty)
    {
        EXPECT_TRUE(dirty.value.load());
    }
}

TEST_F(EditorSceneSettingsTest, FocusLossAndEscapeRetireDelayedSettingsBeforeTheyCanWrite)
{
    auto ui = std::make_unique<ZEngine::UI::ZUIContext>();
    for (bool focus : {false, true})
    {
        auto sky                 = Owner.Sky;
        sky.EnvironmentIntensity = 3.f;
        ASSERT_TRUE(App->RequestSceneSkySettings(sky));
        ui->EscapePressed = focus;
        App->UpdateSessionInput(ui.get(), focus, Engine.get());
        EXPECT_EQ(Owner.Sky, SkyConfig{});
        EXPECT_EQ(App->SceneSkyForUI(), Owner.Sky);
        EXPECT_FALSE(Owner.IsDirty());
        EXPECT_FALSE(ui->EscapePressed);
    }
}

TEST_F(EditorSceneSettingsTest, RepeatedCommittedSettingsReuseArenaAndIdleDoesNotChangeRevisions)
{
    ASSERT_TRUE(Process());
    const auto bytes = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 2000; ++i)
    {
        auto sky                 = Owner.Sky;
        sky.EnvironmentIntensity = float(i + 2);
        ASSERT_TRUE(App->RequestSceneSkySettings(sky));
        ASSERT_TRUE(Process());
    }
    const auto revision = App->Session.DocumentRevision();
    const auto external = Owner.ExternalRevision();
    for (uint32_t i = 0; i < 2000; ++i)
    {
        ASSERT_TRUE(Process());
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(App->Session.DocumentRevision(), revision);
    EXPECT_EQ(Owner.ExternalRevision(), external);
    EXPECT_EQ(App->Session.History().BytesUsed(), 0u);
}
