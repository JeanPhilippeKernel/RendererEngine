#include <pch.h>
#include <Window/GlfwWindow/VulkanWindow.h>
#include <Engine.h>
#include <Inputs/KeyCode.h>
#include <Rendering/Renderers/RenderCommand.h>
#include <Logging/LoggerDefinition.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Event;

namespace ZEngine::Window::GLFWWindow
{
    VulkanWindow::VulkanWindow(WindowProperty& prop) : CoreWindow()
    {
        m_property    = prop;
        int glfw_init = glfwInit();

        if (glfw_init == GLFW_FALSE)
        {
            ZENGINE_CORE_CRITICAL("Unable to initialize glfw..");
            ZENGINE_EXIT_FAILURE();
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfwSetErrorCallback(
            [](int error, const char* description)
            {
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
        return m_native_window;
    }

    const WindowProperty& VulkanWindow::GetWindowProperty() const
    {
        return m_property;
    }

    void VulkanWindow::Initialize()
    {
        auto& vulkan_instance = m_engine->GetVulkanInstance();

        ZENGINE_VALIDATE_ASSERT(
            glfwCreateWindowSurface(vulkan_instance.GetNativeHandle(), m_native_window, nullptr, &m_vulkan_surface) == VK_SUCCESS, "Failed Window Surface from GLFW")

        vulkan_instance.ConfigureDevices(&m_vulkan_surface);

        const auto& current_device        = vulkan_instance.GetHighPerformantDevice();
        auto        current_device_handle = current_device.GetNativePhysicalDeviceHandle();

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
        vkDeviceWaitIdle(device.GetNativeDeviceHandle());
        vkQueueWaitIdle(device.GetCurrentGraphicQueue(true));

        for (uint32_t i = 0; i < m_frame_collection.size(); i++)
        {
            vkDestroyFence(device.GetNativeDeviceHandle(), m_frame_collection[i].Fence, nullptr);
            vkFreeCommandBuffers(device.GetNativeDeviceHandle(), m_frame_collection[i].CommandPool, 1, &(m_frame_collection[i].CommandBuffer));
            vkDestroyCommandPool(device.GetNativeDeviceHandle(), m_frame_collection[i].CommandPool, nullptr);
            m_frame_collection[i].Fence         = {};
            m_frame_collection[i].CommandBuffer = {};
            m_frame_collection[i].CommandPool   = {};

            vkDestroySemaphore(device.GetNativeDeviceHandle(), m_frame_semaphore_collection[i].ImageAcquiredSemaphore, nullptr);
            vkDestroySemaphore(device.GetNativeDeviceHandle(), m_frame_semaphore_collection[i].RenderCompleteSemaphore, nullptr);

            m_frame_semaphore_collection[i].ImageAcquiredSemaphore  = {};
            m_frame_semaphore_collection[i].RenderCompleteSemaphore = {};
        }
        m_frame_collection.clear();
        m_frame_collection.shrink_to_fit();

        for (uint32_t i = 0; i < m_swapchain_image_view_collection.size(); i++)
        {
            vkDestroyImageView(device.GetNativeDeviceHandle(), m_swapchain_image_view_collection[i], nullptr);
        }
        m_swapchain_image_view_collection.clear();
        m_swapchain_image_view_collection.shrink_to_fit();

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
        for (const Ref<Layers::Layer>& layer : *m_layer_stack_ptr)
        {
            layer->Render();
        }
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

    int32_t VulkanWindow::GetCurrentWindowFrameSemaphoreIndex() const
    {
        return m_current_frame_semaphore_index;
    }

    void VulkanWindow::IncrementWindowFrameSemaphoreIndex(int32_t step)
    {
        auto frame_semaphore_count      = (int32_t) m_frame_semaphore_collection.size();
        m_current_frame_semaphore_index = (m_current_frame_semaphore_index + step) % frame_semaphore_count;
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

    const std::vector<VulkanWindowFrameSemaphore>& VulkanWindow::GetWindowFrameSemaphoreCollection() const
    {
        return m_frame_semaphore_collection;
    }

    VulkanWindowFrameSemaphore& VulkanWindow::GetWindowFrameSemaphore(uint32_t index)
    {
        ZENGINE_VALIDATE_ASSERT(index < m_frame_semaphore_collection.size(), "index is out of bound of available window frame semaphore")
        return m_frame_semaphore_collection[index];
    }

    void VulkanWindow::RecreateSwapChain(VkSwapchainKHR old_swapchain, const Hardwares::VulkanDevice& device)
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

        auto device_graphic_queue_family_index_collection = device.GetGraphicQueueFamilyIndexCollection(true);
        if (!device_graphic_queue_family_index_collection.empty())
        {
            const auto index_count = device_graphic_queue_family_index_collection.size();

            createInfo.imageSharingMode      = (index_count > 1) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = index_count;
            createInfo.pQueueFamilyIndices   = device_graphic_queue_family_index_collection.data();
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
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(device.GetNativeDeviceHandle(), m_swapchain, &swapchain_image_count, m_swapchain_image_collection.data()) == VK_SUCCESS,
            "Failed to get Images from Swapchain")

        /* Create ImageView */
        m_swapchain_image_view_collection.resize(m_swapchain_image_collection.size());
        for (size_t i = 0; i < m_swapchain_image_collection.size(); ++i)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image        = m_swapchain_image_collection[i];
            createInfo.viewType     = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format       = m_surface_format.format;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            ZENGINE_VALIDATE_ASSERT(vkCreateImageView(device.GetNativeDeviceHandle(), &createInfo, nullptr, &(m_swapchain_image_view_collection[i])) == VK_SUCCESS,
                "Failed to create Swapchain ImageView")
        }


        /* Create RenderPass */
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format                  = m_surface_format.format;
        colorAttachment.samples                 = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment            = 0;
        colorAttachmentRef.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorAttachmentRef;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass          = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass          = 0;
        dependency.srcStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask       = 0;
        dependency.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount        = 1;
        renderPassInfo.pAttachments           = &colorAttachment;
        renderPassInfo.subpassCount           = 1;
        renderPassInfo.pSubpasses             = &subpass;
        renderPassInfo.dependencyCount        = 1;
        renderPassInfo.pDependencies          = &dependency;

        ZENGINE_VALIDATE_ASSERT(vkCreateRenderPass(device.GetNativeDeviceHandle(), &renderPassInfo, nullptr, &m_render_pass) == VK_SUCCESS, "Failed to create RenderPass")

        m_framebuffer_collection.resize(m_swapchain_image_view_collection.size());
        for (size_t i = 0; i < m_swapchain_image_view_collection.size(); i++)
        {
            VkImageView attachments[] = {m_swapchain_image_view_collection[i]};

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass              = m_render_pass;
            framebufferInfo.attachmentCount         = 1;
            framebufferInfo.pAttachments            = attachments;
            framebufferInfo.width                   = m_vulkan_extent_2d.width;
            framebufferInfo.height                  = m_vulkan_extent_2d.height;
            framebufferInfo.layers                  = 1;

            ZENGINE_VALIDATE_ASSERT(
                vkCreateFramebuffer(device.GetNativeDeviceHandle(), &framebufferInfo, nullptr, &m_framebuffer_collection[i]) == VK_SUCCESS, "Failed to create Framebuffer")
        }

        // Create Frame & FrameSemaphore
        m_frame_collection.resize(m_swapchain_image_collection.size());
        if (!m_frame_collection.empty())
        {
            m_current_frame_index = 0;
        }

        for (uint32_t i = 0; i < m_frame_collection.size(); ++i)
        {
            m_frame_collection[i]                = {};
            m_frame_collection[i].Backbuffer     = m_swapchain_image_collection[i];
            m_frame_collection[i].BackbufferView = m_swapchain_image_view_collection[i];
            m_frame_collection[i].Framebuffer    = m_framebuffer_collection[i];
        }

        m_frame_semaphore_collection.resize(m_swapchain_image_collection.size());
        if (!m_frame_semaphore_collection.empty())
        {
            m_current_frame_semaphore_index = 0;
        }
        for (uint32_t i = 0; i < m_frame_semaphore_collection.size(); ++i)
        {
            m_frame_semaphore_collection[i] = {};
        }

        // Create Command Buffers
        for (uint32_t i = 0; i < m_swapchain_image_collection.size(); i++)
        {
            auto& frame           = m_frame_collection[i];
            auto& frame_semaphore = m_frame_semaphore_collection[i];

            // ToDo : We should have one CommandPool
            VkCommandPoolCreateInfo command_pool_create_info = {};
            command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            command_pool_create_info.queueFamilyIndex        = device.GetGraphicQueueFamilyIndexCollection(true).at(0);
            ZENGINE_VALIDATE_ASSERT(
                vkCreateCommandPool(device.GetNativeDeviceHandle(), &command_pool_create_info, nullptr, &(frame.CommandPool)) == VK_SUCCESS, "Failed to create Command Pool")

            VkCommandBufferAllocateInfo command_buffer_create_info = {};
            command_buffer_create_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_buffer_create_info.commandPool                 = frame.CommandPool;
            command_buffer_create_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_buffer_create_info.commandBufferCount          = 1;
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateCommandBuffers(device.GetNativeDeviceHandle(), &command_buffer_create_info, &(frame.CommandBuffer)) == VK_SUCCESS, "Failed to allocate Command Buffer")

            VkFenceCreateInfo fence_create_info = {};
            fence_create_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_create_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
            ZENGINE_VALIDATE_ASSERT(vkCreateFence(device.GetNativeDeviceHandle(), &fence_create_info, nullptr, &(frame.Fence)) == VK_SUCCESS, "Failed to create Fence")

            VkSemaphoreCreateInfo semaphore_create_info = {};
            semaphore_create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            ZENGINE_VALIDATE_ASSERT(vkCreateSemaphore(device.GetNativeDeviceHandle(), &semaphore_create_info, nullptr, &(frame_semaphore.ImageAcquiredSemaphore)) == VK_SUCCESS,
                "Failed to create Image acquired Semaphore")
            ZENGINE_VALIDATE_ASSERT(vkCreateSemaphore(device.GetNativeDeviceHandle(), &semaphore_create_info, nullptr, &(frame_semaphore.RenderCompleteSemaphore)) == VK_SUCCESS,
                "Failed to create Render complete Semaphore")
        }
    }

    VkRenderPass VulkanWindow::GetRenderPass() const
    {
        return m_render_pass;
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

    VulkanWindow::~VulkanWindow()
    {
        const auto& device = m_engine->GetVulkanInstance().GetHighPerformantDevice();
        MarkVulkanInternalObjectDirty(device);

        vkDestroySwapchainKHR(device.GetNativeDeviceHandle(), m_swapchain, nullptr);
        vkDestroySurfaceKHR(m_engine->GetVulkanInstance().GetNativeHandle(), m_vulkan_surface, nullptr);

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
        ZENGINE_CORE_INFO("Window has been closed");

        Event::EngineClosedEvent e(event.GetName().c_str());
        Event::EventDispatcher   event_dispatcher(e);
        event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, m_engine, std::placeholders::_1));
        return true;
    }

    bool VulkanWindow::OnWindowResized(WindowResizedEvent& event)
    {
        if (event.GetWidth() > 0 && event.GetHeight() > 0)
        {
            RecreateSwapChain(m_swapchain, m_engine->GetVulkanInstance().GetHighPerformantDevice());
        }

        ZENGINE_CORE_INFO("Window has been resized");

        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowResizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnWindowMinimized(Event::WindowMinimizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been minimized");

        m_property.IsMinimized = true;
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowMinimizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnWindowMaximized(Event::WindowMaximizedEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been maximized");

        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowMaximizedEvent>(std::bind(&CoreWindow::ForwardEventToLayers, this, std::placeholders::_1));
        return false;
    }

    bool VulkanWindow::OnWindowRestored(Event::WindowRestoredEvent& event)
    {
        ZENGINE_CORE_INFO("Window has been restored");

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
