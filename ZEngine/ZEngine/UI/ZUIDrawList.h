#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstdint>

// ZUIDrawList — immediate-mode vector draw list.
//
// Mirrors ImDrawList's architecture:
//   - VtxBuffer / IdxBuffer  → flat GPU-ready buffers
//   - CmdBuffer              → scissor/texture batches
//   - _Path                  → scratch path points (CPU only)
//
// AA fringe width = FringeScale (1.0 logical px, matches ImGui).
// Solid-color fills use the atlas white pixel (WhiteU/WhiteV).

namespace ZEngine::UI
{

    /// @brief Vertex layout — 20 bytes, matches zui_draw.vert attribute layout.
    struct ZUIDrawVtx
    {
        float    x   = 0.f; ///< Screen X (logical px)
        float    y   = 0.f; ///< Screen Y (logical px)
        float    u   = 0.f; ///< Atlas UV horizontal
        float    v   = 0.f; ///< Atlas UV vertical
        uint32_t col = 0u;  ///< RGBA8 packed (R in low byte)
    };
    static_assert(sizeof(ZUIDrawVtx) == 20, "ZUIDrawVtx size mismatch");

    /// @brief One scissored draw call with its own clip rect and texture index.
    struct ZUIDrawListCmd
    {
        uint32_t IdxOffset = 0;   // first index in IdxBuffer
        uint32_t ElemCount = 0;   // index count for this call
        float    ClipX     = 0.f; // scissor rect (logical px)
        float    ClipY     = 0.f;
        float    ClipW     = 0.f;
        float    ClipH     = 0.f;
        uint32_t TexIdx    = 0; // bindless texture array index
    };

    /// @brief Immediate-mode vector draw list.
    ///
    /// Mirrors ImDrawList: VtxBuffer/IdxBuffer hold GPU-ready geometry;
    /// CmdBuffer holds scissor/texture batches; PathX/PathY are CPU-only scratch
    /// for building path primitives before they are tessellated into vertices.
    struct ZUIDrawList
    {
        // GPU-bound buffers (allocated from FrameArena each frame)
        ZUIDrawVtx*          Vtx                         = nullptr;
        uint32_t             VtxCount                    = 0;
        uint32_t             VtxCapacity                 = 0;

        uint16_t*            Idx                         = nullptr;
        uint32_t             IdxCount                    = 0;
        uint32_t             IdxCapacity                 = 0;

        // Scissor command buffer
        ZUIDrawListCmd*      Cmds                        = nullptr;
        uint32_t             CmdCount                    = 0;
        uint32_t             CmdCapacity                 = 0;

        // CPU-only path scratch (not sent to GPU)
        float*               PathX                       = nullptr;
        float*               PathY                       = nullptr;
        uint32_t             PathCount                   = 0;
        uint32_t             PathCap                     = 0;

        // Clip rect stack
        static constexpr int kMaxClipDepth               = 16;
        float                ClipStack[kMaxClipDepth][4] = {}; // [x0,y0,x1,y1]
        int                  ClipDepth                   = 0;

        // AA fringe width — 1.0f = 1 logical pixel (matches ImGui)
        float                FringeScale                 = 1.0f;

        // White pixel UV for solid-color fills
        float                WhiteU                      = 0.f;
        float                WhiteV                      = 0.f;

        // Active texture index (atlas slot)
        uint32_t             AtlasTexIdx                 = 0;
    };

    // Lifecycle

    /// @brief Allocate GPU-bound vertex/index buffers and path scratch from @p frame_arena.
    /// @param dl        Target draw list.
    /// @param vtx_cap   Initial vertex buffer capacity (elements).
    /// @param idx_cap   Initial index buffer capacity (elements).
    /// @param white_u   U coordinate of the atlas white texel (used for solid fills).
    /// @param white_v   V coordinate of the atlas white texel.
    /// @param atlas_idx Bindless texture-array slot for the font atlas.
    void            ZUIDrawListInit(ZUIDrawList* dl, ZEngine::Core::Memory::ArenaAllocator* frame_arena, uint32_t vtx_cap, uint32_t idx_cap, float white_u, float white_v, uint32_t atlas_idx);

    /// @brief Reset all counts to zero without freeing buffers; opens a default draw command.
    /// @param dl Target draw list.
    void            ZUIDrawListReset(ZUIDrawList* dl);

    // Clip rect stack

