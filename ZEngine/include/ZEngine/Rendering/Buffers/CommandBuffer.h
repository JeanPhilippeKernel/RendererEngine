#pragma once
#include <array>
#include <atomic>
#include <vulkan/vulkan.h>
#include <ZEngineDef.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/ResourceTypes.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
}

namespace ZEngine::Hardwares
{
    struct BufferView;
    struct BufferImage;
}

namespace ZEngine::Rendering::Buffers
{
    class IndirectBuffer;
    class VertexBuffer;
    class IndexBuffer;

    enum CommanBufferState : uint8_t
    {
        Idle = 0,
        Recording,
        Executable,
        Pending,
        Invalid
    };

    struct CommandBuffer : public Helpers::RefCounted
    {
        CommandBuffer(VkCommandPool command_pool, Rendering::QueueType type, bool one_time);
        ~CommandBuffer();

        VkCommandBuffer   GetHandle() const;
        void              Begin();
        void              End();
        bool              Completed();
        bool              IsExecutable();
        bool              IsRecording();
        void              Submit(bool as_instant_command = false);
        CommanBufferState GetState() const;
        void              ResetState();
        void              SetState(const CommanBufferState& state);

        void                   SetSignalFence(const Ref<Primitives::Fence>& semaphore);
        void                   SetSignalSemaphore(const Ref<Primitives::Semaphore>& semaphore);
        Primitives::Semaphore* GetSignalSemaphore() const;
        Primitives::Fence*     GetSignalFence();

        void ClearColor(float r, float g, float b, float a);
        void ClearDepth(float depth_color, uint32_t stencil);

        void BeginRenderPass(const Ref<Renderers::RenderPasses::RenderPass>&);
        void EndRenderPass();
        void BindDescriptorSets(uint32_t frame_index = 0);
        void BindDescriptorSet(const VkDescriptorSet& descriptor);
        void DrawIndirect(const Buffers::IndirectBuffer& buffer);
        void DrawIndexedIndirect(const Buffers::IndirectBuffer& buffer, uint32_t count);
        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);

        void TransitionImageLayout(const Primitives::ImageMemoryBarrier& image_barrier);

        void CopyBufferToImage(
            const Hardwares::BufferView& source,
            Hardwares::BufferImage&      destination,
            uint32_t                     width,
            uint32_t                     height,
            uint32_t                     layer_count,
            VkImageLayout                new_layout);

        void BindVertexBuffer(const Buffers::VertexBuffer& buffer);
        void BindIndexBuffer(const Buffers::IndexBuffer& buffer, VkIndexType type);
        void SetScissor(const VkRect2D& scissor);

        void PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data);

    private:
        bool                                         m_one_time_usage{false};
        std::atomic_uint8_t                          m_command_buffer_state{CommanBufferState::Idle};
        VkCommandBuffer                              m_command_buffer{VK_NULL_HANDLE};
        VkCommandPool                                m_command_pool{VK_NULL_HANDLE};
        std::array<VkClearValue, 2>                  m_clear_value{};
        Rendering::QueueType                         m_queue_type;
        Ref<Primitives::Fence>                       m_signal_fence;
        Ref<Primitives::Semaphore>                   m_signal_semaphore;
        WeakRef<Renderers::RenderPasses::RenderPass> m_active_render_pass;
    };
}