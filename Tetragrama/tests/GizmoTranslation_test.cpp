#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <limits>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
namespace G = ZEngine::Core::Maths::Geometry;

class GizmoTranslationTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    Scene                                scene      = {};
    ActorManager                         actors     = {};
    EditorSession                        session    = {};
    EntityID                             entity     = {};
    G::ProjectionFrame                   projection = {};
    G::Viewport                          bounds     = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    static uuids::uuid UUID(uint8_t value)
    {
        std::array<uint8_t, 16> bytes = {};
        bytes.back()                  = value;
        return uuids::uuid(bytes);
    }
    EntityID Entity(uint8_t identity, bool transform = true)
    {
        const auto result = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(result, {UUID(identity)});
        if (transform)
        {
            scene.AddComponent<TransformComponent>(result, {});
        }
        return result;
    }
    void SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, scene);
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
        entity = Entity(1);
        ASSERT_TRUE(session.Select(entity));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
        ASSERT_TRUE(session.ProcessRequests());
        using namespace ZEngine::Rendering::Cameras;
        FlyCamera camera(4.f / 3.f, {});
        camera.SetPosition({0.f, 0.f, 10.f});
        camera.SetOrientation(0.f, 0.f);
        const auto captured = camera.CaptureFrameData();
        ASSERT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, G::ProjectionKind::Perspective, false, &projection));
    }
    void TearDown() override
    {
        session.Shutdown();
        actors.Shutdown();
        scene.Shutdown();
    }
    Vec2f Screen(const Vec3f& position)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(position, projection, bounds, &result));
        return result.Screen;
    }
    TranslationBegin Input(Handle handle = Handle::TranslateX, const Vec3f& grab = {.7f, 0.f, 0.f})
    {
        scene.SynchronizeHierarchy();
        TranslationBegin input = {.Projection = projection, .Bounds = bounds, .Key = 123, .PointerSerial = 456, .Constraint = handle};
        EXPECT_TRUE(ResolveTargetFrame(session, input.Expected));
        input.Pointer = Screen(input.Expected.Pivot + grab);
        return input;
    }
    LocalTransformValue Value()
    {
        return CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    }
    void NearPosition(const Vec3f& expected)
    {
        const auto actual = Value().Position;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(actual[axis], expected[axis], .0002f) << axis;
        }
    }
};

TEST_F(GizmoTranslationTest, PresentationCopiesTheSynchronizedPreviewAndImmutableStartPose)
{
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Position = {.3f, .4f, .5f};
    transform->Rotation = {.2f, -.3f, .4f};
    transform->Scale    = {-2.f, 3.f, .5f};
    const auto input    = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    TranslationSnapshot start = {};
    ASSERT_TRUE(session.ReadTranslationSnapshot(start));
    EXPECT_EQ(start.Target.UUID, input.Expected.Target.UUID);
    EXPECT_EQ(start.Key, input.Key);
    EXPECT_EQ(start.PointerSerial, input.PointerSerial);
    EXPECT_EQ(start.Constraint, input.Constraint);
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Vec3f(1.2f, 0.f, 0.f)), true));
    TranslationSnapshot pending = {.Key = 99};
    EXPECT_FALSE(session.ReadTranslationSnapshot(pending));
    EXPECT_EQ(pending.Key, 99u);
    session.EndMutationPhase();
    TranslationSnapshot preview = {};
    ASSERT_TRUE(session.ReadTranslationSnapshot(preview));
    EXPECT_NEAR(preview.Components.x, .5f, .0002f);
    EXPECT_NEAR(preview.Pivot.x, .8f, .0002f);
    EXPECT_EQ(preview.StartPivot.x, start.StartPivot.x);
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            EXPECT_EQ(preview.StartWorld(row, column), start.StartWorld(row, column));
            EXPECT_EQ(preview.World(row, column), transform->WorldTransform(row, column));
        }
    }
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.UpdateTranslation({std::numeric_limits<float>::quiet_NaN(), 0.f}, true));
    session.EndMutationPhase();
    TranslationSnapshot retained = {};
    ASSERT_TRUE(session.ReadTranslationSnapshot(retained));
    EXPECT_EQ(retained.Components.x, preview.Components.x);
    EXPECT_EQ(retained.World(0, 3), preview.World(0, 3));
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    EXPECT_FALSE(session.ReadTranslationSnapshot(preview));
    EXPECT_EQ(preview.Key, input.Key); // Failure preserves the caller's copy.
}

