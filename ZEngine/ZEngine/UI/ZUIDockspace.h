#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstdint>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // ---------------------------------------------------------------
    // ZUIDockspace — arena-based panel split tree
    //
    // A ZUIDockNode is either:
    //   - A SPLIT node  (has children, no content key)
    //   - A LEAF node   (has a content key, assigned a rect each frame)
    //
    // Layout mirrors RAD's CFG_PanelNode: each child owns pct_of_parent
    // of its parent's rect along split_axis. The tree is built once and
    // mutated by Split/Merge/Resize operations; rects are computed every
    // frame from the root rect.
    //
    // Consumers:
    //   1. Build the node tree at startup via ZUIDockBuild
    //   2. Call ZUIDockLayout(tree, root_rect) each frame to recompute rects
    //   3. Query ZUIDockRectForKey(tree, key) to position each panel
    // ---------------------------------------------------------------

    struct ZUIDockNode
    {
        // tree links (arena-allocated)
        ZUIDockNode* Parent   = nullptr;
        ZUIDockNode* First    = nullptr; // first child
        ZUIDockNode* Last     = nullptr; // last child
        ZUIDockNode* Next     = nullptr; // next sibling
        ZUIDockNode* Prev     = nullptr; // prev sibling
        uint32_t     ChildCount = 0;

        // split params (split nodes only)
        ZUIAxis SplitAxis     = ZUIAxis::X;
        float   PctOfParent   = 1.f;     // [0,1] fraction of parent's rect

        // leaf params
        uint64_t ContentKey   = 0;       // hashed panel name; 0 = split node

        // computed each frame by ZUIDockLayout
        float RectMin[2]      = {};
        float RectMax[2]      = {};
    };

    struct ZUIDockTree
    {
        ZUIDockNode* Root    = nullptr;
        ZUIDockNode* Focused = nullptr; // which leaf has keyboard focus
        ArenaAllocator* Arena = nullptr;
    };

    // ---------------------------------------------------------------
    // Build API
    //
    // ZUIDockTreeCreate  — allocate a fresh tree in persistent_arena
    // ZUIDockLeaf        — add a leaf node with a content key
    // ZUIDockSplitH      — split node into left (pct) / right (1-pct)
    // ZUIDockSplitV      — split node into top (pct) / bottom (1-pct)
    // ---------------------------------------------------------------

    ZUIDockTree* ZUIDockTreeCreate(ArenaAllocator* persistent_arena);

    // Make `node` a horizontal split: left child gets `left_pct`, right gets rest.
    // Returns the left child (caller may further split it).
    ZUIDockNode* ZUIDockSplitH(ZUIDockTree* tree, ZUIDockNode* node,
                                float left_pct,
                                uint64_t left_key, uint64_t right_key);

    // Make `node` a vertical split: top child gets `top_pct`, bottom gets rest.
    ZUIDockNode* ZUIDockSplitV(ZUIDockTree* tree, ZUIDockNode* node,
                                float top_pct,
                                uint64_t top_key, uint64_t bot_key);

    // ---------------------------------------------------------------
    // Runtime API
    // ---------------------------------------------------------------

    // Recompute all node rects by walking the tree from root.
    // root_rect: {x0, y0, x1, y1}
    void ZUIDockLayout(ZUIDockTree* tree, const float root_rect[4]);

    // Return the rect for the leaf whose ContentKey == key.
    // out_rect: {x0, y0, x1, y1}. Returns false if key not found.
    bool ZUIDockRectForKey(ZUIDockTree* tree, uint64_t key, float out_rect[4]);

    // Resize the split between two siblings — moves the divider.
    // `node` must be a LEAF; `delta` is in pixels along the parent's split axis.
    void ZUIDockResize(ZUIDockTree* tree, ZUIDockNode* node, float delta_px);

    // Hash a panel name to a content key.
    uint64_t ZUIDockHashName(const char* name);

    // Find the leaf node whose ContentKey == key. Returns nullptr if not found.
    ZUIDockNode* ZUIDockFindLeaf(ZUIDockTree* tree, uint64_t key);

    // Remove a leaf from the tree. The sibling node absorbs the vacated space
    // (takes the parent's PctOfParent and replaces it in the grandparent).
    // Call when a panel is undocked or closed so remaining panels fill in.
    void ZUIDockCollapseLeaf(ZUIDockTree* tree, ZUIDockNode* leaf);

} // namespace ZEngine::UI
