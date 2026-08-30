#include <ZEngine/UI/ZUIInteraction.h>
#include <cmath>

namespace ZEngine::UI
{
    static bool PointInBox(const float pt[2], const ZUIBox* box)
    {
        return pt[0] >= box->ScreenMin[0] && pt[0] <= box->ScreenMax[0] && pt[1] >= box->ScreenMin[1] && pt[1] <= box->ScreenMax[1];
    }

    void ZUIInteractionPass(ZUIContext* ctx)
    {
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
                // When a modal is active, only boxes INSIDE the modal panel receive hover.
                // When a popup is open, only boxes INSIDE a popup receive hover.
                // Both checks mirror ImGui's modal/popup input-blocking behaviour.
                bool can_hover = true;

                if (ctx->ModalBox)
                {
                    can_hover = false;
                    for (ZUIBox* p = box; p && !can_hover; p = p->Parent)
                        if (p == ctx->ModalBox) can_hover = true;
                }

                if (can_hover && ctx->PopupStackSize > 0)
                {
                    can_hover = false;
                    for (ZUIBox* p = box; p && !can_hover; p = p->Parent)
                        for (uint32_t pi = 0; pi < ctx->PopupStackSize && !can_hover; pi++)
                            if (p == ctx->PopupStack[pi].Box)
                                can_hover = true;
                }

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
                ctx->DragDropFired  = true;
                ctx->DragTargetKey  = ctx->HotKey;
                ctx->DragSourceKey  = 0;
                ctx->DragPayloadLen = 0;
            }
            ctx->ActiveKey = 0;
        }
    }

    ZUISignal ZUISignalFromBox(ZUIContext* ctx, ZUIBox* box)
    {
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
