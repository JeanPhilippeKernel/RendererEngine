#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
namespace G = ZEngine::Core::Maths::Geometry;

class GizmoScaleTest : public testing::Test
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
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Scale}));
        ASSERT_TRUE(session.ProcessRequests());
        using namespace ZEngine::Rendering::Cameras;
        FlyCamera camera(4.f / 3.f, {});
        camera.SetPosition({7.f, 5.f, 10.f});
        camera.SetOrientation(25.f, -35.f);
        const auto frame = camera.CaptureFrameData();
        ASSERT_TRUE(G::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, G::ProjectionKind::Perspective, false, &projection));
    }
    void TearDown() override
    {
        session.Shutdown();
        actors.Shutdown();
        scene.Shutdown();
        memory.Shutdown();
    }
    Vec2f Screen(const Vec3f& point)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(point, projection, bounds, &result));
        return result.Screen;
    }
    ScaleBegin Input(Handle handle = Handle::ScaleUniform)
    {
        scene.SynchronizeHierarchy();
        ScaleBegin input = {.Projection = projection, .Bounds = bounds, .Key = 123, .PointerSerial = 456, .Constraint = handle};
        EXPECT_TRUE(ResolveTargetFrame(session, input.Expected));
        input.Pointer = Screen(input.Expected.Pivot);
        if (handle >= Handle::ScaleX && handle <= Handle::ScaleZ)
        {
            const auto   axis   = input.Expected.Axes[uint32_t(handle) - uint32_t(Handle::ScaleX)];
            const double length = std::hypot(double(axis.x), double(axis.y), double(axis.z));
            if (length > 0.0)
            {
                input.Pointer = Screen(input.Expected.Pivot + axis * float(2.0 / length));
            }
        }
        return input;
    }
    Vec2f Pointer(const ScaleBegin& input, double ratio)
    {
        if (input.Constraint == Handle::ScaleUniform)
        {
            return input.Pointer + Vec2f(float(std::log(ratio) * std::numbers::sqrt2 * 120.0), 0.f);
        }
        const auto   axis   = input.Expected.Axes[uint32_t(input.Constraint) - uint32_t(Handle::ScaleX)];
        const double length = std::hypot(double(axis.x), double(axis.y), double(axis.z));
        return Screen(input.Expected.Pivot + axis * float(2.0 * ratio / length));
    }
    LocalTransformValue Value()
    {
        return CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    }
    void Sync()
    {
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
    }
    static void Exact(const Vec3f& actual, const Vec3f& expected)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_EQ(std::bit_cast<uint32_t>(actual[axis]), std::bit_cast<uint32_t>(expected[axis]));
        }
    }
};

TEST_F(GizmoScaleTest, EveryHandleUsesOneSharedBatchAndPreservesAuthoredSignsWithExactUndoRedo)
{
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.3f, -.4f, .5f};
    transform->Scale    = {-2.f, 3.f, .5f};
    for (const auto handle : {Handle::ScaleX, Handle::ScaleY, Handle::ScaleZ, Handle::ScaleUniform})
    {
        const auto before = Value();
        const auto input  = Input(handle);
        ASSERT_TRUE(session.BeginScale(input));
        EXPECT_TRUE(session.ScaleActive());
        EXPECT_FALSE(session.TranslationActive());
        EXPECT_FALSE(session.RotationActive());
        EXPECT_EQ(session.TransformCount(), 1u);
        ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.3), true));
        EXPECT_EQ(session.History().UndoCount(), 0u);
        ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), false));
        EXPECT_FALSE(session.ScaleActive());
        EXPECT_EQ(session.ScaleKey(), 0u);
        EXPECT_EQ(session.ScalePointerSerial(), 0u);
        EXPECT_EQ(session.History().UndoCount(), 1u);
        const auto after = Value();
        Exact(after.Position, before.Position);
        Exact(after.Rotation, before.Rotation);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(after.Scale[axis], before.Scale[axis] * (handle == Handle::ScaleUniform || uint32_t(handle) - uint32_t(Handle::ScaleX) == axis ? 1.5f : 1.f), .0001f);
        }
        ASSERT_TRUE(session.Undo());
        EXPECT_TRUE(SameLocalTransform(Value(), before));
        ASSERT_TRUE(session.Redo());
        EXPECT_TRUE(SameLocalTransform(Value(), after));
        ASSERT_TRUE(session.Undo());
    }
}

TEST_F(GizmoScaleTest, ActorBackedAndPureLightCameraAndEmptyTargetsShareTheAdapter)
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
        const auto input  = Input();
        ASSERT_TRUE(session.BeginScale(input));
        ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), false));
        EXPECT_NEAR(Value().Scale.x, 1.5f, .0001f);
        ASSERT_TRUE(session.Undo());
        EXPECT_TRUE(SameLocalTransform(Value(), before));
    }
}

