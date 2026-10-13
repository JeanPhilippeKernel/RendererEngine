#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Gizmos/EditorGizmoController.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <gtest/gtest.h>
#include <cmath>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::UI;
using Tetragrama::Gizmos::EditorGizmoController;

class EditorGizmoControllerTest : public ::testing::TestWithParam<bool>
{
protected:
    ZEngine::Core::Memory::MemoryManager Memory;
    Scene                                World;
    Tetragrama::EditorScene              EditorScene;
    EditorGizmoController                Controller;
    FlyCamera                            Camera{4.f / 3.f, {}};
    ZUIContext                           UI;
    float                                Rect[4] = {50.f, 30.f, 850.f, 630.f};

    void                                 SetUp() override
    {
        Memory.Initialize(ZMega(384), {});
        World.Initialize(&Memory.MainArena);
        EditorScene.Instances.init(&Memory.MainArena, 8);
        EditorScene.TransformSession.Initialize(&Memory.MainArena, 16, 8, 64);
        Controller.Initialize(&Memory.MainArena, 16);
        Camera.SetPosition({0.f, 0.f, 8.f});
        Camera.SetOrientation(0.f, 0.f);
        Camera.SetViewportSize(800.f, 600.f);
        if (GetParam())
            Camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        (void) Camera.CaptureFrameData();
        UI.ViewportInputKey   = 42;
        UI.ViewportGeneration = 7;
        UI.HotKey             = 42;
        ZEngine::Managers::AssetManager::Initialize(&Memory.MainArena, nullptr, "");
    }

    void TearDown() override
    {
        Controller.Cancel(&UI, EditorScene, World);
        World.Shutdown();
        ZEngine::Managers::AssetManager::Shutdown();
        Memory.Shutdown();
    }

    void Update()
    {
        Controller.Update(&UI, EditorScene, World, Camera, Rect, 42, 7);
    }

    EntityID Create(Vec3f position = {}, Vec3f rotation = {}, Vec3f scale = {1.f, 1.f, 1.f})
    {
        const EntityID entity = World.CreateEntity();
        World.AddComponent<TransformComponent>(entity, {.Position = position, .Rotation = rotation, .Scale = scale});
        Systems::SyncHierarchy(World);
        return entity;
    }

    void Select(EntityID entity, GizmoOperation operation = GizmoOperation::Translate)
    {
        ASSERT_TRUE(EditorScene.TransformSession.SetSelection(World, {&entity, 1}, entity));
        Controller.SetOperation(operation);
        Update();
    }

    void Cursor(Vec3f point)
    {
        // Match the actual positive-height Vulkan viewport, not a GL Y convention.
        const Vec4f clip = Camera.GetViewProjection() * Vec4f(point, 1.f);
        UI.MousePos[0]   = Rect[0] + (clip.x / clip.w * .5f + .5f) * (Rect[2] - Rect[0]);
        UI.MousePos[1]   = Rect[1] + (clip.y / clip.w * .5f + .5f) * (Rect[3] - Rect[1]);
    }

    Vec3f AxisPoint(uint32_t axis, float extent = .72f)
    {
        const auto& render = Controller.RenderState();
        return Vec3f{render.Basis(0, 3), render.Basis(1, 3), render.Basis(2, 3)} + Vec3f{render.Basis(0, axis), render.Basis(1, axis), render.Basis(2, axis)} * (render.Scale * extent);
    }

    void Press(Vec3f point)
    {
        Cursor(point);
        UI.MousePressed[0] = true;
        UI.MouseDown[0]    = true;
        Update();
        UI.MousePressed[0] = false;
    }

    void Release()
    {
        UI.MouseDown[0]     = false;
        UI.MouseReleased[0] = true;
        Update();
        UI.MouseReleased[0] = false;
        Controller.UpdateInputCapture(&UI, EditorScene, World);
    }

