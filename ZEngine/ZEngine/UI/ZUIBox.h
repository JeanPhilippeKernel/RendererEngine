#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>
#include <cstring>

namespace ZEngine::UI
{
    struct ZUIStr
    {
        const char* Ptr = nullptr;
        uint32_t    Len = 0;
    };

    enum class ZUISizeKind : uint8_t
    {
        Pixels,
        Text,
        ChildrenSum,
        ParentPercent,
        Fill
    };

    // Strictness: 1.0 = rigid (never shrunk), 0.0 = fully flexible (absorbed first).
    struct ZUISize
    {
        ZUISizeKind Kind       = ZUISizeKind::Pixels;
        float       Value      = 0.f;
        float       Strictness = 1.f;
    };

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
        ZUI_DrawCheckmark  = 1 << 8,  // ✓ polyline stroke inside the box
        ZUI_DrawCircleFill = 1 << 9,  // filled circle inscribed in box center
        ZUI_DrawTriArrow   = 1 << 10, // collapse arrow; direction from UserData (0=right, 1=down)
        ZUI_DropShadow     = 1 << 11, // dark offset rect emitted behind background
        ZUI_DrawPlotLines  = 1 << 12, // line chart; data in Label.Ptr/Len, range in Padding
        ZUI_DrawPlotBars   = 1 << 13, // bar chart; same data layout as DrawPlotLines
    };

    inline ZUIBoxFlags operator|(ZUIBoxFlags a, ZUIBoxFlags b)
    {
        return static_cast<ZUIBoxFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    enum class ZUIAxis : uint8_t
    {
        X,
        Y
    };
    enum class ZUITextAlign : uint8_t
    {
        Left = 0,
        Center,
        Right
    };
    enum class ZUIFontSize : uint8_t
    {
        Small  = 0,
        Body   = 1,
        Header = 2
    };

    // Corner order: [0]=TL  [1]=TR  [2]=BL  [3]=BR
    // Matches gl_VertexID order in the SDF vertex shader.
    static constexpr int ZUI_CORNER_TL = 0;
    static constexpr int ZUI_CORNER_TR = 1;
    static constexpr int ZUI_CORNER_BL = 2;
    static constexpr int ZUI_CORNER_BR = 3;

    struct ZUIBox
    {
        uint64_t     Key             = 0;
        ZUIStr       Label           = {};

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

    inline void ZUIBoxSetColorArr(ZUIBox* b, const float c[4])
    {
        ZUIBoxSetColor(b, c[0], c[1], c[2], c[3]);
    }

    // Vertical gradient: top row gets `top`, bottom row gets `bot`.
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

    inline void ZUIBoxSetCornerRadius(ZUIBox* b, float r)
    {
        b->CornerRadii[0] = b->CornerRadii[1] = b->CornerRadii[2] = b->CornerRadii[3] = r;
    }

    inline void ZUIBoxSetTopRadius(ZUIBox* b, float r)
    {
        b->CornerRadii[ZUI_CORNER_TL] = b->CornerRadii[ZUI_CORNER_TR] = r;
    }

    inline void ZUIBoxSetBottomRadius(ZUIBox* b, float r)
    {
        b->CornerRadii[ZUI_CORNER_BL] = b->CornerRadii[ZUI_CORNER_BR] = r;
    }

} // namespace ZEngine::UI
