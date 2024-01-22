#include <pch.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/ResourceTypes.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/IndirectBuffer.h>
#include <Rendering/Buffers/VertexBuffer.h>
#include <Rendering/Buffers/IndexBuffer.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>

#include <Engine.h>

namespace ZEngine::Rendering::Buffers
{
    CommandBuffer::CommandBuffer(VkCommandPool command_pool, Rendering::QueueType type, bool one_time)
    {
        m_queue_type     = type;
        m_command_pool   = command_pool;
        m_one_time_usage = one_time;

        ZENGINE_VALIDATE_ASSERT(m_command_pool != VK_NULL_HANDLE, "Command Pool cannot be null")

        auto                        device                         = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        VkCommandBufferAllocateInfo command_buffer_allocation_info = {};
        command_buffer_allocation_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocation_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocation_info.commandBufferCount          = 1;
        command_buffer_allocation_info.commandPool                 = m_command_pool;

        ZENGINE_VALIDATE_ASSERT(vkAllocateCommandBuffers(device, &command_buffer_allocation_info, &m_command_buffer) == VK_SUCCESS, "Failed to allocate command buffer!")
        m_command_buffer_state = CommanBufferState::Idle;
    }

    CommandBuffer::~CommandBuffer()
    {
        if (m_command_pool && m_command_buffer)
        {
            VkCommandBuffer buffers[] = {m_command_buffer};
            auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
            vkFreeCommandBuffers(device, m_command_pool, 1, buffers);
        }
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
        command_buffer_begin_info.flags |= (m_one_time_usage) ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        m_command_buffer_state = CommanBufferState::Recording;
    }

