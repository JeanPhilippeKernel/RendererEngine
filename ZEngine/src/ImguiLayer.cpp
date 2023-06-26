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
#include <Helpers/BufferHelper.h>

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
        if (!m_initialized)
        {
            ZENGINE_VALIDATE_ASSERT(!m_window.expired(), "The Current Window instance has been destroyed");

            auto current_window            = m_window.lock();
            auto current_window_ptr        = current_window.get();
            auto current_vulkan_window_ptr = reinterpret_cast<Window::GLFWWindow::VulkanWindow*>(current_window_ptr);

            auto& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            auto  device_handle     = performant_device.GetNativeDeviceHandle();
            auto  present_queue     = performant_device.GetCurrentGraphicQueueWithPresentSupport().Queue;

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

            //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            auto& style            = ImGui::GetStyle();
            style.WindowBorderSize = 0.f;
            style.ChildBorderSize  = 0.f;

            io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f);
            io.FontDefault = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

            ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window_ptr->GetNativeWindow()), false);

            // Create Descriptor Pool
            std::vector<VkDescriptorPoolSize> pool_sizes = {
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets                    = 1000 * pool_sizes.size();
            pool_info.poolSizeCount              = pool_sizes.size();
            pool_info.pPoolSizes                 = pool_sizes.data();

            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(device_handle, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool -- ImGuiLayer");

            ImGui_ImplVulkan_InitInfo imgui_vulkan_init_info = {};
            imgui_vulkan_init_info.Instance                  = Engine::GetVulkanInstance()->GetNativeHandle();
            imgui_vulkan_init_info.PhysicalDevice            = performant_device.GetNativePhysicalDeviceHandle();
            imgui_vulkan_init_info.Device                    = device_handle;
            imgui_vulkan_init_info.QueueFamily               = performant_device.GetGraphicQueueFamilyIndexCollection().front();
            imgui_vulkan_init_info.Queue                     = present_queue;
            imgui_vulkan_init_info.PipelineCache             = nullptr;
            imgui_vulkan_init_info.DescriptorPool            = m_descriptor_pool;
            imgui_vulkan_init_info.Allocator                 = nullptr;
            imgui_vulkan_init_info.MinImageCount             = current_vulkan_window_ptr->GetSwapChainMinImageCount();
            imgui_vulkan_init_info.ImageCount                = current_vulkan_window_ptr->GetSwapChainImageCollection().size();
            imgui_vulkan_init_info.CheckVkResultFn           = nullptr;

            ImGui_ImplVulkan_Init(&imgui_vulkan_init_info, current_vulkan_window_ptr->GetRenderPass());

            // Upload Fonts
            // Use any command queue from any available Window Frame
            auto& selected_frame = current_vulkan_window_ptr->GetCurrentWindowFrame();

            ZENGINE_VALIDATE_ASSERT(vkResetCommandPool(device_handle, selected_frame.GraphicCommandPool, 0) == VK_SUCCESS, "Failed to reset Command Pool -- ImGuiLayer")

            VkCommandBuffer             command_buffer                     = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo graphic_command_buffer_create_info = {};
            graphic_command_buffer_create_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            graphic_command_buffer_create_info.commandPool                 = selected_frame.GraphicCommandPool;
            graphic_command_buffer_create_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            graphic_command_buffer_create_info.commandBufferCount          = 1;
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateCommandBuffers(performant_device.GetNativeDeviceHandle(), &graphic_command_buffer_create_info, &command_buffer) == VK_SUCCESS,
                "Failed to allocate Command Buffer")

            VkCommandBufferBeginInfo command_buffer_begin_info = {};
            command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            command_buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin Command Buffer -- ImGuiLayer")

            ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

            ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(command_buffer) == VK_SUCCESS, "Failed to end Command Buffer -- ImGuiLayer")

            VkCommandBuffer submit_info_command_buffer_array[] = {command_buffer};
            VkSubmitInfo    submit_info                        = {};
            submit_info.sType                                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount                     = 1;
            submit_info.pCommandBuffers                        = submit_info_command_buffer_array;

            ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(present_queue, 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS, "Failed to submit Command Buffer on Present Queue -- ImGuiLayer")

            ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(present_queue) == VK_SUCCESS, "Failed to wait for GPU device -- ImGuiLayer")

            ImGui_ImplVulkan_DestroyFontUploadObjects();

            m_initialized = true;
        }
    }

    void ImguiLayer::Deinitialize()
    {
        if (m_initialized)
        {
            if (auto current_window = m_window.lock())
            {
                const auto& performant_device  = Engine::GetVulkanInstance()->GetHighPerformantDevice();

                //ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(performant_device.GetCurrentGraphicQueue(true)) == VK_SUCCESS, "Failed to wait for Present Queue -- ImGuiLayer");
                vkDestroyDescriptorPool(performant_device.GetNativeDeviceHandle(), m_descriptor_pool, nullptr);
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
            }

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

            // Resize swap chain?
            if (current_vulkan_window_ptr->HasSwapChainRebuilt())
            {
                if (window_ptr->GetHeight() > 0 && window_ptr->GetWidth() > 0)
                {
                    ImGui_ImplVulkan_SetMinImageCount(current_vulkan_window_ptr->GetSwapChainMinImageCount());
                }
            }

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

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
    }

    void ImguiLayer::PrepareFrame(uint32_t frame_index, VkQueue& present_queue)
    {
        if (auto current_window = m_window.lock())
        {
            if (current_window->IsMinimized())
            {
                return;
            }

            auto  vulkan_window_ptr = reinterpret_cast<Window::GLFWWindow::VulkanWindow*>(current_window.get());
            auto& window_frame      = vulkan_window_ptr->GetWindowFrame(frame_index);
            auto& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();

            VkCommandBuffer             command_buffer                     = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo graphic_command_buffer_create_info = {};
            graphic_command_buffer_create_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            graphic_command_buffer_create_info.commandPool                 = window_frame.GraphicCommandPool;
            graphic_command_buffer_create_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            graphic_command_buffer_create_info.commandBufferCount          = 1;
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateCommandBuffers(performant_device.GetNativeDeviceHandle(), &graphic_command_buffer_create_info, &command_buffer) == VK_SUCCESS,
                "Failed to allocate Command Buffer")

            VkCommandBufferBeginInfo command_buffer_begin_info = {};
            command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            command_buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin Command Buffer -- ImGuiLayer")

            // Begin RenderPass
            {
                VkRenderPassBeginInfo render_pass_begin_info    = {};
                render_pass_begin_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                render_pass_begin_info.renderPass               = vulkan_window_ptr->GetRenderPass();
                render_pass_begin_info.framebuffer              = window_frame.Framebuffer;
                render_pass_begin_info.renderArea.extent.width  = current_window->GetWidth();
                render_pass_begin_info.renderArea.extent.height = current_window->GetHeight();

                std::array<VkClearValue, 2> clear_values   = {};
                clear_values[0].color                    = {{0.0f, 0.0f, 0.0f, 1.0f}};
                clear_values[1].depthStencil               = {1.0f, 0};
                render_pass_begin_info.clearValueCount     = clear_values.size();
                render_pass_begin_info.pClearValues        = clear_values.data();
                vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

                // Record dear imgui primitives into command buffer
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);

                vkCmdEndRenderPass(command_buffer);
            }

            ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(command_buffer) == VK_SUCCESS, "Failed to end Command Buffer -- ImGuiLayer")

            window_frame.GraphicCommandBuffers.push_back(std::move(command_buffer));
        }
    }
} // namespace ZEngine::Layers
