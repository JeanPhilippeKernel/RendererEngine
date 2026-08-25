#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Windows/CoreWindow.h>
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

    // Vertical gradient variant — top vertices use col_top, bottom use col_bot.
    static void EmitQuadGradient(
        UIDrawVert* verts, uint32_t* idxs,
        uint32_t& vert_count, uint32_t& idx_count,
        float x0, float y0, float x1, float y1,
        uint32_t col_top, uint32_t col_bot)
    {
        uint32_t base = vert_count;
        auto push = [&](float x, float y, uint32_t col)
        {
            verts[vert_count].pos.x = x; verts[vert_count].pos.y = y;
            verts[vert_count].uv.x  = 0.f; verts[vert_count].uv.y = 0.f;
            verts[vert_count].col   = col;
            ++vert_count;
        };
        push(x0, y0, col_top); push(x1, y0, col_top);
        push(x1, y1, col_bot); push(x0, y1, col_bot);
        idxs[idx_count++] = base+0; idxs[idx_count++] = base+1;
        idxs[idx_count++] = base+2; idxs[idx_count++] = base+0;
        idxs[idx_count++] = base+2; idxs[idx_count++] = base+3;
    }

    // Rounded rectangle — fan-triangulated from the centre.
    // N arc segments per corner (N=4 gives a smooth appearance).
    static void EmitRoundedRect(
        UIDrawVert* verts, uint32_t* idxs,
        uint32_t& vert_count, uint32_t& idx_count,
        float x0, float y0, float x1, float y1,
        float r, uint32_t color, int N = 4)
    {
        // Clamp radius so it fits inside the rect
        float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
        if (r > hw) r = hw;
        if (r > hh) r = hh;
        if (r <= 0.f) { EmitQuad(verts, idxs, vert_count, idx_count, x0,y0,x1,y1, 0,0,0,0, color); return; }

        // Corner arc centres
        float acx[4] = { x0+r, x1-r, x1-r, x0+r };
        float acy[4] = { y0+r, y0+r, y1-r, y1-r };
        float a0[4]  = { 3.14159265f, 3.f*3.14159265f/2.f, 0.f, 3.14159265f/2.f };

        int total_pts = 4 * (N + 1);
        uint32_t cx_idx = vert_count; // centre vertex
        verts[vert_count] = { { (x0+x1)*0.5f, (y0+y1)*0.5f }, {0,0}, color };
        ++vert_count;

        uint32_t first = vert_count;
        for (int c = 0; c < 4; ++c)
        {
            for (int i = 0; i <= N; ++i)
            {
                float a = a0[c] + (float)i * (3.14159265f * 0.5f / (float)N);
                float px = acx[c] + r * cosf(a);
                float py = acy[c] + r * sinf(a);
                verts[vert_count] = { {px, py}, {0,0}, color };
                ++vert_count;
            }
        }

        for (int i = 0; i < total_pts; ++i)
        {
            uint32_t a = first + (uint32_t)i;
            uint32_t b = first + (uint32_t)((i + 1) % total_pts);
            idxs[idx_count++] = cx_idx;
            idxs[idx_count++] = a;
            idxs[idx_count++] = b;
        }
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
        cmd.VertexOffset = 0; // ZUI uses absolute index values — vertexOffset must be 0
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

    void ZUIRenderer::PreparePayload(UI::ZUIContext* ctx, ZUIRenderPayload* out, Core::Memory::ArenaAllocator* payload_arena)
    {
        if (!ctx || !ctx->Root || !out || !payload_arena) { return; }

        uint32_t max     = ctx->MaxBoxesPerFrame;
        // Use logical window size for NDC scale to match panel positions and
        // mouse coordinates (all in logical pixels from glfwGetWindowSize /
        // GLFW cursor callbacks). NDC [-1,1] maps to the full physical framebuffer
        // regardless of which unit system we use, so this is always correct.
        float    fb_w    = Device->CurrentWindow ? (float)Device->CurrentWindow->GetWidth()
                                                  : (float)Device->SwapchainPtr->SwapchainImageWidth;
        float    fb_h    = Device->CurrentWindow ? (float)Device->CurrentWindow->GetHeight()
                                                  : (float)Device->SwapchainPtr->SwapchainImageHeight;

        // Output arrays go into payload_arena (per-mailbox-slot, lives until the render
        // thread consumes this payload) — NOT FrameArena which is cleared next frame.
        uint32_t max_verts = 65536;
        uint32_t max_idxs  = max_verts * 3 / 2;
        uint32_t max_cmds  = max * 2;

        out->Vertices     = ZPushArray(payload_arena, UIDrawVert, max_verts);
        out->Indices      = ZPushArray(payload_arena, uint32_t,   max_idxs);
        out->Cmds         = ZPushArray(payload_arena, ZUIDrawCmd, max_cmds);
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

        uint32_t current_tex = 0xFFFFFFFFu;

        // ---------------------------------------------------------------
        // Scissor stack for ZUI_ClipChildren boxes.
        // Each entry: {min_x, min_y, max_x, max_y} of the clip box.
        // We always track a "current clip rect" and open a new draw cmd
        // whenever it changes, using a sentinel invalid texture value that
        // can never collide with a real texture index or the solid-color
        // sentinel (0xFFFFFFFF).
        // ---------------------------------------------------------------
        static constexpr uint32_t kClipDepth    = 8;
        static constexpr uint32_t kInvalidTex   = 0xFFFFFFFEu; // used only to force cmd flush

        const UI::ZUIBox* clip_stack[kClipDepth] = {};
        uint32_t          clip_top  = 0;
        float             clip_x    = 0.f, clip_y = 0.f;
        float             clip_w    = fb_w, clip_h = fb_h;

        // Recompute clip_x/y/w/h from the intersection of all active rects
        auto UpdateClip = [&]()
        {
            float x0 = 0.f, y0 = 0.f, x1 = fb_w, y1 = fb_h;
            for (uint32_t ci = 0; ci < clip_top; ++ci)
            {
                const UI::ZUIBox* cb = clip_stack[ci];
                if (cb->ScreenMin[0] > x0) x0 = cb->ScreenMin[0];
                if (cb->ScreenMin[1] > y0) y0 = cb->ScreenMin[1];
                if (cb->ScreenMax[0] < x1) x1 = cb->ScreenMax[0];
                if (cb->ScreenMax[1] < y1) y1 = cb->ScreenMax[1];
            }
            clip_x = x0;  clip_y = y0;
            clip_w = (x1 > x0) ? x1 - x0 : 0.f;
            clip_h = (y1 > y0) ? y1 - y0 : 0.f;
        };

        // Is `ancestor` somewhere in box->Parent chain?
        auto IsAncestor = [](const UI::ZUIBox* ancestor, const UI::ZUIBox* box) -> bool
        {
            for (const UI::ZUIBox* p = box->Parent; p; p = p->Parent)
                if (p == ancestor) return true;
            return false;
        };

        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box = nodes[i];

            // --- Maintain scissor stack ---
            bool clip_changed = false;
            while (clip_top > 0 && !IsAncestor(clip_stack[clip_top - 1], box))
            {
                --clip_top;
                clip_changed = true;
            }
            if ((box->Flags & UI::ZUI_ClipChildren) && clip_top < kClipDepth)
            {
                clip_stack[clip_top++] = box;
                clip_changed = true;
            }
            if (clip_changed)
            {
                UpdateClip();
                // Close the currently open cmd so the next quad gets its own
                // cmd with the new clip rect. Use the safe invalid sentinel.
                current_tex = kInvalidTex;
            }

            float bx0 = box->ScreenMin[0];
            float by0 = box->ScreenMin[1];
            float bx1 = box->ScreenMax[0];
            float by1 = box->ScreenMax[1];

            if (bx1 <= bx0 || by1 <= by0) { continue; }

            // --- Implicit hover highlight for Clickable boxes with no explicit background ---
            // This handles header rows and nav items that intentionally omit ZUI_DrawBackground.
            if ((box->Flags & UI::ZUI_Clickable) && !(box->Flags & UI::ZUI_DrawBackground))
            {
                UI::ZUIPersistentState* ps =
                    UI::ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                if (ps && ps->HotT > 0.02f)
                {
                    float a = ps->HotT * 0.15f - ps->ActiveT * 0.05f;
                    if (a > 0.f)
                    {
                        if (current_tex != 0xFFFFFFFFu || out->CmdCount == 0)
                        {
                            FlushAndBeginCmd(out->Cmds, out->CmdCount, 0xFFFFFFFFu,
                                             clip_x, clip_y, clip_w, clip_h,
                                             out->VertexCount, out->IndexCount);
                            current_tex = 0xFFFFFFFFu;
                        }
                        float hover_bg[4] = {0.50f, 0.50f, 0.56f, a};
                        EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                                 bx0, by0, bx1, by1, 0.f, 0.f, 0.f, 0.f, PackRGBA(hover_bg));
                    }
                }
            }

            // --- Background (solid) or Image (textured) ---
            if (box->Flags & UI::ZUI_DrawBackground)
            {
                bool is_image = (box->TextureIndex != 0xFFFFFFFFu);
                uint32_t tex  = is_image ? box->TextureIndex : 0xFFFFFFFFu;

                float bg[4] = { box->BgColor[0], box->BgColor[1],
                                 box->BgColor[2], box->BgColor[3] };

                // Hover / active tint for clickable solid boxes
                if (!is_image && (box->Flags & UI::ZUI_Clickable))
                {
                    UI::ZUIPersistentState* ps =
                        UI::ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                    if (ps)
                    {
                        // ImGui-style hover: lift brightness + expand alpha toward 1.
                        // Button hover: (0.26,0.59,0.98,0.40) → (0.26,0.59,0.98,1.00)
                        float lift = ps->HotT * 0.08f - ps->ActiveT * 0.05f;
                        bg[0] = bg[0] + lift > 1.f ? 1.f : bg[0] + lift;
                        bg[1] = bg[1] + lift > 1.f ? 1.f : bg[1] + lift;
                        bg[2] = bg[2] + lift > 1.f ? 1.f : bg[2] + lift;
                        // Expand alpha toward 1 on hover (ImGui button effect)
                        bg[3] = bg[3] + ps->HotT * (1.f - bg[3]) * 0.65f;
                        // Transparent rows fade in on hover
                        if (bg[3] < 0.01f) { bg[3] = ps->HotT * 0.18f; }
                    }
                }

                if (!is_image && bg[3] <= 0.f) {} // fully transparent — skip
                else
                {
                    if (current_tex != tex || out->CmdCount == 0)
                    {
                        FlushAndBeginCmd(out->Cmds, out->CmdCount, tex,
                                         clip_x, clip_y, clip_w, clip_h,
                                         out->VertexCount, out->IndexCount);
                        current_tex = tex;
                    }

                    // Drop shadow (emitted before main quad so it appears behind)
                    if (!is_image && box->ShadowColor[3] > 0.f)
                    {
                        float sx = box->ShadowOffset[0], sy = box->ShadowOffset[1];
                        EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                                 bx0+sx, by0+sy, bx1+sx, by1+sy,
                                 0.f, 0.f, 0.f, 0.f, PackRGBA(box->ShadowColor));
                    }

                    if (is_image)
                    {
                        EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                                 bx0, by0, bx1, by1, 0.f, 0.f, 1.f, 1.f, 0xFFFFFFFFu);
                    }
                    else if (box->BgColorB[3] > 0.f)
                    {
                        float bgb[4] = { box->BgColorB[0], box->BgColorB[1],
                                         box->BgColorB[2], box->BgColorB[3] };
                        EmitQuadGradient(out->Vertices, out->Indices,
                                         out->VertexCount, out->IndexCount,
                                         bx0, by0, bx1, by1,
                                         PackRGBA(bg), PackRGBA(bgb));
                    }
                    else if (box->CornerRadius > 0.f)
                    {
                        EmitRoundedRect(out->Vertices, out->Indices,
                                        out->VertexCount, out->IndexCount,
                                        bx0, by0, bx1, by1,
                                        box->CornerRadius, PackRGBA(bg));
                    }
                    else
                    {
                        EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                                 bx0, by0, bx1, by1, 0.f, 0.f, 0.f, 0.f, PackRGBA(bg));
                    }
                }
            }

            // --- Border (4 thin solid rects, 1 px default) ---
            if ((box->Flags & UI::ZUI_DrawBorder) &&
                box->BorderThickness > 0.f && box->BorderColor[3] > 0.f)
            {
                float t = box->BorderThickness;
                if (current_tex != 0xFFFFFFFFu || out->CmdCount == 0)
                {
                    FlushAndBeginCmd(out->Cmds, out->CmdCount, 0xFFFFFFFFu,
                                     clip_x, clip_y, clip_w, clip_h,
                                     out->VertexCount, out->IndexCount);
                    current_tex = 0xFFFFFFFFu;
                }
                uint32_t bc = PackRGBA(box->BorderColor);
                EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                         bx0, by0, bx1, by0 + t, 0.f, 0.f, 0.f, 0.f, bc); // top
                EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                         bx0, by1 - t, bx1, by1, 0.f, 0.f, 0.f, 0.f, bc); // bottom
                EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                         bx0, by0 + t, bx0 + t, by1 - t, 0.f, 0.f, 0.f, 0.f, bc); // left
                EmitQuad(out->Vertices, out->Indices, out->VertexCount, out->IndexCount,
                         bx1 - t, by0 + t, bx1, by1 - t, 0.f, 0.f, 0.f, 0.f, bc); // right
            }

            // --- Text (vertically centred, 6 px left indent) ---
            if ((box->Flags & UI::ZUI_DrawText) && box->Label.Ptr && ctx->GetFont(box->FontSize))
            {
                const UI::ZUIFont* font = ctx->GetFont(box->FontSize);
                uint32_t           font_tex = font->AtlasHandle.Index;

                if (current_tex != font_tex || out->CmdCount == 0)
                {
                    // Use full-framebuffer clip for text — per-box clipping would
                    // cut off glyphs shifted by the left indent. Per-panel clip via
                    // ZUI_ClipChildren will be added in a later pass.
                    FlushAndBeginCmd(out->Cmds, out->CmdCount, font_tex,
                                     clip_x, clip_y, clip_w, clip_h,
                                     out->VertexCount, out->IndexCount);
                    current_tex = font_tex;
                }

                // Vertically centre text. Pixel-snap by truncation (matching ImGui's
                // IM_TRUNC behaviour) so glyphs land on exact pixel boundaries.
                float box_h    = by1 - by0;
                float text_top = floorf(by0 + (box_h - font->LineHeight) * 0.5f);
                float baseline  = text_top + font->Ascent;

                // Horizontal alignment; truncate pen X too
                float cx = floorf(bx0 + 4.f); // default Left
                if (box->TextAlign != UI::ZUITextAlign::Left)
                {
                    float text_size[2] = {0.f, 0.f};
                    ZUIMeasureText(font, box->Label.Ptr, box->Label.Len, text_size);
                    if (box->TextAlign == UI::ZUITextAlign::Center)
                        cx = floorf(bx0 + ((bx1 - bx0) - text_size[0]) * 0.5f);
                    else // Right
                        cx = floorf(bx1 - text_size[0] - 4.f);
                }
                uint32_t color  = PackRGBA(box->TextColor);

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
