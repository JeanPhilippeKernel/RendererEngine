#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIFont.h>

namespace ZEngine::UI
{
    void ZUILayoutSolve(ZUIContext* ctx)
    {
        if (!ctx->Root) { return; }

        uint32_t max = ctx->MaxBoxesPerFrame;

        // Collect nodes in pre-order using a DFS stack.
        // Both arrays live in FrameArena and are reclaimed by Clear() next frame.
        ZUIBox** nodes     = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        ZUIBox** dfs_stack = ZPushArray(&ctx->FrameArena, ZUIBox*, max);
        uint32_t node_count = 0;
        uint32_t stack_top  = 0;

        dfs_stack[stack_top++] = ctx->Root;
        while (stack_top > 0 && node_count < max)
        {
            ZUIBox* box      = dfs_stack[--stack_top];
            nodes[node_count++] = box;

            // push children right-to-left so leftmost child is processed first
            for (ZUIBox* c = box->LastChild; c; c = c->PrevSib)
            {
                if (stack_top < max) { dfs_stack[stack_top++] = c; }
            }
        }

        // ---------------------------------------------------------------
        // Pass 1 — post-order (reverse pre-order): intrinsic sizes
        // Resolves: Pixels, Text (stub=0), ChildrenSum
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
                        if (ctx->Font && box->Label.Ptr)
                        {
                            ZUIMeasureText(ctx->Font, box->Label.Ptr, box->Label.Len, text_size);
                        }
                        box->ComputedSize[axis] = text_size[axis];
                        break;
                    }

                    case ZUISizeKind::ChildrenSum:
                    {
                        float accum = 0.f;
                        if (axis == layout)
                        {
                            // layout axis: sum children along the stacking direction
                            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
                            {
                                accum += c->ComputedSize[axis];
                            }
                        }
                        else
                        {
                            // cross axis: max of children (they all overlap on this axis)
                            for (ZUIBox* c = box->FirstChild; c; c = c->NextSib)
                            {
                                if (c->ComputedSize[axis] > accum) { accum = c->ComputedSize[axis]; }
                            }
                        }
                        box->ComputedSize[axis] = accum;
                        break;
                    }

                    default:
                        // Fill and ParentPercent are deferred to Pass 2
                        break;
                }
            }
        }

        // ---------------------------------------------------------------
        // Pass 2 — pre-order: extrinsic sizes + screen positions
        // Resolves: ParentPercent, Fill; then sets ScreenMin / ScreenMax
        // ---------------------------------------------------------------
        for (uint32_t i = 0; i < node_count; ++i)
        {
            ZUIBox* box    = nodes[i];
            ZUIBox* parent = box->Parent;
            int     layout = parent ? (int)parent->LayoutAxis : 0;

            // --- Resolve extrinsic sizes ---
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

                        if (axis != layout)
                        {
                            // cross axis: fill the entire parent extent on this axis
                            box->ComputedSize[axis] = parent->ComputedSize[axis];
                        }
                        else
                        {
                            // layout axis: divide remaining space among all Fill siblings
                            float    non_fill_sum = 0.f;
                            uint32_t fill_count   = 0;
                            for (ZUIBox* sib = parent->FirstChild; sib; sib = sib->NextSib)
                            {
                                if (sib->Size[axis].Kind == ZUISizeKind::Fill)
                                {
                                    ++fill_count;
                                }
                                else
                                {
                                    non_fill_sum += sib->ComputedSize[axis];
                                }
                            }
                            float remaining        = parent->ComputedSize[axis] - non_fill_sum;
                            box->ComputedSize[axis] = (fill_count > 0) ? remaining / (float)fill_count : 0.f;
                        }
                        break;
                    }

                    default:
                        break;
                }
            }

            // --- Compute screen position ---
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
                        // flow position: immediately after previous sibling on the layout axis
                        box->ScreenMin[axis] = box->PrevSib ? box->PrevSib->ScreenMax[axis]
                                                            : parent->ScreenMin[axis];
                    }
                    else
                    {
                        // cross axis: align to parent origin
                        box->ScreenMin[axis] = parent->ScreenMin[axis];
                    }
                }
            }

            box->ScreenMax[0] = box->ScreenMin[0] + box->ComputedSize[0];
            box->ScreenMax[1] = box->ScreenMin[1] + box->ComputedSize[1];
        }
    }

} // namespace ZEngine::UI
