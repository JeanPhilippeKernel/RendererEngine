#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstring>
#include <cmath>

namespace ZEngine::UI
{
    // ---------------------------------------------------------------
    // Constants matching ImGui's geometry quality
    // ---------------------------------------------------------------
    static constexpr float kPI            = 3.14159265358979f;
    static constexpr float kCircleMaxError= 0.30f; // px
    static constexpr int   kArcFastSize   = 48;    // unit circle LUT size

    // 48-sample unit circle LUT (precomputed, matches ImGui ArcFastVtx)
    static float s_CircleLUTX[kArcFastSize];
    static float s_CircleLUTY[kArcFastSize];
    static bool  s_CircleLUTInited = false;

    static void InitCircleLUT()
    {
        if (s_CircleLUTInited) return;
        for (int i = 0; i < kArcFastSize; ++i)
        {
            float a = (float)i * (2.f * kPI) / (float)kArcFastSize;
            s_CircleLUTX[i] = cosf(a);
            s_CircleLUTY[i] = sinf(a);
        }
        s_CircleLUTInited = true;
    }

    // Compute segment count for a circle of given radius
    static int CircleSegments(float radius)
    {
        if (radius <= 0.f) return 4;
        int n = (int)ceilf(kPI / acosf(1.f - kCircleMaxError / radius));
        if (n < 4)  n = 4;
        if (n > 512) n = 512;
        return n & ~1; // round to even
    }

    // ---------------------------------------------------------------
    // Internal allocation helpers
    // ---------------------------------------------------------------

    static void GrowVtx(ZUIDrawList* dl, ArenaAllocator* arena, uint32_t needed)
    {
        if (dl->VtxCount + needed <= dl->VtxCapacity) return;
        uint32_t new_cap = dl->VtxCapacity ? dl->VtxCapacity * 2 : 4096;
        while (new_cap < dl->VtxCount + needed) new_cap *= 2;
        ZUIDrawVtx* nb = ZPushArray(arena, ZUIDrawVtx, new_cap);
        if (dl->Vtx) memcpy(nb, dl->Vtx, dl->VtxCount * sizeof(ZUIDrawVtx));
        dl->Vtx = nb; dl->VtxCapacity = new_cap;
    }

    static void GrowIdx(ZUIDrawList* dl, ArenaAllocator* arena, uint32_t needed)
    {
        if (dl->IdxCount + needed <= dl->IdxCapacity) return;
        uint32_t new_cap = dl->IdxCapacity ? dl->IdxCapacity * 2 : 8192;
        while (new_cap < dl->IdxCount + needed) new_cap *= 2;
        uint16_t* nb = ZPushArray(arena, uint16_t, new_cap);
        if (dl->Idx) memcpy(nb, dl->Idx, dl->IdxCount * sizeof(uint16_t));
        dl->Idx = nb; dl->IdxCapacity = new_cap;
    }

    static void GrowPath(ZUIDrawList* dl, ArenaAllocator* arena, uint32_t needed)
    {
        if (dl->PathCount + needed <= dl->PathCap) return;
        uint32_t new_cap = dl->PathCap ? dl->PathCap * 2 : 256;
        while (new_cap < dl->PathCount + needed) new_cap *= 2;
        float* nx = ZPushArray(arena, float, new_cap);
        float* ny = ZPushArray(arena, float, new_cap);
        if (dl->PathX) { memcpy(nx, dl->PathX, dl->PathCount * sizeof(float)); memcpy(ny, dl->PathY, dl->PathCount * sizeof(float)); }
        dl->PathX = nx; dl->PathY = ny; dl->PathCap = new_cap;
    }

    // We store the arena for growth in the draw list calls.
    // For simplicity thread it as a file-global (ZUIContext owns a single DL per frame).
    static ArenaAllocator* s_Arena = nullptr;

    // ---------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------