    EntityID CreatePickable(Vec3f position = {})
    {
        const auto uuid = uuids::uuid::from_string("00000000-0000-0000-0000-000000000860").value();
        if (!ZEngine::Managers::AssetManager::IsRegistered(uuid))
        {
            ZEngine::Importers::AssetMesh mesh;
            mesh.MeshUUID = uuid;
            mesh.Vertices.init(&Memory.MainArena, 24);
            mesh.Indices.init(&Memory.MainArena, 3);
            mesh.SubMeshes.init(&Memory.MainArena, 1);
            const float vertices[] = {-1.f, -1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, -1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
            for (float value : vertices)
                mesh.Vertices.push(value);
            for (uint32_t index = 0; index < 3; ++index)
                mesh.Indices.push(index);
            mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3});
            ZEngine::Importers::AssetNodeHierarchy hierarchy;
            hierarchy.MeshUUID          = uuid;
            hierarchy.NodeHierarchyUUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000861").value();
            ZEngine::Managers::AssetManager::IngestMesh(std::move(mesh), std::move(hierarchy));
        }
        const EntityID entity   = Create(position);
        const uint32_t instance = EditorScene.AddMeshInstance(uuid, "test triangle");
        EditorScene.SetInstanceTransform(instance, World.GetComponent<TransformComponent>(entity)->WorldTransform);
        World.AddComponent<MeshComponent>(entity, {.MeshUUID = uuid, .RenderInstanceId = instance});
        return entity;
    }
};

INSTANTIATE_TEST_SUITE_P(PerspectiveAndOrthographic, EditorGizmoControllerTest, ::testing::Bool());

TEST_P(EditorGizmoControllerTest, PicksWithEmptySelectionAndWithNoGizmoOperation)
{
    const EntityID entity = CreatePickable();
    Press({});
    EXPECT_TRUE(EditorScene.TransformSession.Contains(entity));
    EXPECT_FALSE(Controller.IsManipulating());
    Release();
    EditorScene.ClearSelection();
    Controller.SetOperation(GizmoOperation::Translate);
    Press({});
    EXPECT_TRUE(EditorScene.TransformSession.Contains(entity));
    EXPECT_TRUE(Controller.RenderState().Visible);
}

TEST_P(EditorGizmoControllerTest, ControlClickTogglesSelectionAndEmptyClickClearsIt)
{
    const EntityID first  = CreatePickable();
    const EntityID second = CreatePickable({3.f, 0.f, 0.f});
    Press({});
    Release();
    UI.CtrlDown = true;
    Press({3.f, 0.f, 0.f});
    Release();
    EXPECT_EQ(EditorScene.SelectedActorCount(), 2u);
    Press({});
    Release();
    EXPECT_FALSE(EditorScene.TransformSession.Contains(first));
    EXPECT_TRUE(EditorScene.TransformSession.Contains(second));
    UI.CtrlDown = false;
    Press({-3.f, 0.f, 0.f});
    EXPECT_EQ(EditorScene.SelectedActorCount(), 0u);
}

TEST_P(EditorGizmoControllerTest, PicksTheNearestTriangleWhenTheCameraIsInsideBothBounds)
{
    const EntityID farther = CreatePickable({0.f, 0.f, -2.f});
    const EntityID nearer  = CreatePickable();
    const auto     uuid    = World.GetComponent<MeshComponent>(farther)->MeshUUID;
    auto*          mesh    = ZEngine::Managers::AssetManager::Instance()->GetMeshAsset(uuid);
    ASSERT_NE(mesh, nullptr);
    mesh->BoundsCenter = {};
    mesh->BoundsRadius = 20.f;
    Press({});
    EXPECT_TRUE(EditorScene.TransformSession.Contains(nearer));
    EXPECT_FALSE(EditorScene.TransformSession.Contains(farther));
}

