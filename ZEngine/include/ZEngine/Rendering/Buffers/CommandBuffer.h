#pragma once
#include <atomic>
#include <vulkan/vulkan.h>
#include <ZEngineDef.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/ResourceTypes.h>

namespace ZEngine::Rendering::Buffers
{
    enum CommanBufferState : uint8_t
    {
        Idle = 0,
        Recording,
        Ended,
        Submitted
    };

    struct CommandBuffer
    {
        CommandBuffer(VkCommandPool command_pool, Rendering::QueueType type, bool present_on_swapchain);

        ~CommandBuffer();

        VkCommandBuffer GetHandle() const;

        void Begin();

        void End();

        void WaitForExecution();

        bool IsExecuting();

        void Submit();

        CommanBufferState GetState() const;

        Primitives::Semaphore* GetSignalSemaphore() const;

    private:
        std::atomic_uint8_t        m_command_buffer_state{CommanBufferState::Idle};
        VkCommandBuffer            m_command_buffer{VK_NULL_HANDLE};
        Rendering::QueueType       m_queue_type;
        Ref<Primitives::Fence>     m_signal_fence;
        Ref<Primitives::Semaphore> m_signal_semaphore;
    };
}