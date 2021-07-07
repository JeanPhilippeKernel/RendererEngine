#include <Controllers/FirstPersonShooterCameraController.h>
#include <Inputs/KeyCode.h>
#include <Inputs/IDevice.h>
#include <Inputs/Keyboard.h>
#include <Inputs/Mouse.h>
#include <Event/EventDispatcher.h>
		 
#include <Engine.h>

using namespace ZEngine::Inputs;

namespace ZEngine::Controllers {

	void FirstPersonShooterCameraController::Initialize() {
		PerspectiveCameraController::Initialize();
		m_move_speed		= 0.05f;
		m_rotation_speed	= 0.05f;

		SDL_WarpMouseInWindow(
			NULL,
			m_window.lock()->GetWidth() / 2.0f, m_window.lock()->GetHeight() / 2.0f
		);
	}

	void FirstPersonShooterCameraController::Update(Core::TimeStep dt) {
		SDL_WarpMouseInWindow(
			NULL,
			m_window.lock()->GetWidth() / 2.0f, m_window.lock()->GetHeight() / 2.0f
		);
		
		auto camera = std::dynamic_pointer_cast<Rendering::Cameras::FirstPersonShooterCamera>(m_perspective_camera);

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_W)) {
			camera->Move(m_move_speed * dt * camera->GetForward());
		}

		else if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_S)) {
			camera->Move(m_move_speed * dt * -camera->GetForward());
		}


		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_A)) {
			camera->Move(m_move_speed * dt * camera->GetRight());
		}

		else if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_D)) {
			camera->Move(m_move_speed * dt * -camera->GetRight());
		}

		if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_Z)) {
			camera->Move(m_move_speed * dt * -camera->GetUp());
		}

		else if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_X)) {
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
		
		if (IDevice::As<Inputs::Mouse>()->IsKeyPressed(Z_ENGINE_KEY_MOUSE_RIGHT)) {
			const float yaw_angle	= static_cast<float>(((m_window.lock()->GetWidth() / 2.0f) - x_pos) * m_rotation_speed * Engine::GetDeltaTime());
			const float pitch_angle = static_cast<float>(((m_window.lock()->GetHeight() / 2.0f) - y_pos) * m_rotation_speed * Engine::GetDeltaTime());
	
			camera->SetPosition(yaw_angle, pitch_angle);
		}
		return false;
	}
}