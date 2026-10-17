#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>
#include <cstring>

namespace ZEngine::UI
{
    struct ZUIBox;
    struct ZUIDrawList;
    using ZUIDrawFn = void (*)(ZUIDrawList*, const ZUIBox*, const void*);
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
        ZUI_None             = 0,
        ZUI_DrawBackground   = 1 << 0,
        ZUI_DrawBorder       = 1 << 1,
        ZUI_DrawText         = 1 << 2,
        ZUI_Clickable        = 1 << 3,
        ZUI_Scrollable       = 1 << 4,
        ZUI_ClipChildren     = 1 << 5,
        ZUI_FloatX           = 1 << 6,
        ZUI_FloatY           = 1 << 7,
        // Draw-list shape overlays — rendered by PreparePayload after background/border.
        // All use TextColor as the stroke/fill color.
        ZUI_DrawCheckmark    = 1 << 8,  ///< Checkmark polyline stroke inside the box.
        ZUI_DrawCircleFill   = 1 << 9,  ///< Filled circle inscribed in box center.
        ZUI_DrawTriArrow     = 1 << 10, ///< Collapse arrow; direction from UserData (0=right, 1=down).
        ZUI_DropShadow       = 1 << 11, ///< Blurred shadow emitted behind the box, before its own clip.
        ZUI_DrawPlotLines    = 1 << 12, ///< Line chart; data in Label.Ptr/Len, range in Padding.
        ZUI_DrawPlotBars     = 1 << 13, ///< Bar chart; same data layout as DrawPlotLines.
        ZUI_DrawActorIcon    = 1 << 14, ///< Actor-type icon; shape from UserData (see ZUI_ICON_* constants).
        ZUI_DrawIcon         = 1 << 15, ///< Lucide icon glyph centred in the box; id from ZUIBox::IconId, tinted with TextColor.
        ZUI_Disabled         = 1 << 16, ///< No mouse/keyboard interaction, including stale active/focus state.
        ZUI_NoKeyboardFocus  = 1 << 17, ///< Pointer-only surface (scroll lanes, resize handles, viewport).
        ZUI_KeyboardActivate = 1 << 18, ///< Space/Enter produces Clicked for custom controls.
        ZUI_NavScope         = 1 << 19, ///< Dock panel navigation group; children stay together in Tab order.
        ZUI_Tooltip          = 1 << 20, ///< Flip the floated box at window edges after measuring it.
        ZUI_KeyboardFocused  = 1 << 21, ///< Frame-local focus ring, assigned after navigation/layout.
        ZUI_NavList          = 1 << 22, ///< Up/Down navigates focusable descendants (lists and trees).
        ZUI_DashedBorder     = 1 << 23, ///< DrawBorder uses 3 px dashes and gaps; keyboard focus remains solid.
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
    static constexpr float ZUI_ICON_SOURCE_H       = 34.f; // Header source-code badge, "<>" brackets
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

    /// @brief Placement of children inside a container's content area.
    ///
    /// Used for both axes: MainAlign positions children along ZUIBox::LayoutAxis,
    /// CrossAlign positions each child across it. Alignment only has an effect when
    /// free space remains; Fill children consume it first.
    enum class ZUIAlign : uint8_t
    {
        Start        = 0, ///< Pack at the start edge (default; matches the pre-alignment layout).
        Center       = 1, ///< Center in the free space.
        End          = 2, ///< Pack at the end edge.
        SpaceBetween = 3  ///< Main axis only: spread free space between children. Acts as Start on the cross axis.
    };

    /// @brief Font role; selects a size/weight from the shared atlas.
    enum class ZUIFontSize : uint8_t
    {
        Small   = 0, ///< Small UI text (e.g. status bar, tooltips).
        Body    = 1, ///< Default body text.
        Header  = 2, ///< Large titles (12 px semibold).
        Tiny    = 3, ///< Smallest caption text (Figma 9 px).
        Large   = 4, ///< Large regular text (12 px).
        Heading = 5  ///< Section headers and axis labels (11 px semibold).
    };

    /// @brief Figma effect recipe used by boxes with ZUI_DropShadow.
    enum class ZUIShadowKind : uint8_t
    {
        Menu  = 0,
        Modal = 1
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
        uint64_t      Key             = 0;  ///< FNV-1a hash of the key string; 0 = unnamed.
        ZUIStr        Label           = {}; ///< Visible text (FrameArena slice; may be null).

