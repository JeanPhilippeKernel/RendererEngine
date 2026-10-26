#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoCameraBuilder.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUITokens.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;
namespace G  = ZEngine::Core::Maths::Geometry;
namespace C  = Tetragrama::Gizmo::Constants;
namespace UI = ZEngine::UI;

class GizmoCameraTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    Scene                                scene      = {};
    ActorManager                         actors     = {};
    EditorSession                        session    = {};
    EntityID                             entity     = {};
    CameraFrameData                      camera     = {};
    G::ProjectionFrame                   projection = {};
    G::Viewport                          bounds     = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };
    uint64_t           serial = 0;
    static uuids::uuid UUID(uint8_t identity)
    {
        ZEngine::Core::Containers::FixedArray<uint8_t, 16> bytes = {};
        bytes[15]                                                = identity;
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
        scene.AddComponent<CameraComponent>(entity, {.Near = 1.f, .Far = 4.f, .AspectRatio = 1.5f});
        ASSERT_TRUE(session.Select(entity));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = false}));
        ASSERT_TRUE(session.ProcessRequests());
        Projection();
    }
    void TearDown() override
    {
        session.Shutdown();
        actors.Shutdown();
        scene.Shutdown();
        memory.Shutdown();
    }
    void Projection(bool ortho = false, bool reverse = false, bool oblique = true, float far_distance = 1000.f)
    {
        CameraSetting settings = {};
        settings.FarPlane      = far_distance;
        FlyCamera source(4.f / 3.f, settings);
        source.SetPosition(oblique ? Vec3f(7.f, 5.f, 10.f) : Vec3f(0.f, 0.f, 10.f));
        source.SetOrientation(oblique ? 25.f : 0.f, oblique ? -35.f : 0.f);
        source.SetProjectionType(ortho ? CameraType::ORTHOGRAPHIC : CameraType::PERSPECTIVE);
        camera              = source.CaptureFrameData();
        camera.UsesReverseZ = reverse;
        if (reverse)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                camera.Projection(2, column) = camera.Projection(3, column) - camera.Projection(2, column);
            }
        }
        ASSERT_TRUE(G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, ortho ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &projection));
    }
    CameraComponent& Value()
    {
        return *scene.GetComponent<CameraComponent>(entity);
    }
    CameraFrame Frame()
    {
        scene.SynchronizeHierarchy();
        CameraFrame frame = {};
        EXPECT_TRUE(ResolveCameraFrame(session, frame));
        return frame;
    }
    Vec2f Screen(const Vec3f& point)
    {
        G::ProjectedPoint projected = {};
        EXPECT_TRUE(G::Project(point, projection, bounds, &projected));
        return projected.Screen;
    }
    CameraBegin Input(Handle constraint = Handle::CameraFarLowerLeft)
    {
        CameraBegin  input  = {.Expected = Frame(), .Projection = projection, .Bounds = bounds, .Key = 99, .PointerSerial = ++serial, .Constraint = constraint};
        CameraHandle handle = {};
        EXPECT_TRUE(ResolveCameraHandle(input.Expected, constraint, handle));
        input.Pointer = Screen(handle.Position);
        return input;
    }
    Vec2f Motion(const CameraBegin& input, double delta)
    {
        CameraHandle handle = {};
        EXPECT_TRUE(ResolveCameraHandle(input.Expected, input.Constraint, handle));
        return Screen(handle.Position + handle.Direction * float(delta));
    }
    Vec2f MotionValue(const CameraBegin& input, float value)
    {
        CameraHandle handle = {};
        EXPECT_TRUE(ResolveCameraHandle(input.Expected, input.Constraint, handle));
        return Motion(input, handle.Field == AuthoredFloatField::CameraFovY ? handle.Factor * std::tan(double(value) * std::numbers::pi / 360.0) - handle.Parameter : (double(value) - (handle.Field == AuthoredFloatField::CameraNear ? input.Expected.Camera.Near : input.Expected.Camera.Far)) * handle.Factor);
    }
    float FieldValue(AuthoredFloatField field) const
    {
        const auto& value = *scene.GetComponent<CameraComponent>(entity);
        return field == AuthoredFloatField::CameraFovY ? value.FovY : field == AuthoredFloatField::CameraNear ? value.Near : value.Far;
    }
    FrameInput Geometry(uint32_t height = 600, Operation mode = Operation::Select)
    {
        return {.Camera = camera, .Viewport = bounds, .TargetWidth = height * 4 / 3, .TargetHeight = height, .Mode = mode, .Editable = true, .Frustum = Frame()};
    }
    void Sync()
    {
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
    }
};

