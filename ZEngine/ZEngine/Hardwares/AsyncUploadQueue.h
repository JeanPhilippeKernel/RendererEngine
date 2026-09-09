#pragma once
#include <ZEngine/Core/Containers/SPSCQueue.h>
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

    // One GPU submission tracked via a timeline semaphore instead of a CPU-blocking fence.
    struct AsyncUploadJob
    {
        CommandBuffer*                    Buffer       = nullptr;
        Rendering::Primitives::Semaphore* Timeline     = nullptr;
        Rendering::Primitives::Semaphore* WaitTimeline = nullptr;
        VkPipelineStageFlags2             WaitFlag     = 0;
        uint64_t                          SignalValue  = 0;
        uint64_t                          WaitValue    = UINT64_MAX;
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
