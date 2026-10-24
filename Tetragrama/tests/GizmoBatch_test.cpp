#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <thread>

using namespace Tetragrama;
using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using ZEngine::Core::Containers::FixedArray;
namespace G = ZEngine::Core::Maths::Geometry;

class GizmoBatchTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    Scene                                scene      = {};
    ActorManager                         actors     = {};
    EditorSession                        session    = {};
    G::ProjectionFrame                   projection = {};
    G::Viewport                          bounds     = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    static uuids::uuid UUID(uint32_t value)
    {
        FixedArray<uint8_t, 16> bytes = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            bytes[15 - i] = uint8_t(value >> (i * 8));
        }
        return uuids::uuid(bytes.begin(), bytes.end());
    }
    EntityID Entity(uint32_t identity, const Vec3f& position = {}, bool transform = true)
    {
        const auto entity = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(entity, {UUID(identity)});
        if (transform)
        {
            scene.AddComponent<TransformComponent>(entity, {.Position = position});
        }
        return entity;
    }
    void SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, scene);
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 4096, .HistoryEntries = 8, .HistoryBytes = ZMega(1)}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
        using namespace ZEngine::Rendering::Cameras;
        FlyCamera camera(4.f / 3.f, {});
        camera.SetPosition({0.f, 0.f, 10.f});
        camera.SetOrientation(0.f, 0.f);
        const auto frame = camera.CaptureFrameData();
        ASSERT_TRUE(G::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, G::ProjectionKind::Perspective, false, &projection));
        for (const auto snap : {SnapTarget::Translation, SnapTarget::Rotation, SnapTarget::Scale})
        {
            ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = snap, .Enabled = false}));
        }
        ASSERT_TRUE(session.ProcessRequests());
    }
    void TearDown() override
    {
        session.Shutdown();
        actors.Shutdown();
        scene.Shutdown();
    }
    void Choose(std::initializer_list<EntityID> ids, EntityID active = {})
    {
        FixedArray<EntityTarget, 16> targets = {};
        ASSERT_LE(ids.size(), targets.size());
        uint32_t count = 0;
        for (const auto id : ids)
        {
            targets[count++] = session.TargetFor(id);
        }
        ASSERT_TRUE(session.SetSelection({targets.data(), count}, active.IsValid() ? session.TargetFor(active).UUID : uuids::uuid{}));
        scene.SynchronizeHierarchy();
    }
    void Mode(Operation mode)
    {
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
        ASSERT_TRUE(session.ProcessRequests());
    }
    TargetFrame Frame()
    {
        scene.SynchronizeHierarchy();
        TargetFrame frame = {};
        EXPECT_TRUE(ResolveTargetFrame(session, frame));
        return frame;
    }
    Vec2f Screen(const Vec3f& position)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(position, projection, bounds, &result));
        return result.Screen;
    }
    TranslationBegin Translation()
    {
        Mode(Operation::Translate);
        const auto frame = Frame();
        return {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(frame.Pivot + Vec3f(.7f, 0.f, 0.f)), .Key = 42, .PointerSerial = 101, .Constraint = Handle::TranslateX};
    }
    Vec2f TranslatePointer(const TranslationBegin& input, float delta)
    {
        return Screen(input.Expected.Pivot + Vec3f(.7f + delta, 0.f, 0.f));
    }
    RotationBegin Rotation()
    {
        Mode(Operation::Rotate);
        const auto frame = Frame();
        const auto point = frame.Pivot + Vec3f(1.f, 0.f, 0.f);
        return {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(point), .RingPoint = point, .Key = 43, .PointerSerial = 102, .Constraint = Handle::RotateZ};
    }
    Vec2f RotationPointer(const RotationBegin& input, double angle)
    {
        return Screen(input.Expected.Pivot + Vec3f(float(std::cos(angle)), float(std::sin(angle)), 0.f));
    }
    ScaleBegin Scale(Handle handle = Handle::ScaleUniform)
    {
        Mode(Operation::Scale);
        const auto frame = Frame();
        Vec3f      grab  = frame.Pivot;
        if (handle != Handle::ScaleUniform)
        {
            auto axis = frame.Axes[uint32_t(handle) - uint32_t(Handle::ScaleX)];
            axis      = axis / float(std::hypot(double(axis.x), double(axis.y), double(axis.z)));
            grab      = grab + axis * 2.f;
        }
        return {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(grab), .Key = 44, .PointerSerial = 103, .Constraint = handle};
    }
    Vec2f ScalePointer(const ScaleBegin& input, double ratio)
    {
        if (input.Constraint == Handle::ScaleUniform)
        {
            return input.Pointer + Vec2f(float(std::log(ratio) * std::numbers::sqrt2 * Constants::UniformScalePixelsPerEfold), 0.f);
        }
        const auto axis = input.Expected.Axes[uint32_t(input.Constraint) - uint32_t(Handle::ScaleX)];
        return Screen(input.Expected.Pivot + axis * float(2.0 * ratio / std::hypot(double(axis.x), double(axis.y), double(axis.z))));
    }
    LocalTransformValue Value(EntityID id)
    {
        return CaptureLocalTransform(*scene.GetComponent<TransformComponent>(id));
    }
    Vec3f WorldPosition(EntityID id)
    {
        const auto& world = scene.GetComponent<TransformComponent>(id)->WorldTransform;
        return {world(0, 3), world(1, 3), world(2, 3)};
    }
    void Sync()
    {
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
    }
    static void Near(const Vec3f& actual, const Vec3f& expected, float tolerance = .0003f)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(actual[axis], expected[axis], tolerance) << axis;
        }
    }
};

