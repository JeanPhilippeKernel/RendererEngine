#include <Tetragrama/EditorScene.h>
#include <Tetragrama/EditorSession.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/SceneSettingsYAML.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
namespace G = ZEngine::Core::Maths::Geometry;

class GizmoGridTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    memory     = {};
    Scene                                   world      = {};
    EditorScene                             owner      = {};
    EditorSession                           session    = {};
    std::unique_ptr<ZEngine::EngineContext> engine     = {};
    G::ProjectionFrame                      projection = {};
    G::Viewport                             bounds     = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    void SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        world.Initialize(&memory.MainArena);
        owner.UUID                        = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        owner.AuthoredGrid.Origin         = {.4f, -.3f, .2f};
        owner.AuthoredGrid.Orientation    = {.2f, -.3f, .4f, .8f};
        owner.AuthoredGrid.MajorLineEvery = 7;
        owner.AuthoredGrid.UColor         = {.1f, .2f, .3f, .4f};
        owner.AuthoredGrid.Labels         = SceneGridLabels::UV;
        ASSERT_TRUE(CanonicalizeSceneGridSettings(owner.AuthoredGrid));
        owner.SyncGridFromAuthored();
        engine        = std::make_unique<ZEngine::EngineContext>();
        engine->Scene = &world;
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&world));
        ASSERT_TRUE(owner.AttachSession(session, *engine));
        ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
        Mode(Operation::Translate);
        Snap(SnapTarget::Translation, false);
        Snap(SnapTarget::Rotation, false, 10.f);
        Camera({7.f, 5.f, 10.f});
    }

    void TearDown() override
    {
        session.Shutdown();
        world.Shutdown();
        memory.Shutdown();
    }

    void Mode(Operation mode)
    {
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
        ASSERT_TRUE(session.ProcessRequests());
    }

    void Snap(SnapTarget target, bool enabled, float increment = .25f)
    {
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = target, .Enabled = enabled}));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapIncrement, .Target = target, .Increment = increment}));
        ASSERT_TRUE(session.ProcessRequests());
    }

    void Camera(const Vec3f& offset, bool orthographic = false, bool reverse = false)
    {
        using namespace ZEngine::Rendering::Cameras;
        FlyCamera  camera(4.f / 3.f, {});
        const auto direction = -offset.normalize();
        camera.SetPosition(owner.AuthoredGrid.Origin + offset);
        camera.SetOrientation(degrees(-std::asin(direction.y)), degrees(std::atan2(direction.x, -direction.z)));
        if (orthographic)
        {
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        }
        auto frame = camera.CaptureFrameData();
        if (reverse)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                frame.Projection(2, column) = frame.Projection(3, column) - frame.Projection(2, column);
            }
        }
        ASSERT_TRUE(G::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, orthographic ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &projection));
    }

    TargetFrame Frame()
    {
        TargetFrame frame = {};
        EXPECT_TRUE(ResolveTargetFrame(session, frame));
        return frame;
    }

    Vec2f Screen(const Vec3f& point)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(point, projection, bounds, &result));
        return result.Screen;
    }

    static Vec3f Offset(const TargetFrame& frame, const Vec3f& local)
    {
        return frame.Axes[0] * local.x + frame.Axes[1] * local.y + frame.Axes[2] * local.z;
    }

    TranslationBegin Translate(Handle handle = Handle::TranslateX, const Vec3f& grab = {.7f, 0.f, 0.f})
    {
        const auto frame = Frame();
        return {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(frame.Pivot + Offset(frame, grab)), .Key = 123, .PointerSerial = 456, .Constraint = handle};
    }

    RotationBegin Rotate(uint32_t axis = 0, bool view = false)
    {
        const auto frame = Frame();
        Camera(frame.Axes[axis] * 10.f);
        const auto point = frame.Pivot + frame.Axes[(axis + 1) % 3];
        return {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(point), .RingPoint = point, .Key = 123, .PointerSerial = 456, .Constraint = view ? Handle::RotateView : Handle(uint32_t(Handle::RotateX) + axis)};
    }

    Vec2f AnglePointer(const RotationBegin& begin, double angle)
    {
        const auto axis   = begin.Constraint == Handle::RotateView ? -begin.Projection.Forward : begin.Expected.Axes[uint32_t(begin.Constraint) - uint32_t(Handle::RotateX)];
        const auto radial = begin.RingPoint - begin.Expected.Pivot;
        const auto point  = begin.Expected.Pivot + radial * float(std::cos(angle)) + cross3d(axis, radial) * float(std::sin(angle));
        return Screen(point);
    }

    static void Near(const Vec3f& actual, const Vec3f& expected, float tolerance = .0002f)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(actual[axis], expected[axis], tolerance) << axis;
        }
    }

    void Same(const SceneGridSettings& expected)
    {
        EXPECT_TRUE(EqualSceneGridSettings(owner.AuthoredGrid, expected));
        ZEngine::ECS::SceneGridSettings rendered = {};
        ASSERT_TRUE(ZEngine::Rendering::Scenes::ReadSceneGridRenderSnapshot(owner.Grid, rendered));
        EXPECT_TRUE(EqualSceneGridSettings(rendered, expected));
    }

    void Replay(const SceneGridSettings& before, const SceneGridSettings& after)
    {
        EXPECT_EQ(world.AliveCount(), 0u);
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.TranslationKey(), 0u);
        EXPECT_EQ(session.RotationKey(), 0u);
        ASSERT_EQ(session.History().UndoCount(), 1u);
        EXPECT_STREQ(session.History().UndoName(), "Grid settings");
        Same(after);
        ASSERT_TRUE(session.MarkSaved());
        ASSERT_TRUE(session.Undo());
        Same(before);
        EXPECT_TRUE(session.Dirty());
        ASSERT_TRUE(session.Redo());
        Same(after);
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
        EXPECT_TRUE(session.Selection().Entities().empty());
    }
};

