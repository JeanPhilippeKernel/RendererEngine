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
        const char* Title      = "Panel";
        uint64_t    Key        = 0;
        bool        Visible    = true;
        // Optional per-tab accent color (alpha=0 means use theme default).
        // Shown as a subtle tint on the active tab and a colored left-border strip.
        float       TabColor[4] = {0.f, 0.f, 0.f, 0.f};

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
        bool          Hidden    = false; // closed by user; can be restored from Window menu

        // Tab reorder drag (horizontal drag within the same tab bar)
        bool     ReorderActive = false;
        uint32_t ReorderTabIdx = 0;
        float    ReorderAccumX = 0.f;
    };

    // Sentinel: SrcTabIdx == kWholePanel means drag moves the entire panel
    static constexpr uint32_t kWholePanel = 0xFFFFFFFFu;

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
    static constexpr float    kDivGrabW    =  6.f; // divider hit-test width

    struct ZUIPanelManager
    {
        ZUIDockTree* DockTree          = nullptr;
        ZUIPanel     Panels[kMaxPanels];
        uint32_t     PanelCount      = 0;
        uint32_t     FocusedPanelIdx = 0;

        // Central node: the viewport panel key — rendered without chrome
        uint64_t     CentralPanelKey = 0;

        // Ini persistence
        char         LayoutPath[256] = {};  // set before first BuildUI; empty = no persistence
        bool         LayoutDirty     = false;

        ZUIDragDockState Drag;

        void Init    (ArenaAllocator* arena);
        void Shutdown();

        ZUIPanel* AddPanel(uint64_t dock_key);
        void      AddView (ZUIPanel* panel, ZUIPanelView* view);

        void BuildUI(ZUIContext* ctx, float menu_h, float status_h);

        ZUIPanel* FindPanel(uint64_t dock_key);

        void SetCentralPanel(uint64_t dock_key);
        void SetLayoutPath(const char* path);

    private:
        struct SplitDivider
        {
            ZUIDockNode* Node     = nullptr;
            bool         Dragging = false;
        };
        static constexpr uint32_t kMaxSplitDividers = 32;
        SplitDivider  m_split_dividers[kMaxSplitDividers] = {};
        uint32_t      m_split_divider_count               = 0;

        void BuildMenuBar   (ZUIContext* ctx, float sw, float mh);
        void BuildDockedPanel(ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildTabBar    (ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildDropZones (ZUIContext* ctx, ZUIPanel* p, float rect[4]);
        void BuildDividers  (ZUIContext* ctx);

        // CommitDrop: tab_idx == kWholePanel → move all views of src to dst
        void CommitDrop(ZUIPanel* src, uint32_t tab_idx, ZUIDockNode* dst, ZUIDropZone zone);
        void FocusPanel(uint32_t idx);
        void SyncSplitDividers();
        bool GetSplitDividerDragging(ZUIDockNode* node) const;
        void SetSplitDividerDragging(ZUIDockNode* node, bool v);
    };

} // namespace ZEngine::UI
