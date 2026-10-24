#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>
#include <Tetragrama/Gizmo/GizmoLightBuilder.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
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

class GizmoLightTest : public testing::Test
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
        scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 2.f});
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
    void Projection(bool ortho = false, bool reverse = false, bool oblique = true, float near_distance = .1f)
    {
        CameraSetting settings = {};
        settings.NearPlane     = near_distance;
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
    LightComponent& Light()
    {
        return *scene.GetComponent<LightComponent>(entity);
    }
    LightFrame Frame()
    {
        scene.SynchronizeHierarchy();
        LightFrame result = {};
        EXPECT_TRUE(ResolveLightFrame(session, result));
        return result;
    }
    Vec2f Screen(const Vec3f& point)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(point, projection, bounds, &result));
        return result.Screen;
    }
    LightBegin Input(Handle constraint = Handle::LightRangePositiveX)
    {
        LightBegin  result = {.Expected = Frame(), .Projection = projection, .Bounds = bounds, .Key = 99, .PointerSerial = ++serial, .Constraint = constraint};
        LightHandle handle = {};
        EXPECT_TRUE(ResolveLightHandle(result.Expected, constraint, handle));
        result.Pointer = Screen(handle.Position);
        return result;
    }
    Vec2f Motion(const LightBegin& input, float delta)
    {
        LightHandle handle = {};
        EXPECT_TRUE(ResolveLightHandle(input.Expected, input.Constraint, handle));
        return Screen(handle.Position + handle.Direction * delta);
    }
    Vec2f FineMotion(const LightBegin& input, float delta)
    {
        const auto motion = Motion(input, delta);
        Vec2f      scaled = {};
        // The mapping scales in double and narrows once. Chained float-vector
        // arithmetic can instead produce an adjacent logical-coordinate float.
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            scaled[axis] = float(double(input.Pointer[axis]) + (double(motion[axis]) - input.Pointer[axis]) * C::FineMotionFactor);
        }
        return scaled;
    }
    FrameInput Geometry(uint32_t height = 600, Operation mode = Operation::Select)
    {
        return {.Camera = camera, .Viewport = bounds, .TargetWidth = height * 4 / 3, .TargetHeight = height, .Mode = mode, .Editable = true, .Light = Frame()};
    }
    void Sync()
    {
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
    }
};

TEST_F(GizmoLightTest, AllFiveWorldRangeHandlesShareOneTypedFieldTransactionAcrossProjectionConventions)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t index = uint32_t(Handle::LightRangePositiveX); index <= uint32_t(Handle::LightRangeNegativeZ); ++index)
            {
                SCOPED_TRACE(index);
                Projection(ortho, reverse);
                const auto input  = Input(Handle(index));
                const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
                ASSERT_TRUE(session.BeginLight(input));
                ASSERT_TRUE(session.LightActive());
                ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
                EXPECT_NEAR(Light().Range, 2.5f, .0002f);
                LightSnapshot snapshot = {};
                ASSERT_TRUE(session.ReadLightSnapshot(snapshot));
                EXPECT_EQ(snapshot.Constraint, Handle(index));
                EXPECT_EQ(snapshot.Key, 99u);
                EXPECT_EQ(snapshot.PointerSerial, input.PointerSerial);
                EXPECT_NEAR(snapshot.Value, 2.5f, .0002f);
                ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), false));
                EXPECT_FALSE(session.Active());
                EXPECT_TRUE(session.Dirty());
                EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
                ASSERT_TRUE(session.Undo());
                EXPECT_FLOAT_EQ(Light().Range, 2.f);
                EXPECT_FALSE(session.Dirty());
                ASSERT_TRUE(session.Redo());
                EXPECT_NEAR(Light().Range, 2.5f, .0002f);
                ASSERT_TRUE(session.Undo());
                Sync();
            }
        }
    }
}

