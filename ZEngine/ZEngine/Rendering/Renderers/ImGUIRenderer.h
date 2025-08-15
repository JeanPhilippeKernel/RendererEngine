#pragma once
#include <IRenderer.h>
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

    struct ImGUIRenderer : public IRenderer
    {
        void Initialize(Hardwares::VulkanDevicePtr device) override;
        void Deinitialize() override;

        void StyleDarkTheme();

        void NewFrame();
        void DrawFrame(Hardwares::CommandBuffer* const command_buffer);

    private:
        Hardwares::VertexBufferSetHandle m_vertex_buffer_handle;
        Hardwares::IndexBufferSetHandle  m_index_buffer_handle;
        RenderPasses::RenderPass*        m_ui_pass;
    };

    ZDEFINE_PTR(ImGUIRenderer);
} // namespace ZEngine::Rendering::Renderers
