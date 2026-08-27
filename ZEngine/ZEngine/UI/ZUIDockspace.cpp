#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/UI/ZUIDockspace.h>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // Helpers

    static ZUIDockNode* AllocNode(ZUIDockTree* tree)
    {
        return ZPushStructCtor(tree->Arena, ZUIDockNode);
    }

    static void AppendChild(ZUIDockNode* parent, ZUIDockNode* child)
    {
        child->Parent = parent;
        child->Next   = nullptr;
        child->Prev   = parent->Last;
        if (parent->Last)
        {
            parent->Last->Next = child;
        }
        else
        {
            parent->First = child;
        }
        parent->Last = child;
        ++parent->ChildCount;
    }

    static constexpr float kMinPanelPx = 60.f; // minimum panel size in logical pixels

    // Recursive rect computation using absolute SizePx values.
    // On first call (SizePx==0), seeds from PctOfParent then switches to absolute storage.
    // Last child always snaps to the exact parent edge — no sub-pixel gaps from rounding.
    static void            LayoutNode(ZUIDockNode* node)
    {
        if (!node->First)
        {
            return;
        } // leaf — rect already set by parent

        float x0 = node->RectMin[0], y0 = node->RectMin[1];
        float x1 = node->RectMax[0], y1 = node->RectMax[1];
        float total    = (node->SplitAxis == ZUIAxis::X) ? (x1 - x0) : (y1 - y0);
        float cursor   = (node->SplitAxis == ZUIAxis::X) ? x0 : y0;

        // If SizePx uninitialized, seed from PctOfParent fractions.
        float size_sum = 0.f;
        for (ZUIDockNode* c = node->First; c; c = c->Next)
            size_sum += c->SizePx;
        if (size_sum < 1e-6f)
        {
            float pct_sum = 0.f;
            for (ZUIDockNode* c = node->First; c; c = c->Next)
                pct_sum += c->PctOfParent;
            if (pct_sum < 1e-6f)
                pct_sum = 1.f;
            for (ZUIDockNode* c = node->First; c; c = c->Next)
                c->SizePx = total * (c->PctOfParent / pct_sum);
            size_sum = total;
        }

        for (ZUIDockNode* c = node->First; c; c = c->Next)
        {
            // Last child takes exact remainder — eliminates sub-pixel gaps.
            float span = c->Next ? (total * c->SizePx / size_sum) : ((node->SplitAxis == ZUIAxis::X) ? (x1 - cursor) : (y1 - cursor));
            if (node->SplitAxis == ZUIAxis::X)
            {
                c->RectMin[0] = cursor;
                c->RectMin[1] = y0;
                c->RectMax[0] = cursor + span;
                c->RectMax[1] = y1;
            }
            else
            {
                c->RectMin[0] = x0;
                c->RectMin[1] = cursor;
                c->RectMax[0] = x1;
                c->RectMax[1] = cursor + span;
            }
            cursor += span;
            LayoutNode(c);
        }
    }

    // Build API

    ZUIDockTree* ZUIDockTreeCreate(ArenaAllocator* persistent_arena)
    {
        auto* tree              = ZPushStructCtor(persistent_arena, ZUIDockTree);
        tree->Arena             = persistent_arena;
        tree->Root              = AllocNode(tree);
        tree->Root->PctOfParent = 1.f;
        return tree;
    }

    ZUIDockNode* ZUIDockSplitH(ZUIDockTree* tree, ZUIDockNode* node, float left_pct, uint64_t left_key, uint64_t right_key)
    {
        float avail        = node->RectMax[0] - node->RectMin[0]; // >0 if already laid out

        node->SplitAxis    = ZUIAxis::X;
        node->ContentKey   = 0;

        auto* left         = AllocNode(tree);
        auto* right        = AllocNode(tree);
        left->PctOfParent  = left_pct;
        right->PctOfParent = 1.f - left_pct;
        left->ContentKey   = left_key;
        right->ContentKey  = right_key;
        // Seed SizePx from parent rect when available; LayoutNode seeds from PctOfParent otherwise.
        if (avail > 1.f)
        {
            left->SizePx  = avail * left_pct;
            right->SizePx = avail * (1.f - left_pct);
        }

        AppendChild(node, left);
        AppendChild(node, right);
        return left;
    }

    ZUIDockNode* ZUIDockSplitV(ZUIDockTree* tree, ZUIDockNode* node, float top_pct, uint64_t top_key, uint64_t bot_key)
    {
        float avail      = node->RectMax[1] - node->RectMin[1];

        node->SplitAxis  = ZUIAxis::Y;
        node->ContentKey = 0;

        auto* top        = AllocNode(tree);
        auto* bot        = AllocNode(tree);
        top->PctOfParent = top_pct;
        bot->PctOfParent = 1.f - top_pct;
        top->ContentKey  = top_key;
        bot->ContentKey  = bot_key;
        if (avail > 1.f)
        {
            top->SizePx = avail * top_pct;
            bot->SizePx = avail * (1.f - top_pct);
        }

        AppendChild(node, top);
        AppendChild(node, bot);
        return top;
    }

    // Runtime API

    void ZUIDockLayout(ZUIDockTree* tree, const float root_rect[4])
    {
        if (!tree || !tree->Root)
        {
            return;
        }
        tree->Root->RectMin[0] = root_rect[0];
        tree->Root->RectMin[1] = root_rect[1];
        tree->Root->RectMax[0] = root_rect[2];
        tree->Root->RectMax[1] = root_rect[3];
        LayoutNode(tree->Root);
    }

    static ZUIDockNode* FindLeaf(ZUIDockNode* node, uint64_t key)
    {
        if (node->ContentKey == key && !node->First)
        {
            return node;
        }
        for (ZUIDockNode* c = node->First; c; c = c->Next)
        {
            auto* found = FindLeaf(c, key);
            if (found)
            {
                return found;
            }
        }
        return nullptr;
    }

    bool ZUIDockRectForKey(ZUIDockTree* tree, uint64_t key, float out_rect[4])
    {
        if (!tree || !tree->Root)
        {
            return false;
        }
        auto* leaf = FindLeaf(tree->Root, key);
        if (!leaf)
        {
            return false;
        }
        out_rect[0] = leaf->RectMin[0];
        out_rect[1] = leaf->RectMin[1];
        out_rect[2] = leaf->RectMax[0];
        out_rect[3] = leaf->RectMax[1];
        return true;
    }

    void ZUIDockResize(ZUIDockTree* tree, ZUIDockNode* node, float delta_px)
    {
        if (!tree || !node || !node->Parent)
        {
            return;
        }
        auto* sibling = node->Next ? node->Next : node->Prev;
        if (!sibling)
        {
            return;
        }

        // Direct pixel delta — no percentage conversion, no drift.
        if (node->Next == sibling)
        {
            node->SizePx    += delta_px;
            sibling->SizePx -= delta_px;
        }
        else
        {
            node->SizePx    -= delta_px;
            sibling->SizePx += delta_px;
        }

        // Hard minimum size floor — clamp and compensate on the other side.
        auto clamp = [](ZUIDockNode* a, ZUIDockNode* b) {
            if (a->SizePx < kMinPanelPx)
            {
                b->SizePx -= kMinPanelPx - a->SizePx;
                a->SizePx  = kMinPanelPx;
                if (b->SizePx < kMinPanelPx)
                    b->SizePx = kMinPanelPx;
            }
        };
        clamp(node, sibling);
        clamp(sibling, node);
    }

    ZUIDockNode* ZUIDockFindLeaf(ZUIDockTree* tree, uint64_t key)
    {
        if (!tree || !tree->Root)
        {
            return nullptr;
        }
        return FindLeaf(tree->Root, key);
    }

    void ZUIDockCollapseLeaf(ZUIDockTree* tree, ZUIDockNode* leaf)
    {
        if (!tree || !leaf)
        {
            return;
        }
        ZUIDockNode* parent = leaf->Parent;
        if (!parent)
        {
            return;
        } // root leaf — nothing to collapse into

        // Find the sibling (the other child of the binary split parent)
        ZUIDockNode* sibling = (parent->First == leaf) ? leaf->Next : leaf->Prev;
        if (!sibling)
        {
            return;
        }

        ZUIDockNode* gp      = parent->Parent;
        // Sibling inherits parent's share — both storage values for full compatibility.
        sibling->PctOfParent = parent->PctOfParent;
        sibling->SizePx      = parent->SizePx;
        sibling->Parent      = gp;
        // Patch grandparent's sibling links (sibling replaces parent in the list)
        sibling->Prev        = parent->Prev;
        sibling->Next        = parent->Next;
        if (parent->Prev)
            parent->Prev->Next = sibling;
        if (parent->Next)
            parent->Next->Prev = sibling;

        if (gp)
        {
            if (gp->First == parent)
                gp->First = sibling;
            if (gp->Last == parent)
                gp->Last = sibling;
        }
        else
        {
            // parent was the root — sibling becomes the new root
            tree->Root = sibling;
        }

        // Orphan the removed nodes (arena-allocated, cannot free)
        leaf->Parent   = nullptr;
        parent->First  = nullptr;
        parent->Last   = nullptr;
        parent->Parent = nullptr;
    }

    uint64_t ZUIDockHashName(const char* name)
    {
        uint64_t h = 14695981039346656037ULL;
        for (const char* p = name; *p; ++p)
            h = (h ^ (uint8_t) *p) * 1099511628211ULL;
        return h ? h : 1;
    }

    void ZUIDockMarkCentral(ZUIDockTree* tree, uint64_t content_key)
    {
        ZUIDockNode* leaf = ZUIDockFindLeaf(tree, content_key);
        if (leaf)
        {
            leaf->IsCentral = true;
        }
    }

} // namespace ZEngine::UI
