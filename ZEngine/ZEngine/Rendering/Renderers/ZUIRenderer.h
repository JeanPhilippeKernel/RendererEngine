#pragma once
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::UI   { struct ZUIContext; }

namespace ZEngine::Rendering::Renderers
{
    // ---------------------------------------------------------------
    // ZUIRectInst — one instanced rect (RAD Debugger R_Rect2DInst approach).
    // 128 bytes. Matches zui_rect.vert attribute layout exactly.
    //
    // Corner order for colors[] and corner_radii[]:
    //   [0]=TL  [1]=TR  [2]=BL  [3]=BR
    // ---------------------------------------------------------------
    struct ZUIRectInst
    {
        float dst[4];           // x0,y0,x1,y1  screen pixels
        float src[4];           // u0,v0,u1,v1  atlas UV
        float colors[4][4];     // per-corner RGBA  [corner_idx][channel]
        float corner_radii[4];  // per-corner radius
        float style[4];         // [0]=border_thickness [1]=edge_softness
                                // [2]=tex_index (float-cast) [3]=shear
    };
    static_assert(sizeof(ZUIRectInst) == 128, "ZUIRectInst size mismatch");

    // ---------------------------------------------------------------
    // Draw command — a scissored batch of instances.
    // Texture index lives inside ZUIRectInst::style[2], not here.
    // Only clips trigger new commands.
    // ---------------------------------------------------------------
    struct ZUIDrawCmd
    {
        uint32_t InstOffset = 0;
        uint32_t InstCount  = 0;
        float    ClipX      = 0.f;
        float    ClipY      = 0.f;
        float    ClipW      = 0.f;
        float    ClipH      = 0.f;
    };

    struct ZUIRenderPayload
    {
        ZUIRectInst* Instances   = nullptr;
        uint32_t     InstCount   = 0;
        ZUIDrawCmd*  Cmds        = nullptr;
        uint32_t     CmdCount    = 0;
        float        Scale[2]    = {};
        float        Translate[2]= {};
    };

    // Push constant layout shared with zui_rect.vert
    struct ZUIRectPushConstant
    {
        float Scale[2]     = {};
        float Translate[2] = {};
    };

    struct ZUIRenderer : public IRenderer
    {
        static constexpr uint32_t FRAMES_IN_FLIGHT      = 3;
        static constexpr uint32_t ZUICommandBufferIndex = 1;

        RenderPasses::RenderPass* UIPass                        = nullptr;
        Core::Memory::BufferView  InstBHandles[FRAMES_IN_FLIGHT]= {};

        void Initialize  (Hardwares::VulkanDevicePtr device) override;
        void Deinitialize() override;

        // Build ZUIRectInst stream from the box tree.
        void PreparePayload(UI::ZUIContext* ctx,
                            ZUIRenderPayload* out,
                            Core::Memory::ArenaAllocator* payload_arena);

        // Submit to Vulkan.
        void Submit(Hardwares::CommandBuffer* primary_cmd,
                    const ZUIRenderPayload& payload);
    };

    ZDEFINE_PTR(ZUIRenderer);

} // namespace ZEngine::Rendering::Renderers
