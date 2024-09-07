#pragma once
#include <Controllers/ICameraController.h>
#include <Inputs/IMouseEventCallback.h>
#include <Rendering/Cameras/PerspectiveCamera.h>
#include <Window/ICoreWindowEventCallback.h>
#include <mutex>

namespace ZEngine::Controllers
{

    class PerspectiveCameraController : public ICameraController, public Inputs::IMouseEventCallback, public Window::ICoreWindowEventCallback
    {
    public:
        explicit PerspectiveCameraController() : m_perspective_camera(new Rendering::Cameras::PerspectiveCamera(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far))
        {
            m_position        = {0.0f, 0.0f, 1.5f};
            m_controller_type = CameraControllerType::PERSPECTIVE_CONTROLLER;
            m_process_event   = true;
        }

        explicit PerspectiveCameraController(Rendering::Cameras::PerspectiveCamera* const camera) : m_perspective_camera(camera)
        {
            m_position        = {0.0f, 0.0f, 1.5f};
            m_controller_type = CameraControllerType::PERSPECTIVE_CONTROLLER;
            m_process_event   = true;
        }

        explicit PerspectiveCameraController(float aspect_ratio, Rendering::Cameras::PerspectiveCamera* const camera)
            : ICameraController(aspect_ratio), m_perspective_camera(camera)
        {
            m_position        = {0.0f, 0.0f, 1.5f};
            m_controller_type = CameraControllerType::PERSPECTIVE_CONTROLLER;
            m_process_event   = true;
        }

        explicit PerspectiveCameraController(float aspect_ratio)
            : ICameraController(aspect_ratio), m_perspective_camera(new Rendering::Cameras::PerspectiveCamera(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far))
        {
            m_position        = {0.0f, 0.0f, 1.5f};
            m_controller_type = CameraControllerType::PERSPECTIVE_CONTROLLER;
            m_process_event   = true;
        }

        virtual ~PerspectiveCameraController() = default;

        void Initialize() override;
        void Update(Core::TimeStep) override;
        bool OnEvent(Event::CoreEvent&) override;

        const ZEngine::Ref<Rendering::Cameras::Camera> GetCamera() const override;

        void UpdateProjectionMatrix() override;

        virtual glm::vec3 GetPosition() const override;
        virtual void      SetPosition(const glm::vec3& position) override;

        virtual float GetFieldOfView() const;
        virtual void  SetFieldOfView(float rad_fov);

        virtual float GetNear() const;
        virtual void  SetNear(float value);

        virtual float GetFar() const;
        virtual void  SetFar(float value);

        void SetViewport(float width, float height);
        void SetTarget(const glm::vec3& target);

        virtual void ResumeEventProcessing();
        virtual void PauseEventProcessing();

    public:
        bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override;

        bool OnWindowResized(Event::WindowResizedEvent&) override
        {
            return false;
        }

        bool OnWindowClosed(Event::WindowClosedEvent&) override
        {
            return false;
        }

        bool OnWindowMinimized(Event::WindowMinimizedEvent&) override
        {
            return false;
        }

        bool OnWindowMaximized(Event::WindowMaximizedEvent&) override
        {
            return false;
        }

        bool OnWindowRestored(Event::WindowRestoredEvent&) override
        {
            return false;
        }

    protected:
        float                                               m_camera_fov{90.0f};
        float                                               m_camera_near{1.f};
        float                                               m_camera_far{1000.0f};
        std::recursive_mutex                                m_event_mutex;
        bool                                                m_process_event{true};
        glm::vec3                                           m_camera_target{0.0f, 0.0f, 0.0f};
        ZEngine::Ref<Rendering::Cameras::PerspectiveCamera> m_perspective_camera;
    };
} // namespace ZEngine::Controllers
