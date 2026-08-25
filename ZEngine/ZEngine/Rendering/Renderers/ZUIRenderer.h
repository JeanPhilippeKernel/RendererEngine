#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::UI { struct ZUIContext; }

namespace ZEngine::Rendering::Renderers
{
    struct ZUIDrawCmd
    {
        uint32_t IndexOffset;
        uint32_t IndexCount;
        int32_t  VertexOffset;
        uint32_t TextureIndex; // bindless slot; 0xFFFFFFFF = solid color, no sample
        float    ClipX;
        float    ClipY;
        float    ClipW;
        float    ClipH;
    };

    struct ZUIRenderPayload
    {
        UIDrawVert* Vertices      = nullptr; // frame-arena allocated
        uint32_t*   Indices       = nullptr; // frame-arena allocated
        ZUIDrawCmd* Cmds          = nullptr; // frame-arena allocated
        uint32_t    VertexCount   = 0;
        uint32_t    IndexCount    = 0;
        uint32_t    CmdCount      = 0;
        float       Scale[2]      = {};      // push constant: {2/fb_w, 2/fb_h}
        float       Translate[2]  = {};      // push constant: {-1, -1}
    };

    // Push constant layout shared between ZUIRenderer and zui.vert / zui.frag
    struct ZUIPushConstant
    {
        float    Scale[2]     = {};
        float    Translate[2] = {};
        uint32_t TextureId    = 0xFFFFFFFFu;
        uint32_t Padding      = 0u;
    };

    struct ZUIRenderer : public IRenderer
    {
        static constexpr uint32_t FRAMES_IN_FLIGHT     = 3;
        static constexpr uint32_t ZUICommandBufferIndex = 2; // slot above ImGui (1)

        RenderPasses::RenderPass* UIPass                       = nullptr;
        Core::Memory::BufferView  VBHandles[FRAMES_IN_FLIGHT]  = {};
        Core::Memory::BufferView  IdxBHandles[FRAMES_IN_FLIGHT]= {};

        void Initialize(Hardwares::VulkanDevicePtr device) override;
        void Deinitialize() override;

        // Walk the box tree and emit quads. Output arrays are allocated from
        // payload_arena (a per-mailbox-slot sub-arena) so they survive until the
        // render thread consumes the payload — not the shorter-lived FrameArena.
        void PreparePayload(UI::ZUIContext* ctx, ZUIRenderPayload* out, Core::Memory::ArenaAllocator* payload_arena);

        // Upload vertex/index data and record draw calls into a secondary command buffer.
        // primary_cmd must be the current frame's active primary command buffer.
        void Submit(Hardwares::CommandBuffer* primary_cmd, const ZUIRenderPayload& payload);
    };

    ZDEFINE_PTR(ZUIRenderer);
} // namespace ZEngine::Rendering::Renderers
