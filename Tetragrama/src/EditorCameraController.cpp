#include <EditorCameraController.h>

using namespace ZEngine::Inputs;
using namespace ZEngine::Rendering::Cameras;

namespace Tetragrama
{
    EditorCameraController::EditorCameraController(double distance, float yaw_angle_degree, float pitch_angle_degree) : PerspectiveCameraController(0.0f)
    {
        m_process_event      = true;
        m_controller_type    = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;
        m_perspective_camera = ZEngine::CreateRef<PerspectiveCamera>(
            m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, ZEngine::Maths::radians(yaw_angle_degree), ZEngine::Maths::radians(pitch_angle_degree));
        m_perspective_camera->SetDistance(distance);
    }
} // namespace Tetragrama