TEST_F(GizmoTranslationTest, PresentationRejectsWrongThreadAndExternallyChangedTargetsWithoutRetargeting)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    session.EndMutationPhase();
    bool        rejected = false;
    std::thread reader([&] {
        TranslationSnapshot snapshot = {.Key = 77};
        rejected                     = !session.ReadTranslationSnapshot(snapshot) && snapshot.Key == 77;
    });
    reader.join();
    EXPECT_TRUE(rejected);
    auto* transform       = scene.GetComponent<TransformComponent>(entity);
    transform->Position.x = 5.f;
    scene.SynchronizeHierarchy();
    TranslationSnapshot snapshot = {.Key = 83};
    EXPECT_FALSE(session.ReadTranslationSnapshot(snapshot));
    EXPECT_EQ(snapshot.Key, 83u);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoTranslationTest, PureEntityDragUsesOneSharedBatchAndExactUndoRedo)
{
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.3f, -.4f, .5f};
    transform->Scale    = {-2.f, 3.f, .5f};
    const auto before   = Value();
    const auto input    = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    EXPECT_TRUE(session.TranslationActive());
    EXPECT_EQ(session.TranslationKey(), input.Key);
    EXPECT_EQ(session.TranslationPointerSerial(), input.PointerSerial);
    EXPECT_EQ(session.TransformCount(), 1u);
    ASSERT_TRUE(session.UpdateTranslation(Screen({1.2f, 0.f, 0.f}), true));
    NearPosition({.5f, 0.f, 0.f});
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), false));
    NearPosition({1.f, 0.f, 0.f});
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.TranslationKey(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_TRUE(session.Dirty());
    const auto after = Value();
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_EQ(std::bit_cast<uint32_t>(after.Rotation[axis]), std::bit_cast<uint32_t>(before.Rotation[axis]));
        EXPECT_EQ(std::bit_cast<uint32_t>(after.Scale[axis]), std::bit_cast<uint32_t>(before.Scale[axis]));
        if (axis != 0)
        {
            EXPECT_EQ(std::bit_cast<uint32_t>(after.Position[axis]), std::bit_cast<uint32_t>(before.Position[axis]));
        }
    }
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(), before));
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(SameLocalTransform(Value(), after));
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoTranslationTest, ActorBackedAndPureComponentTargetsUseTheSameTranslationPath)
{
    const auto actor = actors.Create();
    ASSERT_TRUE(actor.Valid());
    const auto actor_entity = actors.Access(actor)->GetEntityID();
    scene.AddComponent<UUIDComponent>(actor_entity, {UUID(2)});
    if (!scene.HasComponent<TransformComponent>(actor_entity))
    {
        scene.AddComponent<TransformComponent>(actor_entity, {});
    }
    const auto pure = entity;
    scene.AddComponent<LightComponent>(pure, {});
    scene.AddComponent<CameraComponent>(pure, {});
    for (const auto target : {pure, actor_entity})
    {
        entity = target;
        ASSERT_TRUE(session.Select(entity));
        ASSERT_TRUE(session.BeginTranslation(Input()));
        ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), false));
        NearPosition({1.f, 0.f, 0.f});
        ASSERT_TRUE(session.Undo());
        NearPosition({});
    }
}

