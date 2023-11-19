#include <pch.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/ResourceTypes.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/IndirectBuffer.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>

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

    void CommandBuffer::BeginRenderPass(const Ref<Renderers::RenderPasses::RenderPass>& render_pass)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        auto                  render_pass_pipeline   = render_pass->GetPipeline();
        auto                  framebuffer            = render_pass_pipeline->GetTargetFrameBuffer();
        VkRenderPassBeginInfo render_pass_begin_info = {};
        render_pass_begin_info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass            = render_pass_pipeline->GetRenderPassHandle();
        render_pass_begin_info.framebuffer           = framebuffer->GetHandle();
        render_pass_begin_info.renderArea.offset     = {0, 0};
        render_pass_begin_info.renderArea.extent     = VkExtent2D{framebuffer->GetWidth(), framebuffer->GetHeight()};

        // ToDo : should be move to FrambufferSpecification
        std::array<VkClearValue, 2> clear_values = {};
        clear_values[0].color                    = {{0.1f, 0.1f, 0.1f, 1.0f}};
        clear_values[1].depthStencil             = {1.0f, 0};
        render_pass_begin_info.clearValueCount   = clear_values.size();
        render_pass_begin_info.pClearValues      = clear_values.data();

        vkCmdBeginRenderPass(m_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = framebuffer->GetWidth();
        viewport.height     = framebuffer->GetHeight();
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;

        /*Scissor definition*/
        VkRect2D scissor = {};
        scissor.offset   = {0, 0};
        scissor.extent   = {framebuffer->GetWidth(), framebuffer->GetHeight()};

        vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);
        vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pass_pipeline->GetHandle());

        m_active_render_pass = render_pass;
    }

    void CommandBuffer::EndRenderPass()
    {
        if (auto render_pass = m_active_render_pass.lock())
        {
            ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
            vkCmdEndRenderPass(m_command_buffer);
            m_active_render_pass.reset();
        }
    }

    void CommandBuffer::BindDescriptorSets(uint32_t frame_index)
    {
        if (auto render_pass = m_active_render_pass.lock())
        {
            render_pass->Update();
            auto        render_pass_pipeline = render_pass->GetPipeline();
            auto        pipeline_layout      = render_pass_pipeline->GetPipelineLayout();
            const auto& descriptor_set_map   = render_pass_pipeline->GetShader()->GetDescriptorSetMap();

            std::vector<VkDescriptorSet> frame_set_collection = {};
            for (auto& descriptor_set : descriptor_set_map)
            {
                frame_set_collection.emplace_back(descriptor_set.second.at(frame_index));
            }
            vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, frame_set_collection.size(), frame_set_collection.data(), 0, nullptr);
        }
    }

    void CommandBuffer::DrawIndirect(const Ref<Buffers::IndirectBuffer>& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (buffer->GetNativeBufferHandle())
        {
            vkCmdDrawIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer->GetNativeBufferHandle()), 0, buffer->GetCommandCount(), sizeof(VkDrawIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexedIndirect(const Ref<Buffers::IndirectBuffer>& buffer, uint32_t count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (buffer->GetNativeBufferHandle())
        {
            vkCmdDrawIndexedIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer->GetNativeBufferHandle()), 0, count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }
} // namespace ZEngine::Rendering::Buffers