    /// @brief Push a scissor rect; intersects with the current top entry when @p intersect_with_current is true.
    /// @param dl Target draw list.
    void            ZUIDrawListPushClipRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, bool intersect_with_current = true);

    /// @brief Pop the top scissor rect and flush a new draw command.
    /// @param dl Target draw list.
    void            ZUIDrawListPopClipRect(ZUIDrawList* dl);

    // Shape primitives

    /// @brief AA-stroked line from (x0,y0) to (x1,y1).
    /// @param dl Target draw list.
    void            ZUIDrawListAddLine(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float thickness = 1.f);

    /// @brief Draw a VS Code-style "∨" down chevron (combo open, section expanded).
    /// @param dl Target draw list.
    void            ZUIDrawListAddChevronDown(ZUIDrawList* dl, float cx, float cy, float half_w, float half_h, uint32_t col, float thickness = 1.5f);

    /// @brief Draw a VS Code-style "›" right chevron (section collapsed).
    /// @param dl Target draw list.
    void            ZUIDrawListAddChevronRight(ZUIDrawList* dl, float cx, float cy, float half_w, float half_h, uint32_t col, float thickness = 1.5f);

    /// @brief AA-filled convex polygon from parallel (xs, ys) arrays of @p n points.
    /// @param dl Target draw list.
    void            ZUIDrawListAddPolylineFilled(ZUIDrawList* dl, const float* xs, const float* ys, int n, uint32_t col);

    /// @brief AA-filled rounded rect; @p rounding = corner radius, @p round_flags = per-corner mask.
    /// @param dl Target draw list.
    void            ZUIDrawListAddRectFilled(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float rounding = 0.f, uint32_t round_flags = 0xF);

    /// @brief AA-stroked (outline) rounded rect.
    /// @param dl Target draw list.
    void            ZUIDrawListAddRect(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col, float rounding = 0.f, uint32_t round_flags = 0xF, float thickness = 1.f);

    /// @brief AA-filled circle; @p num_segments = 0 auto-selects quality.
    /// @param dl Target draw list.
    void            ZUIDrawListAddCircleFilled(ZUIDrawList* dl, float cx, float cy, float r, uint32_t col, int num_segments = 0);

    /// @brief AA-stroked circle outline.
    /// @param dl Target draw list.
    void            ZUIDrawListAddCircle(ZUIDrawList* dl, float cx, float cy, float r, uint32_t col, int num_segments = 0, float thickness = 1.f);

    /// @brief AA-filled triangle (vertices a, b, c).
    /// @param dl Target draw list.
    void            ZUIDrawListAddTriangleFilled(ZUIDrawList* dl, float ax, float ay, float bx, float by, float cx, float cy, uint32_t col);

    /// @brief Textured quad for glyph rendering. Temporarily switches to @p tex_idx then restores atlas.
    /// @param dl Target draw list.
    void            ZUIDrawListAddImage(ZUIDrawList* dl, uint32_t tex_idx, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t col);

    /// @brief Flat (no AA) solid rect — fast path for opaque fills.
    /// @param dl Target draw list.
    void            ZUIDrawListAddRectFilledNoAA(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col);

    /// @brief Per-corner colored rect (gradient quad) — no AA, no rounding.
    /// @param dl Target draw list.
    void            ZUIDrawListAddRectFilledMultiColor(ZUIDrawList* dl, float x0, float y0, float x1, float y1, uint32_t col_tl, uint32_t col_tr, uint32_t col_bl, uint32_t col_br);

    // Colour helpers

    /// @brief Pack a linear float[4] RGBA array → uint32_t RGBA8 (matches shader unpackUnorm4x8).
    /// @param c RGBA float[4] color array.
    /// @returns Packed RGBA uint32.
    inline uint32_t ZUIPackColor(const float c[4])
    {
        auto clamp01 = [](float v) -> uint8_t { return v <= 0.f ? 0 : v >= 1.f ? 255 : (uint8_t) (v * 255.f + 0.5f); };
        return (uint32_t) clamp01(c[0]) | ((uint32_t) clamp01(c[1]) << 8) | ((uint32_t) clamp01(c[2]) << 16) | ((uint32_t) clamp01(c[3]) << 24);
    }
    /// @brief Pack component RGBA → uint32_t RGBA8.
    /// @param r Red channel [0,1].
    /// @param g Green channel [0,1].
    /// @param b Blue channel [0,1].
    /// @param a Alpha channel [0,1].
    /// @returns Packed RGBA uint32.
    inline uint32_t ZUIPackColor(float r, float g, float b, float a)
    {
        float c[4] = {r, g, b, a};
        return ZUIPackColor(c);
    }

} // namespace ZEngine::UI
