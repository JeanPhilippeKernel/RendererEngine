#include <pch.h>
#include <ZEngineDef.h>
#include <GLFW/glfw3.h>
#include <Rendering/Swapchain.h>
#include <Helpers/RendererHelper.h>
#include <Rendering/Renderers/RenderPasses/RenderPassSpecification.h>
#include <Hardwares/VulkanDevice.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Rendering
{
    Swapchain::Swapchain(void* native_window)
    {
        m_native_window = native_window;

        Create();
        AcquireNextImage();
    }

    Swapchain::~Swapchain()
    {
        Dispose();
        m_native_window = nullptr; // we are not responsible of the cleanup
    }

    void Swapchain::Resize()
    {
        Dispose();
        Create();
        AcquireNextImage();
    }

    void Swapchain::AcquireNextImage()
    {
        auto frame_count      = (int32_t) m_image_collection.size();
        m_current_frame_index = (m_current_frame_index + 1) % frame_count;

        Primitives::Semaphore* signal_semaphore = m_acquired_semaphore_collection[m_current_frame_index].get();

        ZENGINE_VALIDATE_ASSERT(signal_semaphore->GetState() != Primitives::SemaphoreState::Submitted, "")

        m_last_frame_image_index = m_current_frame_image_index;

        VkResult acquire_image_result = vkAcquireNextImageKHR(
            Hardwares::VulkanDevice::GetNativeDeviceHandle(), m_handle, UINT64_MAX, signal_semaphore->GetHandle(), VK_NULL_HANDLE, &m_current_frame_image_index);

        signal_semaphore->SetState(Primitives::SemaphoreState::Submitted);
    }

    void Swapchain::Present()
    {
        m_wait_semaphore_collection.clear();
        m_wait_semaphore_collection.shrink_to_fit();

        auto& command_pool_collection = Hardwares::VulkanDevice::GetAllCommandPools();
        for (auto& command_pool : command_pool_collection)
        {
            auto semaphore_collection = command_pool->GetAllWaitSemaphoreCollection();
            std::copy(std::begin(semaphore_collection), std::end(semaphore_collection), std::back_inserter(m_wait_semaphore_collection));
        }

        Primitives::Semaphore* signal_semaphore = m_acquired_semaphore_collection[m_current_frame_index].get();
        if (signal_semaphore->GetState() == Primitives::SemaphoreState::Submitted)
        {
            m_wait_semaphore_collection.emplace_back(signal_semaphore);
        }

        Hardwares::VulkanDevice::Present(m_handle, &m_current_frame_image_index, m_wait_semaphore_collection);

        AcquireNextImage();
    }

    uint32_t Swapchain::GetMinImageCount() const
    {
        return m_min_image_count;
    }

    uint32_t Swapchain::GetImageCount() const
    {
        return m_image_collection.size();
    }

    VkRenderPass Swapchain::GetRenderPass() const
    {
        return m_render_pass;
    }

    VkFramebuffer Swapchain::GetCurrentFramebuffer()
    {
        std::lock_guard lock(m_image_mutex);
        ZENGINE_VALIDATE_ASSERT(m_current_frame_index < m_framebuffer_collection.size(), "Index out of range while accessing framebuffer collection")
        return m_framebuffer_collection[m_current_frame_index];
    }

    void Swapchain::Create()
    {
        auto native_device   = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        auto physical_device = Hardwares::VulkanDevice::GetNativePhysicalDeviceHandle();
        auto surface         = Hardwares::VulkanDevice::GetSurface();
        auto surface_format  = Hardwares::VulkanDevice::GetSurfaceFormat();

        // Surface capabilities
        VkExtent2D extent = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &m_capabilities);
        if (m_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            extent = m_capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(reinterpret_cast<GLFWwindow*>(m_native_window), &width, &height);

            extent        = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            extent.width  = std::clamp(extent.width, m_capabilities.minImageExtent.width, m_capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height, m_capabilities.minImageExtent.height, m_capabilities.maxImageExtent.height);
        }

        m_min_image_count = std::clamp(m_min_image_count, m_capabilities.minImageCount + 1, m_capabilities.maxImageCount);

        VkSwapchainCreateInfoKHR swapchain_create_info = {};
        swapchain_create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_create_info.surface                  = surface;
        swapchain_create_info.minImageCount            = m_min_image_count;
        swapchain_create_info.imageFormat              = surface_format.format;
        swapchain_create_info.imageColorSpace          = surface_format.colorSpace;
        swapchain_create_info.imageExtent              = extent;
        swapchain_create_info.imageArrayLayers         = m_capabilities.maxImageArrayLayers;
        swapchain_create_info.preTransform             = m_capabilities.currentTransform;

        std::unordered_set<uint32_t> device_family_indices = {
            Hardwares::VulkanDevice::GetQueue(QueueType::GRAPHIC_QUEUE).FamilyIndex, Hardwares::VulkanDevice::GetQueue(QueueType::TRANSFER_QUEUE).FamilyIndex};

        m_queue_family_index_collection = std::vector<uint32_t>{device_family_indices.begin(), device_family_indices.end()};

        if (m_queue_family_index_collection.size() > 1)
        {
            swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            swapchain_create_info.queueFamilyIndexCount = m_queue_family_index_collection.size();
            swapchain_create_info.pQueueFamilyIndices   = m_queue_family_index_collection.data();
        }
        else
        {
            swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
            swapchain_create_info.queueFamilyIndexCount = 0;
            swapchain_create_info.pQueueFamilyIndices   = nullptr;
        }
        swapchain_create_info.imageUsage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchain_create_info.clipped        = VK_TRUE;

        ZENGINE_VALIDATE_ASSERT(vkCreateSwapchainKHR(native_device, &swapchain_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create Swapchain")

        /*Swapchain Images & ImageView*/
        uint32_t image_count{0};
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(native_device, m_handle, &image_count, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")

        m_image_collection.resize(image_count);
        m_image_view_collection.resize(image_count);
        m_framebuffer_collection.resize(image_count);
        m_acquired_semaphore_collection.resize(image_count);

        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(native_device, m_handle, &image_count, m_image_collection.data()) == VK_SUCCESS, "Failed to get Images from Swapchain")

        /*Transition Image from Undefined to Present_src*/
        auto command_buffer = Hardwares::VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
        for (int i = 0; i < m_image_collection.size(); ++i)
        {
            VkImageMemoryBarrier barrier            = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = m_image_collection[i]; // The image you want to transition
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = 0; // No need for any source access
            barrier.dstAccessMask                   = VK_ACCESS_MEMORY_READ_BIT;
            vkCmdPipelineBarrier(command_buffer->GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        Hardwares::VulkanDevice::EndInstantCommandBuffer(command_buffer);

        for (size_t i = 0; i < m_image_view_collection.size(); ++i)
        {
            m_image_view_collection[i] = Hardwares::VulkanDevice::CreateImageView(m_image_collection[i], surface_format.format, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        /*Depth Image*/
        VkFormat depth_format = Helpers::FindDepthFormat();
        m_depth_buffer = CreateRef<Buffers::Image2DBuffer>(extent.width, extent.height, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

        /* Create RenderPass */
        if (m_render_pass == VK_NULL_HANDLE)
        {
            RenderPasses::AttachmentSpecification attachment_specification = {
                .ColorAttachements = {
                    VkAttachmentDescription{
                        .format        = surface_format.format,
                        .samples       = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp       = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                    VkAttachmentDescription{
                        .format         = Helpers::FindDepthFormat(),
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
            attachment_specification.SubpassDependencies              = {VkSubpassDependency{
                             .srcSubpass    = VK_SUBPASS_EXTERNAL,
                             .dstSubpass    = 0,
                             .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             .srcAccessMask = 0,
                             .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT}};

            attachment_specification.SubpassSpecifications = {sub_pass_specification};
            m_render_pass                                  = Helpers::CreateRenderPass(attachment_specification);
        }

        /*Swapchain framebuffer*/
        for (size_t i = 0; i < m_framebuffer_collection.size(); i++)
        {
            auto attachments            = std::vector<VkImageView>{m_image_view_collection[i], m_depth_buffer->GetImageViewHandle()};
            m_framebuffer_collection[i] = Helpers::CreateFramebuffer(attachments, m_render_pass, extent.width, extent.height);
        }

        /*Swapchain semaphore*/
        for (size_t i = 0; i < m_acquired_semaphore_collection.size(); i++)
        {
            m_acquired_semaphore_collection[i] = CreateRef<Primitives::Semaphore>();
        }
    }

    void Swapchain::Dispose()
    {
        for (VkImageView image_view : m_image_view_collection)
        {
            if (image_view)
            {
                Hardwares::VulkanDevice::EnqueueForDeletion(DeviceResourceType::IMAGEVIEW, image_view);
            }
        }

        for (VkFramebuffer framebuffer : m_framebuffer_collection)
        {
            if (framebuffer)
            {
                Hardwares::VulkanDevice::EnqueueForDeletion(DeviceResourceType::FRAMEBUFFER, framebuffer);
            }
        }
        m_depth_buffer.reset();

        if (m_render_pass)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(DeviceResourceType::RENDERPASS, m_render_pass);
            m_render_pass = VK_NULL_HANDLE;
        }

        m_image_view_collection.clear();
        m_image_collection.clear();
        m_framebuffer_collection.clear();
        m_acquired_semaphore_collection.clear();

        m_image_view_collection.shrink_to_fit();
        m_image_collection.shrink_to_fit();
        m_framebuffer_collection.shrink_to_fit();
        m_acquired_semaphore_collection.shrink_to_fit();

        Hardwares::VulkanDevice::QueueWaitAll();

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_DESTROY_VULKAN_HANDLE(device, vkDestroySwapchainKHR, m_handle, nullptr)

        m_handle = nullptr;
    }
}