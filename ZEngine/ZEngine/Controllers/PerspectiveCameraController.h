#pragma once
#include <ZEngine/Controllers/ICameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Cameras/PerspectiveCamera.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>
#include <mutex>

namespace ZEngine::Controllers
{

    class PerspectiveCameraController : public ICameraController, public Windows::Inputs::IMouseEventCallback
    {
    public:
        PerspectiveCameraController();
        virtual ~PerspectiveCameraController() = default;

        void                          Update(Core::TimeStep) override;
        bool                          OnEvent(Core::CoreEvent&) override;

        Rendering::Cameras::CameraPtr GetCamera() const override;

        virtual Core::Maths::Vec3f    GetPosition() const override;
        virtual void                  SetPosition(const Core::Maths::Vec3f& position) override;

        void                          SetViewport(float width, float height);
        void                          SetTarget(const Core::Maths::Vec3f& target);

        virtual void                  ResumeEventProcessing();
        virtual void                  PauseEventProcessing();

    public:
        bool OnMouseButtonPressed(Windows::Events::MouseButtonPressedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonReleased(Windows::Events::MouseButtonReleasedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonMoved(Windows::Events::MouseButtonMovedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonWheelMoved(Windows::Events::MouseButtonWheelEvent&) override;

    protected:
        float                m_camera_fov                                   = 90.0f;
        float                m_camera_near                                  = 0.1f;
        float                m_camera_far                                   = 1000.0f;
        bool                 m_process_event                                = true;
        Core::Maths::Vec3f   m_camera_target                                = {0.0f, 0.0f, 0.0f};
        std::recursive_mutex m_event_mutex                                  = {};
        ZRawPtr(Rendering::Cameras::PerspectiveCamera) m_perspective_camera = nullptr;
    };
} // namespace ZEngine::Controllers
