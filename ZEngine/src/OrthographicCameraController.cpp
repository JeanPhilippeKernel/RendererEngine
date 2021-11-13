#include <Controllers/OrthographicCameraController.h>
#include <Inputs/KeyCode.h>
#include <Inputs/IDevice.h>
#include <Inputs/Keyboard.h>
#include <Event/EventDispatcher.h>

using namespace ZEngine::Inputs;

namespace ZEngine::Controllers {

	void OrthographicCameraController::Initialize() {
		 m_orthographic_camera->SetPosition(m_position);
		 m_orthographic_camera->SetRotation(m_rotation_angle);
	}

	void OrthographicCameraController::Update(Core::TimeStep dt) {
		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT, m_window.lock())) {
			m_position.x -= m_move_speed * dt;
			m_orthographic_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_RIGHT, m_window.lock())) {
			m_position.x += m_move_speed * dt;
			m_orthographic_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_UP, m_window.lock())) {
			m_position.y += m_move_speed * dt;
			m_orthographic_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_DOWN, m_window.lock())) {
			m_position.y -= m_move_speed * dt;
			m_orthographic_camera->SetPosition(m_position);
		}

		if(m_can_rotate) {
			if(IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_Q, m_window.lock())) {
				m_rotation_angle += m_rotation_speed * dt;
			}

			if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_D, m_window.lock())) {
				m_rotation_angle -= m_rotation_speed * dt;
			}

			m_orthographic_camera->SetRotation(m_rotation_angle);
		}
	}
	
	bool OrthographicCameraController::OnEvent(Event::CoreEvent& e) {
		Event::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&OrthographicCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
		dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&OrthographicCameraController::OnWindowResized, this, std::placeholders::_1));
		return false;
	}

								  
	bool OrthographicCameraController::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
		m_zoom_factor -= m_move_speed * (float)e.GetOffetY();
		m_orthographic_camera->SetProjectionMatrix(glm::ortho(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio * m_zoom_factor, -m_zoom_factor, m_zoom_factor));
		return false;
	}

	bool OrthographicCameraController::OnWindowResized(Event::WindowResizedEvent& e) {
		m_aspect_ratio = (float) e.GetWidth() / (float) e.GetHeight();
		m_orthographic_camera->SetProjectionMatrix(glm::ortho(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio * m_zoom_factor, -m_zoom_factor, m_zoom_factor));
		return false;
	}

}