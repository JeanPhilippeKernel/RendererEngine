#pragma once

#include <Controllers/PerspectiveCameraController.h>
#include <Rendering/Cameras/OrbitCamera.h>

namespace ZEngine::Controllers {

    class OrbitCameraController : public PerspectiveCameraController {

    public:
        OrbitCameraController() {
            m_controller_type = CameraControllerType::PERSPECTIVE_ORBIT_CONTROLLER;
        }

        OrbitCameraController(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window, Maths::Vector3 position, float yaw_angle_degree, float pitch_angle_degree)
            : PerspectiveCameraController(window) {
            m_controller_type = CameraControllerType::PERSPECTIVE_ORBIT_CONTROLLER;
            m_position           = position;
            m_perspective_camera = CreateRef<Rendering::Cameras::OrbitCamera>(
                m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, Maths::radians(yaw_angle_degree), Maths::radians(pitch_angle_degree));
        }

        OrbitCameraController(float aspect_ratio, Maths::Vector3 position, float yaw_angle_degree, float pitch_angle_degree) : PerspectiveCameraController(aspect_ratio) {
            m_controller_type    = CameraControllerType::PERSPECTIVE_ORBIT_CONTROLLER;
            m_position           = position;
            m_perspective_camera = CreateRef<Rendering::Cameras::OrbitCamera>(
                m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, Maths::radians(yaw_angle_degree), Maths::radians(pitch_angle_degree));
        }

        virtual ~OrbitCameraController() = default;

        void Initialize() override;
        void Update(Core::TimeStep) override;
        bool OnEvent(Event::CoreEvent&) override;

    public:
        bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override;
        bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override;

    protected:
        Maths::Vector2 m_mouse_cursor_pos{0.f, 0.0f};
    };
} // namespace ZEngine::Controllers
