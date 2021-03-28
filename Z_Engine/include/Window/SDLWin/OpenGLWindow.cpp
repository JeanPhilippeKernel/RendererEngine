#include "OpenGLWindow.h"
#include "../../Engine.h"
#include "../../dependencies/glew/include/GL/glew.h"
#include <cstdint>

#include "../../Inputs/KeyCode.h"
#include "../../Rendering/Renderers/RenderCommand.h"

#include "../../dependencies/imgui/imgui.h"


using namespace Z_Engine;
using namespace Z_Engine::Rendering::Graphics;
using namespace Z_Engine::Rendering::Renderers;



namespace Z_Engine::Window {

	CoreWindow* Create(WindowProperty prop)
	{
		auto core_window = new SDLWin::OpenGLWindow(prop);
		core_window->SetCallbackFunction(std::bind(&CoreWindow::OnEvent, core_window, std::placeholders::_1));
		return core_window;
	}

}

namespace Z_Engine::Window::SDLWin {

	OpenGLWindow::OpenGLWindow(WindowProperty& prop)
		: m_event(new SDL_Event{}, [](SDL_Event* e) { free(e); })

	{
		m_property = prop;

		const int sdl_init = SDL_Init(SDL_INIT_EVERYTHING);
		if (sdl_init != 0) {
			SDL_Log("Unable to initialize SDL : %s", SDL_GetError());
			exit(EXIT_FAILURE);
		}

		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);


		m_native_window = SDL_CreateWindow(
			m_property.Title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			m_property.Width, m_property.Height,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_CAPTURE
		);

		SetVSync(true);
		SDL_SetWindowData(m_native_window, CoreWindow::ATTACHED_PROPERTY, &m_property);

		m_context = CreateContext(this);
		m_context->MarkActive();

		const GLenum glew_init = glewInit();
		if (glew_init != GLEW_OK) {
			SDL_LogError(SDL_LOG_PRIORITY_CRITICAL, "unable to initialize glew library : %s", glewGetErrorString(glew_init));
			exit(EXIT_FAILURE);
		}

		//glewExperimental = GL_TRUE;
		RendererCommand::SetViewport(0, 0, m_property.Width, m_property.Height);
	}



	void OpenGLWindow::PollEvent() {
		const auto pending = SDL_PollEvent(m_event.get());
		if (pending)
		{
			
			switch (m_event->type)
			{
				case SDL_QUIT: {
					WindowClosedEvent e;
					m_property.CallbackFn(e);
					break;
				}
			
				case SDL_WINDOWEVENT: {
					switch (m_event->window.event)
					{
						case SDL_WINDOWEVENT_RESIZED:
						{
							Event::WindowResizedEvent  e {
								static_cast<uint32_t>(m_event->window.data1),
								static_cast<std::uint32_t>(m_event->window.data2)
							};
							m_property.CallbackFn(e);
							break;
						}

						case SDL_WINDOWEVENT_MAXIMIZED: {
							Event::WindowMaximizedEvent e;
							m_property.CallbackFn(e);
							break;
						}

						case SDL_WINDOWEVENT_MINIMIZED:{
							Event::WindowMinimizedEvent e;
							m_property.CallbackFn(e);
							break;
						}

						case SDL_WINDOWEVENT_RESTORED: {
							Event::WindowRestoredEvent e;
							m_property.CallbackFn(e);
							break;
						}
					}
					break;
				}

				case SDL_TEXTINPUT: {
					TextInputEvent e{m_event->text.text};
					m_property.CallbackFn(e);
					break;
				}

				case SDL_KEYDOWN: {
					if (m_event->key.keysym.sym == SDLK_ESCAPE) {
						Event::WindowClosedEvent e;
						m_property.CallbackFn(e);
						
					}
					else {
						if (m_event->key.keysym.scancode != SDL_Scancode::SDL_SCANCODE_UNKNOWN) {
							Event::KeyPressedEvent e {
								static_cast<Inputs::KeyCode>(m_event->key.keysym.scancode),
								m_event->key.repeat
							};
							m_property.CallbackFn(e);
						}
					}
					break;
				}

				case SDL_KEYUP: {
					if (m_event->key.keysym.scancode != SDL_Scancode::SDL_SCANCODE_UNKNOWN) {
						Event::KeyReleasedEvent e { static_cast<Inputs::KeyCode>(m_event->key.keysym.scancode) };
						m_property.CallbackFn(e);
					}
					break;
				}


				case SDL_MOUSEBUTTONDOWN: {
					MouseButtonPressedEvent e { m_event->button.button };
					m_property.CallbackFn(e);
					break;
				}

				case SDL_MOUSEBUTTONUP: { 
					MouseButtonReleasedEvent e { m_event->button.button };
					m_property.CallbackFn(e);
					break;
				}
				
				case SDL_MOUSEMOTION: {
					MouseButtonMovedEvent e {  m_event->button.x , m_event->button.y };
					m_property.CallbackFn(e);
					break;
				}		 
				
				case SDL_MOUSEWHEEL: {
					MouseButtonWheelEvent e {
						static_cast<std::uint32_t>(m_event->wheel.direction),
						static_cast<int>(m_event->wheel.x),
						static_cast<int>(m_event->wheel.y)
					};
					m_property.CallbackFn(e);
					break;
				}
			}
		}

	}

	void OpenGLWindow::Update(Core::TimeStep delta_time) {

	}

	void OpenGLWindow::Render() {
		SDL_GL_SwapWindow(m_native_window);
	}



	OpenGLWindow::~OpenGLWindow()
	{
		delete m_context;

		SDL_DestroyWindow(m_native_window);
		SDL_Quit();
	}

	bool OpenGLWindow::OnWindowClosed(WindowClosedEvent& event)
	{
		Event::EngineClosedEvent e(event.GetName().c_str());

		Event::EventDispatcher event_dispatcher(e);
		event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, m_engine, std::placeholders::_1));

		return true;
	}

	bool OpenGLWindow::OnWindowResized(WindowResizedEvent& event)
	{
		m_property.SetWidth(event.GetWidth());
		m_property.SetHeight(event.GetHeight());

		RendererCommand::SetViewport(0, 0, m_property.Width, m_property.Height);

		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return false;
	}

	bool OpenGLWindow::OnKeyPressed(KeyPressedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}
	
	bool OpenGLWindow::OnKeyReleased(KeyReleasedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}



	bool OpenGLWindow::OnMouseButtonPressed(MouseButtonPressedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonReleased(MouseButtonReleasedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonMoved(MouseButtonMovedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonWheelMoved(MouseButtonWheelEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnTextInputRaised(TextInputEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnWindowMinimized(Event::WindowMinimizedEvent& event) {
		m_property.IsMinimized = true;
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return false;
	}
	
	bool OpenGLWindow::OnWindowMaximized(Event::WindowMaximizedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return false;
	}
	
	bool OpenGLWindow::OnWindowRestored(Event::WindowRestoredEvent& event) {
		m_property.IsMinimized = false;
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));
		return false;
	}

}