#pragma once

#include <Controllers/PerspectiveCameraController.h>
#include <Rendering/Cameras/FirstPersonShooterCamera.h>

namespace ZEngine::Controllers {

    class FirstPersonShooterCameraController : public PerspectiveCameraController {

    public:
        explicit FirstPersonShooterCameraController() = default;
        explicit FirstPersonShooterCameraController(
            const ZEngine::Ref<ZEngine::Window::CoreWindow>& window, Maths::Vector3 position, float yaw_angle_degree, float pitch_angle_degree)
            : PerspectiveCameraController(window) {
            m_perspective_camera.reset(new Rendering::Cameras::FirstPersonShooterCamera(
                m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, Maths::radians(yaw_angle_degree), Maths::radians(pitch_angle_degree)));
            m_position = position;
        }

        virtual ~FirstPersonShooterCameraController() = default;

        void Initialize() override;
        void Update(Core::TimeStep) override;
        bool OnEvent(Event::CoreEvent&) override;

    public:
        bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override {
            return false;
        }

        bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override;
    };
} // namespace ZEngine::Controllers
