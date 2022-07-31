#include <pch.h>
#include <Controllers/PerspectiveCameraController.h>
#include <Inputs/KeyCodeDefinition.h>
#include <Inputs/IDevice.h>
#include <Inputs/Keyboard.h>
#include <Inputs/Mouse.h>
#include <Event/EventDispatcher.h>
#include <Engine.h>

using namespace ZEngine::Inputs;

namespace ZEngine::Controllers {

    void PerspectiveCameraController::Initialize() {
        m_perspective_camera->SetTarget(m_camera_target);
        m_perspective_camera->SetPosition(m_position);
    }

    void PerspectiveCameraController::Update(Core::TimeStep dt) {
    }

    const Maths::Vector3& PerspectiveCameraController::GetPosition() const {
        return m_perspective_camera->GetPosition();
    }

    void PerspectiveCameraController::SetPosition(const Maths::Vector3& position) {
        m_perspective_camera->SetPosition(position);
    }

    void PerspectiveCameraController::UpdateProjectionMatrix() {
        m_perspective_camera->SetProjectionMatrix(glm::perspective(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far));
    }

    bool PerspectiveCameraController::OnEvent(Event::CoreEvent& e) {
        Event::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&PerspectiveCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        return false;
    }

    bool PerspectiveCameraController::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
        m_zoom_factor -= m_move_speed * static_cast<float>(e.GetOffetY()) * Engine::GetDeltaTime();
        m_camera_fov = m_zoom_factor;
        UpdateProjectionMatrix();
        return false;
    }

} // namespace ZEngine::Controllers