TEST_F(GizmoScaleTest, ScaleAlwaysUsesLocalAxesUnderRotatedReflectedNonuniformParents)
{
    const auto parent   = Entity(2);
    auto*      ancestor = scene.GetComponent<TransformComponent>(parent);
    ancestor->Position  = {-1.f, 2.f, 1.f};
    ancestor->Rotation  = {.2f, -.3f, .1f};
    ancestor->Scale     = {-2.f, 3.f, 4.f};
    auto* transform     = scene.GetComponent<TransformComponent>(entity);
    transform->Rotation = {.4f, .1f, -.2f};
    transform->Scale    = {-2.f, .5f, 3.f};
    scene.AddComponent<ParentComponent>(entity, {parent});
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::World);
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::Local);
    for (const auto handle : {Handle::ScaleX, Handle::ScaleY, Handle::ScaleZ, Handle::ScaleUniform})
    {
        const auto before = Value();
        const auto input  = Input(handle);
        ASSERT_TRUE(session.BeginScale(input));
        ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), true));
        Exact(Value().Position, before.Position);
        Exact(Value().Rotation, before.Rotation);
        session.EndMutationPhase();
        ScaleSnapshot snapshot = {};
        ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
        EXPECT_NEAR(snapshot.Ratio, 1.5f, .0001f);
        for (uint32_t column = 0; column < 3; ++column)
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                EXPECT_NEAR(snapshot.World(row, column), snapshot.StartWorld(row, column) * (handle == Handle::ScaleUniform || uint32_t(handle) - uint32_t(Handle::ScaleX) == column ? 1.5f : 1.f), .001f);
            }
        }
        ASSERT_TRUE(session.BeginMutationPhase());
        session.Cancel();
        EXPECT_TRUE(SameLocalTransform(Value(), before));
    }
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::World);
}

TEST_F(GizmoScaleTest, NearZeroAxesRejectButUniformPreservesZeroAndNegativeZeroComponents)
{
    auto* transform  = scene.GetComponent<TransformComponent>(entity);
    transform->Scale = {-0.f, -1.e-5f, .0001f};
    EXPECT_FALSE(session.BeginScale(Input(Handle::ScaleX)));
    EXPECT_FALSE(session.BeginScale(Input(Handle::ScaleY)));
    const auto axis = Input(Handle::ScaleZ);
    ASSERT_TRUE(session.BeginScale(axis));
    ASSERT_TRUE(session.UpdateScale(Pointer(axis, 1.5), true));
    session.Cancel();
    const auto before  = Value();
    const auto uniform = Input();
    ASSERT_TRUE(session.BeginScale(uniform));
    ASSERT_TRUE(session.UpdateScale(Pointer(uniform, 1.5), false));
    EXPECT_TRUE(std::signbit(Value().Scale.x));
    EXPECT_FLOAT_EQ(Value().Scale.x, 0.f);
    EXPECT_NEAR(Value().Scale.y, -1.5e-5f, 1.e-9f);
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(), before));
}

TEST_F(GizmoScaleTest, SnapInversionFineControlAndSnapshotUseOnlyTheLastAcceptedRatio)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.37), true, false, true));
    session.EndMutationPhase();
    ScaleSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
    EXPECT_FLOAT_EQ(snapshot.Ratio, 1.25f);
    EXPECT_EQ(snapshot.Key, 123u);
    EXPECT_EQ(snapshot.PointerSerial, 456u);
    EXPECT_EQ(snapshot.Target.UUID, UUID(1));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.37), true, true, true));
    session.EndMutationPhase();
    ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
    EXPECT_NEAR(snapshot.Ratio, std::pow(1.37, .1), .00001);
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
}