TEST_F(GizmoBatchTest, TranslationUsesOneSnappedDeltaForMixedKindsAndOneExactHistoryBatch)
{
    const auto first = Entity(1, {-.27f, 0.f, 0.f}), light = Entity(2, {.34f, 0.f, 0.f}), camera = Entity(3, {1.83f, 0.f, 0.f});
    scene.AddComponent<LightComponent>(light, {});
    scene.AddComponent<CameraComponent>(camera, {});
    const auto actor = actors.Create();
    ASSERT_TRUE(actor.Valid());
    const auto actor_id = actors.Access(actor)->GetEntityID();
    scene.AddComponent<UUIDComponent>(actor_id, {UUID(4)});
    scene.AddComponent<TransformComponent>(actor_id, {});
    const auto actor_before = Value(actor_id);
    Choose({first, light, actor_id, camera}, light);
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto snapped = Translation();
    ASSERT_TRUE(session.BeginTranslation(snapped));
    EXPECT_EQ(session.TransformCount(), 4u);
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(snapped, .28f), false));
    const float delta = Value(light).Position.x - .34f;
    EXPECT_NEAR(Value(light).Position.x, .6f, .0002f);
    EXPECT_NEAR(Value(first).Position.x, -.27f + delta, .0002f);
    EXPECT_NEAR(Value(camera).Position.x, 1.83f + delta, .0002f);
    EXPECT_NEAR(Value(actor_id).Position.x, actor_before.Position.x + delta, .0002f);
    EXPECT_EQ(session.Selection().ActiveUUID(), UUID(2));
    EXPECT_EQ(session.Selection().Entities().size(), 4u);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    const auto after = Value(first);
    ASSERT_TRUE(session.Undo());
    Near(Value(first).Position, {-.27f, 0.f, 0.f}, 0.f);
    EXPECT_TRUE(SameLocalTransform(Value(actor_id), actor_before));
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(SameLocalTransform(Value(first), after));
}

TEST_F(GizmoBatchTest, ActiveChildIsNotRetargetedOrDoubleTransformedAcrossOrganizationalParents)
{
    const auto parent = Entity(1), group = Entity(2, {}, false), child = Entity(3, {2.f, 0.f, 0.f}), other = Entity(4, {-3.f, 0.f, 0.f});
    ASSERT_TRUE(scene.SetParent(group, parent));
    ASSERT_TRUE(scene.SetParent(child, group));
    Choose({child, other, parent}, child);
    const auto child_before = Value(child);
    const auto input        = Translation();
    EXPECT_EQ(input.Expected.Target.CachedEntity, child);
    Near(input.Expected.Pivot, {2.f, 0.f, 0.f});
    ASSERT_TRUE(session.BeginTranslation(input));
    EXPECT_EQ(session.TransformCount(), 2u);
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    Sync();
    EXPECT_TRUE(SameLocalTransform(Value(child), child_before));
    Near(WorldPosition(child), {2.5f, 0.f, 0.f});
    Near(Value(parent).Position, {.5f, 0.f, 0.f});
    Near(Value(other).Position, {-2.5f, 0.f, 0.f});
    TranslationSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
    EXPECT_EQ(snapshot.Target.CachedEntity, child);
    Near(snapshot.Pivot, {2.5f, 0.f, 0.f});
    Near(snapshot.StartPivot, {2.f, 0.f, 0.f});
    session.Cancel();
    Sync();
    Near(WorldPosition(child), {2.f, 0.f, 0.f}, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoBatchTest, TransformlessSelectedDescendantCannotDisappearBehindRootPruning)
{
    const auto parent = Entity(1), child = Entity(2, {}, false), other = Entity(3);
    Choose({parent, other});
    const auto prior = Translation();
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({parent, child, other});
    TargetFrame frame = {.Valid = true};
    EXPECT_FALSE(ResolveTargetFrame(session, frame));
    EXPECT_FALSE(frame.Valid);
    EXPECT_FALSE(session.BeginTranslation(prior));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.Selection().Entities().size(), 3u);
}

