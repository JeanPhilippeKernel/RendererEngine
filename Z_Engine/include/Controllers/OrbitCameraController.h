#pragma once

#include "PerspectiveCameraController.h"
#include "../Rendering/Cameras/OrbitCamera.h"

namespace Z_Engine::Controllers {

	class OrbitCameraController :
		public PerspectiveCameraController {

	public:
		OrbitCameraController() = default;
		OrbitCameraController(const Z_Engine::Ref<Z_Engine::Window::CoreWindow>& window, Maths::Vector3 position, float yaw_angle_degree, float pitch_angle_degree)
			: 
			PerspectiveCameraController(window)
		{
			m_perspective_camera.reset(new Rendering::Cameras::OrbitCamera(
				m_camera_fov, m_aspect_ratio,
				m_camera_near, m_camera_far,
				Maths::radians(yaw_angle_degree), Maths::radians(pitch_angle_degree)
			));
			m_position = position;
		}

		virtual ~OrbitCameraController() = default;

		void Initialize()				override;
		void Update(Core::TimeStep)		override;
		bool OnEvent(Event::CoreEvent&) override;

	public:
		bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&)				override;
		bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&)					override;

	protected:
		Maths::Vector2 m_mouse_cursor_pos{0.f, 0.0f};
	};
}
