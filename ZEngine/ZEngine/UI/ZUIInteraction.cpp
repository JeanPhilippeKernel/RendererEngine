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
        if (!ctx->Root)
        {
            ctx->HotKey = 0;
            return;
        }

        // Iterative depth-first traversal using a scratch-allocated stack.
        // Later matches overwrite earlier ones so the deepest / last-sibling
        // clickable box under the cursor becomes the hot candidate.
        uint32_t max       = ctx->MaxBoxesPerFrame;
        auto     scratch   = ZGetScratch(&ctx->FrameArena);
        ZUIBox** stack     = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        uint32_t stack_top = 0;

        uint64_t new_hot = 0;
        stack[stack_top++] = ctx->Root;

        while (stack_top > 0)
        {
            ZUIBox* box = stack[--stack_top];
            if (!box) { continue; }

            if ((box->Flags & ZUI_Clickable) && PointInBox(ctx->MousePos, box))
            {
                new_hot = box->Key;
            }

            // push children left-to-right so right-most (frontmost) is visited last
            for (ZUIBox* child = box->FirstChild; child; child = child->NextSib)
            {
                if (stack_top < max)
                {
                    stack[stack_top++] = child;
                }
            }
        }

        ZReleaseScratch(scratch);

        // only update hot when no button is held — keeps active target stable during drag
        if (!ctx->MouseDown[0])
        {
            ctx->HotKey = new_hot;
        }

        if (ctx->MousePressed[0] && ctx->HotKey)
        {
            ctx->ActiveKey = ctx->HotKey;
        }
        if (ctx->MouseReleased[0])
        {
            ctx->ActiveKey = 0;
        }
    }

    ZUISignal ZUISignalFromBox(ZUIContext* ctx, ZUIBox* box)
    {
        ZUISignal signal = {};

        bool hovered = (ctx->HotKey    == box->Key);
        bool active  = (ctx->ActiveKey == box->Key);

        if (hovered)                                              { signal.Flags |= ZUI_SignalHovered; }
        if (active && ctx->MouseDown[0])
        {
            signal.Flags        |= ZUI_SignalHeld;
            signal.DragDelta[0]  = ctx->MousePos[0] - ctx->PrevMousePos[0];
            signal.DragDelta[1]  = ctx->MousePos[1] - ctx->PrevMousePos[1];
        }
        if (ctx->MousePressed[0]  && hovered)                    { signal.Flags |= ZUI_SignalPressed; }
        if (ctx->MouseReleased[0] && active && hovered)          { signal.Flags |= ZUI_SignalClicked | ZUI_SignalReleased; }
        if (ctx->ScrollDelta != 0.f && hovered)
        {
            signal.Flags        |= ZUI_SignalScrolled;
            signal.ScrollDelta   = ctx->ScrollDelta;
        }

        // advance animation on persistent state
        ZUIPersistentState* state = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        if (state)
        {
            float dt            = ctx->DeltaTime;
            float hot_target    = hovered ? 1.f : 0.f;
            float active_target = active  ? 1.f : 0.f;
            float hot_rate      = 20.f;
            float active_rate   = 30.f;
            state->HotT    += (hot_target    - state->HotT)    * (1.f - expf(-hot_rate    * dt));
            state->ActiveT += (active_target - state->ActiveT) * (1.f - expf(-active_rate * dt));
        }

        return signal;
    }

} // namespace ZEngine::UI
