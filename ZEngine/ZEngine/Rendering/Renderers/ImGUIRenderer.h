#pragma once
#include <GraphicRenderer.h>
#include <Rendering/Buffers/IndexBuffer.h>
#include <Rendering/Buffers/VertexBuffer.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers
{
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
        Helpers::Ref<Buffers::VertexBufferSet> m_vertex_buffer;
        Helpers::Ref<Buffers::IndexBufferSet>  m_index_buffer;
        Helpers::Ref<RenderPasses::RenderPass> m_ui_pass;
        Helpers::Ref<Textures::Texture>        m_font_texture;
    };

} // namespace ZEngine::Rendering::Renderers