TEST_F(GizmoTranslationTest, PlaneAndCentreDraggingPreserveTheirStartGrabOffsets)
{
    for (auto handle : {Handle::TranslateXY, Handle::TranslateView})
    {
        const Vec3f grab(.4f, .7f, 0.f), delta(.8f, -.3f, 0.f);
        ASSERT_TRUE(session.BeginTranslation(Input(handle, grab)));
        ASSERT_TRUE(session.UpdateTranslation(Screen(grab + delta), false));
        NearPosition(delta);
        ASSERT_TRUE(session.Undo());
        NearPosition({});
    }
}

TEST_F(GizmoTranslationTest, FineAndShortcutModifiersUseCapturedSnapSettingsWithoutChangingPreferences)
{
    scene.GetComponent<TransformComponent>(entity)->Position = {.13f, .27f, .11f};
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto input = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    const auto end = Screen(input.Expected.Pivot + Vec3f(1.26f, 0.f, 0.f)); // .7 grab + .56 motion
    ASSERT_TRUE(session.UpdateTranslation(end, true));
    NearPosition({.7f, .27f, .11f});
    ASSERT_TRUE(session.UpdateTranslation(end, true, false, true));
    NearPosition({.69f, .27f, .11f});
    ASSERT_TRUE(session.UpdateTranslation(end, true, true, false));
    NearPosition({.186f, .27f, .11f});
    ASSERT_TRUE(session.UpdateTranslation(end, true, true, true));
    NearPosition({.186f, .27f, .11f});
    EXPECT_TRUE(session.GizmoSettings().TranslationSnap.Enabled);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    session.Cancel();
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoTranslationTest, ChangingToolbarPreferencesDoesNotChangeAnAlreadyCapturedSnapMapping)
{
    scene.GetComponent<TransformComponent>(entity)->Position = {.13f, .27f, .11f};
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto input = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapIncrement, .Target = SnapTarget::Translation, .Increment = 1.f}));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_TRUE(session.TranslationActive());
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Vec3f(1.26f, 0.f, 0.f)), false));
    NearPosition({.7f, .27f, .11f}); // original enabled 0.1 step, not the new disabled 1 step
    EXPECT_FALSE(session.GizmoSettings().TranslationSnap.Enabled);
    EXPECT_FLOAT_EQ(session.GizmoSettings().TranslationSnap.Increment, 1.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoTranslationTest, ClickAndReturningToPressPositionProduceNoHistoryOrDirtyChange)
{
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Position = {-.0f, .27f, .11f};
    transform->Rotation = {-0.f, 7.f, -8.f};
    transform->Scale    = {-2.f, 3.f, .5f};
    const auto before   = Value();
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto input = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(input.Pointer, false));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Vec3f(1.24f, 0.f, 0.f)), true));
    ASSERT_TRUE(session.UpdateTranslation(input.Pointer, false));
    const auto after = Value();
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_EQ(std::bit_cast<uint32_t>(before.Position[axis]), std::bit_cast<uint32_t>(after.Position[axis]));
        EXPECT_EQ(std::bit_cast<uint32_t>(before.Rotation[axis]), std::bit_cast<uint32_t>(after.Rotation[axis]));
        EXPECT_EQ(std::bit_cast<uint32_t>(before.Scale[axis]), std::bit_cast<uint32_t>(after.Scale[axis]));
    }
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoTranslationTest, CancellationOnFocusCaptureAndEscapeRestoresBeforeStateAndRetiresMapping)
{
    scene.GetComponent<TransformComponent>(entity)->PreviousPosition = {7.f, 8.f, 9.f};
    for (uint32_t reason = 0; reason < 3; ++reason)
    {
        ASSERT_TRUE(session.BeginTranslation(Input()));
        ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), true));
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.UpdateTranslation({}, true, false, false, reason != 0, reason == 1, reason == 2));
        NearPosition({});
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.TranslationKey(), 0u);
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->PreviousPosition.x, 7.f);
    }
}