TEST_F(GizmoBatchTest, PrunedDescendantExternalEditCancelsRootsWithoutOverwritingForeignContent)
{
    const auto parent = Entity(1), child = Entity(2, {1.f, 0.f, 0.f}), other = Entity(3, {3.f, 0.f, 0.f});
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({parent, child, other}, child);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    Sync();
    scene.GetComponent<TransformComponent>(child)->Position.y = 7.f;
    EXPECT_FALSE(session.UpdateTranslation(TranslatePointer(input, 1.f), true));
    EXPECT_FALSE(session.Active());
    Near(Value(parent).Position, {}, 0.f);
    Near(Value(other).Position, {3.f, 0.f, 0.f}, 0.f);
    EXPECT_FLOAT_EQ(Value(child).Position.y, 7.f);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoBatchTest, ForeignRootEditPreservesThatPoseAndRestoresOtherOwnedRoots)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    Choose({a, b}, a);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    scene.GetComponent<TransformComponent>(b)->Position.y = 7.f;
    EXPECT_FALSE(session.UpdateTranslation(TranslatePointer(input, 1.f), true));
    EXPECT_FALSE(session.Active());
    Near(Value(a).Position, {}, 0.f);
    EXPECT_FLOAT_EQ(Value(b).Position.y, 7.f);
    EXPECT_NEAR(Value(b).Position.x, 2.5f, .0002f);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoBatchTest, TranslationRejectsTheWholeCandidateWhenALaterRootCannotRepresentTheMotion)
{
    const auto a = Entity(1), b = Entity(2, {1e12f, 0.f, 0.f});
    Choose({a, b}, a);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    EXPECT_FALSE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    EXPECT_TRUE(session.Active());
    Near(Value(a).Position, {}, 0.f);
    EXPECT_FLOAT_EQ(Value(b).Position.x, 1e12f);
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), false));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoBatchTest, RotationUsesTheActiveChildPivotWithoutChangingDescendantLocalValues)
{
    const auto parent = Entity(1), child = Entity(2, {1.f, 0.f, 0.f}), other = Entity(3, {3.f, 0.f, 0.f});
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({parent, other, child}, child);
    const auto child_before  = Value(child);
    const auto parent_before = Value(parent), other_before = Value(other);
    const auto input = Rotation();
    ASSERT_TRUE(session.BeginRotation(input));
    EXPECT_EQ(session.TransformCount(), 2u);
    ASSERT_TRUE(session.UpdateRotation(RotationPointer(input, std::numbers::pi / 2.0), true));
    Sync();
    Near(WorldPosition(child), {1.f, 0.f, 0.f});
    Near(WorldPosition(parent), {1.f, -1.f, 0.f});
    Near(WorldPosition(other), {1.f, 2.f, 0.f});
    EXPECT_TRUE(SameLocalTransform(Value(child), child_before));
    RotationSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
    EXPECT_EQ(snapshot.Target.CachedEntity, child);
    Near(snapshot.Pivot, {1.f, 0.f, 0.f});
    ASSERT_TRUE(session.UpdateRotation(RotationPointer(input, std::numbers::pi / 2.0), false));
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(parent), parent_before));
    EXPECT_TRUE(SameLocalTransform(Value(other), other_before));
    EXPECT_TRUE(SameLocalTransform(Value(child), child_before));
}