TEST_F(GizmoGridTest, ExplicitGridFrameHasNoEntityAndUsesRotatedUNVRegardlessOfEntitySpacePreference)
{
    RecordProperty("TargetFrameBytes", int(sizeof(TargetFrame)));
    RecordProperty("SceneIdentityBytes", int(sizeof(SceneTargetIdentity)));
    RecordProperty("TranslationInteractionBytes", int(sizeof(TranslationInteraction)));
    RecordProperty("RotationInteractionBytes", int(sizeof(RotationInteraction)));
    const auto frame = Frame();
    EXPECT_EQ(frame.Scene.Kind, SceneTarget::Grid);
    EXPECT_EQ(frame.Scene.UUID, owner.UUID);
    EXPECT_EQ(frame.Scene.Epoch, session.SceneEpoch());
    EXPECT_TRUE(frame.Target.UUID.is_nil());
    EXPECT_FALSE(frame.Target.CachedEntity.IsValid());
    EXPECT_FALSE(frame.CustomPivot);
    const auto matrix = quaternionToMat4(owner.AuthoredGrid.Orientation);
    Near(frame.Pivot, owner.AuthoredGrid.Origin);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        Near(frame.Axes[axis], {matrix(0, axis), matrix(1, axis), matrix(2, axis)});
    }
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto local = Frame();
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        Near(local.Axes[axis], frame.Axes[axis]);
    }
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoGridTest, EveryLocalAxisTranslatesFromImmutableStartWithOneSharedGridOperation)
{
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        SCOPED_TRACE(axis);
        Vec3f grab = {}, delta = {};
        grab[axis]         = .7f;
        delta[axis]        = .43f;
        const auto before  = owner.AuthoredGrid;
        const auto input   = Translate(Handle(uint32_t(Handle::TranslateX) + axis), grab);
        const auto pointer = Screen(input.Expected.Pivot + Offset(input.Expected, grab + delta));
        ASSERT_TRUE(session.BeginTranslation(input));
        ASSERT_TRUE(session.UpdateTranslation(pointer, true));
        ASSERT_TRUE(session.UpdateTranslation(pointer, true));
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_EQ(session.TransformCount(), 0u);
        TranslationSnapshot snapshot = {};
        ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
        EXPECT_EQ(snapshot.Scene, input.Expected.Scene);
        Near(snapshot.Components, delta);
        Near(owner.AuthoredGrid.Origin, before.Origin + Offset(input.Expected, delta));
        auto after   = owner.AuthoredGrid;
        auto style   = after;
        style.Origin = before.Origin;
        EXPECT_TRUE(EqualSceneGridSettings(style, before));
        ASSERT_TRUE(session.UpdateTranslation(pointer, false));
        Replay(before, after);
        ASSERT_TRUE(session.Undo()); // Retain a single-entry expectation on the next axis.
    }
}

