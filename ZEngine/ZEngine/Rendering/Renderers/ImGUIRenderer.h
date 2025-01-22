#pragma once
#include <RenderGraph.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer;
    struct ImGUIRenderer : public Helpers::RefCounted
    {
        void            Initialize(GraphicRenderer* renderer);
        void            Deinitialize();

        void            StyleDarkTheme();

        void            NewFrame();
        void            DrawFrame(uint32_t frame_index, Hardwares::CommandBuffer* const command_buffer);

        VkDescriptorSet UpdateFrameOutput(const Textures::TextureHandle& handle);
        VkDescriptorSet UpdateFileIconOutput(const Textures::TextureHandle& handle);
        VkDescriptorSet UpdateDirIconOutput(const Textures::TextureHandle& handle);

    private:
        VkDescriptorSet                        m_frame_output{VK_NULL_HANDLE};
        VkDescriptorSet                        m_font_descriptor_set{VK_NULL_HANDLE};
        VkDescriptorSet                        m_folder_output{VK_NULL_HANDLE};
        VkDescriptorSet                        m_file_output{VK_NULL_HANDLE};
        GraphicRenderer*                       m_renderer;
        Hardwares::VertexBufferSetHandle       m_vertex_buffer_handle;
        Hardwares::IndexBufferSetHandle        m_index_buffer_handle;
        Helpers::Ref<RenderPasses::RenderPass> m_ui_pass;
    };

} // namespace ZEngine::Rendering::Renderers
