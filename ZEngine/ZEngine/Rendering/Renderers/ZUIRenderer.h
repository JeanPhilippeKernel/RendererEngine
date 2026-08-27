#pragma once
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::UI
{
    struct ZUIContext;
}

namespace ZEngine::Rendering::Renderers
{
    // ---------------------------------------------------------------
    // ZUIRenderPayload — draw-list backed (replaces ZUIRectInst approach)
    // ---------------------------------------------------------------
    struct ZUIRenderPayload
    {
        UI::ZUIDrawVtx*     Vtx              = nullptr;
        uint32_t            VtxCount         = 0;
        uint16_t*           Idx              = nullptr;
        uint32_t            IdxCount         = 0;
        UI::ZUIDrawListCmd* Cmds             = nullptr;
        uint32_t            CmdCount         = 0;
        float               FramebufferScale = 1.f; // physical / logical scale for scissor
        float               Scale[2]         = {};  // NDC scale  (2/ScreenW, 2/ScreenH)
        float               Translate[2]     = {};  // NDC offset (-1, -1)
    };

    // Push constant shared with zui_draw.vert
    struct ZUIDrawPushConstant
    {
        float    Scale[2]     = {};
        float    Translate[2] = {};
        uint32_t TexIdx       = 0;
        float    FbScale      = 1.f; // matches uFbScale in zui_draw.vert
    };

    struct ZUIRenderer : public IRenderer
    {
        static constexpr uint32_t FRAMES_IN_FLIGHT              = 3;
        static constexpr uint32_t ZUICommandBufferIndex         = 1;

        RenderPasses::RenderPass* DrawPass                      = nullptr; // zui_draw pipeline

        // Per-frame vertex + index buffers
        Core::Memory::BufferView  VtxBHandles[FRAMES_IN_FLIGHT] = {};
        Core::Memory::BufferView  IdxBHandles[FRAMES_IN_FLIGHT] = {};

        void                      Initialize(Hardwares::VulkanDevicePtr device) override;
        void                      Deinitialize() override;

        // Translate the ZUIBox tree into a flat ZUIRenderPayload.
        void                      PreparePayload(UI::ZUIContext* ctx, ZUIRenderPayload* out, Core::Memory::ArenaAllocator* payload_arena);

        // Submit to Vulkan.
        void                      Submit(Hardwares::CommandBuffer* primary_cmd, const ZUIRenderPayload& payload);
    };

    ZDEFINE_PTR(ZUIRenderer);

} // namespace ZEngine::Rendering::Renderers
