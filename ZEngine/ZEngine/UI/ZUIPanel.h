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
    // ---------------------------------------------------------------
    struct ZUIPanelView
    {
        const char* Title   = "Panel";
        uint64_t    Key     = 0;
        bool        Visible = true;

        virtual ~ZUIPanelView() = default;
        virtual void BuildContent(ZUIContext* ctx, float rect[4]) = 0;
        virtual void Initialize(ZUIContext*) {}
    };

    // ---------------------------------------------------------------
    // ZUIPanel — one panel (docked OR floating) containing 1..N views.
    // ---------------------------------------------------------------
    static constexpr uint32_t kMaxTabsPerPanel = 16;

    struct ZUIPanel
    {
        uint64_t      DockKey   = 0;
        ZUIPanelView* Views[kMaxTabsPerPanel] = {};
        uint32_t      ViewCount = 0;
        uint32_t      ActiveTab = 0;

        // --- Floating state (Level 1 in-app floating) ---
        bool     Floating      = false;
        float    FloatX        = 120.f;
        float    FloatY        = 120.f;
        float    FloatW        = 420.f;
        float    FloatH        = 320.f;
        uint32_t ZOrder        = 0;        // higher = rendered on top
        // Title-bar drag
        bool     DraggingTitle = false;
        float    DragOffX      = 0.f;
        float    DragOffY      = 0.f;
        // Resize drag
        bool     Resizing      = false;
        float    ResizeStartW  = 0.f;
        float    ResizeStartH  = 0.f;
    };

    // ---------------------------------------------------------------
    // Drag-to-dock state
    // ---------------------------------------------------------------
    enum class ZUIDropZone { None, Center, Left, Right, Top, Bottom };

    struct ZUIDragDockState
    {
        bool         Active     = false;
        ZUIPanel*    SrcPanel   = nullptr;
        uint32_t     SrcTabIdx  = 0;
        float        GhostX     = 0.f;
        float        GhostY     = 0.f;
        float        StartX     = 0.f;     // mouse pos when drag detected
        float        StartY     = 0.f;
        ZUIDockNode* HoverNode  = nullptr;
        ZUIDropZone  DropZone   = ZUIDropZone::None;
    };

    // ---------------------------------------------------------------
    // ZUIPanelManager
    // ---------------------------------------------------------------
    static constexpr uint32_t kMaxPanels   = 32;
    static constexpr float    kTabBarH     = 26.f;
    static constexpr float    kMenuBarH    = 26.f;
    static constexpr float    kTitleBarH   = 24.f; // floating panel title bar
    static constexpr float    kResizeGrip  =  8.f; // bottom-right resize handle
    static constexpr float    kDivGrabW    =  6.f; // divider hit-test width

    struct ZUIPanelManager
    {
        ZUIDockTree* DockTree          = nullptr;
        ZUIPanel     Panels[kMaxPanels];
        uint32_t     PanelCount        = 0;
        uint32_t     FocusedPanelIdx   = 0;
        uint32_t     FloatingZCounter  = 0;   // incremented each time a float is focused

        ZUIDragDockState Drag;

        void Init    (ArenaAllocator* arena);
        void Shutdown();

        ZUIPanel* AddPanel(uint64_t dock_key);
        void      AddView (ZUIPanel* panel, ZUIPanelView* view);

        void BuildUI(ZUIContext* ctx, float menu_h, float status_h);

        ZUIPanel* FindPanel(uint64_t dock_key);

    private:
        // ---- per-split-node divider state (auto-detected from tree) ----
        struct SplitDivider
        {
            ZUIDockNode* Node     = nullptr; // the split node (stable arena ptr)
            bool         Dragging = false;
        };
        static constexpr uint32_t kMaxSplitDividers = 32;
        SplitDivider  m_split_dividers[kMaxSplitDividers] = {};
        uint32_t      m_split_divider_count               = 0;

        // ---- helpers ----
        void BuildMenuBar      (ZUIContext* ctx, float sw, float mh);
        void BuildDockedPanel  (ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildFloatingPanel(ZUIContext* ctx, ZUIPanel* p);
        void BuildTabBar       (ZUIContext* ctx, ZUIPanel* p, float rect[4], bool in_float);
        void BuildDropZones    (ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildDividers     (ZUIContext* ctx);
        void BuildEmptySlot    (ZUIContext* ctx, float rect[4], uint64_t key);

        void CommitDrop (ZUIPanel* src, uint32_t tab_idx, ZUIDockNode* dst, ZUIDropZone zone);
        void PopOutPanel(ZUIPanel* p, float x, float y, float w, float h);
        void RedockPanel(ZUIPanel* p, ZUIDockNode* dst, ZUIDropZone zone);
        void FocusPanel (uint32_t idx);
        void SyncSplitDividers();
        bool GetSplitDividerDragging(ZUIDockNode* node) const;
        void SetSplitDividerDragging(ZUIDockNode* node, bool v);
    };

} // namespace ZEngine::UI
