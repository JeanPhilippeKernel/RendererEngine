#pragma once
#include <Controllers/ICameraController.h>
#include <Core/Memory/Allocator.h>
#include <Rendering/Cameras/FlyCamera.h>

namespace ZEngine::Controllers
{
    struct FlyCameraController : public ICameraController, public Windows::Inputs::IMouseEventCallback, public Windows::Inputs::IKeyboardEventCallback
    {
        FlyCameraController()          = default;
        virtual ~FlyCameraController() = default;

        void                          Update(Core::TimeStep) override;
        bool                          OnEvent(Core::CoreEvent&) override;

        Rendering::Cameras::CameraPtr GetCamera() const override;
        virtual Core::Maths::Vec3f    GetPosition() const override;
        virtual void                  SetPosition(const Core::Maths::Vec3f& position) override;

        void                          SetViewport(float width, float height);
        void                          ResumeEventProcessing();
        void                          PauseEventProcessing();

        virtual bool                  OnMouseButtonPressed(Windows::Events::MouseButtonPressedEvent&) override;
        virtual bool                  OnMouseButtonReleased(Windows::Events::MouseButtonReleasedEvent&) override;
        virtual bool                  OnMouseButtonMoved(Windows::Events::MouseButtonMovedEvent&) override;
        virtual bool                  OnMouseButtonWheelMoved(Windows::Events::MouseButtonWheelEvent&) override;

        virtual bool                  OnKeyPressed(Windows::Events::KeyPressedEvent&) override;
        virtual bool                  OnKeyReleased(Windows::Events::KeyReleasedEvent&) override;

    protected:
        PaddedAtomic<bool>               m_process_event = {.value = false};
        Rendering::Cameras::FlyCameraPtr m_camera        = nullptr;
    };
} // namespace ZEngine::Controllers