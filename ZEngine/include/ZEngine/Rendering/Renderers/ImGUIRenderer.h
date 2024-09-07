#pragma once
#include <Rendering/Buffers/IndexBuffer.h>
#include <Rendering/Buffers/VertexBuffer.h>
#include <Rendering/Renderers/RenderGraph.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Swapchain.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers
{
    struct ImGUIRenderer : public Helpers::RefCounted
    {
        void Initialize(RenderGraph* const graph);
        void Deinitialize();

        void StyleDarkTheme();

        void BeginFrame(Rendering::Buffers::CommandBuffer* const command_buffer);
        void Draw(Rendering::Buffers::CommandBuffer* const commandbuffer, uint32_t frame_index);
        void EndFrame(Rendering::Buffers::CommandBuffer* const command_buffer, uint32_t frame_index);

        VkDescriptorSet UpdateFrameOutput(const Hardwares::BufferImage& buffer);

    private:
        VkDescriptorSet               m_frame_output{VK_NULL_HANDLE};
        VkDescriptorSet               m_font_descriptor_set{VK_NULL_HANDLE};
        Ref<Buffers::VertexBufferSet> m_vertex_buffer;
        Ref<Buffers::IndexBufferSet>  m_index_buffer;
        Ref<RenderPasses::RenderPass> m_ui_pass;
    };

} // namespace ZEngine::Rendering::Renderers
