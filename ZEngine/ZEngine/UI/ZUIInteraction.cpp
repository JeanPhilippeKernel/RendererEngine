#include <ZEngine/UI/ZUIInteraction.h>
#include <cmath>

namespace ZEngine::UI
{
    namespace
    {
        void EndPointerCapture(ZUIContext* ctx, ZUIPointerCaptureEndReason reason)
        {
            if (!ctx || !ctx->PointerCapture.Active)
                return;

            ctx->PointerCaptureEnd = {
                .Token    = ctx->PointerCapture.Token,
                .OwnerKey = ctx->PointerCapture.OwnerKey,
                .Reason   = reason,
                .Pending  = true,
            };
            ctx->PointerCapture.Active = false;
        }
    } // namespace

    ZUIPointerCaptureToken ZUICapturePointer(ZUIContext* ctx, uint64_t owner_key, uint64_t viewport_key, uint64_t viewport_generation, uint8_t button)
    {
        if (!ctx || owner_key == 0 || viewport_key == 0 || viewport_generation == 0 || button >= 3 || ctx->PointerCapture.Active || ctx->PointerCaptureEnd.Pending)
            return 0;

        ZUIPointerCaptureToken token = ctx->NextPointerCaptureToken++;
        if (token == 0)
            token = ctx->NextPointerCaptureToken++;
        ctx->PointerCapture = {
            .Token              = token,
            .OwnerKey           = owner_key,
            .ViewportKey        = viewport_key,
            .ViewportGeneration = viewport_generation,
            .Button             = button,
            .Active             = true,
        };
        // Do not let a regular ZUI box continue a drag beneath a native tool.
        ctx->ActiveKey     = 0;
        ctx->DragSourceKey = 0;
        return token;
    }

    bool ZUIReleasePointerCapture(ZUIContext* ctx, ZUIPointerCaptureToken token)
    {
        if (!ctx || !ctx->PointerCapture.Active || token == 0 || ctx->PointerCapture.Token != token)
            return false;
        EndPointerCapture(ctx, ZUIPointerCaptureEndReason::Released);
        return true;
    }

    bool ZUICancelPointerCapture(ZUIContext* ctx, ZUIPointerCaptureToken token, ZUIPointerCaptureEndReason reason)
    {
        if (!ctx || !ctx->PointerCapture.Active || token == 0 || ctx->PointerCapture.Token != token || reason == ZUIPointerCaptureEndReason::None || reason == ZUIPointerCaptureEndReason::Released)
            return false;
        EndPointerCapture(ctx, reason);
        return true;
    }

    bool ZUIHasPointerCapture(const ZUIContext* ctx, ZUIPointerCaptureToken token)
    {
        return ctx && token != 0 && ctx->PointerCapture.Active && ctx->PointerCapture.Token == token;
    }

    bool ZUIConsumePointerCaptureEnd(ZUIContext* ctx, ZUIPointerCaptureToken token, ZUIPointerCaptureEndReason& out_reason)
    {
        if (!ctx || token == 0 || !ctx->PointerCaptureEnd.Pending || ctx->PointerCaptureEnd.Token != token)
            return false;
        out_reason             = ctx->PointerCaptureEnd.Reason;
        ctx->PointerCaptureEnd = {};
        return true;
    }

    void ZUISetPointerCaptureWindowFocused(ZUIContext* ctx, bool focused)
    {
        if (!focused && ctx && ctx->PointerCapture.Active)
            EndPointerCapture(ctx, ZUIPointerCaptureEndReason::WindowFocusLost);
    }

    void ZUIProcessPointerCaptureEndOfFrame(ZUIContext* ctx)
    {
        if (!ctx || !ctx->PointerCapture.Active)
            return;

        const ZUIPointerCapture capture = ctx->PointerCapture;
        if (ctx->EscapePressed)
            EndPointerCapture(ctx, ZUIPointerCaptureEndReason::Escape);
        else if (ctx->ViewportInputKey != capture.ViewportKey || ctx->ViewportGeneration != capture.ViewportGeneration)
            EndPointerCapture(ctx, ZUIPointerCaptureEndReason::ViewportUnavailable);
        else if (ctx->ActiveModalKey != 0 || ctx->ModalBox != nullptr || ctx->PopupStackSize != 0)
            EndPointerCapture(ctx, ZUIPointerCaptureEndReason::ModalInterruption);
        else if (ctx->MouseReleased[capture.Button])
            EndPointerCapture(ctx, ZUIPointerCaptureEndReason::Released);
    }

    static bool PointInBox(const float pt[2], const ZUIBox* box)
    {
        return pt[0] >= box->ScreenMin[0] && pt[0] <= box->ScreenMax[0] && pt[1] >= box->ScreenMin[1] && pt[1] <= box->ScreenMax[1];
    }

