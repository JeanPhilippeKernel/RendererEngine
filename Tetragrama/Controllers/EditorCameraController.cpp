#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Scenes/SceneRayQuery.h>
#include <cmath>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Core::Maths;

namespace
{
    ZEngine::Rendering::Scenes::SceneRaycastHit RaycastEditorScene(void* context, Vec3f origin, Vec3f direction, float max_distance)
    {
        ZEngine::Rendering::Scenes::SceneRaycastHit miss = {};
        miss.Distance                                    = max_distance;

        auto* app                                        = static_cast<ZEngine::Applications::GameApplication*>(context);
        if (!app || !app->CurrentScene)
            return miss;

        return ZEngine::Rendering::Scenes::RaycastSceneBounds(*app->CurrentScene, origin, direction, max_distance);
    }

    bool GetEditorSelectionBounds(void* context, Vec3f& out_center, float& out_radius)
    {
        using namespace ZEngine::Rendering::Scenes;

        auto* app = static_cast<ZEngine::Applications::GameApplication*>(context);
        if (!app || !app->CurrentScene)
            return false;

        auto* scene = static_cast<Tetragrama::EditorScene*>(app->CurrentScene);
        if (!scene || !scene->SelectedActorHandle.Valid())
            return false;

        auto* engine = ZEngine::Engine::GetContext();
        if (!engine || !engine->ActorManager)
            return false;

        auto* actor = engine->ActorManager->Access(scene->SelectedActorHandle);
        if (!actor)
            return false;

        const auto* mesh = actor->GetComponent<ZEngine::ECS::Components::MeshComponent>();
        if (!mesh || mesh->RenderInstanceId == UINT32_MAX)
            return false;

        SceneRaycastBounds bounds = {};
        if (!TryGetSceneInstanceBounds(*scene, mesh->RenderInstanceId, bounds))
            return false;

        out_center = bounds.Center;
        out_radius = bounds.Radius;
        return true;
    }

    bool GetEditorSceneBounds(void* context, Vec3f& out_center, float& out_radius)
    {
        using namespace ZEngine::Rendering::Scenes;

        auto* app = static_cast<ZEngine::Applications::GameApplication*>(context);
        if (!app || !app->CurrentScene)
            return false;

        SceneRaycastBounds bounds = {};
        if (!TryGetSceneBounds(*app->CurrentScene, bounds))
            return false;

        out_center = bounds.Center;
        out_radius = bounds.Radius;
        return true;
    }

    bool GetEditorAtmosphereGroundConstraint(void* context, Vec3f& out_center, float& out_radius)
    {
        auto* editor = static_cast<Tetragrama::Editor*>(context);
        if (!editor || !editor->ConstrainCameraToAtmosphereGround || !editor->CurrentScene)
            return false;

        auto* scene = static_cast<Tetragrama::EditorScene*>(editor->CurrentScene);
        if (!scene || !scene->Sky.IsAtmosphere())
            return false;

        const auto& atmosphere                = scene->Sky.Atmosphere;
        const float world_units_per_kilometer = atmosphere.WorldUnitsPerMeter * 1000.0f;
        const float radius                    = atmosphere.PlanetRadiusKilometers * world_units_per_kilometer;
        if (!std::isfinite(atmosphere.PlanetCenterWorld[0]) || !std::isfinite(atmosphere.PlanetCenterWorld[1]) || !std::isfinite(atmosphere.PlanetCenterWorld[2]) || !std::isfinite(radius) || radius <= 0.0f)
            return false;

        out_center = {atmosphere.PlanetCenterWorld[0], atmosphere.PlanetCenterWorld[1], atmosphere.PlanetCenterWorld[2]};
        out_radius = radius;
        return true;
    }
} // namespace

namespace Tetragrama::Controllers
{
    void EditorCameraController::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, ZEngine::Input::InputManager* input_manager, ZEngine::Applications::GameApplication* app)
    {
        m_window                     = window;
        m_controller_type            = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;

        const auto&   props          = window->GetWindowProperty();
        float         logicalW       = static_cast<float>(props.Width);
        float         logicalH       = static_cast<float>(props.Height);

        CameraSetting settings       = {};
        settings.FOV                 = 60.0f;
        settings.NearPlane           = 0.1f;
        settings.FarPlane            = 10000.0f;
        settings.SmoothingFactor     = 12.0f;
        settings.MinMoveSpeed        = 1.0f;
        settings.MaxMoveSpeed        = 500.0f;
        settings.PanSpeed            = 1.0f;
        settings.RotationSpeed       = 0.25f;
        settings.OrbitSpeed          = 0.25f;
        settings.FastSpeedMultiplier = 4.0f;
        settings.ScrollSpeed         = 0.5f;
        settings.FocusDuration       = 0.25f;
        settings.MinOrbitDistance    = 0.5f;
        settings.MaxOrbitDistance    = 10000.0f;

        m_camera                     = ZPushStructCtorArgs(arena, FlyCamera, logicalW / logicalH, settings);
        m_camera->SetViewportSize(logicalW, logicalH);

        m_camera->Hooks.Context             = app;
        m_camera->Hooks.Raycast             = &RaycastEditorScene;
        m_camera->Hooks.GetSelectionBounds  = &GetEditorSelectionBounds;
        m_camera->Hooks.GetSceneBounds      = &GetEditorSceneBounds;
        m_camera->Hooks.GetGroundConstraint = &GetEditorAtmosphereGroundConstraint;

        FlyCameraController::Initialize(input_manager, arena);
    }
} // namespace Tetragrama::Controllers
