#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>
#include <cstring>

namespace ZEngine::UI
{
    /// @brief Lightweight non-owning string view (pointer + byte length).
    /// @note Ptr is valid only for the current frame — all ZUIStr values are
    ///       arena-allocated in FrameArena and become stale after ZUIBeginFrame.
    struct ZUIStr
    {
        const char* Ptr = nullptr;
        uint32_t    Len = 0;
    };

    /// @brief How a box computes its size along one axis.
    enum class ZUISizeKind : uint8_t
    {
        Pixels        = 0, ///< Fixed logical-pixel size (Value).
        Text          = 1, ///< Size to the rendered text extent (including FramePadding).
        ChildrenSum   = 2, ///< Intrinsic fit: sum (layout axis) or max (cross axis) of children.
        ParentPercent = 3, ///< Fraction of the parent's computed size (Value in [0,1]).
        Fill          = 4  ///< Expand to fill remaining space after other siblings are sized.
    };

    /// @brief Size specification for one axis of a ZUIBox.
    ///
    /// Strictness controls how much this box yields during overflow resolution:
    ///   1.0 = rigid (never shrunk), 0.0 = fully flexible (absorbed first).
    struct ZUISize
    {
        ZUISizeKind Kind       = ZUISizeKind::Pixels;
        float       Value      = 0.f;
        float       Strictness = 1.f;
    };

    /// @brief Bit flags that control per-box draw and behaviour options.
    enum ZUIBoxFlags : uint32_t
    {
        ZUI_None           = 0,
        ZUI_DrawBackground = 1 << 0,
        ZUI_DrawBorder     = 1 << 1,
        ZUI_DrawText       = 1 << 2,
        ZUI_Clickable      = 1 << 3,
        ZUI_Scrollable     = 1 << 4,
        ZUI_ClipChildren   = 1 << 5,
        ZUI_FloatX         = 1 << 6,
        ZUI_FloatY         = 1 << 7,
        // Draw-list shape overlays — rendered by PreparePayload after background/border.
        // All use TextColor as the stroke/fill color.
        ZUI_DrawCheckmark  = 1 << 8,  ///< Checkmark polyline stroke inside the box.
        ZUI_DrawCircleFill = 1 << 9,  ///< Filled circle inscribed in box center.
        ZUI_DrawTriArrow   = 1 << 10, ///< Collapse arrow; direction from UserData (0=right, 1=down).
        ZUI_DropShadow     = 1 << 11, ///< Dark offset rect emitted behind background.
        ZUI_DrawPlotLines  = 1 << 12, ///< Line chart; data in Label.Ptr/Len, range in Padding.
        ZUI_DrawPlotBars   = 1 << 13, ///< Bar chart; same data layout as DrawPlotLines.
        ZUI_DrawActorIcon  = 1 << 14, ///< Actor-type icon; shape from UserData (see ZUI_ICON_* constants).
    };

    // Icon type constants for ZUI_DrawActorIcon (stored in ZUIPersistentState::UserData)
    static constexpr float ZUI_ICON_WORLD          = 10.f; // globe with crosshairs
    static constexpr float ZUI_ICON_LIGHT          = 11.f; // sun — filled circle + radiating lines
    static constexpr float ZUI_ICON_MESH           = 12.f; // 3-D box wireframe
    static constexpr float ZUI_ICON_CAMERA         = 13.f; // camera body + lens triangle
    static constexpr float ZUI_ICON_FOLDER         = 14.f; // two-rect folder shape
    static constexpr float ZUI_ICON_ACTOR          = 15.f; // diamond
    static constexpr float ZUI_ICON_COLLECTION_ADD = 20.f; // folder + embedded "+" cross (New Collection button)
    static constexpr float ZUI_ICON_GEAR           = 21.f; // outer ring + inner dot (Settings button)
    static constexpr float ZUI_ICON_GRID           = 22.f; // 3×3 grid lines (viewport grid toggle)
    static constexpr float ZUI_ICON_TRANSLATE      = 23.f; // 4-way arrow cross (gizmo translate)
    static constexpr float ZUI_ICON_ROTATE         = 24.f; // circle + arrowhead (gizmo rotate)
    static constexpr float ZUI_ICON_SCALE          = 25.f; // square + corner dots (gizmo scale)