TEST_F(GizmoTranslationTest, ParentLocalTranslationUsesTheNearestTransformAcrossOrganizationalParents)
{
    const auto ancestor = Entity(2);
    const auto group    = Entity(3, false);
    auto*      parent   = scene.GetComponent<TransformComponent>(ancestor);
    parent->Position    = {1.f, 2.f, 3.f};
    parent->Rotation    = {.3f, -.4f, .5f};
    parent->Scale       = {-2.f, 3.f, .5f};
    scene.AddComponent<ParentComponent>(group, {ancestor});
    scene.AddComponent<ParentComponent>(entity, {group});
    const auto before = Value();
    const auto input  = Input();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Vec3f(1.7f, 0.f, 0.f)), false));
    session.EndMutationPhase();
    const auto* transformed = scene.GetComponent<TransformComponent>(entity);
    EXPECT_NEAR(transformed->WorldTransform(0, 3), input.Expected.Pivot.x + 1.f, .0002f);
    EXPECT_NEAR(transformed->WorldTransform(1, 3), input.Expected.Pivot.y, .0002f);
    EXPECT_NEAR(transformed->WorldTransform(2, 3), input.Expected.Pivot.z, .0002f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(), before));
}

TEST_F(GizmoTranslationTest, LocalSpaceUsesTheCapturedNormalizedWorldAxisWithoutChangingRotation)
{
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.2f, .6f, -.3f};
    transform->Scale    = {-2.f, 3.f, .5f};
    const auto before   = Value();
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.ProcessRequests());
    scene.SynchronizeHierarchy();
    TargetFrame frame = {};
    ASSERT_TRUE(ResolveTargetFrame(session, frame));
    const auto axis = frame.Axes[0].normalize();
    const auto grab = axis * .7f;
    ASSERT_TRUE(session.BeginTranslation(Input(Handle::TranslateX, grab)));
    ASSERT_TRUE(session.UpdateTranslation(Screen(grab + axis * .8f), false));
    NearPosition(axis * .8f);
    const auto after = Value();
    for (uint32_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(std::bit_cast<uint32_t>(after.Rotation[i]), std::bit_cast<uint32_t>(before.Rotation[i]));
        EXPECT_EQ(std::bit_cast<uint32_t>(after.Scale[i]), std::bit_cast<uint32_t>(before.Scale[i]));
    }
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(), before));
}

TEST_F(GizmoTranslationTest, ChangedParentCancelsBeforeAnotherPreviewAndRetiresTheCapturedMapping)
{
    const auto parent = Entity(2);
    scene.AddComponent<ParentComponent>(entity, {parent});
    ASSERT_TRUE(session.BeginTranslation(Input()));
    ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), true));
    session.EndMutationPhase();
    scene.GetComponent<TransformComponent>(parent)->Position = {3.f, 0.f, 0.f};
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.TranslationKey(), 0u);
    NearPosition({});
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(parent)->Position.x, 3.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    // The unauthorized ancestor edit is not restored or mistaken for saved content.
    EXPECT_TRUE(session.Dirty());
}

