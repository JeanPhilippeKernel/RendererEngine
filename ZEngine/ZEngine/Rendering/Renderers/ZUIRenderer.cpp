#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/ZUIRenderer.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIFont.h>
#include <cmath>
#include <cstring>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::UI;
using namespace ZEngine::Rendering::Specifications;


namespace ZEngine::Rendering::Renderers
{
    // ---------------------------------------------------------------
    // Initialize / Deinitialize
    // ---------------------------------------------------------------

    void ZUIRenderer::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device      = device;
        RenderGraph = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        RenderGraph->Initialize(Device);

        // Instance buffer — holds ZUIRectInst per frame.
        // 8192 rects × 128 bytes = 1 MB per frame-in-flight.
        static constexpr uint32_t kMaxRects = 8192;
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            InstBHandles[i] = Device->GpuMem.AllocateBuffer(
                sizeof(ZUIRectInst) * kMaxRects,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                GpuMemoryDomain::HostUniform, "ZUIInstanceBuffer");
        }

        // Pipeline — 8 instanced attributes from one binding.
        // No per-vertex data; gl_VertexID (0-3) selects the corner.
        auto pass_builder = RenderGraph->RenderPassBuilder;
        pass_builder->SetName("ZUI Rect Pass")
            .SetPipelineName("ZUI-Rect-Pipeline")
            .EnablePipelineBlending(true)
            .SetInputBindingCount(1)
            .SetStride(0, sizeof(ZUIRectInst))
            .SetRate(0, VK_VERTEX_INPUT_RATE_INSTANCE)

            .SetInputAttributeCount(8)
            // location 0: dst (x0,y0,x1,y1)
            .SetLocation(0, 0).SetBinding(0, 0)
            .SetFormat(0, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(0, offsetof(ZUIRectInst, dst))
            // location 1: src (u0,v0,u1,v1)
            .SetLocation(1, 1).SetBinding(1, 0)
            .SetFormat(1, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(1, offsetof(ZUIRectInst, src))
            // location 2-5: per-corner colors
            .SetLocation(2, 2).SetBinding(2, 0)
            .SetFormat(2, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(2, offsetof(ZUIRectInst, colors[0]))
            .SetLocation(3, 3).SetBinding(3, 0)
            .SetFormat(3, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(3, offsetof(ZUIRectInst, colors[1]))
            .SetLocation(4, 4).SetBinding(4, 0)
            .SetFormat(4, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(4, offsetof(ZUIRectInst, colors[2]))
            .SetLocation(5, 5).SetBinding(5, 0)
            .SetFormat(5, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(5, offsetof(ZUIRectInst, colors[3]))
            // location 6: corner radii
            .SetLocation(6, 6).SetBinding(6, 0)
            .SetFormat(6, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(6, offsetof(ZUIRectInst, corner_radii))
            // location 7: style (border, softness, tex_index, shear)
            .SetLocation(7, 7).SetBinding(7, 0)
            .SetFormat(7, ImageFormat::R32G32B32A32_SFLOAT)
            .SetOffset(7, offsetof(ZUIRectInst, style))

            .UseShader("zui_rect")
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
            if (InstBHandles[i]) { Device->GpuMem.FreeBuffer(InstBHandles[i]); }
        }
    }

    // ---------------------------------------------------------------
    // PreparePayload — walk box tree, emit ZUIRectInst stream
    // ---------------------------------------------------------------

    void ZUIRenderer::PreparePayload(UI::ZUIContext*        ctx,
                                      ZUIRenderPayload*      out,
                                      Core::Memory::ArenaAllocator* payload_arena)
    {
        if (!ctx || !ctx->Root || !out || !payload_arena) { return; }

        const uint32_t kMaxInsts = 8192;
        const uint32_t kMaxCmds  = 512;
        const uint32_t max_boxes = ctx->MaxBoxesPerFrame;

        out->Instances  = ZPushArray(payload_arena, ZUIRectInst, kMaxInsts);
        out->Cmds       = ZPushArray(payload_arena, ZUIDrawCmd,  kMaxCmds);
        out->InstCount  = 0;
        out->CmdCount   = 0;

        // Use ctx->ScreenW/H (logical pixels, already divided by ContentScale on macOS)
        // so that NDC matches the same coordinate space as panel bounds and cursor.
        float fb_w = ctx->ScreenW > 0 ? (float)ctx->ScreenW
                                      : (float)Device->SwapchainPtr->SwapchainImageWidth;
        float fb_h = ctx->ScreenH > 0 ? (float)ctx->ScreenH
                                      : (float)Device->SwapchainPtr->SwapchainImageHeight;

        out->Scale[0]     =  2.f / fb_w;
        out->Scale[1]     =  2.f / fb_h;
        out->Translate[0] = -1.f;
        out->Translate[1] = -1.f;
        // Physical/logical scale — same as ImGui's DisplayFramebufferScale.
        // Scissor rects stored in logical pixels; Submit multiplies by this to get physical px.
        out->FramebufferScale = ctx->UIScale > 0.f ? ctx->UIScale : 1.f;

        // Gather atlas info
        float atlas_tex = ctx->Atlas ? (float)ctx->Atlas->Handle.Index : 0.f;
        float wu        = ctx->Atlas ? ctx->Atlas->WhiteU : 0.f;
        float wv        = ctx->Atlas ? ctx->Atlas->WhiteV : 0.f;

        // ---------------------------------------------------------------
        // Clip stack (same as before — one cmd per clip change)
        // ---------------------------------------------------------------
        static constexpr uint32_t kClipDepth = 8;
        const ZUIBox* clip_stack[kClipDepth] = {};
        uint32_t      clip_top  = 0;
        float         clip_x = 0.f, clip_y = 0.f, clip_w = fb_w, clip_h = fb_h;

        auto UpdateClip = [&]()
        {
            float x0=0.f, y0=0.f, x1=fb_w, y1=fb_h;
            for (uint32_t ci = 0; ci < clip_top; ++ci)
            {
                const ZUIBox* cb = clip_stack[ci];
                if (cb->ScreenMin[0] > x0) x0 = cb->ScreenMin[0];
                if (cb->ScreenMin[1] > y0) y0 = cb->ScreenMin[1];
                if (cb->ScreenMax[0] < x1) x1 = cb->ScreenMax[0];
                if (cb->ScreenMax[1] < y1) y1 = cb->ScreenMax[1];
            }
            clip_x = x0; clip_y = y0;
            clip_w = (x1>x0) ? x1-x0 : 0.f;
            clip_h = (y1>y0) ? y1-y0 : 0.f;
        };

        auto IsAncestor = [](const ZUIBox* a, const ZUIBox* b) -> bool
        {
            for (const ZUIBox* p = b->Parent; p; p = p->Parent)
                if (p == a) return true;
            return false;
        };

        // Open/close cmd on clip change
        bool   cmd_open  = false;
        float  cur_cx = -1.f, cur_cy = -1.f, cur_cw = -1.f, cur_ch = -1.f;

        auto EnsureCmd = [&]()
        {
            bool clip_changed = (clip_x != cur_cx || clip_y != cur_cy ||
                                 clip_w != cur_cw || clip_h != cur_ch);
            if (!cmd_open || clip_changed)
            {
                // close previous
                if (cmd_open && out->CmdCount > 0)
                {
                    auto& prev = out->Cmds[out->CmdCount - 1];
                    prev.InstCount = out->InstCount - prev.InstOffset;
                }
                if (out->CmdCount < kMaxCmds)
                {
                    auto& cmd    = out->Cmds[out->CmdCount++];
                    cmd.InstOffset = out->InstCount;
                    cmd.InstCount  = 0;
                    cmd.ClipX = clip_x; cmd.ClipY = clip_y;
                    cmd.ClipW = clip_w; cmd.ClipH = clip_h;
                    cur_cx = clip_x; cur_cy = clip_y;
                    cur_cw = clip_w; cur_ch = clip_h;
                }
                cmd_open = true;
            }
        };

        auto PushInst = [&](const ZUIRectInst& inst)
        {
            EnsureCmd();
            if (out->InstCount < kMaxInsts)
                out->Instances[out->InstCount++] = inst;
        };

        // ---------------------------------------------------------------
        // Helpers to build ZUIRectInst from ZUIBox data
        // ---------------------------------------------------------------

        auto MakeColoredInst = [&](const ZUIBox* box,
                                    const float colors[4][4],
                                    float border, float softness,
                                    float tex_idx, float su0, float sv0,
                                    float su1, float sv1) -> ZUIRectInst
        {
            ZUIRectInst inst = {};
            inst.dst[0] = box->ScreenMin[0]; inst.dst[1] = box->ScreenMin[1];
            inst.dst[2] = box->ScreenMax[0]; inst.dst[3] = box->ScreenMax[1];
            inst.src[0] = su0; inst.src[1] = sv0;
            inst.src[2] = su1; inst.src[3] = sv1;
            for (int c = 0; c < 4; ++c)
                for (int ch = 0; ch < 4; ++ch)
                    inst.colors[c][ch] = colors[c][ch];
            for (int c = 0; c < 4; ++c)
                inst.corner_radii[c] = box->CornerRadii[c];
            inst.style[0] = border;
            inst.style[1] = softness;
            inst.style[2] = tex_idx;
            inst.style[3] = 0.f;
            return inst;
        };

        auto ApplyHover = [&](ZUIBox* box, float out_colors[4][4])
        {
            for (int c = 0; c < 4; ++c)
                for (int ch = 0; ch < 4; ++ch)
                    out_colors[c][ch] = box->Colors[c][ch];

            if (!(box->Flags & ZUI_Clickable)) { return; }
            auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
            if (!ps) { return; }
            float lift = ps->HotT * 0.08f - ps->ActiveT * 0.05f;
            for (int c = 0; c < 4; ++c)
            {
                float* col = out_colors[c];
                col[0] = col[0] + lift > 1.f ? 1.f : col[0] + lift;
                col[1] = col[1] + lift > 1.f ? 1.f : col[1] + lift;
                col[2] = col[2] + lift > 1.f ? 1.f : col[2] + lift;
                col[3] = col[3] + ps->HotT * (1.f - col[3]) * 0.65f;
                if (col[3] < 0.01f) { col[3] = ps->HotT * 0.18f; }
            }
        };

        // ---------------------------------------------------------------
        // DFS walk
        // ---------------------------------------------------------------
        ZUIBox** nodes     = ZPushArray(&ctx->FrameArena, ZUIBox*, max_boxes);
        ZUIBox** dfs_stack = ZPushArray(&ctx->FrameArena, ZUIBox*, max_boxes);
        uint32_t node_count = 0, stack_top = 0;

        dfs_stack[stack_top++] = ctx->Root;
        while (stack_top > 0 && node_count < max_boxes)
        {
            ZUIBox* box = dfs_stack[--stack_top];
            nodes[node_count++] = box;
            for (ZUIBox* c = box->LastChild; c; c = c->PrevSib)
                if (stack_top < max_boxes) dfs_stack[stack_top++] = c;
        }

        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box = nodes[i];

            // Maintain clip stack
            bool clip_changed = false;
            while (clip_top > 0 && !IsAncestor(clip_stack[clip_top-1], box))
            { --clip_top; clip_changed = true; }
            if ((box->Flags & ZUI_ClipChildren) && clip_top < kClipDepth)
            { clip_stack[clip_top++] = box; clip_changed = true; }
            if (clip_changed) { UpdateClip(); }

            float bx0 = box->ScreenMin[0], by0 = box->ScreenMin[1];
            float bx1 = box->ScreenMax[0], by1 = box->ScreenMax[1];
            if (bx1 <= bx0 || by1 <= by0) { continue; }

            // ---- Implicit hover for Clickable-only boxes ----------
            if ((box->Flags & ZUI_Clickable) && !(box->Flags & ZUI_DrawBackground))
            {
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                if (ps && ps->HotT > 0.02f)
                {
                    float a = ps->HotT * 0.15f;
                    float hover_colors[4][4] = {};
                    for (int c = 0; c < 4; ++c)
                    { hover_colors[c][0]=0.5f; hover_colors[c][1]=0.5f;
                      hover_colors[c][2]=0.56f; hover_colors[c][3]=a; }
                    ZUIRectInst inst = {};
                    inst.dst[0]=bx0; inst.dst[1]=by0; inst.dst[2]=bx1; inst.dst[3]=by1;
                    inst.src[0]=wu; inst.src[1]=wv; inst.src[2]=wu; inst.src[3]=wv;
                    for (int c=0;c<4;++c) for (int ch=0;ch<4;++ch)
                        inst.colors[c][ch] = hover_colors[c][ch];
                    inst.style[1] = box->EdgeSoftness;
                    inst.style[2] = atlas_tex;
                    PushInst(inst);
                }
            }

            // ---- Background (solid fill or image) -----------------
            if (box->Flags & ZUI_DrawBackground)
            {
                bool is_image = (box->TextureIndex != 0xFFFFFFFFu);
                float tinted[4][4];
                if (is_image)
                {
                    for (int c=0;c<4;++c) {
                        tinted[c][0]=1.f; tinted[c][1]=1.f;
                        tinted[c][2]=1.f; tinted[c][3]=1.f;
                    }
                }
                else { ApplyHover(box, tinted); }

                // skip fully transparent solid
                bool all_transparent = !is_image;
                if (all_transparent)
                {
                    for (int c=0;c<4;++c)
                        if (tinted[c][3] > 0.f) { all_transparent = false; break; }
                }
                if (!all_transparent)
                {
                    float tex_idx  = is_image ? (float)box->TextureIndex : atlas_tex;
                    float u0 = is_image ? 0.f : wu, v0 = is_image ? 0.f : wv;
                    float u1 = is_image ? 1.f : wu, v1 = is_image ? 1.f : wv;
                    PushInst(MakeColoredInst(box, tinted, 0.f, box->EdgeSoftness,
                                             tex_idx, u0, v0, u1, v1));
                }
            }

            // ---- Border (rendered as a separate instanced rect) ---
            if ((box->Flags & ZUI_DrawBorder) &&
                box->BorderThickness > 0.f && box->BorderColor[3] > 0.f)
            {
                float bc[4][4];
                for (int c=0;c<4;++c) for (int ch=0;ch<4;++ch)
                    bc[c][ch] = box->BorderColor[ch];
                // Border must always use SDF (to discard interior) — enforce min softness 0.5
                float border_softness = box->EdgeSoftness < 0.5f ? 0.5f : box->EdgeSoftness;
                PushInst(MakeColoredInst(box, bc, box->BorderThickness, border_softness,
                                         atlas_tex, wu, wv, wu, wv));
            }

            // ---- Text (one ZUIRectInst per glyph) -----------------
            if ((box->Flags & ZUI_DrawText) && box->Label.Ptr && ctx->GetFont(box->FontSize))
            {
                const ZUIFont* font = ctx->GetFont(box->FontSize);
                float box_h    = by1 - by0;
                float text_top = floorf(by0 + (box_h - font->LineHeight) * 0.5f);
                float baseline = text_top + font->Ascent;
                float text_indent = box->Padding[0] > 0.f ? box->Padding[0] : 4.f;
                float cx       = floorf(bx0 + text_indent);

                if (box->TextAlign != ZUITextAlign::Left)
                {
                    float ts[2] = {0.f,0.f};
                    ZUIMeasureText(font, box->Label.Ptr, box->Label.Len, ts);
                    if (box->TextAlign == ZUITextAlign::Center)
                        cx = floorf(bx0 + ((bx1-bx0) - ts[0]) * 0.5f);
                    else
                        cx = floorf(bx1 - ts[0] - 4.f);
                }

                for (uint32_t ci = 0; ci < box->Label.Len; ++ci)
                {
                    uint32_t cp  = (uint8_t)box->Label.Ptr[ci];
                    uint32_t idx = cp - font->FirstCodepoint;
                    if (cp < font->FirstCodepoint || idx >= font->GlyphCount) { continue; }

                    const ZUIGlyph& g  = font->Glyphs[idx];
                    float gx0 = cx + g.OffsetX;
                    float gy0 = baseline + g.OffsetY;
                    float gx1 = gx0 + g.Width;
                    float gy1 = gy0 + g.Height;

                    ZUIRectInst inst = {};
                    inst.dst[0]=floorf(gx0); inst.dst[1]=floorf(gy0);
                    inst.dst[2]=floorf(gx1); inst.dst[3]=floorf(gy1);
                    inst.src[0]=g.U0; inst.src[1]=g.V0;
                    inst.src[2]=g.U1; inst.src[3]=g.V1;
                    for (int c=0;c<4;++c) for (int ch=0;ch<4;++ch)
                        inst.colors[c][ch] = box->TextColor[ch];
                    // softness = 0 → skip SDF, alpha from texture
                    inst.style[1] = 0.f;
                    inst.style[2] = atlas_tex;

                    PushInst(inst);
                    cx += g.AdvanceX;
                }
            }
        }

        // Close the last open command
        if (cmd_open && out->CmdCount > 0)
        {
            auto& last = out->Cmds[out->CmdCount - 1];
            last.InstCount = out->InstCount - last.InstOffset;
        }
    }

    // ---------------------------------------------------------------
    // Submit
    // ---------------------------------------------------------------

    void ZUIRenderer::Submit(Hardwares::CommandBuffer* primary_cmd,
                              const ZUIRenderPayload& payload)
    {
        if (payload.InstCount == 0 || payload.CmdCount == 0) { return; }

        auto swapchain   = Device->SwapchainPtr;
        auto frame_index = swapchain->CurrentFrame->Index;
        auto current_fb  = swapchain->SwapchainFramebuffers[swapchain->CurrentFrame->ImageIndex];

        uint32_t fi  = frame_index % FRAMES_IN_FLIGHT;
        auto     ib  = InstBHandles[fi];

        primary_cmd->BeginRenderPass(UIPass, current_fb, true);
        {
            auto* rrm = Device->RRM ? reinterpret_cast<RenderResourceManager*>(Device->RRM) : nullptr;
            if (rrm) { rrm->UpdateBuffer(ib, payload.Instances,
                                         payload.InstCount * sizeof(ZUIRectInst)); }

            auto secondary_cb = Device->CommandBufferMgr->GetCommandBuffer(
                Rendering::QueueType::GRAPHIC_QUEUE, frame_index, 0, ZUICommandBufferIndex, false);
            secondary_cb->ResetState();
            secondary_cb->BeginSecondary(UIPass, current_fb);
            secondary_cb->SetViewport(UIPass->GetRenderAreaWidth(), UIPass->GetRenderAreaHeight());
            secondary_cb->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, UIPass->Pipeline);
            secondary_cb->BindVertexBuffer(ib);

            ZUIRectPushConstant pc = {};
            pc.Scale[0]     = payload.Scale[0];
            pc.Scale[1]     = payload.Scale[1];
            pc.Translate[0] = payload.Translate[0];
            pc.Translate[1] = payload.Translate[1];

            for (uint32_t i = 0; i < payload.CmdCount; ++i)
            {
                const ZUIDrawCmd& cmd = payload.Cmds[i];
                if (cmd.InstCount == 0) { continue; }

                // Scale logical scissor → physical pixels (ImGui DisplayFramebufferScale pattern)
                float fs = payload.FramebufferScale;
                secondary_cb->SetScissor(
                    (uint32_t)(cmd.ClipW * fs), (uint32_t)(cmd.ClipH * fs),
                    (int32_t) (cmd.ClipX * fs), (int32_t) (cmd.ClipY * fs));

                secondary_cb->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0,
                                            sizeof(ZUIRectPushConstant), &pc);
                secondary_cb->BindDescriptorSets(frame_index);
                // 6 vertices per instance (2 triangles, TRIANGLE_LIST)
                secondary_cb->Draw(6, cmd.InstCount, 0, cmd.InstOffset);
            }

            secondary_cb->End();
            Core::Containers::ArrayView<Hardwares::CommandBuffer> cbs{secondary_cb, 1};
            primary_cmd->ExecuteSecondaryCommandBuffers(cbs);
        }
        primary_cmd->EndRenderPass();
    }

} // namespace ZEngine::Rendering::Renderers
