#include <EditorCameraController.h>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Helpers;

namespace Tetragrama::Controllers
{
    void EditorCameraController::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, double distance, float yaw_degree, float pitch_degree)
    {
        m_position           = {0.0f, 0.0f, 1.5f};
        m_process_event      = true;
        m_controller_type    = Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;
        m_window             = window;

        m_perspective_camera = ZPushStructCtor(arena, PerspectiveCamera);
        m_perspective_camera->Initialize(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, glm::radians(yaw_degree), glm::radians(pitch_degree));
        m_perspective_camera->SetDistance(distance);
    }
} // namespace Tetragrama::Controllers
