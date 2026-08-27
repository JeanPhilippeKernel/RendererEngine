#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/ZUIRenderer.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIDrawList.h>
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

        // Per-frame vertex + index buffers
        // GPU buffer sizes — must match or exceed the draw list's max capacity.
        // GrowVtx doubles the CPU buffer when exceeded; if CPU > GPU the upload
        // overflows and corrupts GPU memory (observed as flickering triangles).
        // 65536 vertices × 20 bytes = 1.3 MB/frame
        // 131072 indices × 2 bytes  = 256 KB/frame
        static constexpr uint32_t kMaxVtx = 65536;
        static constexpr uint32_t kMaxIdx = 131072;
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            VtxBHandles[i] = Device->GpuMem.AllocateBuffer(
                sizeof(ZUIDrawVtx) * kMaxVtx,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                GpuMemoryDomain::HostUniform, "ZUIVertexBuffer");
            IdxBHandles[i] = Device->GpuMem.AllocateBuffer(
                sizeof(uint16_t) * kMaxIdx,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                GpuMemoryDomain::HostUniform, "ZUIIndexBuffer");
        }

        // Pipeline: vertex-rate, 3 attributes (pos=R32G32, uv=R32G32, col=R8G8B8A8_UNORM)
        // Col is packed uint32 RGBA8 — hardware unpacks to vec4 automatically.
        auto pass_builder = RenderGraph->RenderPassBuilder;
        pass_builder->SetName("ZUI Draw Pass")
            .SetPipelineName("ZUI-Draw-Pipeline")
            .EnablePipelineBlending(true)
            .SetInputBindingCount(1)
            .SetStride(0, (uint32_t)sizeof(ZUIDrawVtx))
            .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)

            .SetInputAttributeCount(3)
            // location 0: position (x, y)
            .SetLocation(0, 0).SetBinding(0, 0)
            .SetFormat(0, ImageFormat::R32G32_SFLOAT)
            .SetOffset(0, (uint32_t)offsetof(ZUIDrawVtx, x))
            // location 1: UV (u, v)
            .SetLocation(1, 1).SetBinding(1, 0)
            .SetFormat(1, ImageFormat::R32G32_SFLOAT)
            .SetOffset(1, (uint32_t)offsetof(ZUIDrawVtx, u))
            // location 2: color (RGBA8 UNORM packed uint32)
            .SetLocation(2, 2).SetBinding(2, 0)
            .SetFormat(2, ImageFormat::R8G8B8A8_UNORM)
            .SetOffset(2, (uint32_t)offsetof(ZUIDrawVtx, col))

            .UseShader("zui_draw")
            .UseSwapchainAsRenderTarget();

        DrawPass = Device->CreateRenderPass(pass_builder->Detach());
        DrawPass->UseTextureArray("TextureArray");
        DrawPass->SetSampler("LinearClampSampler",
                              Device->GlobalLinearClampToEdgeSamplerImageInfo);
        DrawPass->Verify();
        DrawPass->Bake();
    }

    void ZUIRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        if (DrawPass) DrawPass->Dispose();
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            if (VtxBHandles[i]) Device->GpuMem.FreeBuffer(VtxBHandles[i]);
            if (IdxBHandles[i]) Device->GpuMem.FreeBuffer(IdxBHandles[i]);
        }
    }

    // ---------------------------------------------------------------
    // PreparePayload — walk box tree, emit draw list
    // ---------------------------------------------------------------

    void ZUIRenderer::PreparePayload(UI::ZUIContext*        ctx,
                                      ZUIRenderPayload*      out,
                                      Core::Memory::ArenaAllocator* payload_arena)
    {
        if (!ctx || !ctx->Root || !out || !payload_arena) { return; }

        // Match GPU buffer sizes exactly — GrowVtx can expand CPU buffers;
        // if CPU > GPU at upload time we get memory corruption (flickering).
        const uint32_t kMaxVtx   = 65536;
        const uint32_t kMaxIdx   = 131072;
        const uint32_t max_boxes = ctx->MaxBoxesPerFrame;

        float fb_w = ctx->ScreenW > 0 ? (float)ctx->ScreenW
                                      : (float)Device->SwapchainPtr->SwapchainImageWidth;
        float fb_h = ctx->ScreenH > 0 ? (float)ctx->ScreenH
                                      : (float)Device->SwapchainPtr->SwapchainImageHeight;

        out->Scale[0]          =  2.f / fb_w;
        out->Scale[1]          =  2.f / fb_h;
        out->Translate[0]      = -1.f;
        out->Translate[1]      = -1.f;
        out->FramebufferScale  = ctx->UIScale > 0.f ? ctx->UIScale : 1.f;

        uint32_t atlas_idx = ctx->Atlas ? ctx->Atlas->Handle.Index : 0;
        float    wu        = ctx->Atlas ? ctx->Atlas->WhiteU        : 0.f;
        float    wv        = ctx->Atlas ? ctx->Atlas->WhiteV        : 0.f;

        // Init draw list into payload_arena
        ZUIDrawListInit(&ctx->DrawList, payload_arena, kMaxVtx, kMaxIdx, wu, wv, atlas_idx);
        ZUIDrawListPushClipRect(&ctx->DrawList, 0.f, 0.f, fb_w, fb_h, false);

        // ---------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------

        auto PackBoxColor = [](const float c[4][4], bool& all_same) -> uint32_t
        {
            all_same = true;
            for (int k = 1; k < 4; ++k)
                for (int ch = 0; ch < 4; ++ch)
                    if (c[k][ch] != c[0][ch]) { all_same = false; break; }
            return ZUIPackColor(c[0]);
        };

        auto CornersRadius = [](const float r[4]) -> float
        {
            float mx = r[0];
            for (int i = 1; i < 4; ++i) if (r[i] > mx) mx = r[i];
            return mx;
        };

        auto IsAncestor = [](const ZUIBox* a, const ZUIBox* b) -> bool
        {
            for (const ZUIBox* p = b->Parent; p; p = p->Parent)
                if (p == a) return true;
            return false;
        };

        // ---------------------------------------------------------------
        // Clip stack (same ancestor-based approach as the old renderer)
        // ---------------------------------------------------------------
        static constexpr uint32_t kClipDepth = 8;
        const ZUIBox* clip_stack[kClipDepth] = {};
        uint32_t      clip_top = 0;

        auto PushBoxClip = [&](const ZUIBox* box)
        {
            if (clip_top < kClipDepth)
            {
                clip_stack[clip_top++] = box;
                float x0 = box->ScreenMin[0], y0 = box->ScreenMin[1];
                float x1 = box->ScreenMax[0], y1 = box->ScreenMax[1];
                ZUIDrawListPushClipRect(&ctx->DrawList, x0, y0, x1, y1, true);
            }
        };
        auto PopToAncestor = [&](const ZUIBox* box)
        {
            while (clip_top > 0 && !IsAncestor(clip_stack[clip_top - 1], box))
            {
                --clip_top;
                ZUIDrawListPopClipRect(&ctx->DrawList);
            }
        };

        // ---------------------------------------------------------------
        // DFS walk (identical traversal order to old PreparePayload)
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
            float bx0 = box->ScreenMin[0], by0 = box->ScreenMin[1];
            float bx1 = box->ScreenMax[0], by1 = box->ScreenMax[1];

            // Maintain clip stack
            PopToAncestor(box);
            if (box->Flags & ZUI_ClipChildren) PushBoxClip(box);
            if (bx1 <= bx0 || by1 <= by0) { continue; } // zero-area

            float cr = CornersRadius(box->CornerRadii);

            // --- Drop shadow (emitted first, renders behind everything) ---
            if (box->Flags & ZUI_DropShadow)
            {
                float    offset = ctx->Style.DropShadowOffset;
                uint32_t scol   = ZUIPackColor(0.f, 0.f, 0.f, ctx->Style.DropShadowAlpha);
                ZUIDrawListAddRectFilled(&ctx->DrawList,
                    bx0 + offset, by0 + offset,
                    bx1 + offset, by1 + offset,
                    scol, cr);
            }

            // Per-corner round_flags from CornerRadii — allows top-only, bottom-only, etc.
            // ZUIBox index→PathRect bit: TL=0→0x1, TR=1→0x2, BR=3→0x4, BL=2→0x8
            uint32_t rf = 0;
            if (box->CornerRadii[0] > 0.f) rf |= 0x1; // TL
            if (box->CornerRadii[1] > 0.f) rf |= 0x2; // TR
            if (box->CornerRadii[3] > 0.f) rf |= 0x4; // BR
            if (box->CornerRadii[2] > 0.f) rf |= 0x8; // BL
            if (rf == 0 && cr > 0.f) rf = 0xF;        // fallback if all are equal nonzero

            // --- Hover overlay for clickable boxes without background ---
            if ((box->Flags & ZUI_Clickable) && !(box->Flags & ZUI_DrawBackground))
            {
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                if (ps && ps->HotT > 0.01f)
                {
                    uint32_t oc = ZUIPackColor(0.5f, 0.5f, 0.56f, ps->HotT * ctx->Style.HoverOverlayAlpha);
                    ZUIDrawListAddRectFilled(&ctx->DrawList, bx0, by0, bx1, by1, oc, cr, rf);
                }
            }

            // --- Background ---
            if (box->Flags & ZUI_DrawBackground)
            {
                bool all_same = true;
                uint32_t col0 = PackBoxColor(box->Colors, all_same);

                if (box->TextureIndex != 0xFFFFFFFFu)
                {
                    ZUIDrawListAddImage(&ctx->DrawList, box->TextureIndex,
                                        bx0, by0, bx1, by1,
                                        0.f, 0.f, 1.f, 1.f,
                                        ZUIPackColor(1.f, 1.f, 1.f, 1.f));
                }
                else if (all_same)
                {
                    ZUIDrawListAddRectFilled(&ctx->DrawList, bx0, by0, bx1, by1, col0, cr, rf);
                }
                else
                {
                    // Gradient quad — flat (no rounding), per-corner colors
                    ZUIDrawListAddRectFilledMultiColor(
                        &ctx->DrawList, bx0, by0, bx1, by1,
                        ZUIPackColor(box->Colors[0]),  // TL
                        ZUIPackColor(box->Colors[1]),  // TR
                        ZUIPackColor(box->Colors[2]),  // BL
                        ZUIPackColor(box->Colors[3])); // BR
                }
            }

            // --- Border ---
            if ((box->Flags & ZUI_DrawBorder) && box->BorderThickness > 0.f)
            {
                uint32_t bcol = ZUIPackColor(box->BorderColor);
                if ((bcol >> 24) > 2)
                    ZUIDrawListAddRect(&ctx->DrawList, bx0, by0, bx1, by1,
                                        bcol, cr, rf ? rf : 0xF, box->BorderThickness);
            }

            // --- Text ---
            if ((box->Flags & ZUI_DrawText) && box->Label.Ptr && ctx->GetFont(box->FontSize))
            {
                const ZUIFont* font = ctx->GetFont(box->FontSize);
                float fs       = font->FontScale > 0.f ? font->FontScale : 1.f;
                float lh       = font->LineHeight * fs;
                float box_h    = by1 - by0;
                float text_top = floorf(by0 + (box_h - lh) * 0.5f);
                float baseline = text_top + font->Ascent * fs;
                float indent   = box->Padding[0] > 0.f ? box->Padding[0] : ctx->Style.FramePadding[0];
                float cx       = floorf(bx0 + indent);

                if (box->TextAlign != ZUITextAlign::Left)
                {
                    float ts[2] = {0.f, 0.f};
                    ZUIMeasureText(font, box->Label.Ptr, box->Label.Len, ts);
                    if (box->TextAlign == ZUITextAlign::Center)
                        cx = floorf(bx0 + ((bx1 - bx0) - ts[0]) * 0.5f);
                    else
                        cx = floorf(bx1 - ts[0] - ctx->Style.FramePadding[0]);
                }

                uint32_t text_col = ZUIPackColor(box->TextColor);

                for (uint32_t ci = 0; ci < box->Label.Len; ++ci)
                {
                    uint32_t cp  = (uint8_t)box->Label.Ptr[ci];
                    uint32_t idx = cp - font->FirstCodepoint;
                    if (cp < font->FirstCodepoint || idx >= font->GlyphCount) { continue; }

                    const ZUIGlyph& g  = font->Glyphs[idx];
                    float gx0 = cx + g.OffsetX * fs;
                    float gy0 = baseline + g.OffsetY * fs;
                    float gx1 = gx0 + g.Width  * fs;
                    float gy1 = gy0 + g.Height * fs;

                    // Pass raw subpixel coords — OversampleH=3 atlas has sub-pixel columns
                    // that the GPU bilinear filter selects. floorf() here wastes the entire
                    // oversampling benefit (matched against ImGui RenderText lines 5940-5943).
                    ZUIDrawListAddImage(&ctx->DrawList, atlas_idx,
                                        gx0, gy0,
                                        gx1, gy1,
                                        g.U0, g.V0, g.U1, g.V1, text_col);
                    cx += g.AdvanceX * fs;
                }
            }

            // --- Checkmark (✓ polyline stroke) ---
            if (box->Flags & ZUI_DrawCheckmark)
            {
                float w = bx1 - bx0, h = by1 - by0;
                // Three-point tick: (25%,55%) → (42%,75%) → (75%,28%)
                float pts_x[3] = { bx0 + w*0.20f, bx0 + w*0.42f, bx0 + w*0.78f };
                float pts_y[3] = { by0 + h*0.52f, by0 + h*0.76f, by0 + h*0.24f };
                uint32_t cc = ZUIPackColor(box->TextColor);
                float thick = (w < 14.f ? 1.5f : 2.0f);
                ZUIDrawListAddLine(&ctx->DrawList, pts_x[0], pts_y[0], pts_x[1], pts_y[1], cc, thick);
                ZUIDrawListAddLine(&ctx->DrawList, pts_x[1], pts_y[1], pts_x[2], pts_y[2], cc, thick);
            }

            // --- Circle fill (inscribed in box center) ---
            if (box->Flags & ZUI_DrawCircleFill)
            {
                float cx  = (bx0 + bx1) * 0.5f;
                float cy  = (by0 + by1) * 0.5f;
                float r   = ((bx1-bx0) < (by1-by0) ? (bx1-bx0) : (by1-by0)) * 0.32f;
                uint32_t cc = ZUIPackColor(box->TextColor);
                ZUIDrawListAddCircleFilled(&ctx->DrawList, cx, cy, r, cc);
            }

            // --- Plot lines (ZUIPlotLines) ---
            if ((box->Flags & ZUI_DrawPlotLines) && box->Label.Ptr && box->Label.Len >= 2)
            {
                const float* data   = (const float*)box->Label.Ptr;
                int          n      = (int)box->Label.Len;
                float        v_min  = box->Padding[0];
                float        v_max  = box->Padding[2];
                float        range  = v_max - v_min; if (range < 1e-6f) range = 1.f;
                float        pw     = bx1 - bx0, ph = by1 - by0;
                uint32_t pcol = ZUIPackColor(ctx->Theme.PlotLines);
                // Emit line segments (n-1 segments for n data points)
                float prev_x = bx0;
                float v0     = (data[0] - v_min) / range;
                if (v0 < 0.f) v0 = 0.f; if (v0 > 1.f) v0 = 1.f;
                float prev_y = by1 - v0 * ph;
                for (int i = 1; i < n; ++i)
                {
                    float t    = (float)i / (float)(n - 1);
                    float v    = (data[i] - v_min) / range;
                    if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f;
                    float cx   = bx0 + t * pw;
                    float cy   = by1 - v * ph;
                    ZUIDrawListAddLine(&ctx->DrawList, prev_x, prev_y, cx, cy, pcol, 1.5f);
                    prev_x = cx; prev_y = cy;
                }
            }

            // --- Plot histogram (ZUIPlotHistogram) ---
            if ((box->Flags & ZUI_DrawPlotBars) && box->Label.Ptr && box->Label.Len >= 1)
            {
                const float* data  = (const float*)box->Label.Ptr;
                int          n     = (int)box->Label.Len;
                float        v_min = box->Padding[0];
                float        v_max = box->Padding[2];
                float        range = v_max - v_min; if (range < 1e-6f) range = 1.f;
                float        pw    = bx1 - bx0, ph = by1 - by0;
                float        bar_w = pw / (float)n;
                uint32_t     pcol  = ZUIPackColor(ctx->Theme.PlotHistogram);
                for (int i = 0; i < n; ++i)
                {
                    float v = (data[i] - v_min) / range;
                    if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f;
                    float x0 = bx0 + (float)i * bar_w + 1.f;
                    float x1 = x0 + bar_w - 2.f;
                    float y0 = by1 - v * ph;
                    ZUIDrawListAddRectFilled(&ctx->DrawList, x0, y0, x1, by1, pcol, 0.f);
                }
            }

            // --- Triangle arrow (collapse indicator) ---
            // Geometry matches ImGui RenderArrow() exactly:
            //   r = FontSize * 0.40  (5.2px at FontSize=13)
            //   Down ▼:  a=(0,0.75)*r  b=(-0.866,-0.75)*r  c=(0.866,-0.75)*r
            //   Right ►: a=(0.75,0)*r  b=(-0.75,0.866)*r   c=(-0.75,-0.866)*r
            // We scale r from box height so the arrow is proportional to the row.
            if (box->Flags & ZUI_DrawTriArrow)
            {
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                bool down = ps && ps->UserData > 0.5f;

                // Center arrow in the box (ImGui centers it at pos + h*0.5)
                float cx = (bx0 + bx1) * 0.5f;
                float cy = (by0 + by1) * 0.5f;

                // r ≈ FontSize * 0.40; scale proportionally to row height
                // At row_h=19: r = 19*(13/19)*0.40 = 13*0.40 = 5.2px  (matches ImGui)
                float r = (by1 - by0) * (0.40f * 13.f / 19.f);

                uint32_t cc = ZUIPackColor(box->TextColor);
                if (down)
                {
                    // ▼ Down-pointing equilateral
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList,
                        cx + 0.000f * r,  cy + 0.750f * r,   // bottom
                        cx - 0.866f * r,  cy - 0.750f * r,   // upper-left
                        cx + 0.866f * r,  cy - 0.750f * r,   // upper-right
                        cc);
                }
                else
                {
                    // ► Right-pointing equilateral
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList,
                        cx + 0.750f * r,  cy + 0.000f * r,   // right tip
                        cx - 0.750f * r,  cy + 0.866f * r,   // lower-left
                        cx - 0.750f * r,  cy - 0.866f * r,   // upper-left
                        cc);
                }
            }
        }

        // Pop remaining clip rects
        while (clip_top > 0) { --clip_top; ZUIDrawListPopClipRect(&ctx->DrawList); }

        // Fill payload from draw list
        out->Vtx      = ctx->DrawList.Vtx;
        out->VtxCount = ctx->DrawList.VtxCount;
        out->Idx      = ctx->DrawList.Idx;
        out->IdxCount = ctx->DrawList.IdxCount;
        out->Cmds     = ctx->DrawList.Cmds;
        out->CmdCount = ctx->DrawList.CmdCount;
    }

    // ---------------------------------------------------------------
    // Submit
    // ---------------------------------------------------------------

    void ZUIRenderer::Submit(Hardwares::CommandBuffer* primary_cmd,
                              const ZUIRenderPayload& payload)
    {
        if (payload.VtxCount == 0 || payload.CmdCount == 0) { return; }

        auto swapchain   = Device->SwapchainPtr;
        auto frame_index = swapchain->CurrentFrame->Index;
        auto current_fb  = swapchain->SwapchainFramebuffers[swapchain->CurrentFrame->ImageIndex];
        uint32_t fi      = frame_index % FRAMES_IN_FLIGHT;

        auto* rrm = Device->RRM
            ? reinterpret_cast<RenderResourceManager*>(Device->RRM) : nullptr;
        if (!rrm) { return; }

        // Clamp to GPU buffer capacity (65536 vtx, 131072 idx) — safety net
        static constexpr uint32_t kVtxCap = 65536;
        static constexpr uint32_t kIdxCap = 131072;
        uint32_t vtx_upload = (payload.VtxCount > kVtxCap) ? kVtxCap : payload.VtxCount;
        uint32_t idx_upload = (payload.IdxCount > kIdxCap) ? kIdxCap : payload.IdxCount;

        rrm->UpdateBuffer(VtxBHandles[fi], payload.Vtx,
                          vtx_upload * sizeof(ZUIDrawVtx));
        rrm->UpdateBuffer(IdxBHandles[fi], payload.Idx,
                          idx_upload * sizeof(uint16_t));

        primary_cmd->BeginRenderPass(DrawPass, current_fb, true);
        {
            auto secondary_cb = Device->CommandBufferMgr->GetCommandBuffer(
                Rendering::QueueType::GRAPHIC_QUEUE, frame_index, 0, ZUICommandBufferIndex, false);
            secondary_cb->ResetState();
            secondary_cb->BeginSecondary(DrawPass, current_fb);
            secondary_cb->SetViewport(DrawPass->GetRenderAreaWidth(),
                                       DrawPass->GetRenderAreaHeight());
            secondary_cb->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC,
                                        DrawPass->Pipeline);
            secondary_cb->BindVertexBuffer(VtxBHandles[fi]);
            secondary_cb->BindIndexBuffer(IdxBHandles[fi], VK_INDEX_TYPE_UINT16);

            float fs = payload.FramebufferScale;

            for (uint32_t i = 0; i < payload.CmdCount; ++i)
            {
                const ZUIDrawListCmd& cmd = payload.Cmds[i];
                if (cmd.ElemCount == 0) { continue; }
                // Skip commands that reference indices beyond the clamped upload range
                if (cmd.IdxOffset + cmd.ElemCount > idx_upload) { continue; }

                // Logical → physical pixel scissor
                secondary_cb->SetScissor(
                    (uint32_t)(cmd.ClipW * fs), (uint32_t)(cmd.ClipH * fs),
                    (int32_t) (cmd.ClipX * fs), (int32_t) (cmd.ClipY * fs));

                ZUIDrawPushConstant pc = {};
                pc.Scale[0]     = payload.Scale[0];
                pc.Scale[1]     = payload.Scale[1];
                pc.Translate[0] = payload.Translate[0];
                pc.Translate[1] = payload.Translate[1];
                pc.TexIdx       = cmd.TexIdx;

                secondary_cb->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0,
                                             sizeof(ZUIDrawPushConstant), &pc);
                secondary_cb->BindDescriptorSets(frame_index);
                secondary_cb->DrawIndexed(cmd.ElemCount, 1, cmd.IdxOffset, 0, 0);
            }

            secondary_cb->End();
            Core::Containers::ArrayView<Hardwares::CommandBuffer> cbs{secondary_cb, 1};
            primary_cmd->ExecuteSecondaryCommandBuffers(cbs);
        }
        primary_cmd->EndRenderPass();
    }

} // namespace ZEngine::Rendering::Renderers