TEST_F(GizmoCameraTest, AllSixCornerHandlesUseOneTypedFieldOperationAcrossProjectionConventions)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t index = uint32_t(Handle::CameraFovUpperLeft); index <= uint32_t(Handle::CameraNearRight); ++index)
            {
                Projection(ortho, reverse);
                const auto   input  = Input(Handle(index));
                const auto   before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
                CameraHandle handle = {};
                ASSERT_TRUE(ResolveCameraHandle(input.Expected, Handle(index), handle));
                const float target = handle.Field == AuthoredFloatField::CameraFovY ? 80.f : handle.Field == AuthoredFloatField::CameraNear ? .5f : 5.f;
                ASSERT_TRUE(session.BeginCamera(input));
                ASSERT_TRUE(session.CameraActive());
                ASSERT_TRUE(session.UpdateCamera(MotionValue(input, target), true));
                EXPECT_NEAR(FieldValue(handle.Field), target, .001f);
                EXPECT_FLOAT_EQ(handle.Field == AuthoredFloatField::CameraFovY ? Value().Far : Value().FovY, handle.Field == AuthoredFloatField::CameraFovY ? 4.f : 60.f);
                EXPECT_FLOAT_EQ(handle.Field == AuthoredFloatField::CameraNear ? Value().Far : Value().Near, handle.Field == AuthoredFloatField::CameraNear ? 4.f : 1.f);
                EXPECT_TRUE(SameLocalTransform(before, CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity))));
                CameraSnapshot snapshot = {};
                ASSERT_TRUE(session.ReadCameraSnapshot(snapshot));
                EXPECT_EQ(snapshot.Constraint, Handle(index));
                EXPECT_EQ(snapshot.Key, 99u);
                EXPECT_EQ(snapshot.PointerSerial, input.PointerSerial);
                EXPECT_NEAR(snapshot.Value, target, .001f);
                ASSERT_TRUE(session.UpdateCamera(MotionValue(input, target), false));
                EXPECT_FALSE(session.Active());
                EXPECT_EQ(session.History().UndoCount(), 1u);
                EXPECT_TRUE(session.Dirty());
                ASSERT_TRUE(session.Undo());
                EXPECT_FLOAT_EQ(Value().Far, 4.f);
                EXPECT_FLOAT_EQ(Value().FovY, 60.f);
                EXPECT_FLOAT_EQ(Value().Near, 1.f);
                EXPECT_FALSE(session.Dirty());
                ASSERT_TRUE(session.Redo());
                EXPECT_NEAR(FieldValue(handle.Field), target, .001f);
                ASSERT_TRUE(session.Undo());
                Sync();
            }
        }
    }
}

TEST_F(GizmoCameraTest, NoOpReturnToPressAndEveryCancelPathRestoreExactAuthoredValues)
{
    for (const auto constraint : {Handle::CameraFarLowerRight, Handle::CameraFovUpperLeft})
    {
        for (uint32_t kind = 0; kind < 5; ++kind)
        {
            const auto input = Input(constraint);
            ASSERT_TRUE(session.BeginCamera(input));
            ASSERT_TRUE(session.UpdateCamera(MotionValue(input, constraint == Handle::CameraFarLowerRight ? 5.f : 80.f), true));
            if (kind == 0)
            {
                ASSERT_TRUE(session.UpdateCamera(input.Pointer, false));
            }
            else if (kind == 1)
            {
                session.Cancel();
            }
            else
            {
                ASSERT_TRUE(session.UpdateCamera({}, true, false, false, kind != 2, kind == 3, kind == 4));
            }
            EXPECT_FALSE(session.Active());
            EXPECT_FLOAT_EQ(Value().Far, 4.f);
            EXPECT_FLOAT_EQ(Value().FovY, 60.f);
            EXPECT_FALSE(session.Dirty());
            EXPECT_EQ(session.History().UndoCount(), 0u);
        }
    }
    const auto input    = Input();
    const auto revision = session.DocumentRevision();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(input.Pointer, false));
    EXPECT_EQ(session.DocumentRevision(), revision);
}