TEST_P(EditorGizmoControllerTest, TranslationPreservesAnArbitrarilyRotatedAndScaledTransform)
{
    const Vec3f    rotation = {-1.045f, .524f, .109f};
    const Vec3f    scale    = {.8f, 1.2f, 1.5f};
    const EntityID entity   = Create({}, rotation, scale);
    Select(entity);
    const Vec3f start = AxisPoint(0);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    Cursor(start + Vec3f{1.f, 0.f, 0.f});
    Release();
    const auto* transform = World.GetComponent<TransformComponent>(entity);
    EXPECT_NEAR(transform->Position.x, 1.f, 1.e-4f);
    EXPECT_NEAR(transform->Position.y, 0.f, 1.e-4f);
    EXPECT_NEAR(transform->Position.z, 0.f, 1.e-4f);
    EXPECT_NEAR(transform->Rotation.x, rotation.x, 1.e-4f);
    EXPECT_NEAR(transform->Rotation.y, rotation.y, 1.e-4f);
    EXPECT_NEAR(transform->Rotation.z, rotation.z, 1.e-4f);
    EXPECT_NEAR(transform->Scale.x, scale.x, 1.e-4f);
    EXPECT_NEAR(transform->Scale.y, scale.y, 1.e-4f);
    EXPECT_NEAR(transform->Scale.z, scale.z, 1.e-4f);
    ASSERT_TRUE(EditorScene.TransformSession.Undo(World));
    EXPECT_EQ(World.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
}

TEST_P(EditorGizmoControllerTest, TranslatesAtTheRenderedYAxisAndIncludesReleaseMovement)
{
    const EntityID entity = Create();
    Select(entity);
    const Vec3f start = AxisPoint(1);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    EXPECT_EQ(Controller.RenderState().Hovered, GizmoHandle::AxisY);
    Cursor(start + Vec3f{0.f, 1.f, 0.f});
    Release();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.y, 1.f, 1.e-4f);
    EXPECT_EQ(EditorScene.TransformSession.History().UndoCount(), 1u);
    ASSERT_TRUE(EditorScene.TransformSession.Undo(World));
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.y, 0.f, 1.e-4f);
    ASSERT_TRUE(EditorScene.TransformSession.Redo(World));
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.y, 1.f, 1.e-4f);
}

TEST_P(EditorGizmoControllerTest, TranslationSnapsAndPresentationFollowsTheSelection)
{
    const EntityID entity = Create();
    Select(entity);
    const Vec3f start = AxisPoint(0);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    UI.CtrlDown = true;
    Cursor(start + Vec3f{.74f, 0.f, 0.f});
    Update();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.x, .5f, 1.e-4f);
    EXPECT_NEAR(Controller.RenderState().Basis(0, 3), .5f, 1.e-4f);
    Release();
    EXPECT_EQ(EditorScene.TransformSession.History().UndoCount(), 1u);
}

TEST_P(EditorGizmoControllerTest, RotatesAboutTheRenderedZRing)
{
    const EntityID entity = Create();
    Select(entity, GizmoOperation::Rotate);
    const float radius = Controller.RenderState().Scale * .82f;
    const float angle  = .6f;
    Press({radius * std::cos(angle), radius * std::sin(angle), 0.f});
    ASSERT_TRUE(Controller.IsManipulating());
    EXPECT_EQ(Controller.RenderState().Hovered, GizmoHandle::AxisZ);
    Cursor({radius * std::cos(angle + .5f), radius * std::sin(angle + .5f), 0.f});
    Release();
    // The engine's authored Euler convention has the opposite Z sign to RH rotation.
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Rotation.z, -.5f, 1.e-4f);
}

TEST_P(EditorGizmoControllerTest, AxisScaleAndCenterUniformScaleAreStable)
{
    const EntityID entity = Create();
    Select(entity, GizmoOperation::Scale);
    const float scale = Controller.RenderState().Scale;
    const Vec3f start = AxisPoint(0);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    Cursor(start + Vec3f{scale * .36f, 0.f, 0.f});
    Release();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Scale.x, 1.5f, 1.e-4f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Scale.y, 1.f, 1.e-4f);
    Press({});
    ASSERT_TRUE(Controller.IsManipulating());
    EXPECT_EQ(Controller.RenderState().Hovered, GizmoHandle::Uniform);
    Update();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Scale.x, 1.5f, 1.e-4f);
    UI.MousePos[0] += 56.f;
    Release();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Scale.x, 2.25f, 1.e-4f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Scale.y, 1.5f, 1.e-4f);
}

