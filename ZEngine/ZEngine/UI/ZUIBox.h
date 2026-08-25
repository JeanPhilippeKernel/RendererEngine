#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

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
    };

    inline ZUIBoxFlags operator|(ZUIBoxFlags a, ZUIBoxFlags b)
    {
        return static_cast<ZUIBoxFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    enum class ZUIAxis : uint8_t { X, Y };

    enum class ZUITextAlign : uint8_t { Left = 0, Center, Right };
    enum class ZUIFontSize  : uint8_t { Small = 0, Body = 1, Header = 2 };

    struct ZUIBox
    {
        uint64_t    Key   = 0;
        ZUIStr      Label = {};

        // tree — arena pointers valid for one frame only
        ZUIBox*     Parent     = nullptr;
        ZUIBox*     FirstChild = nullptr;
        ZUIBox*     LastChild  = nullptr;
        ZUIBox*     NextSib    = nullptr;
        ZUIBox*     PrevSib    = nullptr;

        // build-time spec
        ZUIBoxFlags Flags            = ZUI_None;
        ZUISize     Size[2]          = {};
        ZUIAxis      LayoutAxis       = ZUIAxis::Y;
        ZUITextAlign TextAlign       = ZUITextAlign::Left;
        ZUIFontSize  FontSize        = ZUIFontSize::Body;
        float       BgColor[4]       = {};
        float       BorderColor[4]   = {};
        float       TextColor[4]     = {};
        float       CornerRadius     = 0.f;
        float       BorderThickness  = 0.f;
        float       FloatPos[2]      = {}; // screen offset from parent origin; used when ZUI_FloatX / ZUI_FloatY is set
        // Padding[0]=left  [1]=top  [2]=right  [3]=bottom
        // Offsets children away from the container walls.
        // ChildrenSum adds both ends; Fill subtracts both ends from available space.
        float       Padding[4]       = {};
        uint32_t    TextureIndex     = 0xFFFFFFFFu; // bindless slot; when != 0xFFFFFFFF and ZUI_DrawBackground set, renders as image

        // layout output — filled by ZUILayout::Solve
        float       ComputedSize[2]  = {};
        float       ScreenMin[2]     = {};
        float       ScreenMax[2]     = {};
    };

} // namespace ZEngine::UI