    void ZUIInteractionPass(ZUIContext* ctx)
    {
        if (ctx && ctx->PointerCapture.Active)
        {
            // The owning native tool receives raw pointer input. Regular widgets
            // must neither become hot nor receive a new active drag underneath it.
            ctx->HotKey        = 0;
            ctx->ActiveKey     = 0;
            ctx->DragSourceKey = 0;
            return;
        }
        if (!ctx->Root)
        {
            ctx->HotKey = 0;
            return;
        }

        uint32_t max             = ctx->MaxBoxesPerFrame;
        auto     scratch         = ZGetScratch(&ctx->FrameArena);
        ZUIBox** stack           = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        uint32_t stack_top       = 0;

        uint64_t new_hot         = 0;
        uint64_t new_scroll_key  = 0;
        ZUIAxis  new_scroll_axis = ZUIAxis::Y; // axis of the nearest scrollable box

        // HOT PATH — runs every frame, no heap allocation allowed.
        stack[stack_top++]       = ctx->Root;
        while (stack_top > 0)
        {
            ZUIBox* box = stack[--stack_top];
            if (!box)
            {
                continue;
            }

            bool under_cursor = PointInBox(ctx->MousePos, box);

            if (under_cursor)
            {
                // Popups are portaled to the root so they render over their owner.
                // A popup opened from a modal therefore is not a descendant of the
                // modal box, but it is still modal input and must remain interactive.
                // Determine both memberships before applying the modal/popup gates.
                bool inside_modal = false;
                bool inside_popup = false;
                for (ZUIBox* p = box; p && (!inside_modal || !inside_popup); p = p->Parent)
                {
                    if (p == ctx->ModalBox)
                        inside_modal = true;
                    for (uint32_t pi = 0; pi < ctx->PopupStackSize && !inside_popup; ++pi)
                        if (p == ctx->PopupStack[pi].Box)
                            inside_popup = true;
                }

                bool can_hover = (!ctx->ModalBox || inside_modal || inside_popup) && (!ctx->PopupStackSize || inside_popup);

                if (can_hover)
                {
                    if (box->Flags & ZUI_Clickable)
                    {
                        new_hot = box->Key;
                    }
                    if (box->Flags & ZUI_Scrollable)
                    {
                        new_scroll_key  = box->Key;
                        new_scroll_axis = box->LayoutAxis;
                    }
                }
            }

            for (ZUIBox* child = box->FirstChild; child; child = child->NextSib)
            {
                if (stack_top < max)
                {
                    stack[stack_top++] = child;
                }
                // else: subtree silently skipped (acceptable degradation, not corruption)
            }
        }

        ZReleaseScratch(scratch);

        // Scroll input — both wheel and keyboard write to ScrollYTarget.
        // ZUIBeginScrollRegion lerps ScrollY toward ScrollYTarget each frame (smooth scroll).
        // MaxScrollY is owned by the layout pass; we only read it here for clamping.
        if (new_scroll_key != 0)
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, new_scroll_key);
            if (ps)
            {
                float max_y       = fmaxf(ps->MaxScrollY, 0.f);
                float max_x       = fmaxf(ps->MaxScrollX, 0.f);
                bool  changed     = false;

                // Clamp to [lo, hi]. When MaxScrollY is 0 (first frame, no overflow yet),
                // hi == 0 and the upper clamp correctly prevents any scroll accumulation.
                auto  clampScroll = [](float v, float lo, float hi) {
                    if (v < lo)
                        v = lo;
                    if (v > hi)
                        v = hi;
                    return v;
                };

                // Mouse wheel — RAD Debugger model: only the target is written here.
                // ZUIBeginScrollRegion animates ScrollY toward ScrollYTarget each frame.
                // Writing both here would make them always equal, killing the animation.
                if (ctx->ScrollDelta != 0.f)
                {
                    if (new_scroll_axis == ZUIAxis::X)
                        ps->ScrollXTarget = clampScroll(ps->ScrollXTarget - ctx->ScrollDelta * ctx->Style.MouseScrollSpeed, 0.f, max_x);
                    else
                        ps->ScrollYTarget = clampScroll(ps->ScrollYTarget - ctx->ScrollDelta * ctx->Style.MouseScrollSpeed, 0.f, max_y);
                    changed = true;
                }

                // Keyboard scroll — active when no widget holds focus.
                // Uses instant snap (sets ScrollY = ScrollYTarget) for crisp keypress feel.
                // Step = 5 × FrameHeight ≈ ImGui's 5 × FontSize rule.
                if (ctx->FocusKey == 0)
                {
                    float kLine = ctx->Style.FrameHeight * 5.f;
                    bool  kb    = false;
                    if (ctx->ArrowUpPressed)
                    {
                        ps->ScrollYTarget = clampScroll(ps->ScrollYTarget - kLine, 0.f, max_y);
                        kb                = true;
                    }
                    if (ctx->ArrowDownPressed)
                    {
                        ps->ScrollYTarget = clampScroll(ps->ScrollYTarget + kLine, 0.f, max_y);
                        kb                = true;
                    }
                    if (ctx->HomePressed)
                    {
                        ps->ScrollYTarget = 0.f;
                        kb                = true;
                    }
                    if (ctx->EndPressed)
                    {
                        ps->ScrollYTarget = max_y;
                        kb                = true;
                    }
                    if (kb)
                    {
                        ps->ScrollY = ps->ScrollYTarget; // instant snap for keyboard
                        changed     = true;
                    }
                }

                if (changed)
                    ps->ScrollbarShowTimer = 0.5f; // show scrollbar for 500ms after any scroll input
            }
        }

        // Close popups when pressing outside — pops from innermost outward.
        // Pressing inside popup N but outside popup N+1 closes only popup N+1.
        bool any_pressed = ctx->MousePressed[0] || ctx->MousePressed[1];
        if (any_pressed && ctx->PopupStackSize > 0)
        {
            // Find the deepest popup whose box contains the cursor
            int inside = -1;
            for (int pi = (int) ctx->PopupStackSize - 1; pi >= 0; pi--)
            {
                if (ctx->PopupStack[pi].Box && PointInBox(ctx->MousePos, ctx->PopupStack[pi].Box))
                {
                    inside = pi;
                    break;
                }
            }
            // Close all popups deeper than 'inside' (inside == -1 → close all)
            ctx->PopupStackSize = (inside < 0) ? 0u : (uint32_t) (inside + 1);
        }

        // Hot / active
        if (!ctx->MouseDown[0])
        {
            ctx->HotKey = new_hot;
        }

        if (ctx->MousePressed[0] && ctx->HotKey)
        {
            ctx->ActiveKey = ctx->HotKey;
        }
        // Clear keyboard focus when the user clicks empty space (no widget under cursor).
        // Mirrors VS Code/ImGui: section header border, text field cursor, etc. all clear.
        if (ctx->MousePressed[0] && !ctx->HotKey)
        {
            ctx->FocusKey = 0;
        }
        if (ctx->MouseReleased[0])
        {
            if (ctx->DragSourceKey != 0)
            {
                // ctx->HotKey is frozen while MouseDown[0] (see above), so it still
                // points at wherever the drag started — new_hot is this frame's
                // fresh hit-test, i.e. wherever the mouse was actually released.
                //
                // DragPayload/DragPayloadLen are left untouched: ZUIAcceptDrop reads
                // them next frame, and clearing DragPayloadLen here made every drop
                // report success with an empty payload. ZUIBeginDragSource overwrites
                // both on the next drag, so nothing goes stale by leaving them.
                ctx->DragDropFired = true;
                ctx->DragTargetKey = new_hot;
                ctx->DragSourceKey = 0;
            }
            ctx->ActiveKey = 0;
        }
    }

    ZUISignal ZUISignalFromBox(ZUIContext* ctx, ZUIBox* box)
    {
        // HOT PATH — runs every frame, no heap allocation allowed.
        ZUISignal signal  = {};

        bool      hovered = (ctx->HotKey == box->Key);
        bool      active  = (ctx->ActiveKey == box->Key);

        if (hovered)
        {
            signal.Flags |= ZUI_SignalHovered;
        }
        if (active && ctx->MouseDown[0])
        {
            signal.Flags        |= ZUI_SignalHeld;
            signal.DragDelta[0]  = ctx->MousePos[0] - ctx->PrevMousePos[0];
            signal.DragDelta[1]  = ctx->MousePos[1] - ctx->PrevMousePos[1];
        }
        if (ctx->MousePressed[0] && hovered)
        {
            signal.Flags |= ZUI_SignalPressed;
        }
        // DragSourceKey != 0 means a drag is in progress — release is a drop, not a click.
        if (ctx->MouseReleased[0] && active && hovered && ctx->DragSourceKey == 0)
        {
            signal.Flags |= ZUI_SignalClicked | ZUI_SignalReleased;
        }
        if (ctx->ScrollDelta != 0.f && hovered)
        {
            signal.Flags       |= ZUI_SignalScrolled;
            signal.ScrollDelta  = ctx->ScrollDelta;
        }

        // Tab focus order tracking — builds prev/next chain during the widget build pass
        if ((ctx->TabPressed || ctx->ShiftTabPressed) && (box->Flags & ZUI_Clickable))
        {
            uint64_t k = box->Key;
            if (ctx->TabNavFirstKey == 0)
                ctx->TabNavFirstKey = k;
            if (!ctx->TabNavSeenFocus)
                ctx->TabNavPrevKey = k;
            if (ctx->TabNavSeenFocus && !ctx->TabNavNextKey)
                ctx->TabNavNextKey = k;
            if (k == ctx->FocusKey)
                ctx->TabNavSeenFocus = true;
            ctx->TabNavLastKey = k;
        }

        // Animate hot/active
        ZUIPersistentState* state = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        if (state)
        {
            float dt          = ctx->DeltaTime > 0.f ? ctx->DeltaTime : (1.f / 60.f);
            float hot_target  = hovered ? 1.f : 0.f;
            float act_target  = active ? 1.f : 0.f;
            state->HotT      += (hot_target - state->HotT) * (1.f - expf(-ctx->Style.HoverAnimSpeed * dt));
            state->ActiveT   += (act_target - state->ActiveT) * (1.f - expf(-ctx->Style.ActiveAnimSpeed * dt));
        }

        return signal;
    }

} // namespace ZEngine::UI
