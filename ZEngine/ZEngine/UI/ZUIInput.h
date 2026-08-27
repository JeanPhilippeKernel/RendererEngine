#pragma once
#include <ZEngine/UI/ZUIKey.h>
#include <cstdint>

namespace ZEngine::UI
{
    struct ZUIContext;

    // ZUI Input Feed API
    //
    // Callers (e.g. Editor::ProcessEvent) translate their native event
    // types into these calls. ZUI lib has no dependency on the host's
    // event system.
    //
    // Call order each frame:
    //   1. ZUIFeedBeginFrame(ctx)          — reset per-frame transient state
    //   2. ZUIFeedMousePos / Button / ...  — any order
    //   3. ZUIBeginFrame(ctx, dt)          — ZUI frame starts
    //   4. (build UI)
    //   5. ZUIEndFrame(ctx)

    // Must be called once before feeding events, before ZUIBeginFrame.
    // Clears MousePressed[], MouseReleased[], TextInput, ScrollDelta.
    void ZUIFeedBeginFrame(ZUIContext* ctx);

    // Mouse position in logical pixels (matches glfwGetCursorPos units).
    void ZUIFeedMousePos(ZUIContext* ctx, float x, float y);

    // btn: 0=left, 1=right, 2=middle
    void ZUIFeedMouseButton(ZUIContext* ctx, int btn, bool pressed);

    // Vertical scroll delta (positive = up, negative = down).
    void ZUIFeedScroll(ZUIContext* ctx, float delta);

    // Single Unicode codepoint from text input (e.g. glfwSetCharCallback).
    void ZUIFeedText(ZUIContext* ctx, uint32_t codepoint);

    // Key press or release. Modifier flags are the current state when the
    // key event fires (not just the modifier keys themselves).
    void ZUIFeedKey(ZUIContext* ctx, ZUIKey key, bool pressed, bool ctrl, bool shift, bool alt);

} // namespace ZEngine::UI
