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
		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT, m_window.lock())) {
			m_camera_target.x -= m_move_speed * dt;
			m_perspective_camera->SetTarget(m_camera_target);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_RIGHT, m_window.lock())) {
			m_camera_target.x += m_move_speed * dt;
			m_perspective_camera->SetTarget(m_camera_target);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_UP, m_window.lock())) {
			m_position.z -= m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_DOWN, m_window.lock())) {
			m_position.z += m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}
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
		m_zoom_factor	-= m_move_speed * static_cast<float>(e.GetOffetY()) * Engine::GetDeltaTime();
		m_camera_fov	= m_zoom_factor;
		UpdateProjectionMatrix();
		return false;
	}

}