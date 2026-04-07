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
        uint32_t padding      = 0xFFFFFFFFu;
    };

    struct ImGUIRenderer : public IRenderer
    {

        RenderPasses::RenderPass*        UIPass     = nullptr;
        Hardwares::VertexBufferSetHandle VBHandle   = {};
        Hardwares::IndexBufferSetHandle  IdxBHandle = {};

        void                             Initialize(Hardwares::VulkanDevicePtr device) override;
        void                             Deinitialize() override;

        void                             StyleDarkTheme();

        void                             NewFrame();
        void                             EndFrame();
        void                             PreparePayload(RenderOverlayPayload& payload);
    };

    ZDEFINE_PTR(ImGUIRenderer);
} // namespace ZEngine::Rendering::Renderers
