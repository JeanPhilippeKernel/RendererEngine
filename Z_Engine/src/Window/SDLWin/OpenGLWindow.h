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

		virtual const WindowProperty& GetWindowProperty() const override { return m_property; }


		virtual void PollEvent() override;
		virtual void Update(Core::TimeStep delta_time) override;
		virtual void Render() override;



	public:
		void OnEvent(Event::CoreEvent& event) override {

			Event::EventDispatcher event_dispatcher(event);
			event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&OpenGLWindow::OnWindowClosed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::WindowResizeEvent>(std::bind(&OpenGLWindow::OnWindowResized, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&OpenGLWindow::OnKeyPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&OpenGLWindow::OnKeyReleased, this, std::placeholders::_1));
		}

	protected:
		bool OnWindowClosed(WindowClosedEvent&) override;
		bool OnWindowResized(WindowResizeEvent&) override;

		
		bool OnKeyPressed(KeyPressedEvent&) override;
		bool OnKeyReleased(KeyReleasedEvent&) override;

	private:
		SDL_Window* m_native_window{ nullptr };
		Rendering::Graphics::GraphicContext* m_context{ nullptr };
		std::unique_ptr<SDL_Event, std::function<void(SDL_Event*)>> m_event;

	};
		
}
