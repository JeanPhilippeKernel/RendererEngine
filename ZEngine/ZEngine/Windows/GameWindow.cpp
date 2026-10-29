#include <ZEngine/Engine.h>
#include <ZEngine/Windows/Platform/DesktopFileDialog.h>
#include <stb/stb_image.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if defined(__APPLE__)
// Implemented in Windows/Platform/MacOSFileDialog.mm. Async — shows the panel
// and returns immediately; on_complete fires later on the main run loop.
// Implemented in Windows/Platform/MacOSAppIcon.mm
extern "C" void ZEngineSetDockIcon(const char* path);
#endif
#include <ZEngine/Event/EngineClosedEvent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Windows/GameWindow.h>
#include <ZEngine/Windows/Inputs/IDevice.h>
#include <ZEngine/Windows/Inputs/KeyCode.h>

#if defined(__linux__)
#include <ZEngine/Windows/Platform/WaylandPortalFileDialog.h>
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

#if defined(__APPLE__)
        // GLFW otherwise tries to dlopen the release Vulkan loader name, while
        // Debug builds link the debug loader directly.
        glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif

        int glfw_init = glfwInit();
        if (glfw_init == GLFW_FALSE)
        {
            ZENGINE_CORE_CRITICAL("Unable to initialize glfw..")
            ZENGINE_EXIT_FAILURE();
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfwSetErrorCallback([](int error, const char* description) {
            ZENGINE_CORE_CRITICAL("{}", description)
            ZENGINE_EXIT_FAILURE();
        });

        m_native_window = glfwCreateWindow(m_property.Width, m_property.Height, m_property.Title, NULL, NULL);

        if (!m_native_window)
        {
            ZENGINE_CORE_CRITICAL("Failed to create GLFW Window")
            ZENGINE_EXIT_FAILURE();
        }

        {
            const auto* engine_context = Engine::GetContext();
            const auto  icon_path      = engine_context && engine_context->EngineAssetsNativeRoot ? (std::filesystem::path(engine_context->EngineAssetsNativeRoot) / "Settings/Icons/AppIconBadge.png").string() : std::string{};
#if !defined(__APPLE__)
#if defined(__linux__)
            // GLFW reports this as an error on Wayland, and this application's GLFW
            // error callback is intentionally fatal. Wayland has no window-icon API;
            // the compositor selects the application icon instead.
            if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
#endif
            {
                int            w = 0, h = 0, channels = 0;
                unsigned char* pixels = stbi_load(icon_path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
                if (pixels)
                {
                    GLFWimage icon{w, h, pixels};
                    glfwSetWindowIcon(m_native_window, 1, &icon);
                    stbi_image_free(pixels);
                }
            }
#else
            ZEngineSetDockIcon(icon_path.c_str());
#endif
        }

        // GetWidth/GetHeight describe physical framebuffer pixels, including before
        // the first resize callback (Retina and Wayland can already be scaled).
        int framebuffer_width = 0, framebuffer_height = 0;
        glfwGetFramebufferSize(m_native_window, &framebuffer_width, &framebuffer_height);
        m_property.SetWidth(static_cast<uint32_t>(framebuffer_width));
        m_property.SetHeight(static_cast<uint32_t>(framebuffer_height));

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
        glfwSetWindowFocusCallback(m_native_window, GameWindow::__OnGlfwWindowFocus);

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
                auto* engine_context = Engine::GetContext();
                if (engine_context && engine_context->InputManager)
                    engine_context->InputManager->SetWindowFocused(false);
                WindowMinimizedEvent e;
                property->CallbackFn(e);
                return;
            }
            WindowRestoredEvent e;
            property->CallbackFn(e);
        }
    }

    void GameWindow::__OnGlfwWindowFocus(GLFWwindow* window, int focused)
    {
        auto* engine_context = Engine::GetContext();
        if (engine_context && engine_context->InputManager)
            engine_context->InputManager->SetWindowFocused(focused == GLFW_TRUE);
        // Preserve the edge as well as the current level. A loss and regain can
        // both arrive inside one glfwPollEvents call, before the next app update.
        auto* property = static_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (!property || !property->CallbackFn)
            return;
        if (focused == GLFW_TRUE)
        {
            WindowFocusGainedEvent event = {};
            property->CallbackFn(event);
        }
        else
        {
            WindowFocusLostEvent event = {};
            property->CallbackFn(event);
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

    void GameWindow::__OnGlfwCursorMoved(GLFWwindow* window, double xpos, double ypos)
    {
        static double   lastX = 0, lastY = 0; // Initial center
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            auto* engine_context = Engine::GetContext();
            if (engine_context && engine_context->InputManager)
                engine_context->InputManager->AccumulateCursorPosition(xpos, ypos);

            double xoffset = (xpos - lastX);
            double yoffset = (ypos - lastY);
            lastX          = xpos;
            lastY          = ypos;
            MouseButtonMovedEvent e{xpos, ypos, xoffset, yoffset};
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
            KeyPressedEvent e{static_cast<Inputs::GlfwKeyCode>(key), 1};
            property->CallbackFn(e);
        }
    }

    FileDialogRequest GameWindow::OpenFileDialogAsync(std::span<std::string_view> type_filters, std::string_view default_dir, std::string_view message)
    {
        return FileDialogAsync(false, type_filters, default_dir, {}, message);
    }

    FileDialogRequest GameWindow::SaveFileDialogAsync(std::span<std::string_view> type_filters, std::string_view default_dir, std::string_view filename, std::string_view message)
    {
        return FileDialogAsync(true, type_filters, default_dir, filename, message);
    }

    FileDialogRequest GameWindow::FileDialogAsync(bool save, std::span<std::string_view> type_filters, std::string_view default_dir, std::string_view filename, std::string_view message)
    {
        return Platform::ShowDesktopFileDialog(m_native_window, save, type_filters, default_dir, filename, message);
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
        // GLFW sets this flag before the callback. The application may defer or
        // reject closing; keep the live window until Engine approves shutdown.
        glfwSetWindowShouldClose(m_native_window, GLFW_FALSE);
        ZENGINE_CORE_INFO("Window close requested")

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