TEST_P(EditorGizmoControllerTest, EscapeFocusLossAndHiddenViewportRollBackWithoutHistory)
{
    const EntityID entity = Create();
    Select(entity);
    for (uint32_t reason = 0; reason < 3; ++reason)
    {
        const Vec3f start = AxisPoint(0);
        Press(start);
        ASSERT_TRUE(Controller.IsManipulating());
        Cursor(start + Vec3f{1.f, 0.f, 0.f});
        Update();
        ASSERT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.x, 1.f, 1.e-4f);
        if (reason == 0)
            UI.EscapePressed = true;
        else if (reason == 1)
            ZUISetPointerCaptureWindowFocused(&UI, false);
        else
        {
            UI.ViewportInputKey = 0;
            ZUIProcessPointerCaptureEndOfFrame(&UI);
        }
        Controller.UpdateInputCapture(&UI, EditorScene, World);
        EXPECT_FALSE(Controller.IsManipulating());
        EXPECT_FALSE(EditorScene.TransformSession.IsActive());
        EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.x, 0.f, 1.e-4f);
        EXPECT_EQ(EditorScene.TransformSession.History().UndoCount(), 0u);
        UI.EscapePressed    = false;
        UI.ViewportInputKey = 42;
        if (reason == 1)
        {
            EXPECT_FALSE(UI.MouseDown[0]);
            ZUISetPointerCaptureWindowFocused(&UI, true);
            EXPECT_FALSE(UI.MouseDown[0]);
        }
        else
            UI.MouseDown[0] = false;
        UI.HotKey = 42;
        Controller.UpdateInputCapture(&UI, EditorScene, World);
        Update();
    }
}

TEST_P(EditorGizmoControllerTest, OverlappingWidgetsPopupsAndModalsCannotStartADrag)
{
    const EntityID entity = Create();
    Select(entity);
    const Vec3f start = AxisPoint(0);
    UI.HotKey         = 99;
    Press(start);
    EXPECT_FALSE(Controller.IsManipulating());
    UI.HotKey         = 42;
    UI.PopupStackSize = 1;
    Press(start);
    EXPECT_FALSE(Controller.IsManipulating());
    UI.PopupStackSize = 0;
    UI.ActiveModalKey = 100;
    Press(start);
    EXPECT_FALSE(Controller.IsManipulating());
    EXPECT_FALSE(EditorScene.TransformSession.IsActive());
}

TEST_P(EditorGizmoControllerTest, LocalAxisTranslationRespectsRotationAndScaledUiCoordinates)
{
    const EntityID entity = Create({}, {0.f, 0.f, .6f});
    UI.InputScale[0]      = .5f;
    UI.InputScale[1]      = .5f;
    Camera.SetViewportSize(1600.f, 1200.f);
    (void) Camera.CaptureFrameData();
    Controller.SetCoordinateSpace(EditorGizmoController::CoordinateSpace::Local);
    Select(entity);
    const auto  basis = Controller.RenderState().Basis;
    const Vec3f axis{basis(0, 0), basis(1, 0), basis(2, 0)};
    const Vec3f start = AxisPoint(0);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    Cursor(start + axis);
    Release();
    const auto* transform = World.GetComponent<TransformComponent>(entity);
    EXPECT_NEAR(transform->Position.x, axis.x, 1.e-4f);
    EXPECT_NEAR(transform->Position.y, axis.y, 1.e-4f);
}

TEST_P(EditorGizmoControllerTest, ClickingAHandleWithoutMovementDoesNotCreateAnUndoEntry)
{
    const EntityID entity = Create({}, {.23f, .47f, .51f}, {1.4f, 2.1f, .7f});
    Select(entity);
    Press(AxisPoint(0));
    ASSERT_TRUE(Controller.IsManipulating());
    Update();
    Release();
    EXPECT_EQ(EditorScene.TransformSession.History().UndoCount(), 0u);
    EXPECT_FALSE(EditorScene.IsDirty());
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(entity)->Rotation.y, .47f);
}

