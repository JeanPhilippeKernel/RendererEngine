#pragma once
#include <Controllers/ICameraController.h>
#include <Rendering/Cameras/PerspectiveCamera.h>
#include <Core/TimeStep.h>
#include <Inputs/IMouseEventCallback.h>
#include <Window/ICoreWindowEventCallback.h>

namespace ZEngine::Controllers {

    class PerspectiveCameraController : public ICameraController, public Inputs::IMouseEventCallback, public Window::ICoreWindowEventCallback {

    public:
        explicit PerspectiveCameraController() = default;
        explicit PerspectiveCameraController(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window)
            : ICameraController(window), m_perspective_camera(new Rendering::Cameras::PerspectiveCamera(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far)) {
            m_position = {0.0f, 0.0f, 1.5f};
        }

        explicit PerspectiveCameraController(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window, Rendering::Cameras::PerspectiveCamera* const camera)
            : ICameraController(window), m_perspective_camera(camera) {
            m_position = {0.0f, 0.0f, 1.5f};
        }

        explicit PerspectiveCameraController(float aspect_ratio)
            : ICameraController(aspect_ratio), m_perspective_camera(new Rendering::Cameras::PerspectiveCamera(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far)) {
            m_position = {0.0f, 0.0f, 1.5f};
        }

        virtual ~PerspectiveCameraController() = default;

        void Initialize() override;
        void Update(Core::TimeStep) override;
        bool OnEvent(Event::CoreEvent&) override;

        const ZEngine::Ref<Rendering::Cameras::Camera> GetCamera() const override {
            return m_perspective_camera;
        }

        void UpdateProjectionMatrix() override;

        virtual const Maths::Vector3& GetPosition() const override;
        virtual void                  SetPosition(const Maths::Vector3& position) override;

    public:
        bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&) override {
            return false;
        }

        bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&) override {
            return false;
        }

        bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override {
            return false;
        }

        bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override;

        bool OnWindowResized(Event::WindowResizedEvent&) override {
            return false;
        }

        bool OnWindowClosed(Event::WindowClosedEvent&) override {
            return false;
        }

        bool OnWindowMinimized(Event::WindowMinimizedEvent&) override {
            return false;
        }

        bool OnWindowMaximized(Event::WindowMaximizedEvent&) override {
            return false;
        }

        bool OnWindowRestored(Event::WindowRestoredEvent&) override {
            return false;
        }

    protected:
        float          m_camera_fov{Maths::half_pi<float>()};
        float          m_camera_near{0.1f};
        float          m_camera_far{3000.0f};
        Maths::Vector3 m_camera_target{0.0f, 0.0f, 0.0f};

        ZEngine::Ref<Rendering::Cameras::PerspectiveCamera> m_perspective_camera;
    };
} // namespace ZEngine::Controllers
