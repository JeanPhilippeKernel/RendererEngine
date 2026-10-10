#pragma once
#include <ZEngine/Core/Containers/SPSCQueue.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace ZEngine::Rendering::Primitives
{
    struct Semaphore;
} // namespace ZEngine::Rendering::Primitives

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
    struct CommandBuffer;

    /// @brief Describes a submitted texture upload awaiting graph-side consumption.
    /// @details The producer releases the image into PostReleaseLayout. The graph waits
    ///          on CompletionTimeline and records the matching consumer acquire barrier.
    struct StreamingUploadTicket
    {
        Rendering::Textures::TextureHandle Texture             = {};
        Rendering::Primitives::Semaphore*  CompletionTimeline  = nullptr;
        uint64_t                           CompletionValue     = 0;
        VkImageLayout                      PostReleaseLayout   = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t                           ProducerQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    };

    using AsyncUploadSubmittedFn = void (*)(void* context, const StreamingUploadTicket& ticket);

    // One GPU submission tracked via a timeline semaphore instead of a CPU-blocking fence.
    struct AsyncUploadJob
    {
        CommandBuffer*                    Buffer            = nullptr;
        Rendering::Primitives::Semaphore* Timeline          = nullptr;
        Rendering::Primitives::Semaphore* WaitTimeline      = nullptr;
        VkPipelineStageFlags2             WaitFlag          = 0;
        uint64_t                          SignalValue       = 0;
        uint64_t                          WaitValue         = UINT64_MAX;
        // Streaming uploads are waited by the graph at their first consumer rather
        // than indiscriminately by the next frame's presentation submission.
        bool                              ExposeToFrameWait = true;
        StreamingUploadTicket             StreamingTicket   = {};
        AsyncUploadSubmittedFn            OnSubmitted       = nullptr;
        void*                             SubmissionContext = nullptr;
    };

    // Submits queued GPU work and registers each job with VulkanDevice::EnqueueAsyncGPUOperation
    // so DeviceSwapchain::Present() waits on it, without blocking the thread that recorded it.
    // Render-thread only, single-producer/single-consumer: Enqueue in BeginFrame, SubmitAll in
    // EndFrame, so the queue is always fully drained within the frame it was recorded in.
    struct AsyncUploadQueue
    {
        static constexpr uint32_t CAPACITY = 1024;

        void                      Initialize(VulkanDevice* device);
        void                      Deinitialize();
        void                      Enqueue(const AsyncUploadJob& job);
        void                      SubmitAll();
        void                      Clear();

    private:
        VulkanDevice*                                         m_device = nullptr;
        Core::Containers::SPSCQueue<AsyncUploadJob, CAPACITY> m_jobs   = {};
    };
} // namespace ZEngine::Hardwares
