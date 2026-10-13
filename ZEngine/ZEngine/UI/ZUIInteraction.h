#pragma once
#include <ZEngine/UI/ZUIContext.h>

namespace ZEngine::UI
{
    /// @brief Bit flags returned by ZUISignalFromBox describing what happened to a box this frame.
    enum ZUISignalFlags : uint32_t
    {
        ZUI_SignalNone          = 0,      ///< No interaction.
        ZUI_SignalHovered       = 1 << 0, ///< Cursor is over the box.
        ZUI_SignalPressed       = 1 << 1, ///< Left mouse button pressed this frame over the box.
        ZUI_SignalReleased      = 1 << 2, ///< Left mouse button released this frame while box was active.
        ZUI_SignalHeld          = 1 << 3, ///< Left mouse button held with box active; DragDelta is valid.
        ZUI_SignalClicked       = 1 << 4, ///< Full press+release cycle completed over the box (no drag).
        ZUI_SignalDoubleClicked = 1 << 5, ///< Two clicks within the double-click time window.
        ZUI_SignalScrolled      = 1 << 6, ///< Mouse wheel turned while hovered; ScrollDelta is valid.
        ZUI_SignalKeyboardFocus = 1 << 7, ///< Box holds keyboard focus (FocusKey == box->Key).
    };

    /// @brief Per-frame interaction result for one box.
    struct ZUISignal
    {
        uint32_t Flags        = ZUI_SignalNone; ///< Bitmask of ZUISignalFlags.
        float    DragDelta[2] = {};             ///< Mouse delta (px) this frame when ZUI_SignalHeld.
        float    ScrollDelta  = 0.f;            ///< Wheel delta when ZUI_SignalScrolled.
    };

    /// @brief Called from ZUIEndFrame — DFS hit-test walk that updates HotKey / ActiveKey on ctx.
    /// @param ctx Active ZUI context.
    void                                 ZUIInteractionPass(ZUIContext* ctx);

    /// @brief Starts an exclusive viewport-tool pointer capture.
    /// @return A non-zero token on success. A capture cannot be stolen and a pending end
    ///         notification must be consumed before another capture starts.
    [[nodiscard]] ZUIPointerCaptureToken ZUICapturePointer(ZUIContext* ctx, uint64_t owner_key, uint64_t viewport_key, uint64_t viewport_generation, uint8_t button = 0);
    [[nodiscard]] bool                   ZUIReleasePointerCapture(ZUIContext* ctx, ZUIPointerCaptureToken token);
    [[nodiscard]] bool                   ZUICancelPointerCapture(ZUIContext* ctx, ZUIPointerCaptureToken token, ZUIPointerCaptureEndReason reason = ZUIPointerCaptureEndReason::ExplicitCancellation);
    [[nodiscard]] bool                   ZUIHasPointerCapture(const ZUIContext* ctx, ZUIPointerCaptureToken token);
    [[nodiscard]] bool                   ZUIConsumePointerCaptureEnd(ZUIContext* ctx, ZUIPointerCaptureToken token, ZUIPointerCaptureEndReason& out_reason);

    /// @brief Cancels pointer interactions and clears held buttons/modifiers on
    /// window focus loss, preserving logical keyboard focus and widget values.
    void                                 ZUISetPointerCaptureWindowFocused(ZUIContext* ctx, bool focused);
    /// @internal Called after the frame's viewport/modal state has been rebuilt.
    void                                 ZUIProcessPointerCaptureEndOfFrame(ZUIContext* ctx);

    /// @brief Query interaction state for @p box after the build phase.
    ///
    /// Also advances HotT / ActiveT animation lerps on the box's persistent state.
    /// @param ctx Active ZUI context.
    /// @param box Box to query signals for.
    /// @returns Interaction signal valid for this frame only.
    ZUISignal                            ZUISignalFromBox(ZUIContext* ctx, ZUIBox* box);

} // namespace ZEngine::UI
