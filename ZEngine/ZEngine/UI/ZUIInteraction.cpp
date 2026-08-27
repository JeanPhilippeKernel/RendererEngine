#include <ZEngine/UI/ZUIInteraction.h>
#include <cmath>

namespace ZEngine::UI
{
    static bool PointInBox(const float pt[2], const ZUIBox* box)
    {
        return pt[0] >= box->ScreenMin[0] && pt[0] <= box->ScreenMax[0] &&
               pt[1] >= box->ScreenMin[1] && pt[1] <= box->ScreenMax[1];
    }

    void ZUIInteractionPass(ZUIContext* ctx)
    {
        if (!ctx->Root) { ctx->HotKey = 0; return; }

        uint32_t max       = ctx->MaxBoxesPerFrame;
        auto     scratch   = ZGetScratch(&ctx->FrameArena);
        ZUIBox** stack     = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        uint32_t stack_top = 0;

        uint64_t new_hot        = 0;
        uint64_t new_scroll_key = 0; // nearest scrollable box under cursor

        stack[stack_top++] = ctx->Root;
        while (stack_top > 0)
        {
            ZUIBox* box = stack[--stack_top];
            if (!box) { continue; }

            bool under_cursor = PointInBox(ctx->MousePos, box);

            if (under_cursor)
            {
                // When a popup is open, only boxes INSIDE the popup receive hover —
                // exactly as ImGui blocks input to windows below the modal/popup stack.
                bool can_hover = (ctx->ActivePopupKey == 0 || ctx->ActivePopupBox == nullptr);
                if (!can_hover)
                {
                    for (ZUIBox* p = box; p; p = p->Parent)
                    {
                        if (p == ctx->ActivePopupBox) { can_hover = true; break; }
                    }
                }

                if (can_hover)
                {
                    if (box->Flags & ZUI_Clickable)  { new_hot        = box->Key; }
                    if (box->Flags & ZUI_Scrollable) { new_scroll_key = box->Key; }
                }
            }

            for (ZUIBox* child = box->FirstChild; child; child = child->NextSib)
            {
                if (stack_top < max) { stack[stack_top++] = child; }
                // else: subtree silently skipped (acceptable degradation, not corruption)
            }
        }

        ZReleaseScratch(scratch);

        // Scroll: update persistent ScrollY for the nearest scrollable box under cursor
        if (ctx->ScrollDelta != 0.f && new_scroll_key != 0)
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, new_scroll_key);
            if (ps)
            {
                ps->ScrollY -= ctx->ScrollDelta * ctx->Style.MouseScrollSpeed;
                if (ps->ScrollY < 0.f) ps->ScrollY = 0.f;
                if (ps->MaxScrollY > 0.f && ps->ScrollY > ps->MaxScrollY)
                    ps->ScrollY = ps->MaxScrollY;
            }
        }

        // Close popup when any mouse button is pressed outside its bounds.
        // (Right-click to open via ZUIBeginPopupContextItem sets OpenPopupKey, which
        // only promotes to ActivePopupKey at end of frame — so no spurious close.)
        bool any_pressed = ctx->MousePressed[0] || ctx->MousePressed[1];
        if (any_pressed && ctx->ActivePopupKey != 0 && ctx->ActivePopupBox)
        {
            if (!PointInBox(ctx->MousePos, ctx->ActivePopupBox))
                ctx->ActivePopupKey = 0;
        }

        // Hot / active
        if (!ctx->MouseDown[0]) { ctx->HotKey = new_hot; }

        if (ctx->MousePressed[0] && ctx->HotKey)  { ctx->ActiveKey = ctx->HotKey; }
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
        ZUISignal signal = {};

        bool hovered = (ctx->HotKey    == box->Key);
        bool active  = (ctx->ActiveKey == box->Key);

        if (hovered)                                     { signal.Flags |= ZUI_SignalHovered; }
        if (active && ctx->MouseDown[0])
        {
            signal.Flags        |= ZUI_SignalHeld;
            signal.DragDelta[0]  = ctx->MousePos[0] - ctx->PrevMousePos[0];
            signal.DragDelta[1]  = ctx->MousePos[1] - ctx->PrevMousePos[1];
        }
        if (ctx->MousePressed[0]  && hovered)            { signal.Flags |= ZUI_SignalPressed; }
        if (ctx->MouseReleased[0] && active && hovered)  { signal.Flags |= ZUI_SignalClicked | ZUI_SignalReleased; }
        if (ctx->ScrollDelta != 0.f && hovered)
        {
            signal.Flags       |= ZUI_SignalScrolled;
            signal.ScrollDelta  = ctx->ScrollDelta;
        }

        // Tab focus order tracking — builds prev/next chain during the widget build pass
        if ((ctx->TabPressed || ctx->ShiftTabPressed) && (box->Flags & ZUI_Clickable))
        {
            uint64_t k = box->Key;
            if (ctx->TabNavFirstKey == 0)                    ctx->TabNavFirstKey = k;
            if (!ctx->TabNavSeenFocus)                       ctx->TabNavPrevKey  = k;
            if (ctx->TabNavSeenFocus && !ctx->TabNavNextKey) ctx->TabNavNextKey  = k;
            if (k == ctx->FocusKey)                          ctx->TabNavSeenFocus = true;
            ctx->TabNavLastKey = k;
        }

        // Animate hot/active
        ZUIPersistentState* state = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        if (state)
        {
            float dt         = ctx->DeltaTime > 0.f ? ctx->DeltaTime : (1.f / 60.f);
            float hot_target = hovered ? 1.f : 0.f;
            float act_target = active  ? 1.f : 0.f;
            state->HotT    += (hot_target - state->HotT)    * (1.f - expf(-ctx->Style.HoverAnimSpeed  * dt));
            state->ActiveT += (act_target - state->ActiveT) * (1.f - expf(-ctx->Style.ActiveAnimSpeed * dt));
        }

        return signal;
    }

} // namespace ZEngine::UI
