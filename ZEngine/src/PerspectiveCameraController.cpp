#include <Controllers/PerspectiveCameraController.h>
#include <Inputs/KeyCode.h>
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
		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_LEFT)) {
			m_camera_target.x -= m_move_speed * dt;
			m_perspective_camera->SetTarget(m_camera_target);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_RIGHT)) {
			m_camera_target.x += m_move_speed * dt;
			m_perspective_camera->SetTarget(m_camera_target);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_UP)) {
			m_position.z -= m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_DOWN)) {
			m_position.z += m_move_speed * dt;
			m_perspective_camera->SetPosition(m_position);
		}
	}

	bool PerspectiveCameraController::OnEvent(Event::CoreEvent& e) {
		Event::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&PerspectiveCameraController::OnMouseButtonWheelMoved, this, std::placeholders::_1));
		dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&PerspectiveCameraController::OnWindowResized, this, std::placeholders::_1));
		return false;
	}


	bool PerspectiveCameraController::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
		m_zoom_factor	-= m_move_speed * static_cast<float>(e.GetOffetY()) * Engine::GetDeltaTime();
		m_camera_fov	= m_zoom_factor;
		m_perspective_camera->SetProjectionMatrix(glm::perspective(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far));
		return false;
	}

	bool PerspectiveCameraController::OnWindowResized(Event::WindowResizedEvent& e) {
		m_aspect_ratio = static_cast<float>(e.GetWidth()) / static_cast<float>(e.GetHeight());
		m_perspective_camera->SetProjectionMatrix(glm::perspective(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far));
		return false;
	}

}