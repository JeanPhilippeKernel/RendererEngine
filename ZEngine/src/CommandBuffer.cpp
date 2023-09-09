#include <pch.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/ResourceTypes.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Buffers
{
    CommandBuffer::CommandBuffer(VkCommandPool command_pool, Rendering::QueueType type, bool present_on_swapchain)
    {
        m_queue_type = type;
        if (present_on_swapchain)
        {
            m_signal_semaphore = CreateRef<Primitives::Semaphore>();
        }
        m_signal_fence = CreateRef<Primitives::Fence>();

        ZENGINE_VALIDATE_ASSERT(command_pool != VK_NULL_HANDLE, "Command Pool cannot be null")

        auto                        device                         = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        VkCommandBufferAllocateInfo command_buffer_allocation_info = {};
        command_buffer_allocation_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocation_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocation_info.commandBufferCount          = 1;
        command_buffer_allocation_info.commandPool                 = command_pool;

        ZENGINE_VALIDATE_ASSERT(vkAllocateCommandBuffers(device, &command_buffer_allocation_info, &m_command_buffer) == VK_SUCCESS, "Failed to allocate command buffer!")
    }

    CommandBuffer::~CommandBuffer()
    {
        m_signal_fence.reset();
        m_signal_semaphore.reset();
    }

    VkCommandBuffer CommandBuffer::GetHandle() const
    {
        return m_command_buffer;
    }

    void CommandBuffer::Begin()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Idle, "command buffer must be in Idle state")

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        m_command_buffer_state = CommanBufferState::Recording;
    }

    void CommandBuffer::End()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Recording, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(m_command_buffer) == VK_SUCCESS, "Failed to end recording command buffer!")

        m_command_buffer_state = CommanBufferState::Ended;
    }

    void CommandBuffer::WaitForExecution()
    {
        if (m_command_buffer_state != CommanBufferState::Submitted)
        {
            ZENGINE_CORE_WARN("Command Buffer can't be waited because it's yet to be submitted")
        }

        if (IsExecuting())
        {
            if (!m_signal_fence->Wait())
            {
                ZENGINE_CORE_WARN("Failed to wait for Command buffer's Fence, due to time out")
            }
        }

        if (m_signal_fence->GetState() == Primitives::FenceState::Submitted)
        {
            m_signal_fence->Reset();
        }

        m_command_buffer_state = CommanBufferState::Idle;
    }

    bool CommandBuffer::IsExecuting()
    {
        return m_command_buffer_state == CommanBufferState::Submitted && !m_signal_fence->IsSignaled();
    }

    void CommandBuffer::Submit()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Ended, "command buffer must be in ended state")

        Hardwares::VulkanDevice::QueueSubmit(
            m_queue_type, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, m_command_buffer, nullptr, m_signal_semaphore.get(), m_signal_fence.get());
        m_command_buffer_state = CommanBufferState::Submitted;
    }

    CommanBufferState CommandBuffer::GetState() const
    {
        return CommanBufferState{m_command_buffer_state.load()};
    }

    Primitives::Semaphore* CommandBuffer::GetSignalSemaphore() const
    {
        return m_signal_semaphore.get();
    }
} // namespace ZEngine::Rendering::Buffers
