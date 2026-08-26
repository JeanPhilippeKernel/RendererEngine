#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstdint>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // ---------------------------------------------------------------
    // ZUIPanelView — base class for any view that can live in a panel.
    // Subclass and override BuildContent. All panels are arena-allocated.
    // ---------------------------------------------------------------
    struct ZUIPanelView
    {
        const char* Title   = "Panel";     // tab label
        uint64_t    Key     = 0;           // unique hash, set by manager
        bool        Visible = true;

        virtual ~ZUIPanelView() = default;

        // Called each frame when this view is the active tab.
        // rect: {x0, y0, x1, y1} in logical pixels (the content area).
        virtual void BuildContent(ZUIContext* ctx, float rect[4]) = 0;

        // Optional: called once after construction.
        virtual void Initialize(ZUIContext*) {}
    };

    // ---------------------------------------------------------------
    // ZUIPanel — one docked panel that contains 1..N tabbed views.
    // Corresponds to one LEAF in the ZUIDockTree.
    // ---------------------------------------------------------------
    static constexpr uint32_t kMaxTabsPerPanel = 16;

    struct ZUIPanel
    {
        uint64_t     DockKey    = 0;       // matches ZUIDockNode::ContentKey
        ZUIPanelView* Views[kMaxTabsPerPanel] = {};
        uint32_t     ViewCount  = 0;
        uint32_t     ActiveTab  = 0;       // which view is shown
    };

    // ---------------------------------------------------------------
    // Drag-to-dock state — one global floating state at a time.
    // ---------------------------------------------------------------
    enum class ZUIDropZone { None, Center, Left, Right, Top, Bottom };

    struct ZUIDragDockState
    {
        bool         Active     = false;
        ZUIPanel*    SrcPanel   = nullptr; // panel the tab came from
        uint32_t     SrcTabIdx  = 0;
        float        GhostX     = 0.f;
        float        GhostY     = 0.f;
        ZUIDockNode* HoverNode  = nullptr; // dock node the ghost is hovering
        ZUIDropZone  DropZone   = ZUIDropZone::None;
    };

    // ---------------------------------------------------------------
    // ZUIPanelManager — owns the dock tree + all panels.
    // Call BuildUI() once per frame from the editor layer.
    // ---------------------------------------------------------------
    static constexpr uint32_t kMaxPanels   = 32;
    static constexpr float    kTabBarH     = 26.f; // logical px, scaled internally
    static constexpr float    kMenuBarH    = 26.f;

    struct ZUIPanelManager
    {
        ZUIDockTree* DockTree        = nullptr;
        ZUIPanel     Panels[kMaxPanels];
        uint32_t     PanelCount      = 0;
        uint32_t     FocusedPanelIdx = 0;  // index into Panels[], set on tab click

        // Drag-to-dock transient state
        ZUIDragDockState Drag;

        // Lifecycle
        void Init(ArenaAllocator* arena);
        void Shutdown();

        // Panel registration (call before first BuildUI)
        ZUIPanel* AddPanel(uint64_t dock_key);
        void      AddView(ZUIPanel* panel, ZUIPanelView* view);

        // Per-frame rendering — builds the full editor UI.
        // Call this from ZUILayer::Render once the box tree is open.
        void BuildUI(ZUIContext* ctx, float menu_h, float status_h);

        // Helpers
        ZUIPanel* FindPanel(uint64_t dock_key);

    private:
        void BuildMenuBar  (ZUIContext* ctx, float sw, float mh);
        void BuildPanel    (ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildTabBar   (ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildDividers (ZUIContext* ctx, float sw, float sh,
                            float menu_h, float status_h);
        void CommitDrop    (ZUIPanel* src, uint32_t tab_idx,
                            ZUIDockNode* dst, ZUIDropZone zone);

        struct Divider {
            const char* leaf_name;
            bool horizontal;
            bool use_near;
            bool dragging = false;
        };
        Divider m_dividers[4] = {
            {"Hierarchy", false, false},
            {"Inspector", false, true},
            {"Viewport",  true,  false},
            {"Log",       false, false},
        };
    };

} // namespace ZEngine::UI
