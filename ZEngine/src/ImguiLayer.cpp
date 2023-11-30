#include <pch.h>
#include <Layers/ImguiLayer.h>
#include <ZEngineDef.h>
#include <fmt/format.h>
#include <ZEngine/Window/GlfwWindow/VulkanWindow.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/ImGUIRenderer.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Layers
{
    bool ImguiLayer::m_initialized = false;

    ImguiLayer::~ImguiLayer()
    {
    }

    void ImguiLayer::Initialize()
    {
        if (m_initialized)
        {
            ZENGINE_CORE_INFO("ImGUI layer already initialized")
            return;
        }

        if (auto current_window = m_window.lock())
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            StyleDarkTheme();

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigViewportsNoTaskBarIcon = true;
            io.ConfigViewportsNoDecoration  = true;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
            io.BackendRendererName = "ZEngine";

            std::string_view default_layout_ini = "Settings/DefaultLayout.ini";
            const auto       current_directoy   = std::filesystem::current_path();
            auto             layout_file_path   = fmt::format("{0}/{1}", current_directoy.string(), default_layout_ini);
            if (std::filesystem::exists(std::filesystem::path(layout_file_path)))
            {
                io.IniFilename = default_layout_ini.data();
            }

            // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            auto& style            = ImGui::GetStyle();
            style.WindowBorderSize = 0.f;
            style.ChildBorderSize  = 0.f;
            style.FrameRounding    = 7.0f;

            auto window_property = current_window->GetWindowProperty();
            io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f * window_property.DpiScale);
            io.FontDefault = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f * window_property.DpiScale);
            io.FontGlobalScale = window_property.DpiScale;

            ImGUIRenderer::Initialize(current_window->GetNativeWindow(), current_window->GetSwapchain());
            auto& platform_io                  = ImGui::GetPlatformIO();
            platform_io.Renderer_CreateWindow  = __ImGUIRendererCreateWindowCallback;
            platform_io.Renderer_DestroyWindow = __ImGUIPlatformDestroyWindowCallback;
            platform_io.Renderer_RenderWindow  = __ImGUIPlatformRenderWindowCallback;
            platform_io.Renderer_SetWindowSize = __ImGUIPlatformSetWindowSizeCallback;
            platform_io.Renderer_SwapBuffers   = __ImGUIPlatformSwapBuffersCallback;

            m_initialized = true;
        }
    }

    void ImguiLayer::Deinitialize()
    {
        m_ui_components.clear();
        m_ui_components.shrink_to_fit();

        if (m_initialized)
        {
            ImGUIRenderer::Deinitialize();
            m_initialized = false;
        }
    }

    bool ImguiLayer::OnEvent(Event::CoreEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);

        event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&ImguiLayer::OnKeyPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&ImguiLayer::OnKeyReleased, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&ImguiLayer::OnMouseButtonPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&ImguiLayer::OnMouseButtonReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&ImguiLayer::OnMouseButtonMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&ImguiLayer::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&ImguiLayer::OnTextInputRaised, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&ImguiLayer::OnWindowClosed, this, std::placeholders::_1));

        return false;
    }

    void ImguiLayer::Update(Core::TimeStep dt)
    {
        ImGUIRenderer::Tick();

        ImGuiIO& io = ImGui::GetIO();
        if (dt > 0.0f)
        {
            io.DeltaTime = dt;
        }
        for (const auto& component : m_ui_components)
        {
            component->Update(dt);
        }

        if (ImGUIRenderer::HasPendingCleanupResource())
        {
            ImGUIRenderer::CleanupResource();
        }
    }

    void ImguiLayer::AddUIComponent(const Ref<Components::UI::UIComponent>& component)
    {
        m_ui_components.push_back(component);

        if (!component->HasParentLayer())
        {
            auto last = std::prev(std::end(m_ui_components));
            (*last)->SetParentLayer(this);
        }
    }

    void ImguiLayer::AddUIComponent(Ref<Components::UI::UIComponent>&& component)
    {
        if (!component->HasParentLayer())
        {
            component->SetParentLayer(this);
        }
        m_ui_components.push_back(component);
    }

    void ImguiLayer::AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components)
    {
        std::for_each(std::begin(components), std::end(components),
            [this](Ref<Components::UI::UIComponent>& component)
            {
                if (!component->HasParentLayer())
                {
                    component->SetParentLayer(this);
                }
            });

        std::move(std::begin(components), std::end(components), std::back_inserter(m_ui_components));
    }

    bool ImguiLayer::OnKeyPressed(Event::KeyPressedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = true;
        return false;
    }

    bool ImguiLayer::OnKeyReleased(Event::KeyReleasedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonPressed(Event::MouseButtonPressedEvent& e)
    {
        ImGuiIO& io                            = ImGui::GetIO();
        io.MouseDown[(uint32_t) e.GetButton()] = true;
        return false;
    }

    bool ImguiLayer::OnMouseButtonReleased(Event::MouseButtonReleasedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.MouseDown[(int) e.GetButton()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonMoved(Event::MouseButtonMovedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(float(e.GetPosX()), float(e.GetPosY()));
        return false;
    }

    bool ImguiLayer::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (e.GetOffetX() > 0)
        {
            io.MouseWheelH += 1;
        }
        if (e.GetOffetX() < 0)
        {
            io.MouseWheelH -= 1;
        }
        if (e.GetOffetY() > 0)
        {
            io.MouseWheel += 1;
        }
        if (e.GetOffetY() < 0)
        {
            io.MouseWheel -= 1;
        }
        return false;
    }

    bool ImguiLayer::OnTextInputRaised(Event::TextInputEvent& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        for (unsigned char c : event.GetText())
        {
            io.AddInputCharacter(c);
        }
        return false;
    }

    bool ImguiLayer::OnWindowClosed(Event::WindowClosedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowClosedEvent>(std::bind(&ZEngine::Window::CoreWindow::OnWindowClosed, GetAttachedWindow().get(), std::placeholders::_1));
        return true;
    }

    bool ImguiLayer::OnWindowResized(Event::WindowResizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowMinimized(Event::WindowMinimizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowMaximized(Event::WindowMaximizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowRestored(Event::WindowRestoredEvent&)
    {
        return false;
    }

    void ImguiLayer::StyleDarkTheme()
    {
        auto& colors              = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{0.1f, 0.105f, 0.11f, 1.0f};

        // Headers
        colors[ImGuiCol_Header]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_HeaderHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_HeaderActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Buttons
        colors[ImGuiCol_Button]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_ButtonActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Frame BG
        colors[ImGuiCol_FrameBg]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_FrameBgHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_FrameBgActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Tabs
        colors[ImGuiCol_Tab]                = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabHovered]         = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
        colors[ImGuiCol_TabActive]          = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
        colors[ImGuiCol_TabUnfocused]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

        // Title
        colors[ImGuiCol_TitleBg]          = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgActive]    = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        colors[ImGuiCol_DockingPreview]   = ImVec4{0.2f, 0.205f, 0.21f, .5f};
        colors[ImGuiCol_SeparatorHovered] = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_SeparatorActive]  = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_CheckMark]        = ImVec4{1.0f, 1.f, 1.0f, 1.f};
    }

    void ImguiLayer::__ImGUIRendererCreateWindowCallback(ImGuiViewport* viewport)
    {
        ImguiViewPortWindowChild* viewport_window = new ImguiViewPortWindowChild{viewport->PlatformHandle};
        viewport->RendererUserData                = viewport_window;
    }

    void ImguiLayer::__ImGUIPlatformDestroyWindowCallback(ImGuiViewport* viewport)
    {
        if (viewport->RendererUserData)
        {
            auto window_child = reinterpret_cast<ImguiViewPortWindowChild*>(viewport->RendererUserData);
            Hardwares::VulkanDevice::DisposeCommandPool(window_child->CommandPool);
            window_child->Swapchain.reset();
            delete window_child;
        }
        viewport->RendererUserData = nullptr;
    }

    void ImguiLayer::__ImGUIPlatformRenderWindowCallback(ImGuiViewport* viewport, void* args)
    {
        if (viewport->RendererUserData)
        {
            auto window_child = reinterpret_cast<ImguiViewPortWindowChild*>(viewport->RendererUserData);
            ImGUIRenderer::DrawChildWindow(viewport->Size.x, viewport->Size.y, &window_child, viewport->DrawData);
        }
    }

    void ImguiLayer::__ImGUIPlatformSwapBuffersCallback(ImGuiViewport* viewport, void* args)
    {
        if (viewport->RendererUserData)
        {
            auto window_child = reinterpret_cast<ImguiViewPortWindowChild*>(viewport->RendererUserData);
            window_child->Swapchain->Present();
        }
    }

    void ImguiLayer::__ImGUIPlatformSetWindowSizeCallback(ImGuiViewport* viewport, ImVec2 size)
    {
        if (viewport->RendererUserData)
        {
            auto window_child = reinterpret_cast<ImguiViewPortWindowChild*>(viewport->RendererUserData);
            window_child->Swapchain->Resize();
        }
    }

    void ImguiLayer::Render()
    {
        if (auto window_ptr = m_window.lock())
        {
            if (window_ptr->IsMinimized())
            {
                return;
            }

            auto current_vulkan_window_ptr = reinterpret_cast<Window::GLFWWindow::VulkanWindow*>(window_ptr.get());
            auto swapchain                 = current_vulkan_window_ptr->GetSwapchain();

            ImGUIRenderer::BeginFrame();

            for (const auto& component : m_ui_components)
            {
                if (component->GetVisibility() == true)
                {
                    component->Render();
                }
            }
            ImGui::Render();
            /*Frame and CommandBuffer preparing*/
            ImGUIRenderer::Draw(window_ptr->GetWidth(), window_ptr->GetHeight(), swapchain, nullptr, ImGui::GetDrawData());

            ImGUIRenderer::EndFrame();
        }
    }
} // namespace ZEngine::Layers