TEST_F(GizmoLightTest, SpotOuterIsFullApertureAndRangeDoesNotChangeTheAngle)
{
    Light().LightType   = LightComponent::Type::Spot;
    const auto   before = Light().SpotAngle;
    auto         input  = Input(Handle::SpotOuterAngle);
    const double radius = 2.0 * std::tan(22.5 * std::numbers::pi / 180.0);
    const float  delta  = float(2.0 * std::tan(30.0 * std::numbers::pi / 180.0) - radius);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, delta), false));
    EXPECT_NEAR(Light().SpotAngle, 60.f, .0002f);
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Light().SpotAngle, before);
    Sync();
    input = Input(Handle::SpotRange);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, 1.f), false));
    EXPECT_NEAR(Light().Range, 3.f, .0002f);
    EXPECT_FLOAT_EQ(Light().SpotAngle, before);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
}

TEST_F(GizmoLightTest, InvalidSamplesKeepLastPreviewAndInvalidReleaseCommitsIt)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    const auto accepted = Light().Range;
    EXPECT_FALSE(session.UpdateLight(Motion(input, -3.f), true));
    EXPECT_FLOAT_EQ(Light().Range, accepted);
    EXPECT_FALSE(session.UpdateLight({std::numeric_limits<float>::quiet_NaN(), 0.f}, true));
    ASSERT_TRUE(session.UpdateLight({std::numeric_limits<float>::infinity(), 0.f}, false));
    EXPECT_FLOAT_EQ(Light().Range, accepted);
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoLightTest, NoOpReturnToPressAndAllCancellationPathsRestoreExactCheckpoint)
{
    for (uint32_t kind = 0; kind < 5; ++kind)
    {
        const auto input = Input();
        ASSERT_TRUE(session.BeginLight(input));
        ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
        if (kind == 0)
        {
            ASSERT_TRUE(session.UpdateLight(input.Pointer, false));
        }
        else if (kind == 1)
        {
            session.Cancel();
        }
        else
        {
            ASSERT_TRUE(session.UpdateLight({}, true, false, false, kind != 2, kind == 3, kind == 4));
        }
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(Light().Range, 2.f);
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
    const auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(input.Pointer, false));
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoLightTest, RangeSnapCtrlAndFineUseImmutablePointerMappingAndCapturedPreferences)
{
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .26f), true));
    EXPECT_NEAR(Light().Range, 2.3f, .0001f);
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapIncrement, .Target = SnapTarget::Translation, .Increment = .5f}));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.UpdateLight(Motion(input, .26f), true));
    EXPECT_NEAR(Light().Range, 2.3f, .0001f);
    ASSERT_TRUE(session.UpdateLight(Motion(input, .26f), true, false, true));
    EXPECT_NEAR(Light().Range, 2.26f, .0001f);
    ASSERT_TRUE(session.UpdateLight(Motion(input, 1.f), true, true));
    const auto fine = Light().Range;
    // Fine scales the screen delta before the captured perspective mapping,
    // not the already reconstructed world displacement.
    ASSERT_TRUE(session.UpdateLight(FineMotion(input, 1.f), true, false, true));
    EXPECT_FLOAT_EQ(Light().Range, fine);
    ASSERT_TRUE(session.UpdateLight(input.Pointer, false));
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoLightTest, OuterAngleUsesRotationSnapAndFineMotionBeforeTheNonlinearAngleMapping)
{
    Light().LightType = LightComponent::Type::Spot;
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto  input  = Input(Handle::SpotOuterAngle);
    LightHandle handle = {};
    ASSERT_TRUE(ResolveLightHandle(input.Expected, input.Constraint, handle));
    const float delta = float(2.0 * std::tan(28.0 * std::numbers::pi / 180.0) - handle.Parameter);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, delta), true));
    EXPECT_NEAR(Light().SpotAngle, 60.f, .001f);
    ASSERT_TRUE(session.UpdateLight(Motion(input, delta), true, false, true));
    EXPECT_NEAR(Light().SpotAngle, 56.f, .001f);
    ASSERT_TRUE(session.UpdateLight(Motion(input, delta), true, true));
    const auto fine = Light().SpotAngle;
    ASSERT_TRUE(session.UpdateLight(FineMotion(input, delta), true, false, true));
    EXPECT_FLOAT_EQ(Light().SpotAngle, fine);
    session.Cancel();
    EXPECT_FLOAT_EQ(Light().SpotAngle, 45.f);
}

