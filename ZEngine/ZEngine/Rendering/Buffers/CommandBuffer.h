#pragma once
#include <GraphicBuffer.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/ResourceTypes.h>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
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
        CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool one_time);
        ~CommandBuffer();

        Rendering::QueueType     QueueType;
        Hardwares::VulkanDevice* Device = nullptr;

        void                   Create();
        void                   Free();
        VkCommandBuffer        GetHandle() const;
        void                   Begin();
        void                   End();
        bool                   Completed();
        bool                   IsExecutable();
        bool                   IsRecording();
        CommanBufferState      GetState() const;
        void                   ResetState();
        void                   SetState(const CommanBufferState& state);
        void                   SetSignalFence(const Helpers::Ref<Primitives::Fence>& semaphore);
        void                   SetSignalSemaphore(const Helpers::Ref<Primitives::Semaphore>& semaphore);
        Primitives::Semaphore* GetSignalSemaphore() const;
        Primitives::Fence*     GetSignalFence();
        void                   ClearColor(float r, float g, float b, float a);
        void                   ClearDepth(float depth_color, uint32_t stencil);
        void                   BeginRenderPass(const Helpers::Ref<Renderers::RenderPasses::RenderPass>&, VkFramebuffer framebuffer);
        void                   EndRenderPass();
        void                   BindDescriptorSets(uint32_t frame_index = 0);
        void                   BindDescriptorSet(const VkDescriptorSet& descriptor);
        void                   DrawIndirect(const Buffers::IndirectBuffer& buffer);
        void                   DrawIndexedIndirect(const Buffers::IndirectBuffer& buffer, uint32_t count);
        void                   DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void                   Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance);
        void                   TransitionImageLayout(const Primitives::ImageMemoryBarrier& image_barrier);
        void CopyBufferToImage(const BufferView& source, BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout);
        void BindVertexBuffer(const Buffers::VertexBuffer& buffer);
        void BindIndexBuffer(const Buffers::IndexBuffer& buffer, VkIndexType type);
        void SetScissor(const VkRect2D& scissor);
        void PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data);

    private:
        std::atomic_uint8_t                                   m_command_buffer_state{CommanBufferState::Idle};
        VkCommandBuffer                                       m_command_buffer{VK_NULL_HANDLE};
        VkCommandPool                                         m_command_pool{VK_NULL_HANDLE};
        std::array<VkClearValue, 2>                           m_clear_value{};
        Helpers::Ref<Primitives::Fence>                       m_signal_fence;
        Helpers::Ref<Primitives::Semaphore>                   m_signal_semaphore;
        Helpers::WeakRef<Renderers::RenderPasses::RenderPass> m_active_render_pass;
    };
} // namespace ZEngine::Rendering::Buffers