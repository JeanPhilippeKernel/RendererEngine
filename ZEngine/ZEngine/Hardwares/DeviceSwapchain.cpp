#include <Hardwares/DeviceSwapchain.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Specifications/AttachmentSpecification.h>
#include <Rendering/Specifications/FormatSpecification.h>
#include <Windows/CoreWindow.h>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Hardwares
{
    void DeviceSwapchain::Initialize(VulkanDevice* const device)
    {
        device->Arena->CreateSubArena(ZMega(3), &Arena);

        Device                                                           = device;

        Specifications::AttachmentSpecification attachment_specification = {.BindPoint = Specifications::PipelineBindPoint::GRAPHIC};
        attachment_specification.ColorsMap.init(&Arena, 2);
        attachment_specification.ColorsMap[0]                 = {};
        attachment_specification.ColorsMap[0].Format          = ImageFormat::FORMAT_FROM_DEVICE;
        attachment_specification.ColorsMap[0].Load            = LoadOperation::CLEAR;
        attachment_specification.ColorsMap[0].Store           = StoreOperation::STORE;
        attachment_specification.ColorsMap[0].Initial         = ImageLayout::UNDEFINED;
        attachment_specification.ColorsMap[0].Final           = ImageLayout::PRESENT_SRC;
        attachment_specification.ColorsMap[0].ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
        SwapchainAttachment                                   = ZPushStructCtorArgs(&Arena, RenderPasses::Attachment, Device, attachment_specification);
        PreviousFrameIndex                                    = 0;
        CurrentFrameIndex                                     = 0;

        Create();

        SwapchainRenderCompleteSemaphores.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        SwapchainAcquiredSemaphores.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        SwapchainSignalFences.init(&Arena, SwapchainImageCount, SwapchainImageCount);

        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainAcquiredSemaphores[i]       = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
            SwapchainRenderCompleteSemaphores[i] = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
            SwapchainSignalFences[i]             = ZPushStructCtorArgs(&Arena, Primitives::Fence, Device, true);
        }
    }

    void DeviceSwapchain::Create()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Device->PhysicalDevice, Device->Surface, &capabilities);
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            SwapchainImageWidth  = capabilities.currentExtent.width;
            SwapchainImageHeight = capabilities.currentExtent.height;
        }

        auto                     min_image_count       = std::clamp(capabilities.minImageCount, capabilities.minImageCount, capabilities.maxImageCount == 0 ? capabilities.minImageCount + 1 : capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR swapchain_create_info = {
            .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext            = nullptr,
            .surface          = Device->Surface,
            .minImageCount    = min_image_count,
            .imageFormat      = Device->SurfaceFormat.format,
            .imageColorSpace  = Device->SurfaceFormat.colorSpace,
            .imageExtent      = VkExtent2D{.width = SwapchainImageWidth, .height = SwapchainImageHeight},
            .imageArrayLayers = 1,
            .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .preTransform     = capabilities.currentTransform,
            .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode      = Device->PresentMode,
            .clipped          = VK_TRUE,
            .oldSwapchain     = (SwapchainHandle != VK_NULL_HANDLE) ? SwapchainHandle : VK_NULL_HANDLE
        };

        auto            scratch             = ZGetScratch(&Arena);

        Array<uint32_t> family_indice       = {};
        uint32_t        family_indice_count = Device->HasSeperateTransfertQueueFamily ? 2 : 1;
        family_indice.init(scratch.Arena, family_indice_count, family_indice_count);
        family_indice[0] = Device->GraphicFamilyIndex;
        if (Device->HasSeperateTransfertQueueFamily)
        {
            family_indice[1] = Device->TransferFamilyIndex;
        }
        swapchain_create_info.imageSharingMode      = Device->HasSeperateTransfertQueueFamily ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = Device->HasSeperateTransfertQueueFamily ? 2 : 1;
        swapchain_create_info.pQueueFamilyIndices   = family_indice.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateSwapchainKHR(Device->LogicalDevice, &swapchain_create_info, nullptr, &SwapchainHandle) == VK_SUCCESS, "Failed to create Swapchain")

        ZReleaseScratch(scratch);

        uint32_t image_count = 0;
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &image_count, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")

        bool swapchainImageCountChanged = false;
        if (image_count != SwapchainImageCount)
        {
            ZENGINE_CORE_WARN("Max Swapchain image count supported is {}, but requested {}", image_count, SwapchainImageCount)
            auto old_swapchain_image_count = SwapchainImageCount;
            SwapchainImageCount            = image_count;
            ZENGINE_CORE_WARN("Swapchain image count has changed from {} to {}", old_swapchain_image_count, image_count)

            swapchainImageCountChanged = true;
        }

        if ((SwapchainImageCountChangeCount > 0) && (PreviousSwapchainImageCount != SwapchainImageCount))
        {
            ZENGINE_CORE_WARN("Swapchain image count has changed from previous creation")

            auto delta = SwapchainImageCount - PreviousSwapchainImageCount;

            // When delta is less or equal of zero, it means we have enough memory to handle ops
            if (delta > 0 && delta < std::numeric_limits<uint32_t>::max())
            {
                SwapchainImageViews.push({});
                SwapchainFramebuffers.push({});

                Device->CommandBufferMgr->IncreaseBuffers();
                // EnqueuedCommandBuffers.reserve(m_buffer_manager.TotalCommandBufferCount);
            }
        }
        else
        {
            if (SwapchainImageViews.capacity() <= 0)
            {
                SwapchainImageViews.init(&Arena, SwapchainImageCount, SwapchainImageCount);
            }

            if (SwapchainFramebuffers.capacity() <= 0)
            {
                SwapchainFramebuffers.init(&Arena, SwapchainImageCount, SwapchainImageCount);
            }
        }

        if (swapchainImageCountChanged)
        {
            SwapchainImageCountChangeCount++;
        }
    }

    void DeviceSwapchain::Clear()
    {
        PreviousSwapchainImageCount = SwapchainImageCount;

        for (VkImageView image_view : SwapchainImageViews)
        {
            if (image_view)
            {
                Device->EnqueueForDeletion(DeviceResourceType::IMAGEVIEW, image_view);
            }
        }

        for (VkFramebuffer framebuffer : SwapchainFramebuffers)
        {
            if (framebuffer)
            {
                Device->EnqueueForDeletion(DeviceResourceType::FRAMEBUFFER, framebuffer);
            }
        }

        // We don't call .clear() because we want to reuse the allocated space
        // SwapchainImageViews.clear();
        // SwapchainFramebuffers.clear();
    }

    void DeviceSwapchain::Dispose()
    {
        Clear();
        SwapchainAttachment->Dispose();
    }

    void DeviceSwapchain::AcquireNextImage()
    {
        Primitives::Fence* signal_fence = SwapchainSignalFences[CurrentFrameIndex];
        if (!signal_fence->IsSignaled())
        {
            if (!signal_fence->Wait(UINT64_MAX))
            {
                return;
            }
        }

        signal_fence->Reset();
        Primitives::Semaphore* acquired_semaphore = SwapchainAcquiredSemaphores[CurrentFrameIndex];
        ZENGINE_VALIDATE_ASSERT(acquired_semaphore->GetState() != Primitives::SemaphoreState::Submitted, "")

        VkResult acquire_image_result = vkAcquireNextImageKHR(Device->LogicalDevice, SwapchainHandle, UINT64_MAX, acquired_semaphore->GetHandle(), VK_NULL_HANDLE, &SwapchainImageIndex);
        acquired_semaphore->SetState(Primitives::SemaphoreState::Submitted);

        if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_image_result == VK_SUBOPTIMAL_KHR)
        {
            Clear();
            Create();
            AsPresentSource();
            return;
        }
    }

    void DeviceSwapchain::AsPresentSource()
    {
        auto           command_buffer_info = Device->CommandBufferMgr->GetInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, CurrentFrameIndex, 0, 2, true);
        auto           scratch             = ZGetScratch(&Arena);

        Array<VkImage> SwapchainImages     = {};
        SwapchainImages.init(scratch.Arena, SwapchainImageCount, SwapchainImageCount);
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &SwapchainImageCount, SwapchainImages.data()) == VK_SUCCESS, "Failed to get VkImages from Swapchain")

        {
            for (int i = 0; i < SwapchainImages.size(); ++i)
            {
                Rendering::Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                barrier_spec.ImageHandle                                                = SwapchainImages[i];
                barrier_spec.OldLayout                                                  = Specifications::ImageLayout::UNDEFINED;
                barrier_spec.NewLayout                                                  = Specifications::ImageLayout::PRESENT_SRC;
                barrier_spec.ImageAspectMask                                            = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier_spec.SourceAccessMask                                           = 0;
                barrier_spec.DestinationAccessMask                                      = VK_ACCESS_MEMORY_READ_BIT;
                barrier_spec.SourceStageMask                                            = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                barrier_spec.DestinationStageMask                                       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                barrier_spec.LayerCount                                                 = 1;

                Rendering::Primitives::ImageMemoryBarrier barrier{barrier_spec};
                command_buffer_info.Buffer->TransitionImageLayout(barrier);
            }
        }
        Device->CommandBufferMgr->EndInstantCommandBuffer(command_buffer_info);

        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainImageViews[i] = Device->CreateImageView(SwapchainImages[i], Device->SurfaceFormat.format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

            Array<VkImageView> fb_images_views;
            fb_images_views.init(scratch.Arena, 1);
            fb_images_views.push(SwapchainImageViews[i]);
            SwapchainFramebuffers[i] = Device->CreateFramebuffer(ArrayView{fb_images_views}, SwapchainAttachment->GetHandle(), SwapchainImageWidth, SwapchainImageHeight);
        }

        ZReleaseScratch(scratch);
    }

    void DeviceSwapchain::IncrementFrameImageCount()
    {
        PreviousFrameIndex = CurrentFrameIndex;
        CurrentFrameIndex  = (CurrentFrameIndex + 1) % SwapchainImageCount;
    }
} // namespace ZEngine::Hardwares
