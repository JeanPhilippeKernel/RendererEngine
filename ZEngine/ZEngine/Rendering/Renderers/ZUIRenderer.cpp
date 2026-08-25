#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/ZUIRenderer.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIFont.h>
#include <cmath>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::UI;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    // ---------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------

    static uint32_t PackRGBA(const float c[4])
    {
        auto clamp8 = [](float v) -> uint32_t
        {
            float clamped = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            return (uint32_t)(clamped * 255.f + 0.5f);
        };
        return clamp8(c[0]) | (clamp8(c[1]) << 8) | (clamp8(c[2]) << 16) | (clamp8(c[3]) << 24);
    }

    static void EmitQuad(
        UIDrawVert* verts, uint32_t* idxs,
        uint32_t&   vert_count, uint32_t& idx_count,
        float x0, float y0, float x1, float y1,
        float u0, float v0, float u1, float v1,
        uint32_t color)
    {
        uint32_t base = vert_count;

        verts[vert_count].pos.x = x0; verts[vert_count].pos.y = y0;
        verts[vert_count].uv.x  = u0; verts[vert_count].uv.y  = v0;
        verts[vert_count].col   = color;
        ++vert_count;

        verts[vert_count].pos.x = x1; verts[vert_count].pos.y = y0;
        verts[vert_count].uv.x  = u1; verts[vert_count].uv.y  = v0;
        verts[vert_count].col   = color;
        ++vert_count;

        verts[vert_count].pos.x = x1; verts[vert_count].pos.y = y1;
        verts[vert_count].uv.x  = u1; verts[vert_count].uv.y  = v1;
        verts[vert_count].col   = color;
        ++vert_count;

        verts[vert_count].pos.x = x0; verts[vert_count].pos.y = y1;
        verts[vert_count].uv.x  = u0; verts[vert_count].uv.y  = v1;
        verts[vert_count].col   = color;
        ++vert_count;

        idxs[idx_count++] = base + 0;
        idxs[idx_count++] = base + 1;
        idxs[idx_count++] = base + 2;
        idxs[idx_count++] = base + 0;
        idxs[idx_count++] = base + 2;
        idxs[idx_count++] = base + 3;
    }

    // Start a new draw command if the texture index changed or no command is open.
    static void FlushAndBeginCmd(
        ZUIDrawCmd* cmds, uint32_t& cmd_count,
        uint32_t    new_tex, float clip_x, float clip_y, float clip_w, float clip_h,
        uint32_t    current_vert_count, uint32_t current_idx_count)
    {
        // close previous cmd by recording its final index count
        if (cmd_count > 0)
        {
            ZUIDrawCmd& prev = cmds[cmd_count - 1];
            prev.IndexCount = current_idx_count - prev.IndexOffset;
        }

        ZUIDrawCmd& cmd  = cmds[cmd_count++];
        cmd.IndexOffset  = current_idx_count;
        cmd.IndexCount   = 0; // filled when next cmd is opened or at end
        cmd.VertexOffset = (int32_t)current_vert_count;
        cmd.TextureIndex = new_tex;
        cmd.ClipX        = clip_x;
        cmd.ClipY        = clip_y;
        cmd.ClipW        = clip_w;
        cmd.ClipH        = clip_h;
    }

    // ---------------------------------------------------------------
    // Initialize / Deinitialize
    // ---------------------------------------------------------------

    void ZUIRenderer::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device      = device;
        RenderGraph = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        RenderGraph->Initialize(Device);

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            VBHandles[i]  = Device->GpuMem.AllocateBuffer(
                ZMega(2), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                GpuMemoryDomain::HostUniform, "ZUIVertexBuffer");
            IdxBHandles[i] = Device->GpuMem.AllocateBuffer(
                ZMega(1), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                GpuMemoryDomain::HostUniform, "ZUIIndexBuffer");
        }

        auto pass_builder = RenderGraph->RenderPassBuilder;
        pass_builder->SetName("ZUI Pass")
            .SetPipelineName("ZUI-Pipeline")
            .EnablePipelineBlending(true)
            .SetInputBindingCount(1)
            .SetStride(0, sizeof(UIDrawVert))
            .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)

            .SetInputAttributeCount(3)
            .SetLocation(0, 0)
            .SetBinding(0, 0)
            .SetFormat(0, ImageFormat::R32G32_SFLOAT)
            .SetOffset(0, offsetof(UIDrawVert, pos))
            .SetLocation(1, 1)
            .SetBinding(1, 0)
            .SetFormat(1, ImageFormat::R32G32_SFLOAT)
            .SetOffset(1, offsetof(UIDrawVert, uv))
            .SetLocation(2, 2)
            .SetBinding(2, 0)
            .SetFormat(2, ImageFormat::R8G8B8A8_UNORM)
            .SetOffset(2, offsetof(UIDrawVert, col))

            .UseShader("zui")
            .UseSwapchainAsRenderTarget();

        UIPass = Device->CreateRenderPass(pass_builder->Detach());
        UIPass->UseTextureArray("TextureArray");
        UIPass->SetSampler("LinearClampSampler", Device->GlobalLinearClampToEdgeSamplerImageInfo);
        UIPass->Verify();
        UIPass->Bake();
    }

    void ZUIRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        UIPass->Dispose();

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            if (VBHandles[i])  { Device->GpuMem.FreeBuffer(VBHandles[i]); }
            if (IdxBHandles[i]) { Device->GpuMem.FreeBuffer(IdxBHandles[i]); }
        }
    }

    // ---------------------------------------------------------------
    // PreparePayload
    // ---------------------------------------------------------------

    void ZUIRenderer::PreparePayload(UI::ZUIContext* ctx, ZUIRenderPayload* out)
    {
        if (!ctx || !ctx->Root || !out) { return; }

        uint32_t max     = ctx->MaxBoxesPerFrame;
        float    fb_w    = (float)Device->SwapchainPtr->SwapchainImageWidth;
        float    fb_h    = (float)Device->SwapchainPtr->SwapchainImageHeight;

        // Pre-allocate output arrays in the frame arena
        uint32_t max_verts = 65536;
        uint32_t max_idxs  = max_verts * 3 / 2; // 6 idxs per quad, 4 verts = 1.5 ratio
        uint32_t max_cmds  = max * 2;

        out->Vertices     = ZPushArray(&ctx->FrameArena, UIDrawVert, max_verts);
        out->Indices      = ZPushArray(&ctx->FrameArena, uint32_t,   max_idxs);
        out->Cmds         = ZPushArray(&ctx->FrameArena, ZUIDrawCmd, max_cmds);
        out->VertexCount  = 0;
        out->IndexCount   = 0;
        out->CmdCount     = 0;

        out->Scale[0]     =  2.f / fb_w;
        out->Scale[1]     =  2.f / fb_h;
        out->Translate[0] = -1.f;
        out->Translate[1] = -1.f;

        // Collect boxes in pre-order
        ZUIBox** nodes     = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        ZUIBox** dfs_stack = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        uint32_t node_count = 0;
        uint32_t stack_top  = 0;

        dfs_stack[stack_top++] = ctx->Root;
        while (stack_top > 0 && node_count < max)
        {
            ZUIBox* box      = dfs_stack[--stack_top];
            nodes[node_count++] = box;
            for (ZUIBox* c = box->LastChild; c; c = c->PrevSib)
            {
                if (stack_top < max) { dfs_stack[stack_top++] = c; }
            }
        }

        uint32_t current_tex = 0xFFFFFFFFu; // start invalid so first box opens a cmd

        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box = nodes[i];

            float bx0 = box->ScreenMin[0];
            float by0 = box->ScreenMin[1];
            float bx1 = box->ScreenMax[0];
            float by1 = box->ScreenMax[1];

            // Skip zero-size boxes
            if (bx1 <= bx0 || by1 <= by0) { continue; }

            // --- Background ---
            if ((box->Flags & UI::ZUI_DrawBackground) && box->BgColor[3] > 0.f)
            {
                constexpr uint32_t SOLID_TEX = 0xFFFFFFFFu;
                if (current_tex != SOLID_TEX || out->CmdCount == 0)
                {
                    FlushAndBeginCmd(out->Cmds, out->CmdCount, SOLID_TEX,
                                     0.f, 0.f, fb_w, fb_h,
                                     out->VertexCount, out->IndexCount);
                    current_tex = SOLID_TEX;
                }
                EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                         bx0, by0, bx1, by1, 0.f, 0.f, 0.f, 0.f,
                         PackRGBA(box->BgColor));
            }

            // --- Text ---
            if ((box->Flags & UI::ZUI_DrawText) && box->Label.Ptr && ctx->Font)
            {
                const UI::ZUIFont* font    = ctx->Font;
                uint32_t           font_tex = font->AtlasHandle.Index;

                if (current_tex != font_tex || out->CmdCount == 0)
                {
                    FlushAndBeginCmd(out->Cmds, out->CmdCount, font_tex,
                                     bx0, by0, bx1 - bx0, by1 - by0,
                                     out->VertexCount, out->IndexCount);
                    current_tex = font_tex;
                }

                float cx       = bx0;
                float baseline = by0 + font->Ascent;
                uint32_t color = PackRGBA(box->TextColor);

                for (uint32_t ci = 0; ci < box->Label.Len; ++ci)
                {
                    uint32_t cp  = (uint8_t)box->Label.Ptr[ci];
                    uint32_t idx = cp - font->FirstCodepoint;
                    if (cp < font->FirstCodepoint || idx >= font->GlyphCount) { continue; }

                    const UI::ZUIGlyph& g = font->Glyphs[idx];

                    float gx0 = cx + g.OffsetX;
                    float gy0 = baseline + g.OffsetY;
                    float gx1 = gx0 + g.Width;
                    float gy1 = gy0 + g.Height;

                    EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                             gx0, gy0, gx1, gy1,
                             g.U0, g.V0, g.U1, g.V1,
                             color);

                    cx += g.AdvanceX;
                }
            }
        }

        // Close the last open command
        if (out->CmdCount > 0)
        {
            out->Cmds[out->CmdCount - 1].IndexCount = out->IndexCount - out->Cmds[out->CmdCount - 1].IndexOffset;
        }
    }

    // ---------------------------------------------------------------
    // Submit
    // ---------------------------------------------------------------

    void ZUIRenderer::Submit(Hardwares::CommandBuffer* primary_cmd, const ZUIRenderPayload& payload)
    {
        if (payload.VertexCount == 0 || payload.CmdCount == 0) { return; }

        auto swapchain        = Device->SwapchainPtr;
        auto frame_index      = swapchain->CurrentFrame->Index;
        auto current_fb       = swapchain->SwapchainFramebuffers[swapchain->CurrentFrame->ImageIndex];

        uint32_t fi           = frame_index % FRAMES_IN_FLIGHT;
        auto     vb           = VBHandles[fi];
        auto     ib           = IdxBHandles[fi];

        primary_cmd->BeginRenderPass(UIPass, current_fb, true);
        {
            auto* rrm = Device->RRM ? reinterpret_cast<RenderResourceManager*>(Device->RRM) : nullptr;
            if (rrm)
            {
                rrm->UpdateBuffer(vb, payload.Vertices, payload.VertexCount * sizeof(UIDrawVert));
                rrm->UpdateBuffer(ib, payload.Indices,  payload.IndexCount  * sizeof(uint32_t));
            }

            auto secondary_cb = Device->CommandBufferMgr->GetCommandBuffer(
                Rendering::QueueType::GRAPHIC_QUEUE, frame_index, 0, ZUICommandBufferIndex, false);

            secondary_cb->ResetState();
            secondary_cb->BeginSecondary(UIPass, current_fb);
            secondary_cb->SetViewport(UIPass->GetRenderAreaWidth(), UIPass->GetRenderAreaHeight());
            secondary_cb->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, UIPass->Pipeline);
            secondary_cb->BindVertexBuffer(vb);
            secondary_cb->BindIndexBuffer(ib, VK_INDEX_TYPE_UINT32);

            ZUIPushConstant  pc      = {};
            pc.Scale[0]              = payload.Scale[0];
            pc.Scale[1]              = payload.Scale[1];
            pc.Translate[0]          = payload.Translate[0];
            pc.Translate[1]          = payload.Translate[1];

            for (uint32_t i = 0; i < payload.CmdCount; ++i)
            {
                const ZUIDrawCmd& cmd = payload.Cmds[i];
                if (cmd.IndexCount == 0) { continue; }

                secondary_cb->SetScissor(
                    (uint32_t)cmd.ClipW, (uint32_t)cmd.ClipH,
                    (int32_t)cmd.ClipX,  (int32_t)cmd.ClipY);

                pc.TextureId = cmd.TextureIndex;
                secondary_cb->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ZUIPushConstant), &pc);
                secondary_cb->BindDescriptorSets(frame_index);
                secondary_cb->DrawIndexed(cmd.IndexCount, 1, cmd.IndexOffset, cmd.VertexOffset, 0);
            }

            secondary_cb->End();

            Core::Containers::ArrayView<Hardwares::CommandBuffer> cbs{secondary_cb, 1};
            primary_cmd->ExecuteSecondaryCommandBuffers(cbs);
        }
        primary_cmd->EndRenderPass();
    }

} // namespace ZEngine::Rendering::Renderers
