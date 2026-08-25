#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIFont.h>

namespace ZEngine::UI
{
    // Padding helpers: [0]=left [1]=top [2]=right [3]=bottom
    static inline float PadStart(const ZUIBox* b, int axis) { return (axis == 0) ? b->Padding[0] : b->Padding[1]; }
    static inline float PadEnd  (const ZUIBox* b, int axis) { return (axis == 0) ? b->Padding[2] : b->Padding[3]; }

    void ZUILayoutSolve(ZUIContext* ctx)
    {
        if (!ctx->Root) { return; }

        uint32_t max = ctx->MaxBoxesPerFrame;

        ZUIBox** nodes     = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        ZUIBox** dfs_stack = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        uint32_t node_count = 0;
        uint32_t stack_top  = 0;

        dfs_stack[stack_top++] = ctx->Root;
        while (stack_top > 0 && node_count < max)
        {
            ZUIBox* box      = dfs_stack[--stack_top];
            nodes[node_count++] = box;
            for (ZUIBox* c = box->LastChild; c; c = c->PrevSib)
                if (stack_top < max) { dfs_stack[stack_top++] = c; }
        }

        // ---------------------------------------------------------------
        // Pass 1 — post-order: intrinsic sizes (Pixels, Text, ChildrenSum)
        // Padding is included in ChildrenSum totals.
        // ---------------------------------------------------------------
        for (uint32_t i = node_count; i > 0; --i)
        {
            ZUIBox* box    = nodes[i - 1];
            int     layout = (int)box->LayoutAxis;

            for (int axis = 0; axis < 2; ++axis)
            {
                ZUISize s = box->Size[axis];
                switch (s.Kind)
                {
                    case ZUISizeKind::Pixels:
                        box->ComputedSize[axis] = s.Value;
                        break;

                    case ZUISizeKind::Text:
                    {
                        float text_size[2] = {0.f, 0.f};
                        if (ctx->GetFont(box->FontSize) && box->Label.Ptr)
                            ZUIMeasureText(ctx->GetFont(box->FontSize), box->Label.Ptr, box->Label.Len, text_size);
                        box->ComputedSize[axis] = text_size[axis];
                        break;
                    }

                    case ZUISizeKind::ChildrenSum:
                    {
                        float ps = PadStart(box, axis);
                        float pe = PadEnd  (box, axis);
                        float accum = ps + pe;
                        if (axis == layout)
                        {
                            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
                                accum += c->ComputedSize[axis];
                        }
                        else
                        {
                            float mx = 0.f;
                            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
                                if (c->ComputedSize[axis] > mx) mx = c->ComputedSize[axis];
                            accum = mx + ps + pe;
                        }
                        box->ComputedSize[axis] = accum;
                        break;
                    }

                    default:
                        break; // Fill / ParentPercent deferred to Pass 2
                }
            }
        }

        // ---------------------------------------------------------------
        // Pass 2 — pre-order: extrinsic sizes + screen positions
        // Fill subtracts parent padding from available space.
        // Child placement starts at parent->ScreenMin + parent padding.
        // ---------------------------------------------------------------
        // Pass 2a: resolve extrinsic sizes only (no positions yet)
        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box    = nodes[i];
            ZUIBox* parent = box->Parent;
            int     layout = parent ? (int)parent->LayoutAxis : 0;

            for (int axis = 0; axis < 2; ++axis)
            {
                ZUISize s = box->Size[axis];
                switch (s.Kind)
                {
                    case ZUISizeKind::ParentPercent:
                        box->ComputedSize[axis] = parent ? parent->ComputedSize[axis] * s.Value : 0.f;
                        break;

                    case ZUISizeKind::Fill:
                    {
                        if (!parent) { box->ComputedSize[axis] = 0.f; break; }
                        float ps = PadStart(parent, axis);
                        float pe = PadEnd  (parent, axis);
                        if (axis != layout)
                        {
                            box->ComputedSize[axis] = parent->ComputedSize[axis] - ps - pe;
                        }
                        else
                        {
                            float    non_fill = 0.f;
                            uint32_t fill_n   = 0;
                            for (ZUIBox* sib = parent->FirstChild; sib; sib = sib->NextSib)
                            {
                                if (sib->Size[axis].Kind == ZUISizeKind::Fill) ++fill_n;
                                else non_fill += sib->ComputedSize[axis];
                            }
                            float remaining = parent->ComputedSize[axis] - ps - pe - non_fill;
                            box->ComputedSize[axis] = (fill_n > 0) ? remaining / (float)fill_n : 0.f;
                        }
                        break;
                    }

                    default:
                        break;
                }
            }
        }

