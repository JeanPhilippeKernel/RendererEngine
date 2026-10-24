#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
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
namespace G = ZEngine::Core::Maths::Geometry;

class GizmoPivotTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    Scene                                scene      = {};
    EditorSession                        session    = {};
    EntityID                             entity     = {};
    G::ProjectionFrame                   projection = {};
    G::Viewport                          bounds     = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };
    uint64_t serial = 0;

    EntityID Entity(uint8_t identity)
    {
        ZEngine::Core::Containers::FixedArray<uint8_t, 16> bytes = {};
        bytes[15]                                                = identity;
        const auto result                                        = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(result, {uuids::uuid(bytes.begin(), bytes.end())});
        scene.AddComponent<TransformComponent>(result, {});
        return result;
    }
    void SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
        entity = Entity(1);
        ASSERT_TRUE(session.Select(entity));
        Mode(Operation::Translate);
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = false}));
        ASSERT_TRUE(session.ProcessRequests());
        Projection();
    }
    void TearDown() override
    {
        session.Shutdown();
        scene.Shutdown();
        memory.Shutdown();
    }
    void Mode(Operation mode)
    {
        ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Operation, .Mode = mode}));
        ASSERT_TRUE(session.ProcessRequests());
    }
    void Projection(bool ortho = false, bool reverse = false)
    {
        using namespace ZEngine::Rendering::Cameras;
        FlyCamera camera(4.f / 3.f, {});
        camera.SetPosition({0.f, 0.f, 10.f});
        camera.SetOrientation(0.f, 0.f);
        camera.SetProjectionType(ortho ? CameraType::ORTHOGRAPHIC : CameraType::PERSPECTIVE);
        auto frame = camera.CaptureFrameData();
        if (reverse)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                frame.Projection(2, column) = frame.Projection(3, column) - frame.Projection(2, column);
            }
        }
        ASSERT_TRUE(G::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, ortho ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &projection));
    }
    Vec2f Screen(const Vec3f& point)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(point, projection, bounds, &result));
        return result.Screen;
    }
    PivotBegin Input()
    {
        scene.SynchronizeHierarchy();
        PivotBegin result = {.Projection = projection, .Bounds = bounds, .Key = 99, .PointerSerial = ++serial};
        EXPECT_TRUE(ResolveTargetFrame(session, result.Expected));
        result.Pointer = Screen(result.Expected.Pivot);
        return result;
    }
    void Custom(const Vec3f& point)
    {
        ASSERT_TRUE(session.BeginPivot(Input()));
        ASSERT_TRUE(session.UpdatePivot(Screen(point), false));
    }
    Vec3f Custom()
    {
        Vec3f point = {};
        EXPECT_TRUE(session.ReadCustomPivot(point));
        return point;
    }
    void Sync()
    {
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
    }
};

TEST_F(GizmoPivotTest, ViewPlaneEditsAcrossProjectionsDoNotTouchAuthoredValuesHistoryOrDirtyState)
{
    const auto pose     = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    const auto revision = session.DocumentRevision();
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            Projection(ortho, reverse);
            ASSERT_TRUE(session.BeginPivot(Input()));
            EXPECT_TRUE(session.Active());
            EXPECT_EQ(session.TransformCount(), 0u);
            ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
            PivotSnapshot snapshot = {};
            ASSERT_TRUE(session.ReadPivotSnapshot(snapshot));
            EXPECT_NEAR(snapshot.Position.x, 1.f, .0001f);
            EXPECT_NEAR(snapshot.Position.y, 2.f, .0001f);
            EXPECT_NEAR(snapshot.Position.z, 0.f, .0001f);
            EXPECT_EQ(snapshot.Origin.x, 0.f);
            ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), false));
            EXPECT_FALSE(session.Active());
            EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), pose));
            EXPECT_EQ(session.DocumentRevision(), revision);
            EXPECT_FALSE(session.Dirty());
            EXPECT_EQ(session.History().UndoCount(), 0u);
            EXPECT_EQ(session.History().RedoCount(), 0u);
            ASSERT_TRUE(session.ClearCustomPivot());
        }
    }
}

TEST_F(GizmoPivotTest, PivotCommitPreservesExistingUndoRedoAndSavedCheckpoint)
{
    auto target = session.TargetFor(entity);
    ASSERT_TRUE(session.BeginTransform({&target, 1}));
    auto value       = *session.TransformBefore(0);
    value.Position.x = 1.f;
    ASSERT_TRUE(session.PreviewTransforms({&value, 1}));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.Undo());
    Sync();
    ASSERT_EQ(session.History().RedoCount(), 1u);
    const auto revision = session.DocumentRevision();
    Custom({.3f, .7f, 0.f});
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_EQ(session.History().RedoCount(), 1u);
    EXPECT_EQ(session.DocumentRevision(), revision);
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f);
}