TEST_F(GizmoCameraTest, InvalidCandidatesAndFinalSampleKeepOnlyTheLastAcceptedPreview)
{
    auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    const auto accepted = Value().Far;
    EXPECT_FALSE(session.UpdateCamera(MotionValue(input, .5f), true));
    EXPECT_FLOAT_EQ(Value().Far, accepted);
    ASSERT_TRUE(session.UpdateCamera({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
    EXPECT_FLOAT_EQ(Value().Far, accepted);
    ASSERT_TRUE(session.Undo());
    input = Input(Handle::CameraFovUpperRight);
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 80.f), true));
    const auto angle = Value().FovY;
    EXPECT_FALSE(session.UpdateCamera(MotionValue(input, -20.f), true));
    ASSERT_TRUE(session.UpdateCamera({std::numeric_limits<float>::infinity(), 0.f}, false));
    EXPECT_FLOAT_EQ(Value().FovY, angle);
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
}

TEST_F(GizmoCameraTest, CapturedFarAndFovSnapsCtrlAndFineUseScreenMotionRatherThanAccumulation)
{
    for (const auto constraint : {Handle::CameraFarLowerLeft, Handle::CameraFovUpperLeft})
    {
        const bool angle = constraint == Handle::CameraFovUpperLeft;
        const auto snap  = angle ? SnapTarget::Rotation : SnapTarget::Translation;
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = snap, .Enabled = true}));
        ASSERT_TRUE(session.ProcessRequests());
        const auto  input  = Input(constraint);
        const float target = angle ? 76.f : 4.26f;
        const auto  point  = MotionValue(input, target);
        ASSERT_TRUE(session.BeginCamera(input));
        ASSERT_TRUE(session.UpdateCamera(point, true));
        EXPECT_NEAR(angle ? Value().FovY : Value().Far, angle ? 80.f : 4.3f, .001f);
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapIncrement, .Target = snap, .Increment = angle ? 45.f : .5f}));
        ASSERT_TRUE(session.ProcessRequests());
        ASSERT_TRUE(session.UpdateCamera(point, true));
        EXPECT_NEAR(angle ? Value().FovY : Value().Far, angle ? 80.f : 4.3f, .001f);
        ASSERT_TRUE(session.UpdateCamera(point, true, false, true));
        EXPECT_NEAR(angle ? Value().FovY : Value().Far, target, .001f);
        ASSERT_TRUE(session.UpdateCamera(point, true, true));
        const float fine         = angle ? Value().FovY : Value().Far;
        // Match the mapping's single narrowing, not a chain of rounded float
        // vector operations that may name an adjacent physical screen point.
        Vec2f       fine_pointer = {};
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            fine_pointer[axis] = float(double(input.Pointer[axis]) + (double(point[axis]) - input.Pointer[axis]) * C::FineMotionFactor);
        }
        ASSERT_TRUE(session.UpdateCamera(fine_pointer, true, false, true));
        EXPECT_FLOAT_EQ(angle ? Value().FovY : Value().Far, fine);
        session.Cancel();
    }
}

TEST_F(GizmoCameraTest, ActorAndPureECSCameraEditsRemainActiveTargetOnlyInSelectMode)
{
    const auto actor_handle = actors.Create();
    auto*      actor        = actors.Access(actor_handle);
    actor->AddComponent<UUIDComponent>({UUID(2)});
    actor->AddComponent<TransformComponent>({});
    actor->AddComponent<CameraComponent>({.Near = 1.f, .Far = 4.f, .AspectRatio = 1.5f});
    const auto other = actor->GetEntityID();
    ASSERT_TRUE(session.Select(other, SelectionMode::Add));
    auto input = Input();
    EXPECT_EQ(input.Expected.Target.CachedEntity, other);
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), false));
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(other)->Far, 5.f, .001f);
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
    ASSERT_TRUE(session.Select(entity));
    input = Input(Handle::CameraFovUpperLeft);
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 80.f), false));
    EXPECT_NEAR(Value().FovY, 80.f, .001f);
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(other)->FovY, 60.f);
}

