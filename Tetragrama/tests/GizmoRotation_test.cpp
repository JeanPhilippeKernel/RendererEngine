#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
namespace G = ZEngine::Core::Maths::Geometry;

class GizmoRotationTest : public testing::Test
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
        ZEngine::Core::Containers::FixedArray<uint8_t, 16> bytes = {};
        bytes[15]                                                = value;
        return uuids::uuid(bytes.begin(), bytes.end());
    }
    EntityID Entity(uint8_t identity)
    {
        const auto result = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(result, {UUID(identity)});
        scene.AddComponent<TransformComponent>(result, {});
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
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Rotate}));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = false}));
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
    Vec2f Screen(const Vec3f& point)
    {
        G::ProjectedPoint projected = {};
        EXPECT_TRUE(G::Project(point, projection, bounds, &projected));
        return projected.Screen;
    }
    RotationBegin Input(Handle handle = Handle::RotateZ, const Vec3f& radial = {1.f, 0.f, 0.f})
    {
        scene.SynchronizeHierarchy();
        RotationBegin input = {.Projection = projection, .Bounds = bounds, .Key = 123, .PointerSerial = 456, .Constraint = handle};
        EXPECT_TRUE(ResolveTargetFrame(session, input.Expected));
        input.RingPoint = input.Expected.Pivot + radial;
        input.Pointer   = Screen(input.RingPoint);
        return input;
    }
    LocalTransformValue Value()
    {
        return CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    }
    Vec2f AnglePointer(double angle, const Vec3f& pivot = {})
    {
        return Screen(pivot + Vec3f(float(std::cos(angle)), float(std::sin(angle)), 0.f));
    }
    void Sync()
    {
        session.EndMutationPhase();
        EXPECT_TRUE(session.BeginMutationPhase());
    }
};

TEST_F(GizmoRotationTest, PureEntityUsesOneSharedTransformBatchWithExactUndoRedo)
{
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.3f, -.4f, .5f};
    transform->Scale    = {-2.f, 3.f, .5f};
    const auto before   = Value();
    ASSERT_TRUE(session.BeginRotation(Input()));
    EXPECT_TRUE(session.RotationActive());
    EXPECT_FALSE(session.TranslationActive());
    EXPECT_EQ(session.TransformCount(), 1u);
    ASSERT_TRUE(session.UpdateRotation(AnglePointer(.4), true));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    Sync();
    ASSERT_TRUE(session.UpdateRotation(AnglePointer(.7), false));
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.RotationActive());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_TRUE(session.Dirty());
    const auto after = Value();
    EXPECT_FALSE(SameLocalTransform(after, before));
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(), before));
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(SameLocalTransform(Value(), after));
}

TEST_F(GizmoRotationTest, ActorBackedAndPureLightCameraAndEmptyTargetsShareThePath)
{
    const auto actor = actors.Create();
    ASSERT_TRUE(actor.Valid());
    const auto actor_entity = actors.Access(actor)->GetEntityID();
    scene.AddComponent<UUIDComponent>(actor_entity, {UUID(2)});
    if (!scene.HasComponent<TransformComponent>(actor_entity))
    {
        scene.AddComponent<TransformComponent>(actor_entity, {});
    }
    const auto light = Entity(3), camera = Entity(4);
    scene.AddComponent<LightComponent>(light, {});
    scene.AddComponent<CameraComponent>(camera, {});
    for (const auto target : {entity, actor_entity, light, camera})
    {
        entity = target;
        ASSERT_TRUE(session.Select(entity));
        const auto before = Value();
        ASSERT_TRUE(session.BeginRotation(Input()));
        ASSERT_TRUE(session.UpdateRotation(AnglePointer(.5), false));
        EXPECT_FALSE(SameLocalTransform(Value(), before));
        ASSERT_TRUE(session.Undo());
        EXPECT_TRUE(SameLocalTransform(Value(), before));
    }
}