TEST_F(GizmoLightTest, RangeAndConeAreWorldSizedUnderNonUniformReflectedParentAndIgnoreCustomPivot)
{
    const auto parent = Entity(2);
    auto*      pose   = scene.GetComponent<TransformComponent>(parent);
    pose->Position    = {1.f, 2.f, 3.f};
    pose->Rotation    = {.2f, .4f, -.7f};
    pose->Scale       = {-2.f, 3.f, .5f};
    ASSERT_TRUE(scene.SetParent(entity, parent));
    Light().LightType = LightComponent::Type::Spot;
    auto frame        = Frame();
    EXPECT_TRUE(ValidLightGeometry(frame));
    EXPECT_EQ(frame.Origin, pose->Position);
    LightHandle range = {}, angle = {};
    ASSERT_TRUE(ResolveLightHandle(frame, Handle::SpotRange, range));
    ASSERT_TRUE(ResolveLightHandle(frame, Handle::SpotOuterAngle, angle));
    EXPECT_NEAR(std::hypot(double(range.Position.x - frame.Origin.x), double(range.Position.y - frame.Origin.y), double(range.Position.z - frame.Origin.z)), 2.0, .00001);
    const auto radius = angle.Position - range.Position;
    EXPECT_NEAR(std::hypot(double(radius.x), double(radius.y), double(radius.z)), 2.0 * std::tan(std::numbers::pi / 8.0), .00001);
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
    ASSERT_TRUE(session.ProcessRequests());
    PivotBegin pivot = {.Projection = projection, .Bounds = bounds, .Key = 77, .PointerSerial = 44};
    ASSERT_TRUE(ResolveTargetFrame(session, pivot.Expected));
    pivot.Pointer = Screen(pivot.Expected.Pivot);
    ASSERT_TRUE(session.BeginPivot(pivot));
    ASSERT_TRUE(session.UpdatePivot(Screen(frame.Origin + Vec3f(1.f, 1.f, 0.f)), false));
    Vec3f custom = {};
    ASSERT_TRUE(session.ReadCustomPivot(custom));
    EXPECT_TRUE(SameLightFrame(frame, Frame()));
    const auto input = Input(Handle::SpotRange);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), false));
    EXPECT_NEAR(Light().Range, 2.5f, .0001f);
    Vec3f after = {};
    ASSERT_TRUE(session.ReadCustomPivot(after));
    EXPECT_EQ(after, custom);
}

TEST_F(GizmoLightTest, OnlyActiveSelectedLightIsEditedWithoutActorFacadeOrTransformMode)
{
    const auto actor_handle = actors.Create();
    auto*      actor        = actors.Access(actor_handle);
    actor->AddComponent<UUIDComponent>({UUID(2)});
    actor->AddComponent<TransformComponent>({});
    actor->AddComponent<LightComponent>({.LightType = LightComponent::Type::Point, .Range = 3.f});
    const auto other = actor->GetEntityID();
    ASSERT_TRUE(session.Select(other, SelectionMode::Add));
    auto input = Input();
    EXPECT_EQ(input.Expected.Target.CachedEntity, other);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), false));
    EXPECT_NEAR(scene.GetComponent<LightComponent>(other)->Range, 3.5f, .0001f);
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(other)->Range, 3.f);
    ASSERT_TRUE(session.Select(entity));
    input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), false));
    EXPECT_NEAR(Light().Range, 2.5f, .0001f);
}

