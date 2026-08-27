#pragma once
#include <ZEngine/UI/ZUIContext.h>

namespace ZEngine::UI
{
    enum ZUISignalFlags : uint32_t
    {
        ZUI_SignalNone          = 0,
        ZUI_SignalHovered       = 1 << 0,
        ZUI_SignalPressed       = 1 << 1,
        ZUI_SignalReleased      = 1 << 2,
        ZUI_SignalHeld          = 1 << 3,
        ZUI_SignalClicked       = 1 << 4,
        ZUI_SignalDoubleClicked = 1 << 5,
        ZUI_SignalScrolled      = 1 << 6,
        ZUI_SignalKeyboardFocus = 1 << 7,
    };

    struct ZUISignal
    {
        uint32_t Flags        = ZUI_SignalNone;
        float    DragDelta[2] = {};
        float    ScrollDelta  = 0.f;
    };

    // Called from ZUIEndFrame — walks the box tree and updates HotKey / ActiveKey on ctx
    void      ZUIInteractionPass(ZUIContext* ctx);

    // Called per-widget after the build phase to query interaction state for a specific box.
    // Also advances HotT / ActiveT animation on the box's persistent state.
    ZUISignal ZUISignalFromBox(ZUIContext* ctx, ZUIBox* box);

} // namespace ZEngine::UI