    // Source-code file icons — colored badge + symbol + glow halo
    static constexpr float ZUI_ICON_SOURCE_CPP     = 30.f; // C / C++ — blue badge, "++" symbol
    static constexpr float ZUI_ICON_SOURCE_CS      = 31.f; // C#     — purple badge, "#" symbol
    static constexpr float ZUI_ICON_SOURCE_JS      = 32.f; // JS/TS  — yellow badge, ">" symbol
    static constexpr float ZUI_ICON_SOURCE_PY      = 33.f; // Python — green badge, diamond
    static constexpr float ZUI_ICON_SOURCE_H       = 34.f; // Header — teal badge, "<>" brackets
    static constexpr float ZUI_ICON_SOURCE_JSON    = 35.f; // JSON   — yellow badge, "{}" symbol

    /// @brief Bitwise-OR two ZUIBoxFlags values.
    /// @param a First flags value.
    /// @param b Second flags value.
    /// @returns Combined ZUIBoxFlags.
    inline ZUIBoxFlags     operator|(ZUIBoxFlags a, ZUIBoxFlags b)
    {
        return static_cast<ZUIBoxFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    /// @brief Primary layout axis for a container box.
    enum class ZUIAxis : uint8_t
    {
        X = 0, ///< Children are laid out left to right.
        Y = 1  ///< Children are laid out top to bottom.
    };

    /// @brief Horizontal alignment for text drawn inside a box.
    enum class ZUITextAlign : uint8_t
    {
        Left   = 0, ///< Text starts at the left edge (plus padding).
        Center = 1, ///< Text is horizontally centered.
        Right  = 2  ///< Text is right-aligned (minus padding).
    };

    /// @brief Font size variant; selects one of the three atlas slots.
    enum class ZUIFontSize : uint8_t
    {
        Small  = 0, ///< Small UI text (e.g. status bar, tooltips).
        Body   = 1, ///< Default body text.
        Header = 2  ///< Section headers and panel titles.
    };

    // Corner order: [0]=TL  [1]=TR  [2]=BL  [3]=BR
    // Matches gl_VertexID order in the SDF vertex shader.
    static constexpr int ZUI_CORNER_TL = 0;
    static constexpr int ZUI_CORNER_TR = 1;
    static constexpr int ZUI_CORNER_BL = 2;
    static constexpr int ZUI_CORNER_BR = 3;

    /// @brief Core building block of the ZUI retained box tree.
    ///
    /// Every visible or interactive element is represented as a ZUIBox.
    /// Boxes are arena-allocated from FrameArena each frame by ZUIPushBox;
    /// all ZUIBox pointers are stale after the next ZUIBeginFrame call.
    ///
    /// Build-time fields (Flags, Size, colors, etc.) are set by widget functions
    /// during the BuildUI pass.  Layout fields (ComputedSize, ScreenMin, ScreenMax)
    /// are written by ZUILayoutSolve at the start of ZUIEndFrame.
    struct ZUIBox
    {
        uint64_t     Key             = 0; ///< FNV-1a hash of the key string; 0 = unnamed.
        ZUIStr       Label           = {}; ///< Visible text (FrameArena slice; may be null).

        // tree — arena pointers valid for one frame only
        ZUIBox*      Parent          = nullptr;
        ZUIBox*      FirstChild      = nullptr;
        ZUIBox*      LastChild       = nullptr;
        ZUIBox*      NextSib         = nullptr;
        ZUIBox*      PrevSib         = nullptr;

        // build-time spec
        ZUIBoxFlags  Flags           = ZUI_None;
        ZUISize      Size[2]         = {};
        ZUIAxis      LayoutAxis      = ZUIAxis::Y;
        ZUITextAlign TextAlign       = ZUITextAlign::Left;
        ZUIFontSize  FontSize        = ZUIFontSize::Body;

        // Per-corner colors (RAD approach).
        // Colors[0]=TL  [1]=TR  [2]=BL  [3]=BR, each RGBA float[4].
        // Use ZUIBoxSetColor / ZUIBoxSetGradientV helpers below.
        float        Colors[4][4]    = {}; // all transparent by default
        float        TextColor[4]    = {};
        float        BorderColor[4]  = {};

        // Per-corner radii + edge softness (RAD SDF renderer).
        // CornerRadii[0]=TL [1]=TR [2]=BL [3]=BR.
        // EdgeSoftness: AA ramp width in pixels (0.5 = 1 pixel, 0 = hard edge).
        float        CornerRadii[4]  = {};
        float        EdgeSoftness    = 0.5f;

        float        BorderThickness = 0.f;
        float        FloatPos[2]     = {};
        float        Padding[4]      = {}; // left, top, right, bottom
        uint32_t     TextureIndex    = 0xFFFFFFFFu;

        // layout output — filled by ZUILayout::Solve
        float        ComputedSize[2] = {};
        float        ScreenMin[2]    = {};
        float        ScreenMax[2]    = {};
    };

    // Inline helpers — set colors and radii in a single call

    /// @brief Set all four corner background colors to the same RGBA value.
    /// @param b  Target box.
    /// @param r  Red channel [0,1].
    /// @param g  Green channel [0,1].
    /// @param bl Blue channel [0,1].
    /// @param a  Alpha channel [0,1].
    inline void ZUIBoxSetColor(ZUIBox* b, float r, float g, float bl, float a)
    {
        for (int i = 0; i < 4; ++i)
        {
            b->Colors[i][0] = r;
            b->Colors[i][1] = g;
            b->Colors[i][2] = bl;
            b->Colors[i][3] = a;
        }
    }

    /// @brief Set all four corner background colors from a packed float[4] RGBA array.
    /// @param b Target box.
    /// @param c RGBA float[4] color array.
    inline void ZUIBoxSetColorArr(ZUIBox* b, const float c[4])
    {
        ZUIBoxSetColor(b, c[0], c[1], c[2], c[3]);
    }

    /// @brief Apply a vertical gradient: top corners get @p top, bottom corners get @p bot.
    /// @param b   Target box.
    /// @param top Top color as RGBA float[4].
    /// @param bot Bottom color as RGBA float[4].
    inline void ZUIBoxSetGradientV(ZUIBox* b, const float top[4], const float bot[4])
    {
        for (int ch = 0; ch < 4; ++ch)
        {
            b->Colors[ZUI_CORNER_TL][ch] = top[ch];
            b->Colors[ZUI_CORNER_TR][ch] = top[ch];
            b->Colors[ZUI_CORNER_BL][ch] = bot[ch];
            b->Colors[ZUI_CORNER_BR][ch] = bot[ch];
        }
    }

    /// @brief Set all four corner radii to the same value.
    /// @param b Target box.
    /// @param r Corner radius in logical pixels.
    inline void ZUIBoxSetCornerRadius(ZUIBox* b, float r)
    {
        b->CornerRadii[0] = b->CornerRadii[1] = b->CornerRadii[2] = b->CornerRadii[3] = r;
    }

    /// @brief Set only the top-left and top-right corner radii (e.g. for top-rounded tabs).
    /// @param b Target box.
    /// @param r Corner radius applied to top-left and top-right corners.
    inline void ZUIBoxSetTopRadius(ZUIBox* b, float r)
    {
        b->CornerRadii[ZUI_CORNER_TL] = b->CornerRadii[ZUI_CORNER_TR] = r;
    }

    /// @brief Set only the bottom-left and bottom-right corner radii.
    /// @param b Target box.
    /// @param r Corner radius applied to bottom-left and bottom-right corners.
    inline void ZUIBoxSetBottomRadius(ZUIBox* b, float r)
    {
        b->CornerRadii[ZUI_CORNER_BL] = b->CornerRadii[ZUI_CORNER_BR] = r;
    }

} // namespace ZEngine::UI
