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
    struct ImGUIRenderer : public Helpers::RefCounted
    {
        void Initialize(GraphicRenderer* renderer);
        void Deinitialize();

        void StyleDarkTheme();

        void NewFrame();
        void DrawFrame(uint32_t frame_index, Hardwares::CommandBuffer* const command_buffer);

    private:
        GraphicRenderer*                       m_renderer;
        Hardwares::VertexBufferSetHandle       m_vertex_buffer_handle;
        Hardwares::IndexBufferSetHandle        m_index_buffer_handle;
        Helpers::Ref<RenderPasses::RenderPass> m_ui_pass;
    };

} // namespace ZEngine::Rendering::Renderers