TEST_F(GizmoLightTest, ExternalFieldEditIsNeverOverwrittenByCancellation)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    Light().Range = 7.f;
    EXPECT_FALSE(session.UpdateLight(Motion(input, 1.f), true));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, 7.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(GizmoLightTest, PointRangePreviewCancelAndHistoryFeedTheExistingRendererSynchronization)
{
    ZEngine::Rendering::Scenes::RenderScene render = {};
    const auto                              input  = Input();
    const auto                              radius = [&] {
        Systems::SyncECSToLights(scene, render);
        EXPECT_EQ(render.PendingLights.PointCount, 1u);
        return render.PendingLights.PointLights[0].Radius;
    };
    EXPECT_FLOAT_EQ(radius(), 2.f);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    EXPECT_NEAR(radius(), 2.5f, .0001f);
    session.Cancel();
    EXPECT_FLOAT_EQ(radius(), 2.f);
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), false));
    EXPECT_NEAR(radius(), 2.5f, .0001f);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(radius(), 2.f);
    ASSERT_TRUE(session.Redo());
    EXPECT_NEAR(radius(), 2.5f, .0001f);
}

TEST_F(GizmoLightTest, UnrelatedLightFieldsArePreservedOnCancelCommitUndoAndRedo)
{
    auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    Light().Intensity = 7.f;
    Light().Color[1]  = .25f;
    session.Cancel();
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    EXPECT_FLOAT_EQ(Light().Intensity, 7.f);
    EXPECT_FLOAT_EQ(Light().Color[1], .25f);
    input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), false));
    ASSERT_TRUE(session.Undo());
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(Light().Intensity, 7.f);
    EXPECT_FLOAT_EQ(Light().Color[1], .25f);
}

TEST_F(GizmoLightTest, ChangesToCapturedTypeAnglePoseOrParentCancelWithoutUndoingExternalData)
{
    for (uint32_t kind = 0; kind < 4; ++kind)
    {
        const auto parent = Entity(uint8_t(kind + 2));
        ASSERT_TRUE(scene.SetParent(entity, parent));
        Light().LightType = LightComponent::Type::Spot;
        Light().SpotAngle = 45.f;
        const auto input  = Input(Handle::SpotRange);
        ASSERT_TRUE(session.BeginLight(input));
        ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
        if (kind == 0)
        {
            Light().LightType = LightComponent::Type::Point;
        }
        if (kind == 1)
        {
            Light().SpotAngle = 60.f;
        }
        if (kind == 2)
        {
            scene.GetComponent<TransformComponent>(entity)->Position.y += 1.f;
        }
        if (kind == 3)
        {
            scene.GetComponent<TransformComponent>(parent)->Rotation.y = .7f;
        }
        Sync();
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(Light().Range, 2.f);
        if (kind == 0)
        {
            EXPECT_EQ(Light().LightType, LightComponent::Type::Point);
        }
        if (kind == 1)
        {
            EXPECT_FLOAT_EQ(Light().SpotAngle, 60.f);
        }
        if (kind == 2)
        {
            EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.y, 1.f);
        }
        if (kind == 3)
        {
            EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(parent)->Rotation.y, .7f);
        }
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
}

TEST_F(GizmoLightTest, StaleExpectedFramesWrongHandlesInvalidIdentityAndWrongThreadFailWithoutWrites)
{
    auto input    = Input();
    Light().Range = 3.f;
    EXPECT_FALSE(session.BeginLight(input));
    input            = Input();
    input.Constraint = Handle::SpotRange;
    EXPECT_FALSE(session.BeginLight(input));
    input     = Input();
    input.Key = 0;
    EXPECT_FALSE(session.BeginLight(input));
    input               = Input();
    input.PointerSerial = 0;
    EXPECT_FALSE(session.BeginLight(input));
    input             = Input();
    bool        began = true;
    std::thread worker([&] { began = session.BeginLight(input); });
    worker.join();
    EXPECT_FALSE(began);
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, 3.f);
    session.EndMutationPhase();
    EXPECT_FALSE(session.BeginLight(input));
    ASSERT_TRUE(session.BeginMutationPhase());
    scene.DestroyEntity(entity);
    entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 9.f});
    EXPECT_FALSE(session.BeginLight(input));
    EXPECT_FLOAT_EQ(Light().Range, 9.f);
}