TEST_F(GizmoGridTest, LocalPlaneSnappingFineAndInversionUseCapturedSettings)
{
    struct Plane
    {
        Handle Constraint = Handle::None;
        Vec3f  Grab = {}, Delta = {};
    };
    const Plane planes[] = {
        {Handle::TranslateXY, {.5f, .5f, 0.f}, {.37f, -.12f, 0.f}},
        {Handle::TranslateYZ, {0.f, .5f, .5f}, {0.f, .37f, -.12f}},
        {Handle::TranslateZX, {.5f, 0.f, .5f}, {-.12f, 0.f, .37f}},
    };
    for (const auto& plane : planes)
    {
        Snap(SnapTarget::Translation, true);
        const auto before  = owner.AuthoredGrid;
        const auto input   = Translate(plane.Constraint, plane.Grab);
        const auto pointer = Screen(input.Expected.Pivot + Offset(input.Expected, plane.Grab + plane.Delta));
        ASSERT_TRUE(session.BeginTranslation(input));
        // Changing preferences does not reinterpret a captured mapping/snap.
        Snap(SnapTarget::Translation, false, 1.f);
        ASSERT_TRUE(session.UpdateTranslation(pointer, true));
        Vec3f          snapped     = {};
        const uint32_t normal_axis = plane.Constraint == Handle::TranslateXY ? 2u : plane.Constraint == Handle::TranslateYZ ? 0u : 1u;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (axis != normal_axis)
            {
                const float start = dot(input.Expected.Axes[axis], before.Origin);
                snapped[axis]     = std::round((start + plane.Delta[axis]) / .25f) * .25f - start;
            }
        }
        Near(owner.AuthoredGrid.Origin, before.Origin + Offset(input.Expected, snapped));
        ASSERT_TRUE(session.UpdateTranslation(pointer, true, false, true));
        Near(owner.AuthoredGrid.Origin, before.Origin + Offset(input.Expected, plane.Delta));
        ASSERT_TRUE(session.UpdateTranslation(pointer, true, true));
        // Fine scales screen displacement before perspective unprojection,
        // not the resulting world displacement on an oblique plane.
        G::Ray      press_ray = {}, fine_ray = {};
        G::PlaneHit press_hit = {}, fine_hit = {};
        ASSERT_TRUE(G::UnprojectRay(input.Pointer, projection, bounds, &press_ray));
        ASSERT_TRUE(G::UnprojectRay(input.Pointer + (pointer - input.Pointer) * Constants::FineMotionFactor, projection, bounds, &fine_ray));
        ASSERT_TRUE(G::RayPlane(press_ray, before.Origin, input.Expected.Axes[normal_axis], Constants::PlaneParallelCosine, &press_hit));
        ASSERT_TRUE(G::RayPlane(fine_ray, before.Origin, input.Expected.Axes[normal_axis], Constants::PlaneParallelCosine, &fine_hit));
        Near(owner.AuthoredGrid.Origin, before.Origin + fine_hit.Position - press_hit.Position);
        session.Cancel();
        Same(before);
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.TranslationKey(), 0u);
    }
}

TEST_F(GizmoGridTest, ViewTranslationSnapsInLocalFrameInPerspectiveOrthographicAndReverseDepth)
{
    Snap(SnapTarget::Translation, true);
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            Camera({7.f, 5.f, 10.f}, ortho, reverse);
            const auto before   = owner.AuthoredGrid;
            const auto input    = Translate(Handle::TranslateView, {});
            auto       delta    = Offset(input.Expected, {.37f, -.12f, .2f});
            delta              -= projection.Forward * dot(delta, projection.Forward);
            const auto pointer  = Screen(input.Expected.Pivot + delta);
            ASSERT_TRUE(session.BeginTranslation(input));
            ASSERT_TRUE(session.UpdateTranslation(pointer, true));
            Vec3f local = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const float start = dot(before.Origin, input.Expected.Axes[axis]);
                local[axis]       = std::round((start + dot(delta, input.Expected.Axes[axis])) / .25f) * .25f - start;
            }
            Near(owner.AuthoredGrid.Origin, before.Origin + Offset(input.Expected, local));
            session.Cancel();
            Same(before);
        }
    }
}

