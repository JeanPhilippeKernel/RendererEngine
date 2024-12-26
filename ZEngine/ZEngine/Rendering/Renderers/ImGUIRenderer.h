#pragma once
#include <RenderGraph.h>
#include <Rendering/Buffers/IndexBuffer.h>
#include <Rendering/Buffers/VertexBuffer.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer;
    struct ImGUIRenderer : public Helpers::RefCounted
    {
        void Initialize(GraphicRenderer* renderer);
        void Deinitialize();

        void StyleDarkTheme();

        void BeginFrame();
        void EndFrame(Rendering::Buffers::CommandBuffer* const command_buffer, uint32_t frame_index);

        VkDescriptorSet UpdateFrameOutput(const Textures::TextureHandle& handle);

    private:
        VkDescriptorSet                        m_frame_output{VK_NULL_HANDLE};
        VkDescriptorSet                        m_font_descriptor_set{VK_NULL_HANDLE};
        GraphicRenderer*                       m_renderer;
        Buffers::VertexBufferSetHandle         m_vertex_buffer_handle;
        Buffers::IndexBufferSetHandle          m_index_buffer_handle;
        Helpers::Ref<RenderPasses::RenderPass> m_ui_pass;
    };

} // namespace ZEngine::Rendering::Renderers
