#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <cstdint>

namespace ZEngine::UI
{
    /// @brief Base class for any view that can live in a panel tab.
    ///
    /// Derive from ZUIPanelView and implement BuildContent() to provide the
    /// panel's rendered content. Register the view with ZUIPanelManager::AddView().
    struct ZUIPanelView
    {
        const char* Title                                         = "Panel"; ///< Displayed in the tab bar
        uint64_t    Key                                           = 0;       ///< Optional stable key; 0 = derive from Title
        bool        Visible                                       = true;
        bool        Closeable                                     = true;  ///< false = no close button on this tab (e.g. main viewport)
        float       TabColor[4]                                   = {0.f, 0.f, 0.f, 0.f}; ///< Per-tab accent (alpha=0 → theme default)

        virtual ~ZUIPanelView()                                   = default;

        /// @brief Called every frame to build the view's widget tree.
        /// @param ctx  Active ZUI context.
        /// @param rect Screen rect {x0, y0, x1, y1} allocated to this view.
        virtual void BuildContent(ZUIContext* ctx, float rect[4]) = 0;

        /// @brief Called once after the ZUI font atlas is baked. Optional.
        virtual void Initialize(ZUIContext*) {}
    };

    static constexpr uint32_t kMaxTabsPerPanel = 16;

    /// @brief One docked panel holding 1..N views as tabs.
    struct ZUIPanel
    {
        uint64_t      DockKey                 = 0;
        ZUIPanelView* Views[kMaxTabsPerPanel] = {};
        uint32_t      ViewCount               = 0;
        uint32_t      ActiveTab               = 0;
        bool          Hidden                  = false; ///< Closed by user; restorable from Window menu

        bool          ReorderActive           = false;
        uint32_t      ReorderTabIdx           = 0;
        float         ReorderAccumX           = 0.f;
    };

    /// SrcTabIdx == kWholePanel means the drag moves the entire panel, not a single tab.
    static constexpr uint32_t kWholePanel = 0xFFFFFFFFu;

    /// @brief Target zone when a dragged panel is released over another panel.
    enum class ZUIDropZone
    {
        None   = 0, ///< No drop target.
        Center = 1, ///< Drop onto the target panel (merge into its tab bar).
        Left   = 2, ///< Split: insert as new panel to the left.
        Right  = 3, ///< Split: insert as new panel to the right.
        Top    = 4, ///< Split: insert as new panel above.
        Bottom = 5  ///< Split: insert as new panel below.
    };

    /// @brief Transient drag-to-dock state — valid only while a drag is active.
    struct ZUIDragDockState
    {
        bool         Active           = false;
        ZUIPanel*    SrcPanel         = nullptr;
        uint32_t     SrcTabIdx        = 0; ///< Tab index or kWholePanel
        float        GhostX           = 0.f;
        float        GhostY           = 0.f;
        float        StartX           = 0.f;
        float        StartY           = 0.f;
        ZUIDockNode* HoverNode        = nullptr;
        ZUIDropZone  DropZone         = ZUIDropZone::None;
        uint32_t     DropTabInsertIdx = 0xFFFFFFFFu; ///< kWholePanel = append at end
    };

    static constexpr uint32_t kMaxPanels = 32;

    /// @brief Manages the panel layout: docking tree, tab bars, drag-to-dock,
    ///        divider resize, and ini persistence.
    ///
    /// Typical setup:
    /// @code
    ///   ZUIPanelManager mgr;
    ///   mgr.Init(&arena);
    ///   ZUIDockSplitH(mgr.DockTree, mgr.DockTree->Root, 0.25f, kHierKey, kViewportKey);
    ///   auto* p = mgr.AddPanel(kHierKey);
    ///   mgr.AddView(p, &hierarchyView);
    ///   mgr.SetLayoutPath("layout.ini");
    ///   // each frame:
    ///   mgr.BuildUI(ctx, menu_h, status_h);
    /// @endcode
    struct ZUIPanelManager
    {
        ZUIDockTree*     DockTree = nullptr;
        ZUIPanel         Panels[kMaxPanels] = {};
        uint32_t         PanelCount                   = 0;
        uint32_t         FocusedPanelIdx              = 0;