TEST_F(GizmoTranslationTest, SelectionChangesRejectTheOldPressWithoutTruncatingTheNewSelection)
{
    const auto input = Input();
    const auto other = Entity(2);
    ASSERT_TRUE(session.Select(other, SelectionMode::Add));
    EXPECT_FALSE(session.BeginTranslation(input));
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoTranslationTest, StaleExpectedFramesAndInvalidPressIntentsCannotBegin)
{
    const auto original = Input();
    for (uint32_t field = 0; field < 8; ++field)
    {
        auto input = original;
        switch (field)
        {
            case 0:
                input.Key = 0;
                break;
            case 1:
                input.PointerSerial = 0;
                break;
            case 2:
                input.Constraint = Handle::None;
                break;
            case 3:
                ++input.Expected.SelectionRevision;
                break;
            case 4:
                ++input.Expected.Target.SceneEpoch;
                break;
            case 5:
                ++input.Expected.WorldRevision;
                break;
            case 6:
                ++input.Expected.SettingsRevision;
                break;
            case 7:
                input.Bounds.Size.y = 0.f;
                break;
        }
        EXPECT_FALSE(session.BeginTranslation(input));
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
    scene.GetComponent<TransformComponent>(entity)->Position.x = .1f;
    EXPECT_FALSE(session.BeginTranslation(original));
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoTranslationTest, CallerInputMutationCannotChangeTheCapturedCameraOrBasis)
{
    auto       input   = Input();
    const auto pointer = Screen({1.7f, 0.f, 0.f});
    ASSERT_TRUE(session.BeginTranslation(input));
    input.Projection = {};
    input.Bounds     = {};
    input.Expected   = {};
    input.Pointer    = {999.f, 999.f};
    ASSERT_TRUE(session.UpdateTranslation(pointer, false));
    NearPosition({1.f, 0.f, 0.f});
}

TEST_F(GizmoTranslationTest, InvalidFinalSampleCommitsOnlyTheLastValidPreviewAndReleasesMapping)
{
    ASSERT_TRUE(session.BeginTranslation(Input()));
    ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), true));
    const auto  last = Value();
    const Vec2f invalid(std::numeric_limits<float>::quiet_NaN(), 0.f);
    EXPECT_FALSE(session.UpdateTranslation(invalid, true));
    EXPECT_TRUE(SameLocalTransform(Value(), last));
    ASSERT_TRUE(session.UpdateTranslation(invalid, false));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.TranslationPointerSerial(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_TRUE(SameLocalTransform(Value(), last));
}

TEST_F(GizmoTranslationTest, SelectionModeAndSceneTransitionsCancelTheOriginalTransaction)
{
    const auto other = Entity(2);
    for (uint32_t transition = 0; transition < 3; ++transition)
    {
        ASSERT_TRUE(session.Select(entity));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
        ASSERT_TRUE(session.ProcessRequests());
        ASSERT_TRUE(session.BeginTranslation(Input()));
        ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), true));
        if (transition == 0)
        {
            ASSERT_TRUE(session.Select(other));
        }
        else if (transition == 1)
        {
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Rotate}));
            ASSERT_TRUE(session.ProcessRequests());
        }
        else
        {
            ASSERT_TRUE(session.BindScene(nullptr));
        }
        NearPosition({});
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.TranslationKey(), 0u);
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
}

TEST_F(GizmoTranslationTest, DeletedAndRecycledEntityCannotReceiveAnOldTranslation)
{
    ASSERT_TRUE(session.BeginTranslation(Input()));
    ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), true));
    const auto original = entity;
    scene.DestroyEntity(entity);
    entity = Entity(1);
    ASSERT_EQ(entity.Index, original.Index);
    ASSERT_NE(entity.Generation, original.Generation);
    EXPECT_FALSE(session.UpdateTranslation(Screen({2.7f, 0.f, 0.f}), false));
    NearPosition({});
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.TranslationKey(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoTranslationTest, TranslationCannotReplaceAnotherInteractionOrRunOutsideItsOwnerPhase)
{
    const auto input  = Input();
    const auto target = session.TargetFor(entity);
    ASSERT_TRUE(session.BeginTransform({&target, 1}));
    EXPECT_FALSE(session.BeginTranslation(input));
    EXPECT_TRUE(session.Active());
    session.Cancel();
    session.EndMutationPhase();
    EXPECT_FALSE(session.BeginTranslation(input));
    bool        accepted = true;
    std::thread worker([&] { accepted = session.BeginTranslation(input); });
    worker.join();
    EXPECT_FALSE(accepted);
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.BeginTranslation(input));
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoTranslationTest, RepeatedDragsReuseScratchAndBoundedHistoryWithoutArenaGrowth)
{
    const auto input = Input();
    const auto bytes = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.BeginTranslation(input));
        ASSERT_TRUE(session.UpdateTranslation(Screen({1.7f, 0.f, 0.f}), true));
        ASSERT_TRUE(session.UpdateTranslation(Screen({2.7f, 0.f, 0.f}), false));
        ASSERT_TRUE(session.Undo());
        EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
        EXPECT_LE(session.History().UndoCount(), 8u);
        EXPECT_FALSE(session.Dirty());
    }
    NearPosition({});
}
