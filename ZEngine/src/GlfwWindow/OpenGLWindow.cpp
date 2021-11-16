#include <pch.h>
#include <Window/GlfwWindow/OpenGLWindow.h>
#include <Engine.h>
#include <Inputs/KeyCode.h>
#include <Rendering/Renderers/RenderCommand.h>
#include <Logging/LoggerDefinition.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Graphics;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Event;

namespace ZEngine::Window::GLFWWindow {

	OpenGLWindow::OpenGLWindow(WindowProperty& prop)
		: CoreWindow()

	{
		m_property = prop;
		int glfw_init = glfwInit();

		if (glfw_init == GLFW_FALSE) {
			ZENGINE_CORE_CRITICAL("Unable to initialize glfw..");
			ZENGINE_EXIT_FAILURE();
		}

		glfwWindowHint(GLFW_DEPTH_BITS , 32);
		glfwWindowHint(GLFW_STENCIL_BITS , 8);
		glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

#ifdef __APPLE__
        m_desired_gl_context_major_version = 4;
        m_desired_gl_context_minor_version = 1;
        glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR , m_desired_gl_context_major_version);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR , m_desired_gl_context_minor_version);

        glfwSetErrorCallback([](int error, const char* description) {
            ZENGINE_CORE_CRITICAL(description);
            ZENGINE_EXIT_FAILURE();
        });
        
		m_native_window  = glfwCreateWindow(m_property.Width, m_property.Height, m_property.Title.c_str(), NULL, NULL);
		
		ZENGINE_CORE_INFO("Window created, Properties : Width = {0}, Height = {1}", m_property.Width, m_property.Height);
		
		glfwSetWindowUserPointer(m_native_window,  &m_property);

		m_context = CreateContext(this);
		m_context->MarkActive();
        
        SetVSync(true);
		
        int glad_init = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		if (glad_init == 0) {
			ZENGINE_CORE_CRITICAL("Unable to initialize glad library...");
			ZENGINE_EXIT_FAILURE();
		}

		RendererCommand::SetViewport(0, 0, m_property.Width, m_property.Height);

		glfwSetFramebufferSizeCallback(m_native_window, OpenGLWindow::__OnGlfwFrameBufferSizeChanged);

		glfwSetWindowCloseCallback(m_native_window, OpenGLWindow::__OnGlfwWindowClose);
		glfwSetWindowSizeCallback(m_native_window, OpenGLWindow::__OnGlfwWindowResized);
		glfwSetWindowMaximizeCallback(m_native_window, OpenGLWindow::__OnGlfwWindowMaximized);
		glfwSetWindowIconifyCallback(m_native_window, OpenGLWindow::__OnGlfwWindowMinimized);

		glfwSetMouseButtonCallback(m_native_window, OpenGLWindow::__OnGlfwMouseButtonRaised);
		glfwSetScrollCallback(m_native_window, OpenGLWindow::__OnGlfwMouseScrollRaised);
		glfwSetKeyCallback(m_native_window, OpenGLWindow::__OnGlfwKeyboardRaised);

		glfwSetCursorPosCallback(m_native_window, OpenGLWindow::__OnGlfwCursorMoved);
		glfwSetCharCallback(m_native_window, OpenGLWindow::__OnGlfwTextInputRaised);
	}


	void OpenGLWindow::Initialize() {
		for(Ref<Layers::Layer>& layer : *m_layer_stack_ptr) {
			layer->SetAttachedWindow(shared_from_this());
			layer->Initialize();
		}
	}

	void OpenGLWindow::PollEvent() {
		glfwPollEvents();
	}

	void OpenGLWindow::__OnGlfwFrameBufferSizeChanged(GLFWwindow* window, int width, int height) {

		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			property->SetWidth(width);
			property->SetHeight(height);

			ZENGINE_CORE_INFO("Window size updated, Properties : Width = {0}, Height = {1}", property->Width, property->Height);

			RendererCommand::SetViewport(0, 0, property->Width, property->Height);
		}
	}

	void OpenGLWindow::__OnGlfwWindowClose(GLFWwindow* window) {
		WindowProperty * property = static_cast<WindowProperty *>(glfwGetWindowUserPointer(window));
		if(property) {
			WindowClosedEvent e;
			property->CallbackFn(e);
		}
	}

	void OpenGLWindow::__OnGlfwWindowResized(GLFWwindow* window, int width, int height) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			Event::WindowResizedEvent e{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
			property->CallbackFn(e);
		}
	}

	void OpenGLWindow::__OnGlfwWindowMaximized(GLFWwindow* window, int maximized) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			if (maximized == GLFW_TRUE) {
				Event::WindowMaximizedEvent e;
				property->CallbackFn(e);
				return;
			}

			Event::WindowRestoredEvent e;
			property->CallbackFn(e);	
		}
	}

	void OpenGLWindow::__OnGlfwWindowMinimized(GLFWwindow* window, int minimized) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			if (minimized == GLFW_TRUE) {
				Event::WindowMinimizedEvent e;
				property->CallbackFn(e);
				return;
			}
			Event::WindowRestoredEvent e;
			property->CallbackFn(e);
		}
	}

	void OpenGLWindow::__OnGlfwMouseButtonRaised(GLFWwindow* window, int button, int action, int mods) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			if (action == GLFW_PRESS) {
				Event::MouseButtonPressedEvent e{ static_cast<Inputs::GlfwKeyCode>(button) };
				property->CallbackFn(e);
				return;
			}

			Event::MouseButtonReleasedEvent e{ static_cast<Inputs::GlfwKeyCode>(button) };
			property->CallbackFn(e);
		}

	}
	
	void OpenGLWindow::__OnGlfwMouseScrollRaised(GLFWwindow* window, double xoffset, double yoffset) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			MouseButtonWheelEvent e{ xoffset, yoffset };
			property->CallbackFn(e);
		}
	}

	void OpenGLWindow::__OnGlfwCursorMoved(GLFWwindow* window, double xoffset, double yoffset) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			MouseButtonMovedEvent e{ xoffset, yoffset };
			property->CallbackFn(e);
		}
	}

	void OpenGLWindow::__OnGlfwTextInputRaised(GLFWwindow* window, unsigned int character) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			std::string arr;
			arr.append(1, character);
			TextInputEvent e{ arr.c_str() };
			property->CallbackFn(e);
		}
	}
	
	void OpenGLWindow::__OnGlfwKeyboardRaised(GLFWwindow* window, int key, int scancode, int action, int mods) {
		WindowProperty* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
		if (property) {
			switch (action)
			{
				case GLFW_PRESS: {
					Event::KeyPressedEvent e{ static_cast<Inputs::GlfwKeyCode>(key), 0 };
					property->CallbackFn(e);
					break;
				}
				
				case GLFW_RELEASE: {
					Event::KeyReleasedEvent e{ static_cast<Inputs::GlfwKeyCode>(key) };
					property->CallbackFn(e);
					break;
				}
				
				case GLFW_REPEAT: {
					Event::KeyPressedEvent e{ static_cast<Inputs::GlfwKeyCode>(key), 0};
					property->CallbackFn(e);
					break;
				}
			}

			if (key == GLFW_KEY_ESCAPE) {
				WindowClosedEvent e;
				property->CallbackFn(e);
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

		glfwSwapBuffers(m_native_window);
	}

	OpenGLWindow::~OpenGLWindow()
	{
		delete m_context;

		glfwSetErrorCallback(NULL);
		glfwDestroyWindow(m_native_window);
		glfwTerminate();
	}

	bool OpenGLWindow::OnWindowClosed(WindowClosedEvent& event) {
		glfwSetWindowShouldClose(m_native_window, GLFW_TRUE);
		ZENGINE_CORE_INFO("Window has been closed");

		Event::EngineClosedEvent e(event.GetName().c_str());
		Event::EventDispatcher event_dispatcher(e);
		event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, m_engine, std::placeholders::_1));

		return true;
	}

	bool OpenGLWindow::OnWindowResized(WindowResizedEvent& event) {
		ZENGINE_CORE_INFO("Window has been resized");

		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowResizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}

	bool OpenGLWindow::OnWindowMinimized(Event::WindowMinimizedEvent& event) {
		ZENGINE_CORE_INFO("Window has been minimized");

		m_property.IsMinimized = true;
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowMinimizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}

	bool OpenGLWindow::OnWindowMaximized(Event::WindowMaximizedEvent& event) {
		ZENGINE_CORE_INFO("Window has been maximized");

		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowMaximizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
		return false;
	}

	bool OpenGLWindow::OnWindowRestored(Event::WindowRestoredEvent& event) {
		ZENGINE_CORE_INFO("Window has been restored");

		m_property.IsMinimized = false;
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowRestoredEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
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
}