TEST_F(GizmoCameraTest, NewerExternalFieldValueAndInvalidatedRestoreConstraintAreNeverOverwritten)
{
    auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    Value().Far = 7.f;
    EXPECT_FALSE(session.UpdateCamera(MotionValue(input, 6.f), true));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value().Far, 7.f);
    input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 8.f), true));
    Value().Near = 7.5f;
    session.Cancel();
    EXPECT_NEAR(Value().Far, 8.f, .001f);
    EXPECT_FLOAT_EQ(Value().Near, 7.5f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(GizmoCameraTest, NearAspectOtherGeometryPoseAndParentChangesInvalidateCapturedMapping)
{
    const auto parent = Entity(2);
    ASSERT_TRUE(scene.SetParent(entity, parent));
    for (uint32_t kind = 0; kind < 5; ++kind)
    {
        const auto input = Input();
        ASSERT_TRUE(session.BeginCamera(input));
        ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
        if (kind == 0)
        {
            Value().Near = .5f;
        }
        if (kind == 1)
        {
            Value().AspectRatio = 2.f;
        }
        if (kind == 2)
        {
            Value().FovY = 70.f;
        }
        if (kind == 3)
        {
            scene.GetComponent<TransformComponent>(entity)->Position.x += .5f;
        }
        if (kind == 4)
        {
            scene.GetComponent<TransformComponent>(parent)->Rotation.y = .3f;
        }
        Sync();
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(Value().Far, 4.f);
        if (kind == 0)
        {
            EXPECT_FLOAT_EQ(Value().Near, .5f);
        }
        if (kind == 1)
        {
            EXPECT_FLOAT_EQ(Value().AspectRatio, 2.f);
        }
        if (kind == 2)
        {
            EXPECT_FLOAT_EQ(Value().FovY, 70.f);
        }
        if (kind == 3)
        {
            EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, .5f);
        }
        if (kind == 4)
        {
            EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(parent)->Rotation.y, .3f);
        }
    }
}

TEST_F(GizmoCameraTest, UnrelatedIsMainStateSurvivesCancelCommitAndHistory)
{
    auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    Value().IsMain = true;
    session.Cancel();
    EXPECT_TRUE(Value().IsMain);
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
    input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), false));
    ASSERT_TRUE(session.Undo());
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(Value().IsMain);
}

TEST_F(GizmoCameraTest, StaleFramesWrongHandlesReadOnlyAndWrongThreadCannotBegin)
{
    auto input          = Input();
    Value().AspectRatio = 2.f;
    EXPECT_FALSE(session.BeginCamera(input));
    input            = Input();
    input.Constraint = Handle::SpotRange;
    EXPECT_FALSE(session.BeginCamera(input));
    input     = Input();
    input.Key = 0;
    EXPECT_FALSE(session.BeginCamera(input));
    input               = Input();
    input.PointerSerial = 0;
    EXPECT_FALSE(session.BeginCamera(input));
    input             = Input();
    bool        began = true;
    std::thread worker([&] { began = session.BeginCamera(input); });
    worker.join();
    EXPECT_FALSE(began);
    session.EndMutationPhase();
    EXPECT_FALSE(session.BeginCamera(input));
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.BeginCamera(input));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
}

