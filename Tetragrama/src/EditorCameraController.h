#pragma once
#include <ZEngine.h>
#include <EditorCamera.h>

namespace Tetragrama {

    class EditorCameraController : public ZEngine::Controllers::PerspectiveCameraController {

    public:
        EditorCameraController() {
            m_controller_type = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;
        }

        EditorCameraController(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window, float distance, float yaw_angle_degree, float pitch_angle_degree)
            : PerspectiveCameraController(window) {
            m_controller_type    = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;
            m_perspective_camera = ZEngine::CreateRef<EditorCamera>(
                m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, ZEngine::Maths::radians(yaw_angle_degree), ZEngine::Maths::radians(pitch_angle_degree));

            auto const camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
            camera->SetDistance(distance);
        }

        EditorCameraController(float aspect_ratio, float distance, float yaw_angle_degree, float pitch_angle_degree) : PerspectiveCameraController(aspect_ratio) {
            m_controller_type    = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;
            m_perspective_camera = ZEngine::CreateRef<EditorCamera>(
                m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far, ZEngine::Maths::radians(yaw_angle_degree), ZEngine::Maths::radians(pitch_angle_degree));

            auto const camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
            camera->SetDistance(distance);
        }

        virtual ~EditorCameraController() = default;

        void Initialize() override;
        void Update(ZEngine::Core::TimeStep) override;
        bool OnEvent(ZEngine::Event::CoreEvent&) override;

        void SetViewportSize(float width, float height);

    public:
        bool OnMouseButtonWheelMoved(ZEngine::Event::MouseButtonWheelEvent&) override;

    private:
        glm::vec2 m_initial_mouse_position = {0.0f, 0.0f};
    };
} // namespace Tetragrama