TEST_F(GizmoGridTest, LocalAndViewRotationPreserveOriginAndStyleWithCanonicalQuaternionAndExactReplay)
{
    Mode(Operation::Rotate);
    for (uint32_t ring = 0; ring < 4; ++ring)
    {
        const uint32_t axis   = ring % 3;
        const auto     before = owner.AuthoredGrid;
        const auto     input  = Rotate(axis, ring == 3);
        ASSERT_TRUE(session.BeginRotation(input));
        ASSERT_TRUE(session.UpdateRotation(AnglePointer(input, .4), true));
        ASSERT_TRUE(session.UpdateRotation(AnglePointer(input, .7), false));
        auto       after       = owner.AuthoredGrid;
        const auto local_delta = fromAxisAngle(Vec3f(axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f), .7f);
        const auto expected    = quaternionToMat4(before.Orientation * local_delta);
        const auto actual      = quaternionToMat4(after.Orientation);
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                EXPECT_NEAR(actual(row, column), expected(row, column), .0002f);
            }
        }
        EXPECT_NEAR(after.Orientation.dot(after.Orientation), 1.f, .000001f);
        EXPECT_GE(after.Orientation.w, 0.f);
        auto style        = after;
        style.Orientation = before.Orientation;
        EXPECT_TRUE(EqualSceneGridSettings(style, before));
        Replay(before, after);
        ASSERT_TRUE(session.Undo());
    }
}

TEST_F(GizmoGridTest, RotationSnapFineAndInversionRetainTheCapturedLocalAxis)
{
    Mode(Operation::Rotate);
    Snap(SnapTarget::Rotation, true, 10.f);
    const auto before = owner.AuthoredGrid;
    const auto input  = Rotate(1);
    ASSERT_TRUE(session.BeginRotation(input));
    Snap(SnapTarget::Rotation, false, 45.f);
    const auto pointer = AnglePointer(input, 26.0 * std::numbers::pi / 180.0);
    for (uint32_t policy = 0; policy < 3; ++policy)
    {
        ASSERT_TRUE(session.UpdateRotation(pointer, true, policy == 2, policy == 1));
        RotationSnapshot snapshot = {};
        ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
        EXPECT_EQ(snapshot.Scene, input.Expected.Scene);
        const double expected = policy == 0 ? 30.0 : policy == 1 ? 26.0 : 2.6;
        EXPECT_NEAR(snapshot.Angle, expected * std::numbers::pi / 180.0, .00001);
        Near(snapshot.Axis, input.Expected.Axes[1]);
        Near(snapshot.Pivot, before.Origin);
    }
    session.Cancel();
    Same(before);
    EXPECT_EQ(session.RotationKey(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoGridTest, NoopAndReturnToPressPreserveRedoAndClearAllAdapterTokens)
{
    const auto before = owner.AuthoredGrid;
    const auto input  = Translate();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f})), false));
    ASSERT_TRUE(session.Undo());
    Same(before);
    for (uint32_t kind = 0; kind < 2; ++kind)
    {
        if (kind == 0)
        {
            ASSERT_TRUE(session.BeginTranslation(Translate()));
            ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.4f, 0.f, 0.f})), true));
            ASSERT_TRUE(session.UpdateTranslation(input.Pointer, false));
        }
        else
        {
            Mode(Operation::Rotate);
            const auto ring = Rotate();
            ASSERT_TRUE(session.BeginRotation(ring));
            ASSERT_TRUE(session.UpdateRotation(AnglePointer(ring, .4), true));
            ASSERT_TRUE(session.UpdateRotation(ring.Pointer, false));
        }
        Same(before);
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.TranslationKey(), 0u);
        EXPECT_EQ(session.RotationKey(), 0u);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_EQ(session.History().RedoCount(), 1u);
        EXPECT_FALSE(session.Dirty());
    }
}