TEST_F(GizmoPivotTest, CancelAndReturnToPressRestoreTheExactPreviousCustomPivotOrItsAbsence)
{
    Vec3f untouched(123.f, 456.f, 789.f);
    EXPECT_FALSE(session.ReadCustomPivot(untouched));
    EXPECT_EQ(untouched.x, 123.f);
    auto input = Input();
    ASSERT_TRUE(session.BeginPivot(input));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
    session.Cancel();
    EXPECT_FALSE(session.ReadCustomPivot(untouched));
    ASSERT_TRUE(session.BeginPivot(Input()));
    ASSERT_TRUE(session.UpdatePivot(input.Pointer, false));
    EXPECT_FALSE(session.ReadCustomPivot(untouched));
    Custom({.5f, .25f, 0.f});
    const auto before = Custom();
    input             = Input();
    ASSERT_TRUE(session.BeginPivot(input));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
    ASSERT_TRUE(session.UpdatePivot(input.Pointer, false));
    EXPECT_EQ(Custom(), before);
    ASSERT_TRUE(session.BeginPivot(Input()));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
    session.Cancel();
    EXPECT_EQ(Custom(), before);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoPivotTest, CapturedSnappingFineControlAndInvalidReleaseKeepOnlyTheAcceptedPoint)
{
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.BeginPivot(Input()));
    ASSERT_TRUE(session.UpdatePivot(Screen({.26f, .34f, 0.f}), true));
    EXPECT_NEAR(Custom().x, .3f, .0001f);
    EXPECT_NEAR(Custom().y, .3f, .0001f);
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true, true));
    EXPECT_NEAR(Custom().x, .1f, .0001f);
    EXPECT_NEAR(Custom().y, .2f, .0001f);
    const auto accepted = Custom();
    ASSERT_TRUE(session.UpdatePivot({std::numeric_limits<float>::quiet_NaN(), 0.f}, false));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(Custom(), accepted);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoPivotTest, OnlyActualSelectionAndModeChangesClearTheCustomPivot)
{
    Custom({.5f, .25f, 0.f});
    ASSERT_TRUE(session.Select(entity));
    ASSERT_TRUE(session.SetSelection(session.Selection().Entities(), session.Selection().ActiveUUID()));
    Mode(Operation::Translate);
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.ProcessRequests());
    Vec3f point = {};
    ASSERT_TRUE(session.ReadCustomPivot(point));
    Mode(Operation::Rotate);
    EXPECT_FALSE(session.ReadCustomPivot(point));
    Custom({.5f, .25f, 0.f});
    const auto other = Entity(2);
    ASSERT_TRUE(session.Select(other));
    EXPECT_FALSE(session.ReadCustomPivot(point));
    Custom({.5f, .25f, 0.f});
    ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
    EXPECT_FALSE(session.ReadCustomPivot(point));
}

TEST_F(GizmoPivotTest, InvalidOrReplacedTargetCancelsWithoutOverwritingExternalAuthoredChanges)
{
    scene.GetComponent<TransformComponent>(entity)->Scale = {};
    Custom({.5f, .25f, 0.f});
    const auto before = Custom();
    ASSERT_TRUE(session.BeginPivot(Input()));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
    scene.GetComponent<TransformComponent>(entity)->Rotation.z = .5f;
    Sync(); // a collapsed world matrix alone cannot detect this authored change
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(Custom(), before);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Rotation.z, .5f);
    ASSERT_TRUE(session.BeginPivot(Input()));
    scene.DestroyEntity(entity);
    Sync();
    EXPECT_FALSE(session.Active());
    Vec3f output = {};
    EXPECT_FALSE(session.ReadCustomPivot(output));
    EXPECT_TRUE(session.Selection().Entities().empty());
}

