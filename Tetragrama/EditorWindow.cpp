#include <pch.h>
#include <EditorWindow.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Event/EngineClosedEvent.h>
#include <ZEngine/Inputs/KeyCode.h>
#include <ZEngine/Logging/LoggerDefinition.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>

using namespace ZEngine;
using namespace ZEngine::Event;
using namespace ZEngine::Window;
using namespace ZEngine::Rendering;

namespace Tetragrama
{
    EditorWindow::EditorWindow(const WindowConfiguration& configuration) : CoreWindow()
    {
        m_property.Height = configuration.Height;
        m_property.Width  = configuration.Width;
        m_property.Title  = configuration.Title;
        m_property.VSync  = configuration.EnableVsync;

        int glfw_init = glfwInit();
        if (glfw_init == GLFW_FALSE)
        {
            ZENGINE_CORE_CRITICAL("Unable to initialize glfw..")
            ZENGINE_EXIT_FAILURE();
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfwSetErrorCallback([](int error, const char* description) {
            ZENGINE_CORE_CRITICAL("{}", description)
            ZENGINE_EXIT_FAILURE()
        });

        m_native_window = glfwCreateWindow(m_property.Width, m_property.Height, m_property.Title.c_str(), NULL, NULL);

        if (!m_native_window)
        {
            ZENGINE_CORE_CRITICAL("Failed to create GLFW Window")
            ZENGINE_EXIT_FAILURE()
        }

        int window_width = 0, window_height = 0;
        glfwGetWindowSize(m_native_window, &window_width, &window_height);
        if ((window_width > 0) && (window_height > 0) && (m_property.Width != window_width) && (m_property.Height != window_height))
        {
            m_property.SetWidth(window_width);
            m_property.SetHeight(window_height);
        }

#ifdef _WIN32
        auto native_hwnd = glfwGetWin32Window(m_native_window);
        m_property.Dpi   = GetDpiForWindow(native_hwnd);
#endif // _WIN32

        float x_scale, y_scale;
        glfwGetWindowContentScale(m_native_window, &x_scale, &y_scale);
        m_property.DpiScale = x_scale;

        ZENGINE_CORE_INFO("Window created, Width = {0}, Height = {1}", m_property.Width, m_property.Height)
    }

    uint32_t EditorWindow::GetWidth() const
    {
        return m_property.Width;
    }

    std::string_view EditorWindow::GetTitle() const
    {
        return m_property.Title;
    }

    bool EditorWindow::IsMinimized() const
    {
        return m_property.IsMinimized;
    }

    void EditorWindow::SetTitle(std::string_view title)
    {
        m_property.Title = title;
        glfwSetWindowTitle(m_native_window, m_property.Title.c_str());
    }

    bool EditorWindow::IsVSyncEnable() const
    {
        return m_property.VSync;
    }

    void EditorWindow::SetVSync(bool value)
    {
        m_property.VSync = value;
        if (value)
        {
            glfwSwapInterval(1);
        }
        else
        {
            glfwSwapInterval(0);
        }
    }

    void EditorWindow::SetCallbackFunction(const EventCallbackFn& callback)
    {
        m_property.CallbackFn = callback;
    }

    void* EditorWindow::GetNativeWindow() const
    {
        return reinterpret_cast<void*>(m_native_window);
    }

    const WindowProperty& EditorWindow::GetWindowProperty() const
    {
        return m_property;
    }

    void EditorWindow::Initialize()
    {
        m_swapchain = CreateRef<Rendering::Swapchain>();

        glfwSetWindowUserPointer(m_native_window, &m_property);

        glfwSetFramebufferSizeCallback(m_native_window, EditorWindow::__OnGlfwFrameBufferSizeChanged);

        glfwSetWindowCloseCallback(m_native_window, EditorWindow::__OnGlfwWindowClose);
        glfwSetWindowSizeCallback(m_native_window, EditorWindow::__OnGlfwWindowResized);
        glfwSetWindowMaximizeCallback(m_native_window, EditorWindow::__OnGlfwWindowMaximized);
        glfwSetWindowIconifyCallback(m_native_window, EditorWindow::__OnGlfwWindowMinimized);

        glfwSetMouseButtonCallback(m_native_window, EditorWindow::__OnGlfwMouseButtonRaised);
        glfwSetScrollCallback(m_native_window, EditorWindow::__OnGlfwMouseScrollRaised);
        glfwSetKeyCallback(m_native_window, EditorWindow::__OnGlfwKeyboardRaised);

        glfwSetCursorPosCallback(m_native_window, EditorWindow::__OnGlfwCursorMoved);
        glfwSetCharCallback(m_native_window, EditorWindow::__OnGlfwTextInputRaised);

        glfwMaximizeWindow(m_native_window);
    }

    void EditorWindow::InitializeLayer()
    {
        auto& layer_stack = *m_layer_stack_ptr;

        // Initialize in reverse order, so overlay layers can be initialize first
        // this give us opportunity to initialize UI-like layers before graphic render-like layers
        for (auto rlayer_it = std::rbegin(layer_stack); rlayer_it != std::rend(layer_stack); ++rlayer_it)
        {
            (*rlayer_it)->SetAttachedWindow(this);
            (*rlayer_it)->Initialize();
        }

        ZENGINE_CORE_INFO("Windows layers initialized")
    }

    void EditorWindow::Deinitialize()
    {
        auto& layer_stack = *m_layer_stack_ptr;
        for (auto rlayer_it = std::rbegin(layer_stack); rlayer_it != std::rend(layer_stack); ++rlayer_it)
        {
            (*rlayer_it)->Deinitialize();
        }

        m_swapchain.reset();
    }

    void EditorWindow::PollEvent()
    {
        glfwPollEvents();
    }

    float EditorWindow::GetTime()
    {
        return (float) glfwGetTime();
    }

    float EditorWindow::GetDeltaTime()
    {
        static float last_frame_time = 0.f;

        float time      = GetTime();
        m_delta_time    = time - last_frame_time;
        last_frame_time = time;
        return m_delta_time;
    }

    void EditorWindow::__OnGlfwFrameBufferSizeChanged(GLFWwindow* window, int width, int height)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            property->SetWidth(width);
            property->SetHeight(height);

            ZENGINE_CORE_INFO("Window size updated, Width = {0}, Height = {1}", property->Width, property->Height)
        }
    }

    void EditorWindow::__OnGlfwWindowClose(GLFWwindow* window)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            WindowClosedEvent e;
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwWindowResized(GLFWwindow* window, int width, int height)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            Event::WindowResizedEvent e{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwWindowMaximized(GLFWwindow* window, int maximized)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            if (maximized == GLFW_TRUE)
            {
                Event::WindowMaximizedEvent e;
                property->CallbackFn(e);
                return;
            }

            Event::WindowRestoredEvent e;
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwWindowMinimized(GLFWwindow* window, int minimized)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            if (minimized == GLFW_TRUE)
            {
                Event::WindowMinimizedEvent e;
                property->CallbackFn(e);
                return;
            }
            Event::WindowRestoredEvent e;
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwMouseButtonRaised(GLFWwindow* window, int button, int action, int mods)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            if (action == GLFW_PRESS)
            {
                Event::MouseButtonPressedEvent e{static_cast<Inputs::GlfwKeyCode>(button)};
                property->CallbackFn(e);
                return;
            }

            Event::MouseButtonReleasedEvent e{static_cast<Inputs::GlfwKeyCode>(button)};
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwMouseScrollRaised(GLFWwindow* window, double xoffset, double yoffset)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            MouseButtonWheelEvent e{xoffset, yoffset};
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwCursorMoved(GLFWwindow* window, double xoffset, double yoffset)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            MouseButtonMovedEvent e{xoffset, yoffset};
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwTextInputRaised(GLFWwindow* window, unsigned int character)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            std::string arr;
            arr.append(1, character);
            TextInputEvent e{arr.c_str()};
            property->CallbackFn(e);
        }
    }

    void EditorWindow::__OnGlfwKeyboardRaised(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            switch (action)
            {
                case GLFW_PRESS:
                {
                    Event::KeyPressedEvent e{static_cast<Inputs::GlfwKeyCode>(key), 0};
                    property->CallbackFn(e);
                    break;
                }

                case GLFW_RELEASE:
                {
                    Event::KeyReleasedEvent e{static_cast<Inputs::GlfwKeyCode>(key)};
                    property->CallbackFn(e);
                    break;
                }

                case GLFW_REPEAT:
                {
                    Event::KeyPressedEvent e{static_cast<Inputs::GlfwKeyCode>(key), 0};
                    property->CallbackFn(e);
                    break;
                }
            }

            if (key == GLFW_KEY_ESCAPE)
            {
                WindowClosedEvent e;
                property->CallbackFn(e);
            }
        }
    }

    void EditorWindow::Update(Core::TimeStep delta_time)
    {
        for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr)
        {
            layer->Update(delta_time);
        }
    }

    void EditorWindow::Render()
    {
        for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr)
        {
            layer->Render();
        }

        m_swapchain->Present();
    }

    bool EditorWindow::CreateSurface(void* instance, void** out_window_surface)
    {
        if (!instance || !out_window_surface)
        {
            return false;
        }
        VkInstance    vkInstance = reinterpret_cast<VkInstance>(instance);
        VkSurfaceKHR* pSurface   = reinterpret_cast<VkSurfaceKHR*>(out_window_surface);
        VkResult      result     = glfwCreateWindowSurface(vkInstance, m_native_window, nullptr, pSurface);
        return (result == VK_SUCCESS);
    }

    std::vector<std::string> EditorWindow::GetRequiredExtensionLayers()
    {
        uint32_t                 count                  = 0;
        const char**             extensions_layer_names = glfwGetRequiredInstanceExtensions(&count);
        std::vector<std::string> outputs(count);

        for (unsigned i = 0; i < count; ++i)
        {
            outputs[i] = extensions_layer_names[i];
        }
        return outputs;
    }

    Ref<Rendering::Swapchain> EditorWindow::GetSwapchain() const
    {
        return m_swapchain;
    }

    EditorWindow::~EditorWindow()
    {
        glfwSetErrorCallback(NULL);
        glfwDestroyWindow(m_native_window);
        glfwTerminate();
    }

    uint32_t EditorWindow::GetHeight() const
    {
        return m_property.Height;
    }

    bool EditorWindow::OnWindowClosed(WindowClosedEvent& event)
    {
        glfwSetWindowShouldClose(m_native_window, GLFW_TRUE);
        ZENGINE_CORE_INFO("Window has been closed")

        Event::EngineClosedEvent e(event.GetName());
        Event::EventDispatcher   event_dispatcher(e);
        event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnWindowResized(WindowResizedEvent& event)
    {
        if (event.GetWidth() > 0 && event.GetHeight() > 0)
        {
            m_swapchain->Resize();
        }

        ZENGINE_CORE_INFO("Window has been resized")

        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowResizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool EditorWindow::OnWindowMinimized(Event::WindowMinimizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been minimized")

        m_property.IsMinimized = true;
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowMinimizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool EditorWindow::OnWindowMaximized(Event::WindowMaximizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been maximized")

        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowMaximizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool EditorWindow::OnWindowRestored(Event::WindowRestoredEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been restored")

        m_property.IsMinimized = false;
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowRestoredEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool EditorWindow::OnEvent(Event::CoreEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&EditorWindow::OnWindowClosed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&EditorWindow::OnWindowResized, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&EditorWindow::OnKeyPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&EditorWindow::OnKeyReleased, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&EditorWindow::OnMouseButtonPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&EditorWindow::OnMouseButtonReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&EditorWindow::OnMouseButtonMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&EditorWindow::OnMouseButtonWheelMoved, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&EditorWindow::OnTextInputRaised, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::WindowMinimizedEvent>(std::bind(&EditorWindow::OnWindowMinimized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::WindowMaximizedEvent>(std::bind(&EditorWindow::OnWindowMaximized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::WindowRestoredEvent>(std::bind(&EditorWindow::OnWindowRestored, this, std::placeholders::_1));

        return true;
    }

    bool EditorWindow::OnKeyPressed(KeyPressedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::KeyPressedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnKeyReleased(KeyReleasedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::KeyReleasedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnMouseButtonPressed(MouseButtonPressedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonPressedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnMouseButtonReleased(MouseButtonReleasedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonReleasedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnMouseButtonMoved(MouseButtonMovedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonMovedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnMouseButtonWheelMoved(MouseButtonWheelEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonWheelEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool EditorWindow::OnTextInputRaised(TextInputEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::TextInputEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }
} // namespace Tetragrama

namespace ZEngine::Window
{
    CoreWindow* Create(const WindowConfiguration& configuration)
    {
        auto core_window = new Tetragrama::EditorWindow(configuration);
        core_window->SetCallbackFunction(std::bind(&CoreWindow::OnEvent, core_window, std::placeholders::_1));
        return core_window;
    }
} // namespace ZEngine::Window