TEST_F(GizmoGridTest, InvalidSamplesKeepAcceptedPresentationAndMalformedReleaseCommitsOnlyThatPreview)
{
    for (uint32_t kind = 0; kind < 2; ++kind)
    {
        const auto before = owner.AuthoredGrid;
        if (kind == 0)
        {
            const auto input = Translate();
            ASSERT_TRUE(session.BeginTranslation(input));
            ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f})), true));
            TranslationSnapshot accepted = {};
            ASSERT_TRUE(session.ReadTranslationSnapshot(accepted));
            EXPECT_FALSE(session.UpdateTranslation({std::numeric_limits<float>::quiet_NaN(), 0.f}, true));
            TranslationSnapshot retained = {};
            ASSERT_TRUE(session.ReadTranslationSnapshot(retained));
            Near(retained.Components, accepted.Components, 0.f);
            ASSERT_TRUE(session.UpdateTranslation({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
        }
        else
        {
            Mode(Operation::Rotate);
            const auto input = Rotate();
            ASSERT_TRUE(session.BeginRotation(input));
            ASSERT_TRUE(session.UpdateRotation(AnglePointer(input, .5), true));
            RotationSnapshot accepted = {};
            ASSERT_TRUE(session.ReadRotationSnapshot(accepted));
            EXPECT_FALSE(session.UpdateRotation({std::numeric_limits<float>::quiet_NaN(), 0.f}, true));
            RotationSnapshot retained = {};
            ASSERT_TRUE(session.ReadRotationSnapshot(retained));
            EXPECT_EQ(retained.Angle, accepted.Angle);
            ASSERT_TRUE(session.UpdateRotation({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
        }
        const auto after = owner.AuthoredGrid;
        Replay(before, after);
        ASSERT_TRUE(session.Undo());
    }
}

TEST_F(GizmoGridTest, ScaleAndCustomPivotRejectBeforeMutatingOrCancellingAnOwnedGridPreview)
{
    const auto input = Translate();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f})), true));
    const auto accepted = owner.AuthoredGrid;
    const auto revision = session.GizmoSettings().Revision;
    EXPECT_FALSE(session.BeginScale({.Expected = input.Expected, .Projection = projection, .Bounds = bounds, .Pointer = input.Pointer, .Key = 234, .PointerSerial = 567, .Constraint = Handle::ScaleX}));
    EXPECT_FALSE(session.BeginPivot({.Expected = input.Expected, .Projection = projection, .Bounds = bounds, .Pointer = input.Pointer, .Key = 234, .PointerSerial = 567}));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Scale}));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(session.GizmoSettings().Revision, revision);
    EXPECT_TRUE(session.TranslationActive());
    Same(accepted);
    session.Cancel();
    EXPECT_FALSE(session.BeginScale({.Expected = Frame(), .Projection = projection, .Bounds = bounds, .Pointer = input.Pointer, .Key = 234, .PointerSerial = 567, .Constraint = Handle::ScaleUniform}));
    EXPECT_FALSE(session.BeginPivot({.Expected = Frame(), .Projection = projection, .Bounds = bounds, .Pointer = input.Pointer, .Key = 234, .PointerSerial = 567}));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoGridTest, StaleStyleOwnerEpochSelectionAndPreferencesCannotRearmCapturedFrames)
{
    for (uint32_t fault = 0; fault < 5; ++fault)
    {
        const auto input = Translate();
        if (fault == 0)
        {
            owner.AuthoredGrid.LineWidth += .1f;
        }
        else if (fault == 1)
        {
            owner.UUID = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        }
        else if (fault == 2)
        {
            ASSERT_TRUE(session.BindScene(&world));
            ASSERT_TRUE(owner.AttachSession(session, *engine));
            ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
        }
        else if (fault == 3)
        {
            session.ClearSelection();
            ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
        }
        else
        {
            Snap(SnapTarget::Translation, true);
        }
        EXPECT_FALSE(session.BeginTranslation(input));
        EXPECT_FALSE(session.Active());
    }
}

