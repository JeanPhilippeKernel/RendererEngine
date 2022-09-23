#include <pch.h>
#include <Controllers/FirstPersonShooterCameraController.h>
#include <Inputs/KeyCodeDefinition.h>
#include <Inputs/IDevice.h>
#include <Inputs/Keyboard.h>
#include <Inputs/Mouse.h>
#include <Event/EventDispatcher.h>

#include <Engine.h>

using namespace ZEngine::Inputs;

namespace ZEngine::Controllers {

    void FirstPersonShooterCameraController::Initialize() {
        PerspectiveCameraController::Initialize();
        m_move_speed     = 0.05f;
        m_rotation_speed = 0.05f;


        GLFWwindow* current_window = static_cast<GLFWwindow*>(m_window.lock()->GetNativeWindow());
        glfwSetInputMode(current_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPos(current_window, m_window.lock()->GetWidth() / 2.0, m_window.lock()->GetHeight() / 2.0);
    }

    void FirstPersonShooterCameraController::Update(Core::TimeStep dt) {

        GLFWwindow* current_window = static_cast<GLFWwindow*>(m_window.lock()->GetNativeWindow());
        glfwSetInputMode(current_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPos(current_window, m_window.lock()->GetWidth() / 2.0, m_window.lock()->GetHeight() / 2.0);
        
        auto camera = std::dynamic_pointer_cast<Rendering::Cameras::FirstPersonShooterCamera>(m_perspective_camera);

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_W, m_window.lock())) {
            camera->Move(m_move_speed * dt * camera->GetForward());
        }

        else if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_S, m_window.lock())) {
            camera->Move(m_move_speed * dt * -camera->GetForward());
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_A, m_window.lock())) {
            camera->Move(m_move_speed * dt * camera->GetRight());
        }

        else if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_D, m_window.lock())) {
            camera->Move(m_move_speed * dt * -camera->GetRight());
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_Z, m_window.lock())) {
            camera->Move(m_move_speed * dt * -camera->GetUp());
        }

        else if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_X, m_window.lock())) {
            camera->Move(m_move_speed * dt * camera->GetUp());
        }
    }

    bool FirstPersonShooterCameraController::OnEvent(Event::CoreEvent& e) {
        Event::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&FirstPersonShooterCameraController::OnMouseButtonMoved, this, std::placeholders::_1));
        return PerspectiveCameraController::OnEvent(e);
    }

    bool FirstPersonShooterCameraController::OnMouseButtonMoved(Event::MouseButtonMovedEvent& e) {
        const float x_pos = static_cast<float>(e.GetPosX());
        const float y_pos = static_cast<float>(e.GetPosY());

        auto camera = std::dynamic_pointer_cast<Rendering::Cameras::FirstPersonShooterCamera>(m_perspective_camera);

        if (IDevice::As<Inputs::Mouse>()->IsKeyPressed(ZENGINE_KEY_MOUSE_RIGHT, m_window.lock())) {
            const float yaw_angle   = static_cast<float>(((m_window.lock()->GetWidth() / 2.0f) - x_pos) * m_rotation_speed * Engine::GetDeltaTime());
            const float pitch_angle = static_cast<float>(((m_window.lock()->GetHeight() / 2.0f) - y_pos) * m_rotation_speed * Engine::GetDeltaTime());

            camera->SetPosition(yaw_angle, pitch_angle);
        }
        return false;
    }
} // namespace ZEngine::Controllers