        // ---------------------------------------------------------------
        // Pass 2.5 — enforce constraints: when children overflow a parent
        // along its layout axis, shrink flexible (low-strictness) children
        // proportionally to absorb the overflow. Prevents panel content from
        // bleeding outside its bounds. Strictness 1.0 = rigid, 0.0 = fully
        // flexible. ZFill defaults to Strictness=0, ZPx to Strictness=1.
        // ---------------------------------------------------------------
        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box = nodes[i];
            if (!box->FirstChild) { continue; }

            int axis = (int)box->LayoutAxis;
            float ps = PadStart(box, axis);
            float pe = PadEnd(box, axis);
            float available = box->ComputedSize[axis] - ps - pe;

            float total = 0.f;
            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
                total += c->ComputedSize[axis];

            float overflow = total - available;
            if (overflow <= 0.001f) { continue; }

            // Weighted flexibility pool: each child contributes (1-strictness) fraction
            float flex_pool = 0.f;
            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
            {
                float flex = 1.f - c->Size[axis].Strictness;
                if (flex > 0.f) { flex_pool += c->ComputedSize[axis] * flex; }
            }
            if (flex_pool <= 0.f) { continue; }

            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
            {
                float flex = 1.f - c->Size[axis].Strictness;
                if (flex <= 0.f) { continue; }
                float share   = c->ComputedSize[axis] * flex / flex_pool;
                c->ComputedSize[axis] -= overflow * share;
                if (c->ComputedSize[axis] < 0.f) { c->ComputedSize[axis] = 0.f; }
            }
        }

        // ---------------------------------------------------------------
        // Pass 3 — assign screen positions (top-down, after sizes finalized)
        // ---------------------------------------------------------------
        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box    = nodes[i];
            ZUIBox* parent = box->Parent;
            int     layout = parent ? (int)parent->LayoutAxis : 0;

            if (!parent)
            {
                box->ScreenMin[0] = 0.f;
                box->ScreenMin[1] = 0.f;
            }
            else
            {
                for (int axis = 0; axis < 2; ++axis)
                {
                    bool floating = (axis == 0) ? !!(box->Flags & ZUI_FloatX)
                                                : !!(box->Flags & ZUI_FloatY);
                    if (floating)
                    {
                        box->ScreenMin[axis] = parent->ScreenMin[axis] + box->FloatPos[axis];
                    }
                    else if (axis == layout)
                    {
                        float ps     = PadStart(parent, axis);
                        float scroll = 0.f;
                        if (!box->PrevSib && (parent->Flags & ZUI_Scrollable))
                        {
                            ZUIPersistentState* ps_state =
                                ZUIStateGetOrInsert(&ctx->StateStore, parent->Key);
                            if (ps_state)
                                scroll = (axis == 1) ? ps_state->ScrollY : ps_state->ScrollX;
                        }
                        box->ScreenMin[axis] = box->PrevSib ? box->PrevSib->ScreenMax[axis]
                                                            : parent->ScreenMin[axis] + ps - scroll;
                    }
                    else
                    {
                        float ps = PadStart(parent, axis);
                        box->ScreenMin[axis] = parent->ScreenMin[axis] + ps;
                    }
                }
            }

            box->ScreenMax[0] = box->ScreenMin[0] + box->ComputedSize[0];
            box->ScreenMax[1] = box->ScreenMin[1] + box->ComputedSize[1];
        }

        // ---------------------------------------------------------------
        // Pass 4 — compute MaxScrollY for every ZUI_Scrollable box so that
        // ZUIInteractionPass can clamp the scroll offset.
        // ---------------------------------------------------------------
        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box = nodes[i];
            if (!(box->Flags & ZUI_Scrollable)) { continue; }

            int layout = (int)box->LayoutAxis;
            float content = 0.f;
            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
                content += c->ComputedSize[layout];

            float visible = box->ComputedSize[layout];
            float max_scroll = content - visible;

            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
            if (ps) ps->MaxScrollY = max_scroll > 0.f ? max_scroll : 0.f;
        }
    }

} // namespace ZEngine::UI
