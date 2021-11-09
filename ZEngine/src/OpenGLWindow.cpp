#include <cstdint>
#include <Engine.h>

#include <Window/SDLWin/OpenGLWindow.h>
#include <glad/include/glad/glad.h>

#include <Inputs/KeyCode.h>
#include <Rendering/Renderers/RenderCommand.h>
#include <Logging/LoggerDefinition.h>

#include <imgui.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Graphics;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Event;

namespace ZEngine::Window {

	CoreWindow* Create(WindowProperty prop)
	{
		auto core_window = new SDLWin::OpenGLWindow(prop);
		core_window->SetCallbackFunction(std::bind(&CoreWindow::OnEvent, core_window, std::placeholders::_1));
		return core_window;
	}

}

namespace ZEngine::Window::SDLWin {

	OpenGLWindow::OpenGLWindow(WindowProperty& prop)
		: CoreWindow(), m_event(new SDL_Event{}, [](SDL_Event* e) { free(e); })

	{
		m_property = prop;
		int sdl_init = 0;
#ifdef _WIN32
		sdl_init = SDL_Init(SDL_INIT_EVERYTHING);
#else
		sdl_init = SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS);
#endif
		if (sdl_init != 0) {
			Z_ENGINE_CORE_CRITICAL("Unable to initialize SDL subsystems : {}", SDL_GetError());
			Z_ENGINE_EXIT_FAILURE();
		}

		const int loaded_gl_library = SDL_GL_LoadLibrary(NULL);
		if (loaded_gl_library != 0) {
			Z_ENGINE_CORE_CRITICAL("Failed to load OpenGL library...");
			Z_ENGINE_EXIT_FAILURE();
		}

		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);

#ifndef _WIN32
		m_desired_gl_context_major_version = 3;
		m_desired_gl_context_minor_version = 3;
#endif
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, m_desired_gl_context_major_version);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, m_desired_gl_context_minor_version);

		m_native_window = SDL_CreateWindow(
			m_property.Title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			m_property.Width, m_property.Height,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_CAPTURE
		);
		Z_ENGINE_CORE_INFO("Window created, Properties : Width = {0}, Height = {1}", m_property.Width, m_property.Height);
		
		SetVSync(true);
		SDL_SetWindowData(m_native_window, CoreWindow::ATTACHED_PROPERTY, &m_property);

		m_context = CreateContext(this);
		m_context->MarkActive();

		int glad_init = gladLoadGLLoader(SDL_GL_GetProcAddress);
		if (glad_init == 0) {
			Z_ENGINE_CORE_CRITICAL("Unable to initialize glad library...");
			Z_ENGINE_EXIT_FAILURE();
		}

		RendererCommand::SetViewport(0, 0, m_property.Width, m_property.Height);
	}


	void OpenGLWindow::Initialize() {
		for(Ref<Layers::Layer>& layer : *m_layer_stack_ptr) {
			layer->SetAttachedWindow(shared_from_this());
			layer->Initialize();
		}
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
		for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr) {
			layer->Update(delta_time);
		}
	}

	void OpenGLWindow::Render() {
		for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr) {
			layer->Render();
		}
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

		Z_ENGINE_CORE_INFO("Window size updated, Properties : Width = {0}, Height = {1}", m_property.Width, m_property.Height);

		RendererCommand::SetViewport(0, 0, m_property.Width, m_property.Height);

		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowResizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}

	bool OpenGLWindow::OnKeyPressed(KeyPressedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::KeyPressedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}
	
	bool OpenGLWindow::OnKeyReleased(KeyReleasedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::KeyReleasedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonPressed(MouseButtonPressedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::MouseButtonPressedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonReleased(MouseButtonReleasedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::MouseButtonReleasedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonMoved(MouseButtonMovedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::MouseButtonMovedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnMouseButtonWheelMoved(MouseButtonWheelEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::MouseButtonWheelEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnTextInputRaised(TextInputEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::TextInputEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return true;
	}

	bool OpenGLWindow::OnWindowMinimized(Event::WindowMinimizedEvent& event) {
		m_property.IsMinimized = true;
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowMinimizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}
	
	bool OpenGLWindow::OnWindowMaximized(Event::WindowMaximizedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowMaximizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}
	
	bool OpenGLWindow::OnWindowRestored(Event::WindowRestoredEvent& event) {
		m_property.IsMinimized = false;
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowRestoredEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}

}