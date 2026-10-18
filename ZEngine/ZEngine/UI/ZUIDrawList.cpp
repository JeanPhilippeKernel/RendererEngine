#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <algorithm>
#include <cmath>
#include <type_traits>

using namespace ZEngine::Core::Memory;

namespace ZEngine::UI
{
    // Constants matching ImGui's geometry quality
    static constexpr float kPI                        = 3.14159265358979f;
    static constexpr float kCircleMaxError            = 0.30f; // px
    static constexpr int   kArcFastSize               = 48;    // unit circle LUT size

    // 48-sample unit circle LUT (precomputed, matches ImGui ArcFastVtx)
    static float           s_CircleLUTX[kArcFastSize] = {};
    static float           s_CircleLUTY[kArcFastSize] = {};
    static bool            s_CircleLUTInited          = false;

    static void            InitCircleLUT()
    {
        if (s_CircleLUTInited)
            return;
        for (int i = 0; i < kArcFastSize; ++i)
        {
            float a         = (float) i * (2.f * kPI) / (float) kArcFastSize;
            s_CircleLUTX[i] = cosf(a);
            s_CircleLUTY[i] = sinf(a);
        }
        s_CircleLUTInited = true;
    }

    // Compute segment count for a circle of given radius
    static int CircleSegments(float radius)
    {
        if (!std::isfinite(radius) || radius <= 0.f)
            return 4;
        // Tiny radii can leave acos's domain; enormous radii round its input
        // to one. Clamp before acos and before converting the count to int.
        const double angle = std::acos(std::clamp(1.0 - double(kCircleMaxError) / radius, -1.0, 1.0));
        const double count = angle > 0.0 ? std::ceil(double(kPI) / angle) : 512.0;
        const int    n     = static_cast<int>(std::clamp(count, 4.0, 512.0));
        return n & ~1; // round to even
    }

    // Allocation helpers