    void CommandBuffer::End()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Recording, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(m_command_buffer) == VK_SUCCESS, "Failed to end recording command buffer!")

        m_command_buffer_state = CommanBufferState::Executable;
    }

    bool CommandBuffer::Completed()
    {
        return m_signal_fence ? m_signal_fence->IsSignaled() : false;
    }

    bool CommandBuffer::IsExecutable()
    {
        return m_command_buffer_state == CommanBufferState::Executable;
    }

    bool CommandBuffer::IsRecording()
    {
        return m_command_buffer_state == CommanBufferState::Recording;
    }

    void CommandBuffer::Submit(bool as_instant_command)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Executable, "command buffer must be in ended state")
        Hardwares::VulkanDevice::QueueSubmit(m_queue_type, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, *this, as_instant_command);
    }

    CommanBufferState CommandBuffer::GetState() const
    {
        return CommanBufferState{m_command_buffer_state.load()};
    }

    void CommandBuffer::ResetState()
    {
        if (!m_one_time_usage)
        {
            vkResetCommandBuffer(m_command_buffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            m_command_buffer_state = CommanBufferState::Idle;
            m_signal_fence         = {};
            m_signal_semaphore     = {};
        }
    }

    void CommandBuffer::SetState(const CommanBufferState& state)
    {
        m_command_buffer_state = state;
    }

    Primitives::Semaphore* CommandBuffer::GetSignalSemaphore() const
    {
        return m_signal_semaphore.get();
    }

    void CommandBuffer::SetSignalFence(const Ref<Primitives::Fence>& semaphore)
    {
        m_signal_fence = semaphore;
    }

    void CommandBuffer::SetSignalSemaphore(const Ref<Primitives::Semaphore>& semaphore)
    {
        m_signal_semaphore = semaphore;
    }

    Primitives::Fence* CommandBuffer::GetSignalFence()
    {
        return m_signal_fence.get();
    }

    void CommandBuffer::ClearColor(float r, float g, float b, float a)
    {
        m_clear_value[0].color = {r, g, b, a};
    }

    void CommandBuffer::ClearDepth(float depth_color, uint32_t stencil)
    {
        m_clear_value[1].depthStencil.depth = depth_color;
        m_clear_value[1].depthStencil.stencil = stencil;
    }

    void CommandBuffer::BeginRenderPass(const Ref<Renderers::RenderPasses::RenderPass>& render_pass)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        const auto&    render_pass_spec     = render_pass->GetSpecification();
        auto           render_pass_pipeline = render_pass->GetPipeline();
        const uint32_t width                = render_pass->GetRenderAreaWidth();
        const uint32_t height               = render_pass->GetRenderAreaHeight();

        std::vector<VkClearValue> clear_values = {};

        if (render_pass_spec.SwapchainAsRenderTarget)
        {
            clear_values.push_back(m_clear_value[0]);
        }
        else
        {
            auto& spec = render_pass->GetSpecification();
            for (const auto& render_target : spec.Inputs)
            {
                if (render_target->IsDepthTexture())
                {
                    clear_values.push_back(m_clear_value[1]);
                    continue;
                }
                clear_values.push_back(m_clear_value[0]);
            }
            for (const auto& render_target : spec.ExternalOutputs)
            {
                if (render_target->IsDepthTexture())
                {
                    clear_values.push_back(m_clear_value[1]);
                    continue;
                }
                clear_values.push_back(m_clear_value[0]);
            }
        }

        VkRenderPassBeginInfo render_pass_begin_info = {};
        render_pass_begin_info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass            = render_pass->GetAttachment()->GetHandle();
        render_pass_begin_info.framebuffer           = render_pass->GetFramebuffer();
        render_pass_begin_info.renderArea.offset     = {0, 0};
        render_pass_begin_info.renderArea.extent     = VkExtent2D{width, height};
        render_pass_begin_info.clearValueCount       = clear_values.size();
        render_pass_begin_info.pClearValues          = clear_values.data();

        vkCmdBeginRenderPass(m_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = width;
        viewport.height     = height;
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;
        vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);

        /*Scissor definition*/
        VkRect2D scissor = {};
        scissor.offset   = {0, 0};
        scissor.extent   = {width, height};
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

    void CommandBuffer::BindDescriptorSet(const VkDescriptorSet& descriptor)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(descriptor != nullptr, "DescriptorSet can't be null")
        if (auto render_pass = m_active_render_pass.lock())
        {
            auto render_pass_pipeline = render_pass->GetPipeline();
            auto pipeline_layout = render_pass_pipeline->GetPipelineLayout();
            VkDescriptorSet desc_set[1]          = {descriptor};
            vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, desc_set, 0, nullptr);
        }
    }

    void CommandBuffer::DrawIndirect(const Buffers::IndirectBuffer& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.GetNativeBufferHandle())
        {
            vkCmdDrawIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, buffer.GetCommandCount(), sizeof(VkDrawIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexedIndirect(const Buffers::IndirectBuffer& buffer, uint32_t count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (buffer.GetNativeBufferHandle())
        {
            vkCmdDrawIndexedIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexed(uint32_t index_count, uint32_t instanceCount, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        vkCmdDrawIndexed(m_command_buffer, index_count, instanceCount, first_index, vertex_offset, first_instance);
    }

    void CommandBuffer::TransitionImageLayout(const Primitives::ImageMemoryBarrier& image_barrier)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        const auto& barrier_handle = image_barrier.GetHandle();
        const auto& barrier_spec   = image_barrier.GetSpecification();
        vkCmdPipelineBarrier(m_command_buffer, barrier_spec.SourceStageMask, barrier_spec.DestinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier_handle);
    }

    void CommandBuffer::CopyBufferToImage(
        const Hardwares::BufferView& source,
        Hardwares::BufferImage&      destination,
        uint32_t                     width,
        uint32_t                     height,
        uint32_t                     layer_count,
        VkImageLayout                new_layout)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        VkBufferImageCopy buffer_image_copy               = {};
        buffer_image_copy.bufferOffset                    = 0;
        buffer_image_copy.bufferRowLength                 = 0;
        buffer_image_copy.bufferImageHeight               = 0;
        buffer_image_copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        buffer_image_copy.imageSubresource.mipLevel       = 0;
        buffer_image_copy.imageSubresource.baseArrayLayer = 0;
        buffer_image_copy.imageSubresource.layerCount     = layer_count;
        buffer_image_copy.imageOffset                     = {0, 0, 0};
        buffer_image_copy.imageExtent                     = {width, height, 1};

        vkCmdCopyBufferToImage(m_command_buffer, source.Handle, destination.Handle, new_layout, 1, &buffer_image_copy);
    }

    void CommandBuffer::BindVertexBuffer(const Buffers::VertexBuffer& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (buffer.GetNativeBufferHandle())
        {
            VkDeviceSize vertex_offset[1]  = {0};
            VkBuffer     vertex_buffers[1] = {reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle())};
            vkCmdBindVertexBuffers(m_command_buffer, 0, 1, vertex_buffers, vertex_offset);
        }
    }

    void CommandBuffer::BindIndexBuffer(const Buffers::IndexBuffer& buffer, VkIndexType type)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.GetNativeBufferHandle())
        {
            vkCmdBindIndexBuffer(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, type);
        }
    }

    void CommandBuffer::SetScissor(const VkRect2D& scissor)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);
    }

    void CommandBuffer::PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (auto render_pass = m_active_render_pass.lock())
        {
            auto render_pass_pipeline = render_pass->GetPipeline();
            auto pipeline_layout      = render_pass_pipeline->GetPipelineLayout();
            vkCmdPushConstants(m_command_buffer, pipeline_layout, stage_flags, offset, size, data);
        }
    }
} // namespace ZEngine::Rendering::Buffers
