#pragma once
#include <memory>

#define SDL_MAIN_HANDLED
#include <SDL2/include/SDL.h>

#include <Window/CoreWindow.h>
#include <Rendering/Graphics/GraphicContext.h>

namespace ZEngine::Window::SDLWin {

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

		virtual void Initialize() override;
		virtual void PollEvent() override;
		virtual float GetTime() override { return (float)SDL_GetTicks(); }
		virtual void Update(Core::TimeStep delta_time) override;
		virtual void Render() override;

	public:
		bool OnEvent(Event::CoreEvent& event) override {

			Event::EventDispatcher event_dispatcher(event);
			event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&OpenGLWindow::OnWindowClosed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&OpenGLWindow::OnWindowResized, this, std::placeholders::_1));

			event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&OpenGLWindow::OnKeyPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&OpenGLWindow::OnKeyReleased, this, std::placeholders::_1));

			event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&OpenGLWindow::OnMouseButtonPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&OpenGLWindow::OnMouseButtonReleased, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&OpenGLWindow::OnMouseButtonMoved, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&OpenGLWindow::OnMouseButtonWheelMoved, this, std::placeholders::_1));
			
			event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&OpenGLWindow::OnTextInputRaised, this, std::placeholders::_1));

			event_dispatcher.Dispatch<Event::WindowMinimizedEvent>(std::bind(&OpenGLWindow::OnWindowMinimized, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::WindowMaximizedEvent>(std::bind(&OpenGLWindow::OnWindowMaximized, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::WindowRestoredEvent>(std::bind(&OpenGLWindow::OnWindowRestored, this, std::placeholders::_1));

			 return true;
		}

	protected:
		virtual bool OnKeyPressed(Event::KeyPressedEvent&)							override;
		virtual bool OnKeyReleased(Event::KeyReleasedEvent&)						override;

		virtual bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&)			override;
		virtual bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&)		override;
		virtual bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&)				override;
		virtual bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&)			override;

		virtual bool OnTextInputRaised(Event::TextInputEvent&)						override;

		virtual bool OnWindowClosed(Event::WindowClosedEvent&)						override;
		virtual bool OnWindowResized(Event::WindowResizedEvent&)					override;
		virtual bool OnWindowMinimized(Event::WindowMinimizedEvent&)				override;
		virtual bool OnWindowMaximized(Event::WindowMaximizedEvent&)				override;
		virtual bool OnWindowRestored(Event::WindowRestoredEvent&)					override;

	private:
		unsigned int m_desired_gl_context_major_version{ 4 };
		unsigned int m_desired_gl_context_minor_version{ 5 };
		SDL_Window* m_native_window{ nullptr };
		Rendering::Graphics::GraphicContext* m_context{ nullptr };
		std::unique_ptr<SDL_Event, std::function<void(SDL_Event*)>> m_event;
	};
		
}
