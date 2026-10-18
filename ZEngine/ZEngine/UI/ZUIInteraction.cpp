#include <ZEngine/UI/ZUIInteraction.h>
#include <algorithm>
#include <cmath>

namespace ZEngine::UI
{
    static bool PointInBox(const float pt[2], const ZUIBox* box)
    {
        return pt[0] >= box->ScreenMin[0] && pt[0] <= box->ScreenMax[0] && pt[1] >= box->ScreenMin[1] && pt[1] <= box->ScreenMax[1];
    }

    static bool DescendantOf(const ZUIBox* box, const ZUIBox* ancestor)
    {
        for (; box; box = box->Parent)
            if (box == ancestor)
                return true;
        return false;
    }

    static bool PointInHitBox(const float pt[2], const ZUIBox* box)
    {
        if (box->ScreenMax[0] <= box->ScreenMin[0] || box->ScreenMax[1] <= box->ScreenMin[1])
            return false;
        for (int axis = 0; axis < 2; ++axis)
        {
            const float pad = fmaxf(0.f, (box->MinHitSize[axis] - (box->ScreenMax[axis] - box->ScreenMin[axis])) * 0.5f);
            if (pt[axis] < box->ScreenMin[axis] - pad || pt[axis] > box->ScreenMax[axis] + pad)
                return false;
        }
        return true;
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
        ZUIBox*  hot_box         = nullptr;
        bool     hot_exact       = false;
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

            const bool exact        = PointInBox(ctx->MousePos, box);
            bool       under_cursor = (box->Flags & ZUI_Clickable) ? PointInHitBox(ctx->MousePos, box) : exact;
            // Clipped-away tabs/widgets must not steal clicks or drops from
            // neighboring controls even though their layout rect still exists.
            for (const ZUIBox* parent = box->Parent; parent && under_cursor; parent = parent->Parent)
                if ((parent->Flags & ZUI_ClipChildren) && !PointInBox(ctx->MousePos, parent))
                    under_cursor = false;

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
                    if ((box->Flags & ZUI_Clickable) && !(box->Flags & ZUI_Disabled))
                    {
                        // Padding may win over its own container, but never steal
                        // a neighboring control's actual rectangle.
                        if (!hot_box || exact || !hot_exact || DescendantOf(box, hot_box))
                        {
                            new_hot   = box->Key;
                            hot_box   = box;
                            hot_exact = exact;
                        }
                    }
                    if ((box->Flags & ZUI_Scrollable) && exact)
                    {
                        new_scroll_key  = box->Key;
                        new_scroll_axis = box->LayoutAxis;
                    }
                }
            }

            // Match paint order: later-painted siblings (such as overlay
            // scrollbars) must win over clickable content beneath them.
            // The stack is LIFO, so push children in reverse sibling order.
            for (ZUIBox* child = box->LastChild; child; child = child->PrevSib)
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
        if (!ctx->MouseDown[0] || ctx->MousePressed[0])
        {
            ctx->HotKey = new_hot;
        }

        if (ctx->MousePressed[0] && ctx->HotKey)
        {
            ctx->ActiveKey = ctx->HotKey;
            if (hot_box && !(hot_box->Flags & ZUI_NoKeyboardFocus))
                ctx->FocusKey = hot_box->Key;
            else if (hot_box && !(hot_box->Flags & ZUI_Scrollable))
                ctx->FocusKey = 0;
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

        if (ctx->HotKey != ctx->TooltipHoverKey || ctx->MouseDown[0] || ctx->MousePressed[0] || ctx->ScrollDelta != 0.f)
        {
            ctx->TooltipHoverKey  = ctx->HotKey;
            ctx->TooltipHoverTime = 0.f;
        }
        else
            ctx->TooltipHoverTime = fminf(0.4f, ctx->TooltipHoverTime + fmaxf(ctx->DeltaTime, 0.f));
    }

    void ZUIFocusPass(ZUIContext* ctx)
    {
        if (!ctx->Root)
            return;
        if (!ctx->FocusKey && !ctx->TabPressed && !ctx->ShiftTabPressed && !ctx->PopupStackSize && !ctx->ModalBox && !ctx->FocusTrapKey)
            return;
        ZUIBox*        trap         = ctx->PopupStackSize ? ctx->PopupStack[ctx->PopupStackSize - 1].Box : ctx->ModalBox;
        const uint64_t trap_key     = trap ? trap->Key : 0;
        const bool     leaving_trap = ctx->FocusTrapKey && !trap_key;
        if (trap_key != ctx->FocusTrapKey)
        {
            // A combo may live inside a modal, and a submenu inside another
            // popup. Returning to that overlay restores its opener, not its
            // first control (the outer SavedFocusKey belongs to the window).
            if (trap_key && ctx->FocusTrapKey)
                for (uint32_t i = ctx->PopupStackSize; i < ZUIContext::kMaxPopupDepth; ++i)
                    if (ctx->PopupStack[i].Key == ctx->FocusTrapKey)
                    {
                        ctx->FocusKey = ctx->PopupStack[i].OwnerFocusKey;
                        break;
                    }
            if (!ctx->FocusTrapKey && trap_key)
                ctx->SavedFocusKey = ctx->FocusKey;
            else if (!trap_key)
            {
                ctx->FocusKey      = ctx->SavedFocusKey;
                ctx->SavedFocusKey = 0;
            }
            ctx->FocusTrapKey = trap_key;
        }

        struct Entry
        {
            ZUIBox* Box   = nullptr;
            ZUIBox* Scope = nullptr;
        };
        const auto scratch = ZGetScratch(&ctx->FrameArena);
        auto*      entries = ZPushArray(&ctx->FrameArena, Entry, ctx->MaxBoxesPerFrame);
        uint32_t   count   = 0;
        ZUIBox*    focused = nullptr;
        // Stackless pre-order walk: hierarchy order is a stable tie-breaker.
        for (auto* box = ctx->Root; box;)
        {
            bool eligible = box->Key && (box->Flags & ZUI_Clickable) && !(box->Flags & (ZUI_Disabled | ZUI_NoKeyboardFocus));
            eligible      = eligible && (!trap || DescendantOf(box, trap));
            float   lo[2] = {box->ScreenMin[0], box->ScreenMin[1]};
            float   hi[2] = {box->ScreenMax[0], box->ScreenMax[1]};
            ZUIBox* scope = nullptr;
            for (auto* p = box; p && eligible; p = p->Parent)
            {
                if (!scope && (p->Flags & ZUI_NavScope))
                    scope = p;
                if (p->Flags & ZUI_Disabled)
                    eligible = false;
                if (p->Flags & ZUI_ClipChildren)
                    for (int axis = 0; axis < 2; ++axis)
                    {
                        lo[axis] = fmaxf(lo[axis], p->ScreenMin[axis]);
                        hi[axis] = fminf(hi[axis], p->ScreenMax[axis]);
                    }
            }
            eligible = eligible && hi[0] > lo[0] && hi[1] > lo[1];
            if (eligible && count < ctx->MaxBoxesPerFrame)
            {
                entries[count++] = {box, scope};
                if (box->Key == ctx->FocusKey)
                    focused = box;
            }
            if (box->FirstChild)
                box = box->FirstChild;
            else
            {
                while (box && !box->NextSib)
                    box = box->Parent;
                if (box)
                    box = box->NextSib;
            }
        }

        // Esc dismisses menus first. Modals retain their focus trap until closed.
        if (ctx->EscapePressed && !trap && !leaving_trap)
            ctx->FocusKey = 0;
        const bool tab  = ctx->TabPressed || ctx->ShiftTabPressed;
        ZUIBox*    list = nullptr;
        for (auto* p = focused; p; p = p->Parent)
            if (p->Flags & ZUI_NavList)
            {
                list = p;
                break;
            }
        const bool arrows      = ((trap && ctx->PopupStackSize) || (list && !ctx->TextInputActive)) && (ctx->ArrowDownPressed || ctx->ArrowUpPressed);
        const bool needs_first = trap && !focused && !(ctx->EnterPressed && ctx->TextInputActive);
        if (count && (tab || arrows || needs_first))
        {
            // Group panels by dock-tree order, then form overlapping visual rows.
            // Sorting runs only on navigation, not on ordinary hover frames.
            std::sort(entries, entries + count, [](const Entry& a, const Entry& b) {
                const uint32_t ao = a.Scope ? a.Scope->NavOrder : 0;
                const uint32_t bo = b.Scope ? b.Scope->NavOrder : 0;
                if (ao != bo)
                    return ao < bo;
                const uint64_t ak = a.Scope ? a.Scope->Key : 0;
                const uint64_t bk = b.Scope ? b.Scope->Key : 0;
                if (ak != bk)
                    return ak < bk;
                if (a.Box->ScreenMin[1] != b.Box->ScreenMin[1])
                    return a.Box->ScreenMin[1] < b.Box->ScreenMin[1];
                if (a.Box->ScreenMin[0] != b.Box->ScreenMin[0])
                    return a.Box->ScreenMin[0] < b.Box->ScreenMin[0];
                return a.Box->Key < b.Box->Key;
            });
            for (uint32_t start = 0; start < count;)
            {
                uint32_t end    = start + 1;
                float    bottom = entries[start].Box->ScreenMax[1];
                while (end < count && entries[end].Scope == entries[start].Scope && entries[end].Box->ScreenMin[1] < bottom)
                {
                    bottom = fminf(bottom, entries[end].Box->ScreenMax[1]);
                    ++end;
                }
                std::sort(entries + start, entries + end, [](const Entry& a, const Entry& b) {
                    if (a.Box->ScreenMin[0] != b.Box->ScreenMin[0])
                        return a.Box->ScreenMin[0] < b.Box->ScreenMin[0];
                    return a.Box->Key < b.Box->Key;
                });
                start = end;
            }
            uint32_t nav_count = 0;
            for (uint32_t i = 0; i < count; ++i)
                if (!arrows || !list || DescendantOf(entries[i].Box, list))
                    entries[nav_count++] = entries[i];
            int index = -1;
            for (uint32_t i = 0; i < nav_count; ++i)
                if (entries[i].Box == focused)
                    index = (int) i;
            const bool reverse = ctx->ShiftTabPressed || (arrows && ctx->ArrowUpPressed);
            if (nav_count)
            {
                if (index < 0)
                {
                    index = reverse ? (int) nav_count - 1 : 0;
                    if (!trap)
                        for (uint32_t i = 0; i < nav_count; ++i)
                            if (entries[i].Scope && entries[i].Scope->Key == ctx->PreferredFocusScope)
                            {
                                index = (int) i;
                                if (!reverse)
                                    break;
                            }
                }
                else if (tab || arrows)
                    index = (index + (reverse ? (int) nav_count - 1 : 1)) % (int) nav_count;
                focused       = entries[index].Box;
                ctx->FocusKey = focused->Key;
                if (tab || arrows)
                    ctx->KeyboardFocus = true;
            }
        }
        else if (!focused)
            ctx->FocusKey = 0; // Removed, disabled or fully clipped control.

        if (focused && focused->Key == ctx->FocusKey && ctx->KeyboardFocus)
        {
            auto* target  = focused->FocusRingTarget ? focused->FocusRingTarget : focused;
            target->Flags = target->Flags | ZUI_KeyboardFocused;
        }
        if (focused && !trap)
            for (auto* p = focused; p; p = p->Parent)
                if (p->Flags & ZUI_NavScope)
                {
                    ctx->PreferredFocusScope = p->Key;
                    break;
                }
        ZReleaseScratch(scratch);
    }

    ZUISignal ZUISignalFromBox(ZUIContext* ctx, ZUIBox* box)
    {
        // HOT PATH — runs every frame, no heap allocation allowed.
        ZUISignal signal = {};
        signal.Key       = box->Key;

        if (ctx->Disabled || (box->Flags & ZUI_Disabled))
        {
            box->Flags = ZUIBoxFlags((box->Flags | ZUI_Disabled) & ~ZUI_Clickable);
            if (ctx->HotKey == box->Key)
                ctx->HotKey = 0;
            if (ctx->ActiveKey == box->Key)
                ctx->ActiveKey = 0;
            if (ctx->FocusKey == box->Key)
                ctx->FocusKey = 0;
            if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, box->Key))
                state->HotT = state->ActiveT = 0.f;
            return signal;
        }

        bool hovered = (ctx->HotKey == box->Key);
        bool active  = (ctx->ActiveKey == box->Key);

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

        if (ctx->KeyboardFocus && ctx->FocusKey == box->Key && !(box->Flags & ZUI_NoKeyboardFocus))
        {
            signal.Flags |= ZUI_SignalKeyboardFocus;
            if ((box->Flags & ZUI_KeyboardActivate) && (ctx->EnterPressed || ctx->SpacePressed))
                signal.Flags |= ZUI_SignalClicked;
        }

        // Animate hot/active
        ZUIPersistentState* state = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        if (state)
        {
            float dt          = ctx->DeltaTime > 0.f ? ctx->DeltaTime : (1.f / 60.f);
            float hot_target  = hovered ? 1.f : 0.f;
            float act_target  = active ? 1.f : 0.f;
            state->HotT      += (hot_target - state->HotT) * (ctx->Style.ReducedMotion ? 1.f : 1.f - expf(-ctx->Style.HoverAnimSpeed * dt));
            state->ActiveT   += (act_target - state->ActiveT) * (ctx->Style.ReducedMotion ? 1.f : 1.f - expf(-ctx->Style.ActiveAnimSpeed * dt));
        }

        return signal;
    }

} // namespace ZEngine::UI
