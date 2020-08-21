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
		OrthographicCameraController(float aspect_ratio) 
			:ICameraController()
		{
			m_aspect_ratio =  aspect_ratio;
			m_orthographic_camera.reset(new Rendering::Cameras::OrthographicCamera(-m_aspect_ratio * m_zoom_factor, m_aspect_ratio* m_zoom_factor, -m_zoom_factor, m_zoom_factor));
		}

		virtual ~OrthographicCameraController() =  default;

		void Initialize()				override;
		void Update(Core::TimeStep dt)	override;
		bool OnEvent(Event::CoreEvent&) override;


		Z_Engine::Ref<Rendering::Cameras::Camera> GetCamera() override { return m_orthographic_camera; }


	public:
		bool OnMouseButtonPressed(Z_Engine::Event::MouseButtonPressedEvent&)	override { return false; }
		bool OnMouseButtonReleased(Z_Engine::Event::MouseButtonReleasedEvent&)  override { return false; }
		bool OnMouseButtonMoved(Z_Engine::Event::MouseButtonMovedEvent&)		override { return false; }
		bool OnMouseButtonWheelMoved(Z_Engine::Event::MouseButtonWheelEvent&)	override;

		bool OnWindowClosed(Event::WindowClosedEvent&)							override { return false; }
		bool OnWindowResized(Event::WindowResizedEvent&)							override;
		bool OnWindowMinimized(Event::WindowMinimizedEvent&)					override { return false; }
		bool OnWindowMaximized(Event::WindowMaximizedEvent&)					override { return false; }
		bool OnWindowRestored(Event::WindowRestoredEvent&)						override { return false; }

	private:
		Z_Engine::Ref<Rendering::Cameras::OrthographicCamera> m_orthographic_camera;
	};
}