    void ZUIDrawListInit(ZUIDrawList* dl, ArenaAllocator* frame_arena,
                         uint32_t vtx_cap, uint32_t idx_cap,
                         float white_u, float white_v, uint32_t atlas_idx)
    {
        InitCircleLUT();
        s_Arena         = frame_arena;
        dl->VtxCapacity = vtx_cap;
        dl->IdxCapacity = idx_cap;
        dl->Vtx         = vtx_cap ? ZPushArray(frame_arena, ZUIDrawVtx, vtx_cap) : nullptr;
        dl->Idx         = idx_cap ? ZPushArray(frame_arena, uint16_t,   idx_cap) : nullptr;
        dl->CmdCapacity = 512;
        dl->Cmds        = ZPushArray(frame_arena, ZUIDrawListCmd, dl->CmdCapacity);
        dl->PathCap     = 256;
        dl->PathX       = ZPushArray(frame_arena, float, dl->PathCap);
        dl->PathY       = ZPushArray(frame_arena, float, dl->PathCap);
        dl->WhiteU      = white_u;
        dl->WhiteV      = white_v;
        dl->AtlasTexIdx = atlas_idx;
        dl->FringeScale = 1.0f;
        ZUIDrawListReset(dl);
    }

    void ZUIDrawListReset(ZUIDrawList* dl)
    {
        dl->VtxCount   = 0;
        dl->IdxCount   = 0;
        dl->CmdCount   = 0;
        dl->PathCount  = 0;
        dl->ClipDepth  = 0;
        // Open a default command with no clip
        if (dl->Cmds && dl->CmdCapacity > 0)
        {
            dl->CmdCount = 1;
            dl->Cmds[0]  = {};
            dl->Cmds[0].TexIdx = dl->AtlasTexIdx;
        }
    }

    // ---------------------------------------------------------------
    // Ensure current command matches clip rect + tex; open new cmd if needed
    // ---------------------------------------------------------------

    static void EnsureCmd(ZUIDrawList* dl, float cx0, float cy0, float cx1, float cy1, uint32_t tex_idx)
    {
        if (dl->CmdCount == 0)
        {
            if (dl->CmdCount >= dl->CmdCapacity) return;
            dl->Cmds[dl->CmdCount++] = {};
        }
        ZUIDrawListCmd& cur = dl->Cmds[dl->CmdCount - 1];
        bool same = (cur.ClipX == cx0 && cur.ClipY == cy0 &&
                     cur.ClipW == cx1 - cx0 && cur.ClipH == cy1 - cy0 &&
                     cur.TexIdx == tex_idx);
        if (same) return;
        // Open new cmd
        if (dl->CmdCount >= dl->CmdCapacity) return;
        ZUIDrawListCmd nc = {};
        nc.ClipX    = cx0; nc.ClipY    = cy0;
        nc.ClipW    = cx1 - cx0; nc.ClipH = cy1 - cy0;
        nc.TexIdx   = tex_idx;
        nc.IdxOffset= dl->IdxCount;
        nc.ElemCount= 0;
        dl->Cmds[dl->CmdCount++] = nc;
    }

    static void GetCurrentClip(const ZUIDrawList* dl, float& x0, float& y0, float& x1, float& y1)
    {
        if (dl->ClipDepth > 0)
        {
            const float* r = dl->ClipStack[dl->ClipDepth - 1];
            x0=r[0]; y0=r[1]; x1=r[2]; y1=r[3];
        }
        else
        {
            x0=-1e9f; y0=-1e9f; x1=1e9f; y1=1e9f;
        }
    }

    static void FlushCmd(ZUIDrawList* dl)
    {
        float cx0,cy0,cx1,cy1;
        GetCurrentClip(dl, cx0, cy0, cx1, cy1);
        EnsureCmd(dl, cx0, cy0, cx1, cy1, dl->AtlasTexIdx);
    }

    // ---------------------------------------------------------------
    // Clip rect stack
    // ---------------------------------------------------------------

    void ZUIDrawListPushClipRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, bool intersect)
    {
        if (intersect && dl->ClipDepth > 0)
        {
            const float* prev = dl->ClipStack[dl->ClipDepth - 1];
            if (x0 < prev[0]) x0 = prev[0];
            if (y0 < prev[1]) y0 = prev[1];
            if (x1 > prev[2]) x1 = prev[2];
            if (y1 > prev[3]) y1 = prev[3];
        }
        if (dl->ClipDepth < ZUIDrawList::kMaxClipDepth)
        {
            float* slot = dl->ClipStack[dl->ClipDepth++];
            slot[0]=x0; slot[1]=y0; slot[2]=x1; slot[3]=y1;
        }
        FlushCmd(dl);
    }

    void ZUIDrawListPopClipRect(ZUIDrawList* dl)
    {
        if (dl->ClipDepth > 0) --dl->ClipDepth;
        FlushCmd(dl);
    }

    // ---------------------------------------------------------------
    // Primitive reservation
    // ---------------------------------------------------------------

    // Reserve `vtx` vertices and `idx` indices; return write pointer.
    // Caller must write exactly that many.
    static ZUIDrawVtx* PrimReserve(ZUIDrawList* dl, uint32_t vtx, uint32_t idx)
    {
        FlushCmd(dl);
        GrowVtx(dl, s_Arena, vtx);
        GrowIdx(dl, s_Arena, idx);
        ZUIDrawVtx* vw = dl->Vtx + dl->VtxCount;
        uint16_t*   iw = dl->Idx + dl->IdxCount;
        uint16_t    base = (uint16_t)dl->VtxCount;
        dl->VtxCount += vtx;
        dl->IdxCount += idx;
        // Update current cmd elem count
        dl->Cmds[dl->CmdCount - 1].ElemCount += idx;
        return vw;
        (void)iw; // caller writes indices via separate helpers
    }

    // ---------------------------------------------------------------
    // Fast flat colored rect (no AA, no rounding) — 4 vtx, 6 idx
    // ---------------------------------------------------------------

    void ZUIDrawListAddRectFilledNoAA(ZUIDrawList* dl,
                                       float x0, float y0, float x1, float y1,
                                       uint32_t col)
    {
        FlushCmd(dl);
        GrowVtx(dl, s_Arena, 4);
        GrowIdx(dl, s_Arena, 6);
        uint16_t base = (uint16_t)dl->VtxCount;
        ZUIDrawVtx* v = dl->Vtx + dl->VtxCount;
        uint16_t*   i = dl->Idx  + dl->IdxCount;
        v[0] = {x0,y0, dl->WhiteU,dl->WhiteV, col};
        v[1] = {x1,y0, dl->WhiteU,dl->WhiteV, col};
        v[2] = {x1,y1, dl->WhiteU,dl->WhiteV, col};
        v[3] = {x0,y1, dl->WhiteU,dl->WhiteV, col};
        i[0]=base; i[1]=(uint16_t)(base+1); i[2]=(uint16_t)(base+2);
        i[3]=base; i[4]=(uint16_t)(base+2); i[5]=(uint16_t)(base+3);
        dl->VtxCount += 4; dl->IdxCount += 6;
        dl->Cmds[dl->CmdCount-1].ElemCount += 6;
    }

    void ZUIDrawListAddRectFilledMultiColor(ZUIDrawList* dl,
                                             float x0, float y0, float x1, float y1,
                                             uint32_t col_tl, uint32_t col_tr,
                                             uint32_t col_bl, uint32_t col_br)
    {
        FlushCmd(dl);
        GrowVtx(dl, s_Arena, 4); GrowIdx(dl, s_Arena, 6);
        uint16_t base = (uint16_t)dl->VtxCount;
        ZUIDrawVtx* v = dl->Vtx + dl->VtxCount;
        uint16_t*   i = dl->Idx  + dl->IdxCount;
        float wu = dl->WhiteU, wv = dl->WhiteV;
        v[0]={x0,y0,wu,wv,col_tl}; v[1]={x1,y0,wu,wv,col_tr};
        v[2]={x1,y1,wu,wv,col_br}; v[3]={x0,y1,wu,wv,col_bl};
        i[0]=base; i[1]=(uint16_t)(base+1); i[2]=(uint16_t)(base+2);
        i[3]=base; i[4]=(uint16_t)(base+2); i[5]=(uint16_t)(base+3);
        dl->VtxCount+=4; dl->IdxCount+=6;
        dl->Cmds[dl->CmdCount-1].ElemCount+=6;
    }

    // ---------------------------------------------------------------
    // Path helpers
    // ---------------------------------------------------------------

    static void PathClear(ZUIDrawList* dl) { dl->PathCount = 0; }

    static void PathLineTo(ZUIDrawList* dl, float x, float y)
    {
        GrowPath(dl, s_Arena, 1);
        dl->PathX[dl->PathCount] = x;
        dl->PathY[dl->PathCount] = y;
        ++dl->PathCount;
    }

    // Append arc to path using the fast 48-sample LUT.
    // angle_min/max in turns [0..1], a_min_sample/a_max_sample in LUT indices.
    static void PathArcToFast(ZUIDrawList* dl, float cx, float cy, float r,
                               int a_min, int a_max)
    {
        if (r <= 0.f || a_min > a_max) { PathLineTo(dl, cx, cy); return; }
        GrowPath(dl, s_Arena, a_max - a_min + 1);
        for (int i = a_min; i <= a_max; ++i)
        {
            int idx = i % kArcFastSize;
            dl->PathX[dl->PathCount] = cx + s_CircleLUTX[idx] * r;
            dl->PathY[dl->PathCount] = cy + s_CircleLUTY[idx] * r;
            ++dl->PathCount;
        }
    }

    // Append rounded rect path. round_flags: bit 0=TL 1=TR 2=BR 3=BL
    static void PathRect(ZUIDrawList* dl,
                          float x0, float y0, float x1, float y1,
                          float r, uint32_t flags = 0xF)
    {
        if (r < 0.5f) { flags = 0; }
        bool tl = (flags & 1) != 0;
        bool tr = (flags & 2) != 0;
        bool br = (flags & 4) != 0;
        bool bl = (flags & 8) != 0;
        // clamp radius
        float half = (x1-x0 < y1-y0 ? x1-x0 : y1-y0) * 0.5f;
        if (r > half) r = half;
        // LUT indices: TL=6-12, TR=0-6, BR=18-24, BL=30-36 (0..47 range)
        if (tl) { PathArcToFast(dl, x0+r, y0+r, r,  6, 12); } else PathLineTo(dl, x0, y0);
        if (tr) { PathArcToFast(dl, x1-r, y0+r, r,  0,  6); } else PathLineTo(dl, x1, y0);
        if (br) { PathArcToFast(dl, x1-r, y1-r, r, 42, 48); } else PathLineTo(dl, x1, y1);
        if (bl) { PathArcToFast(dl, x0+r, y1-r, r, 30, 36); } else PathLineTo(dl, x0, y1);
    }

    // ---------------------------------------------------------------
    // PathFillConvex — AA filled polygon (ImGui AddConvexPolyFilled)
    // ---------------------------------------------------------------
    static void PathFillConvex(ZUIDrawList* dl, uint32_t col)
    {
        int n = (int)dl->PathCount;
        if (n < 3) { PathClear(dl); return; }

        float fs = dl->FringeScale;
        uint32_t col_trans = col & 0x00FFFFFFu; // alpha = 0

        FlushCmd(dl);
        GrowVtx(dl, s_Arena, (uint32_t)(n * 2));
        GrowIdx(dl, s_Arena, (uint32_t)((n - 2) * 3 + n * 6));

        uint16_t base = (uint16_t)dl->VtxCount;
        ZUIDrawVtx* vw = dl->Vtx + dl->VtxCount;
        uint16_t*   iw = dl->Idx  + dl->IdxCount;
        float wu = dl->WhiteU, wv = dl->WhiteV;

        // Compute per-vertex normals (dm = miter direction)
        // dm[i] = average of normals of edge (i-1,i) and edge (i,i+1)
        GrowPath(dl, s_Arena, n); // temp space for dm_x, dm_y reusing path

        float* dmx = dl->PathX + dl->PathCount; // NOT modifying PathCount, using tail as scratch
        float* dmy = dl->PathY + dl->PathCount;
        // (ensure capacity)
        if (dl->PathCount + (uint32_t)n > dl->PathCap)
        {
            GrowPath(dl, s_Arena, n);
            dmx = dl->PathX + dl->PathCount;
            dmy = dl->PathY + dl->PathCount;
        }

        for (int i = 0; i < n; ++i)
        {
            int ni = (i + 1) % n;
            float ex = dl->PathX[ni] - dl->PathX[i];
            float ey = dl->PathY[ni] - dl->PathY[i];
            float len = sqrtf(ex*ex + ey*ey);
            if (len > 1e-6f) { ex /= len; ey /= len; }
            // Store edge normal (rotated 90° inward)
            dmx[i] = ey;
            dmy[i] = -ex;
        }
        // Average adjacent normals
        for (int i = 0; i < n; ++i)
        {
            int pi = (i + n - 1) % n;
            float avg_x = (dmx[i] + dmx[pi]) * 0.5f;
            float avg_y = (dmy[i] + dmy[pi]) * 0.5f;
            float dot   = avg_x*avg_x + avg_y*avg_y;
            if (dot > 1e-9f) { float inv = 1.f / dot; avg_x *= inv; avg_y *= inv; }
            dmx[i] = avg_x * fs * 0.5f;
            dmy[i] = avg_y * fs * 0.5f;
        }

        // Emit vertices: inner (col) + outer fringe (col_trans)
        for (int i = 0; i < n; ++i)
        {
            float px = dl->PathX[i], py = dl->PathY[i];
            vw[i*2+0] = {px - dmx[i], py - dmy[i], wu, wv, col};
            vw[i*2+1] = {px + dmx[i], py + dmy[i], wu, wv, col_trans};
        }
        dl->VtxCount += (uint32_t)(n * 2);

        // Fill indices: fan over inner vertices
        uint32_t fill_idx = (uint32_t)((n - 2) * 3);
        for (int i = 2; i < n; ++i)
        {
            iw[0] = base;
            iw[1] = (uint16_t)(base + (uint16_t)((i-1)*2));
            iw[2] = (uint16_t)(base + (uint16_t)(i*2));
            iw += 3;
        }
        // Fringe quads
        for (int i = 0; i < n; ++i)
        {
            int ni = (i + 1) % n;
            uint16_t i0 = (uint16_t)(base + (uint16_t)(i  * 2));
            uint16_t i1 = (uint16_t)(base + (uint16_t)(i  * 2 + 1));
            uint16_t i2 = (uint16_t)(base + (uint16_t)(ni * 2 + 1));
            uint16_t i3 = (uint16_t)(base + (uint16_t)(ni * 2));
            iw[0]=i0; iw[1]=i1; iw[2]=i2;
            iw[3]=i0; iw[4]=i2; iw[5]=i3;
            iw += 6;
        }
        uint32_t total_idx = (uint32_t)(fill_idx + (uint32_t)(n * 6));
        dl->IdxCount += total_idx;
        dl->Cmds[dl->CmdCount-1].ElemCount += total_idx;

        PathClear(dl);
    }

    // ---------------------------------------------------------------
    // PathStroke — AA stroked polyline (ImGui AddPolyline)
    // closed: whether last point connects back to first
    // ---------------------------------------------------------------
    static void PathStroke(ZUIDrawList* dl, uint32_t col, bool closed, float thickness)
    {
        int n = (int)dl->PathCount;
        int count = closed ? n : n - 1;
        if (count <= 0 || n < 2) { PathClear(dl); return; }

        float fs    = dl->FringeScale;
        float half  = thickness * 0.5f;
        uint32_t col_trans = col & 0x00FFFFFFu;
        float wu = dl->WhiteU, wv = dl->WhiteV;

        FlushCmd(dl);
        GrowVtx(dl, s_Arena, (uint32_t)(n * 4));
        GrowIdx(dl, s_Arena, (uint32_t)(count * 18));

        uint16_t base = (uint16_t)dl->VtxCount;
        ZUIDrawVtx* vw = dl->Vtx + dl->VtxCount;
        uint16_t*   iw = dl->Idx  + dl->IdxCount;

        // For each segment compute outward normals
        // Each point produces 4 vertices:
        //   inner-left, outer-left (fringe), inner-right, outer-right (fringe)
        for (int i1 = 0; i1 < n; ++i1)
        {
            int i2 = (i1 + 1) < n ? i1 + 1 : 0;
            float dx = dl->PathX[i2] - dl->PathX[i1];
            float dy = dl->PathY[i2] - dl->PathY[i1];
            float len = sqrtf(dx*dx + dy*dy); if (len < 1e-6f) len = 1.f;
            float nx = dy / len, ny = -dx / len; // outward normal (left side)

            float x = dl->PathX[i1], y = dl->PathY[i1];
            // Outer fringe (left), inner (left), inner (right), outer fringe (right)
            vw[i1*4+0] = {x - nx*(half + fs), y - ny*(half + fs), wu, wv, col_trans};
            vw[i1*4+1] = {x - nx* half,       y - ny* half,       wu, wv, col};
            vw[i1*4+2] = {x + nx* half,        y + ny* half,       wu, wv, col};
            vw[i1*4+3] = {x + nx*(half + fs),  y + ny*(half + fs), wu, wv, col_trans};
        }
        dl->VtxCount += (uint32_t)(n * 4);

        // Indices: for each segment i1→i2, 3 quads (left fringe, fill, right fringe)
        for (int i1 = 0; i1 < count; ++i1)
        {
            int i2 = (i1 + 1) < n ? i1 + 1 : 0;
            uint16_t b1 = (uint16_t)(base + (uint16_t)(i1 * 4));
            uint16_t b2 = (uint16_t)(base + (uint16_t)(i2 * 4));
            // left fringe
            iw[0]=b1;     iw[1]=(uint16_t)(b1+1); iw[2]=(uint16_t)(b2+1);
            iw[3]=b1;     iw[4]=(uint16_t)(b2+1); iw[5]=b2;
            // fill
            iw[6]=(uint16_t)(b1+1); iw[7]=(uint16_t)(b1+2); iw[8]=(uint16_t)(b2+2);
            iw[9]=(uint16_t)(b1+1); iw[10]=(uint16_t)(b2+2);iw[11]=(uint16_t)(b2+1);
            // right fringe
            iw[12]=(uint16_t)(b1+2);iw[13]=(uint16_t)(b1+3);iw[14]=(uint16_t)(b2+3);
            iw[15]=(uint16_t)(b1+2);iw[16]=(uint16_t)(b2+3);iw[17]=(uint16_t)(b2+2);
            iw += 18;
        }
        uint32_t total_idx = (uint32_t)(count * 18);
        dl->IdxCount += total_idx;
        dl->Cmds[dl->CmdCount-1].ElemCount += total_idx;

        PathClear(dl);
    }

    // ---------------------------------------------------------------
    // Public shape functions
    // ---------------------------------------------------------------

    void ZUIDrawListAddLine(ZUIDrawList* dl, float x0, float y0, float x1, float y1,
                             uint32_t col, float thickness)
    {
        PathLineTo(dl, x0 + 0.5f, y0 + 0.5f);
        PathLineTo(dl, x1 + 0.5f, y1 + 0.5f);
        PathStroke(dl, col, false, thickness);
    }

    void ZUIDrawListAddPolylineFilled(ZUIDrawList* dl, const float* xs, const float* ys,
                                       int n, uint32_t col)
    {
        for (int i = 0; i < n; ++i) PathLineTo(dl, xs[i], ys[i]);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddRectFilled(ZUIDrawList* dl,
                                   float x0, float y0, float x1, float y1,
                                   uint32_t col, float rounding, uint32_t round_flags)
    {
        if ((col >> 24) == 0) { PathClear(dl); return; }
        if (rounding < 0.5f) { ZUIDrawListAddRectFilledNoAA(dl, x0, y0, x1, y1, col); return; }
        // Nudge inward by 0.5px like ImGui (pixel-center convention)
        PathRect(dl, x0 + 0.5f, y0 + 0.5f, x1 - 0.5f, y1 - 0.5f, rounding, round_flags);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddRect(ZUIDrawList* dl,
                             float x0, float y0, float x1, float y1,
                             uint32_t col, float rounding, uint32_t round_flags,
                             float thickness)
    {
        if ((col >> 24) == 0) { PathClear(dl); return; }
        PathRect(dl, x0 + 0.5f, y0 + 0.5f, x1 - 0.5f, y1 - 0.5f, rounding, round_flags);
        PathStroke(dl, col, true, thickness);
    }

    void ZUIDrawListAddCircleFilled(ZUIDrawList* dl, float cx, float cy, float r,
                                     uint32_t col, int num_segments)
    {
        if ((col >> 24) == 0 || r <= 0.f) return;
        int n = (num_segments > 0) ? num_segments : CircleSegments(r);
        float step = 2.f * kPI / (float)n;
        for (int i = 0; i < n; ++i)
            PathLineTo(dl, cx + cosf(i * step) * r, cy + sinf(i * step) * r);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddCircle(ZUIDrawList* dl, float cx, float cy, float r,
                               uint32_t col, int num_segments, float thickness)
    {
        if ((col >> 24) == 0 || r <= 0.f) return;
        int n = (num_segments > 0) ? num_segments : CircleSegments(r);
        float step = 2.f * kPI / (float)n;
        for (int i = 0; i < n; ++i)
            PathLineTo(dl, cx + cosf(i * step) * (r - 0.5f), cy + sinf(i * step) * (r - 0.5f));
        PathStroke(dl, col, true, thickness);
    }

    void ZUIDrawListAddTriangleFilled(ZUIDrawList* dl,
                                       float ax, float ay,
                                       float bx, float by,
                                       float cx, float cy, uint32_t col)
    {
        PathLineTo(dl, ax, ay);
        PathLineTo(dl, bx, by);
        PathLineTo(dl, cx, cy);
        PathFillConvex(dl, col);
    }

    void ZUIDrawListAddImage(ZUIDrawList* dl, uint32_t tex_idx,
                              float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              uint32_t col)
    {
        // Switch texture for this draw
        float cx0,cy0,cx1,cy1;
        GetCurrentClip(dl, cx0, cy0, cx1, cy1);
        EnsureCmd(dl, cx0, cy0, cx1, cy1, tex_idx);

        GrowVtx(dl, s_Arena, 4); GrowIdx(dl, s_Arena, 6);
        uint16_t base = (uint16_t)dl->VtxCount;
        ZUIDrawVtx* v = dl->Vtx + dl->VtxCount;
        uint16_t*   i = dl->Idx  + dl->IdxCount;
        v[0]={x0,y0,u0,v0,col}; v[1]={x1,y0,u1,v0,col};
        v[2]={x1,y1,u1,v1,col}; v[3]={x0,y1,u0,v1,col};
        i[0]=base; i[1]=(uint16_t)(base+1); i[2]=(uint16_t)(base+2);
        i[3]=base; i[4]=(uint16_t)(base+2); i[5]=(uint16_t)(base+3);
        dl->VtxCount+=4; dl->IdxCount+=6;
        dl->Cmds[dl->CmdCount-1].ElemCount+=6;

        // Restore atlas texture
        EnsureCmd(dl, cx0, cy0, cx1, cy1, dl->AtlasTexIdx);
    }

} // namespace ZEngine::UI