    // Buffer capacity is not live data. Every emitted vertex, index, command
    // and path coordinate is fully assigned before its count is advanced; do
    // not zero megabytes of unused capacity every frame. Metadata and command
    // descriptors still use value initialization, including nonzero defaults.
    template <typename T>
    static T* AllocateDrawBuffer(ArenaAllocator* arena, uint32_t count)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        auto* buffer = static_cast<T*>(arena->AllocateNoZero(sizeof(T) * count, ZAlignof(T)));
        ZENGINE_VALIDATE_ASSERT(buffer != nullptr, "ZUI draw buffer arena exhausted");
        return buffer;
    }

    static bool CanAppendGeometry(const ZUIDrawList* dl, uint32_t vertices, uint32_t indices)
    {
        return dl->VtxCount <= ZUIDrawList::VertexLimit && vertices <= ZUIDrawList::VertexLimit - dl->VtxCount && dl->IdxCount <= ZUIDrawList::IndexLimit && indices <= ZUIDrawList::IndexLimit - dl->IdxCount;
    }

    static void GrowVtx(ZUIDrawList* dl, ArenaAllocator* arena, uint32_t needed)
    {
        if (dl->VtxCount + needed <= dl->VtxCapacity)
            return;
        uint32_t new_cap = dl->VtxCapacity ? dl->VtxCapacity * 2 : 4096;
        while (new_cap < dl->VtxCount + needed)
            new_cap *= 2;
        new_cap        = std::min(new_cap, ZUIDrawList::VertexLimit);
        ZUIDrawVtx* nb = AllocateDrawBuffer<ZUIDrawVtx>(arena, new_cap);
        if (dl->Vtx)
            Helpers::secure_memcpy(nb, new_cap * sizeof(ZUIDrawVtx), dl->Vtx, dl->VtxCount * sizeof(ZUIDrawVtx));
        dl->Vtx         = nb;
        dl->VtxCapacity = new_cap;
    }

    static void GrowIdx(ZUIDrawList* dl, ArenaAllocator* arena, uint32_t needed)
    {
        if (dl->IdxCount + needed <= dl->IdxCapacity)
            return;
        uint32_t new_cap = dl->IdxCapacity ? dl->IdxCapacity * 2 : 8192;
        while (new_cap < dl->IdxCount + needed)
            new_cap *= 2;
        new_cap      = std::min(new_cap, ZUIDrawList::IndexLimit);
        uint16_t* nb = AllocateDrawBuffer<uint16_t>(arena, new_cap);
        if (dl->Idx)
            Helpers::secure_memcpy(nb, new_cap * sizeof(uint16_t), dl->Idx, dl->IdxCount * sizeof(uint16_t));
        dl->Idx         = nb;
        dl->IdxCapacity = new_cap;
    }

    static void GrowPath(ZUIDrawList* dl, ArenaAllocator* arena, uint32_t needed)
    {
        if (dl->PathCount + needed <= dl->PathCap)
            return;
        uint32_t new_cap = dl->PathCap ? dl->PathCap * 2 : 256;
        while (new_cap < dl->PathCount + needed)
            new_cap *= 2;
        float* nx = AllocateDrawBuffer<float>(arena, new_cap);
        float* ny = AllocateDrawBuffer<float>(arena, new_cap);
        if (dl->PathX)
        {
            Helpers::secure_memcpy(nx, new_cap * sizeof(float), dl->PathX, dl->PathCount * sizeof(float));
            Helpers::secure_memcpy(ny, new_cap * sizeof(float), dl->PathY, dl->PathCount * sizeof(float));
        }
        dl->PathX   = nx;
        dl->PathY   = ny;
        dl->PathCap = new_cap;
    }

    // Lifecycle

    void ZUIDrawListInit(ZUIDrawList* dl, ArenaAllocator* frame_arena, uint32_t vtx_cap, uint32_t idx_cap, float white_u, float white_v, uint32_t atlas_idx)
    {
        InitCircleLUT();
        dl->Arena       = frame_arena;
        dl->VtxCapacity = std::min(vtx_cap, ZUIDrawList::VertexLimit);
        dl->IdxCapacity = std::min(idx_cap, ZUIDrawList::IndexLimit);
        dl->Vtx         = dl->VtxCapacity ? AllocateDrawBuffer<ZUIDrawVtx>(frame_arena, dl->VtxCapacity) : nullptr;
        dl->Idx         = dl->IdxCapacity ? AllocateDrawBuffer<uint16_t>(frame_arena, dl->IdxCapacity) : nullptr;
        // Retain bounded growth headroom across frames, just like the renderer's
        // index buffer. No stale buffer contents or pointers are reused.
        dl->CmdCapacity = std::clamp(dl->CmdCapacity, 512u, ZUIDrawList::VertexLimit);
        dl->Cmds        = AllocateDrawBuffer<ZUIDrawListCmd>(frame_arena, dl->CmdCapacity);
        dl->PathCap     = std::clamp(dl->PathCap, 256u, ZUIDrawList::VertexLimit);
        dl->PathX       = AllocateDrawBuffer<float>(frame_arena, dl->PathCap);
        dl->PathY       = AllocateDrawBuffer<float>(frame_arena, dl->PathCap);
        dl->WhiteU      = white_u;
        dl->WhiteV      = white_v;
        dl->AtlasTexIdx = atlas_idx;
        dl->FringeScale = 1.0f;
        ZUIDrawListReset(dl);
    }

    void ZUIDrawListReset(ZUIDrawList* dl)
    {
        dl->VtxCount  = 0;
        dl->IdxCount  = 0;
        dl->CmdCount  = 0;
        dl->PathCount = 0;
        dl->ClipDepth = 0;
        // Open a default command with no clip
        if (dl->Cmds && dl->CmdCapacity > 0)
        {
            dl->CmdCount       = 1;
            dl->Cmds[0]        = {};
            dl->Cmds[0].TexIdx = dl->AtlasTexIdx;
        }
    }

    // Ensure current command matches clip + texture + effect. Never append shadow
    // geometry to a textured batch (or vice versa), even after the initial capacity.

    static void EnsureCmd(ZUIDrawList* dl, float cx0, float cy0, float cx1, float cy1, uint32_t tex_idx, const ZUIShadowDrawParams& shadow = {}, const ZUIColorFieldDrawParams& color_field = {})
    {
        ZUIDrawListCmd nc = {};
        nc.ClipX          = cx0;
        nc.ClipY          = cy0;
        nc.ClipW          = cx1 - cx0;
        nc.ClipH          = cy1 - cy0;
        nc.TexIdx         = tex_idx;
        nc.Shadow         = shadow;
        nc.ColorField     = color_field;
        nc.IdxOffset      = dl->IdxCount;

        if (dl->CmdCount > 0)
        {
            ZUIDrawListCmd& cur = dl->Cmds[dl->CmdCount - 1];
            if (cur.ClipX == nc.ClipX && cur.ClipY == nc.ClipY && cur.ClipW == nc.ClipW && cur.ClipH == nc.ClipH && cur.TexIdx == tex_idx && cur.Shadow.HalfSize[0] == shadow.HalfSize[0] && cur.Shadow.HalfSize[1] == shadow.HalfSize[1] && cur.Shadow.Radius == shadow.Radius && cur.Shadow.Sigma == shadow.Sigma && cur.ColorField == color_field)
                return;
            if (cur.ElemCount == 0)
            {
                cur = nc;
                return;
            }
        }
        if (dl->CmdCount >= dl->CmdCapacity)
        {
            const uint32_t capacity = dl->CmdCapacity ? dl->CmdCapacity * 2 : 512;
            auto*          cmds     = AllocateDrawBuffer<ZUIDrawListCmd>(dl->Arena, capacity);
            if (dl->CmdCount > 0)
                Helpers::secure_memcpy(cmds, capacity * sizeof(ZUIDrawListCmd), dl->Cmds, dl->CmdCount * sizeof(ZUIDrawListCmd));
            dl->Cmds        = cmds;
            dl->CmdCapacity = capacity;
        }
        dl->Cmds[dl->CmdCount++] = nc;
    }

    static void GetCurrentClip(const ZUIDrawList* dl, float& x0, float& y0, float& x1, float& y1)
    {
        if (dl->ClipDepth > 0)
        {
            const float* r = dl->ClipStack[dl->ClipDepth - 1];
            x0             = r[0];
            y0             = r[1];
            x1             = r[2];
            y1             = r[3];
        }
        else
        {
            x0 = -1e9f;
            y0 = -1e9f;
            x1 = 1e9f;
            y1 = 1e9f;
        }
    }

    static void FlushCmd(ZUIDrawList* dl)
    {
        float cx0 = 0.f, cy0 = 0.f, cx1 = 0.f, cy1 = 0.f;
        GetCurrentClip(dl, cx0, cy0, cx1, cy1);
        EnsureCmd(dl, cx0, cy0, cx1, cy1, dl->AtlasTexIdx);
    }

    // Clip rect stack

    void ZUIDrawListPushClipRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, bool intersect)
    {
        if (intersect && dl->ClipDepth > 0)
        {
            const float* prev = dl->ClipStack[dl->ClipDepth - 1];
            if (x0 < prev[0])
                x0 = prev[0];
            if (y0 < prev[1])
                y0 = prev[1];
            if (x1 > prev[2])
                x1 = prev[2];
            if (y1 > prev[3])
                y1 = prev[3];
        }
        if (dl->ClipDepth < ZUIDrawList::kMaxClipDepth)
        {
            float* slot = dl->ClipStack[dl->ClipDepth++];
            slot[0]     = x0;
            slot[1]     = y0;
            slot[2]     = x1;
            slot[3]     = y1;
        }
        FlushCmd(dl);
    }

    void ZUIDrawListPopClipRect(ZUIDrawList* dl)
    {
        if (dl->ClipDepth > 0)
            --dl->ClipDepth;
        FlushCmd(dl);
    }

    // Fast flat colored rect (no AA, no rounding) — 4 vtx, 6 idx

    void ZUIDrawListAddRectFilledNoAA(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col)
    {
        if (!CanAppendGeometry(dl, 4, 6))
            return;
        FlushCmd(dl);
        GrowVtx(dl, dl->Arena, 4);
        GrowIdx(dl, dl->Arena, 6);
        uint16_t    base                      = (uint16_t) dl->VtxCount;
        ZUIDrawVtx* v                         = dl->Vtx + dl->VtxCount;
        uint16_t*   i                         = dl->Idx + dl->IdxCount;
        v[0]                                  = {x0, y0, dl->WhiteU, dl->WhiteV, col};
        v[1]                                  = {x1, y0, dl->WhiteU, dl->WhiteV, col};
        v[2]                                  = {x1, y1, dl->WhiteU, dl->WhiteV, col};
        v[3]                                  = {x0, y1, dl->WhiteU, dl->WhiteV, col};
        i[0]                                  = base;
        i[1]                                  = (uint16_t) (base + 1);
        i[2]                                  = (uint16_t) (base + 2);
        i[3]                                  = base;
        i[4]                                  = (uint16_t) (base + 2);
        i[5]                                  = (uint16_t) (base + 3);
        dl->VtxCount                         += 4;
        dl->IdxCount                         += 6;
        dl->Cmds[dl->CmdCount - 1].ElemCount += 6;
    }

    void ZUIDrawListAddShadow(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float rounding, float blur)
    {
        if (!(x1 > x0) || !(y1 > y0) || !(blur > 0.f) || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(blur) || !std::isfinite(rounding) || (col >> 24) == 0 || !CanAppendGeometry(dl, 4, 6))
            return;

        ZUIShadowDrawParams shadow = {};
        shadow.HalfSize[0]         = (x1 - x0) * 0.5f;
        shadow.HalfSize[1]         = (y1 - y0) * 0.5f;
        shadow.Radius              = std::clamp(rounding, 0.f, std::min(shadow.HalfSize[0], shadow.HalfSize[1]));
        shadow.Sigma               = blur * 0.5f;
        const float pad            = shadow.Sigma * 3.f;
        const float u              = shadow.HalfSize[0] + pad;
        const float v              = shadow.HalfSize[1] + pad;
        if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(x0 - pad) || !std::isfinite(x1 + pad) || !std::isfinite(y0 - pad) || !std::isfinite(y1 + pad))
            return;
        float cx0 = 0.f, cy0 = 0.f, cx1 = 0.f, cy1 = 0.f;
        GetCurrentClip(dl, cx0, cy0, cx1, cy1);
        // Avoid a draw and fragment work for fully clipped shadows.
        if (x1 + pad <= cx0 || y1 + pad <= cy0 || x0 - pad >= cx1 || y0 - pad >= cy1)
            return;
        EnsureCmd(dl, cx0, cy0, cx1, cy1, dl->AtlasTexIdx, shadow);
        GrowVtx(dl, dl->Arena, 4);
        GrowIdx(dl, dl->Arena, 6);
        const uint16_t base                   = (uint16_t) dl->VtxCount;
        ZUIDrawVtx*    vertices               = dl->Vtx + dl->VtxCount;
        uint16_t*      indices                = dl->Idx + dl->IdxCount;
        // UVs are local logical coordinates for the shadow shader, not atlas UVs.
        vertices[0]                           = {x0 - pad, y0 - pad, -u, -v, col};
        vertices[1]                           = {x1 + pad, y0 - pad, u, -v, col};
        vertices[2]                           = {x1 + pad, y1 + pad, u, v, col};
        vertices[3]                           = {x0 - pad, y1 + pad, -u, v, col};
        indices[0]                            = base;
        indices[1]                            = (uint16_t) (base + 1);
        indices[2]                            = (uint16_t) (base + 2);
        indices[3]                            = base;
        indices[4]                            = (uint16_t) (base + 2);
        indices[5]                            = (uint16_t) (base + 3);
        dl->VtxCount                         += 4;
        dl->IdxCount                         += 6;
        dl->Cmds[dl->CmdCount - 1].ElemCount += 6;
    }

    void ZUIDrawListAddColorField(ZUIDrawList* dl, float cx, float cy, float radius, float brightness, float lightness, bool lightness_mode)
    {
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius) || !(radius > 0.f) || !std::isfinite(brightness) || !std::isfinite(lightness) || !CanAppendGeometry(dl, 4, 6))
            return;
        const float extent = radius + 1.f;
        const float uv     = extent / radius;
        if (!std::isfinite(uv) || !std::isfinite(cx - extent) || !std::isfinite(cx + extent) || !std::isfinite(cy - extent) || !std::isfinite(cy + extent))
            return;
        float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
        GetCurrentClip(dl, x0, y0, x1, y1);
        if (cx + extent <= x0 || cx - extent >= x1 || cy + extent <= y0 || cy - extent >= y1)
            return;
        const ZUIColorFieldDrawParams field = {std::clamp(brightness, 0.f, 1.f), std::clamp(lightness, 0.f, 1.f), lightness_mode ? 1.f : 0.f, 1.f};
        EnsureCmd(dl, x0, y0, x1, y1, dl->AtlasTexIdx, {}, field);
        GrowVtx(dl, dl->Arena, 4);
        GrowIdx(dl, dl->Arena, 6);
        const uint16_t     base   = static_cast<uint16_t>(dl->VtxCount);
        constexpr uint32_t white  = 0xFFFFFFFFu;
        dl->Vtx[dl->VtxCount++]   = {cx - extent, cy - extent, -uv, -uv, white};
        dl->Vtx[dl->VtxCount++]   = {cx + extent, cy - extent, uv, -uv, white};
        dl->Vtx[dl->VtxCount++]   = {cx + extent, cy + extent, uv, uv, white};
        dl->Vtx[dl->VtxCount++]   = {cx - extent, cy + extent, -uv, uv, white};
        const uint16_t indices[6] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2), base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)};
        Helpers::secure_memcpy(dl->Idx + dl->IdxCount, (dl->IdxCapacity - dl->IdxCount) * sizeof(uint16_t), indices, sizeof(indices));
        dl->IdxCount                         += 6;
        dl->Cmds[dl->CmdCount - 1].ElemCount += 6;
    }

    void ZUIDrawListAddColorRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, const uint32_t* stops, int count, float rounding, bool checker, bool vertical, bool split)
    {
        if (!stops || count < 2 || count > 16 || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(rounding) || !(x1 > x0) || !(y1 > y0))
            return;
        const float hw = (x1 - x0) * .5f, hh = (y1 - y0) * .5f;
        if (!std::isfinite(hw) || !std::isfinite(hh))
            return;
        const int intervals = split ? 2 : count - 1;
        if (!CanAppendGeometry(dl, static_cast<uint32_t>(intervals * 4), static_cast<uint32_t>(intervals * 6)))
            return;
        float cx0 = 0.f, cy0 = 0.f, cx1 = 0.f, cy1 = 0.f;
        GetCurrentClip(dl, cx0, cy0, cx1, cy1);
        if (x1 <= cx0 || x0 >= cx1 || y1 <= cy0 || y0 >= cy1)
            return;
        const ZUIShadowDrawParams shape = {
            {hw, hh},
            std::clamp(rounding, 0.f, std::min(hw, hh)), 0.f
        };
        const ZUIColorFieldDrawParams paint = {0.f, 0.f, checker ? 1.f : 0.f, 2.f};
        EnsureCmd(dl, cx0, cy0, cx1, cy1, dl->AtlasTexIdx, shape, paint);
        GrowVtx(dl, dl->Arena, static_cast<uint32_t>(intervals * 4));
        GrowIdx(dl, dl->Arena, static_cast<uint32_t>(intervals * 6));
        for (int segment = 0; segment < intervals; ++segment)
        {
            const float    t0 = static_cast<float>(segment) / intervals, t1 = static_cast<float>(segment + 1) / intervals;
            const float    left = vertical ? x0 : x0 + (x1 - x0) * t0, right = vertical ? x1 : x0 + (x1 - x0) * t1;
            const float    top = vertical ? y0 + (y1 - y0) * t0 : y0, bottom = vertical ? y0 + (y1 - y0) * t1 : y1;
            const uint32_t first = stops[segment], last = split ? first : stops[segment + 1];
            const uint16_t base      = static_cast<uint16_t>(dl->VtxCount);
            // UV is position relative to the complete rounded shape, not each
            // interval: internal colour stops must never introduce rounded seams.
            dl->Vtx[dl->VtxCount++]  = {left, top, left - x0 - hw, top - y0 - hh, first};
            dl->Vtx[dl->VtxCount++]  = {right, top, right - x0 - hw, top - y0 - hh, vertical ? first : last};
            dl->Vtx[dl->VtxCount++]  = {right, bottom, right - x0 - hw, bottom - y0 - hh, last};
            dl->Vtx[dl->VtxCount++]  = {left, bottom, left - x0 - hw, bottom - y0 - hh, vertical ? last : first};
            const uint16_t indices[] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2), base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)};
            Helpers::secure_memcpy(dl->Idx + dl->IdxCount, (dl->IdxCapacity - dl->IdxCount) * sizeof(uint16_t), indices, sizeof(indices));
            dl->IdxCount                         += 6;
            dl->Cmds[dl->CmdCount - 1].ElemCount += 6;
        }
    }

    void ZUIDrawListAddRectFilledMultiColor(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col_tl, uint32_t col_tr, uint32_t col_bl, uint32_t col_br)
    {
        if (!CanAppendGeometry(dl, 4, 6))
            return;
        FlushCmd(dl);
        GrowVtx(dl, dl->Arena, 4);
        GrowIdx(dl, dl->Arena, 6);
        uint16_t    base = (uint16_t) dl->VtxCount;
        ZUIDrawVtx* v    = dl->Vtx + dl->VtxCount;
        uint16_t*   i    = dl->Idx + dl->IdxCount;
        float       wu = dl->WhiteU, wv = dl->WhiteV;
        v[0]                                  = {x0, y0, wu, wv, col_tl};
        v[1]                                  = {x1, y0, wu, wv, col_tr};
        v[2]                                  = {x1, y1, wu, wv, col_br};
        v[3]                                  = {x0, y1, wu, wv, col_bl};
        i[0]                                  = base;
        i[1]                                  = (uint16_t) (base + 1);
        i[2]                                  = (uint16_t) (base + 2);
        i[3]                                  = base;
        i[4]                                  = (uint16_t) (base + 2);
        i[5]                                  = (uint16_t) (base + 3);
        dl->VtxCount                         += 4;
        dl->IdxCount                         += 6;
        dl->Cmds[dl->CmdCount - 1].ElemCount += 6;
    }

    // Path helpers

    static void PathClear(ZUIDrawList* dl)
    {
        dl->PathCount = 0;
    }

    static void PathLineTo(ZUIDrawList* dl, float x, float y)
    {
        GrowPath(dl, dl->Arena, 1);
        dl->PathX[dl->PathCount] = x;
        dl->PathY[dl->PathCount] = y;
        ++dl->PathCount;
    }

    // Append arc to path using the fast 48-sample LUT.
    // angle_min/max in turns [0..1], a_min_sample/a_max_sample in LUT indices.
    static void PathArcToFast(ZUIDrawList* dl, float cx, float cy, float r, int a_min, int a_max)
    {
        if (r <= 0.f || a_min > a_max)
        {
            PathLineTo(dl, cx, cy);
            return;
        }
        GrowPath(dl, dl->Arena, a_max - a_min + 1);
        for (int i = a_min; i <= a_max; ++i)
        {
            int idx                  = i % kArcFastSize;
            dl->PathX[dl->PathCount] = cx + s_CircleLUTX[idx] * r;
            dl->PathY[dl->PathCount] = cy + s_CircleLUTY[idx] * r;
            ++dl->PathCount;
        }
    }

    // Append rounded rect path. round_flags: bit 0=TL 1=TR 2=BR 3=BL
    static void PathRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, float r, uint32_t flags = 0xF)
    {
        if (r < 0.5f)
        {
            flags = 0;
        }
        bool  tl   = (flags & 1) != 0;
        bool  tr   = (flags & 2) != 0;
        bool  br   = (flags & 4) != 0;
        bool  bl   = (flags & 8) != 0;
        // clamp radius
        float half = (x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0) * 0.5f;
        if (r > half)
            r = half;
        // 48-step LUT indices matching ImGui's 12-step convention (multiply by 4):
        // TL: 24-36 (180°→270° = left→up),  TR: 36-48 (270°→360° = up→right)
        // BR:  0-12 (  0°→ 90° = right→down), BL: 12-24 ( 90°→180° = down→left)
        if (tl)
        {
            PathArcToFast(dl, x0 + r, y0 + r, r, 24, 36);
        }
        else
            PathLineTo(dl, x0, y0);
        if (tr)
        {
            PathArcToFast(dl, x1 - r, y0 + r, r, 36, 48);
        }
        else
            PathLineTo(dl, x1, y0);
        if (br)
        {
            PathArcToFast(dl, x1 - r, y1 - r, r, 0, 12);
        }
        else
            PathLineTo(dl, x1, y1);
        if (bl)
        {
            PathArcToFast(dl, x0 + r, y1 - r, r, 12, 24);
        }
        else
            PathLineTo(dl, x0, y1);
    }

    // PathFillConvex — AA filled polygon (ImGui AddConvexPolyFilled)
    static void PathFillConvex(ZUIDrawList* dl, uint32_t col)
    {
        int n = (int) dl->PathCount;
        if (n < 3 || n > static_cast<int>(ZUIDrawList::VertexLimit / 2) || !CanAppendGeometry(dl, static_cast<uint32_t>(n * 2), static_cast<uint32_t>((n - 2) * 3 + n * 6)))
        {
            PathClear(dl);
            return;
        }

        float    fs        = dl->FringeScale;
        uint32_t col_trans = col & 0x00FFFFFFu; // alpha = 0

        FlushCmd(dl);
        GrowVtx(dl, dl->Arena, (uint32_t) (n * 2));
        GrowIdx(dl, dl->Arena, (uint32_t) ((n - 2) * 3 + n * 6));

        uint16_t    base = (uint16_t) dl->VtxCount;
        ZUIDrawVtx* vw   = dl->Vtx + dl->VtxCount;
        uint16_t*   iw   = dl->Idx + dl->IdxCount;
        float       wu = dl->WhiteU, wv = dl->WhiteV;

        // Compute per-vertex normals (dm = miter direction)
        // dm[i] = average of normals of edge (i-1,i) and edge (i,i+1)
        GrowPath(dl, dl->Arena, n);             // One normal per point in each coordinate array.
        float* dmx = dl->PathX + dl->PathCount; // NOT modifying PathCount, using tail as scratch
        float* dmy = dl->PathY + dl->PathCount;

        for (int i = 0; i < n; ++i)
        {
            int   ni  = (i + 1) % n;
            float ex  = dl->PathX[ni] - dl->PathX[i];
            float ey  = dl->PathY[ni] - dl->PathY[i];
            float len = sqrtf(ex * ex + ey * ey);
            if (len > 1e-6f)
            {
                ex /= len;
                ey /= len;
            }
            // Store edge normal (rotated 90° inward)
            dmx[i] = ey;
            dmy[i] = -ex;
        }
        // Average adjacent normals without overwriting the edge normals: each
        // vertex must see the same unmodified pair, including the closing edge.
        for (int i = 0; i < n; ++i)
        {
            int   pi    = (i + n - 1) % n;
            float avg_x = (dmx[i] + dmx[pi]) * 0.5f;
            float avg_y = (dmy[i] + dmy[pi]) * 0.5f;
            float dot   = avg_x * avg_x + avg_y * avg_y;
            if (dot > 1e-9f)
            {
                float inv = 1.f / dot;
                if (inv > 100.f)
                    inv = 100.f; // match ImGui IM_FIXNORMAL2F_MAX_INVLEN2
                avg_x *= inv;
                avg_y *= inv;
            }
            const float offset_x = avg_x * fs * 0.5f;
            const float offset_y = avg_y * fs * 0.5f;
            // Emit inner fill and outer fringe while normals are still intact.
            float       px = dl->PathX[i], py = dl->PathY[i];
            vw[i * 2 + 0] = {px - offset_x, py - offset_y, wu, wv, col};
            vw[i * 2 + 1] = {px + offset_x, py + offset_y, wu, wv, col_trans};
        }
        dl->VtxCount      += (uint32_t) (n * 2);

        // Fill indices: fan over inner vertices
        uint32_t fill_idx  = (uint32_t) ((n - 2) * 3);
        for (int i = 2; i < n; ++i)
        {
            iw[0]  = base;
            iw[1]  = (uint16_t) (base + (uint16_t) ((i - 1) * 2));
            iw[2]  = (uint16_t) (base + (uint16_t) (i * 2));
            iw    += 3;
        }
        // Fringe quads
        for (int i = 0; i < n; ++i)
        {
            int      ni  = (i + 1) % n;
            uint16_t i0  = (uint16_t) (base + (uint16_t) (i * 2));
            uint16_t i1  = (uint16_t) (base + (uint16_t) (i * 2 + 1));
            uint16_t i2  = (uint16_t) (base + (uint16_t) (ni * 2 + 1));
            uint16_t i3  = (uint16_t) (base + (uint16_t) (ni * 2));
            iw[0]        = i0;
            iw[1]        = i1;
            iw[2]        = i2;
            iw[3]        = i0;
            iw[4]        = i2;
            iw[5]        = i3;
            iw          += 6;
        }
        uint32_t total_idx                    = (uint32_t) (fill_idx + (uint32_t) (n * 6));
        dl->IdxCount                         += total_idx;
        dl->Cmds[dl->CmdCount - 1].ElemCount += total_idx;

        PathClear(dl);
    }

    // PathStroke — AA stroked polyline (ImGui AddPolyline)
    // closed: whether last point connects back to first
    static void PathStroke(ZUIDrawList* dl, uint32_t col, bool closed, float thickness)
    {
        int n     = (int) dl->PathCount;
        int count = closed ? n : n - 1;
        if (count <= 0 || n < 2 || n > static_cast<int>(ZUIDrawList::VertexLimit / 4) || !CanAppendGeometry(dl, static_cast<uint32_t>(n * 4), static_cast<uint32_t>(count * 18)))
        {
            PathClear(dl);
            return;
        }

        float    fs        = dl->FringeScale;
        // Coverage is centred on the requested edge, not added outside an
        // already full-width opaque stroke (which made 1 px borders look 3 px).
        float    half      = std::max(0.f, thickness - fs) * 0.5f;
        uint32_t col_trans = col & 0x00FFFFFFu;
        float    wu = dl->WhiteU, wv = dl->WhiteV;

        // Pass 1: compute per-segment left normals (outward = left of direction)
        // Matches ImGui AddPolyline temp_normals[] pattern exactly.
        GrowPath(dl, dl->Arena, (uint32_t) (n)); // Exactly one extra normal per point per coordinate array.
        float* snx = dl->PathX + dl->PathCount;  // segment normal X (scratch after path pts)
        float* sny = dl->PathY + dl->PathCount;

        for (int i = 0; i < n; ++i)
        {
            int   j   = (i + 1) < n ? i + 1 : 0; // next index (wraps for closed)
            float dx  = dl->PathX[j] - dl->PathX[i];
            float dy  = dl->PathY[j] - dl->PathY[i];
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-6f)
                len = 1.f;
            snx[i] = dy / len; // left normal
            sny[i] = -dx / len;
        }
        // For open lines: copy last segment normal to last vertex
        // (endpoint inherits its only adjacent segment, not the phantom wrap)
        if (!closed)
        {
            snx[n - 1] = snx[n - 2];
            sny[n - 1] = sny[n - 2];
        }

        // Pass 2: per vertex — average adjacent segment normals (miter),
        // apply IM_FIXNORMAL2F with 100.0 cap, split into inner + outer fringe.
        // 4 vertices per point: outer-fringe-left, inner-left, inner-right, outer-fringe-right
        FlushCmd(dl);
        GrowVtx(dl, dl->Arena, (uint32_t) (n * 4));
        GrowIdx(dl, dl->Arena, (uint32_t) (count * 18));

        uint16_t    base = (uint16_t) dl->VtxCount;
        ZUIDrawVtx* vw   = dl->Vtx + dl->VtxCount;
        uint16_t*   iw   = dl->Idx + dl->IdxCount;

        for (int i = 0; i < n; ++i)
        {
            // Average with previous segment normal (miter)
            int   prev = closed ? (i + n - 1) % n : (i > 0 ? i - 1 : 0);
            float dm_x = (snx[prev] + snx[i]) * 0.5f;
            float dm_y = (sny[prev] + sny[i]) * 0.5f;

            // IM_FIXNORMAL2F: scale by 1/|dm|² capped at 100 to handle sharp angles
            float d2   = dm_x * dm_x + dm_y * dm_y;
            if (d2 > 1e-6f)
            {
                float inv = 1.f / d2;
                if (inv > 100.f)
                    inv = 100.f;
                dm_x *= inv;
                dm_y *= inv;
            }

            float x = dl->PathX[i], y = dl->PathY[i];
            float ox = dm_x * (half + fs), oy = dm_y * (half + fs); // outer (fringe)
            float ix = dm_x * half, iy = dm_y * half;               // inner

            vw[i * 4 + 0] = {x + ox, y + oy, wu, wv, col_trans}; // outer-left AA
            vw[i * 4 + 1] = {x + ix, y + iy, wu, wv, col};       // inner-left
            vw[i * 4 + 2] = {x - ix, y - iy, wu, wv, col};       // inner-right
            vw[i * 4 + 3] = {x - ox, y - oy, wu, wv, col_trans}; // outer-right AA
        }
        dl->VtxCount += (uint32_t) (n * 4);

        // Indices: 3 quads (18 indices) per segment
        for (int i1 = 0; i1 < count; ++i1)
        {
            int      i2  = (i1 + 1) < n ? i1 + 1 : 0;
            uint16_t b1  = (uint16_t) (base + (uint16_t) (i1 * 4));
            uint16_t b2  = (uint16_t) (base + (uint16_t) (i2 * 4));
            // left fringe quad
            iw[0]        = b1;
            iw[1]        = (uint16_t) (b1 + 1);
            iw[2]        = (uint16_t) (b2 + 1);
            iw[3]        = b1;
            iw[4]        = (uint16_t) (b2 + 1);
            iw[5]        = b2;
            // fill quad
            iw[6]        = (uint16_t) (b1 + 1);
            iw[7]        = (uint16_t) (b1 + 2);
            iw[8]        = (uint16_t) (b2 + 2);
            iw[9]        = (uint16_t) (b1 + 1);
            iw[10]       = (uint16_t) (b2 + 2);
            iw[11]       = (uint16_t) (b2 + 1);
            // right fringe quad
            iw[12]       = (uint16_t) (b1 + 2);
            iw[13]       = (uint16_t) (b1 + 3);
            iw[14]       = (uint16_t) (b2 + 3);
            iw[15]       = (uint16_t) (b1 + 2);
            iw[16]       = (uint16_t) (b2 + 3);
            iw[17]       = (uint16_t) (b2 + 2);
            iw          += 18;
        }
        uint32_t total_idx                    = (uint32_t) (count * 18);
        dl->IdxCount                         += total_idx;
        dl->Cmds[dl->CmdCount - 1].ElemCount += total_idx;

        PathClear(dl);
    }

    // Public shape functions

    void ZUIDrawListAddLine(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float thickness)
    {
        PathLineTo(dl, x0 + 0.5f, y0 + 0.5f);
        PathLineTo(dl, x1 + 0.5f, y1 + 0.5f);
        PathStroke(dl, col, false, thickness);
    }

    void ZUIDrawListAddChevronDown(ZUIDrawList* dl, float cx, float cy, float half_w, float half_h, uint32_t col, float thickness)
    {
        PathLineTo(dl, cx - half_w + 0.5f, cy - half_h + 0.5f); // top-left
        PathLineTo(dl, cx + 0.5f, cy + half_h + 0.5f);          // apex (bottom-center)
        PathLineTo(dl, cx + half_w + 0.5f, cy - half_h + 0.5f); // top-right
        PathStroke(dl, col, false, thickness);
    }

    void ZUIDrawListAddChevronRight(ZUIDrawList* dl, float cx, float cy, float half_w, float half_h, uint32_t col, float thickness)
    {
        // 90° CW rotation of ChevronDown: half_w → vertical span, half_h → horizontal span
        PathLineTo(dl, cx - half_h + 0.5f, cy - half_w + 0.5f); // top-left
        PathLineTo(dl, cx + half_h + 0.5f, cy + 0.5f);          // apex (right-center)
        PathLineTo(dl, cx - half_h + 0.5f, cy + half_w + 0.5f); // bottom-left
        PathStroke(dl, col, false, thickness);
    }

    void ZUIDrawListAddPolylineFilled(ZUIDrawList* dl, const float* xs, const float* ys, int n, uint32_t col)
    {
        if (!xs || !ys || n < 3 || n > static_cast<int>(ZUIDrawList::VertexLimit / 2) || !CanAppendGeometry(dl, static_cast<uint32_t>(n * 2), static_cast<uint32_t>((n - 2) * 3 + n * 6)))
            return;
        for (int i = 0; i < n; ++i)
            PathLineTo(dl, xs[i], ys[i]);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddRectFilled(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float rounding, uint32_t round_flags)
    {
        if ((col >> 24) == 0)
        {
            PathClear(dl);
            return;
        }
        if (rounding < 0.5f)
        {
            ZUIDrawListAddRectFilledNoAA(dl, x0, y0, x1, y1, col);
            return;
        }
        // Nudge inward by 0.5px like ImGui (pixel-center convention)
        PathRect(dl, x0 + 0.5f, y0 + 0.5f, x1 - 0.5f, y1 - 0.5f, rounding, round_flags);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float rounding, uint32_t round_flags, float thickness)
    {
        if ((col >> 24) == 0 || !(thickness > 0.f) || !(x1 > x0) || !(y1 > y0))
        {
            PathClear(dl);
            return;
        }
        if (thickness * 2.f >= std::min(x1 - x0, y1 - y0))
        {
            ZUIDrawListAddRectFilled(dl, x0, y0, x1, y1, col, rounding, round_flags);
            return;
        }
        // Figma strokes are inside, independent of thickness. PathStroke draws
        // around its centreline; only the AA fringe extends outside the box.
        const float inset = thickness * 0.5f;
        PathRect(dl, x0 + inset, y0 + inset, x1 - inset, y1 - inset, std::max(0.f, rounding - inset), round_flags);
        PathStroke(dl, col, true, thickness);
    }

    void ZUIDrawListAddRectDashed(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float rounding, float thickness)
    {
        PathClear(dl);
        if ((col >> 24) == 0 || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x1 - x0) || !std::isfinite(y1 - y0) || !std::isfinite(rounding) || !std::isfinite(thickness) || !(thickness > 0.f) || !(x1 > x0) || !(y1 > y0))
            return;
        if (thickness * 2.f >= std::min(x1 - x0, y1 - y0))
        {
            ZUIDrawListAddRect(dl, x0, y0, x1, y1, col, rounding, 0xF, thickness);
            return;
        }
        const float inset = thickness * 0.5f;
        PathRect(dl, x0 + inset, y0 + inset, x1 - inset, y1 - inset, std::max(0.f, rounding - inset));
        // PathRect emits at most 4 * 13 LUT points. Preserve them on the stack:
        // emitting each dash reuses the draw list's path scratch, without a heap allocation.
        float          xs[52] = {};
        float          ys[52] = {};
        const uint32_t count  = dl->PathCount;
        if (count > 52)
        {
            PathClear(dl);
            return;
        }
        Helpers::secure_memcpy(xs, sizeof(xs), dl->PathX, count * sizeof(float));
        Helpers::secure_memcpy(ys, sizeof(ys), dl->PathY, count * sizeof(float));
        PathClear(dl);
        // Keep tessellation bounded even for unusually large logical canvases.
        const double perimeter = 2.0 * ((double(x1) - x0) + (double(y1) - y0));
        const float  dash      = std::max(3.f, static_cast<float>(perimeter / 4096.0));
        float        phase     = 0.f;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t next   = (i + 1) % count;
            const float    dx     = xs[next] - xs[i];
            const float    dy     = ys[next] - ys[i];
            const float    length = std::hypot(dx, dy);
            float          offset = 0.f;
            while (offset < length)
            {
                const bool  ink  = phase < dash;
                const float step = std::min(length - offset, (ink ? dash : dash * 2.f) - phase);
                if (!(step > 0.f))
                    break;
                if (ink)
                {
                    if (!dl->PathCount)
                        PathLineTo(dl, xs[i] + dx * (offset / length), ys[i] + dy * (offset / length));
                    PathLineTo(dl, xs[i] + dx * ((offset + step) / length), ys[i] + dy * ((offset + step) / length));
                }
                offset += step;
                phase  += step;
                if (ink && phase >= dash)
                    PathStroke(dl, col, false, thickness);
                if (phase >= dash * 2.f)
                    phase = 0.f;
            }
        }
        PathStroke(dl, col, false, thickness);
    }

    void ZUIDrawListAddCircleFilled(ZUIDrawList* dl, float cx, float cy, float r, uint32_t col, int num_segments)
    {
        if ((col >> 24) == 0 || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r) || r <= 0.f)
            return;
        int n = (num_segments > 0) ? num_segments : CircleSegments(r);
        if (n < 3 || n > static_cast<int>(ZUIDrawList::VertexLimit / 2) || !CanAppendGeometry(dl, static_cast<uint32_t>(n * 2), static_cast<uint32_t>((n - 2) * 3 + n * 6)))
            return;
        float step = 2.f * kPI / (float) n;
        for (int i = 0; i < n; ++i)
            PathLineTo(dl, cx + cosf(i * step) * r, cy + sinf(i * step) * r);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddCircle(ZUIDrawList* dl, float cx, float cy, float r, uint32_t col, int num_segments, float thickness)
    {
        if ((col >> 24) == 0 || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r) || r <= 0.f || !std::isfinite(thickness) || thickness <= 0.f)
            return;
        int n = (num_segments > 0) ? num_segments : CircleSegments(r);
        if (n < 3 || n > static_cast<int>(ZUIDrawList::VertexLimit / 4) || !CanAppendGeometry(dl, static_cast<uint32_t>(n * 4), static_cast<uint32_t>(n * 18)))
            return;
        float step = 2.f * kPI / (float) n;
        for (int i = 0; i < n; ++i)
            PathLineTo(dl, cx + cosf(i * step) * (r - 0.5f), cy + sinf(i * step) * (r - 0.5f));
        PathStroke(dl, col, true, thickness);
    }

    void ZUIDrawListAddTriangleFilled(ZUIDrawList* dl, float ax, float ay, float bx, float by, float cx, float cy, uint32_t col)
    {
        PathLineTo(dl, ax, ay);
        PathLineTo(dl, bx, by);
        PathLineTo(dl, cx, cy);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddImage(ZUIDrawList* dl, uint32_t tex_idx, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t col)
    {
        if (!CanAppendGeometry(dl, 4, 6))
            return;
        // Switch texture for this draw
        float cx0 = 0.f, cy0 = 0.f, cx1 = 0.f, cy1 = 0.f;
        GetCurrentClip(dl, cx0, cy0, cx1, cy1);
        EnsureCmd(dl, cx0, cy0, cx1, cy1, tex_idx);

        GrowVtx(dl, dl->Arena, 4);
        GrowIdx(dl, dl->Arena, 6);
        uint16_t    base                      = (uint16_t) dl->VtxCount;
        ZUIDrawVtx* v                         = dl->Vtx + dl->VtxCount;
        uint16_t*   i                         = dl->Idx + dl->IdxCount;
        v[0]                                  = {x0, y0, u0, v0, col};
        v[1]                                  = {x1, y0, u1, v0, col};
        v[2]                                  = {x1, y1, u1, v1, col};
        v[3]                                  = {x0, y1, u0, v1, col};
        i[0]                                  = base;
        i[1]                                  = (uint16_t) (base + 1);
        i[2]                                  = (uint16_t) (base + 2);
        i[3]                                  = base;
        i[4]                                  = (uint16_t) (base + 2);
        i[5]                                  = (uint16_t) (base + 3);
        dl->VtxCount                         += 4;
        dl->IdxCount                         += 6;
        dl->Cmds[dl->CmdCount - 1].ElemCount += 6;

        // Restore atlas texture
        EnsureCmd(dl, cx0, cy0, cx1, cy1, dl->AtlasTexIdx);
    }

} // namespace ZEngine::UI
