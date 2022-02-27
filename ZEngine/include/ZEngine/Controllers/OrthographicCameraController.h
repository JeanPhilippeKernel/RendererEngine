#pragma once
#include <Controllers/ICameraController.h>
#include <Rendering/Cameras/OrthographicCamera.h>
#include <Core/TimeStep.h>
#include <Inputs/IMouseEventCallback.h>
#include <Window/ICoreWindowEventCallback.h>

namespace ZEngine::Controllers {

    class OrthographicCameraController : public ICameraController, public Inputs::IMouseEventCallback, public Window::ICoreWindowEventCallback {

    public:
        OrthographicCameraController() = default;
        OrthographicCameraController(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window, bool can_rotate = false)
            : ICameraController(window, can_rotate),
              m_orthographic_camera(new Rendering::Cameras::OrthographicCamera(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio * m_zoom_factor, -m_zoom_factor, m_zoom_factor)) {}

        OrthographicCameraController(float aspect_ratio)
            : ICameraController(aspect_ratio),
              m_orthographic_camera(new Rendering::Cameras::OrthographicCamera(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio * m_zoom_factor, -m_zoom_factor, m_zoom_factor)) {}

        virtual ~OrthographicCameraController() = default;

        void Initialize() override;
        void Update(Core::TimeStep) override;
        bool OnEvent(Event::CoreEvent&) override;

        const ZEngine::Ref<Rendering::Cameras::Camera> GetCamera() const override {
            return m_orthographic_camera;
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

    private:
        ZEngine::Ref<Rendering::Cameras::OrthographicCamera> m_orthographic_camera;
    };
} // namespace ZEngine::Controllers