TEST_F(GizmoLightTest, ComponentRemovalAndSameUUIDReplacementNeverReceiveOldRollback)
{
    auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    scene.RemoveComponent<LightComponent>(entity);
    Sync();
    EXPECT_FALSE(session.Active());
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 2.f});
    input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    scene.DestroyEntity(entity);
    entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 9.f});
    Sync();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, 9.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoLightTest, SameEntityComponentReplacementCannotReceiveTheRemovedLightsRollback)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    const auto replacement = Light(); // Deliberately identical to the owned preview.
    ASSERT_NE(replacement.Range, 2.f);
    scene.RemoveComponent<LightComponent>(entity);
    scene.AddComponent<LightComponent>(entity, replacement);
    Sync();
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, replacement.Range);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(GizmoLightTest, PointWireframeHasThreeDashedWorldRingsAndLogicalPixelHandles)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t height : {600u, 1200u})
            {
                Projection(ortho, reverse);
                auto           input  = Geometry(height);
                GizmoFrameData output = {};
                HitFrame       hits   = {};
                ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                ASSERT_EQ(output.LineCount, 3 * C::RingSegments + 8);
                EXPECT_EQ(output.SpriteCount, 6u);
                EXPECT_EQ(hits.Count, 5u);
                EXPECT_FALSE(output.DecorationsOmitted);
                const float scale = float(height) / bounds.Size.y;
                for (uint32_t i = 0; i < 3 * C::RingSegments; ++i)
                {
                    const auto& line = output.Lines[i];
                    EXPECT_NEAR(std::hypot(double(line.A.x), double(line.A.y), double(line.A.z)), 2.0, .00001);
                    EXPECT_NEAR(std::hypot(double(line.B.x), double(line.B.y), double(line.B.z)), 2.0, .00001);
                    EXPECT_EQ(line.Width, 1.5f * scale);
                    EXPECT_EQ(line.DashOn, 4.f * scale);
                    EXPECT_EQ(line.DashOff, 3.f * scale);
                    EXPECT_EQ(line.Color, UI::ZUIPackColor(UI::Tokens::PinVector.R, UI::Tokens::PinVector.G, UI::Tokens::PinVector.B, 1.f));
                }
                for (const auto& shape : hits.View())
                {
                    EXPECT_EQ(shape.Priority, HitPriority::Component);
                    EXPECT_EQ(shape.Points[1].x - shape.Points[0].x, 9.f);
                    EXPECT_EQ(hits.Pick((shape.Points[0] + shape.Points[1]) * .5f).Handle, shape.Handle);
                }
                EXPECT_EQ(output.Sprites[0].Size, 9.f * scale);
                EXPECT_EQ(output.Sprites[5].Size, 14.f * scale);
            }
        }
    }
}

TEST_F(GizmoLightTest, PointDashPhaseUsesFullProjectedPolylineRatherThanRestartingEachChord)
{
    auto           input  = Geometry();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    for (uint32_t ring = 0; ring < 3; ++ring)
    {
        float phase = 0.f;
        for (uint32_t i = 0; i < C::RingSegments; ++i)
        {
            const auto& line = output.Lines[ring * C::RingSegments + i];
            EXPECT_NEAR(line.DashPhase, phase, .001f);
            const auto a = Screen(line.A), b = Screen(line.B);
            phase = float(std::fmod(double(phase) + std::hypot(double(b.x) - a.x, double(b.y) - a.y), 7.0));
        }
    }
}