TEST_F(GizmoPivotTest, PhaseThreadReadonlyAndStaleFrameRejectionsDoNotConsumeOrMutatePivotState)
{
    auto input = Input();
    session.EndMutationPhase();
    EXPECT_FALSE(session.BeginPivot(input));
    ASSERT_TRUE(session.BeginMutationPhase());
    bool        accepted = true;
    std::thread other([&] { accepted = session.BeginPivot(input); });
    other.join();
    EXPECT_FALSE(accepted);
    input.Expected.Pivot.x = 999.f;
    EXPECT_FALSE(session.BeginPivot(input));
    ASSERT_TRUE(session.BeginPivot(Input()));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
    input = Input();
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.BeginPivot(input));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoPivotTest, SceneEpochAndPrunedSelectionCannotCarryAPivotToAnotherDocument)
{
    Custom({.5f, .25f, 0.f});
    auto stale = Input();
    ASSERT_TRUE(session.BeginPivot(stale));
    ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
    ASSERT_TRUE(session.BindScene(&scene));
    ASSERT_TRUE(session.Select(entity));
    EXPECT_FALSE(session.BeginPivot(stale));
    Vec3f output = {};
    EXPECT_FALSE(session.ReadCustomPivot(output));
    EXPECT_FALSE(session.Active());
}

TEST_F(GizmoPivotTest, RotationUsesTheEditedWorldPivotAndOneSharedAuthoredHistoryEntry)
{
    scene.GetComponent<TransformComponent>(entity)->Position.x = 1.f;
    Mode(Operation::Rotate);
    Custom({0.f, 0.f, 0.f});
    auto          pivot = Input();
    RotationBegin input = {
        .Expected = pivot.Expected, .Projection = projection, .Bounds = bounds, .Pointer = Screen({2.f, 0.f, 0.f}
                   ), .RingPoint = {2.f, 0.f, 0.f},
                   .Key = 99, .PointerSerial = ++serial, .Constraint = Handle::RotateZ
    };
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::SnapEnabled, .Target = SnapTarget::Rotation, .Enabled = false}));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(ResolveTargetFrame(session, input.Expected));
    const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    ASSERT_TRUE(session.BeginRotation(input));
    ASSERT_TRUE(session.UpdateRotation(Screen({0.f, 2.f, 0.f}), false));
    Sync();
    const auto after = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    EXPECT_NEAR(after.Position.x, 0.f, .0002f);
    EXPECT_NEAR(after.Position.y, 1.f, .0002f);
    EXPECT_NEAR(Custom().x, 0.f, .0001f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), after));
}

TEST_F(GizmoPivotTest, ScaleUsesTheEditedPivotWithoutAddingPivotStateToHistory)
{
    scene.GetComponent<TransformComponent>(entity)->Position.x = 1.f;
    scene.GetComponent<TransformComponent>(entity)->Scale      = {-1.f, 2.f, .5f};
    Mode(Operation::Scale);
    Custom({0.f, 0.f, 0.f});
    auto       pivot  = Input();
    ScaleBegin input  = {.Expected = pivot.Expected, .Projection = projection, .Bounds = bounds, .Pointer = pivot.Pointer, .Key = 99, .PointerSerial = ++serial, .Constraint = Handle::ScaleUniform};
    const auto before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    ASSERT_TRUE(session.BeginScale(input));
    ASSERT_TRUE(session.UpdateScale(input.Pointer + Vec2f(float(std::log(2.0) * std::numbers::sqrt2 * Constants::UniformScalePixelsPerEfold), 0.f), false));
    const auto after = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    EXPECT_NEAR(after.Position.x, 2.f, .0001f);
    EXPECT_NEAR(after.Scale.x, -2.f, .0001f);
    EXPECT_NEAR(Custom().x, 0.f, .0001f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
}

TEST_F(GizmoPivotTest, TranslationCarriesCustomPivotOnCommitAndPreservesItOnCancel)
{
    Custom({0.f, 1.f, 0.f});
    auto             pivot = Input();
    TranslationBegin input = {.Expected = pivot.Expected, .Projection = projection, .Bounds = bounds, .Pointer = Screen({1.f, 1.f, 0.f}), .Key = 99, .PointerSerial = ++serial, .Constraint = Handle::TranslateX};
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen({1.5f, 1.f, 0.f}), false));
    EXPECT_NEAR(Custom().x, .5f, .0001f);
    EXPECT_NEAR(Custom().y, 1.f, .0001f);
    const auto before = Custom();
    Sync();
    pivot               = Input();
    input.Expected      = pivot.Expected;
    input.PointerSerial = ++serial;
    input.Pointer       = Screen(pivot.Expected.Pivot + Vec3f(1.f, 0.f, 0.f));
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(pivot.Expected.Pivot + Vec3f(2.f, 0.f, 0.f)), true));
    session.Cancel();
    EXPECT_EQ(Custom(), before);
}