        char             LayoutPath[256]              = {}; ///< Set before first BuildUI; empty = no persistence
        bool             LayoutDirty                  = false;
        bool             DrawMenuBar                  = true;  ///< false = external shell owns the menu bar
        bool             DrawBuiltinStatusBar         = true;  ///< false = external component owns the status bar

        uint64_t         PendingCloseKeys[kMaxPanels] = {};
        uint32_t         PendingCloseCount            = 0;

        ZUIDragDockState Drag = {};
        uint64_t         DragKeySeq = 0xD0C400000000ULL; ///< Monotone counter for drag-split panel keys

        /// @brief Allocate the dock tree and reset panel state.
        /// @param arena Persistent arena; must outlive the manager.
        void             Init(ZEngine::Core::Memory::ArenaAllocator* arena);
        void             Shutdown();

        /// @brief Register a new panel slot keyed by @p dock_key.
        /// @param dock_key  ZUIDockHashName("PanelName") or a drag-generated key.
        /// @returns Pointer to the new panel, or nullptr if capacity is reached.
        ZUIPanel*        AddPanel(uint64_t dock_key);

        /// @brief Append @p view to @p panel's tab list.
        /// @param panel Target panel to add the view to.
        /// @param view  View to register.
        /// @note No-op when panel is null or already at kMaxTabsPerPanel.
        void             AddView(ZUIPanel* panel, ZUIPanelView* view);

        /// @brief Build the entire panel UI for this frame.
        /// @param ctx      Active ZUI context.
        /// @param menu_h   Height of the menu bar in logical pixels.
        /// @param status_h Height of the status bar in logical pixels.
        void             BuildUI(ZUIContext* ctx, float menu_h, float status_h);

        /// @brief Find the panel registered with @p dock_key.
        /// @param dock_key Dock node key to search for.
        /// @return Pointer to the panel, or nullptr if not found.
        ZUIPanel*        FindPanel(uint64_t dock_key);

        /// @brief Set the filesystem path used for ini layout persistence.
        /// @param path Relative or absolute path; empty string disables saving.
        void             SetLayoutPath(const char* path);

        // Shell API — used by ZUIDockspaceComponent's Layout settings page.

        /// @brief Clear the current dock layout and reset to default.
        void             ResetLayout();

        /// @brief Show or hide a panel by its dock key.
        /// @param key     Dock node key.
        /// @param visible True to show, false to hide.
        void             SetPanelVisible(const char* name, bool visible);

        /// @brief Return whether the panel for @p key is currently visible.
        /// @param key Dock node key.
        /// @returns True if the panel is visible.
        bool             IsPanelVisible(const char* name) const;

    private:
        struct SplitDivider
        {
            ZUIDockNode* Node     = nullptr;
            bool         Dragging = false;
        };
        static constexpr uint32_t kMaxSplitDividers                   = 32;
        SplitDivider              m_split_dividers[kMaxSplitDividers] = {};
        uint32_t                  m_split_divider_count               = 0;

        void                      PreDetectCloseEvents(ZUIContext* ctx);
        void                      BuildMenuBar(ZUIContext* ctx, float sw, float mh);
        void                      BuildDockedPanel(ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void                      BuildTabBar(ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void                      BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void                      BuildDividerHitZones(ZUIContext* ctx); ///< Input pass — first; owns ActiveKey for resize
        void                      BuildDividerVisuals(ZUIContext* ctx);  ///< Render pass — last; always on top of panels
        void                      CommitDrop(ZUIPanel* src, uint32_t tab_idx, ZUIDockNode* dst, ZUIDropZone zone);
        void                      FocusPanel(uint32_t idx);
        void                      SyncSplitDividers();
        bool                      GetSplitDividerDragging(ZUIDockNode* node) const;
        void                      SetSplitDividerDragging(ZUIDockNode* node, bool v);
    };

} // namespace ZEngine::UI
