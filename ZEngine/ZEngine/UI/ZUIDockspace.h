#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/UI/ZUIBox.h>
#include <cstdint>

namespace ZEngine::UI
{
    /// @brief Arena-based binary split tree for panel docking.
    ///
    /// Each node is either a SPLIT node (has children, ContentKey==0) or a
    /// LEAF node (no children, ContentKey != 0, assigned a rect each frame).
    /// The tree is built once at startup and mutated by Split/Resize/Collapse
    /// operations; rects are recomputed every frame from the root rect.
    ///
    /// Typical usage:
    /// @code
    ///   ZUIDockSplitH(tree, root, 0.25f, kLeftKey, kRightKey);
    ///   ZUIDockLayout(tree, root_rect);
    ///   ZUIDockRectForKey(tree, kLeftKey, out_rect);
    /// @endcode
    struct ZUIDockNode
    {
        ZUIDockNode* Parent         = nullptr;
        ZUIDockNode* First          = nullptr; ///< First child
        ZUIDockNode* Last           = nullptr; ///< Last child
        ZUIDockNode* Next           = nullptr; ///< Next sibling
        ZUIDockNode* Prev           = nullptr; ///< Previous sibling
        uint32_t     ChildCount     = 0;

        ZUIAxis      SplitAxis      = ZUIAxis::X;
        float        PctOfParent    = 1.f; ///< Serialization seed — ZUIDockLoad reads it, ZUIDockSave derives from SizePx/sum
        float        SizePx         = 0.f; ///< Runtime absolute px along the parent split axis (0 = uninitialized, seeded from PctOfParent on first layout)

        uint64_t     ContentKey     = 0; ///< Hashed panel name; 0 on split nodes

        float        RectMin[2]     = {}; ///< Computed each frame by ZUIDockLayout
        float        RectMax[2]     = {};

        bool         IsCentral      = false; ///< Passthrough — no chrome, no click interception (e.g. 3D viewport)
        bool         AutoHideTabBar = false; ///< When true and ViewCount==1 shows a title strip instead of a tab bar
    };

    struct ZUIDockTree
    {
        ZUIDockNode*                           Root    = nullptr;
        ZUIDockNode*                           Focused = nullptr; ///< Leaf with keyboard focus
        ZEngine::Core::Memory::ArenaAllocator* Arena   = nullptr;
    };

    /// @brief Allocate a fresh dock tree in @p persistent_arena.
    /// @param persistent_arena Must outlive the tree (arena-allocated, no heap).
    /// @return Pointer to the newly created ZUIDockTree.
    ZUIDockTree* ZUIDockTreeCreate(ZEngine::Core::Memory::ArenaAllocator* persistent_arena);

    /// @brief Split @p node into a left and right child along the X axis.
    /// @param tree       Owning tree (used for node allocation).
    /// @param node       Node to split — may be a leaf or an existing split.
    /// @param left_pct   Fraction [0,1] of the parent rect given to the left child.
    /// @param left_key   ContentKey for the left leaf (0 = intermediate split node).
    /// @param right_key  ContentKey for the right leaf.
    /// @return The left child node (caller may further split it).
    ZUIDockNode* ZUIDockSplitH(ZUIDockTree* tree, ZUIDockNode* node, float left_pct, uint64_t left_key, uint64_t right_key);

    /// @brief Split @p node into a top and bottom child along the Y axis.
    /// @param tree      Owning tree.
    /// @param node      Node to split.
    /// @param top_pct   Fraction [0,1] given to the top child.
    /// @param top_key   ContentKey for the top leaf.
    /// @param bot_key   ContentKey for the bottom leaf.
    /// @return The top child node.
    ZUIDockNode* ZUIDockSplitV(ZUIDockTree* tree, ZUIDockNode* node, float top_pct, uint64_t top_key, uint64_t bot_key);

    /// @brief Recompute all node rects by walking the tree from the root.
    /// @param tree      Target tree.
    /// @param root_rect Bounding rect {x0, y0, x1, y1} for the root node.
    void         ZUIDockLayout(ZUIDockTree* tree, const float root_rect[4]);

    /// @brief Get the screen rect for the leaf whose ContentKey equals @p key.
    /// @param tree      Target tree.
    /// @param key       ContentKey to search for.
    /// @param out_rect  Receives {x0, y0, x1, y1} if found.
    /// @return true if the leaf was found and @p out_rect was written.
    bool         ZUIDockRectForKey(ZUIDockTree* tree, uint64_t key, float out_rect[4]);

    /// @brief Move the divider between two siblings by @p delta_px pixels.
    /// @param tree     Target tree.
    /// @param node     A LEAF node whose sibling will absorb the delta.
    /// @param delta_px Positive moves the divider toward the next sibling.
    /// @note Enforces a minimum panel size (kMinPanelPx) on both sides.
    void         ZUIDockResize(ZUIDockTree* tree, ZUIDockNode* node, float delta_px);

    /// @brief Hash a panel name string to a stable 64-bit ContentKey.
    /// @param name Null-terminated panel name (e.g. "Hierarchy").
    /// @return Non-zero FNV-1a hash — guaranteed non-zero.
    uint64_t     ZUIDockHashName(const char* name);

    /// @brief Find the leaf node whose ContentKey equals @p key.
    /// @param tree Target tree.
    /// @param key  ContentKey to search for.
    /// @return Pointer to the leaf, or nullptr if not found.
    ZUIDockNode* ZUIDockFindLeaf(ZUIDockTree* tree, uint64_t key);

    /// @brief Remove a leaf from the tree; the sibling absorbs the freed space.
    /// @param tree Owning tree.
    /// @param leaf Leaf node to remove. Must have a parent (root leaf is a no-op).
    /// @note Nodes are arena-allocated and cannot be freed — orphaned nodes are
    ///       simply disconnected. Call before re-inserting a view elsewhere.
    void         ZUIDockCollapseLeaf(ZUIDockTree* tree, ZUIDockNode* leaf);

    /// @brief Mark a leaf as the central passthrough node.
    ///
    /// A central node receives no chrome (no tab bar, no border, no focus strip)
    /// and does not intercept clicks, making it suitable for a 3D viewport.
    /// @param tree        Owning tree.
    /// @param content_key ContentKey of the leaf to mark.
    void         ZUIDockMarkCentral(ZUIDockTree* tree, uint64_t content_key);

} // namespace ZEngine::UI