TEST_F(GizmoRotationTest, AxisAndViewRingsUseTheCapturedConstraintRatherThanAnActorKind)
{
    for (const auto handle : {Handle::RotateX, Handle::RotateY, Handle::RotateZ, Handle::RotateView})
    {
        const bool tangent = handle == Handle::RotateX || handle == Handle::RotateY;
        const auto input   = Input(handle, tangent ? Vec3f(0.f, 0.f, 1.f) : Vec3f(1.f, 0.f, 0.f));
        ASSERT_TRUE(session.BeginRotation(input));
        Vec2f end = {};
        if (tangent)
        {
            const auto           axis    = handle == Handle::RotateX ? Vec3f(1.f, 0.f, 0.f) : Vec3f(0.f, 1.f, 0.f);
            G::RotationReference mapping = {};
            ASSERT_TRUE(G::CaptureRotation({.Projection = projection, .Bounds = bounds, .Pointer = input.Pointer, .Axis = axis, .RingPoint = input.RingPoint, .RadiusPx = Constants::RingRadiusPx, .TangentFallbackCosine = Constants::RotateTangentFallbackCosine, .PlaneMinCosine = Constants::PlaneParallelCosine}, &mapping));
            end = input.Pointer + mapping.ScreenTangent * 50.f;
        }
        else
        {
            end = AnglePointer(.5);
        }
        ASSERT_TRUE(session.UpdateRotation(end, true));
        session.EndMutationPhase();
        RotationSnapshot snapshot = {};
        ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
        EXPECT_NEAR(snapshot.Angle, .5, .0001);
        EXPECT_EQ(snapshot.Constraint, handle);
        ASSERT_TRUE(session.BeginMutationPhase());
        session.Cancel();
        Sync();
    }
}

TEST_F(GizmoRotationTest, ClickReturnAndCancellationDoNotCreateHistoryOrDirtyTheScene)
{
    const auto before = Value();
    for (uint32_t reason = 0; reason < 5; ++reason)
    {
        const auto input = Input();
        ASSERT_TRUE(session.BeginRotation(input));
        if (reason == 0)
        {
            ASSERT_TRUE(session.UpdateRotation(input.Pointer, false));
        }
        else
        {
            ASSERT_TRUE(session.UpdateRotation(AnglePointer(.4), true));
            if (reason == 1)
            {
                ASSERT_TRUE(session.UpdateRotation(input.Pointer, false));
            }
            else
            {
                ASSERT_TRUE(session.UpdateRotation(AnglePointer(.4), true, false, false, reason != 2, reason == 3, reason == 4));
            }
        }
        EXPECT_TRUE(SameLocalTransform(Value(), before));
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
        Sync();
    }
}

TEST_F(GizmoRotationTest, ContinuousWholeTurnReturnsTheExactOriginalPoseAndNoHistory)
{
    const auto before = Value();
    for (bool snap : {false, true})
    {
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = snap}));
        ASSERT_TRUE(session.ProcessRequests());
        const auto input = Input();
        ASSERT_TRUE(session.BeginRotation(input));
        for (uint32_t step = 1; step < 16; ++step)
        {
            ASSERT_TRUE(session.UpdateRotation(AnglePointer(step * std::numbers::pi / 8.0), true));
            Sync();
        }
        ASSERT_TRUE(session.UpdateRotation(input.Pointer, false));
        EXPECT_TRUE(SameLocalTransform(Value(), before));
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
        Sync();
    }
}

TEST_F(GizmoRotationTest, SnapInversionFineMotionAndPresentationUseTheAcceptedAngle)
{
    ASSERT_TRUE(session.BeginRotation(Input()));
    const auto pointer = AnglePointer(.43);
    ASSERT_TRUE(session.UpdateRotation(pointer, true, false, true)); // Invert disabled base snap on.
    session.EndMutationPhase();
    RotationSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
    EXPECT_NEAR(snapshot.Angle, 20.0 * std::numbers::pi / 180.0, .00001);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateRotation(pointer, true, true, true)); // Fine never snaps.
    session.EndMutationPhase();
    ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
    EXPECT_NEAR(snapshot.Angle, .043, .00001);
    EXPECT_EQ(snapshot.Key, 123u);
    EXPECT_EQ(snapshot.PointerSerial, 456u);
    EXPECT_EQ(snapshot.Target.UUID, UUID(1));
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
}

