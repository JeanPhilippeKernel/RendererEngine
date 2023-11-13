#pragma once

#include <Controllers/PerspectiveCameraController.h>
#include <Rendering/Cameras/FirstPersonShooterCamera.h>

namespace ZEngine::Controllers
{

    class FirstPersonShooterCameraController : public PerspectiveCameraController
    {
    public:
        explicit FirstPersonShooterCameraController()
        {
            m_controller_type = CameraControllerType::PERSPECTIVE_FPS_CONTROLLER;
        }

        explicit FirstPersonShooterCameraController(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window, glm::vec3 position, float yaw_angle_degree, float pitch_angle_degree)
            : PerspectiveCameraController(window)
        {
            m_perspective_camera.reset(new Rendering::Cameras::FirstPersonShooterCamera(
                m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, glm::radians(yaw_angle_degree), glm::radians(pitch_angle_degree)));
            m_position        = position;
            m_controller_type = CameraControllerType::PERSPECTIVE_FPS_CONTROLLER;
        }

        virtual ~FirstPersonShooterCameraController() = default;

        void Initialize() override;
        void Update(Core::TimeStep) override;
        bool OnEvent(Event::CoreEvent&) override;

    public:
        bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override
        {
            return false;
        }

        bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override;
    };
} // namespace ZEngine::Controllers