TEST_F(GizmoLightTest, DashedRingsKeepTheirPhaseAcrossNearClippedAndCulledChords)
{
    for (bool reverse : {false, true})
    {
        Projection(false, reverse, false, 1.f);
        Light().Range         = 9.5f;
        const auto     input  = Geometry();
        GizmoFrameData output = {};
        HitFrame       hits   = {};
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        EXPECT_FALSE(output.DecorationsOmitted);
        uint32_t emitted = 0;
        for (uint32_t ring = 0; ring < 3; ++ring)
        {
            float      phase = 0.f;
            const auto point = [&](uint32_t index) {
                const double angle = 2.0 * std::numbers::pi * (index % C::RingSegments) / C::RingSegments;
                const float  x = 9.5f * float(std::cos(angle)), y = 9.5f * float(std::sin(angle));
                return ring == 0 ? Vec3f(x, y, 0.f) : ring == 1 ? Vec3f(x, 0.f, y) : Vec3f(0.f, x, y);
            };
            for (uint32_t i = 0; i < C::RingSegments; ++i)
            {
                const auto a = point(i), b = point(i + 1);
                const auto screen_a = Screen(a), screen_b = Screen(b);
                if (a.z <= 9.f || b.z <= 9.f)
                {
                    ASSERT_LT(emitted, output.LineCount - 8);
                    const auto&  line    = output.Lines[emitted++];
                    const auto   clipped = Screen(line.A);
                    const double lost    = a.z > 9.f ? std::hypot(double(clipped.x) - screen_a.x, double(clipped.y) - screen_a.y) : 0.0;
                    EXPECT_NEAR(std::remainder(double(line.DashPhase) - phase - lost, 7.0), 0.0, .02);
                    EXPECT_LE(line.A.z, 9.001f);
                    EXPECT_LE(line.B.z, 9.001f);
                }
                phase = float(std::fmod(double(phase) + std::hypot(double(screen_b.x) - screen_a.x, double(screen_b.y) - screen_a.y), 7.0));
            }
        }
        EXPECT_EQ(emitted + 8, output.LineCount);
        EXPECT_LT(emitted, 3 * C::RingSegments);
    }
}

TEST_F(GizmoLightTest, OuterConeOnlyAndDirectionalDecorationHaveNoInventedFieldHandles)
{
    Light().LightType     = LightComponent::Type::Spot;
    auto           input  = Geometry();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, C::RingSegments + 4 + 1 + 8);
    EXPECT_EQ(output.SpriteCount, 3u);
    EXPECT_EQ(output.Sprites[2].Size, 12.f);
    ASSERT_EQ(hits.Count, 2u);
    EXPECT_EQ(hits.Shapes[0].Handle, uint32_t(Handle::SpotRange));
    EXPECT_EQ(hits.Shapes[1].Handle, uint32_t(Handle::SpotOuterAngle));
    const double radius = 2.0 * std::tan(std::numbers::pi / 8.0);
    for (uint32_t i = 0; i < C::RingSegments; ++i)
    {
        EXPECT_EQ(output.Lines[i].A.z, 2.f);
        EXPECT_NEAR(std::hypot(double(output.Lines[i].A.x), double(output.Lines[i].A.y)), radius, .00001);
        EXPECT_EQ(output.Lines[i].DashOn, 0.f);
    }
    Light().LightType = LightComponent::Type::Directional;
    input             = Geometry();
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, 11u);
    EXPECT_EQ(output.SpriteCount, 1u);
    EXPECT_EQ(output.Sprites[0].Size, 18.f);
    EXPECT_EQ(hits.Count, 0u);
    EXPECT_EQ(output.Lines[0].B.z, 2.f);
    EXPECT_EQ(output.Lines[0].Width, 2.5f);
    EXPECT_EQ(output.Lines[1].DashOn, 4.f);
    EXPECT_EQ(output.Lines[1].Color >> 24, uint32_t(.55f * 255.f + .5f));
    LightHandle unchanged = {.Parameter = 123.f};
    EXPECT_FALSE(ResolveLightHandle(input.Light, Handle::SpotRange, unchanged));
    EXPECT_EQ(unchanged.Parameter, 123.f);
}

