#include "PerspectiveCameraController.h"
#include "../Inputs/KeyCode.h"
#include "../Inputs/IDevice.h"
#include "../Inputs/Keyboard.h"
#include "../Inputs/Mouse.h"
#include "../Event/EventDispatcher.h"

using namespace Z_Engine::Inputs;

namespace Z_Engine::Controllers {

	void PerspectiveCameraController::Initialize() {
		m_perspective_camera->SetPosition(m_position);
		m_perspective_camera->SetTarget(m_camera_target);
	}

	void PerspectiveCameraController::Update(Core::TimeStep dt) {
		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_LEFT)) {
			m_position.x += m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_RIGHT)) {
			m_position.x -= m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_UP)) {
			m_position.z -= m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_DOWN)) {
			m_position.z += m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}
		
		//if (m_can_rotate) {
		//	if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_Q)) {
		//		m_rotation_angle -= m_rotation_speed * dt;
		//	}

		//	if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_D)) {
		//		m_rotation_angle += m_rotation_speed * dt;
		//	}

		//	m_Perspective_camera->SetRotation(m_rotation_angle);
		//}
	}

	bool PerspectiveCameraController::OnEvent(Event::CoreEvent& e) {
		Event::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&PerspectiveCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
		dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&PerspectiveCameraController::OnWindowResized, this, std::placeholders::_1));
		return false;
	}


	bool PerspectiveCameraController::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
		m_zoom_factor	-= m_move_speed * (float)e.GetOffetY();
		m_camera_fov	= m_zoom_factor;
		m_perspective_camera->SetProjectionMatrix(glm::perspective(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far));
		return false;
	}

	bool PerspectiveCameraController::OnWindowResized(Event::WindowResizedEvent& e) {
		m_aspect_ratio = (float)e.GetWidth() / (float)e.GetHeight();
		m_perspective_camera->SetProjectionMatrix(glm::perspective(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far));
		return false;
	}

}