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
    void DeviceSwapchain::Initialize(VulkanDevice* const device, uint32_t buffered_frame_size)
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
        BufferredFrameCount                                   = buffered_frame_size;
        IdleFrameThreshold.store(BufferredFrameCount * 3 * 3, std::memory_order_release);
        FrameContexts.init(&Arena, BufferredFrameCount, BufferredFrameCount);

        for (uint32_t i = 0; i < BufferredFrameCount; ++i)
        {
            auto& frame    = FrameContexts[i];

            frame.Index    = i;
            frame.Acquired = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
            frame.Fence    = ZPushStructCtorArgs(&Arena, Primitives::Fence, Device, true);
        }

        Create();

        ImageInFlights.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        RenderCompletes.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        for (uint32_t i = 0; i < SwapchainImageCount; ++i)
        {
            ImageInFlights[i]  = nullptr;
            RenderCompletes[i] = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
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
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &SwapchainImageCount, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")
        ZReleaseScratch(scratch);

        if (SwapchainImageViews.capacity() <= 0)
        {
            SwapchainImageViews.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        }

        if (SwapchainFramebuffers.capacity() <= 0)
        {
            SwapchainFramebuffers.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        }
    }

    void DeviceSwapchain::Clear()
    {
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

        for (uint32_t i = 0; i < SwapchainImageCount; ++i)
        {
            ImageInFlights[i] = nullptr;
        }
        // We don't call .clear() because we want to reuse the allocated space
        // SwapchainImageViews.clear();
        // SwapchainFramebuffers.clear();
    }

    void DeviceSwapchain::Dispose()
    {
        Clear();
        ZENGINE_DESTROY_VULKAN_HANDLE(Device->LogicalDevice, vkDestroySwapchainKHR, SwapchainHandle, nullptr)
        SwapchainAttachment->Dispose();
    }

    void DeviceSwapchain::AcquireNextImage(uint32_t frame_context_idx)
    {
        if (HasRecreationPending)
        {
#ifdef __APPLE__
            vkDeviceWaitIdle(Device->LogicalDevice);
#endif
            for (uint32_t i = 0; i < ImageInFlights.size(); ++i)
            {
                if (ImageInFlights[i] != nullptr)
                {
                    ImageInFlights[i]->Wait(UINT64_MAX);
                }
            }
            Clear();
            Create();
            AsPresentSource();

            for (auto& frame : FrameContexts)
            {
                *(frame.Fence) = Rendering::Primitives::Fence(Device, true);
                frame.Acquired->SetState(Primitives::SemaphoreState::Idle);
            }

            HasRecreationPending = false;
            ZENGINE_CORE_WARN("Swapchain has been re-created")
        }

        FrameContext& frame = FrameContexts[frame_context_idx];

        if (!(frame.Fence)->IsSignaled())
        {
            frame.Fence->Wait(UINT64_MAX);
        }
        frame.Fence->Reset();

        ZENGINE_VALIDATE_ASSERT(frame.Acquired->GetState() != Primitives::SemaphoreState::Submitted, "")

        uint32_t image_idx            = 0;
        VkResult acquire_image_result = vkAcquireNextImageKHR(Device->LogicalDevice, SwapchainHandle, UINT64_MAX, frame.Acquired->GetHandle(), VK_NULL_HANDLE, &(image_idx));
        frame.Acquired->SetState(Primitives::SemaphoreState::Submitted);
        RenderCompletes[image_idx]->SetState(Rendering::Primitives::SemaphoreState::Idle);

        if (ImageInFlights[image_idx] != nullptr)
        {
            if (!(ImageInFlights[image_idx])->IsSignaled())
            {
                ImageInFlights[image_idx]->Wait(UINT64_MAX);
            }
        }

        ImageInFlights[image_idx] = frame.Fence;
        frame.ImageIndex          = image_idx;
        CurrentFrame              = &frame;

        if (acquire_image_result == VK_SUBOPTIMAL_KHR || acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            ImageInFlights[frame.ImageIndex] = nullptr;
            HasRecreationPending             = true;
        }
    }

    void DeviceSwapchain::AsPresentSource()
    {
        auto           command_buffer_info = Device->CommandBufferMgr->GetInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, (CurrentFrame == nullptr) ? 0u : CurrentFrame->Index, 0, 2, true);

        auto           scratch             = ZGetScratch(&Arena);

        Array<VkImage> SwapchainImages     = {};
        SwapchainImages.init(scratch.Arena, SwapchainImageCount, SwapchainImageCount);
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &SwapchainImageCount, SwapchainImages.data()) == VK_SUCCESS, "Failed to get VkImages from Swapchain")

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
            barrier_spec.DestinationStageMask                                       = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier_spec.LayerCount                                                 = 1;

            Rendering::Primitives::ImageMemoryBarrier barrier{barrier_spec};
            command_buffer_info.Buffer->TransitionImageLayout(barrier);
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

    void DeviceSwapchain::Present()
    {
        if (HasRecreationPending)
        {
            IdleFrameCount.fetch_add(1);
            Device->CommandBufferMgr->EndEnqueuedBuffers();
            Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
            return;
        }

        Textures::TextureHandle tex_handle = {};
        if (Device->TextureHandleToUpdates.Pop(tex_handle))
        {
            auto texture = Device->GlobalTextures.Access(tex_handle);

            if (!texture)
            {
                Device->TextureHandleToUpdates.Enqueue(tex_handle);
            }

            else
            {
                auto        img_buf    = Device->Image2DBufferManager.Access(texture->BufferHandle);
                const auto& image_info = img_buf->GetDescriptorImageInfo();

                auto        scratch    = ZGetScratch(&Arena);
                {
                    Array<VkWriteDescriptorSet> write_descriptor_sets = {};
                    write_descriptor_sets.init(scratch.Arena, Device->WriteBindlessDescriptorSetRequests.size());

                    for (auto& req : Device->WriteBindlessDescriptorSetRequests)
                    {
                        write_descriptor_sets.push(VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = req.DstSet, .dstBinding = req.Binding, .dstArrayElement = (uint32_t) tex_handle.Index, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &(image_info), .pBufferInfo = nullptr, .pTexelBufferView = nullptr});
                    }

                    vkUpdateDescriptorSets(Device->LogicalDevice, write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);
                }
                ZReleaseScratch(scratch);
            }
        }

        Textures::TextureHandle tex_to_dispose = {};
        if (Device->TextureHandleToDispose.Pop(tex_to_dispose))
        {
            auto texture = Device->GlobalTextures.Access(tex_to_dispose);
            if (texture)
            {
                texture->Dispose();
                Device->GlobalTextures.Remove(tex_to_dispose);
            }
        }

        Device->CommandBufferMgr->EndEnqueuedBuffers();
        auto                   scratch = ZGetScratch(&Arena);

        Array<VkCommandBuffer> buffer  = {};
        buffer.init(scratch.Arena, Device->CommandBufferMgr->EnqueuedCommandBufferIndex, Device->CommandBufferMgr->EnqueuedCommandBufferIndex);
        for (int i = 0; i < buffer.size(); ++i)
        {
            buffer[i] = Device->CommandBufferMgr->EnqueuedCommandBuffers[i]->GetHandle();
        }

        auto render_complete = RenderCompletes[CurrentFrame->ImageIndex];

        ZENGINE_VALIDATE_ASSERT(render_complete->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        ZENGINE_VALIDATE_ASSERT(CurrentFrame->Fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")

        QueueView            queue               = Device->GetQueue(Rendering::QueueType::GRAPHIC_QUEUE);
        VkSemaphore          wait_semaphores[]   = {CurrentFrame->Acquired->GetHandle()};
        VkSemaphore          signal_semaphores[] = {render_complete->GetHandle()};
        VkPipelineStageFlags stage_flags[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info         = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = nullptr, .waitSemaphoreCount = 1, .pWaitSemaphores = wait_semaphores, .pWaitDstStageMask = stage_flags, .commandBufferCount = (uint32_t) buffer.size(), .pCommandBuffers = buffer.data(), .signalSemaphoreCount = 1, .pSignalSemaphores = signal_semaphores};

        auto                 submit              = vkQueueSubmit(queue.Handle, 1, &(submit_info), CurrentFrame->Fence->GetHandle());
        ZENGINE_VALIDATE_ASSERT(submit == VK_SUCCESS, "Failed to submit queue")

        ZReleaseScratch(scratch);

        Device->CommandBufferMgr->ResetEnqueuedBufferIndex();

        CurrentFrame->Fence->SetState(Rendering::Primitives::FenceState::Submitted);
        render_complete->SetState(Rendering::Primitives::SemaphoreState::Submitted);

        VkSwapchainKHR   swapchains[]   = {SwapchainHandle};
        uint32_t         frames[]       = {CurrentFrame->ImageIndex};
        VkPresentInfoKHR present_info   = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = nullptr, .waitSemaphoreCount = 1, .pWaitSemaphores = signal_semaphores, .swapchainCount = 1, .pSwapchains = swapchains, .pImageIndices = frames};
        VkResult         present_result = vkQueuePresentKHR(queue.Handle, &present_info);

        CurrentFrame->Acquired->SetState(Rendering::Primitives::SemaphoreState::Idle);

        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
        {
            HasRecreationPending = true;
            if (present_result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                return;
            }
        }

        ZENGINE_VALIDATE_ASSERT(present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR, "Failed to present current frame on Window")

        IdleFrameCount.fetch_add(1);
    }
} // namespace ZEngine::Hardwares