TEST_F(GizmoBatchTest, RotationShearFailureKeepsEveryRootAtTheLastValidPreview)
{
    const auto a = Entity(1), parent = Entity(2), b = Entity(3, {1.f, 0.f, 0.f});
    scene.GetComponent<TransformComponent>(parent)->Scale = {2.f, 1.f, 1.f};
    ASSERT_TRUE(scene.SetParent(b, parent));
    Choose({a, b}, a);
    const auto input = Rotation();
    ASSERT_TRUE(session.BeginRotation(input));
    ASSERT_TRUE(session.UpdateRotation(RotationPointer(input, std::numbers::pi), true));
    Sync();
    const auto       before_a = Value(a), before_b = Value(b);
    RotationSnapshot accepted = {};
    ASSERT_TRUE(session.ReadRotationSnapshot(accepted));
    EXPECT_NEAR(std::abs(accepted.Angle), std::numbers::pi, .0001);
    EXPECT_FALSE(session.UpdateRotation(RotationPointer(input, std::numbers::pi / 4.0), true));
    EXPECT_TRUE(SameLocalTransform(Value(a), before_a));
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    RotationSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadRotationSnapshot(snapshot));
    EXPECT_EQ(snapshot.Angle, accepted.Angle);
    ASSERT_TRUE(session.UpdateRotation({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_TRUE(SameLocalTransform(Value(a), before_a));
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
}

TEST_F(GizmoBatchTest, SharedAxisScaleMapsDifferentlyOrientedRootsIntoOneCapturedActiveFrame)
{
    const auto a = Entity(1), b = Entity(2, {3.f, 4.f, 0.f});
    scene.GetComponent<TransformComponent>(b)->Rotation.z = std::numbers::pi_v<float> / 2.f;
    scene.GetComponent<TransformComponent>(b)->Scale      = {1.f, -2.f, 3.f};
    Choose({b, a}, a);
    const auto before_b = Value(b);
    const auto input    = Scale(Handle::ScaleX);
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(ScalePointer(input, 2.0), true));
    Sync();
    Near(Value(a).Scale, {2.f, 1.f, 1.f});
    Near(Value(b).Position, {6.f, 4.f, 0.f});
    Near(Value(b).Scale, {1.f, -4.f, 3.f});
    ScaleSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadScaleSnapshot(snapshot));
    EXPECT_EQ(snapshot.Target.CachedEntity, a);
    EXPECT_NEAR(snapshot.Ratio, 2.f, .0001f);
    Near(snapshot.Axes[0], {1.f, 0.f, 0.f});
    ASSERT_TRUE(session.UpdateScale(ScalePointer(input, 2.0), false));
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    Near(Value(a).Scale, {1.f, 1.f, 1.f}, 0.f);
}

TEST_F(GizmoBatchTest, APrunedActiveChildStillDefinesTheSharedScaleAxesAndPivot)
{
    const auto parent = Entity(1), child = Entity(2, {1.f, 0.f, 0.f});
    scene.GetComponent<TransformComponent>(child)->Rotation.z = std::numbers::pi_v<float> / 2.f;
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({parent, child}, child);
    const auto child_before = Value(child);
    const auto input        = Scale(Handle::ScaleX);
    ASSERT_TRUE(session.BeginScale(input));
    EXPECT_EQ(session.TransformCount(), 1u);
    ASSERT_TRUE(session.UpdateScale(ScalePointer(input, 2.0), false));
    Sync();
    Near(Value(parent).Scale, {1.f, 2.f, 1.f});
    Near(WorldPosition(child), {1.f, 0.f, 0.f});
    EXPECT_TRUE(SameLocalTransform(Value(child), child_before));
    EXPECT_EQ(session.Selection().ActiveUUID(), UUID(2));
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoBatchTest, GroupScaleRejectsLaterShearAtomicallyAndUniformScalingRetainsZeroAndNegativeSigns)
{
    const auto a = Entity(1), b = Entity(2, {3.f, 0.f, 0.f});
    scene.GetComponent<TransformComponent>(b)->Rotation.z = .7f;
    scene.GetComponent<TransformComponent>(b)->Scale      = {-0.f, -1e-6f, 2.f};
    Choose({a, b}, a);
    const auto before_a = Value(a), before_b = Value(b);
    const auto axis = Scale(Handle::ScaleX);
    ASSERT_TRUE(session.BeginScale(axis));
    EXPECT_FALSE(session.UpdateScale(ScalePointer(axis, 2.0), true));
    EXPECT_TRUE(SameLocalTransform(Value(a), before_a));
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    session.Cancel();
    const auto uniform = Scale();
    ASSERT_TRUE(session.BeginScale(uniform));
    ASSERT_TRUE(session.UpdateScale(ScalePointer(uniform, 1.5), true));
    Near(Value(b).Position, {4.5f, 0.f, 0.f});
    Near(Value(b).Scale, {-0.f, -1.5e-6f, 3.f});
    EXPECT_TRUE(std::signbit(Value(b).Scale.x));
    Near(Value(b).Rotation, before_b.Rotation, 0.f);
    ASSERT_TRUE(session.UpdateScale(uniform.Pointer, false));
    EXPECT_TRUE(SameLocalTransform(Value(a), before_a));
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoBatchTest, SelectionAndModeTransitionsCancelTheWholeRootBatch)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    Choose({a, b}, a);
    const auto before_a = Value(a), before_b = Value(b);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    ASSERT_TRUE(session.RequestSelection(b));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_TRUE(SameLocalTransform(Value(a), before_a));
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    Choose({a, b}, a);
    const auto scale = Scale();
    ASSERT_TRUE(session.BeginScale(scale));
    ASSERT_TRUE(session.UpdateScale(ScalePointer(scale, 2.0), true));
    Mode(Operation::Rotate);
    EXPECT_FALSE(session.Active());
    EXPECT_TRUE(SameLocalTransform(Value(a), before_a));
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoBatchTest, RepeatedMixedOperationBatchesReuseAllWorkspaceAndBoundedHistory)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f}), child = Entity(3, {.5f, 0.f, 0.f});
    ASSERT_TRUE(scene.SetParent(child, a));
    Choose({a, b, child}, a);
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        if (i % 3 == 0)
        {
            const auto input = Translation();
            ASSERT_TRUE(session.BeginTranslation(input));
            ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), false));
        }
        else if (i % 3 == 1)
        {
            const auto input = Rotation();
            ASSERT_TRUE(session.BeginRotation(input));
            ASSERT_TRUE(session.UpdateRotation(RotationPointer(input, .3), false));
        }
        else
        {
            const auto input = Scale();
            ASSERT_TRUE(session.BeginScale(input));
            ASSERT_TRUE(session.UpdateScale(ScalePointer(input, 1.5), false));
        }
        EXPECT_LE(session.History().UndoCount(), 8u);
        EXPECT_LE(session.History().BytesUsed(), session.History().ByteCapacity());
        ASSERT_TRUE(session.Undo());
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_FALSE(session.Dirty());
    Near(Value(child).Position, {.5f, 0.f, 0.f}, 0.f);
}

