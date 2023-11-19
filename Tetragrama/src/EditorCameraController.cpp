#include <EditorCameraController.h>

using namespace ZEngine::Inputs;

namespace Tetragrama
{
    void EditorCameraController::Initialize()
    {
        if (auto current_window = m_window.lock())
        {
            SetViewportSize(current_window->GetWidth(), current_window->GetHeight());
        }
    }

    void EditorCameraController::Update(ZEngine::Core::TimeStep time_step)
    {
        auto camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());

        if (auto window = m_window.lock())
        {
            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_ALT, window))
            {
                auto const camera         = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
                const auto mouse_position = IDevice::As<Mouse>()->GetMousePosition(window);
                const auto mouse          = ZEngine::Maths::Vector2(mouse_position[0], mouse_position[1]);
                glm::vec2  delta          = (mouse - m_initial_mouse_position) * 0.003f;
                m_initial_mouse_position  = mouse;

                if (IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_MIDDLE, window))
                {
                    camera->MousePan(delta);
                }

                if (IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_LEFT, window))
                {
                    camera->MouseRotate(delta);
                }

                if (IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_RIGHT, window))
                {
                    camera->MouseZoom(delta.y);
                }
            }

            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_UP, window))
            {
                camera->Move(MoveDirection::FORWARD, time_step);
            }
            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_DOWN, window))
            {
            
                camera->Move(MoveDirection::BACKWARD, time_step);
            }
            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT, window))
            {
                camera->Move(MoveDirection::LEFT, time_step);
            }
            if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_RIGHT, window))
            {
                camera->Move(MoveDirection::RIGHT, time_step);
            }
        }
    }

    void EditorCameraController::SetViewportSize(float width, float height)
    {
        auto const camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
        camera->SetViewPortSize(width, height);
    }

    bool EditorCameraController::OnEvent(ZEngine::Event::CoreEvent& e)
    {
        ZEngine::Event::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<ZEngine::Event::MouseButtonWheelEvent>(std::bind(&EditorCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        return PerspectiveCameraController::OnEvent(e);
    }

    bool EditorCameraController::OnMouseButtonWheelMoved(ZEngine::Event::MouseButtonWheelEvent& e)
    {
        auto const camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
        float      delta  = e.GetOffetY() * 0.1f;
        camera->MouseZoom(delta);
        return false;
    }
} // namespace Tetragrama
