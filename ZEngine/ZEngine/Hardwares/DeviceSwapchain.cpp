
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
        FrameAsyncOperations.init(&Arena, 32);
        RenderWorkSubmittedCallbacks.init(&Arena, 8);

        for (uint32_t i = 0; i < FrameContextPoolSize; ++i)
        {
            auto& frame    = FrameContexts[i];

            frame.Index    = (i % BufferredFrameCount);
            frame.Acquired = ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device);
            frame.Fence    = ZPushStructCtorArgs(&Arena, Primitives::Fence, Device, true);
        }

        Create();
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

        const uint32_t previous_image_count         = SwapchainImageCount;
        ZENGINE_VALIDATE_ASSERT(vkCreateSwapchainKHR(Device->LogicalDevice, &swapchain_create_info, nullptr, &SwapchainHandle) == VK_SUCCESS, "Failed to create Swapchain")
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &SwapchainImageCount, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")
        ZReleaseScratch(scratch);

        if (SwapchainImages.capacity() == 0)
        {
            SwapchainImages.init(&Arena, SwapchainImageCount);
            SwapchainImageViews.init(&Arena, SwapchainImageCount);
            SwapchainFramebuffers.init(&Arena, SwapchainImageCount);
            SwapchainImageLayouts.init(&Arena, SwapchainImageCount);
        }
        else
        {
            SwapchainImages.clear();
            SwapchainImages.reserve(SwapchainImageCount);
            SwapchainImageViews.clear();
            SwapchainImageViews.reserve(SwapchainImageCount);
            SwapchainFramebuffers.clear();
            SwapchainFramebuffers.reserve(SwapchainImageCount);
            SwapchainImageLayouts.clear();
            SwapchainImageLayouts.reserve(SwapchainImageCount);
        }

        SwapchainImages.clear();
        for (uint32_t i = 0; i < SwapchainImageCount; ++i)
            SwapchainImages.push(VK_NULL_HANDLE);
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(Device->LogicalDevice, SwapchainHandle, &SwapchainImageCount, SwapchainImages.data()) == VK_SUCCESS, "Failed to get VkImages from Swapchain")

        if (ImageInFlights.capacity() == 0)
        {
            ImageInFlights.init(&Arena, SwapchainImageCount);
            RenderCompletes.init(&Arena, SwapchainImageCount);
            PresentCompletes.init(&Arena, SwapchainImageCount);
        }
        else if (ImageInFlights.size() != SwapchainImageCount)
        {
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
            ImageInFlights.clear();
            ImageInFlights.reserve(SwapchainImageCount);
            RenderCompletes.clear();
            RenderCompletes.reserve(SwapchainImageCount);
            PresentCompletes.clear();
            PresentCompletes.reserve(SwapchainImageCount);
        }

        if (ImageInFlights.size() != SwapchainImageCount)
        {
            for (uint32_t i = 0; i < SwapchainImageCount; ++i)
            {
                ImageInFlights.push(nullptr);
                RenderCompletes.push(ZPushStructCtorArgs(&Arena, Primitives::Semaphore, Device));
                PresentCompletes.push(ZPushStructCtorArgs(&Arena, Primitives::Fence, Device));
            }
        }

        for (uint32_t i = 0; i < SwapchainImageCount; ++i)
        {
            VkImageView image_view = Device->CreateImageView(SwapchainImages[i], Device->SurfaceFormat.format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
            SwapchainImageViews.push(image_view);
            SwapchainImageLayouts.push(VK_IMAGE_LAYOUT_UNDEFINED);

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            if (!Device->PhysicalDeviceSupportDynamicRendering)
                framebuffer = Device->CreateFramebuffer(ArrayView<VkImageView>{&image_view, 1}, SwapchainAttachment->GetHandle(), SwapchainImageWidth, SwapchainImageHeight);
            SwapchainFramebuffers.push(framebuffer);
        }

        PreviousSwapchainImageCount = previous_image_count;
        if (previous_image_count != SwapchainImageCount)
            ++SwapchainImageCountChangeCount;

        if (old_swapchain != VK_NULL_HANDLE)
        {
            ZENGINE_DESTROY_VULKAN_HANDLE(Device->LogicalDevice, vkDestroySwapchainKHR, old_swapchain, nullptr)
        }
    }

    void DeviceSwapchain::Clear()
    {
        for (uint32_t i = 0; i < SwapchainImageViews.size(); ++i)
        {
            if (SwapchainImageViews[i] != VK_NULL_HANDLE)
            {
                DeferredFreeEntry iv = {};
                iv.EntryKind         = DeferredFreeEntry::Kind::VkHandle;
                iv.Data.Vk           = {SwapchainImageViews[i], DeviceResourceType::IMAGEVIEW, nullptr};
                Device->DeferFree(iv);
            }
            if (SwapchainFramebuffers[i] != VK_NULL_HANDLE)
            {
                DeferredFreeEntry fb = {};
                fb.EntryKind         = DeferredFreeEntry::Kind::VkHandle;
                fb.Data.Vk           = {SwapchainFramebuffers[i], DeviceResourceType::FRAMEBUFFER, nullptr};
                Device->DeferFree(fb);
            }
            SwapchainImageViews[i]   = VK_NULL_HANDLE;
            SwapchainFramebuffers[i] = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < ImageInFlights.size(); ++i)
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

    void DeviceSwapchain::CollectAsyncGPUOperations()
    {
        AsyncGPUOperationHandle deferred_op = {};
        while (Device->DeferredAsyncGPUOperations.pop(deferred_op))
            ZENGINE_VALIDATE_ASSERT(Device->AsyncGPUOperations.push(deferred_op), "Async GPU operation queue overflow")

        AsyncGPUOperationHandle operation = {};
        while (Device->AsyncGPUOperations.pop(operation))
            FrameAsyncOperations.push({operation.StageFlags, operation.SignalValue, operation.Timeline});
    }

    void DeviceSwapchain::Present()
    {

        auto discard_submission_callbacks = [this]() { RenderWorkSubmittedCallbacks.clear(); };

        if (Recreation == RecreationState::FrameAborted)
        {
            // OOD at acquire: semaphore not signalled, no GPU work submitted.
            IdleFrameCount.value.fetch_add(1, std::memory_order_acq_rel);
            Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
            discard_submission_callbacks();
            return;
        }

        // The device can go lost mid-frame before Present() runs — continuing into more
        // Vulkan calls here would just add cascade errors on top of the real failure.
        if (Device->IsDeviceLost.load(std::memory_order_acquire))
        {
            Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
            discard_submission_callbacks();
            return;
        }

        CollectAsyncGPUOperations();

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

        auto                             scratch   = ZGetScratch(&Arena);

        Array<VkCommandBufferSubmitInfo> cmd_infos = {};
        cmd_infos.init(scratch.Arena, Device->CommandBufferMgr->EnqueuedCommandBufferIndex, Device->CommandBufferMgr->EnqueuedCommandBufferIndex);
        for (int i = 0; i < cmd_infos.size(); ++i)
        {
            cmd_infos[i] = {
                .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = Device->CommandBufferMgr->EnqueuedCommandBuffers[i]->GetHandle(),
            };
        }

        auto render_complete  = RenderCompletes[CurrentFrame->ImageIndex];
        auto present_complete = PresentCompletes[CurrentFrame->ImageIndex];

        if (render_complete->GetState() == Rendering::Primitives::SemaphoreState::Submitted)
            render_complete->SetState(Rendering::Primitives::SemaphoreState::Idle);
        if (CurrentFrame->Fence->GetState() == Rendering::Primitives::FenceState::Submitted)
            CurrentFrame->Fence->Wait(UINT64_MAX);

        QueueView queue = Device->GetQueue(Rendering::QueueType::GRAPHIC_QUEUE);

        // Two-submit Synchronization2 pattern:
        //   1 - Render work: acquired binary semaphore + async timeline waits → RenderTimeline
        //   2 - Present bridge: RenderTimeline → binary render_complete
        // The acquired-image semaphore must wait in the submission containing the
        // image transition and writes. A wait in an empty earlier submission does
        // not make the later command buffers' stages wait for WSI image ownership.

        struct TimelineAggregate
        {
            uint64_t              MaxValue  = 0;
            VkPipelineStageFlags2 StageMask = 0;
        };

        Array<VkSemaphoreSubmitInfo>                                wait_sem_infos              = {};
        UnorderedHashMap<Primitives::Semaphore*, TimelineAggregate> max_val_timeline_semaphores = {};

        wait_sem_infos.init(scratch.Arena, 10);
        max_val_timeline_semaphores.init(scratch.Arena);

        wait_sem_infos.push({
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = CurrentFrame->Acquired->GetHandle(),
            .value     = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });

        // RenderTimeline is the signal target of this submission, never a wait
        // source. Async producer timelines are appended below.

        {
            for (const auto& op : FrameAsyncOperations)
            {
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
            wait_sem_infos.push({
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = sem->GetHandle(),
                .value     = val.MaxValue,
                .stageMask = val.StageMask,
            });
        }

        uint64_t              work_complete_value  = ++RenderTimelineNextValue;

        VkSemaphoreSubmitInfo work_complete_signal = {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = RenderTimeline->GetHandle(),
            .value     = work_complete_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        VkSubmitInfo2 submit_info_1 = {
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount   = (uint32_t) wait_sem_infos.size(),
            .pWaitSemaphoreInfos      = wait_sem_infos.data(),
            .commandBufferInfoCount   = (uint32_t) cmd_infos.size(),
            .pCommandBufferInfos      = cmd_infos.data(),
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos    = &work_complete_signal,
        };

        Device->FrameHeaps[CurrentFrame->Index].Flush(&Device->GpuMem);

        auto submit = vkQueueSubmit2(queue.Handle, 1, &submit_info_1, CurrentFrame->Fence->GetHandle());
        if (Device->CheckDeviceLost(submit, "Present: render work submit"))
        {
            ZReleaseScratch(scratch);
            discard_submission_callbacks();
            return;
        }
        ZENGINE_VALIDATE_ASSERT(submit == VK_SUCCESS, "Failed to submit queue")

        // The graphics command buffers are now owned by Vulkan. Deliver callbacks
        // before presentation: a later WSI error cannot undo this submission.
        for (const RenderWorkSubmissionCallback& callback : RenderWorkSubmittedCallbacks)
            if (callback.Function)
                callback.Function(callback.Context, RenderTimeline, work_complete_value);
        RenderWorkSubmittedCallbacks.clear();

        ZReleaseScratch(scratch);

        Device->CommandBufferMgr->ResetEnqueuedBufferIndex();
        CurrentFrame->Fence->SetState(Rendering::Primitives::FenceState::Submitted);

        VkSemaphoreSubmitInfo present_wait_info = {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = RenderTimeline->GetHandle(),
            .value     = work_complete_value,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
        VkSemaphoreSubmitInfo present_signal_info = {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = render_complete->GetHandle(),
            .value     = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        VkSubmitInfo2 submit2 = {
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount   = 1,
            .pWaitSemaphoreInfos      = &present_wait_info,
            .commandBufferInfoCount   = 0,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos    = &present_signal_info,
        };

        VkResult r2 = vkQueueSubmit2(queue.Handle, 1, &submit2, present_complete->GetHandle());
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

    void DeviceSwapchain::EnqueueRenderWorkSubmittedCallback(RenderWorkSubmittedFn fn, void* context)
    {
        if (fn)
            RenderWorkSubmittedCallbacks.push({.Function = fn, .Context = context});
    }
} // namespace ZEngine::Hardwares
