#pragma once
#include <RenderGraph.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers
{
    struct PushConstantData
    {
        float    Scale[2]     = {0};
        float    Translate[2] = {0};
        uint32_t TextureId    = 0xFFFFFFFFu;
    };

    struct GraphicRenderer;
    struct ImGUIRenderer
    {
        Hardwares::VulkanDevicePtr Device = nullptr;

        void                       Initialize(Hardwares::VulkanDevicePtr device, RenderPasses::RenderPassBuilder* pass_builder);
        void                       Deinitialize();

        void                       StyleDarkTheme();

        void                       NewFrame();
        void                       DrawFrame(uint32_t frame_index, Hardwares::CommandBuffer* const command_buffer);

    private:
        Hardwares::VertexBufferSetHandle m_vertex_buffer_handle;
        Hardwares::IndexBufferSetHandle  m_index_buffer_handle;
        RenderPasses::RenderPass*        m_ui_pass;
    };

    ZDEFINE_PTR(ImGUIRenderer);
} // namespace ZEngine::Rendering::Renderers