TEST_P(EditorGizmoControllerTest, EveryRenderedRingPicksAndRotatesItsOwnAxis)
{
    Camera.SetPosition({4.f, 3.f, 8.f});
    Camera.SetOrientation(18.f, -25.f);
    (void) Camera.CaptureFrameData();
    const EntityID entity = Create();
    Select(entity, GizmoOperation::Rotate);
    const Vec3f axes[] = {
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f}
    };
    const GizmoHandle handles[] = {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ};
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        Update();
        const float radius = Controller.RenderState().Scale * .82f;
        float       angle  = 0.f;
        bool        found  = false;
        for (uint32_t sample = 0; sample < 48; ++sample)
        {
            angle = 6.2831853f * (float(sample) + .5f) / 48.f;
            Cursor((axes[(axis + 1) % 3] * std::cos(angle) + axes[(axis + 2) % 3] * std::sin(angle)) * radius);
            Update();
            if (Controller.RenderState().Hovered == handles[axis])
            {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found) << "axis " << axis;
        Press((axes[(axis + 1) % 3] * std::cos(angle) + axes[(axis + 2) % 3] * std::sin(angle)) * radius);
        ASSERT_TRUE(Controller.IsManipulating());
        Cursor((axes[(axis + 1) % 3] * std::cos(angle + .25f) + axes[(axis + 2) % 3] * std::sin(angle + .25f)) * radius);
        Release();
        Vec3f rotation{};
        rotation[axis]        = -.25f;
        const Mat4f expected  = ComposeTransformMatrix({}, rotation, {1.f, 1.f, 1.f});
        const auto* transform = World.GetComponent<TransformComponent>(entity);
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                EXPECT_NEAR(transform->WorldTransform(row, column), expected(row, column), 1.e-4f) << "axis " << axis;
        ASSERT_TRUE(EditorScene.TransformSession.Undo(World));
    }
}

TEST_P(EditorGizmoControllerTest, MultiSelectionMovesOnlyRootsAndRestoresTheWholeCommand)
{
    const EntityID parent = Create({-1.f, 0.f, 0.f});
    const EntityID child  = Create({0.f, 1.f, 0.f});
    const EntityID other  = Create({1.f, 0.f, 0.f});
    World.AddComponent<ParentComponent>(child, {.Parent = parent});
    Systems::SyncHierarchy(World);
    const EntityID selection[] = {parent, child, other};
    ASSERT_TRUE(EditorScene.TransformSession.SetSelection(World, {selection, 3}, parent));
    Controller.SetOperation(GizmoOperation::Translate);
    Update();
    const Vec3f start = AxisPoint(0);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    EXPECT_EQ(EditorScene.TransformSession.ActiveTargets().size(), 2u);
    Cursor(start + Vec3f{1.f, 0.f, 0.f});
    Release();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(parent)->Position.x, 0.f, 1.e-4f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(other)->Position.x, 2.f, 1.e-4f);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(child)->Position.x, 0.f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 0.f, 1.e-4f);
    ASSERT_TRUE(EditorScene.TransformSession.Undo(World));
    EXPECT_NEAR(World.GetComponent<TransformComponent>(parent)->Position.x, -1.f, 1.e-4f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(other)->Position.x, 1.f, 1.e-4f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), -1.f, 1.e-4f);
}

TEST_P(EditorGizmoControllerTest, PlaneTranslationUsesBothAxes)
{
    const EntityID entity = Create();
    Select(entity);
    const float scale = Controller.RenderState().Scale;
    const Vec3f start{scale * .39f, scale * .39f, 0.f};
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    EXPECT_EQ(Controller.RenderState().Hovered, GizmoHandle::PlaneXY);
    Cursor(start + Vec3f{.5f, .75f, 0.f});
    Release();
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.x, .5f, 1.e-4f);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.y, .75f, 1.e-4f);
}

TEST_P(EditorGizmoControllerTest, NativeReleaseEventCommitsTheLatestCursorWithoutAViewportUpdate)
{
    const EntityID entity = Create();
    Select(entity);
    const Vec3f start = AxisPoint(0);
    Press(start);
    ASSERT_TRUE(Controller.IsManipulating());
    Cursor(start + Vec3f{.75f, 0.f, 0.f});
    UI.MouseReleased[0] = true;
    ZUIProcessPointerCaptureEndOfFrame(&UI);
    Controller.UpdateInputCapture(&UI, EditorScene, World);
    EXPECT_FALSE(Controller.IsManipulating());
    EXPECT_FALSE(UI.PointerCaptureEnd.Pending);
    EXPECT_NEAR(World.GetComponent<TransformComponent>(entity)->Position.x, .75f, 1.e-4f);
    EXPECT_EQ(EditorScene.TransformSession.History().UndoCount(), 1u);
}