TEST_F(GizmoGridTest, CancellationReasonsRestoreTheExactOwnedGridAndNeverLeakCaptureIntoNextInteraction)
{
    for (uint32_t kind = 0; kind < 2; ++kind)
    {
        for (uint32_t reason = 0; reason < 7; ++reason)
        {
            const auto before = owner.AuthoredGrid;
            Mode(kind == 0 ? Operation::Translate : Operation::Rotate);
            ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
            if (kind == 0)
            {
                Camera({7.f, 5.f, 10.f});
                const auto input = Translate();
                ASSERT_TRUE(session.BeginTranslation(input));
                ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f})), true));
            }
            else
            {
                const auto input = Rotate();
                ASSERT_TRUE(session.BeginRotation(input));
                ASSERT_TRUE(session.UpdateRotation(AnglePointer(input, .5), true));
            }
            if (reason < 3)
            {
                if (kind == 0)
                {
                    ASSERT_TRUE(session.UpdateTranslation({}, true, false, false, reason != 0, reason == 1, reason == 2));
                }
                else
                {
                    ASSERT_TRUE(session.UpdateRotation({}, true, false, false, reason != 0, reason == 1, reason == 2));
                }
            }
            else if (reason == 3)
            {
                session.ClearSelection();
            }
            else if (reason == 4)
            {
                Mode(Operation::Select);
            }
            else if (reason == 5)
            {
                owner.SourceReadOnly = true;
            }
            else
            {
                ASSERT_TRUE(session.BindScene(&world));
                ASSERT_TRUE(owner.AttachSession(session, *engine));
            }
            if (reason == 5)
            {
                session.EndMutationPhase();
                ASSERT_TRUE(session.BeginMutationPhase());
                owner.SourceReadOnly = false;
            }
            EXPECT_FALSE(session.Active());
            EXPECT_EQ(session.TranslationKey(), 0u);
            EXPECT_EQ(session.RotationKey(), 0u);
            Same(before);
            EXPECT_EQ(session.History().UndoCount(), 0u);
            EXPECT_FALSE(session.Dirty());
        }
    }
}

TEST_F(GizmoGridTest, ForeignGridIsNeverOverwrittenAndSnapshotReadsPreserveOutputOnWrongThread)
{
    const auto input = Translate();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f})), true));
    bool        rejected = false;
    std::thread reader([&] {
        TranslationSnapshot snapshot = {.Key = 77};
        rejected                     = !session.ReadTranslationSnapshot(snapshot) && snapshot.Key == 77;
    });
    reader.join();
    EXPECT_TRUE(rejected);
    owner.AuthoredGrid.BaseSpacing = .7f;
    const auto          foreign    = owner.AuthoredGrid;
    TranslationSnapshot stale      = {.Key = 88};
    EXPECT_FALSE(session.ReadTranslationSnapshot(stale));
    EXPECT_EQ(stale.Key, 88u);
    EXPECT_FALSE(session.UpdateTranslation(input.Pointer, true));
    EXPECT_TRUE(EqualSceneGridSettings(owner.AuthoredGrid, foreign));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.TranslationKey(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(GizmoGridTest, RepeatedGridDragsReuseAllScratchAndTheSharedEntryAndByteBudgets)
{
    const auto bytes = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 500; ++i)
    {
        const auto before = owner.AuthoredGrid;
        const auto input  = Translate();
        ASSERT_TRUE(session.BeginTranslation(input));
        const auto pointer = Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f}));
        ASSERT_TRUE(session.UpdateTranslation(pointer, true));
        ASSERT_TRUE(session.UpdateTranslation(pointer, false));
        ASSERT_TRUE(session.Undo());
        Same(before);
        ASSERT_TRUE(session.Redo());
        EXPECT_LE(session.History().UndoCount(), 8u);
        EXPECT_LE(session.History().BytesUsed(), session.History().ByteCapacity());
        if (i % 10 == 0)
        {
            Mode(Operation::Rotate);
            const auto rotate = Rotate(1);
            const auto start  = owner.AuthoredGrid;
            ASSERT_TRUE(session.BeginRotation(rotate));
            ASSERT_TRUE(session.UpdateRotation(AnglePointer(rotate, .3), true));
            ASSERT_TRUE(session.UpdateRotation(AnglePointer(rotate, .5), false));
            ASSERT_TRUE(session.Undo());
            Same(start);
            ASSERT_TRUE(session.Redo());
            Mode(Operation::Translate);
        }
        Camera({7.f, 5.f, 10.f});
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
}

