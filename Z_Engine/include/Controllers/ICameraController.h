#pragma once
#include "IController.h"
#include "../Rendering/Cameras/Camera.h"
#include "../Window/CoreWindow.h"

namespace Z_Engine::Controllers {

	struct ICameraController : public IController {
		
		ICameraController() = default;
		ICameraController(float aspect_ratio, bool can_rotate = false)
			:
			m_aspect_ratio(aspect_ratio), m_can_rotate(false)
		{}

		ICameraController(const Z_Engine::Ref<Z_Engine::Window::CoreWindow>& window, bool can_rotate) 
			: 
			m_can_rotate(can_rotate),
			m_window(window)
		{
			const auto window_ptr = m_window.lock();
			assert(window_ptr != nullptr);

			m_aspect_ratio = window_ptr->GetWindowProperty().AspectRatio;
		}

		ICameraController(const Z_Engine::Ref<Z_Engine::Window::CoreWindow>& window)
			:
			m_can_rotate(false),
			m_window(window)
		{
			const auto window_ptr = m_window.lock();
			assert(window_ptr != nullptr);

			m_aspect_ratio = window_ptr->GetWindowProperty().AspectRatio;
		}

		virtual ~ICameraController() = default;

		const glm::vec3& GetPosition()	const { return m_position; }
		float GetRotationAngle()		const { return m_rotation_angle; }
		float GetZoomFactor()			const { return m_zoom_factor; }
		
		float GetMoveSpeed()			const { return m_move_speed; }
		float GetRotationSpeed()		const { return m_rotation_speed; }
		float GetAspectRatio()			const { return m_aspect_ratio; }

		void SetPosition(glm::vec3 position)	{ m_position = position; }
		void SetRotationAngle(float angle)		{ m_rotation_angle =  angle; }
		void SetZoomFactor(float factor)		{ m_zoom_factor = factor; }

		void SetMoveSpeed(float speed)			{ m_move_speed =  speed; }
		void SetRotationSpeed(float speed)		{ m_rotation_speed = speed; }
		void SetAspectRatio(float ar)			{ m_aspect_ratio = ar; }

		virtual const Z_Engine::Ref<Rendering::Cameras::Camera> GetCamera() const =  0;

	protected:
		glm::vec3 m_position			{ 0.0f, 0.0f, 0.0f };
		float m_rotation_angle			{ 0.0f };
		float m_zoom_factor				{ 1.0f };

		float m_move_speed				{ 0.05f };
		float m_rotation_speed			{ 0.05f };
		float m_aspect_ratio			{ 0.0f };

		bool m_can_rotate				{ false };


		Z_Engine::WeakRef<Z_Engine::Window::CoreWindow> m_window;
	};
}
