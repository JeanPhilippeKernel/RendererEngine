#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUIFont.h>
#include <cmath>
#include <cstring>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::UI;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    // Initialize / Deinitialize

    void ZUIPass::Initialize(Hardwares::VulkanDevicePtr device)
    {
        if (m_initialized)
            return;

        Device = device;
        ZENGINE_VALIDATE_ASSERT(Device != nullptr, "ZUI pass requires a Vulkan device")
        ZENGINE_VALIDATE_ASSERT(Device->SwapchainPtr->BufferredFrameCount <= FRAMES_IN_FLIGHT, "ZUI buffers must cover every buffered frame")

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
            VtxBHandles[i] = Device->GpuMem.AllocateBuffer(sizeof(ZUIDrawVtx) * kMaxVtx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, GpuMemoryDomain::HostUniform, "ZUIVertexBuffer");
            IdxBHandles[i] = Device->GpuMem.AllocateBuffer(sizeof(uint16_t) * kMaxIdx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, GpuMemoryDomain::HostUniform, "ZUIIndexBuffer");
        }

        m_initialized = true;
    }

    void ZUIPass::Deinitialize(Hardwares::VulkanDevicePtr const /*device*/)
    {
        ReleaseResources();
    }

    void ZUIPass::ReleaseResources()
    {
        m_payload = nullptr;
        if (DrawPass)
        {
            DrawPass->Dispose();
            DrawPass = nullptr;
        }

        if (!m_initialized)
            return;

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            if (VtxBHandles[i])
                Device->GpuMem.FreeBuffer(VtxBHandles[i]);
            if (IdxBHandles[i])
                Device->GpuMem.FreeBuffer(IdxBHandles[i]);
            VtxBHandles[i] = {};
            IdxBHandles[i] = {};
        }
        m_initialized = false;
    }

    void ZUIPass::SetPayload(const ZUIRenderPayload* payload)
    {
        m_payload = payload;
    }

    bool ZUIPass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        ZENGINE_VALIDATE_ASSERT(res_builder != nullptr, "ZUI pass requires a render graph resource builder")
        res_builder->ReadTexture(RendererResourceName::FrameColorRenderTargetName);
        res_builder->WriteSwapchain();

        m_vtx_upload = 0;
        m_idx_upload = 0;
        if (!m_payload || m_payload->VtxCount == 0 || m_payload->CmdCount == 0)
            return true;

        auto* rrm = Device->RRM ? reinterpret_cast<RenderResourceManager*>(Device->RRM) : nullptr;
        if (!rrm)
            return true;

        static constexpr uint32_t kVtxCap = 65536;
        static constexpr uint32_t kIdxCap = 131072;
        m_vtx_upload                      = m_payload->VtxCount > kVtxCap ? kVtxCap : m_payload->VtxCount;
        m_idx_upload                      = m_payload->IdxCount > kIdxCap ? kIdxCap : m_payload->IdxCount;
        const uint32_t frame_buffer_index = frame_context.FrameIndex % FRAMES_IN_FLIGHT;
        rrm->UpdateBuffer(VtxBHandles[frame_buffer_index], m_payload->Vtx, m_vtx_upload * sizeof(ZUIDrawVtx));
        rrm->UpdateBuffer(IdxBHandles[frame_buffer_index], m_payload->Idx, m_idx_upload * sizeof(uint16_t));
        return true;
    }

    Specifications::GraphicsPipelineDesc ZUIPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "ZUI-Draw-Pipeline";
        desc.EnableBlending                       = true;
        desc.ShaderSpecificationValue.Name        = "zui_draw";

        desc.VertexInputBindingSpecifications.init(arena, 1, 1);
        desc.VertexInputBindingSpecifications[0] = {.Stride = static_cast<uint32_t>(sizeof(ZUIDrawVtx)), .Rate = VK_VERTEX_INPUT_RATE_VERTEX, .Binding = 0};
        desc.VertexInputAttributeSpecifications.init(arena, 3, 3);
        desc.VertexInputAttributeSpecifications[0] = {.Location = 0, .Binding = 0, .Offset = static_cast<uint32_t>(offsetof(ZUIDrawVtx, x)), .Format = ImageFormat::R32G32_SFLOAT};
        desc.VertexInputAttributeSpecifications[1] = {.Location = 1, .Binding = 0, .Offset = static_cast<uint32_t>(offsetof(ZUIDrawVtx, u)), .Format = ImageFormat::R32G32_SFLOAT};
        desc.VertexInputAttributeSpecifications[2] = {.Location = 2, .Binding = 0, .Offset = static_cast<uint32_t>(offsetof(ZUIDrawVtx, col)), .Format = ImageFormat::R8G8B8A8_UNORM};
        return desc;
    }

    void ZUIPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!pass)
            return;

        DrawPass = static_cast<RenderPasses::GraphicPass*>(pass);
        DrawPass->UseTextureArray("TextureArray");
        DrawPass->SetSampler("LinearClampSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    // PreparePayload — walk box tree, emit draw list

    void ZUIPass::PreparePayload(UI::ZUIContext* ctx, ZUIRenderPayload* out, Core::Memory::ArenaAllocator* payload_arena)
    {
        if (!ctx || !ctx->Root || !out || !payload_arena)
        {
            return;
        }

        // Match GPU buffer sizes exactly — GrowVtx can expand CPU buffers;
        // if CPU > GPU at upload time we get memory corruption (flickering).
        const uint32_t kMaxVtx   = 65536;
        const uint32_t kMaxIdx   = 131072;
        const uint32_t max_boxes = ctx->MaxBoxesPerFrame;

        float          fb_w      = ctx->ScreenW > 0 ? (float) ctx->ScreenW : (float) Device->SwapchainPtr->SwapchainImageWidth;
        float          fb_h      = ctx->ScreenH > 0 ? (float) ctx->ScreenH : (float) Device->SwapchainPtr->SwapchainImageHeight;

        out->Scale[0]            = 2.f / fb_w;
        out->Scale[1]            = 2.f / fb_h;
        out->Translate[0]        = -1.f;
        out->Translate[1]        = -1.f;
        out->FramebufferScale    = ctx->UIScale > 0.f ? ctx->UIScale : 1.f;

        uint32_t atlas_idx       = ctx->Atlas ? static_cast<uint32_t>(ctx->Atlas->Handle.Index) : 0;
        float    wu              = ctx->Atlas ? ctx->Atlas->WhiteU : 0.f;
        float    wv              = ctx->Atlas ? ctx->Atlas->WhiteV : 0.f;

        // Init draw list into payload_arena
        ZUIDrawListInit(&ctx->DrawList, payload_arena, kMaxVtx, kMaxIdx, wu, wv, atlas_idx);
        ZUIDrawListPushClipRect(&ctx->DrawList, 0.f, 0.f, fb_w, fb_h, false);

        // Helpers

        auto PackBoxColor = [](const float c[4][4], bool& all_same) -> uint32_t {
            all_same = true;
            for (int k = 1; k < 4; ++k)
                for (int ch = 0; ch < 4; ++ch)
                    if (c[k][ch] != c[0][ch])
                    {
                        all_same = false;
                        break;
                    }
            return ZUIPackColor(c[0]);
        };

        auto CornersRadius = [](const float r[4]) -> float {
            float mx = r[0];
            for (int i = 1; i < 4; ++i)
                if (r[i] > mx)
                    mx = r[i];
            return mx;
        };

        auto IsAncestor = [](const ZUIBox* a, const ZUIBox* b) -> bool {
            for (const ZUIBox* p = b->Parent; p; p = p->Parent)
                if (p == a)
                    return true;
            return false;
        };

        // Clip stack (same ancestor-based approach as the old renderer)
        static constexpr uint32_t kClipDepth             = 8;
        const ZUIBox*             clip_stack[kClipDepth] = {};
        uint32_t                  clip_top               = 0;

        auto                      PushBoxClip            = [&](const ZUIBox* box) {
            if (clip_top < kClipDepth)
            {
                clip_stack[clip_top++] = box;
                float x0 = box->ScreenMin[0], y0 = box->ScreenMin[1];
                float x1 = box->ScreenMax[0], y1 = box->ScreenMax[1];
                ZUIDrawListPushClipRect(&ctx->DrawList, x0, y0, x1, y1, true);
            }
        };
        auto PopToAncestor = [&](const ZUIBox* box) {
            while (clip_top > 0 && !IsAncestor(clip_stack[clip_top - 1], box))
            {
                --clip_top;
                ZUIDrawListPopClipRect(&ctx->DrawList);
            }
        };

        // DFS walk (identical traversal order to old PreparePayload)
        // HOT PATH — runs every frame, no heap allocation allowed.
        ZUIBox** nodes      = ZPushArray(&ctx->FrameArena, ZUIBox*, max_boxes);
        ZUIBox** dfs_stack  = ZPushArray(&ctx->FrameArena, ZUIBox*, max_boxes);
        uint32_t node_count = 0, stack_top = 0;

        dfs_stack[stack_top++] = ctx->Root;
        while (stack_top > 0 && node_count < max_boxes)
        {
            ZUIBox* box         = dfs_stack[--stack_top];
            nodes[node_count++] = box;
            for (ZUIBox* c = box->LastChild; c; c = c->PrevSib)
                if (stack_top < max_boxes)
                    dfs_stack[stack_top++] = c;
        }

        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box = nodes[i];
            float   bx0 = box->ScreenMin[0], by0 = box->ScreenMin[1];
            float   bx1 = box->ScreenMax[0], by1 = box->ScreenMax[1];

            // Maintain clip stack
            PopToAncestor(box);
            if (box->Flags & ZUI_ClipChildren)
                PushBoxClip(box);
            if (bx1 <= bx0 || by1 <= by0)
            {
                continue;
            } // zero-area

            float cr = CornersRadius(box->CornerRadii);

            // Drop shadow (emitted first, renders behind everything)
            if (box->Flags & ZUI_DropShadow)
            {
                float    offset = ctx->Style.DropShadowOffset;
                uint32_t scol   = ZUIPackColor(0.f, 0.f, 0.f, ctx->Style.DropShadowAlpha);
                ZUIDrawListAddRectFilled(&ctx->DrawList, bx0 + offset, by0 + offset, bx1 + offset, by1 + offset, scol, cr);
            }

            // Per-corner round_flags from CornerRadii — allows top-only, bottom-only, etc.
            // ZUIBox index→PathRect bit: TL=0→0x1, TR=1→0x2, BR=3→0x4, BL=2→0x8
            uint32_t rf = 0;
            if (box->CornerRadii[0] > 0.f)
                rf |= 0x1; // TL
            if (box->CornerRadii[1] > 0.f)
                rf |= 0x2; // TR
            if (box->CornerRadii[3] > 0.f)
                rf |= 0x4; // BR
            if (box->CornerRadii[2] > 0.f)
                rf |= 0x8; // BL
            if (rf == 0 && cr > 0.f)
                rf = 0xF; // fallback if all are equal nonzero

            // Hover overlay for clickable boxes without background
            if ((box->Flags & ZUI_Clickable) && !(box->Flags & ZUI_DrawBackground))
            {
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                if (ps && ps->HotT > 0.01f)
                {
                    uint32_t oc = ZUIPackColor(0.5f, 0.5f, 0.56f, ps->HotT * ctx->Style.HoverOverlayAlpha);
                    ZUIDrawListAddRectFilled(&ctx->DrawList, bx0, by0, bx1, by1, oc, cr, rf);
                }
            }

            // Background
            if (box->Flags & ZUI_DrawBackground)
            {
                bool     all_same = true;
                uint32_t col0     = PackBoxColor(box->Colors, all_same);

                if (box->TextureIndex != 0xFFFFFFFFu)
                {
                    ZUIDrawListAddImage(&ctx->DrawList, box->TextureIndex, bx0, by0, bx1, by1, 0.f, 0.f, 1.f, 1.f, ZUIPackColor(1.f, 1.f, 1.f, 1.f));
                }
                else if (all_same)
                {
                    ZUIDrawListAddRectFilled(&ctx->DrawList, bx0, by0, bx1, by1, col0, cr, rf);
                }
                else
                {
                    // Gradient quad — flat (no rounding), per-corner colors
                    ZUIDrawListAddRectFilledMultiColor(
                        &ctx->DrawList,
                        bx0,
                        by0,
                        bx1,
                        by1,
                        ZUIPackColor(box->Colors[0]),  // TL
                        ZUIPackColor(box->Colors[1]),  // TR
                        ZUIPackColor(box->Colors[2]),  // BL
                        ZUIPackColor(box->Colors[3])); // BR
                }
            }

            // Border
            if ((box->Flags & ZUI_DrawBorder) && box->BorderThickness > 0.f)
            {
                uint32_t bcol = ZUIPackColor(box->BorderColor);
                if ((bcol >> 24) > 2)
                    ZUIDrawListAddRect(&ctx->DrawList, bx0, by0, bx1, by1, bcol, cr, rf ? rf : 0xF, box->BorderThickness);
            }

            // Text
            if ((box->Flags & ZUI_DrawText) && box->Label.Ptr && ctx->GetFont(box->FontSize))
            {
                const ZUIFont* font     = ctx->GetFont(box->FontSize);
                float          fs       = font->FontScale > 0.f ? font->FontScale : 1.f;
                float          lh       = font->LineHeight * fs;
                float          box_h    = by1 - by0;
                float          text_top = floorf(by0 + (box_h - lh) * 0.5f);
                float          baseline = text_top + font->Ascent * fs;
                float          indent   = box->Padding[0] > 0.f ? box->Padding[0] : ctx->Style.FramePadding[0];
                float          cx       = floorf(bx0 + indent);

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
                    uint32_t cp  = (uint8_t) box->Label.Ptr[ci];
                    uint32_t idx = cp - font->FirstCodepoint;
                    if (cp < font->FirstCodepoint || idx >= font->GlyphCount)
                    {
                        continue;
                    }

                    const ZUIGlyph& g   = font->Glyphs[idx];
                    float           gx0 = cx + g.OffsetX * fs;
                    float           gy0 = baseline + g.OffsetY * fs;
                    float           gx1 = gx0 + g.Width * fs;
                    float           gy1 = gy0 + g.Height * fs;

                    // Pass raw subpixel coords — OversampleH=3 atlas has sub-pixel columns
                    // that the GPU bilinear filter selects. floorf() here wastes the entire
                    // oversampling benefit (matched against ImGui RenderText lines 5940-5943).
                    ZUIDrawListAddImage(&ctx->DrawList, atlas_idx, gx0, gy0, gx1, gy1, g.U0, g.V0, g.U1, g.V1, text_col);
                    cx += g.AdvanceX * fs;
                }
            }

            // Checkmark (✓ polyline stroke)
            if (box->Flags & ZUI_DrawCheckmark)
            {
                float    w = bx1 - bx0, h = by1 - by0;
                // Three-point tick: (25%,55%) → (42%,75%) → (75%,28%)
                float    pts_x[3] = {bx0 + w * 0.20f, bx0 + w * 0.42f, bx0 + w * 0.78f};
                float    pts_y[3] = {by0 + h * 0.52f, by0 + h * 0.76f, by0 + h * 0.24f};
                uint32_t cc       = ZUIPackColor(box->TextColor);
                float    thick    = (w < 14.f ? 1.5f : 2.0f);
                ZUIDrawListAddLine(&ctx->DrawList, pts_x[0], pts_y[0], pts_x[1], pts_y[1], cc, thick);
                ZUIDrawListAddLine(&ctx->DrawList, pts_x[1], pts_y[1], pts_x[2], pts_y[2], cc, thick);
            }

            // Circle fill (inscribed in box center)
            if (box->Flags & ZUI_DrawCircleFill)
            {
                float    cx = (bx0 + bx1) * 0.5f;
                float    cy = (by0 + by1) * 0.5f;
                float    r  = ((bx1 - bx0) < (by1 - by0) ? (bx1 - bx0) : (by1 - by0)) * 0.32f;
                uint32_t cc = ZUIPackColor(box->TextColor);
                ZUIDrawListAddCircleFilled(&ctx->DrawList, cx, cy, r, cc);
            }

            // Plot lines (ZUIPlotLines)
            if ((box->Flags & ZUI_DrawPlotLines) && box->Label.Ptr && box->Label.Len >= 2)
            {
                const float* data  = (const float*) box->Label.Ptr;
                int          n     = (int) box->Label.Len;
                float        v_min = box->Padding[0];
                float        v_max = box->Padding[2];
                float        range = v_max - v_min;
                if (range < 1e-6f)
                    range = 1.f;
                float    pw = bx1 - bx0, ph = by1 - by0;
                // Per-box color override: use TextColor when alpha > 0, else theme default
                uint32_t pcol   = (box->TextColor[3] > 0.f) ? ZUIPackColor(box->TextColor) : ZUIPackColor(ctx->Theme.PlotLines);
                // Emit line segments (n-1 segments for n data points)
                float    prev_x = bx0;
                float    v0     = (data[0] - v_min) / range;
                if (v0 < 0.f)
                    v0 = 0.f;
                if (v0 > 1.f)
                    v0 = 1.f;
                float prev_y = by1 - v0 * ph;
                for (int i = 1; i < n; ++i)
                {
                    float t = (float) i / (float) (n - 1);
                    float v = (data[i] - v_min) / range;
                    if (v < 0.f)
                        v = 0.f;
                    if (v > 1.f)
                        v = 1.f;
                    float cx = bx0 + t * pw;
                    float cy = by1 - v * ph;
                    ZUIDrawListAddLine(&ctx->DrawList, prev_x, prev_y, cx, cy, pcol, 1.5f);
                    prev_x = cx;
                    prev_y = cy;
                }
            }

            // Plot histogram (ZUIPlotHistogram)
            if ((box->Flags & ZUI_DrawPlotBars) && box->Label.Ptr && box->Label.Len >= 1)
            {
                const float* data  = (const float*) box->Label.Ptr;
                int          n     = (int) box->Label.Len;
                float        v_min = box->Padding[0];
                float        v_max = box->Padding[2];
                float        range = v_max - v_min;
                if (range < 1e-6f)
                    range = 1.f;
                float    pw = bx1 - bx0, ph = by1 - by0;
                float    bar_w = pw / (float) n;
                uint32_t pcol  = ZUIPackColor(ctx->Theme.PlotHistogram);
                for (int i = 0; i < n; ++i)
                {
                    float v = (data[i] - v_min) / range;
                    if (v < 0.f)
                        v = 0.f;
                    if (v > 1.f)
                        v = 1.f;
                    float x0 = bx0 + (float) i * bar_w + 1.f;
                    float x1 = x0 + bar_w - 2.f;
                    float y0 = by1 - v * ph;
                    ZUIDrawListAddRectFilled(&ctx->DrawList, x0, y0, x1, by1, pcol, 0.f);
                }
            }

            // Triangle arrow (collapse indicator)
            // Geometry matches ImGui RenderArrow() exactly:
            //   r = FontSize * 0.40  (5.2px at FontSize=13)
            //   Down ▼:  a=(0,0.75)*r  b=(-0.866,-0.75)*r  c=(0.866,-0.75)*r
            //   Right ►: a=(0.75,0)*r  b=(-0.75,0.866)*r   c=(-0.75,-0.866)*r
            // We scale r from box height so the arrow is proportional to the row.
            if (box->Flags & ZUI_DrawTriArrow)
            {
                auto*    ps    = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                float    udata = ps ? ps->UserData : 0.f;
                float    cx    = (bx0 + bx1) * 0.5f;
                float    cy    = (by0 + by1) * 0.5f;
                uint32_t cc    = ZUIPackColor(box->TextColor);
                float    fs    = ctx->Style.FontSize;

                if (udata > 1.5f)
                {
                    float bw = bx1 - bx0;
                    float bh = by1 - by0;
                    // 90° opening angle: hw = 2 × hh  →  cos(θ)=0  →  arms at 45° each
                    // hh derived from the shorter box dimension so the chevron stays compact
                    float hh = fminf(bw, bh) * 0.185f;
                    float hw = hh * 2.0f;
                    if (udata < 2.5f)
                        ZUIDrawListAddChevronDown(&ctx->DrawList, cx, cy, hw, hh, cc, 1.0f);
                    else
                        ZUIDrawListAddChevronRight(&ctx->DrawList, cx, cy, hw, hh, cc, 1.0f);
                }
                else
                {
                    // Filled equilateral triangle (tree nodes, collapsing headers)
                    float r    = (by1 - by0) * (0.40f * 13.f / 19.f);
                    bool  down = udata > 0.5f;
                    if (down)
                    {
                        // ▼ Down
                        ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx + 0.000f * r, cy + 0.750f * r, cx - 0.866f * r, cy - 0.750f * r, cx + 0.866f * r, cy - 0.750f * r, cc);
                    }
                    else
                    {
                        // ► Right
                        ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx + 0.750f * r, cy + 0.000f * r, cx - 0.750f * r, cy + 0.866f * r, cx - 0.750f * r, cy - 0.866f * r, cc);
                    }
                }
            }

            // Actor-type icon (ZUI_DrawActorIcon)
            // Icon shape is selected by ZUIPersistentState::UserData (ZUI_ICON_* constants).
            // Color comes from TextColor.  Geometry mirrors ImGui DrawTypeIcon() exactly.
            if (box->Flags & ZUI_DrawActorIcon)
            {
                auto*    ps    = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
                float    itype = ps ? ps->UserData : ZUI_ICON_ACTOR;
                float    cx    = (bx0 + bx1) * 0.5f;
                float    cy    = (by0 + by1) * 0.5f;
                float    sz    = fminf(bx1 - bx0, by1 - by0);
                uint32_t cc    = ZUIPackColor(box->TextColor);

                if (itype < 10.5f) // ZUI_ICON_WORLD = 10
                {
                    float r = sz * 0.40f;
                    ZUIDrawListAddCircle(&ctx->DrawList, cx, cy, r, cc, 16, 1.2f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - r, cy, cx + r, cy, cc, 1.0f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx, cy - r, cx, cy + r, cc, 1.0f);
                }
                else if (itype < 11.5f) // ZUI_ICON_LIGHT = 11
                {
                    float r    = sz * 0.20f;
                    float ray  = sz * 0.40f;
                    float diag = ray * 0.70f;
                    float rd   = r * 0.70f;
                    ZUIDrawListAddCircleFilled(&ctx->DrawList, cx, cy, r, cc, 8);
                    ZUIDrawListAddLine(&ctx->DrawList, cx, cy - ray, cx, cy - r, cc, 1.2f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx, cy + r, cx, cy + ray, cc, 1.2f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - ray, cy, cx - r, cy, cc, 1.2f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx + r, cy, cx + ray, cy, cc, 1.2f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - diag, cy - diag, cx - rd, cy - rd, cc, 1.0f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx + rd, cy - rd, cx + diag, cy - diag, cc, 1.0f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - diag, cy + diag, cx - rd, cy + rd, cc, 1.0f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx + rd, cy + rd, cx + diag, cy + diag, cc, 1.0f);
                }
                else if (itype < 12.5f) // ZUI_ICON_MESH = 12
                {
                    float hw = sz * 0.28f, hh = sz * 0.28f, d = sz * 0.16f;
                    float bx = cx - hw, by = cy;
                    ZUIDrawListAddRect(&ctx->DrawList, bx, by, bx + hw * 2, by + hh * 2, cc, 0.f, 0xF, 1.0f);
                    ZUIDrawListAddRect(&ctx->DrawList, bx + d, by - d, bx + hw * 2 + d, by + hh * 2 - d, cc, 0.f, 0xF, 0.6f);
                    ZUIDrawListAddLine(&ctx->DrawList, bx, by, bx + d, by - d, cc, 0.6f);
                    ZUIDrawListAddLine(&ctx->DrawList, bx + hw * 2, by, bx + hw * 2 + d, by - d, cc, 0.6f);
                    ZUIDrawListAddLine(&ctx->DrawList, bx, by + hh * 2, bx + d, by + hh * 2 - d, cc, 0.6f);
                }
                else if (itype < 13.5f) // ZUI_ICON_CAMERA = 13
                {
                    float bw = sz * 0.55f, bh = sz * 0.40f;
                    float ibx = bx0 + sz * 0.05f, iby = cy - bh * 0.5f;
                    ZUIDrawListAddRectFilled(&ctx->DrawList, ibx, iby, ibx + bw, iby + bh, cc, 1.5f);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, ibx + bw, iby + bh * 0.1f, ibx + bw + sz * 0.25f, cy, ibx + bw, iby + bh * 0.9f, cc);
                }
                else if (itype < 14.5f) // ZUI_ICON_FOLDER = 14
                {
                    float fw = sz * 0.80f, fh = sz * 0.65f;
                    float fix = bx0 + (sz - fw) * 0.5f;
                    float fiy = by0 + sz - fh;
                    ZUIDrawListAddRectFilled(&ctx->DrawList, fix, fiy + fh * 0.28f, fix + fw, fiy + fh, cc, 1.5f);
                    ZUIDrawListAddRectFilled(&ctx->DrawList, fix, fiy, fix + fw * 0.44f, fiy + fh * 0.32f, cc, 1.5f);
                }
                else if (itype < 15.5f) // ZUI_ICON_ACTOR = 15: diamond (explicit — avoids fall-through to COLLECTION_ADD)
                {
                    float r = sz * 0.35f;
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx, cy - r, cx + r, cy, cx, cy + r, cc);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx, cy - r, cx, cy + r, cx - r, cy, cc);
                }
                else if (itype < 20.5f) // ZUI_ICON_COLLECTION_ADD = 20: folder shape + "+" cross
                {
                    // Folder shape (mirrors develop DrawTypeIcon / ICON_FOLDER)
                    float fw = sz * 0.80f, fh = sz * 0.65f;
                    float fix = bx0 + (sz - fw) * 0.5f;
                    float fiy = by0 + sz - fh;
                    ZUIDrawListAddRectFilled(&ctx->DrawList, fix, fiy + fh * 0.28f, fix + fw, fiy + fh, cc, 1.5f);
                    ZUIDrawListAddRectFilled(&ctx->DrawList, fix, fiy, fix + fw * 0.44f, fiy + fh * 0.32f, cc, 1.5f);

                    // "+" cross centered on the folder body, with shadow for depth
                    float    body_cy = fiy + fh * 0.28f + (fh * 0.72f) * 0.5f;
                    float    arm     = sz * 0.15f;
                    uint32_t shadow  = ZUIPackColor(0.14f, 0.15f, 0.18f, 1.f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - arm, body_cy, cx + arm, body_cy, shadow, 2.5f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx, body_cy - arm, cx, body_cy + arm, shadow, 2.5f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - arm, body_cy, cx + arm, body_cy, cc, 1.5f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx, body_cy - arm, cx, body_cy + arm, cc, 1.5f);
                }
                else if (itype < 21.5f) // ZUI_ICON_GEAR = 21: outer ring + inner dot
                {
                    float gs = sz * 0.45f;
                    ZUIDrawListAddCircle(&ctx->DrawList, cx, cy, gs * 0.5f, cc, 8, 1.5f);
                    ZUIDrawListAddCircleFilled(&ctx->DrawList, cx, cy, gs * 0.2f, cc, 8);
                }
                else if (itype < 22.5f) // ZUI_ICON_GRID = 22: 3×3 grid lines, centered on cx/cy
                {
                    float gs2 = sz * 0.66f;
                    float gx0 = cx - gs2 * 0.5f, gy0 = cy - gs2 * 0.5f;
                    for (int gi = 1; gi <= 3; ++gi)
                    {
                        float tx = gx0 + gs2 * gi / 4.f, ty = gy0 + gs2 * gi / 4.f;
                        ZUIDrawListAddLine(&ctx->DrawList, tx, gy0, tx, gy0 + gs2, cc, 1.2f);
                        ZUIDrawListAddLine(&ctx->DrawList, gx0, ty, gx0 + gs2, ty, cc, 1.2f);
                    }
                }
                else if (itype < 23.5f) // ZUI_ICON_TRANSLATE = 23: 4-way arrow cross (port of develop icon_translate)
                {
                    float r = sz * 0.28f, al = r * 0.9f, aw = r * 0.35f, inset = aw * 0.3f;
                    ZUIDrawListAddLine(&ctx->DrawList, cx, cy - r, cx, cy + r, cc, 1.5f);
                    ZUIDrawListAddLine(&ctx->DrawList, cx - r, cy, cx + r, cy, cc, 1.5f);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx, cy - r - al, cx - aw, cy - r + inset, cx + aw, cy - r + inset, cc);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx, cy + r + al, cx - aw, cy + r - inset, cx + aw, cy + r - inset, cc);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx + r + al, cy, cx + r - inset, cy - aw, cx + r - inset, cy + aw, cc);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx - r - al, cy, cx - r + inset, cy - aw, cx - r + inset, cy + aw, cc);
                }
                else if (itype < 24.5f) // ZUI_ICON_ROTATE = 24: circle + arrowhead (port of develop icon_rotate)
                {
                    float r  = sz * 0.28f;
                    float aw = r * 0.40f;
                    ZUIDrawListAddCircle(&ctx->DrawList, cx, cy, r, cc, 24, 1.8f);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx + r, cy - aw * 0.4f, cx + r + aw * 0.7f, cy, cx + r, cy + aw * 0.4f, cc);
                }
                else if (itype < 25.5f) // ZUI_ICON_SCALE = 25: square + corner dots (port of develop icon_scale)
                {
                    float r = sz * 0.28f, h = r * 0.75f, dot = r * 0.22f;
                    ZUIDrawListAddRect(&ctx->DrawList, cx - h, cy - h, cx + h, cy + h, cc, 0.f, 0xF, 1.5f);
                    ZUIDrawListAddCircleFilled(&ctx->DrawList, cx - h, cy - h, dot, cc, 6);
                    ZUIDrawListAddCircleFilled(&ctx->DrawList, cx + h, cy - h, dot, cc, 6);
                    ZUIDrawListAddCircleFilled(&ctx->DrawList, cx - h, cy + h, dot, cc, 6);
                    ZUIDrawListAddCircleFilled(&ctx->DrawList, cx + h, cy + h, dot, cc, 6);
                }
                else if (itype > 29.5f && itype < 35.5f) // source-code icons 30-35
                {
                    // Badge color — Material Icon Theme palette
                    float br = 0.f, bg = 0.f, bb = 0.f;
                    if (itype < 30.5f)
                    {
                        br = 0.396f;
                        bg = 0.604f;
                        bb = 0.824f;
                    } // CPP  #659ad2
                    else if (itype < 31.5f)
                    {
                        br = 0.608f;
                        bg = 0.286f;
                        bb = 0.576f;
                    } // CS   #9b4993
                    else if (itype < 32.5f)
                    {
                        br = 0.945f;
                        bg = 0.878f;
                        bb = 0.353f;
                    } // JS   #f1e05a
                    else if (itype < 33.5f)
                    {
                        br = 0.208f;
                        bg = 0.447f;
                        bb = 0.647f;
                    } // PY   #3572a5
                    else if (itype < 34.5f)
                    {
                        br = 0.627f;
                        bg = 0.455f;
                        bb = 0.769f;
                    } // H    #a074c4
                    else
                    {
                        br = 0.796f;
                        bg = 0.796f;
                        bb = 0.255f;
                    } // JSON #cbcb41

                    // Badge geometry (used by all except JSON)
                    float bw = sz * 0.80f, bh = sz * 0.62f;
                    float bbx = cx - bw * 0.5f, bby = cy - bh * 0.5f;

                    // JSON (itype >= 34.5f): no badge, symbol drawn directly in yellow
                    bool  json_icon = (itype >= 34.5f);
                    if (!json_icon)
                        ZUIDrawListAddRectFilled(&ctx->DrawList, bbx, bby, bbx + bw, bby + bh, ZUIPackColor(br, bg, bb, 1.f), 5.f);

                    // Symbol: white on badge, or language color directly for JSON
                    uint32_t sym = json_icon ? ZUIPackColor(br, bg, bb, 1.f) : ZUIPackColor(1.f, 1.f, 1.f, 0.92f);
                    float    t   = fmaxf(sz * 0.04f, 1.2f); // line thickness
                    float    ps  = bh * 0.22f;              // symbol half-size

                    if (itype < 30.5f) // "++" — two plus signs
                    {
                        for (int pi = 0; pi < 2; ++pi)
                        {
                            float px = cx + (pi == 0 ? -bw * 0.20f : bw * 0.20f);
                            ZUIDrawListAddLine(&ctx->DrawList, px - ps, cy, px + ps, cy, sym, t);
                            ZUIDrawListAddLine(&ctx->DrawList, px, cy - ps, px, cy + ps, sym, t);
                        }
                    }
                    else if (itype < 31.5f) // "#" — two horiz + two vert, slightly offset
                    {
                        float g = ps * 0.45f;
                        ZUIDrawListAddLine(&ctx->DrawList, cx - ps, cy - g, cx + ps, cy - g, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx - ps, cy + g, cx + ps, cy + g, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx - g, cy - ps * 1.1f, cx - g, cy + ps * 1.1f, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx + g, cy - ps * 1.1f, cx + g, cy + ps * 1.1f, sym, t);
                    }
                    else if (itype < 32.5f) // ">" — right-pointing chevron
                    {
                        float ax = cx - ps * 0.4f;
                        ZUIDrawListAddLine(&ctx->DrawList, ax, cy - ps, cx + ps * 0.5f, cy, sym, t * 1.3f);
                        ZUIDrawListAddLine(&ctx->DrawList, ax, cy + ps, cx + ps * 0.5f, cy, sym, t * 1.3f);
                    }
                    else if (itype < 33.5f) // Python — diamond outline
                    {
                        ZUIDrawListAddLine(&ctx->DrawList, cx, cy - ps, cx + ps, cy, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx + ps, cy, cx, cy + ps, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx, cy + ps, cx - ps, cy, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx - ps, cy, cx, cy - ps, sym, t);
                    }
                    else if (itype < 34.5f) // "<>" header brackets
                    {
                        float g = ps * 0.55f;
                        ZUIDrawListAddLine(&ctx->DrawList, cx - g, cy - ps, cx - ps * 1.1f, cy, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx - ps * 1.1f, cy, cx - g, cy + ps, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx + g, cy - ps, cx + ps * 1.1f, cy, sym, t);
                        ZUIDrawListAddLine(&ctx->DrawList, cx + ps * 1.1f, cy, cx + g, cy + ps, sym, t);
                    }
                    else // "{}" JSON — no badge, yellow curved braces
                    {
                        float bh = sz * 0.42f; // half-height
                        float cw = sz * 0.17f; // cap horizontal extent
                        float cr = sz * 0.09f; // corner rounding
                        float nw = sz * 0.07f; // nub depth (small)
                        float nt = sz * 0.09f; // nub half-height
                        float t2 = fmaxf(sz * 0.05f, 1.8f);

                        // Left brace { — spine at lx, caps go right
                        float lx = cx - sz * 0.20f;
                        ZUIDrawListAddLine(&ctx->DrawList, lx + cw, cy - bh, lx + cr, cy - bh, sym, t2); // top cap
                        ZUIDrawListAddLine(&ctx->DrawList, lx + cr, cy - bh, lx, cy - bh + cr, sym, t2); // top corner
                        ZUIDrawListAddLine(&ctx->DrawList, lx, cy - bh + cr, lx, cy - nt, sym, t2);      // upper spine
                        ZUIDrawListAddLine(&ctx->DrawList, lx, cy - nt, lx - nw, cy, sym, t2);           // nub top
                        ZUIDrawListAddLine(&ctx->DrawList, lx - nw, cy, lx, cy + nt, sym, t2);           // nub bot
                        ZUIDrawListAddLine(&ctx->DrawList, lx, cy + nt, lx, cy + bh - cr, sym, t2);      // lower spine
                        ZUIDrawListAddLine(&ctx->DrawList, lx, cy + bh - cr, lx + cr, cy + bh, sym, t2); // bot corner
                        ZUIDrawListAddLine(&ctx->DrawList, lx + cr, cy + bh, lx + cw, cy + bh, sym, t2); // bot cap

                        // Right brace } — mirror
                        float rx = cx + sz * 0.20f;
                        ZUIDrawListAddLine(&ctx->DrawList, rx - cw, cy - bh, rx - cr, cy - bh, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx - cr, cy - bh, rx, cy - bh + cr, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx, cy - bh + cr, rx, cy - nt, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx, cy - nt, rx + nw, cy, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx + nw, cy, rx, cy + nt, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx, cy + nt, rx, cy + bh - cr, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx, cy + bh - cr, rx - cr, cy + bh, sym, t2);
                        ZUIDrawListAddLine(&ctx->DrawList, rx - cr, cy + bh, rx - cw, cy + bh, sym, t2);
                    }
                }
                else // ZUI_ICON_ACTOR = 15 (diamond) — default fallback
                {
                    float r = sz * 0.35f;
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx, cy - r, cx + r, cy, cx, cy + r, cc);
                    ZUIDrawListAddTriangleFilled(&ctx->DrawList, cx, cy - r, cx, cy + r, cx - r, cy, cc);
                }
            }
        }

        // Pop remaining clip rects
        while (clip_top > 0)
        {
            --clip_top;
            ZUIDrawListPopClipRect(&ctx->DrawList);
        }

        // Fill payload from draw list
        out->Vtx      = ctx->DrawList.Vtx;
        out->VtxCount = ctx->DrawList.VtxCount;
        out->Idx      = ctx->DrawList.Idx;
        out->IdxCount = ctx->DrawList.IdxCount;
        out->Cmds     = ctx->DrawList.Cmds;
        out->CmdCount = ctx->DrawList.CmdCount;
    }

    void ZUIPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_payload || m_vtx_upload == 0 || m_idx_upload == 0)
            return;

        auto  swapchain  = Device->SwapchainPtr;
        auto  current_fb = swapchain->SwapchainFramebuffers[swapchain->CurrentFrame->ImageIndex];
        auto* gp         = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, current_fb, false);
        RecordDraw(device, res_inspector, scene, pass, nullptr, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool ZUIPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_payload || m_vtx_upload == 0 || m_idx_upload == 0)
            return false;

        const ZUIRenderPayload& payload     = *m_payload;
        const uint32_t          frame_index = device->SwapchainPtr->CurrentFrame->Index;
        const uint32_t          fi          = frame_index % FRAMES_IN_FLIGHT;
        {
            auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
            command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
            command_buffer->BindPipeline(gp->Pipeline);
            command_buffer->BindVertexBuffer(VtxBHandles[fi]);
            command_buffer->BindIndexBuffer(IdxBHandles[fi], VK_INDEX_TYPE_UINT16);

            float fs = payload.FramebufferScale;

            for (uint32_t i = 0; i < payload.CmdCount; ++i)
            {
                const ZUIDrawListCmd& cmd = payload.Cmds[i];
                if (cmd.ElemCount == 0)
                {
                    continue;
                }
                // Skip commands that reference indices beyond the clamped upload range
                if (cmd.IdxOffset + cmd.ElemCount > m_idx_upload)
                {
                    continue;
                }

                // Logical → physical pixel scissor
                command_buffer->SetScissor((uint32_t) (cmd.ClipW * fs), (uint32_t) (cmd.ClipH * fs), (int32_t) (cmd.ClipX * fs), (int32_t) (cmd.ClipY * fs));

                ZUIDrawPushConstant pc = {};
                pc.Scale[0]            = payload.Scale[0];
                pc.Scale[1]            = payload.Scale[1];
                pc.Translate[0]        = payload.Translate[0];
                pc.Translate[1]        = payload.Translate[1];
                pc.TexIdx              = cmd.TexIdx;
                pc.FbScale             = payload.FramebufferScale;

                command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ZUIDrawPushConstant), &pc);
                command_buffer->BindDescriptorSets(frame_index);
                command_buffer->DrawIndexed(cmd.ElemCount, 1, cmd.IdxOffset, 0, 0);
            }
        }
        return true;
    }

} // namespace ZEngine::Rendering::Renderers