TEST_F(GizmoCameraTest, RemovedAndReplacedTargetsNeverReceiveCapturedRollback)
{
    auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    scene.RemoveComponent<CameraComponent>(entity);
    Sync();
    EXPECT_FALSE(session.Active());
    scene.AddComponent<CameraComponent>(entity, {.Near = 1.f, .Far = 4.f, .AspectRatio = 1.5f});
    input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    scene.DestroyEntity(entity);
    entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {.Far = 9.f});
    Sync();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value().Far, 9.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoCameraTest, SameEntityComponentReplacementCannotReceiveTheRemovedCamerasRollback)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    const auto replacement = Value();
    ASSERT_NE(replacement.Far, 4.f);
    scene.RemoveComponent<CameraComponent>(entity);
    scene.AddComponent<CameraComponent>(entity, replacement);
    Sync();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value().Far, replacement.Far);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(GizmoCameraTest, IdentityFrustumMatchesEngineMinusZProjectionAndAuthoredNearFarFovAspect)
{
    const auto frame = Frame();
    EXPECT_EQ(frame.Forward, Vec3f(0.f, 0.f, -1.f));
    FlyCamera source(Value().AspectRatio, {.FOV = Value().FovY, .NearPlane = Value().Near, .FarPlane = Value().Far});
    source.SetPosition({0.f, 0.f, 0.f});
    source.SetOrientation(0.f, 0.f);
    const auto         engine_camera     = source.CaptureFrameData();
    G::ProjectionFrame engine_projection = {};
    ASSERT_TRUE(G::MakeProjectionFrame(engine_camera.View, engine_camera.Projection, engine_camera.Position, G::ProjectionKind::Perspective, false, &engine_projection));
    for (uint32_t corner = 0; corner < 4; ++corner)
    {
        for (bool far_plane : {false, true})
        {
            Vec3f point = {};
            ASSERT_TRUE(CameraCorner(frame, corner, far_plane ? Value().Far : Value().Near, point));
            G::ProjectedPoint projected = {};
            ASSERT_TRUE(G::Project(point, engine_projection, bounds, &projected));
            EXPECT_NEAR(projected.Screen.x, bounds.Origin.x + (corner & 1 ? bounds.Size.x : 0.f), .001f);
            EXPECT_NEAR(projected.Screen.y, bounds.Origin.y + (corner & 2 ? bounds.Size.y : 0.f), .001f);
            EXPECT_NEAR(projected.Depth, far_plane ? 1.f : 0.f, .00001f);
        }
    }
}

TEST_F(GizmoCameraTest, ParentScaleShearAndCustomPivotDoNotDistortWorldSizedFrustum)
{
    const auto parent                                        = Entity(2);
    auto*      pose                                          = scene.GetComponent<TransformComponent>(parent);
    pose->Position                                           = {1.f, 2.f, 3.f};
    pose->Rotation                                           = {.2f, -.4f, .3f};
    pose->Scale                                              = {-2.f, 3.f, .5f};
    scene.GetComponent<TransformComponent>(entity)->Rotation = {.3f, .2f, -.1f};
    ASSERT_TRUE(scene.SetParent(entity, parent));
    const auto frame = Frame();
    ASSERT_TRUE(ValidCameraGeometry(frame));
    Vec3f point = {};
    ASSERT_TRUE(CameraCorner(frame, 0, Value().Far, point));
    const auto offset = point - frame.Origin;
    const auto along  = [&](const Vec3f& axis) { return double(offset.x) * axis.x + double(offset.y) * axis.y + double(offset.z) * axis.z; };
    EXPECT_NEAR(along(frame.Forward), 4.f, .00001f);
    EXPECT_NEAR(along(frame.Up), 4.0 * std::tan(std::numbers::pi / 6.0), .00001f);
    EXPECT_NEAR(along(frame.Right), -6.0 * std::tan(std::numbers::pi / 6.0), .00001f);
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
    ASSERT_TRUE(session.ProcessRequests());
    PivotBegin pivot = {.Projection = projection, .Bounds = bounds, .Key = 77, .PointerSerial = 22};
    ASSERT_TRUE(ResolveTargetFrame(session, pivot.Expected));
    pivot.Pointer = Screen(pivot.Expected.Pivot);
    ASSERT_TRUE(session.BeginPivot(pivot));
    ASSERT_TRUE(session.UpdatePivot(Screen(frame.Origin + Vec3f(.5f, .5f, 0.f)), false));
    Vec3f custom = {};
    ASSERT_TRUE(session.ReadCustomPivot(custom));
    EXPECT_TRUE(SameCameraFrame(frame, Frame()));
    const auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), false));
    Vec3f after = {};
    ASSERT_TRUE(session.ReadCustomPivot(after));
    EXPECT_EQ(after, custom);
}

