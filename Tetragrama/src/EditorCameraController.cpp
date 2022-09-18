#include <EditorCameraController.h>

using namespace ZEngine::Inputs;

namespace Tetragrama {
    void EditorCameraController::Initialize() {
        if (auto current_window = m_window.lock()) {
            SetViewportSize(current_window->GetWidth(), current_window->GetHeight());
        }
    }

    void EditorCameraController::Update(ZEngine::Core::TimeStep) {
        auto camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());

        if (IDevice::As<Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT_ALT, m_window.lock())) {
            auto const camera         = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
            const auto mouse_position = IDevice::As<Mouse>()->GetMousePosition(m_window.lock());
            const auto mouse          = ZEngine::Maths::Vector2(mouse_position[0], mouse_position[1]);
            glm::vec2  delta          = (mouse - m_initial_mouse_position) * 0.003f;
            m_initial_mouse_position  = mouse;

            if (IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_MIDDLE, m_window.lock())) {
                camera->MousePan(delta);
            } else if (IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_LEFT, m_window.lock())) {
                camera->MouseRotate(delta);
            } else if (IDevice::As<Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_RIGHT, m_window.lock())) {
                camera->MouseZoom(delta.y);
            }
        }

        camera->UpdateViewMatrix();
    }

    void EditorCameraController::SetViewportSize(float width, float height) {
        auto const camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
        camera->SetViewPortSize(width, height);
    }

    bool EditorCameraController::OnEvent(ZEngine::Event::CoreEvent& e) {
        ZEngine::Event::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<ZEngine::Event::MouseButtonWheelEvent>(std::bind(&EditorCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        return PerspectiveCameraController::OnEvent(e);
    }

    bool EditorCameraController::OnMouseButtonWheelMoved(ZEngine::Event::MouseButtonWheelEvent& e) {
        auto const camera = reinterpret_cast<EditorCamera*>(m_perspective_camera.get());
        float      delta  = e.GetOffetY() * 0.1f;
        camera->MouseZoom(delta);
        camera->UpdateViewMatrix();
        return false;
    }
} // namespace Tetragrama
