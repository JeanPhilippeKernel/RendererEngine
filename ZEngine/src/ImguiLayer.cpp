#include <pch.h>
#include <Layers/ImguiLayer.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

#include <GLFW/glfw3.h>

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
        m_ui_components.shrink_to_fit();
    }

    void ImguiLayer::Initialize()
    {
        if (!m_initialized)
        {
            assert(!m_window.expired(), "The Current Window instance has been destroyed");

            auto current_window            = m_window.lock();
            auto current_window_ptr        = current_window.get();
            auto current_vulkan_window_ptr = reinterpret_cast<Window::GLFWWindow::OpenGLWindow*>(current_window_ptr);
            auto current_engine_ptr        = current_window->GetAttachedEngine();

            const auto& performant_device = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice();
            auto        device_handle     = performant_device.GetNativeDeviceHandle();
            auto        present_queue     = performant_device.GetCurrentGraphicQueue(true);

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

            auto& style            = ImGui::GetStyle();
            style.WindowBorderSize = 0.f;
            style.ChildBorderSize  = 0.f;

            io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f);
            io.FontDefault = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

            ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window_ptr->GetNativeWindow()), false);

            // Create Descriptor Pool
            std::vector<VkDescriptorPoolSize> pool_sizes = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets                    = 1000 * pool_sizes.size();
            pool_info.poolSizeCount              = pool_sizes.size();
            pool_info.pPoolSizes                 = pool_sizes.data();

            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(device_handle, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool -- ImGuiLayer");


            ImGui_ImplVulkan_InitInfo imgui_vulkan_init_info = {};
            imgui_vulkan_init_info.Instance                  = current_engine_ptr->GetVulkanInstance().GetNativeHandle();
            imgui_vulkan_init_info.PhysicalDevice            = performant_device.GetNativePhysicalDeviceHandle();
            imgui_vulkan_init_info.Device                    = device_handle;
            imgui_vulkan_init_info.QueueFamily               = performant_device.GetGraphicQueueFamilyIndexCollection(true).at(0);
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
            uint32_t   requested_frame{0};
            auto       available_frame_collection = current_vulkan_window_ptr->GetWindowFrameCollection();
            const auto selected_frame_index       = requested_frame % available_frame_collection.size();
            auto&      selected_frame             = available_frame_collection[selected_frame_index];

            ZENGINE_VALIDATE_ASSERT(vkResetCommandPool(device_handle, selected_frame.CommandPool, 0) == VK_SUCCESS, "Failed to reset Command Pool -- ImGuiLayer")

            VkCommandBufferBeginInfo command_buffer_begin_info = {};
            command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            command_buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(selected_frame.CommandBuffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin Command Buffer -- ImGuiLayer")

            ImGui_ImplVulkan_CreateFontsTexture(selected_frame.CommandBuffer);

            VkCommandBuffer submit_info_command_buffer_array[] = {selected_frame.CommandBuffer};
            VkSubmitInfo    submit_info                        = {};
            submit_info.sType                                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount                     = 1;
            submit_info.pCommandBuffers                        = submit_info_command_buffer_array;

            ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(selected_frame.CommandBuffer) == VK_SUCCESS, "Failed to end Command Buffer -- ImGuiLayer")

            ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(present_queue, 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS, "Failed to submit Command Buffer on Present Queue -- ImGuiLayer")

            ZENGINE_VALIDATE_ASSERT(vkDeviceWaitIdle(device_handle) == VK_SUCCESS, "Failed to wait for GPU device -- ImGuiLayer")

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
                auto        current_engine_ptr = current_window->GetAttachedEngine();
                const auto& performant_device  = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice();

                ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(performant_device.GetCurrentGraphicQueue(true)) == VK_SUCCESS, "Failed to wait for Present Queue -- ImGuiLayer");

                vkDestroyDescriptorPool(performant_device.GetNativeDeviceHandle(), m_descriptor_pool, nullptr);

                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
            }

            m_initialized = false;
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

    void ImguiLayer::Render()
    {
        if (auto window_ptr = m_window.lock())
        {
            // Resize swap chain?
            if (m_swap_chain_rebuild)
            {
                if (window_ptr->GetHeight() > 0 && window_ptr->GetWidth() > 0)
                {
                    ImGui_ImplVulkan_SetMinImageCount(reinterpret_cast<Window::GLFWWindow::OpenGLWindow*>(window_ptr.get())->GetSwapChainMinImageCount());
                }
                m_swap_chain_rebuild = false;
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

            ImDrawData* main_draw_data = ImGui::GetDrawData();

            if (window_ptr->GetWindowProperty().IsMinimized)
            {
                return;
            }

            // Render and Present Main Platform Window
            __frameRenderAndPresent(window_ptr, main_draw_data);
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

    void ImguiLayer::__frameRenderAndPresent(const Ref<ZEngine::Window::CoreWindow>& window, ImDrawData* draw_data)
    {
        auto       vulkan_window_ptr       = reinterpret_cast<Window::GLFWWindow::OpenGLWindow*>(window.get());
        auto const current_frame_index     = vulkan_window_ptr->GetCurrentWindowFrameIndex();
        auto&      current_frame           = vulkan_window_ptr->GetWindowFrame(current_frame_index);
        auto&      current_frame_semaphore = vulkan_window_ptr->GetWindowFrameSemaphore(current_frame_index);

        if (auto current_window = m_window.lock())
        {
            auto        current_engine_ptr = current_window->GetAttachedEngine();
            const auto& performant_device  = current_engine_ptr->GetVulkanInstance().GetHighPerformantDevice();
            auto        device_handle      = performant_device.GetNativeDeviceHandle();
            auto        present_queue      = performant_device.GetCurrentGraphicQueue(true);

            ZENGINE_VALIDATE_ASSERT(vkWaitForFences(device_handle, 1, &(current_frame.Fence), VK_TRUE, UINT64_MAX) == VK_SUCCESS,
                "Failed to wait for Fence semaphore -- ImGuiLayer") // wait indefinitely instead of periodically checking

            ZENGINE_VALIDATE_ASSERT(vkResetFences(device_handle, 1, &(current_frame.Fence)) == VK_SUCCESS, "Failed to reset Fence semaphore -- ImGuiLayer")

            ZENGINE_VALIDATE_ASSERT(vkResetCommandPool(device_handle, current_frame.CommandPool, 0) == VK_SUCCESS, "Failed to reset Command Pool -- ImGuiLayer")


            uint32_t image_index;
            VkResult acquire_image_result =
                vkAcquireNextImageKHR(device_handle, vulkan_window_ptr->GetSwapChain(), UINT64_MAX, current_frame_semaphore.ImageAcquiredSemaphore, VK_NULL_HANDLE, &image_index);
            if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_image_result == VK_SUBOPTIMAL_KHR)
            {
                m_swap_chain_rebuild = true;
                return;
            }

            ZENGINE_VALIDATE_ASSERT(acquire_image_result == VK_SUCCESS, "Failed to acquire image from Swapchain")

            VkCommandBufferBeginInfo command_buffer_begin_info = {};
            command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            command_buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(current_frame.CommandBuffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin Command Buffer -- ImGuiLayer")

            VkRenderPassBeginInfo render_pass_begin_info    = {};
            render_pass_begin_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_pass_begin_info.renderPass               = vulkan_window_ptr->GetRenderPass();
            render_pass_begin_info.framebuffer              = current_frame.Framebuffer;
            render_pass_begin_info.renderArea.extent.width  = window->GetWidth();
            render_pass_begin_info.renderArea.extent.height = window->GetHeight();
            render_pass_begin_info.clearValueCount          = 1;

            VkClearValue clear_value            = {};
            render_pass_begin_info.pClearValues = &clear_value;
            vkCmdBeginRenderPass(current_frame.CommandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            // Record dear imgui primitives into command buffer
            ImGui_ImplVulkan_RenderDrawData(draw_data, current_frame.CommandBuffer);

            // Submit command buffer
            vkCmdEndRenderPass(current_frame.CommandBuffer);

            std::vector<VkSemaphore> submit_info_semaphore_collection = {current_frame_semaphore.ImageAcquiredSemaphore};
            VkSubmitInfo             submit_info                      = {};
            VkPipelineStageFlags     wait_stage                       = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            submit_info.sType                                         = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount                            = submit_info_semaphore_collection.size();
            submit_info.pWaitSemaphores                               = submit_info_semaphore_collection.data();
            submit_info.pWaitDstStageMask                             = &wait_stage;
            submit_info.commandBufferCount                            = 1;
            submit_info.pCommandBuffers                               = &(current_frame.CommandBuffer);
            submit_info.signalSemaphoreCount                          = 1;
            submit_info.pSignalSemaphores                             = &(current_frame_semaphore.RenderCompleteSemaphore);

            ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(current_frame.CommandBuffer) == VK_SUCCESS, "Failed to end Command Buffer -- ImGuiLayer")

            ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(present_queue, 1, &submit_info, current_frame.Fence) == VK_SUCCESS, "Failed to submit Frame rendering commands -- ImGuiLayer")


            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            /*Window Frame presentation operation*/
            std::vector<VkSwapchainKHR> present_swapchain_collection = {vulkan_window_ptr->GetSwapChain()};
            std::vector<VkSemaphore>    present_semaphore_collection = {current_frame_semaphore.RenderCompleteSemaphore};
            VkPresentInfoKHR            present_info                 = {};
            present_info.sType                                       = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount                          = present_semaphore_collection.size();
            present_info.pWaitSemaphores                             = present_semaphore_collection.data();
            present_info.swapchainCount                              = present_swapchain_collection.size();
            present_info.pSwapchains                                 = present_swapchain_collection.data();
            present_info.pImageIndices                               = &image_index;

            VkResult present_result = vkQueuePresentKHR(present_queue, &present_info);
            if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
            {
                m_swap_chain_rebuild = true;
                return;
            }

            ZENGINE_VALIDATE_ASSERT(present_result == VK_SUCCESS, "Failed to present current frame on Window -- ImGuiLayer")

            vulkan_window_ptr->IncrementWindowFrameIndex();
        }
    }
} // namespace ZEngine::Layers
