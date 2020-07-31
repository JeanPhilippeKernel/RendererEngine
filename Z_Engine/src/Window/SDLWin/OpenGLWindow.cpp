#include "OpenGLWindow.h"
#include "../../Engine.h"
#include <GL/glew.h>

using namespace Z_Engine;
using namespace Z_Engine::Rendering::Graphics;


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
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS
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

		glewExperimental = GL_TRUE;
		glViewport(0, 0, m_property.Width, m_property.Height);
	}


	void OpenGLWindow::Update(float delta_time) {

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
		event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEvent, m_engine, std::placeholders::_1));

		return true;
	}

	bool OpenGLWindow::OnWindowResized(WindowResizeEvent& event)
	{
		m_property.Width = event.GetWidth();
		m_property.Height = event.GetHeight();

		glViewport(0, 0, m_property.Width, m_property.Height);

		return true;
	}
}