TEST_F(GizmoPivotTest, RepeatedPreviewCancelReusesReservedStorageAndPreservesNoopDocumentRevision)
{
    const auto bytes    = memory.MainArena.m_current_offset;
    const auto revision = session.DocumentRevision();
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.BeginPivot(Input()));
        ASSERT_TRUE(session.UpdatePivot(Screen({1.f, 2.f, 0.f}), true));
        session.Cancel();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(session.DocumentRevision(), revision);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(GizmoPivotTest, LocalFrameDirectionsSurviveReflectedNonuniformParentsGroupingAndCollapsedAuthoredAxes)
{
    const auto parent                                        = Entity(2);
    const auto group                                         = scene.CreateEntity();
    scene.GetComponent<TransformComponent>(parent)->Rotation = {.2f, -.4f, .6f};
    scene.GetComponent<TransformComponent>(parent)->Scale    = {-2.f, 3.f, .5f};
    ASSERT_TRUE(scene.SetParent(group, parent));
    ASSERT_TRUE(scene.SetParent(entity, group));
    scene.GetComponent<TransformComponent>(entity)->Rotation = {.3f, .1f, -.2f};
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.ProcessRequests());
    for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        Mode(mode);
        for (float scale : {0.f, -0.f, .00001f, -.00001f, 2.f, -2.f})
        {
            auto* transform        = scene.GetComponent<TransformComponent>(entity);
            transform->Scale       = {scale, 2.f, -3.f};
            const auto frame       = Input().Expected;
            const auto orientation = scene.GetComponent<TransformComponent>(parent)->WorldTransform * ComposeTransformMatrix({}, transform->Rotation, {1.f, 1.f, 1.f});
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                auto expected = Vec3f(orientation(0, axis), orientation(1, axis), orientation(2, axis));
                if (std::signbit(transform->Scale[axis]))
                {
                    expected = -expected;
                }
                expected          = expected.normalize();
                const auto actual = frame.Axes[axis].normalize();
                EXPECT_NEAR(actual.x, expected.x, .00001f);
                EXPECT_NEAR(actual.y, expected.y, .00001f);
                EXPECT_NEAR(actual.z, expected.z, .00001f);
            }
        }
    }
    // Scale uses Local without overwriting the saved space preference.
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::World}));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.GizmoSettings().EffectiveSpace(), TransformSpacePolicy::Local);
    EXPECT_EQ(session.GizmoSettings().Space, TransformSpacePolicy::World);
    Mode(Operation::Translate);
    const auto world = Input().Expected;
    EXPECT_EQ(world.Axes[0], Vec3f(1.f, 0.f, 0.f));
    EXPECT_EQ(world.Axes[1], Vec3f(0.f, 1.f, 0.f));
    EXPECT_EQ(world.Axes[2], Vec3f(0.f, 0.f, 1.f));
}

TEST_F(GizmoPivotTest, CollapsedLocalAxisCanTranslateWithAnEditedPivotAndExactCancel)
{
    const auto parent                                        = Entity(2);
    scene.GetComponent<TransformComponent>(parent)->Rotation = {.2f, -.4f, .6f};
    scene.GetComponent<TransformComponent>(parent)->Scale    = {-2.f, 3.f, .5f};
    ASSERT_TRUE(scene.SetParent(entity, parent));
    scene.GetComponent<TransformComponent>(entity)->Rotation = {.3f, .1f, -.2f};
    scene.GetComponent<TransformComponent>(entity)->Scale    = {0.f, 2.f, -3.f};
    ASSERT_TRUE(session.RequestGizmoChange({.Kind = ChangeKind::Space, .Space = TransformSpacePolicy::Local}));
    ASSERT_TRUE(session.ProcessRequests());
    Custom({.5f, .25f, 0.f});
    const auto             pivot  = Custom();
    const auto             before = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    const auto             frame  = Input().Expected;
    const auto             axis   = frame.Axes[0].normalize();
    const TranslationBegin input  = {.Expected = frame, .Projection = projection, .Bounds = bounds, .Pointer = Screen(pivot + axis), .Key = 99, .PointerSerial = ++serial, .Constraint = Handle::TranslateX};
    ASSERT_TRUE(session.BeginTranslation(input));
    ASSERT_TRUE(session.UpdateTranslation(Screen(pivot + axis * 1.5f), true));
    Sync();
    TranslationSnapshot snapshot = {};
    ASSERT_TRUE(session.ReadTranslationSnapshot(snapshot));
    EXPECT_NEAR(snapshot.Pivot.x - pivot.x, axis.x * .5f, .0002f);
    EXPECT_NEAR(snapshot.Pivot.y - pivot.y, axis.y * .5f, .0002f);
    EXPECT_NEAR(snapshot.Pivot.z - pivot.z, axis.z * .5f, .0002f);
    session.Cancel();
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), before));
    EXPECT_EQ(Custom(), pivot);
    EXPECT_FALSE(session.Dirty());
}
