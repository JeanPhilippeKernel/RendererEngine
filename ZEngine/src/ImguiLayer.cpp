#include <pch.h>
#include <Layers/ImguiLayer.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

#include <GLFW/glfw3.h>

#include <backends/imgui_impl_vulkan.h>
#include <Engine.h>
#include <ZEngine/Window/GlfwWindow/VulkanWindow.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Helpers/RendererHelper.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Layers
{
    bool ImguiLayer::m_initialized = false;

    ImguiLayer::~ImguiLayer()
    {
        m_ui_components.clear();
        m_ui_components.shrink_to_fit();
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
            auto current_window_ptr        = current_window.get();
            auto current_vulkan_window_ptr = reinterpret_cast<Window::GLFWWindow::VulkanWindow*>(current_window_ptr);
            auto swapchain                 = current_vulkan_window_ptr->GetSwapchain();

            m_ui_command_pool = Hardwares::VulkanDevice::CreateCommandPool(Rendering::QueueType::GRAPHIC_QUEUE, true);

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

            // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            auto& style            = ImGui::GetStyle();
            style.WindowBorderSize = 0.f;
            style.ChildBorderSize  = 0.f;

            io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f);
            io.FontDefault = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

            ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window_ptr->GetNativeWindow()), false);

            ImGui_ImplVulkan_InitInfo imgui_vulkan_init_info = {};
            imgui_vulkan_init_info.Instance                  = Hardwares::VulkanDevice::GetNativeInstanceHandle();
            imgui_vulkan_init_info.PhysicalDevice            = Hardwares::VulkanDevice::GetNativePhysicalDeviceHandle();
            imgui_vulkan_init_info.Device                    = Hardwares::VulkanDevice::GetNativeDeviceHandle();
            auto queue_view                                  = Hardwares::VulkanDevice::GetQueue(Rendering::QueueType::GRAPHIC_QUEUE);
            imgui_vulkan_init_info.QueueFamily               = queue_view.FamilyIndex;
            imgui_vulkan_init_info.Queue                     = queue_view.Handle;
            imgui_vulkan_init_info.PipelineCache             = nullptr;
            imgui_vulkan_init_info.DescriptorPool            = Hardwares::VulkanDevice::GetDescriptorPool();
            imgui_vulkan_init_info.Allocator                 = nullptr;
            imgui_vulkan_init_info.MinImageCount             = swapchain->GetMinImageCount();
            imgui_vulkan_init_info.ImageCount                = swapchain->GetImageCount();
            imgui_vulkan_init_info.CheckVkResultFn           = nullptr;

            ImGui_ImplVulkan_Init(&imgui_vulkan_init_info, swapchain->GetRenderPass());

            // Upload Fonts
            auto command_buffer = Hardwares::VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
            {
                ImGui_ImplVulkan_CreateFontsTexture(command_buffer->GetHandle());
            }
            Hardwares::VulkanDevice::EndInstantCommandBuffer(command_buffer);

            m_initialized = true;
        }
    }

    void ImguiLayer::Deinitialize()
    {
        if (m_initialized)
        {
            Hardwares::VulkanDevice::DisposeCommandPool(m_ui_command_pool);
            ImGui_ImplVulkan_DestroyFontUploadObjects();
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();

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
        std::for_each(std::begin(components), std::end(components),
            [this](Ref<Components::UI::UIComponent>& component)
            {
                if (!component->HasParentLayer())
                {
                    component->SetParentLayer(shared_from_this());
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

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();

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

            /*Frame and CommandBuffer preparing*/
            ZENGINE_VALIDATE_ASSERT(m_ui_command_pool != nullptr, "UI Command pool can't be null")

            auto command_buffer = m_ui_command_pool->GetCurrentCommmandBuffer();
            command_buffer->Begin();
            {
                // Begin RenderPass
                VkRenderPassBeginInfo render_pass_begin_info    = {};
                render_pass_begin_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                render_pass_begin_info.renderPass               = swapchain->GetRenderPass();
                render_pass_begin_info.framebuffer              = swapchain->GetCurrentFramebuffer();
                render_pass_begin_info.renderArea.extent.width  = window_ptr->GetWidth();
                render_pass_begin_info.renderArea.extent.height = window_ptr->GetHeight();

                std::array<VkClearValue, 2> clear_values = {};
                clear_values[0].color                    = {{0.0f, 0.0f, 0.0f, 1.0f}};
                clear_values[1].depthStencil             = {1.0f, 0};
                render_pass_begin_info.clearValueCount   = clear_values.size();
                render_pass_begin_info.pClearValues      = clear_values.data();
                vkCmdBeginRenderPass(command_buffer->GetHandle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

                // Record dear imgui primitives into command buffer
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer->GetHandle());

                vkCmdEndRenderPass(command_buffer->GetHandle());
            }
            command_buffer->End();

            // Submit UI Command buffer
            command_buffer->Submit();

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
    }
} // namespace ZEngine::Layers
