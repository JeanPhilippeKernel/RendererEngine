#include <Controllers/PerspectiveCameraController.h>
#include <Core/EventDispatcher.h>
#include <Inputs/IDevice.h>
#include <Inputs/KeyCodeDefinition.h>
#include <Inputs/Keyboard.h>
#include <Inputs/Mouse.h>

using namespace ZEngine;
using namespace ZEngine::Helpers;
using namespace ZEngine::Windows::Inputs;
using namespace ZEngine::Windows::Events;

namespace ZEngine::Controllers
{
    PerspectiveCameraController::PerspectiveCameraController() {}
    void PerspectiveCameraController::Update(Core::TimeStep dt)
    {
        if (auto window = m_window)
        {
            if (!m_process_event)
            {
                return;
            }

            const auto mouse_position = IDevice::As<Mouse>()->GetMousePosition(window);
            const auto mouse          = Core::Maths::Vec2f(mouse_position[0], mouse_position[1]);
            bool       mouse_pressed  = false;
            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_ALT, window))
            {
                m_perspective_camera->Movement.MousePan    = IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_MIDDLE, window);
                m_perspective_camera->Movement.MouseRotate = IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_LEFT, window);
                m_perspective_camera->Movement.MouseZoom   = IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_RIGHT, window);
                mouse_pressed                              = true;
            }
            m_perspective_camera->Update(dt, mouse, mouse_pressed);
        }
    }

    Core::Maths::Vec3f PerspectiveCameraController::GetPosition() const
    {
        return m_perspective_camera->GetPosition();
    }

    void PerspectiveCameraController::SetPosition(const Core::Maths::Vec3f& position)
    {
        m_perspective_camera->SetPosition(position);
    }

    float PerspectiveCameraController::GetFieldOfView() const
    {
        return m_perspective_camera->Fov;
    }

    void PerspectiveCameraController::SetFieldOfView(float rad_fov)
    {
        m_perspective_camera->Fov = rad_fov;
    }

    float PerspectiveCameraController::GetNear() const
    {
        return m_perspective_camera->ClipNear;
    }

    void PerspectiveCameraController::SetNear(float value)
    {
        m_perspective_camera->ClipNear = value;
    }

    float PerspectiveCameraController::GetFar() const
    {
        return m_perspective_camera->ClipFar;
    }
    void PerspectiveCameraController::SetFar(float value)
    {
        m_perspective_camera->ClipFar = value;
    }

    void PerspectiveCameraController::SetViewport(float width, float height)
    {
        m_perspective_camera->SetViewport(width, height);
    }

    void PerspectiveCameraController::SetTarget(const Core::Maths::Vec3f& target)
    {
        m_perspective_camera->SetTarget(target);
    }

    void PerspectiveCameraController::ResumeEventProcessing()
    {
        {
            std::lock_guard l(m_event_mutex);
            m_process_event = true;
        }
    }

    void PerspectiveCameraController::PauseEventProcessing()
    {
        {
            std::lock_guard l(m_event_mutex);
            m_process_event = false;
        }
    }

    ZRawPtr(Rendering::Cameras::Camera) PerspectiveCameraController::GetCamera() const
    {
        return m_perspective_camera;
    }

    void PerspectiveCameraController::UpdateProjectionMatrix() {}

    bool PerspectiveCameraController::OnEvent(Core::CoreEvent& e)
    {
        if (!m_process_event)
        {
            return false;
        }

        Core::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseButtonWheelEvent>(std::bind(&PerspectiveCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        return false;
    }

    bool PerspectiveCameraController::OnMouseButtonWheelMoved(MouseButtonWheelEvent& e)
    {
        float delta = e.GetOffetY() * 0.1f;
        m_perspective_camera->Zoom(delta);
        return false;
    }

} // namespace ZEngine::Controllers
