#include <Controllers/OrbitCameraController.h>
#include <Inputs/KeyCode.h>
#include <Inputs/IDevice.h>
#include <Inputs/Keyboard.h>
#include <Inputs/Mouse.h>
#include <Event/EventDispatcher.h>

#include <Engine.h>

using namespace ZEngine::Inputs;

namespace ZEngine::Controllers {


	void OrbitCameraController::Initialize() {
		PerspectiveCameraController::Initialize();
		m_move_speed		= 1.f;
		m_rotation_speed	= 0.2f;
	}

	void OrbitCameraController::Update(Core::TimeStep dt) {
		static Maths::Vector2 last_mouse_cursor_pos;

		if (IDevice::As<Inputs::Mouse>()->IsKeyPressed(Z_ENGINE_KEY_MOUSE_RIGHT)) {
			auto camera = std::dynamic_pointer_cast<Rendering::Cameras::OrbitCamera>(m_perspective_camera);
			float yaw_angle_degree		= (m_mouse_cursor_pos.x - last_mouse_cursor_pos.x) * m_rotation_speed * dt;
			float pitch_angle_degree	= (m_mouse_cursor_pos.y - last_mouse_cursor_pos.y) * m_rotation_speed * dt;
			camera->SetPosition(yaw_angle_degree, pitch_angle_degree);
		}
		last_mouse_cursor_pos = m_mouse_cursor_pos;
	}


	bool OrbitCameraController::OnEvent(Event::CoreEvent& e) {
		Event::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&OrbitCameraController::OnMouseButtonMoved, this, std::placeholders::_1));
		return PerspectiveCameraController::OnEvent(e);
	}


	bool OrbitCameraController::OnMouseButtonMoved(Event::MouseButtonMovedEvent& e) {
		m_mouse_cursor_pos.x = static_cast<float>(e.GetPosX());
		m_mouse_cursor_pos.y = static_cast<float>(e.GetPosY());
		return false;
	}

	bool OrbitCameraController::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
		auto camera = std::dynamic_pointer_cast<Rendering::Cameras::OrbitCamera>(m_perspective_camera);
		auto radius = camera->GetRadius();
		radius += e.GetOffetY() * m_move_speed * Engine::GetDeltaTime();
		camera->SetRadius(radius);
		return false;
	}
}