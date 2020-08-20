#pragma once
#include <memory>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "../CoreWindow.h"
#include "../../Rendering/Graphics/GraphicContext.h"


namespace Z_Engine::Window::SDLWin {

	class OpenGLWindow : public CoreWindow {
	public:
		OpenGLWindow(WindowProperty& prop);
		virtual ~OpenGLWindow();

		unsigned int GetHeight() const override { return m_property.Height; }
		unsigned int GetWidth() const override { return m_property.Width; }
		const std::string& GetTitle() const override { return m_property.Title; }

		bool IsVSyncEnable() const override { return m_property.VSync; }
		void SetVSync(bool value) override { 
			m_property.VSync = value; 
			if (value) {
				SDL_GL_SetSwapInterval(1);
			}
			else {
				SDL_GL_SetSwapInterval(0);
			}
		}

		void SetCallbackFunction(const EventCallbackFn& callback) override { m_property.CallbackFn = callback; }

		void* GetNativeWindow() const override { return m_native_window; }
		void* GetNativeContext() const override { return m_context->GetNativeContext(); }


		virtual const WindowProperty& GetWindowProperty() const override { return m_property; }


		virtual void PollEvent() override;
		virtual void Update(Core::TimeStep delta_time) override;
		virtual void Render() override;

	public:
		bool OnEvent(Event::CoreEvent& event) override {

			Event::EventDispatcher event_dispatcher(event);
			event_dispatcher.Dispatch<WindowClosedEvent>(std::bind(&OpenGLWindow::OnWindowClosed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<WindowResizeEvent>(std::bind(&OpenGLWindow::OnWindowResized, this, std::placeholders::_1));

			event_dispatcher.Dispatch<KeyPressedEvent>(std::bind(&OpenGLWindow::OnKeyPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<KeyReleasedEvent>(std::bind(&OpenGLWindow::OnKeyReleased, this, std::placeholders::_1));

			event_dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&OpenGLWindow::OnMouseButtonPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<MouseButtonReleasedEvent>(std::bind(&OpenGLWindow::OnMouseButtonReleased, this, std::placeholders::_1));
			event_dispatcher.Dispatch<MouseButtonMovedEvent>(std::bind(&OpenGLWindow::OnMouseButtonMoved, this, std::placeholders::_1));
			event_dispatcher.Dispatch<MouseButtonWheelEvent>(std::bind(&OpenGLWindow::OnMouseButtonWheelMoved, this, std::placeholders::_1));
			
			event_dispatcher.Dispatch<TextInputEvent>(std::bind(&OpenGLWindow::OnTextInputRaised, this, std::placeholders::_1));

			event_dispatcher.Dispatch<WindowMinimizedEvent>(std::bind(&OpenGLWindow::OnWindowMinimized, this, std::placeholders::_1));
			event_dispatcher.Dispatch<WindowMaximizedEvent>(std::bind(&OpenGLWindow::OnWindowMaximized, this, std::placeholders::_1));
			event_dispatcher.Dispatch<WindowRestoredEvent>(std::bind(&OpenGLWindow::OnWindowRestored, this, std::placeholders::_1));


			 return true;
		}

	protected:
		virtual bool OnWindowClosed(WindowClosedEvent&)						override;
		virtual bool OnWindowResized(WindowResizeEvent&)					override;

		
		virtual bool OnKeyPressed(KeyPressedEvent&)							override;
		virtual bool OnKeyReleased(KeyReleasedEvent&)						override;

		virtual bool OnMouseButtonPressed(MouseButtonPressedEvent&)			override;
		virtual bool OnMouseButtonReleased(MouseButtonReleasedEvent&)		override;
		virtual bool OnMouseButtonMoved(MouseButtonMovedEvent&)				override;
		virtual bool OnMouseButtonWheelMoved(MouseButtonWheelEvent&)		override;

		virtual bool OnTextInputRaised(TextInputEvent&)						override;

		virtual bool OnWindowMinimized(Event::WindowMinimizedEvent&)		override;
		virtual bool OnWindowMaximized(Event::WindowMaximizedEvent&)		override;
		virtual bool OnWindowRestored(Event::WindowRestoredEvent&)			override;

	private:
		SDL_Window* m_native_window{ nullptr };
		Rendering::Graphics::GraphicContext* m_context{ nullptr };
		std::unique_ptr<SDL_Event, std::function<void(SDL_Event*)>> m_event;

	};
		
}
