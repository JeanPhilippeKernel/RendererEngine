#pragma once
#include <Controllers/ICameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Cameras/PerspectiveCamera.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>
#include <mutex>

namespace Tetragrama::Controllers
{

    class PerspectiveCameraController : public ICameraController, public ZEngine::Windows::Inputs::IMouseEventCallback
    {
    public:
        PerspectiveCameraController();
        virtual ~PerspectiveCameraController() = default;

        void Update(ZEngine::Core::TimeStep) override;
        bool OnEvent(ZEngine::Core::CoreEvent&) override;

        ZRawPtr(ZEngine::Rendering::Cameras::Camera) GetCamera() const override;

        void                                UpdateProjectionMatrix() override;

        virtual ZEngine::Core::Maths::Vec3f GetPosition() const override;
        virtual void                        SetPosition(const ZEngine::Core::Maths::Vec3f& position) override;

        virtual float                       GetFieldOfView() const;
        virtual void                        SetFieldOfView(float rad_fov);

        virtual float                       GetNear() const;
        virtual void                        SetNear(float value);

        virtual float                       GetFar() const;
        virtual void                        SetFar(float value);

        void                                SetViewport(float width, float height);
        void                                SetTarget(const ZEngine::Core::Maths::Vec3f& target);

        virtual void                        ResumeEventProcessing();
        virtual void                        PauseEventProcessing();

    public:
        bool OnMouseButtonPressed(ZEngine::Windows::Events::MouseButtonPressedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonReleased(ZEngine::Windows::Events::MouseButtonReleasedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonMoved(ZEngine::Windows::Events::MouseButtonMovedEvent&) override
        {
            return false;
        }

        bool OnMouseButtonWheelMoved(ZEngine::Windows::Events::MouseButtonWheelEvent&) override;

    protected:
        float                       m_camera_fov                                     = 90.0f;
        float                       m_camera_near                                    = 1.f;
        float                       m_camera_far                                     = 1000.0f;
        bool                        m_process_event                                  = true;
        ZEngine::Core::Maths::Vec3f m_camera_target                                  = {0.0f, 0.0f, 0.0f};
        std::recursive_mutex        m_event_mutex                                    = {};
        ZRawPtr(ZEngine::Rendering::Cameras::PerspectiveCamera) m_perspective_camera = nullptr;
    };
} // namespace Tetragrama::Controllers