TEST_F(GizmoCameraTest, FrustumArcMarkerAndHandlesUseNativeThemeAndLogicalDpiMetrics)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t height : {600u, 1200u})
            {
                Projection(ortho, reverse);
                const auto     input  = Geometry(height);
                GizmoFrameData output = {};
                HitFrame       hits   = {};
                ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                ASSERT_EQ(output.LineCount, 12 + C::CameraFovArcSegments + 1);
                EXPECT_EQ(output.TriangleCount, 22u);
                EXPECT_EQ(output.SpriteCount, 6u);
                EXPECT_EQ(hits.Count, 6u);
                EXPECT_FALSE(output.DecorationsOmitted);
                const float scale = float(height) / bounds.Size.y;
                for (uint32_t i = 0; i < output.SpriteCount; ++i)
                {
                    EXPECT_EQ(output.Sprites[i].Size, 8.f * scale);
                    EXPECT_EQ(output.Sprites[i].Fill, UI::ZUIPackColor(UI::Tokens::TextPrimary.R, UI::Tokens::TextPrimary.G, UI::Tokens::TextPrimary.B, 1.f));
                }
                for (const auto& shape : hits.View())
                {
                    EXPECT_EQ(shape.Priority, HitPriority::Component);
                    EXPECT_EQ(shape.Points[1].x - shape.Points[0].x, 8.f);
                    EXPECT_EQ(hits.Pick((shape.Points[0] + shape.Points[1]) * .5f).Handle, shape.Handle);
                }
                EXPECT_EQ(output.Lines[0].Width, 1.5f * scale);
                EXPECT_EQ(output.Lines[12].Color, UI::ZUIPackColor(UI::Tokens::AccentBlue.R, UI::Tokens::AccentBlue.G, UI::Tokens::AccentBlue.B, 1.f));
                for (uint32_t i = 12; i < 12 + C::CameraFovArcSegments; ++i)
                {
                    const auto& point = output.Lines[i].A;
                    EXPECT_NEAR(std::hypot(double(point.y), double(point.z)), 1.0, .00001);
                    EXPECT_EQ(point.x, 0.f);
                }
                EXPECT_EQ(output.Lines[12 + C::CameraFovArcSegments].DashOn, 4.f * scale);
                for (uint32_t i = 0; i < output.TriangleCount; ++i)
                {
                    const auto& triangle = output.Triangles[i];
                    EXPECT_EQ(triangle.Color, UI::ZUIPackColor(UI::Tokens::BgViewport.R, UI::Tokens::BgViewport.G, UI::Tokens::BgViewport.B, UI::Tokens::BgViewport.A));
                    EXPECT_EQ(triangle.AntiAliasEdges, 0u);
                }
            }
        }
    }
}

TEST_F(GizmoCameraTest, MalformedAndOverBudgetCameraGeometryCannotEraseOtherComponentsOrTransforms)
{
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 2.f});
    auto input = Geometry(600, Operation::Translate);
    ASSERT_TRUE(ResolveLightFrame(session, input.Light));
    GizmoFrameData output = {}, baseline = {};
    HitFrame       hits = {}, base_hits = {};
    const auto     frame = input.Frustum;
    input.Frustum        = {};
    ASSERT_EQ(BuildFrame(input, baseline, base_hits), FrameBuildResult::Built);
    input.Frustum         = frame;
    input.Frustum.Forward = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, baseline.LineCount);
    EXPECT_EQ(output.SpriteCount, baseline.SpriteCount);
    EXPECT_EQ(hits.Count, base_hits.Count);
    EXPECT_TRUE(output.DecorationsOmitted);
    input.Frustum                    = frame;
    input.Frustum.Camera.AspectRatio = std::numeric_limits<float>::max();
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, baseline.LineCount);
    EXPECT_EQ(hits.Count, base_hits.Count);
    output.Reset();
    hits.Reset();
    output.LineCount      = GizmoFrameData::MaxLines - 1;
    output.Lines[0].Color = 123u;
    hits.Count            = 1;
    hits.Shapes[0].Handle = 456u;
    input.Frustum         = frame;
    BuildCameraGeometry(input, projection, output, hits);
    EXPECT_EQ(output.LineCount, GizmoFrameData::MaxLines - 1);
    EXPECT_EQ(output.Lines[0].Color, 123u);
    EXPECT_EQ(hits.Count, 1u);
    EXPECT_EQ(hits.Shapes[0].Handle, 456u);
    EXPECT_TRUE(output.DecorationsOmitted);
}