TEST_F(GizmoBatchTest, Full4096RootPointerBatchIsNeverTruncatedAndDoesNotAllocateDuringPreview)
{
    constexpr uint32_t                             count   = 4096;
    ZEngine::Core::Containers::Array<EntityTarget> targets = {};
    targets.init(&memory.MainArena, count);
    for (uint32_t i = 0; i < count; ++i)
    {
        targets.push(session.TargetFor(Entity(i + 1, {float(i % 17) * .1f, float(i % 13) * .1f, 0.f})));
    }
    ASSERT_TRUE(session.SetSelection({targets.data(), targets.size()}, targets[0].UUID));
    const auto input  = Translation();
    const auto offset = memory.MainArena.m_current_offset;
    ASSERT_TRUE(session.BeginTranslation(input));
    EXPECT_EQ(session.TransformCount(), count);
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), false));
    EXPECT_EQ(session.History().UndoCount(), 1u);
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_NEAR(Value(targets[i].CachedEntity).Position.x, float(i % 17) * .1f + .5f, .0003f);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    ASSERT_TRUE(session.Undo());
    EXPECT_FALSE(session.Dirty());
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_FLOAT_EQ(Value(targets[i].CachedEntity).Position.x, float(i % 17) * .1f);
    }
}

TEST_F(GizmoBatchTest, SharedCustomPivotRotatesAndScalesTheWholeSelectionAndMovesWithTranslation)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    Choose({a, b}, a);
    Mode(Operation::Translate);
    const auto       frame = Frame();
    const PivotBegin pivot = {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(frame.Pivot), .Key = 51, .PointerSerial = 201};
    ASSERT_TRUE(session.BeginPivot(pivot));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 0.f, 0.f}), false));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    const auto input = Translation();
    Near(input.Expected.Pivot, {1.f, 0.f, 0.f});
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), false));
    Vec3f carried = {};
    ASSERT_TRUE(session.ReadCustomPivot(carried));
    Near(carried, {1.5f, 0.f, 0.f});
    Near(Value(a).Position, {.5f, 0.f, 0.f});
    Near(Value(b).Position, {2.5f, 0.f, 0.f});
    // Mode changes clear the custom pivot by the approved editor-local policy.
    // Recreate it in Scale mode before exercising that shared pivot.
    Mode(Operation::Scale);
    const auto       scale_frame = Frame();
    const PivotBegin scale_pivot = {.Expected = scale_frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(scale_frame.Pivot), .Key = 52, .PointerSerial = 202};
    ASSERT_TRUE(session.BeginPivot(scale_pivot));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.5f, 0.f, 0.f}), false));
    const auto scale = Scale();
    ASSERT_TRUE(session.BeginScale(scale));
    ASSERT_TRUE(session.UpdateScale(ScalePointer(scale, 2.0), true));
    Near(Value(a).Position, {-.5f, 0.f, 0.f});
    Near(Value(b).Position, {3.5f, 0.f, 0.f});
    session.Cancel();
    Near(Value(a).Position, {.5f, 0.f, 0.f});
    Near(Value(b).Position, {2.5f, 0.f, 0.f});
    ASSERT_TRUE(session.ReadCustomPivot(carried));
    Near(carried, {1.5f, 0.f, 0.f});
    EXPECT_EQ(session.History().UndoCount(), 1u);
    Mode(Operation::Rotate);
    const auto       rotate_frame = Frame();
    const PivotBegin rotate_pivot = {.Expected = rotate_frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(rotate_frame.Pivot), .Key = 53, .PointerSerial = 203};
    ASSERT_TRUE(session.BeginPivot(rotate_pivot));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.5f, 0.f, 0.f}), false));
    const auto rotation = Rotation();
    ASSERT_TRUE(session.BeginRotation(rotation));
    ASSERT_TRUE(session.UpdateRotation(RotationPointer(rotation, std::numbers::pi / 2.0), true));
    Near(Value(a).Position, {1.5f, -1.f, 0.f});
    Near(Value(b).Position, {1.5f, 1.f, 0.f});
    session.Cancel();
    Near(Value(a).Position, {.5f, 0.f, 0.f});
    Near(Value(b).Position, {2.5f, 0.f, 0.f});
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(GizmoBatchTest, ScaleSnappingAndFineInversionApplyOneRatioToAllRootsFromTheImmutableStart)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    Choose({a, b}, a);
    Mode(Operation::Scale);
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Scale, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    const auto input = Scale();
    ASSERT_TRUE(session.BeginScale(input));
    const auto pointer = ScalePointer(input, 1.37);
    ASSERT_TRUE(session.UpdateScale(pointer, true));
    EXPECT_NEAR(Value(a).Scale.x, 1.25f, .0001f);
    EXPECT_NEAR(Value(b).Position.x, 2.5f, .0001f);
    ASSERT_TRUE(session.UpdateScale(pointer, true, false, true));
    EXPECT_NEAR(Value(a).Scale.x, 1.37f, .0001f);
    EXPECT_NEAR(Value(b).Position.x, 2.74f, .0001f);
    ASSERT_TRUE(session.UpdateScale(pointer, true, true));
    const double fine_ratio = std::exp(std::log(1.37) * Constants::FineMotionFactor);
    EXPECT_NEAR(Value(a).Scale.x, fine_ratio, .0001f);
    EXPECT_NEAR(Value(b).Position.x, 2.0 * fine_ratio, .0001f);
    ASSERT_TRUE(session.UpdateScale(input.Pointer, false));
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoBatchTest, HistoryBudgetRejectsTheCompleteBatchInsteadOfReducingItToOneRoot)
{
    const auto    a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    EditorSession bounded = {};
    ASSERT_TRUE(bounded.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 192}));
    ASSERT_TRUE(bounded.BeginMutationPhase());
    ASSERT_TRUE(bounded.BindScene(&scene));
    ASSERT_TRUE(bounded.Select(a));
    ASSERT_TRUE(bounded.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = Operation::Translate}));
    ASSERT_TRUE(bounded.ProcessRequests());
    auto input = TranslationBegin{.Projection = projection, .Bounds = bounds, .Pointer = Screen({.7f, 0.f, 0.f}), .Key = 61, .PointerSerial = 301, .Constraint = Handle::TranslateX};
    ASSERT_TRUE(ResolveTargetFrame(bounded, input.Expected));
    ASSERT_TRUE(bounded.BeginTranslation(input)); // Budget holds one root.
    bounded.Cancel();
    ASSERT_TRUE(bounded.Select(b, SelectionMode::Add));
    ASSERT_TRUE(ResolveTargetFrame(bounded, input.Expected));
    input.Pointer = Screen(input.Expected.Pivot + Vec3f(.7f, 0.f, 0.f));
    EXPECT_FALSE(bounded.BeginTranslation(input));
    EXPECT_FALSE(bounded.Active());
    EXPECT_EQ(bounded.Selection().Entities().size(), 2u);
    EXPECT_EQ(bounded.History().UndoCount(), 0u);
    Near(Value(a).Position, {}, 0.f);
    Near(Value(b).Position, {2.f, 0.f, 0.f}, 0.f);
    bounded.Shutdown();
}

