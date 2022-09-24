#include <pch.h>
#include <Layers/ImguiLayer.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

#ifdef ZENGINE_WINDOW_SDL
#include <SDL.h>
#else
#include <GLFW/glfw3.h>
#endif

#include <backends/imgui_impl_vulkan.h>
#include <Engine.h>
#include <ZEngine/Window/GlfwWindow/OpenGLWindow.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Layers
{

    bool ImguiLayer::m_initialized = false;

    ImguiLayer::~ImguiLayer()
    {
        m_ui_components.clear();
        if (m_initialized)
        {
            ImGui_ImplOpenGL3_Shutdown();

#ifdef ZENGINE_WINDOW_SDL
            ImGui_ImplSDL2_Shutdown();
#else
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
#endif
            ImGui::DestroyContext();

            m_initialized = false;
        }
    }

    void ImguiLayer::Initialize()
    {
        if (!m_initialized)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            StyleDarkTheme();

            ImGuiIO& io = ImGui::GetIO();

            std::string_view default_layout_ini = "Settings/DefaultLayout.ini";
            const auto       current_directoy   = std::filesystem::current_path();
            auto             layout_file_path   = fmt::format("{0}/{1}", current_directoy.string(), default_layout_ini);
            if (std::filesystem::exists(std::filesystem::path(layout_file_path)))
            {
                io.IniFilename = default_layout_ini.data();
            }

            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f);
            io.FontDefault = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

            auto& style            = ImGui::GetStyle();
            style.WindowBorderSize = 0.f;
            style.ChildBorderSize  = 0.f;

#ifdef ZENGINE_WINDOW_SDL
            ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(m_window.lock()->GetNativeWindow()), m_window.lock()->GetNativeContext());
#else
            ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(m_window.lock()->GetNativeWindow()), false);
#endif

            auto current_engine_ptr = m_window.lock()->GetAttachedEngine();

            // Create Descriptor Pool
            VkDescriptorPoolSize pool_sizes[]    = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                   {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                   {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                   {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets                    = 1000 * IM_ARRAYSIZE(pool_sizes);
            pool_info.poolSizeCount              = (uint32_t) IM_ARRAYSIZE(pool_sizes);
            pool_info.pPoolSizes                 = pool_sizes;
            vkCreateDescriptorPool(current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice().GetNativeDeviceHandle(), &pool_info, nullptr, &m_descriptor_pool);


            // we should get Version information from attached Window...
            ImGui_ImplVulkan_InitInfo imgui_vulkan_init_info = {};
            imgui_vulkan_init_info.Instance                  = current_engine_ptr->GetVulkanInstance().GetNativeHandle();
            imgui_vulkan_init_info.PhysicalDevice            = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice().GetNativePhysicalDeviceHandle();
            imgui_vulkan_init_info.Device                    = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice().GetNativeDeviceHandle();
            imgui_vulkan_init_info.QueueFamily               = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice().GetGraphicQueueFamilyIndexCollection().at(0);
            imgui_vulkan_init_info.Queue                     = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice().GetCurrentGraphicQueue();
            imgui_vulkan_init_info.PipelineCache             = nullptr;
            imgui_vulkan_init_info.DescriptorPool            = m_descriptor_pool;
            imgui_vulkan_init_info.Allocator                 = nullptr;
            imgui_vulkan_init_info.MinImageCount             = reinterpret_cast<Window::GLFWWindow::OpenGLWindow*>(m_window.lock().get())->GetSwapChainMinImageCount();
            imgui_vulkan_init_info.ImageCount                = reinterpret_cast<Window::GLFWWindow::OpenGLWindow*>(m_window.lock().get())->GetSwapChainImageCollection().size();
            imgui_vulkan_init_info.CheckVkResultFn           = __imguiVulkanCallback;

            ImGui_ImplVulkan_Init(&imgui_vulkan_init_info, reinterpret_cast<Window::GLFWWindow::OpenGLWindow*>(m_window.lock().get())->GetRenderPass());

            m_initialized = true;
        }
    }

    void ImguiLayer::Update(Core::TimeStep dt)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (dt > 0.0f)
        {
            io.DeltaTime = dt;
        }
        for (const auto& component : m_ui_components)
        {
            component->Update(dt);
        }
    }

    void ImguiLayer::AddUIComponent(const Ref<Components::UI::UIComponent>& component)
    {
        m_ui_components.push_back(component);

        if (!component->HasParentLayer())
        {
            auto last = std::prev(std::end(m_ui_components));
            (*last)->SetParentLayer(shared_from_this());
        }
    }

    void ImguiLayer::AddUIComponent(Ref<Components::UI::UIComponent>&& component)
    {
        if (!component->HasParentLayer())
        {
            component->SetParentLayer(shared_from_this());
        }
        m_ui_components.push_back(component);
    }

    void ImguiLayer::AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components)
    {
        std::for_each(std::begin(components), std::end(components), [this](Ref<Components::UI::UIComponent>& component) {
            if (!component->HasParentLayer())
            {
                component->SetParentLayer(shared_from_this());
            }
        });

        std::move(std::begin(components), std::end(components), std::back_inserter(m_ui_components));
    }

    void ImguiLayer::Render()
    {
        ImGui_ImplVulkan_NewFrame();
#ifdef ZENGINE_WINDOW_SDL
        ImGui_ImplSDL2_NewFrame(static_cast<SDL_Window*>(m_window.lock()->GetNativeWindow()));
#else
        ImGui_ImplGlfw_NewFrame();
#endif

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        for (const auto& component : m_ui_components)
        {
            if (component->GetVisibility() == true)
            {
                component->Render();
            }
        }

        ImGui::Render();
        ImDrawData* main_draw_data = ImGui::GetDrawData();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
#ifdef ZENGINE_WINDOW_SDL
            SDL_Window*   backup_current_window  = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
#else
            GLFWwindow* backup_current_context = static_cast<GLFWwindow*>(m_window.lock()->GetNativeContext());
#endif
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();

#ifdef ZENGINE_WINDOW_SDL
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
#else
            glfwMakeContextCurrent(backup_current_context);
#endif
        }
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
            io.MouseWheelH += 1;
        if (e.GetOffetX() < 0)
            io.MouseWheelH -= 1;
        if (e.GetOffetY() > 0)
            io.MouseWheel += 1;
        if (e.GetOffetY() < 0)
            io.MouseWheel -= 1;
        return false;
    }

    bool ImguiLayer::OnTextInputRaised(Event::TextInputEvent& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        for (unsigned char c : event.GetText())
            io.AddInputCharacter(c);
        return false;
    }

    bool ImguiLayer::OnWindowClosed(Event::WindowClosedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowClosedEvent>(std::bind(&ZEngine::Window::CoreWindow::OnWindowClosed, GetAttachedWindow().get(), std::placeholders::_1));
        return true;
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

    void ImguiLayer::__imguiVulkanCallback(VkResult result)
    {
        if (result == 0)
            return;

        if (result < 0)
        {
            ZENGINE_CORE_ERROR("Imgui Vulkan Error: {}", result)
            ZENGINE_EXIT_FAILURE()
        }

        ZENGINE_CORE_ERROR("Imgui Vulkan Error: {}", result)
    }

} // namespace ZEngine::Layers