TEST_F(GizmoGridTest, MissingOwnerInvalidSettingsAndReadonlySceneFailBeforeAnyInteractionBegins)
{
    const auto value     = owner.AuthoredGrid;
    owner.SourceReadOnly = true;
    TargetFrame frame    = {};
    EXPECT_FALSE(ResolveTargetFrame(session, frame));
    EXPECT_FALSE(frame.Valid);
    EXPECT_FALSE(session.BeginTranslation({}));
    owner.SourceReadOnly           = false;
    const auto input               = Translate();
    owner.AuthoredGrid.Orientation = {0.f, 0.f, 0.f, 0.f};
    EXPECT_FALSE(session.BeginTranslation(input));
    EXPECT_FALSE(session.Active());
    owner.AuthoredGrid = value;
    session.EndMutationPhase();
    EXPECT_FALSE(session.BeginTranslation(input));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.BindScene(&world));
    ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
    EXPECT_FALSE(ResolveTargetFrame(session, frame));
    EXPECT_FALSE(session.BeginTranslation(input));
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoGridTest, RetainedGridFrameDoesNotChangeAcrossCancelOrSceneReplacement)
{
    using namespace ZEngine::Applications;
    AppRenderPipeline pipeline = {};
    pipeline.InitializeFrameStorage(&memory.MainArena, 4);
    const auto before = owner.AuthoredGrid;
    const auto input  = Translate();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(input.Expected.Pivot + Offset(input.Expected, {1.2f, 0.f, 0.f})), true));
    const auto       preview = owner.AuthoredGrid;
    RenderFrameState state = {}, retained = {};
    ASSERT_TRUE(pipeline.PublishFrameState(state, &owner));
    ASSERT_TRUE(pipeline.TryReadFrameState(retained));
    session.Cancel();
    Same(before);
    SceneGridSettings copied = {};
    ASSERT_TRUE(ZEngine::Rendering::Scenes::ReadSceneGridRenderSnapshot(retained.Scene.Grid, copied));
    EXPECT_TRUE(EqualSceneGridSettings(copied, preview));
    ASSERT_TRUE(session.BindScene(nullptr));
    owner.Grid = {};
    ASSERT_TRUE(ZEngine::Rendering::Scenes::ReadSceneGridRenderSnapshot(retained.Scene.Grid, copied));
    EXPECT_TRUE(EqualSceneGridSettings(copied, preview));
    pipeline.ReleaseFrameState();
}

TEST_F(GizmoGridTest, TranslatedAndRotatedGridRoundTripsThroughIndependentYAMLStorage)
{
    const auto translate = Translate();
    ASSERT_TRUE(session.BeginTranslation(translate));
    ASSERT_TRUE(session.UpdateTranslation(Screen(translate.Expected.Pivot + Offset(translate.Expected, {1.2f, 0.f, 0.f})), false));
    Mode(Operation::Rotate);
    const auto rotate = Rotate(2);
    ASSERT_TRUE(session.BeginRotation(rotate));
    ASSERT_TRUE(session.UpdateRotation(AnglePointer(rotate, .6), false));
    const auto  authored = owner.AuthoredGrid;
    std::string bytes    = {};
    {
        SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(1)));
        auto root = storage.Destination();
        ASSERT_TRUE(EncodeSceneGridYAML(authored, &root, nullptr));
        SceneYAML::Emitter emitter(4096);
        emitter << root;
        ASSERT_TRUE(emitter.Output(&bytes));
    }
    SceneYAML::Storage independent = {};
    ASSERT_TRUE(independent.Initialize(ZMega(1)));
    auto root = independent.Destination();
    ASSERT_TRUE(ReadSceneYAML(bytes, {}, &root, nullptr));
    SceneGridSettings loaded = {};
    ASSERT_TRUE(DecodeSceneGridYAML(root, &loaded, nullptr));
    EXPECT_TRUE(EqualSceneGridSettings(loaded, authored));
    EXPECT_EQ(session.History().UndoCount(), 2u);
}
