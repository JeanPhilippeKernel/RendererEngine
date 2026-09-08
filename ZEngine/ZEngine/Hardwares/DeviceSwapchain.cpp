
#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/Rendering/Specifications/AttachmentSpecification.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <ZEngine/Windows/CoreWindow.h>

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

        BufferredFrameCount                                              = buffered_frame_size;
        FrameContextPoolSize                                             = BufferredFrameCount * FrameContextPoolSizeFactor;

        RenderTimeline                                                   = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device, true);

        Specifications::AttachmentSpecification attachment_specification = {.BindPoint = Specifications::PipelineBindPoint::GRAPHIC};
        attachment_specification.ColorsMap.init(&Arena, 2);
        attachment_specification.ColorsMap[0]                 = {};
        attachment_specification.ColorsMap[0].Format          = ImageFormat::FORMAT_FROM_DEVICE;
        attachment_specification.ColorsMap[0].Load            = LoadOperation::CLEAR;
        attachment_specification.ColorsMap[0].Store           = StoreOperation::STORE;
        attachment_specification.ColorsMap[0].Initial         = ImageLayout::UNDEFINED;
        attachment_specification.ColorsMap[0].Final           = ImageLayout::PRESENT_SRC;
        attachment_specification.ColorsMap[0].ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
        SwapchainAttachment                                   = ZPushStructCtorArgs(&Arena, RenderPasses::Attachment, Device, std::move(attachment_specification));

        IdleFrameThreshold                                    = (BufferredFrameCount * 3 * 3 * 3);
        FrameContexts.init(&Arena, FrameContextPoolSize, FrameContextPoolSize);

        for (uint32_t i = 0; i < FrameContextPoolSize; ++i)
        {
            auto& frame    = FrameContexts[i];

            frame.Index    = (i % BufferredFrameCount);
            frame.Acquired = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
            frame.Fence    = ZPushStructCtorArgs(&Arena, Primitives::Fence, Device, true);
        }

        Create();

        ImageInFlights.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        RenderCompletes.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        PresentCompletes.init(&Arena, SwapchainImageCount, SwapchainImageCount);
        for (uint32_t i = 0; i < SwapchainImageCount; ++i)
        {
            ImageInFlights[i]   = nullptr;
            RenderCompletes[i]  = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
            PresentCompletes[i] = ZPushStructCtorArgs(&Arena, Primitives::Fence, Device);
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
        else
        {
            // Surface does not report a concrete extent (Wayland, headless) — ask the window.
            SwapchainImageWidth  = Device->CurrentWindow->GetWidth();
            SwapchainImageHeight = Device->CurrentWindow->GetHeight();
        }

        // {0,0} extent is a spec violation; destroy stale swapchain and retry next frame.
        if (SwapchainImageWidth == 0 || SwapchainImageHeight == 0)
        {
            if (SwapchainHandle != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(Device->LogicalDevice, SwapchainHandle, nullptr);
                SwapchainHandle = VK_NULL_HANDLE;
            }
            return;
        }

        VkSwapchainKHR           old_swapchain         = (SwapchainHandle != VK_NULL_HANDLE) ? SwapchainHandle : VK_NULL_HANDLE;
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
            .oldSwapchain     = old_swapchain,
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

        scratch                         = ZGetScratch(&Arena);

        Array<VkImage> swapchain_images = {};
        swapchain_images.init(scratch.Arena, SwapchainImageCount, SwapchainImageCount);
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &SwapchainImageCount, swapchain_images.data()) == VK_SUCCESS, "Failed to get VkImages from Swapchain")
        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainImageViews[i] = Device->CreateImageView(swapchain_images[i], Device->SurfaceFormat.format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

            Array<VkImageView> fb_images_views;
            fb_images_views.init(scratch.Arena, 1);
            fb_images_views.push(SwapchainImageViews[i]);
            SwapchainFramebuffers[i] = Device->CreateFramebuffer(ArrayView{fb_images_views}, SwapchainAttachment->GetHandle(), SwapchainImageWidth, SwapchainImageHeight);
        }

        ZReleaseScratch(scratch);

        if (old_swapchain != VK_NULL_HANDLE)
        {
            ZENGINE_DESTROY_VULKAN_HANDLE(Device->LogicalDevice, vkDestroySwapchainKHR, old_swapchain, nullptr)
        }
    }

    void DeviceSwapchain::Clear()
    {
        for (uint32_t i = 0; i < SwapchainImageCount; ++i)
        {
            DeferredFreeEntry iv = {};
            iv.EntryKind         = DeferredFreeEntry::Kind::VkHandle;
            iv.Data.Vk           = {SwapchainImageViews[i], DeviceResourceType::IMAGEVIEW, nullptr};
            Device->DeferFree(iv);
            DeferredFreeEntry fb = {};
            fb.EntryKind         = DeferredFreeEntry::Kind::VkHandle;
            fb.Data.Vk           = {SwapchainFramebuffers[i], DeviceResourceType::FRAMEBUFFER, nullptr};
            Device->DeferFree(fb);
            SwapchainImageViews[i]   = VK_NULL_HANDLE;
            SwapchainFramebuffers[i] = VK_NULL_HANDLE;
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
        // Destroy frame-context and swapchain-image Vulkan objects.
        // All destructors use Device->DeferFree — the second PendingFree.Drain()
        // at the end of VulkanDevice::Deinitialize() drains them before vkDestroyDevice.
        for (uint32_t i = 0; i < FrameContexts.size(); ++i)
        {
            if (FrameContexts[i].Acquired)
                FrameContexts[i].Acquired->~Semaphore();
            if (FrameContexts[i].Fence)
                FrameContexts[i].Fence->~Fence();
        }
        for (uint32_t i = 0; i < RenderCompletes.size(); ++i)
        {
            if (RenderCompletes[i])
                RenderCompletes[i]->~Semaphore();
        }
        for (uint32_t i = 0; i < PresentCompletes.size(); ++i)
        {
            if (PresentCompletes[i])
                PresentCompletes[i]->~Fence();
        }
        if (RenderTimeline)
        {
            RenderTimeline->~Semaphore();
            RenderTimeline = nullptr;
        }

        Clear();
        ZENGINE_DESTROY_VULKAN_HANDLE(Device->LogicalDevice, vkDestroySwapchainKHR, SwapchainHandle, nullptr)
        SwapchainAttachment->Dispose();
    }

    void DeviceSwapchain::AcquireNextImage(uint32_t frame_context_idx)
    {
        if (Recreation != RecreationState::None)
        {
            // ImageInFlights covers submit_1 (render), PresentCompletes covers submit_2
            // (present bridge). Together they drain the full in-flight GPU pipeline.
            for (uint32_t i = 0; i < ImageInFlights.size(); ++i)
            {
                if (ImageInFlights[i] != nullptr)
                    ImageInFlights[i]->Wait(UINT64_MAX);
            }

            for (uint32_t i = 0; i < PresentCompletes.size(); ++i)
            {
                if (PresentCompletes[i]->GetState() == Rendering::Primitives::FenceState::Submitted)
                {
                    PresentCompletes[i]->Wait(UINT64_MAX);
                    PresentCompletes[i]->Reset();
                }
            }

            // Defense-in-depth: ImageInFlights/PresentCompletes are sized SwapchainImageCount,
            // but the engine allows up to FrameContextPoolSize (BufferredFrameCount * 4) frames'
            // command buffers to be concurrently in-flight — more than those two arrays track.
            // Wait on every frame context's own fence too, so recreation never proceeds while a
            // command buffer beyond the swapchain-image-indexed slots is still executing
            // (issue #736).
            for (uint32_t i = 0; i < FrameContexts.size(); ++i)
            {
                if (FrameContexts[i].Fence->GetState() == Rendering::Primitives::FenceState::Submitted)
                {
                    FrameContexts[i].Fence->Wait(UINT64_MAX);
                    FrameContexts[i].Fence->Reset();
                }
            }

            for (int i = 0; i < FrameContextPoolSizeFactor; ++i)
            {
                FrameContexts[i + FrameContextOffset].Acquired->SetState(Primitives::SemaphoreState::Idle);
            }

            if (Device->RRM)
            {
                auto* rrm = static_cast<Rendering::RenderResourceManager*>(Device->RRM);
                rrm->ClearAsyncUploads();
                rrm->ResetTextureTimelines();
            }

            // RenderTimelineNextValue is a CPU-side counter incremented exactly once per real
            // submission (see the ++RenderTimelineNextValue call sites) — it is already correct
            // and monotonic on its own. Do NOT resync it to vkGetSemaphoreCounterValue here: that
            // "completed" value only reflects the fence slots waited on above (ImageInFlights /
            // PresentCompletes, sized SwapchainImageCount), which can be smaller than the actual
            // in-flight depth the engine allows (FrameContextPoolSize = BufferedFrameCount * 4).
            // Rewinding this counter backward stamps every DeferFree call made afterward (by this
            // Clear() and by unrelated callers like RenderGraph::Resize for viewport render
            // targets) with an already-satisfied timeline value, so Drain() destroys them
            // immediately — even while a still-executing command buffer from a frame beyond the
            // waited-on slots is referencing them. This was the root cause of the
            // vkDestroyFramebuffer/vkDestroyImage "in use by VkCommandBuffer" crash on fast
            // resize (issue #736).
            FrameContextOffset = (FrameContextOffset + FrameContextPoolSizeFactor) % FrameContextPoolSize;

            Clear();
            Create(); // may leave SwapchainHandle == VK_NULL_HANDLE on zero-size surface

            if (SwapchainHandle == VK_NULL_HANDLE)
            {
                // Zero-size surface — retry next frame.
                Recreation            = RecreationState::Pending;
                FrameContext& aborted = FrameContexts[frame_context_idx + FrameContextOffset];
                aborted.ImageIndex    = std::numeric_limits<uint32_t>::max();
                CurrentFrame          = &aborted;
                return;
            }

            Recreation = RecreationState::None;
            ZENGINE_CORE_WARN("Swapchain recreated: {}x{}", SwapchainImageWidth, SwapchainImageHeight)

            if (OnSwapchainResized)
                OnSwapchainResized(SwapchainImageWidth, SwapchainImageHeight, OnSwapchainResizedCtx);
        }

        FrameContext& frame = FrameContexts[frame_context_idx + FrameContextOffset];
        if (frame.Fence->GetState() == Rendering::Primitives::FenceState::Submitted)
            frame.Fence->Wait(UINT64_MAX);
        frame.Fence->Reset();
        frame.Acquired->SetState(Primitives::SemaphoreState::Idle);

        uint32_t image_idx            = 0;
        VkResult acquire_image_result = vkAcquireNextImageKHR(Device->LogicalDevice, SwapchainHandle, UINT64_MAX, frame.Acquired->GetHandle(), VK_NULL_HANDLE, &image_idx);
        frame.Acquired->SetState(Primitives::SemaphoreState::Submitted);
        Device->TickMemory();

        if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // Semaphore not signalled (spec) — image_idx invalid, skip all GPU work.
            frame.ImageIndex = std::numeric_limits<uint32_t>::max();
            CurrentFrame     = &frame;
            Recreation       = RecreationState::FrameAborted;
            return;
        }

        if (Device->CheckDeviceLost(acquire_image_result, "AcquireNextImage"))
        {
            frame.ImageIndex = std::numeric_limits<uint32_t>::max();
            CurrentFrame     = &frame;
            Recreation       = RecreationState::FrameAborted;
            return;
        }

        if (PresentCompletes[image_idx]->GetState() == Rendering::Primitives::FenceState::Submitted)
        {
            PresentCompletes[image_idx]->Wait(UINT64_MAX);
            PresentCompletes[image_idx]->Reset();
        }

        if (ImageInFlights[image_idx] != nullptr && !ImageInFlights[image_idx]->IsSignaled())
            ImageInFlights[image_idx]->Wait(UINT64_MAX);

        RenderCompletes[image_idx]->SetState(Rendering::Primitives::SemaphoreState::Idle);

        ImageInFlights[image_idx] = frame.Fence;
        frame.ImageIndex          = image_idx;
        CurrentFrame              = &frame;
        // SUBOPTIMAL: image is valid; Present() schedules recreation after vkQueuePresentKHR.
    }

    void DeviceSwapchain::Present()
    {
        if (Recreation == RecreationState::FrameAborted)
        {
            // OOD at acquire: semaphore not signalled, no GPU work submitted.
            IdleFrameCount.value.fetch_add(1, std::memory_order_acq_rel);
            Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
            return;
        }

        // The device can go lost mid-frame, inside AppRenderPipeline::EndFrame's own
        // SubmitAsyncUploads() call, before Present() runs — continuing into more Vulkan
        // calls (including the unchecked vkGetSemaphoreCounterValue below) here would just
        // add cascade errors on top of an already-lost device.
        if (Device->IsDeviceLost.load(std::memory_order_acquire))
        {
            Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
            return;
        }

        {
            // Watermark — warn once when live texture slots exceed 75% of pool capacity.
            static bool s_watermark_warned = false;
            if (!s_watermark_warned)
            {
                size_t   live     = Device->GlobalTextures.Size();
                uint32_t capacity = Device->MaxGlobalTexture;
                if (live > static_cast<size_t>(capacity) * 3 / 4)
                {
                    ZENGINE_CORE_WARN("[Bindless] Texture pool at {}/{} slots ({:.0f}%) — consider releasing unused textures", live, capacity, live * 100.0 / capacity)
                    s_watermark_warned = true;
                }
            }

            // Drain all pending texture updates into one batched vkUpdateDescriptorSets call.
            // One call per frame (covering all dequeued handles × all registered DstSets) is
            // cheaper than N calls for N textures arriving in the same frame (e.g. scene load).
            auto scratch = ZGetScratch(&Arena);
            {
                size_t                       req_count             = Device->BindlessTextureSlotRequests.size();
                Array<VkWriteDescriptorSet>  write_descriptor_sets = {};
                Array<VkDescriptorImageInfo> image_infos           = {};
                write_descriptor_sets.init(scratch.Arena, 64 * req_count);
                image_infos.init(scratch.Arena, 64);

                Textures::TextureHandle tex_handle = {};
                while (Device->TextureHandleToUpdates.Pop(tex_handle))
                {
                    auto texture = Device->GlobalTextures.Access(tex_handle);
                    if (!texture)
                    {
                        Device->TextureHandleToUpdates.Enqueue(tex_handle);
                        break;
                    }
                    auto img_buf = Device->ImageBufferManager.Access(texture->BufferHandle);
                    image_infos.push(img_buf->GetDescriptorImageInfo());
                    const VkDescriptorImageInfo* info_ptr = &image_infos[image_infos.size() - 1];

                    for (auto& req : Device->BindlessTextureSlotRequests)
                    {
                        write_descriptor_sets.push(
                            VkWriteDescriptorSet{
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstSet           = req.DstSet,
                            .dstBinding       = req.Binding,
                            .dstArrayElement  = (uint32_t) tex_handle.Index,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                            .pImageInfo       = info_ptr,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                            });
                    }
                }

                if (write_descriptor_sets.size() > 0)
                    vkUpdateDescriptorSets(Device->LogicalDevice, (uint32_t) write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);
            }
            ZReleaseScratch(scratch);
        }

        {
            uint64_t completed = 0;
            vkGetSemaphoreCounterValue(Device->LogicalDevice, RenderTimeline->GetHandle(), &completed);

            TextureDisposeEntry entry = {};
            while (Device->TextureHandleToDispose.pop(entry))
            {
                if (entry.TimelineValue > completed)
                {
                    // Not yet safe — push back and stop rather than skip past it (mirrors the
                    // TextureHandleToUpdates pattern above; render-thread-only, so no race).
                    Device->TextureHandleToDispose.push(entry);
                    break;
                }
                auto texture = Device->GlobalTextures.Access(entry.Handle);
                if (texture)
                {
                    auto buf = Device->ImageBufferManager.Access(texture->BufferHandle);
                    if (buf)
                    {
                        buf->Dispose();
                    }
                    Device->ImageBufferManager.Remove(texture->BufferHandle);
                    Device->GlobalTextures.Remove(entry.Handle);
                }
            }
        }

        auto                   scratch = ZGetScratch(&Arena);

        Array<VkCommandBuffer> buffer  = {};
        buffer.init(scratch.Arena, Device->CommandBufferMgr->EnqueuedCommandBufferIndex, Device->CommandBufferMgr->EnqueuedCommandBufferIndex);
        for (int i = 0; i < buffer.size(); ++i)
        {
            buffer[i] = Device->CommandBufferMgr->EnqueuedCommandBuffers[i]->GetHandle();
        }

        auto render_complete  = RenderCompletes[CurrentFrame->ImageIndex];
        auto present_complete = PresentCompletes[CurrentFrame->ImageIndex];

        if (render_complete->GetState() == Rendering::Primitives::SemaphoreState::Submitted)
            render_complete->SetState(Rendering::Primitives::SemaphoreState::Idle);
        if (CurrentFrame->Fence->GetState() == Rendering::Primitives::FenceState::Submitted)
            CurrentFrame->Fence->Wait(UINT64_MAX);

        QueueView queue             = Device->GetQueue(Rendering::QueueType::GRAPHIC_QUEUE);

        // for the rendering and presentation, we use the 3-submit pattern
        // This is due to Intel drivers bug that deosn't support well the combinaison of Timeline + Binary Semaphore.
        //
        // 1 - Acquire bridge
        // 2 - Rendering work
        // 3 - Present bridge

        // 1- Binary Acquire to a Timeline value
        uint64_t  frame_start_value = ++RenderTimelineNextValue;
        ASSERT_TIMELINE_MONOTONIC(Device->LogicalDevice, RenderTimeline->GetHandle(), frame_start_value);
        uint64_t                      ignored_wait_val = 0;
        VkTimelineSemaphoreSubmitInfo timeline_info0   = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .waitSemaphoreValueCount   = 1, // must match waitSemaphoreCount
            .pWaitSemaphoreValues      = &ignored_wait_val,
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues    = &frame_start_value,
        };

        VkPipelineStageFlags acquire_wait_stage          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore          acquire_wait_semaphores[]   = {CurrentFrame->Acquired->GetHandle()};
        VkSemaphore          acquire_signal_semaphores[] = {RenderTimeline->GetHandle()};
        VkSubmitInfo         submit_0                    = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &timeline_info0,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = acquire_wait_semaphores,
            .pWaitDstStageMask    = &acquire_wait_stage,
            .commandBufferCount   = 0,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = acquire_signal_semaphores,
        };
        VkResult r0 = vkQueueSubmit(queue.Handle, 1, &submit_0, VK_NULL_HANDLE);
        if (Device->CheckDeviceLost(r0, "Present: acquire bridge submit"))
        {
            ZReleaseScratch(scratch);
            return;
        }
        ZENGINE_VALIDATE_ASSERT(r0 == VK_SUCCESS, "Failed to submit acquire bridge")

        struct TimelineAggregate
        {
            uint64_t             MaxValue  = 0;
            VkPipelineStageFlags StageMask = 0;
        };

        Array<VkSemaphore>                                          wait_semaphores             = {};
        Array<uint64_t>                                             wait_values                 = {};
        Array<VkPipelineStageFlags>                                 stage_flags                 = {};
        UnorderedHashMap<Primitives::Semaphore*, TimelineAggregate> max_val_timeline_semaphores = {};

        wait_semaphores.init(scratch.Arena, 10);
        stage_flags.init(scratch.Arena, 10);
        wait_values.init(scratch.Arena, 10);
        max_val_timeline_semaphores.init(scratch.Arena);

        // Seed with RenderTimeline's own acquire-bridge value rather than pushing it
        // directly — an AsyncGPUOperation can also target RenderTimeline (e.g. RRM's mesh
        // batch upload), and pushing both separately would put the same semaphore twice
        // in one submit's wait list with two different values.
        max_val_timeline_semaphores.insert(RenderTimeline, {frame_start_value, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});

        {
            Hardwares::AsyncGPUOperationHandle op;
            while (Device->AsyncGPUOperations.Pop(op))
            {
                ZENGINE_CORE_TRACE("[Present] AsyncGPUOperation: timeline={} signal_value={} stage_flags={:#x}", (void*) op.Timeline->GetHandle(), op.SignalValue, op.StageFlags)

                if (!max_val_timeline_semaphores.contains(op.Timeline))
                {
                    max_val_timeline_semaphores.insert(op.Timeline, {op.SignalValue, op.StageFlags});
                    continue;
                }
                auto& val      = max_val_timeline_semaphores[op.Timeline];
                val.MaxValue   = std::max(val.MaxValue, op.SignalValue);
                val.StageMask |= op.StageFlags;
            }
        }

        for (auto [sem, val] : max_val_timeline_semaphores)
        {
            wait_semaphores.push(sem->GetHandle());
            wait_values.push(val.MaxValue);
            stage_flags.push(val.StageMask);
        }

        uint64_t work_complete_value = ++RenderTimelineNextValue;
        ASSERT_TIMELINE_MONOTONIC(Device->LogicalDevice, RenderTimeline->GetHandle(), work_complete_value);
        VkSemaphore                   work_signal_semaphores[] = {RenderTimeline->GetHandle()};
        VkTimelineSemaphoreSubmitInfo timeline_info_1          = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .waitSemaphoreValueCount   = (uint32_t) wait_values.size(),
            .pWaitSemaphoreValues      = wait_values.data(),
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues    = &work_complete_value,
        };

        VkSubmitInfo submit_info_1 = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &timeline_info_1,
            .waitSemaphoreCount   = (uint32_t) wait_semaphores.size(),
            .pWaitSemaphores      = wait_semaphores.data(),
            .pWaitDstStageMask    = stage_flags.data(),
            .commandBufferCount   = (uint32_t) buffer.size(),
            .pCommandBuffers      = buffer.data(),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = work_signal_semaphores,
        };

        Device->FrameHeaps[CurrentFrame->Index].Flush(&Device->GpuMem);

        auto submit = vkQueueSubmit(queue.Handle, 1, &(submit_info_1), CurrentFrame->Fence->GetHandle());
        if (Device->CheckDeviceLost(submit, "Present: render work submit"))
        {
            ZReleaseScratch(scratch);
            return;
        }
        ZENGINE_VALIDATE_ASSERT(submit == VK_SUCCESS, "Failed to submit queue")

        ZReleaseScratch(scratch);

        Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
        CurrentFrame->Fence->SetState(Rendering::Primitives::FenceState::Submitted);

        uint64_t                      dummy_signal_val            = 0;
        VkPipelineStageFlags          present_wait_stage          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore                   present_wait_semaphores[]   = {RenderTimeline->GetHandle()};
        VkSemaphore                   present_signal_semaphores[] = {render_complete->GetHandle()};
        VkTimelineSemaphoreSubmitInfo timeline_info2              = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .waitSemaphoreValueCount   = 1,
            .pWaitSemaphoreValues      = &work_complete_value,
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues    = &dummy_signal_val,
        };

        VkSubmitInfo submit2 = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &timeline_info2,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = present_wait_semaphores,
            .pWaitDstStageMask    = &present_wait_stage,
            .commandBufferCount   = 0,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = present_signal_semaphores,
        };

        VkResult r2 = vkQueueSubmit(queue.Handle, 1, &submit2, present_complete->GetHandle());
        if (Device->CheckDeviceLost(r2, "Present: present bridge submit"))
            return;
        ZENGINE_VALIDATE_ASSERT(r2 == VK_SUCCESS, "Failed to submit present bridge")

        render_complete->SetState(Rendering::Primitives::SemaphoreState::Submitted);
        present_complete->SetState(Rendering::Primitives::FenceState::Submitted);

        VkSwapchainKHR   swapchains[] = {SwapchainHandle};
        uint32_t         frames[]     = {CurrentFrame->ImageIndex};
        VkSemaphore      semaphores[] = {render_complete->GetHandle()};
        VkPresentInfoKHR present_info = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = semaphores,
            .swapchainCount     = 1,
            .pSwapchains        = swapchains,
            .pImageIndices      = frames,
        };
        VkResult present_result = vkQueuePresentKHR(queue.Handle, &present_info);

        IdleFrameCount.value.fetch_add(1, std::memory_order_acq_rel);

        if (Device->CheckDeviceLost(present_result, "Present: vkQueuePresentKHR"))
            return;

        if (present_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // render_complete was not consumed by present (spec). Drain it before reuse.
            VkPipelineStageFlags drain_stage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            VkSemaphore          drain_wait[] = {render_complete->GetHandle()};
            VkSubmitInfo         drain        = {
                .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount   = 1,
                .pWaitSemaphores      = drain_wait,
                .pWaitDstStageMask    = &drain_stage,
                .commandBufferCount   = 0,
                .signalSemaphoreCount = 0,
            };
            vkQueueSubmit(queue.Handle, 1, &drain, VK_NULL_HANDLE);
            render_complete->SetState(Rendering::Primitives::SemaphoreState::Idle);

            Recreation = RecreationState::Pending;
            return;
        }

        if (present_result == VK_SUBOPTIMAL_KHR)
        {
            Recreation = RecreationState::Pending;
        }
    }
} // namespace ZEngine::Hardwares
