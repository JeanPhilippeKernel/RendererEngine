#pragma once
#include <ZEngineDef.h>
#include <Controllers/ICameraController.h>

namespace ZEngine::Rendering::Components {
    struct CameraComponent {
        CameraComponent(Ref<Controllers::ICameraController>&& controller) {
            m_camera_controller = controller;
            m_camera_controller->Initialize();
        }

        CameraComponent(Controllers::ICameraController* const controller) : m_camera_controller(controller) {
            m_camera_controller->Initialize();
        }
        ~CameraComponent() = default;

        Ref<Cameras::Camera> GetCamera() const {
            return m_camera_controller->GetCamera();
        }

        Controllers::ICameraController* GetCameraController() {
            return m_camera_controller.get();
        }

        bool IsPrimaryCamera{true};

    private:
        Ref<Controllers::ICameraController> m_camera_controller;
    };
} // namespace ZEngine::Rendering::Components
