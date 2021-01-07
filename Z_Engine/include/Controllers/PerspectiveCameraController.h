#pragma once

#include "ICameraController.h"
#include "../Rendering/Cameras/PerspectiveCamera.h"
#include "../Core/TimeStep.h"
#include "../Inputs/IMouseEventCallback.h"
#include "../Window/ICoreWindowEventCallback.h"

namespace Z_Engine::Controllers {

	class PerspectiveCameraController :
		public ICameraController,
		public Inputs::IMouseEventCallback,
		public Window::ICoreWindowEventCallback {

	public:
		PerspectiveCameraController() = default;
		PerspectiveCameraController(const Z_Engine::Ref<Z_Engine::Window::CoreWindow>& window, bool can_rotate = false)
			:
			ICameraController(window, can_rotate),
			m_camera_fov(glm::half_pi<float>()),
			m_camera_near(1.0f),
			m_camera_far(150.0f),
			m_perspective_camera(new Rendering::Cameras::PerspectiveCamera(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far))
		{
			m_position = { 0.0f, 0.0f, 1.5f };
		}

		PerspectiveCameraController(float aspect_ratio)
			:
			ICameraController(aspect_ratio),
			m_camera_fov(glm::half_pi<float>()),
			m_camera_near(1.0f),
			m_camera_far(150.0f),
			m_perspective_camera(new Rendering::Cameras::PerspectiveCamera(m_camera_fov, m_aspect_ratio, m_camera_near, m_camera_far))
		{
			m_position = { 0.0f, 0.0f, 1.5f };
		}

		virtual ~PerspectiveCameraController() = default;

		void Initialize()				override;
		void Update(Core::TimeStep)		override;
		bool OnEvent(Event::CoreEvent&) override;

		const Z_Engine::Ref<Rendering::Cameras::Camera>& GetCamera() const override { return m_perspective_camera; }

	public:
		bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&)				override { return false; }
		bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&)			override { return false; }
		bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&)					override { return false; }
		bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&)				override;

		bool OnWindowResized(Event::WindowResizedEvent&)						override;
		bool OnWindowClosed(Event::WindowClosedEvent&)							override { return false; }
		bool OnWindowMinimized(Event::WindowMinimizedEvent&)					override { return false; }
		bool OnWindowMaximized(Event::WindowMaximizedEvent&)					override { return false; }
		bool OnWindowRestored(Event::WindowRestoredEvent&)						override { return false; }

	private:
		float m_camera_fov			{ 0.0f };
		float m_camera_near			{ 0.0f };
		float m_camera_far			{ 0.0f };
		glm::vec3 m_camera_target	{ 0.0f, 0.0f, 0.0f };


		Z_Engine::Ref<Rendering::Cameras::PerspectiveCamera> m_perspective_camera;
	};
}
