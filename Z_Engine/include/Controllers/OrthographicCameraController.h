#pragma once

#include "ICameraController.h"
#include "../Rendering/Cameras/OrthographicCamera.h"
#include "../Core/TimeStep.h"
#include "../Inputs/IMouseEventCallback.h"
#include "../Window/ICoreWindowEventCallback.h"

namespace Z_Engine::Controllers {

	class OrthographicCameraController : 
		public ICameraController, 
		public Inputs::IMouseEventCallback, 
		public Window::ICoreWindowEventCallback {

	public:
		OrthographicCameraController() = default;
		OrthographicCameraController(const Z_Engine::Ref<Z_Engine::Window::CoreWindow>& window, bool can_rotate = false) 
			:
			ICameraController(window, can_rotate), 
			m_orthographic_camera(new Rendering::Cameras::OrthographicCamera(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio * m_zoom_factor, -m_zoom_factor, m_zoom_factor))
		{
		} 

		OrthographicCameraController(float aspect_ratio) 
			:
			ICameraController(aspect_ratio),
			m_orthographic_camera(new Rendering::Cameras::OrthographicCamera(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio * m_zoom_factor, -m_zoom_factor, m_zoom_factor))
		{
		}

		virtual ~OrthographicCameraController() = default;

		void Initialize()				override;
		void Update(Core::TimeStep)		override;
		bool OnEvent(Event::CoreEvent&) override;

		const Z_Engine::Ref<Rendering::Cameras::Camera> GetCamera() const override { return m_orthographic_camera; }

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
		Z_Engine::Ref<Rendering::Cameras::OrthographicCamera> m_orthographic_camera;
	};
}