TEST_F(GizmoCameraTest, FarClippingRemovesCornerSpritesAndTheirHitsAcrossDepthConventions)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            Projection(ortho, reverse, false, 12.f);
            const auto     input  = Geometry();
            GizmoFrameData output = {};
            HitFrame       hits   = {};
            ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
            EXPECT_FALSE(output.DecorationsOmitted);
            // Near-plane side handles stay visible even after the Far plane clips.
            EXPECT_EQ(output.SpriteCount, 2u);
            EXPECT_EQ(hits.Count, 2u);
            EXPECT_GT(output.LineCount, 0u);
            for (uint32_t i = 0; i < output.LineCount; ++i)
            {
                EXPECT_GE(output.Lines[i].A.z, -2.001f);
                EXPECT_GE(output.Lines[i].B.z, -2.001f);
            }
        }
    }
}

TEST_F(GizmoCameraTest, TransformHitsWinAndHiddenTransformPivotDoesNotHideActualCameraOrigin)
{
    Projection(false, false, false);
    auto  input  = Geometry(600, Operation::Translate);
    Vec3f corner = {};
    ASSERT_TRUE(CameraCorner(input.Frustum, 0, Value().Far, corner));
    input.Pivot           = corner;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(hits.Pick(Screen(corner)).Handle, uint32_t(Handle::TranslateView));
    input.Pivot = camera.Position;
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    for (const auto& shape : hits.View())
    {
        EXPECT_TRUE(IsCameraHandle(Handle(shape.Handle)));
    }
    input.Mode           = Operation::Select;
    input.Frustum.Origin = camera.Position;
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(hits.Count, 0u);
}

TEST_F(GizmoCameraTest, CameraAndLightComponentsCoexistWithinReservedPickingAndPrimitiveBudgets)
{
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 2.f});
    auto input = Geometry(600, Operation::Rotate);
    ASSERT_TRUE(ResolveLightFrame(session, input.Light));
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_FALSE(output.DecorationsOmitted);
    uint32_t components = 0;
    for (const auto& shape : hits.View())
    {
        if (shape.Priority == HitPriority::Component)
        {
            ++components;
        }
    }
    EXPECT_EQ(components, 11u);
    EXPECT_LE(hits.Count, HitFrame::MaxShapes);
    EXPECT_TRUE(output.ValidCounts());
}

TEST_F(GizmoCameraTest, SelectionModeAndReadOnlyExitCancelAndRetireTheCameraAdapter)
{
    const auto other = Entity(2);
    session.InvalidateForExternalMutation();
    ASSERT_TRUE(session.MarkSaved());
    auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.CameraActive());
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
    input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    ASSERT_TRUE(session.Select(other));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
    ASSERT_TRUE(session.Select(entity));
    input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
    EXPECT_FALSE(session.BeginCamera(input));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoCameraTest, ACollapsedForwardCannotFabricateAFrustumOrAFieldMapping)
{
    scene.GetComponent<TransformComponent>(entity)->Scale.z = 0.f;
    scene.SynchronizeHierarchy();
    CameraFrame frame = {.Valid = true};
    EXPECT_FALSE(ResolveCameraFrame(session, frame));
    EXPECT_FALSE(frame.Valid);
    CameraHandle handle = {.Parameter = 123.0};
    EXPECT_FALSE(ResolveCameraHandle(frame, Handle::CameraFarLowerLeft, handle));
    EXPECT_EQ(handle.Parameter, 123.0);
    EXPECT_FALSE(session.BeginCamera({.Expected = frame, .Projection = projection, .Bounds = bounds, .Key = 99, .PointerSerial = 1, .Constraint = Handle::CameraFarLowerLeft}));
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoCameraTest, ThousandPreviewCancelCyclesReuseSessionAndHistoryStorage)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginCamera(input));
    session.Cancel();
    const auto bytes = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.BeginCamera(input));
        ASSERT_TRUE(session.UpdateCamera(MotionValue(input, 5.f), true));
        session.Cancel();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_FLOAT_EQ(Value().Far, 4.f);
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}
