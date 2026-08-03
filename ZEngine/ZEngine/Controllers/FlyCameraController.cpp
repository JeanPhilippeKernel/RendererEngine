#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <ZEngine/Windows/Inputs/Keyboard.h>
#include <ZEngine/Windows/Inputs/Mouse.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Windows::Inputs;
using namespace ZEngine::Windows::Events;

namespace ZEngine::Controllers
{
    void FlyCameraController::Update(Core::TimeStep dt)
    {
        m_camera->OnUpdate(dt);
    }

    bool FlyCameraController::OnEvent(Core::CoreEvent& e)
    {
        if (!m_process_event.value.load(std::memory_order_acquire))
        {
            return false;
        }

        Core::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseButtonWheelEvent>(std::bind(&FlyCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseButtonReleasedEvent>(std::bind(&FlyCameraController::OnMouseButtonReleased, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&FlyCameraController::OnMouseButtonPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseButtonMovedEvent>(std::bind(&FlyCameraController::OnMouseButtonMoved, this, std::placeholders::_1));

        dispatcher.Dispatch<KeyPressedEvent>(std::bind(&FlyCameraController::OnKeyPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<KeyReleasedEvent>(std::bind(&FlyCameraController::OnKeyReleased, this, std::placeholders::_1));
        return false;
    }

    Rendering::Cameras::CameraPtr FlyCameraController::GetCamera() const
    {
        return m_camera;
    }

    Core::Maths::Vec3f FlyCameraController::GetPosition() const
    {
        return m_camera->GetPosition();
    }

    void FlyCameraController::SetPosition(const Core::Maths::Vec3f& position)
    {
        m_camera->SetPosition(position);
    }

    void FlyCameraController::SetViewport(float width, float height)
    {
        m_camera->SetViewportSize(width, height);
    }

    bool FlyCameraController::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        m_camera->OnMouseButtonUp((int) e.GetButton());
        return false;
    }

    bool FlyCameraController::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        m_camera->OnMouseButtonDown((int) e.GetButton());
        return false;
    }

    bool FlyCameraController::OnMouseButtonWheelMoved(MouseButtonWheelEvent& e)
    {
        const auto mouse_position = IDevice::As<Mouse>()->GetMousePosition(m_window);
        m_camera->OnMouseScroll(e.GetOffetY(), mouse_position[0], mouse_position[1]);
        return false;
    }

    bool FlyCameraController::OnMouseButtonMoved(MouseButtonMovedEvent& e)
    {
        m_camera->OnMouseMove(e.GetXOffset(), e.GetYOffset());
        return false;
    }

    bool FlyCameraController::OnKeyPressed(KeyPressedEvent& e)
    {
        m_camera->OnKeyDown((int) e.GetKeyCode());
        return false;
    }

    bool FlyCameraController::OnKeyReleased(KeyReleasedEvent& e)
    {
        m_camera->OnKeyUp((int) e.GetKeyCode());
        return false;
    }

    void FlyCameraController::ResumeEventProcessing()
    {
        m_process_event.value.store(true, std::memory_order_release);
    }

    void FlyCameraController::PauseEventProcessing()
    {
        m_process_event.value.store(false, std::memory_order_release);
    }
} // namespace ZEngine::Controllers