TEST_F(GizmoLightTest, TransformHitPriorityAndNearHiddenCustomPivotAreIndependentOfActualLightOrigin)
{
    Projection(false, false, false);
    auto           input   = Geometry(600, Operation::Translate);
    const auto     handles = Geometry();
    GizmoFrameData base = {}, output = {};
    HitFrame       base_hits = {}, hits = {};
    ASSERT_EQ(BuildFrame(input, base, base_hits), FrameBuildResult::Built);
    input.Light.Light.Range = 1.f;
    input.Pivot             = {1.f, 0.f, 0.f};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(hits.Pick(Screen(input.Pivot)).Handle, uint32_t(Handle::TranslateView));
    input.Pivot = camera.Position;
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, 3 * C::RingSegments + 8);
    for (const auto& shape : hits.View())
    {
        EXPECT_TRUE(IsLightHandle(Handle(shape.Handle)));
    }
    input              = handles;
    input.Light.Origin = camera.Position;
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(hits.Count, 0u);
}

TEST_F(GizmoLightTest, MalformedOptionalGeometryRollsBackOnlyComponentsAndNeverLeavesInvisibleHits)
{
    auto input             = Geometry(600, Operation::Translate);
    input.Light.Light.Type = LightComponent::Type::Spot;
    input.Light.Forward    = {};
    GizmoFrameData output  = {};
    HitFrame       hits    = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    for (const auto& shape : hits.View())
    {
        EXPECT_FALSE(IsLightHandle(Handle(shape.Handle)));
    }
    input.Light          = Frame();
    input.Light.Origin.x = std::numeric_limits<float>::quiet_NaN();
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    input.Mode = Operation::Select;
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(hits.Count, 0u);
}

TEST_F(GizmoLightTest, ComponentCapacityFailurePreservesExistingPrefixes)
{
    const auto     input  = Geometry();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    output.LineCount      = GizmoFrameData::MaxLines - 1;
    output.Lines[0].Color = 123u;
    hits.Count            = 1;
    hits.Shapes[0].Handle = 456u;
    BuildLightGeometry(input, projection, output, hits);
    EXPECT_EQ(output.LineCount, GizmoFrameData::MaxLines - 1);
    EXPECT_EQ(output.Lines[0].Color, 123u);
    EXPECT_EQ(hits.Count, 1u);
    EXPECT_EQ(hits.Shapes[0].Handle, 456u);
    EXPECT_TRUE(output.DecorationsOmitted);
}

TEST_F(GizmoLightTest, ViewParallelHandlesAreVisibleButDoNotClaimAnUnsafeMapping)
{
    Projection(false, false, false);
    const auto     input  = Geometry();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.SpriteCount, 6u);
    ASSERT_EQ(hits.Count, 3u);
    for (const auto& shape : hits.View())
    {
        EXPECT_NE(shape.Handle, uint32_t(Handle::LightRangePositiveZ));
        EXPECT_NE(shape.Handle, uint32_t(Handle::LightRangeNegativeZ));
    }
    auto begin = Input(Handle::LightRangePositiveZ);
    EXPECT_FALSE(session.BeginLight(begin));
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoLightTest, SelectionModeChangesAndReadOnlyExitCancelBeforeReleasingFieldOwnership)
{
    const auto other = Entity(2);
    session.InvalidateForExternalMutation();
    ASSERT_TRUE(session.MarkSaved());
    auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    ASSERT_TRUE(session.Select(other));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    ASSERT_TRUE(session.Select(entity));
    input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Light().Range, 2.f);
    EXPECT_FALSE(session.BeginLight(input));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoLightTest, RepeatedTransactionsReuseCapturedFieldAndHistoryStorage)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginLight(input));
    session.Cancel();
    const auto bytes = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.BeginLight(input));
        ASSERT_TRUE(session.UpdateLight(Motion(input, .5f), true));
        session.Cancel();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}
