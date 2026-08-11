#include <Tetragrama/Controllers/EditorCameraController.h>
#include <ZEngine/Core/Maths/MathUtils.h>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Core::Maths;

namespace Tetragrama::Controllers
{
    void EditorCameraController::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, ZEngine::Input::InputManager* input_manager)
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
        settings.ScrollSpeed         = 3.0f;
        settings.FocusDuration       = 0.25f;
        settings.MinOrbitDistance    = 0.5f;
        settings.MaxOrbitDistance    = 10000.0f;

        m_camera                     = ZPushStructCtorArgs(arena, FlyCamera, logicalW / logicalH, settings);
        m_camera->SetViewportSize(logicalW, logicalH);

        m_camera->Hooks.Raycast            = [](Vec3f, Vec3f, float maxDist) { return maxDist; };
        m_camera->Hooks.GetSelectionBounds = []() -> std::pair<Vec3f, float> {
            return {
                Vec3f{0.0f, 0.0f, 0.0f},
                5.0f
            };
        };

        FlyCameraController::Initialize(input_manager, arena);
    }
} // namespace Tetragrama::Controllers
