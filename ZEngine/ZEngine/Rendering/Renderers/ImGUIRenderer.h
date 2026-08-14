#pragma once
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>

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

        static constexpr uint32_t          FRAMES_IN_FLIGHT              = 3;
        RenderPasses::RenderPass*          UIPass                        = nullptr;
        Core::Memory::BufferView           VBHandles[FRAMES_IN_FLIGHT]   = {};
        Core::Memory::BufferView           IdxBHandles[FRAMES_IN_FLIGHT] = {};
        Rendering::Textures::TextureHandle FontTexture                   = {};

        void                               Initialize(Hardwares::VulkanDevicePtr device) override;
        void                               Deinitialize() override;

        void                               StyleDarkTheme();

        void                               NewFrame();
        void                               EndFrame();
        void                               PreparePayload(RenderOverlayPayload& payload);
    };

    ZDEFINE_PTR(ImGUIRenderer);
} // namespace ZEngine::Rendering::Renderers