TEST_F(GizmoRotationTest, RejectedParentShearPreservesPoseAndPillButStillTracksCrossingsOfPi)
{
    const auto parent                                     = Entity(2);
    scene.GetComponent<TransformComponent>(parent)->Scale = {2.f, 1.f, 3.f};
    scene.AddComponent<ParentComponent>(entity, {parent});
    const auto input = Input();
    ASSERT_TRUE(session.BeginRotation(input));
    LocalTransformValue last       = Value();
    double              last_angle = 0.0;
    for (uint32_t step = 1; step <= 7; ++step)
    {
        const double angle    = step * std::numbers::pi / 4.0;
        const bool   accepted = step % 2 == 0;
        EXPECT_EQ(session.UpdateRotation(AnglePointer(angle), true), accepted);
        if (!accepted)
        {
            EXPECT_TRUE(SameLocalTransform(Value(), last));
        }
        else
        {
            last       = Value();
            last_angle = angle;
        }
        session.EndMutationPhase();
        RotationSnapshot snapshot = {};
        ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
        EXPECT_NEAR(snapshot.Angle, last_angle, .00001);
        ASSERT_TRUE(session.BeginMutationPhase());
    }
    ASSERT_TRUE(session.UpdateRotation({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
    EXPECT_FALSE(session.RotationActive());
    EXPECT_TRUE(SameLocalTransform(Value(), last));
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoRotationTest, ModeSelectionAndSceneChangesCancelTheOriginalRotation)
{
    const auto other = Entity(2);
    for (uint32_t transition = 0; transition < 3; ++transition)
    {
        ASSERT_TRUE(session.Select(entity));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Rotate}));
        ASSERT_TRUE(session.ProcessRequests());
        const auto before = Value();
        ASSERT_TRUE(session.BeginRotation(Input()));
        ASSERT_TRUE(session.UpdateRotation(AnglePointer(.4), true));
        if (transition == 0)
        {
            ASSERT_TRUE(session.Select(other));
        }
        else if (transition == 1)
        {
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
            ASSERT_TRUE(session.ProcessRequests());
        }
        else
        {
            ASSERT_TRUE(session.BindScene(&scene));
        }
        EXPECT_FALSE(session.Active());
        EXPECT_TRUE(SameLocalTransform(Value(), before));
        EXPECT_EQ(session.History().UndoCount(), 0u);
        Sync();
    }
}

TEST_F(GizmoRotationTest, StaleMalformedOrForeignThreadIntentsCannotBeginOrAdvanceRotation)
{
    const auto original = Input();
    for (uint32_t failure = 0; failure < 7; ++failure)
    {
        auto input = original;
        switch (failure)
        {
            case 0:
                input.Key = 0;
                break;
            case 1:
                input.PointerSerial = 0;
                break;
            case 2:
                input.Constraint = Handle::TranslateX;
                break;
            case 3:
                ++input.Expected.Target.SceneEpoch;
                break;
            case 4:
                ++input.Expected.WorldRevision;
                break;
            case 5:
                ++input.Expected.SettingsRevision;
                break;
            case 6:
                input.Bounds.Size.x = 0.f;
                break;
        }
        EXPECT_FALSE(session.BeginRotation(input));
        EXPECT_FALSE(session.Active());
    }
    bool        rejected = false;
    std::thread wrong_thread([&] { rejected = !session.BeginRotation(original); });
    wrong_thread.join();
    EXPECT_TRUE(rejected);
    ASSERT_TRUE(session.BeginRotation(original));
    const auto  pointer = AnglePointer(.5);
    std::thread update_thread([&] { rejected = !session.UpdateRotation(pointer, false); });
    update_thread.join();
    EXPECT_TRUE(rejected);
    EXPECT_TRUE(session.Active());
    EXPECT_FALSE(session.Dirty());
    session.EndMutationPhase();
    EXPECT_FALSE(session.UpdateRotation(pointer, false));
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
}

TEST_F(GizmoRotationTest, PresentationRequiresSynchronizedUnchangedOwnersAndPreservesOutputOnFailure)
{
    ASSERT_TRUE(session.BeginRotation(Input()));
    ASSERT_TRUE(session.UpdateRotation(AnglePointer(.5), true));
    RotationSnapshot output = {.Key = 77};
    EXPECT_FALSE(session.ReadRotationSnapshot(output));
    EXPECT_EQ(output.Key, 77u);
    session.EndMutationPhase();
    ASSERT_TRUE(session.ReadRotationSnapshot(output));
    EXPECT_NEAR(output.Angle, .5, .00001);
    bool        rejected = false;
    std::thread reader([&] {
        RotationSnapshot foreign = {.Key = 83};
        rejected                 = !session.ReadRotationSnapshot(foreign) && foreign.Key == 83;
    });
    reader.join();
    EXPECT_TRUE(rejected);
    scene.GetComponent<TransformComponent>(entity)->Position.x = 5.f;
    scene.SynchronizeHierarchy();
    output.Key = 91;
    EXPECT_FALSE(session.ReadRotationSnapshot(output));
    EXPECT_EQ(output.Key, 91u);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoRotationTest, RepeatedRotationsReuseTransactionAndArenaStorage)
{
    ASSERT_TRUE(session.BeginRotation(Input()));
    ASSERT_TRUE(session.UpdateRotation(AnglePointer(.3), true));
    session.Cancel();
    Sync();
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t iteration = 0; iteration < 1000; ++iteration)
    {
        ASSERT_TRUE(session.BeginRotation(Input()));
        ASSERT_TRUE(session.UpdateRotation(AnglePointer(.3), true));
        session.Cancel();
        Sync();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}