TEST_F(GizmoBatchTest, ReparentAndReplacementRetireTheOriginalBatchWithoutWritingARecycledEntity)
{
    const auto parent = Entity(1), a = Entity(2), b = Entity(3, {2.f, 0.f, 0.f});
    Choose({a, b}, a);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    ASSERT_TRUE(session.RequestReparent(b, parent));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    Near(Value(a).Position, {}, 0.f);
    Near(Value(b).Position, {2.f, 0.f, 0.f}, 0.f);
    Choose({a, b}, a);
    const auto next = Translation();
    ASSERT_TRUE(session.BeginTranslation(next));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(next, .5f), true));
    scene.DestroyEntity(b);
    const auto replacement = Entity(3, {7.f, 0.f, 0.f});
    EXPECT_NE(replacement, b);
    EXPECT_FALSE(session.UpdateTranslation(TranslatePointer(next, 1.f), true));
    EXPECT_FALSE(session.Active());
    Near(Value(a).Position, {}, 0.f);
    Near(Value(replacement).Position, {7.f, 0.f, 0.f}, 0.f);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoBatchTest, EachRootUsesItsOwnRotatedReflectedAndNearZeroParentInverse)
{
    const auto a = Entity(1), parent_b = Entity(2), b = Entity(3, {.3f, -.4f, .5f}), parent_c = Entity(4), c = Entity(5, {.1f, .2f, .3f});
    scene.GetComponent<TransformComponent>(parent_b)->Rotation = {.2f, .3f, .4f};
    scene.GetComponent<TransformComponent>(parent_b)->Scale    = {-2.f, .5f, 3.f};
    scene.GetComponent<TransformComponent>(parent_c)->Scale    = {1e-5f, 2.f, 1.f};
    ASSERT_TRUE(scene.SetParent(b, parent_b));
    ASSERT_TRUE(scene.SetParent(c, parent_c));
    Choose({a, b, c}, a);
    const auto before_b = Value(b), before_c = Value(c);
    const auto world_b = WorldPosition(b), world_c = WorldPosition(c);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    Sync();
    Near(WorldPosition(a), {.5f, 0.f, 0.f});
    Near(WorldPosition(b), world_b + Vec3f(.5f, 0.f, 0.f));
    Near(WorldPosition(c), world_c + Vec3f(.5f, 0.f, 0.f));
    Near(Value(b).Rotation, before_b.Rotation, 0.f);
    Near(Value(c).Scale, before_c.Scale, 0.f);
    session.Cancel();
    EXPECT_TRUE(SameLocalTransform(Value(b), before_b));
    EXPECT_TRUE(SameLocalTransform(Value(c), before_c));
    scene.GetComponent<TransformComponent>(parent_c)->Scale.x = 0.f;
    scene.SynchronizeHierarchy();
    TargetFrame invalid = {};
    if (ResolveTargetFrame(session, invalid))
    {
        auto bad     = input;
        bad.Expected = invalid;
        EXPECT_FALSE(session.BeginTranslation(bad));
    }
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(GizmoBatchTest, RootFramesCapturePressTimeWorldAndMeshWithTheActiveRootFirst)
{
    const auto a = Entity(1, {1.f, 0.f, 0.f}), b = Entity(2, {2.f, 0.f, 0.f}), child = Entity(3);
    ASSERT_TRUE(scene.SetParent(child, a));
    scene.AddComponent<MeshComponent>(a, {.MeshUUID = UUID(10)});
    scene.AddComponent<MeshComponent>(b, {.MeshUUID = UUID(11)});
    Choose({a, child, b}, b);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    // The first renderer read may be delayed until after the authored preview
    // and an unrelated mesh binding change. Neither may change the capture.
    scene.GetComponent<MeshComponent>(b)->MeshUUID = UUID(12);
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    Sync();
    RootFrameView frames = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    EXPECT_NE(frames.Generation, 0u);
    EXPECT_EQ(frames.TotalRoots, 2u);
    ASSERT_EQ(frames.Frames.size(), 2u);
    EXPECT_EQ(frames.Frames[0].Target.CachedEntity, b);
    EXPECT_EQ(frames.Frames[1].Target.CachedEntity, a);
    EXPECT_EQ(frames.Frames[0].MeshUUID, UUID(11));
    EXPECT_EQ(frames.Frames[1].MeshUUID, UUID(10));
    EXPECT_FLOAT_EQ(frames.Frames[0].StartWorld(0, 3), 2.f);
    EXPECT_FLOAT_EQ(frames.Frames[1].StartWorld(0, 3), 1.f);
    EXPECT_NEAR(WorldPosition(b).x, 2.5f, .0002f);
    EXPECT_EQ(session.TransformCount(), 2u);
    EXPECT_EQ(session.Selection().ActiveUUID(), UUID(2));
}

TEST_F(GizmoBatchTest, PrunedActiveChildNeverBecomesAnAdditionalDecorationRoot)
{
    const auto a = Entity(1), child = Entity(2, {1.f, 0.f, 0.f}), b = Entity(3, {2.f, 0.f, 0.f});
    ASSERT_TRUE(scene.SetParent(child, a));
    Choose({child, a, b}, child);
    ASSERT_TRUE(session.BeginRotation(Rotation()));
    RootFrameView frames = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    ASSERT_EQ(frames.Frames.size(), 2u);
    EXPECT_EQ(frames.Frames[0].Target.CachedEntity, a);
    EXPECT_EQ(frames.Frames[1].Target.CachedEntity, b);
    EXPECT_EQ(session.Selection().ActiveUUID(), UUID(2));
}

TEST_F(GizmoBatchTest, RootFrameReadsPreserveOutputOnWrongThreadPendingHierarchyForeignEditAndInactiveState)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    Choose({a, b});
    RootFrameView frames = {.Generation = 77, .TotalRoots = 99};
    EXPECT_FALSE(session.ReadGizmoRootFrames(frames));
    EXPECT_EQ(frames.Generation, 77u);
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    bool        read = true;
    std::thread other([&] { read = session.ReadGizmoRootFrames(frames); });
    other.join();
    EXPECT_FALSE(read);
    EXPECT_EQ(frames.Generation, 77u);
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), true));
    EXPECT_FALSE(session.ReadGizmoRootFrames(frames));
    EXPECT_EQ(frames.TotalRoots, 99u);
    Sync();
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    const auto accepted                                   = frames;
    scene.GetComponent<TransformComponent>(b)->Position.y = 7.f;
    EXPECT_FALSE(session.ReadGizmoRootFrames(frames));
    EXPECT_EQ(frames.Generation, accepted.Generation);
    EXPECT_EQ(frames.Frames.data(), accepted.Frames.data());
    session.Cancel();
    EXPECT_FALSE(session.ReadGizmoRootFrames(frames));
    EXPECT_FLOAT_EQ(Value(b).Position.y, 7.f);
}

