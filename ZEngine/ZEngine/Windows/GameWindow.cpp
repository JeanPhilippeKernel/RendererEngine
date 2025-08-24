#include <Core/Coroutine.h>
#include <Engine.h>
#include <Event/EngineClosedEvent.h>
#include <GameWindow.h>
#include <Inputs/IDevice.h>
#include <Inputs/KeyCode.h>
#include <Logging/LoggerDefinition.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32

#include <ShObjIdl.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;

#endif
#include <GLFW/glfw3native.h>

using namespace ZEngine;
using namespace ZEngine::Windows::Events;
using namespace ZEngine::Windows;
using namespace ZEngine::Rendering;
using namespace ZEngine::Helpers;
using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Windows
{
    uint32_t GameWindow::GetWidth() const
    {
        return m_property.Width;
    }

    Core::Containers::StringView GameWindow::GetTitle() const
    {
        return m_property.Title;
    }

    bool GameWindow::IsMinimized() const
    {
        return m_property.IsMinimized;
    }

    void GameWindow::SetTitle(Core::Containers::StringView title)
    {
        m_property.Title = title.data();
        glfwSetWindowTitle(m_native_window, m_property.Title);
    }

    bool GameWindow::IsVSyncEnable() const
    {
        return m_property.VSync;
    }

    void GameWindow::SetVSync(bool value)
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

    void GameWindow::SetCallbackFunction(const EventCallbackFn& callback)
    {
        m_property.CallbackFn = callback;
    }

    void* GameWindow::GetNativeWindow() const
    {
        return reinterpret_cast<void*>(m_native_window);
    }

    const WindowProperty& GameWindow::GetWindowProperty() const
    {
        return m_property;
    }

    void GameWindow::Initialize(Core::Memory::ArenaAllocator* arena, const WindowConfiguration& cfg)
    {
        m_configuration   = cfg;

        m_property.Height = cfg.Height;
        m_property.Width  = cfg.Width;
        m_property.Title  = cfg.Title.c_str();
        m_property.VSync  = cfg.EnableVsync;

        int glfw_init     = glfwInit();
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

        m_native_window = glfwCreateWindow(m_property.Width, m_property.Height, m_property.Title, NULL, NULL);

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

        uint32_t     count                  = 0;
        const char** extensions_layer_names = glfwGetRequiredInstanceExtensions(&count);
        RequiredExtensionLayers.init(arena, count, count);
        for (unsigned i = 0; i < count; ++i)
        {
            RequiredExtensionLayers[i] = extensions_layer_names[i];
        }

#ifdef _WIN32
        auto native_hwnd = glfwGetWin32Window(m_native_window);
        m_property.Dpi   = GetDpiForWindow(native_hwnd);
#endif // _WIN32

        float x_scale, y_scale;
        glfwGetWindowContentScale(m_native_window, &x_scale, &y_scale);
        m_property.DpiScale = x_scale;

        ZENGINE_CORE_INFO("Window created, Width = {0}, Height = {1}", m_property.Width, m_property.Height)

        // Initialize Input Devices (Keyboard, Mouse)
        Inputs::IDevice::Initialize(arena);

        ZENGINE_CORE_INFO("Input devices initialized")

        glfwSetWindowUserPointer(m_native_window, &m_property);

        glfwSetFramebufferSizeCallback(m_native_window, GameWindow::__OnGlfwFrameBufferSizeChanged);

        glfwSetWindowCloseCallback(m_native_window, GameWindow::__OnGlfwWindowClose);
        glfwSetWindowSizeCallback(m_native_window, GameWindow::__OnGlfwWindowResized);
        glfwSetWindowMaximizeCallback(m_native_window, GameWindow::__OnGlfwWindowMaximized);
        glfwSetWindowIconifyCallback(m_native_window, GameWindow::__OnGlfwWindowMinimized);

        glfwSetMouseButtonCallback(m_native_window, GameWindow::__OnGlfwMouseButtonRaised);
        glfwSetScrollCallback(m_native_window, GameWindow::__OnGlfwMouseScrollRaised);
        glfwSetKeyCallback(m_native_window, GameWindow::__OnGlfwKeyboardRaised);

        glfwSetCursorPosCallback(m_native_window, GameWindow::__OnGlfwCursorMoved);
        glfwSetCharCallback(m_native_window, GameWindow::__OnGlfwTextInputRaised);

        glfwMaximizeWindow(m_native_window);
    }

    void GameWindow::PollEvent()
    {
        glfwPollEvents();
    }

    float GameWindow::GetTime()
    {
        return (float) glfwGetTime();
    }

    float GameWindow::GetDeltaTime()
    {
        static float last_frame_time = 0.f;

        float        time            = GetTime();
        m_delta_time                 = time - last_frame_time;
        last_frame_time              = time;
        return m_delta_time;
    }

    void GameWindow::__OnGlfwFrameBufferSizeChanged(GLFWwindow* window, int width, int height)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            property->SetWidth(width);
            property->SetHeight(height);

            ZENGINE_CORE_INFO("Window size updated, Width = {0}, Height = {1}", property->Width, property->Height)
        }
    }

    void GameWindow::__OnGlfwWindowClose(GLFWwindow* window)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            WindowClosedEvent e;
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwWindowResized(GLFWwindow* window, int width, int height)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            WindowResizedEvent e{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwWindowMaximized(GLFWwindow* window, int maximized)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            if (maximized == GLFW_TRUE)
            {
                WindowMaximizedEvent e;
                property->CallbackFn(e);
                return;
            }

            WindowRestoredEvent e;
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwWindowMinimized(GLFWwindow* window, int minimized)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            if (minimized == GLFW_TRUE)
            {
                WindowMinimizedEvent e;
                property->CallbackFn(e);
                return;
            }
            WindowRestoredEvent e;
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwMouseButtonRaised(GLFWwindow* window, int button, int action, int mods)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            if (action == GLFW_PRESS)
            {
                MouseButtonPressedEvent e{static_cast<Inputs::GlfwKeyCode>(button)};
                property->CallbackFn(e);
                return;
            }

            MouseButtonReleasedEvent e{static_cast<Inputs::GlfwKeyCode>(button)};
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwMouseScrollRaised(GLFWwindow* window, double xoffset, double yoffset)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            MouseButtonWheelEvent e{xoffset, yoffset};
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwCursorMoved(GLFWwindow* window, double xoffset, double yoffset)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            MouseButtonMovedEvent e{xoffset, yoffset};
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwTextInputRaised(GLFWwindow* window, unsigned int character)
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

    void GameWindow::__OnGlfwKeyboardRaised(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (!property)
        {
            return;
        }

        if (action == GLFW_PRESS)
        {
            KeyPressedEvent e{static_cast<Inputs::GlfwKeyCode>(key), 0};
            property->CallbackFn(e);
        }

        if (action == GLFW_RELEASE)
        {
            KeyReleasedEvent e{static_cast<Inputs::GlfwKeyCode>(key)};
            property->CallbackFn(e);
        }

        if (action == GLFW_REPEAT)
        {
            KeyPressedEvent e{static_cast<Inputs::GlfwKeyCode>(key), 0};
            property->CallbackFn(e);
        }

        if (key == GLFW_KEY_ESCAPE)
        {
            WindowClosedEvent e;
            property->CallbackFn(e);
        }
    }

    std::future<std::string> GameWindow::OpenFileDialogAsync(std::span<std::string_view> type_filters)
    {
        std::string path{""};
#ifdef _WIN32

        auto           native_hwnd = glfwGetWin32Window(m_native_window);

        FileOpenPicker file_picker;
        file_picker.ViewMode(PickerViewMode::Thumbnail);
        file_picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
        file_picker.as<::IInitializeWithWindow>()->Initialize(native_hwnd);

        if (!type_filters.empty())
        {
            auto filters = file_picker.FileTypeFilter();
            filters.Clear();

            for (std::string_view type : type_filters)
            {
                filters.Append(winrt::to_hstring(type));
            }
        }

        IStorageFile file = co_await file_picker.PickSingleFileAsync();

        if (file)
        {
            path = winrt::to_string(file.Path());
        }
#endif
        co_return path;
    }

    bool GameWindow::CreateSurface(void* instance, void** out_window_surface)
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

    GameWindow::~GameWindow()
    {
        glfwSetErrorCallback(NULL);
        glfwDestroyWindow(m_native_window);
        glfwTerminate();
    }

    uint32_t GameWindow::GetHeight() const
    {
        return m_property.Height;
    }

    bool GameWindow::OnWindowClosed(WindowClosedEvent& event)
    {
        glfwSetWindowShouldClose(m_native_window, GLFW_TRUE);
        ZENGINE_CORE_INFO("Window has been closed")

        Event::EngineClosedEvent e(event.GetName());
        Core::EventDispatcher    event_dispatcher(e);
        event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, std::placeholders::_1));
        return true;
    }

    bool GameWindow::OnWindowResized(WindowResizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been resized")

        return false;
    }

    bool GameWindow::OnWindowMinimized(WindowMinimizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been minimized")

        m_property.IsMinimized = true;
        return false;
    }

    bool GameWindow::OnWindowMaximized(WindowMaximizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been maximized")

        m_property.IsMinimized = false;
        return false;
    }

    bool GameWindow::OnWindowRestored(WindowRestoredEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been restored")

        m_property.IsMinimized = false;
        return false;
    }

    bool GameWindow::OnEvent(Core::CoreEvent& event)
    {
        Core::EventDispatcher event_dispatcher(event);
        event_dispatcher.Dispatch<WindowClosedEvent>(std::bind(&GameWindow::OnWindowClosed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<WindowResizedEvent>(std::bind(&GameWindow::OnWindowResized, this, std::placeholders::_1));

        event_dispatcher.Dispatch<WindowMinimizedEvent>(std::bind(&GameWindow::OnWindowMinimized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<WindowMaximizedEvent>(std::bind(&GameWindow::OnWindowMaximized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<WindowRestoredEvent>(std::bind(&GameWindow::OnWindowRestored, this, std::placeholders::_1));

        return true;
    }
} // namespace ZEngine::Windows