        // tree — arena pointers valid for one frame only
        ZUIBox*       Parent          = nullptr;
        ZUIBox*       FirstChild      = nullptr;
        ZUIBox*       LastChild       = nullptr;
        ZUIBox*       NextSib         = nullptr;
        ZUIBox*       PrevSib         = nullptr;

        // build-time spec
        ZUIBoxFlags   Flags           = ZUI_None;
        ZUISize       Size[2]         = {};
        float         MinSize[2]      = {}; ///< Intrinsic-size floor; controls can hug growing labels above a design minimum.
        ZUIAxis       LayoutAxis      = ZUIAxis::Y;
        ZUIAlign      MainAlign       = ZUIAlign::Start; ///< Children placement along LayoutAxis.
        ZUIAlign      CrossAlign      = ZUIAlign::Start; ///< Children placement across LayoutAxis.
        float         Gap             = 0.f;             ///< Spacing between consecutive in-flow children (logical px).
        ZUITextAlign  TextAlign       = ZUITextAlign::Left;
        ZUIFontSize   FontSize        = ZUIFontSize::Body;

        // Per-corner colors (RAD approach).
        // Colors[0]=TL  [1]=TR  [2]=BL  [3]=BR, each RGBA float[4].
        // Use ZUIBoxSetColor / ZUIBoxSetGradientV helpers below.
        float         Colors[4][4]    = {}; // all transparent by default
        float         TextColor[4]    = {};
        float         BorderColor[4]  = {};

        // Per-corner radii + edge softness (RAD SDF renderer).
        // CornerRadii[0]=TL [1]=TR [2]=BL [3]=BR.
        // EdgeSoftness: AA ramp width in pixels (0.5 = 1 pixel, 0 = hard edge).
        float         CornerRadii[4]  = {};
        float         EdgeSoftness    = 0.5f;

        float         BorderThickness = 0.f;
        ZUIShadowKind ShadowKind      = ZUIShadowKind::Menu;
        float         FloatPos[2]     = {};
        float         FloatAnchor[2]  = {};          ///< Parent-size fraction added to FloatPos on floating axes (resolved this frame).
        float         MinHitSize[2]   = {0.f, 22.f}; ///< Centered hit rectangle, independent of draw geometry; ancestors still clip it.
        uint32_t      NavOrder        = 0;           ///< Dock traversal order on ZUI_NavScope roots.
        ZUIBox*       FocusRingTarget = nullptr;     ///< Optional composite frame (e.g. search field), valid this frame only.
        float         Padding[4]      = {};          // left, top, right, bottom
        uint32_t      TextureIndex    = 0xFFFFFFFFu;
        bool          TextureFlipV    = false; ///< Sample TextureIndex upside down (file-decoded images vs render targets).
        uint16_t      IconId          = 0;     ///< ZUIIcon for ZUI_DrawIcon (index into ZUIFontAtlas::Icon).
        bool          ArrowFitBounds  = false; ///< ZUI_DrawTriArrow: exact box silhouette for color-bar markers.

        /// Frame-local drawing, after background and before border/text. The
        /// renderer supplies solved coordinates and the current ancestor clip.
        ZUIDrawFn     CustomDraw      = nullptr;
        const void*   CustomDrawData  = nullptr;

        // layout output — filled by ZUILayout::Solve
        float         ComputedSize[2] = {};
        float         ScreenMin[2]    = {};
        float         ScreenMax[2]    = {};
        float         FlowOffset      = 0.f; ///< Resolved main-axis offset of the first child (MainAlign Center/End).
        float         FlowGap         = 0.f; ///< Resolved spacing between children (Gap, plus SpaceBetween share).
    };

    // Inline helpers — set colors and radii in a single call

    /// @brief Set the spacing between consecutive in-flow children of @p b.
    /// @param b   Target container.
    /// @param gap Spacing in logical pixels; floated children are ignored.
    inline void ZUIBoxSetGap(ZUIBox* b, float gap)
    {
        b->Gap = gap;
    }

    /// @brief Set how @p b places its children along and across its layout axis.
    /// @param b     Target container.
    /// @param main  Placement along ZUIBox::LayoutAxis.
    /// @param cross Placement across ZUIBox::LayoutAxis.
    inline void ZUIBoxSetAlign(ZUIBox* b, ZUIAlign main, ZUIAlign cross = ZUIAlign::Start)
    {
        b->MainAlign  = main;
        b->CrossAlign = cross;
    }

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