TEST_F(GizmoBatchTest, RootFrameGenerationChangesEvenWhenATestReusesPhysicalTokensWithoutAnIdleOverlay)
{
    const auto a = Entity(1), b = Entity(2, {2.f, 0.f, 0.f});
    Choose({a, b});
    const auto input = Translation();
    ASSERT_TRUE(session.BeginTranslation(input));
    RootFrameView first = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(first));
    session.Cancel();
    ASSERT_TRUE(session.BeginTranslation(input));
    RootFrameView second = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(second));
    EXPECT_NE(first.Generation, second.Generation);
    EXPECT_EQ(first.TotalRoots, second.TotalRoots);
    session.Cancel();
    const auto target = session.TargetFor(a);
    ASSERT_TRUE(session.BeginTransform({&target, 1}));
    EXPECT_FALSE(session.ReadGizmoRootFrames(second)); // numeric controls do not reuse old visual captures
}

TEST_F(GizmoBatchTest, DecorationCaptureCapacityDoesNotTruncateTheAuthoredRootsAndKeepsALateActiveRoot)
{
    constexpr uint32_t              count   = RootFrameView::Capacity + 17;
    FixedArray<EntityTarget, count> targets = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        targets[i] = session.TargetFor(Entity(i + 1));
    }
    ASSERT_TRUE(session.SetSelection({targets.data(), count}, targets[count - 1].UUID));
    const auto input = Translation();
    const auto bytes = memory.MainArena.m_current_offset;
    ASSERT_TRUE(session.BeginTranslation(input));
    EXPECT_EQ(session.TransformCount(), count);
    RootFrameView frames = {};
    ASSERT_TRUE(session.ReadGizmoRootFrames(frames));
    EXPECT_EQ(frames.TotalRoots, count);
    ASSERT_EQ(frames.Frames.size(), RootFrameView::Capacity);
    EXPECT_EQ(frames.Frames[0].Target.UUID, targets[count - 1].UUID);
    for (uint32_t i = 1; i < frames.Frames.size(); ++i)
    {
        EXPECT_EQ(frames.Frames[i].Target.UUID, targets[i - 1].UUID);
    }
    ASSERT_TRUE(session.UpdateTranslation(TranslatePointer(input, .5f), false));
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    for (const auto& target : targets)
    {
        EXPECT_NEAR(Value(target.CachedEntity).Position.x, .5f, .0002f);
    }
    ASSERT_TRUE(session.Undo());
    for (const auto& target : targets)
    {
        EXPECT_FLOAT_EQ(Value(target.CachedEntity).Position.x, 0.f);
    }
}