TEST_F(GizmoScaleTest, ClickReturnToPressAndUnchangedZeroScaleAddNoHistoryOrDirtyState)
{
    const auto before = Value();
    const auto input  = Input();
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(input.Pointer, false, false, true));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), true));
    ASSERT_TRUE(session.UpdateScale(input.Pointer, false));
    EXPECT_TRUE(SameLocalTransform(Value(), before));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    scene.GetComponent<TransformComponent>(entity)->Scale = {-0.f, 0.f, 0.f};
    const auto zero                                       = Value();
    const auto zero_input                                 = Input();
    ASSERT_TRUE(session.BeginScale(zero_input));
    ASSERT_TRUE(session.UpdateScale(Pointer(zero_input, 1.5), false));
    EXPECT_TRUE(SameLocalTransform(Value(), zero));
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoScaleTest, RejectedOverflowPreservesPoseAndPresentationAndInvalidReleaseCommitsLastPreview)
{
    scene.GetComponent<TransformComponent>(entity)->Scale = {2.f, 3.f, 4.f};
    const auto input                                      = Input();
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), true));
    const auto valid = Value();
    EXPECT_FALSE(session.UpdateScale(Pointer(input, 1.e38), true));
    EXPECT_TRUE(SameLocalTransform(Value(), valid));
    session.EndMutationPhase();
    ScaleSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
    EXPECT_NEAR(snapshot.Ratio, 1.5f, .00001f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.UpdateScale({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
    EXPECT_TRUE(SameLocalTransform(Value(), valid));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoScaleTest, FocusEscapeCaptureSelectionModeSceneAndParentChangesCancelExactly)
{
    const auto other  = Entity(2);
    const auto before = Value();
    for (uint32_t transition = 0; transition < 7; ++transition)
    {
        ASSERT_TRUE(session.Select(entity));
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Scale}));
        ASSERT_TRUE(session.ProcessRequests());
        const auto input = Input();
        ASSERT_TRUE(session.BeginScale(input));
        ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), true));
        switch (transition)
        {
            case 0:
                EXPECT_TRUE(session.UpdateScale({}, true, false, false, false));
                break;
            case 1:
                EXPECT_TRUE(session.UpdateScale({}, true, false, false, true, true));
                break;
            case 2:
                EXPECT_TRUE(session.UpdateScale({}, true, false, false, true, false, true));
                break;
            case 3:
                EXPECT_TRUE(session.Select(other));
                break;
            case 4:
                EXPECT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
                EXPECT_TRUE(session.ProcessRequests());
                break;
            case 5:
                EXPECT_TRUE(session.BindScene(&scene));
                break;
            case 6:
                EXPECT_TRUE(session.Reparent(entity, other));
                break;
        }
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(session.ScaleActive());
        EXPECT_TRUE(SameLocalTransform(Value(), before));
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
}

TEST_F(GizmoScaleTest, StaleMalformedForeignThreadAndOutOfPhaseCallsCannotMutate)
{
    const auto original = Input();
    for (uint32_t failure = 0; failure < 8; ++failure)
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
                input.Constraint = Handle::RotateX;
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
            case 7:
                input.Projection.Valid = false;
                break;
        }
        EXPECT_FALSE(session.BeginScale(input)) << failure;
        EXPECT_FALSE(session.Active());
    }
    bool        rejected = false;
    std::thread begin([&] { rejected = !session.BeginScale(original); });
    begin.join();
    EXPECT_TRUE(rejected);
    ASSERT_TRUE(session.BeginScale(original));
    std::thread update([&] { rejected = !session.UpdateScale(Pointer(original, 1.5), false); });
    update.join();
    EXPECT_TRUE(rejected);
    EXPECT_TRUE(session.Active());
    session.EndMutationPhase();
    EXPECT_FALSE(session.UpdateScale(Pointer(original, 1.5), false));
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
}

TEST_F(GizmoScaleTest, SnapshotRequiresSynchronizedOwnerAndPreservesCopiedLifetime)
{
    const auto input = Input();
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(Pointer(input, 1.5), true));
    ScaleSnapshot snapshot = {.Key = 77};
    EXPECT_FALSE(session.ReadScaleSnapshot(snapshot));
    EXPECT_EQ(snapshot.Key, 77u);
    session.EndMutationPhase();
    ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
    const auto  copied   = snapshot;
    bool        rejected = false;
    std::thread reader([&] { rejected = !session.ReadScaleSnapshot(snapshot); });
    reader.join();
    EXPECT_TRUE(rejected);
    EXPECT_EQ(snapshot.Key, copied.Key);
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    EXPECT_FALSE(session.ReadScaleSnapshot(snapshot));
    EXPECT_FLOAT_EQ(snapshot.Ratio, copied.Ratio);
    EXPECT_EQ(snapshot.Key, copied.Key);
}

TEST_F(GizmoScaleTest, RepeatedPreviewCancelReusesSessionStorageAndImmutableBeforePose)
{
    const auto input   = Input();
    const auto pointer = Pointer(input, 1.5);
    const auto before  = Value();
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(pointer, true));
    session.Cancel();
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t iteration = 0; iteration < 1000; ++iteration)
    {
        ASSERT_TRUE(session.BeginScale(Input()));
        for (uint32_t preview = 0; preview < 4; ++preview)
        {
            ASSERT_TRUE(session.UpdateScale(pointer, true));
        }
        session.Cancel();
        EXPECT_TRUE(SameLocalTransform(Value(), before));
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}
