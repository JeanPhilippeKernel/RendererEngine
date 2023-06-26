#include <pch.h>
#include <Window/GlfwWindow/VulkanWindow.h>
#include <Engine.h>
#include <Inputs/KeyCode.h>
#include <Rendering/Renderers/RenderCommand.h>
#include <Logging/LoggerDefinition.h>
#include <Helpers/RendererHelper.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Event;

namespace ZEngine::Window::GLFWWindow
{
    VulkanWindow::VulkanWindow(const WindowConfiguration& configuration) : CoreWindow()
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
            ZENGINE_CORE_CRITICAL(description)
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

        ZENGINE_CORE_INFO("Window created, Properties : Width = {0}, Height = {1}", m_property.Width, m_property.Height)
    }

    uint32_t VulkanWindow::GetWidth() const
    {
        return m_property.Width;
    }

    std::string_view VulkanWindow::GetTitle() const
    {
        return m_property.Title;
    }

    bool VulkanWindow::IsMinimized() const
    {
        return m_property.IsMinimized;
    }

    void VulkanWindow::SetTitle(std::string_view title)
    {
        m_property.Title = title;
        glfwSetWindowTitle(m_native_window, m_property.Title.c_str());
    }

    bool VulkanWindow::IsVSyncEnable() const
    {
        return m_property.VSync;
    }

    void VulkanWindow::SetVSync(bool value)
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

    void VulkanWindow::SetCallbackFunction(const EventCallbackFn& callback)
    {
        m_property.CallbackFn = callback;
    }

    void* VulkanWindow::GetNativeWindow() const
    {
        return reinterpret_cast<void*>(m_native_window);
    }

    const WindowProperty& VulkanWindow::GetWindowProperty() const
    {
        return m_property;
    }

    void VulkanWindow::Initialize()
    {
        auto vulkan_instance = ZEngine::Engine::GetVulkanInstance();

        ZENGINE_VALIDATE_ASSERT(
            glfwCreateWindowSurface(vulkan_instance->GetNativeHandle(), m_native_window, nullptr, &m_vulkan_surface) == VK_SUCCESS, "Failed Window Surface from GLFW")

        vulkan_instance->ConfigureDevices(&m_vulkan_surface);

        auto& current_device        = vulkan_instance->GetHighPerformantDevice();
        auto  current_device_handle = current_device.GetNativePhysicalDeviceHandle();

        uint32_t formatCount{0};
        vkGetPhysicalDeviceSurfaceFormatsKHR(current_device_handle, m_vulkan_surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            m_surface_format_collection.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(current_device_handle, m_vulkan_surface, &formatCount, m_surface_format_collection.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(current_device_handle, m_vulkan_surface, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            m_present_mode_collection.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(current_device_handle, m_vulkan_surface, &presentModeCount, m_present_mode_collection.data());
        }

        m_surface_format = (m_surface_format_collection.size() > 0) ? m_surface_format_collection[0] : VkSurfaceFormatKHR{};
        for (const VkSurfaceFormatKHR& format_khr : m_surface_format_collection)
        {
            // default is: VK_FORMAT_B8G8R8A8_SRGB
            // but Imgui wants : VK_FORMAT_B8G8R8A8_UNORM ...
            if ((format_khr.format == VK_FORMAT_B8G8R8A8_UNORM) && (format_khr.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
            {
                m_surface_format = format_khr;
                break;
            }
        }

        m_vulkan_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const VkPresentModeKHR present_mode_khr : m_present_mode_collection)
        {
            if (present_mode_khr == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                m_vulkan_present_mode = present_mode_khr;
                break;
            }
        }

        RecreateSwapChain(nullptr, current_device);

        auto& layer_stack = *m_layer_stack_ptr;

        // Initialize in reverse order, so overlay layers can be initialize first
        // this give us opportunity to initialize UI-like layers before graphic render-like layers
        for (auto rlayer_it = std::rbegin(layer_stack); rlayer_it != std::rend(layer_stack); ++rlayer_it)
        {
            (*rlayer_it)->SetAttachedWindow(shared_from_this());
            (*rlayer_it)->Initialize();
        }

        glfwSetWindowUserPointer(m_native_window, &m_property);

        glfwSetFramebufferSizeCallback(m_native_window, VulkanWindow::__OnGlfwFrameBufferSizeChanged);

        glfwSetWindowCloseCallback(m_native_window, VulkanWindow::__OnGlfwWindowClose);
        glfwSetWindowSizeCallback(m_native_window, VulkanWindow::__OnGlfwWindowResized);
        glfwSetWindowMaximizeCallback(m_native_window, VulkanWindow::__OnGlfwWindowMaximized);
        glfwSetWindowIconifyCallback(m_native_window, VulkanWindow::__OnGlfwWindowMinimized);

        glfwSetMouseButtonCallback(m_native_window, VulkanWindow::__OnGlfwMouseButtonRaised);
        glfwSetScrollCallback(m_native_window, VulkanWindow::__OnGlfwMouseScrollRaised);
        glfwSetKeyCallback(m_native_window, VulkanWindow::__OnGlfwKeyboardRaised);

        glfwSetCursorPosCallback(m_native_window, VulkanWindow::__OnGlfwCursorMoved);
        glfwSetCharCallback(m_native_window, VulkanWindow::__OnGlfwTextInputRaised);

        glfwMaximizeWindow(m_native_window);
    }

    void VulkanWindow::Deinitialize()
    {
        auto& layer_stack = *m_layer_stack_ptr;
        for (auto rlayer_it = std::rbegin(layer_stack); rlayer_it != std::rend(layer_stack); ++rlayer_it)
        {
            (*rlayer_it)->Deinitialize();
        }
    }

    void VulkanWindow::PollEvent()
    {
        glfwPollEvents();
    }

    float VulkanWindow::GetTime()
    {
        return (float) glfwGetTime();
    }

    void VulkanWindow::__OnGlfwFrameBufferSizeChanged(GLFWwindow* window, int width, int height)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            property->SetWidth(width);
            property->SetHeight(height);

            ZENGINE_CORE_INFO("Window size updated, Properties : Width = {0}, Height = {1}", property->Width, property->Height)
        }
    }

    void VulkanWindow::MarkVulkanInternalObjectDirty(const Hardwares::VulkanDevice& device)
    {
        vkQueueWaitIdle(device.GetCurrentGraphicQueueWithPresentSupport().Queue);
        vkQueueWaitIdle(device.GetCurrentTransferQueue().Queue);

        for (uint32_t i = 0; i < m_frame_collection.size(); i++)
        {
            vkDestroyFence(device.GetNativeDeviceHandle(), m_frame_collection[i].Fence, nullptr);
            vkFreeCommandBuffers(device.GetNativeDeviceHandle(), m_frame_collection[i].GraphicCommandPool, 1, &(m_frame_collection[i].GraphicCommandBuffer));
            if (!m_frame_collection[i].GraphicCommandBuffers.empty())
            {
                vkFreeCommandBuffers(
                    device.GetNativeDeviceHandle(),
                    m_frame_collection[i].GraphicCommandPool,
                    m_frame_collection[i].GraphicCommandBuffers.size(),
                    m_frame_collection[i].GraphicCommandBuffers.data());
            }
            vkDestroyCommandPool(device.GetNativeDeviceHandle(), m_frame_collection[i].GraphicCommandPool, nullptr);
            vkFreeCommandBuffers(device.GetNativeDeviceHandle(), m_frame_collection[i].TransferCommandPool, 1, &(m_frame_collection[i].TransferCommandBuffer));
            vkDestroyCommandPool(device.GetNativeDeviceHandle(), m_frame_collection[i].TransferCommandPool, nullptr);
            m_frame_collection[i].Fence                = {};
            m_frame_collection[i].GraphicCommandBuffer = {};
            m_frame_collection[i].GraphicCommandBuffers.clear();
            m_frame_collection[i].GraphicCommandBuffers.shrink_to_fit();
            m_frame_collection[i].GraphicCommandPool    = {};
            m_frame_collection[i].TransferCommandBuffer = {};
            m_frame_collection[i].TransferCommandPool   = {};

            vkDestroySemaphore(device.GetNativeDeviceHandle(), m_frame_collection[i].ImageAcquiredSemaphore, nullptr);
            vkDestroySemaphore(device.GetNativeDeviceHandle(), m_frame_collection[i].RenderCompleteSemaphore, nullptr);

            m_frame_collection[i].ImageAcquiredSemaphore  = {};
            m_frame_collection[i].RenderCompleteSemaphore = {};
        }
        m_frame_collection.clear();
        m_frame_collection.shrink_to_fit();

        for (uint32_t i = 0; i < m_swapchain_image_view_collection.size(); i++)
        {
            vkDestroyImageView(device.GetNativeDeviceHandle(), m_swapchain_image_view_collection[i], nullptr);
        }
        m_swapchain_image_view_collection.clear();
        m_swapchain_image_view_collection.shrink_to_fit();

        if (m_depth_image_view)
        {
            vkDestroyImageView(device.GetNativeDeviceHandle(), m_depth_image_view, nullptr);
            m_depth_image_view = VK_NULL_HANDLE;
        }
        if (m_depth_image)
        {
            vkDestroyImage(device.GetNativeDeviceHandle(), m_depth_image, nullptr);
            m_depth_image = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < m_framebuffer_collection.size(); i++)
        {
            vkDestroyFramebuffer(device.GetNativeDeviceHandle(), m_framebuffer_collection[i], nullptr);
        }
        m_framebuffer_collection.clear();
        m_framebuffer_collection.shrink_to_fit();

        if (m_render_pass)
        {
            vkDestroyRenderPass(device.GetNativeDeviceHandle(), m_render_pass, nullptr);
            m_render_pass = VK_NULL_HANDLE;
        }
    }

    void VulkanWindow::__OnGlfwWindowClose(GLFWwindow* window)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            WindowClosedEvent e;
            property->CallbackFn(e);
        }
    }

    void VulkanWindow::__OnGlfwWindowResized(GLFWwindow* window, int width, int height)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            Event::WindowResizedEvent e{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            property->CallbackFn(e);
        }
    }

    void VulkanWindow::__OnGlfwWindowMaximized(GLFWwindow* window, int maximized)
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

    void VulkanWindow::__OnGlfwWindowMinimized(GLFWwindow* window, int minimized)
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

    void VulkanWindow::__OnGlfwMouseButtonRaised(GLFWwindow* window, int button, int action, int mods)
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

    void VulkanWindow::__OnGlfwMouseScrollRaised(GLFWwindow* window, double xoffset, double yoffset)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            MouseButtonWheelEvent e{xoffset, yoffset};
            property->CallbackFn(e);
        }
    }

    void VulkanWindow::__OnGlfwCursorMoved(GLFWwindow* window, double xoffset, double yoffset)
    {
        WindowProperty* property = reinterpret_cast<WindowProperty*>(glfwGetWindowUserPointer(window));
        if (property)
        {
            MouseButtonMovedEvent e{xoffset, yoffset};
            property->CallbackFn(e);
        }
    }

    void VulkanWindow::__OnGlfwTextInputRaised(GLFWwindow* window, unsigned int character)
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

    void VulkanWindow::__OnGlfwKeyboardRaised(GLFWwindow* window, int key, int scancode, int action, int mods)
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

    void VulkanWindow::Update(Core::TimeStep delta_time)
    {
        for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr)
        {
            layer->Update(delta_time);
        }
    }

    void VulkanWindow::Render()
    {
        auto&       current_frame     = this->GetCurrentWindowFrame();
        const auto& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        auto        device_handle     = performant_device.GetNativeDeviceHandle();
        auto        present_queue     = performant_device.GetCurrentGraphicQueueWithPresentSupport().Queue;

        VkResult wait_for_fences_result = vkWaitForFences(device_handle, 1, &(current_frame.Fence), VK_TRUE, UINT64_MAX); // wait indefinitely instead of periodically checking
        if (wait_for_fences_result == VK_TIMEOUT)
        {
            return;
        }

        ZENGINE_VALIDATE_ASSERT(vkResetFences(device_handle, 1, &current_frame.Fence) == VK_SUCCESS, "Failed to reset Fence semaphore")

        uint32_t image_index;
        VkResult acquire_image_result = vkAcquireNextImageKHR(device_handle, m_swapchain, UINT64_MAX, current_frame.ImageAcquiredSemaphore, VK_NULL_HANDLE, &image_index);
        if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_image_result == VK_SUBOPTIMAL_KHR || acquire_image_result == VK_NOT_READY || acquire_image_result == VK_TIMEOUT)
        {
            return;
        }
        ZENGINE_VALIDATE_ASSERT(acquire_image_result == VK_SUCCESS, "Failed to acquire image from Swapchain")

        if (!current_frame.GraphicCommandBuffers.empty())
        {
            vkFreeCommandBuffers(
                performant_device.GetNativeDeviceHandle(),
                current_frame.GraphicCommandPool,
                current_frame.GraphicCommandBuffers.size(),
                current_frame.GraphicCommandBuffers.data());

            current_frame.GraphicCommandBuffers.clear();
            current_frame.GraphicCommandBuffers.shrink_to_fit();
        }

        ZENGINE_VALIDATE_ASSERT(vkResetCommandPool(device_handle, current_frame.GraphicCommandPool, 0) == VK_SUCCESS, "Failed to reset Command Pool")

        for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr)
        {
            layer->Render();
            layer->PrepareFrame(image_index, present_queue);
        }

        // Submit command buffers
        std::vector<VkSemaphore> wait_semaphore_collection = {current_frame.ImageAcquiredSemaphore};
        VkPipelineStageFlags     wait_stage                = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo             submit_info               = {};
        submit_info.sType                                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount                     = wait_semaphore_collection.size();
        submit_info.pWaitSemaphores                        = wait_semaphore_collection.data();
        submit_info.pWaitDstStageMask                      = &wait_stage;
        submit_info.commandBufferCount                     = current_frame.GraphicCommandBuffers.size();
        submit_info.pCommandBuffers                        = current_frame.GraphicCommandBuffers.data();
        submit_info.signalSemaphoreCount                   = 1;
        submit_info.pSignalSemaphores                      = &(current_frame.RenderCompleteSemaphore);

        VkResult queue_submit_result = vkQueueSubmit(present_queue, 1, &submit_info, current_frame.Fence);
        if (queue_submit_result == VK_ERROR_OUT_OF_HOST_MEMORY || queue_submit_result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        {
            return;
        }

        /*Window Frame presentation operation*/
        std::vector<VkSwapchainKHR> present_swapchain_collection = {m_swapchain};
        std::vector<VkSemaphore>    present_semaphore_collection = {current_frame.RenderCompleteSemaphore};
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
            return;
        }

        ZENGINE_VALIDATE_ASSERT(present_result == VK_SUCCESS, "Failed to present current frame on Window")

        this->IncrementWindowFrameIndex();

        /*Reset the swapchain state info for next frame*/
        m_has_swap_chain_rebuilt = false;
    }

    uint32_t VulkanWindow::GetSwapChainMinImageCount() const
    {
        return m_min_image_count;
    }

    const std::vector<VkImage>& VulkanWindow::GetSwapChainImageCollection() const
    {
        return m_swapchain_image_collection;
    }

    const std::vector<VkImageView>& VulkanWindow::GetSwapChainImageViewCollection() const
    {
        return m_swapchain_image_view_collection;
    }

    const std::vector<VkFramebuffer>& VulkanWindow::GetFramebufferCollection() const
    {
        return m_framebuffer_collection;
    }

    int32_t VulkanWindow::GetCurrentWindowFrameIndex() const
    {
        return m_current_frame_index;
    }

    VulkanWindowFrame& VulkanWindow::GetCurrentWindowFrame()
    {
        int32_t frame_index = GetCurrentWindowFrameIndex();
        return m_frame_collection[frame_index];
    }

    const VulkanWindowFrame& VulkanWindow::GetCurrentWindowFrame() const
    {
        // TODO: insert return statement here
        int32_t frame_index = GetCurrentWindowFrameIndex();
        return m_frame_collection[frame_index];
    }

    void VulkanWindow::IncrementWindowFrameIndex(int32_t step)
    {
        auto frame_count      = (int32_t) m_frame_collection.size();
        m_current_frame_index = (m_current_frame_index + step) % frame_count;
    }

    std::vector<VulkanWindowFrame>& VulkanWindow::GetWindowFrameCollection()
    {
        return m_frame_collection;
    }

    VulkanWindowFrame& VulkanWindow::GetWindowFrame(uint32_t index)
    {
        ZENGINE_VALIDATE_ASSERT(index < m_frame_collection.size(), "index is out of bound of available window frame")
        return m_frame_collection[index];
    }

    const VkExtent2D& VulkanWindow::GetSwapchainExtent() const
    {
        return m_vulkan_extent_2d;
    }

    void VulkanWindow::RecreateSwapChain(VkSwapchainKHR old_swapchain, Hardwares::VulkanDevice& device)
    {
        MarkVulkanInternalObjectDirty(device);

        // Surface capabilities
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.GetNativePhysicalDeviceHandle(), m_vulkan_surface, &m_surface_capabilities);
        if (m_surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            m_vulkan_extent_2d = m_surface_capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(m_native_window, &width, &height);

            m_vulkan_extent_2d        = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            m_vulkan_extent_2d.width  = std::clamp(m_vulkan_extent_2d.width, m_surface_capabilities.minImageExtent.width, m_surface_capabilities.maxImageExtent.width);
            m_vulkan_extent_2d.height = std::clamp(m_vulkan_extent_2d.height, m_surface_capabilities.minImageExtent.height, m_surface_capabilities.maxImageExtent.height);
        }

        m_min_image_count = std::clamp(m_min_image_count, m_surface_capabilities.minImageCount + 1, m_surface_capabilities.maxImageCount);

        /* Create SwapChain */
        m_swapchain = VK_NULL_HANDLE;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = m_vulkan_surface;
        createInfo.minImageCount    = m_min_image_count;
        createInfo.imageFormat      = m_surface_format.format;
        createInfo.imageColorSpace  = m_surface_format.colorSpace;
        createInfo.imageExtent      = m_vulkan_extent_2d;
        createInfo.imageArrayLayers = m_surface_capabilities.maxImageArrayLayers;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto device_graphic_queue_family_index_collection  = device.GetGraphicQueueFamilyIndexCollection();
        auto device_transfer_queue_family_index_collection = device.GetTransferQueueFamilyIndexCollection();

        std::vector<uint32_t> device_family_indice;
        if (!device_graphic_queue_family_index_collection.empty() || !device_transfer_queue_family_index_collection.empty())
        {
            const auto index_count = device_graphic_queue_family_index_collection.size() + device_transfer_queue_family_index_collection.size();

            std::copy(std::begin(device_graphic_queue_family_index_collection), std::end(device_graphic_queue_family_index_collection), std::back_inserter(device_family_indice));
            std::copy(std::begin(device_transfer_queue_family_index_collection), std::end(device_transfer_queue_family_index_collection), std::back_inserter(device_family_indice));

            createInfo.imageSharingMode      = (index_count > 1) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = index_count;
            createInfo.pQueueFamilyIndices   = device_family_indice.data();
        }

        createInfo.preTransform   = m_surface_capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode    = m_vulkan_present_mode;
        createInfo.clipped        = VK_TRUE;
        createInfo.oldSwapchain   = old_swapchain;

        ZENGINE_VALIDATE_ASSERT(vkCreateSwapchainKHR(device.GetNativeDeviceHandle(), &createInfo, nullptr, &m_swapchain) == VK_SUCCESS, "Failed to create Swapchain")

        if (old_swapchain)
        {
            vkDestroySwapchainKHR(device.GetNativeDeviceHandle(), old_swapchain, nullptr);
        }

        uint32_t swapchain_image_count{0};
        ZENGINE_VALIDATE_ASSERT(
            vkGetSwapchainImagesKHR(device.GetNativeDeviceHandle(), m_swapchain, &swapchain_image_count, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")

        m_swapchain_image_collection.resize(swapchain_image_count);
        ZENGINE_VALIDATE_ASSERT(
            vkGetSwapchainImagesKHR(device.GetNativeDeviceHandle(), m_swapchain, &swapchain_image_count, m_swapchain_image_collection.data()) == VK_SUCCESS,
            "Failed to get Images from Swapchain")

        /* Create Depth Image and ImageView*/
        const auto& memory_properties = device.GetPhysicalDeviceMemoryProperties();
        VkFormat    depth_format      = Helpers::FindDepthFormat(device);
        m_depth_image                 = Helpers::CreateImage(
            device,
            m_vulkan_extent_2d.width,
            m_vulkan_extent_2d.height,
            VK_IMAGE_TYPE_2D,
            depth_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_SAMPLE_COUNT_1_BIT,
            m_depth_image_device_memory,
            memory_properties,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        m_depth_image_view = Helpers::CreateImageView(device, m_depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);

        /* Create ImageView */
        m_swapchain_image_view_collection.resize(m_swapchain_image_collection.size());
        for (size_t i = 0; i < m_swapchain_image_collection.size(); ++i)
        {
            m_swapchain_image_view_collection[i] = Helpers::CreateImageView(device, m_swapchain_image_collection[i], m_surface_format.format, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        /* Create RenderPass */
        RenderPasses::RenderPassSpecification render_pass_specification = {
            .ColorAttachements = {
                VkAttachmentDescription{
                    .format        = m_surface_format.format,
                    .samples       = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp       = VK_ATTACHMENT_STORE_OP_STORE,
                    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                VkAttachmentDescription{
                    .format         = Helpers::FindDepthFormat(device),
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}}};
        RenderPasses::SubPassSpecification sub_pass_specification = {};
        sub_pass_specification.ColorAttachementReferences         = {VkAttachmentReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        sub_pass_specification.DepthStencilAttachementReference   = VkAttachmentReference{.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        sub_pass_specification.SubpassDescription                 = VkSubpassDescription{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
        render_pass_specification.SubpassDependencies             = {VkSubpassDependency{
                        .srcSubpass    = VK_SUBPASS_EXTERNAL,
                        .dstSubpass    = 0,
                        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .srcAccessMask = 0,
                        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT}};

        render_pass_specification.SubpassSpecifications = {sub_pass_specification};
        m_render_pass                                   = Helpers::CreateRenderPass(device, render_pass_specification);

        /* Create Framebuffer */
        m_framebuffer_collection.resize(m_swapchain_image_view_collection.size());
        for (size_t i = 0; i < m_swapchain_image_view_collection.size(); i++)
        {
            auto attachments            = std::vector<VkImageView>{m_swapchain_image_view_collection[i], m_depth_image_view};
            m_framebuffer_collection[i] = Helpers::CreateFramebuffer(device, attachments, m_render_pass, m_vulkan_extent_2d);
        }

        // Create Frame & FrameSemaphore
        m_frame_collection.resize(m_swapchain_image_collection.size());
        if (!m_frame_collection.empty())
        {
            m_current_frame_index = 0;
        }

        for (uint32_t i = 0; i < m_frame_collection.size(); ++i)
        {
            m_frame_collection[i]                         = {};
            m_frame_collection[i].FrameIndex              = i;
            m_frame_collection[i].Backbuffer              = m_swapchain_image_collection[i];
            m_frame_collection[i].BackbufferView          = m_swapchain_image_view_collection[i];
            m_frame_collection[i].Framebuffer             = m_framebuffer_collection[i];
            m_frame_collection[i].ImageAcquiredSemaphore  = {};
            m_frame_collection[i].RenderCompleteSemaphore = {};
        }

        // Create Command Buffers
        for (uint32_t i = 0; i < m_swapchain_image_collection.size(); i++)
        {
            auto& frame = m_frame_collection[i];

            /* Graphic Command Pool & Command Buffer */
            VkCommandPoolCreateInfo graphic_command_pool_create_info = {};
            graphic_command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            graphic_command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            graphic_command_pool_create_info.queueFamilyIndex        = device.GetGraphicQueueFamilyIndexCollection().front();
            ZENGINE_VALIDATE_ASSERT(
                vkCreateCommandPool(device.GetNativeDeviceHandle(), &graphic_command_pool_create_info, nullptr, &(frame.GraphicCommandPool)) == VK_SUCCESS,
                "Failed to create Command Pool")

            /* Transfer Command Pool & Command Buffer */
            VkCommandPoolCreateInfo transfer_command_pool_create_info = {};
            transfer_command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            transfer_command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            transfer_command_pool_create_info.queueFamilyIndex        = device.GetTransferQueueFamilyIndexCollection().front();
            ZENGINE_VALIDATE_ASSERT(
                vkCreateCommandPool(device.GetNativeDeviceHandle(), &transfer_command_pool_create_info, nullptr, &(frame.TransferCommandPool)) == VK_SUCCESS,
                "Failed to create Command Pool")

            VkCommandBufferAllocateInfo transfer_command_buffer_create_info = {};
            transfer_command_buffer_create_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            transfer_command_buffer_create_info.commandPool                 = frame.TransferCommandPool;
            transfer_command_buffer_create_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            transfer_command_buffer_create_info.commandBufferCount          = 1;
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateCommandBuffers(device.GetNativeDeviceHandle(), &transfer_command_buffer_create_info, &(frame.TransferCommandBuffer)) == VK_SUCCESS,
                "Failed to allocate Command Buffer")

            VkFenceCreateInfo fence_create_info = {};
            fence_create_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_create_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
            ZENGINE_VALIDATE_ASSERT(vkCreateFence(device.GetNativeDeviceHandle(), &fence_create_info, nullptr, &(frame.Fence)) == VK_SUCCESS, "Failed to create Fence")

            VkSemaphoreCreateInfo semaphore_create_info = {};
            semaphore_create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            ZENGINE_VALIDATE_ASSERT(
                vkCreateSemaphore(device.GetNativeDeviceHandle(), &semaphore_create_info, nullptr, &(frame.ImageAcquiredSemaphore)) == VK_SUCCESS,
                "Failed to create Image acquired Semaphore")
            ZENGINE_VALIDATE_ASSERT(
                vkCreateSemaphore(device.GetNativeDeviceHandle(), &semaphore_create_info, nullptr, &(frame.RenderCompleteSemaphore)) == VK_SUCCESS,
                "Failed to create Render complete Semaphore")
        }

        m_has_swap_chain_rebuilt = true;
    }

    VkSurfaceKHR VulkanWindow::GetSurface() const
    {
        return m_vulkan_surface;
    }

    VkSurfaceFormatKHR VulkanWindow::GetSurfaceFormat() const
    {
        return m_surface_format;
    }

    VkPresentModeKHR VulkanWindow::GetPresentMode() const
    {
        return m_vulkan_present_mode;
    }

    VkSwapchainKHR VulkanWindow::GetSwapChain() const
    {
        return m_swapchain;
    }

    VkRenderPass VulkanWindow::GetRenderPass() const
    {
        ZENGINE_VALIDATE_ASSERT(m_render_pass != VK_NULL_HANDLE, "")
        return m_render_pass;
    }

    bool VulkanWindow::HasSwapChainRebuilt() const
    {
        return m_has_swap_chain_rebuilt;
    }

    VulkanWindow::~VulkanWindow()
    {
        const auto& device = ZEngine::Engine::GetVulkanInstance()->GetHighPerformantDevice();
        MarkVulkanInternalObjectDirty(device);

        vkDestroySwapchainKHR(device.GetNativeDeviceHandle(), m_swapchain, nullptr);
        vkDestroySurfaceKHR(ZEngine::Engine::GetVulkanInstance()->GetNativeHandle(), m_vulkan_surface, nullptr);

        glfwSetErrorCallback(NULL);
        glfwDestroyWindow(m_native_window);
        glfwTerminate();
    }

    uint32_t VulkanWindow::GetHeight() const
    {
        return m_property.Height;
    }

    bool VulkanWindow::OnWindowClosed(WindowClosedEvent& event)
    {
        glfwSetWindowShouldClose(m_native_window, GLFW_TRUE);
        ZENGINE_CORE_INFO("Window has been closed")

        Event::EngineClosedEvent e(event.GetName().c_str());
        Event::EventDispatcher   event_dispatcher(e);
        event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnWindowResized(WindowResizedEvent& event)
    {
        if (event.GetWidth() > 0 && event.GetHeight() > 0)
        {
            RecreateSwapChain(m_swapchain, ZEngine::Engine::GetVulkanInstance()->GetHighPerformantDevice());
        }

        ZENGINE_CORE_INFO("Window has been resized")

        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowResizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnWindowMinimized(Event::WindowMinimizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been minimized")

        m_property.IsMinimized = true;
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowMinimizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnWindowMaximized(Event::WindowMaximizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been maximized")

        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowMaximizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnWindowRestored(Event::WindowRestoredEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been restored")

        m_property.IsMinimized = false;
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowRestoredEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnEvent(Event::CoreEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&VulkanWindow::OnWindowClosed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&VulkanWindow::OnWindowResized, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&VulkanWindow::OnKeyPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&VulkanWindow::OnKeyReleased, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&VulkanWindow::OnMouseButtonPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&VulkanWindow::OnMouseButtonReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&VulkanWindow::OnMouseButtonMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&VulkanWindow::OnMouseButtonWheelMoved, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&VulkanWindow::OnTextInputRaised, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::WindowMinimizedEvent>(std::bind(&VulkanWindow::OnWindowMinimized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::WindowMaximizedEvent>(std::bind(&VulkanWindow::OnWindowMaximized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::WindowRestoredEvent>(std::bind(&VulkanWindow::OnWindowRestored, this, std::placeholders::_1));

        return true;
    }

    bool VulkanWindow::OnKeyPressed(KeyPressedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::KeyPressedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnKeyReleased(KeyReleasedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::KeyReleasedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnMouseButtonPressed(MouseButtonPressedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonPressedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnMouseButtonReleased(MouseButtonReleasedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonReleasedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnMouseButtonMoved(MouseButtonMovedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonMovedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnMouseButtonWheelMoved(MouseButtonWheelEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::MouseButtonWheelEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnTextInputRaised(TextInputEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::TextInputEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return true;
    }
} // namespace ZEngine::Window::GLFWWindow
