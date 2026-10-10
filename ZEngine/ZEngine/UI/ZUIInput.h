#pragma once
#include <ZEngine/UI/ZUIKey.h>
#include <cstdint>

namespace ZEngine::UI
{
    struct ZUIContext;

    /// @brief ZUI Input Feed API — translate native events into ZUI calls.
    ///
    /// Callers (e.g. Editor::ProcessEvent) call these instead of writing
    /// to ZUIContext fields directly. ZUI has no dependency on the host event system.
    ///
    /// Call order each frame:
    ///   1. ZUIFeedBeginFrame(ctx)          — reset per-frame transient state
    ///   2. ZUIFeedMousePos / Button / ...  — any order
    ///   3. ZUIBeginFrame(ctx, dt)          — ZUI frame starts
    ///   4. (build UI)
    ///   5. ZUIEndFrame(ctx)

    /// @brief Reset per-frame transient input state.
    ///
    /// Clears MousePressed[], MouseReleased[], TextInput, and ScrollDelta.
    /// Must be called once per frame before feeding any events.
    /// @param ctx Active ZUI context.
    void ZUIFeedBeginFrame(ZUIContext* ctx);

    /// @brief Update the mouse cursor position.
    /// @param ctx Active ZUI context.
    /// @param x X in logical pixels (matches glfwGetCursorPos units).
    /// @param y Y in logical pixels.
    void ZUIFeedMousePos(ZUIContext* ctx, float x, float y);

    /// @brief Report a mouse button press or release.
    /// @param ctx     Active ZUI context.
    /// @param btn     Button index: 0=left, 1=right, 2=middle.
    /// @param pressed true on press, false on release.
    void ZUIFeedMouseButton(ZUIContext* ctx, int btn, bool pressed);

    /// @brief Accumulate a vertical scroll delta for this frame.
    /// @param ctx   Active ZUI context.
    /// @param delta Positive = scroll up, negative = scroll down.
    void ZUIFeedScroll(ZUIContext* ctx, float delta);

    /// @brief Submit a single Unicode codepoint from the system text-input callback.
    /// @param ctx       Active ZUI context.
    /// @param codepoint UTF-32 codepoint (e.g. from glfwSetCharCallback).
    void ZUIFeedText(ZUIContext* ctx, uint32_t codepoint);

    /// @brief Report a key press or release with current modifier state.
    /// @param ctx     Active ZUI context.
    /// @param key     ZUIKey code for the key that changed.
    /// @param pressed true on press, false on release.
    /// @param ctrl    true when Ctrl is held at the time of this event.
    /// @param shift   true when Shift is held.
    /// @param alt     true when Alt is held.
    void ZUIFeedKey(ZUIContext* ctx, ZUIKey key, bool pressed, bool ctrl, bool shift, bool alt);

} // namespace ZEngine::UI
