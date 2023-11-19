#include <pch.h>
#include <Controllers/PerspectiveCameraController.h>
#include <Inputs/KeyCodeDefinition.h>
#include <Inputs/IDevice.h>
#include <Inputs/Keyboard.h>
#include <Inputs/Mouse.h>
#include <Event/EventDispatcher.h>

using namespace ZEngine::Inputs;

namespace ZEngine::Controllers
{

    void PerspectiveCameraController::Initialize()
    {
    }

    void PerspectiveCameraController::Update(Core::TimeStep dt)
    {
        if (auto window = m_window.lock())
        {
            const auto mouse_position = IDevice::As<Mouse>()->GetMousePosition(window);
            const auto mouse          = glm::vec2(mouse_position[0], mouse_position[1]);
            bool       mouse_pressed  = false;
            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_ALT, window))
            {
                m_perspective_camera->movement_.mousePan    = IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_MIDDLE, window);
                m_perspective_camera->movement_.mouseRotate = IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_LEFT, window);
                m_perspective_camera->movement_.mouseZoom   = IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_RIGHT, window);
                mouse_pressed                               = true;
            }
            else
            {
                m_perspective_camera->movement_.forward_  = IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_W, window);
                m_perspective_camera->movement_.backward_ = IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_S, window);
                m_perspective_camera->movement_.left_     = IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_A, window);
                m_perspective_camera->movement_.right_    = IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_D, window);
                mouse_pressed                             = false;
            }
            m_perspective_camera->Update(dt, mouse, mouse_pressed);
        }
    }

    glm::vec3 PerspectiveCameraController::GetPosition() const
    {
        return m_perspective_camera->GetPosition();
    }

    void PerspectiveCameraController::SetPosition(const glm::vec3& position)
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

    void PerspectiveCameraController::SetTarget(const glm::vec3& target)
    {
        m_perspective_camera->SetTarget(target);
    }

    const ZEngine::Ref<Rendering::Cameras::Camera> PerspectiveCameraController::GetCamera() const
    {
        return m_perspective_camera;
    }

    void PerspectiveCameraController::UpdateProjectionMatrix()
    {
    }

    bool PerspectiveCameraController::OnEvent(Event::CoreEvent& e)
    {
        Event::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&PerspectiveCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        return false;
    }

    bool PerspectiveCameraController::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e)
    {
        float delta = e.GetOffetY() * 0.1f;
        m_perspective_camera->Zoom(delta);
        return false;
    }

} // namespace ZEngine::Controllers
