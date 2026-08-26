#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace ZEngine::UI
{
    // ---------------------------------------------------------------
    // Init / Shutdown
    // ---------------------------------------------------------------

    void ZUIPanelManager::Init(ArenaAllocator* arena)
    {
        DockTree              = ZUIDockTreeCreate(arena);
        PanelCount            = 0;
        m_split_divider_count = 0;
    }

    void ZUIPanelManager::Shutdown() {}

    // ---------------------------------------------------------------
    // Panel registration
    // ---------------------------------------------------------------

    ZUIPanel* ZUIPanelManager::AddPanel(uint64_t dock_key)
    {
        if (PanelCount >= kMaxPanels) { return nullptr; }
        ZUIPanel* p  = &Panels[PanelCount++];
        p->DockKey   = dock_key;
        p->ViewCount = 0;
        p->ActiveTab = 0;
        p->Floating  = false;
        return p;
    }

    void ZUIPanelManager::AddView(ZUIPanel* panel, ZUIPanelView* view)
    {
        if (!panel || panel->ViewCount >= kMaxTabsPerPanel) { return; }
        panel->Views[panel->ViewCount++] = view;
    }

    ZUIPanel* ZUIPanelManager::FindPanel(uint64_t dock_key)
    {
        for (uint32_t i = 0; i < PanelCount; ++i)
            if (Panels[i].DockKey == dock_key) return &Panels[i];
        return nullptr;
    }

    // ---------------------------------------------------------------
    // Focus helpers
    // ---------------------------------------------------------------

    void ZUIPanelManager::FocusPanel(uint32_t idx)
    {
        FocusedPanelIdx = idx;
        if (Panels[idx].Floating)
        {
            Panels[idx].ZOrder = ++FloatingZCounter;
        }
    }

    // ---------------------------------------------------------------
    // SplitDivider state — keyed by stable arena node pointer
    // ---------------------------------------------------------------

    bool ZUIPanelManager::GetSplitDividerDragging(ZUIDockNode* node) const
    {
        for (uint32_t i = 0; i < m_split_divider_count; ++i)
            if (m_split_dividers[i].Node == node) return m_split_dividers[i].Dragging;
        return false;
    }

    void ZUIPanelManager::SetSplitDividerDragging(ZUIDockNode* node, bool v)
    {
        for (uint32_t i = 0; i < m_split_divider_count; ++i)
        {
            if (m_split_dividers[i].Node == node) { m_split_dividers[i].Dragging = v; return; }
        }
        if (m_split_divider_count < kMaxSplitDividers)
        {
            m_split_dividers[m_split_divider_count++] = { node, v };
        }
    }

    // Walk tree, collect split nodes (non-leaf nodes).
    void ZUIPanelManager::SyncSplitDividers()
    {
        if (!DockTree || !DockTree->Root) return;

        // BFS walk with a fixed stack
        ZUIDockNode* stack[64];
        int top = 0;
        stack[top++] = DockTree->Root;

        // Mark which nodes we've seen this frame
        ZUIDockNode* seen[kMaxSplitDividers];
        uint32_t seen_count = 0;

        while (top > 0)
        {
            ZUIDockNode* node = stack[--top];
            if (!node) { continue; }

            if (node->ContentKey == 0 && node->First) // split node
            {
                seen[seen_count++] = node;
                // Ensure it has a slot in m_split_dividers
                bool found = false;
                for (uint32_t i = 0; i < m_split_divider_count; ++i)
                    if (m_split_dividers[i].Node == node) { found = true; break; }
                if (!found && m_split_divider_count < kMaxSplitDividers)
                    m_split_dividers[m_split_divider_count++] = { node, false };
            }

            // push children
            for (ZUIDockNode* c = node->First; c; c = c->Next)
                if (top < 64) stack[top++] = c;
        }

        // Prune stale entries (nodes removed from tree)
        for (uint32_t i = 0; i < m_split_divider_count; )
        {
            bool alive = false;
            for (uint32_t j = 0; j < seen_count; ++j)
                if (seen[j] == m_split_dividers[i].Node) { alive = true; break; }
            if (!alive)
            {
                m_split_dividers[i] = m_split_dividers[--m_split_divider_count];
            }
            else { ++i; }
        }
    }

    // ---------------------------------------------------------------
    // BuildUI — top-level per-frame entry
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildUI(ZUIContext* ctx, float menu_h, float status_h)
    {
        float sw = (float)ctx->ScreenW;
        float sh = (float)ctx->ScreenH;
        float scale = ctx->UIScale;

        float real_menu_h   = menu_h;
        float real_status_h = status_h;

        // Recompute dock layout every frame
        if (DockTree)
        {
            float root_rect[4] = { 0.f, real_menu_h, sw, sh - real_status_h };
            ZUIDockLayout(DockTree, root_rect);
            SyncSplitDividers();
        }

        // Full-screen opaque background
        ZUIBox* bg = ZUIBeginColumn(ctx, "##pm_bg", ZPx(sw), ZPx(sh));
        bg->Flags  = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f; bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        bg->EdgeSoftness = 0.f;

        // Menu bar
        BuildMenuBar(ctx, sw, real_menu_h);

        // Docked panels (only non-floating)
        Drag.HoverNode = nullptr;
        if (DockTree)
        {
            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                float r[4];
                if (!ZUIDockRectForKey(DockTree, p->DockKey, r)) { continue; }

                if (p->Floating)
                {
                    // Dock node was collapsed on pop-out — ZUIDockRectForKey returns false.
                    // This branch only fires if the panel floated before the collapse fix.
                    continue;
                }

                // Hover detection for drop zones
                if (Drag.Active && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                    Drag.HoverNode = ZUIDockFindLeaf(DockTree, p->DockKey);

                BuildDockedPanel(ctx, p, r);

                if (ctx->MousePressed[0] && !Drag.Active &&
                    mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                {
                    FocusPanel(i);
                }
            }
        }

        // Status bar
        if (real_status_h > 0.f)
        {
            ZUIBox* sbar = ZUIBeginRow(ctx, "##pm_sbar", ZPx(sw), ZPx(real_status_h));
            sbar->Flags       = sbar->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
            sbar->FloatPos[0] = 0.f;
            sbar->FloatPos[1] = sh - real_status_h;
            ZUIBoxSetColorArr(sbar, ctx->Theme.StatusBarBg);
            sbar->EdgeSoftness = 0.f;
            // Left: status text
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, "ZEngine Editor", ctx->Theme.TextDefault);

            // Fill spacer
            {
                char sfk[] = "##sbfill";
                ZUIBox* sf = ZUIPushBox(ctx, sfk, 8, ZUI_None);
                sf->Size[0] = ZFill(); sf->Size[1] = ZPx(real_status_h);
                ZUIPopBox(ctx);
            }

            // Right: FPS + UIScale indicator
            {
                char fps_buf[48];
                float fps = (ctx->DeltaTime > 0.0005f && ctx->DeltaTime < 1.f)
                          ? (1.f / ctx->DeltaTime) : 0.f;
                if (fps > 0.f)
                    snprintf(fps_buf, sizeof(fps_buf), "%.0f fps  |  UIScale %.1f",
                             (double)fps, (double)ctx->UIScale);
                else
                    snprintf(fps_buf, sizeof(fps_buf), "UIScale %.1f",
                             (double)ctx->UIScale);
                ZUILabel(ctx, fps_buf, ctx->Theme.TextDefault);
                ZUISpacer(ctx, 12.f);
            }

            ZUIEndRow(ctx);
        }

        // Dividers (dynamic from tree)
        BuildDividers(ctx);

        // Floating panels — sorted by ZOrder so highest renders last (on top)
        // Simple insertion sort over a small N
        uint32_t float_order[kMaxPanels];
        uint32_t float_count = 0;
        for (uint32_t i = 0; i < PanelCount; ++i)
            if (Panels[i].Floating) float_order[float_count++] = i;

        for (uint32_t i = 1; i < float_count; ++i)
        {
            uint32_t key = float_order[i];
            int j = (int)i - 1;
            while (j >= 0 && Panels[float_order[j]].ZOrder > Panels[key].ZOrder)
            {
                float_order[j + 1] = float_order[j];
                --j;
            }
            float_order[j + 1] = key;
        }
        for (uint32_t fi = 0; fi < float_count; ++fi)
            BuildFloatingPanel(ctx, &Panels[float_order[fi]]);

        // Drag ghost
        if (Drag.Active)
        {
            Drag.GhostX = ctx->MousePos[0];
            Drag.GhostY = ctx->MousePos[1];

            if (ctx->MouseReleased[0] && Drag.DropZone == ZUIDropZone::None)
                Drag.Active = false;

            if (Drag.Active)
            {
                ZUIPanel* sp    = Drag.SrcPanel;
                ZUIPanelView* sv = (sp && Drag.SrcTabIdx < sp->ViewCount)
                                 ? sp->Views[Drag.SrcTabIdx] : nullptr;
                const char* title = sv ? sv->Title : "Tab";

                float ghost_h = kTabBarH;
                float ghost_w = (float)(strlen(title) * 8 + 48);

                // Ghost looks like a detached tab — matches the tab bar style
                ZUIBox* ghost = ZUIBeginRow(ctx, "##drag_ghost", ZPx(ghost_w), ZPx(ghost_h));
                ghost->Flags      = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                                    ZUI_FloatX   | ZUI_FloatY;
                ghost->FloatPos[0] = Drag.GhostX - ghost_w * 0.3f;
                ghost->FloatPos[1] = Drag.GhostY - ghost_h * 0.5f;
                // Active tab appearance: #1e1e1e bg + blue border
                ZUIBoxSetColorArr(ghost, ctx->Theme.TabActiveBg);
                ghost->BorderColor[0] = ctx->Theme.TabActiveBorder[0];
                ghost->BorderColor[1] = ctx->Theme.TabActiveBorder[1];
                ghost->BorderColor[2] = ctx->Theme.TabActiveBorder[2];
                ghost->BorderColor[3] = 0.90f;
                ghost->BorderThickness = 1.f;
                ZUIBoxSetCornerRadius(ghost, 3.f);
                ghost->EdgeSoftness = 0.f;

                // Small colored icon dot matching the view type (uses hash of title as color seed)
                {
                    char ik[32] = "##ghost_icon";
                    ZUIBox* ic = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                    ic->Size[0] = ZPx(8.f); ic->Size[1] = ZPx(8.f);
                    // Deterministic color from title: cycle through a small palette
                    static const float kIconPalette[][4] = {
                        {0.40f,0.78f,1.00f,1.f},
                        {0.72f,0.50f,0.98f,1.f},
                        {0.30f,0.78f,0.30f,1.f},
                        {0.98f,0.85f,0.25f,1.f},
                        {0.98f,0.40f,0.40f,1.f},
                    };
                    uint32_t ci = (title[0] % 5);
                    ZUIBoxSetColorArr(ic, kIconPalette[ci]);
                    ZUIBoxSetCornerRadius(ic, 2.f);
                    ic->EdgeSoftness = 0.5f;
                    ZUIPopBox(ctx);
                }
                ZUISpacer(ctx, 6.f);
                ZUILabel(ctx, title, ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
        }

        ZUIEndColumn(ctx); // close pm_bg
    }

    // ---------------------------------------------------------------
    // BuildMenuBar
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildMenuBar(ZUIContext* ctx, float sw, float mh)
    {
        ZUIBox* bar = ZUIBeginRow(ctx, "##pm_menubar", ZPx(sw), ZPx(mh));
        bar->Flags  = bar->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bar, ctx->Theme.MenuBarBg);
        bar->EdgeSoftness = 0.f;
        // Bottom border only — VS Code / Unreal style
        bar->BorderColor[0] = ctx->Theme.Separator[0];
        bar->BorderColor[1] = ctx->Theme.Separator[1];
        bar->BorderColor[2] = ctx->Theme.Separator[2];
        bar->BorderColor[3] = ctx->Theme.Separator[3];
        bar->BorderThickness = 1.f;
        bar->Flags = bar->Flags | ZUI_DrawBorder;

        ZUISpacer(ctx, 10.f);
        if (ZUIBeginMenu(ctx, "File")) {
            ZUIMenuItem(ctx, "New Scene");
            ZUIMenuItem(ctx, "Open Scene...");
            ZUIMenuItem(ctx, "Save Scene");
            ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Save Layout");
            ZUIMenuItem(ctx, "Load Layout");
            ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Quit");
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Edit")) {
            ZUIMenuItem(ctx, "Undo");
            ZUIMenuItem(ctx, "Redo");
            ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Preferences...");
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Window")) {
            ZUIMenuItem(ctx, "Reset Layout");
            ZUISeparator(ctx);
            // Toggle visibility for each panel
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->ViewCount == 0) { continue; }
                const char* name = p->Views[0] ? p->Views[0]->Title : "Panel";
                char buf[80];
                snprintf(buf, sizeof(buf), "%s##wm_%u", name, i);
                if (ZUIMenuItem(ctx, buf) && p->Floating)
                    p->Floating = false; // re-dock if floating
            }
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Help")) {
            ZUIMenuItem(ctx, "Documentation");
            ZUIMenuItem(ctx, "About ZEngine");
            ZUIEndMenu(ctx);
        }

        // Right side: engine label
        {
            char fill_key[] = "##mb_fill";
            ZUIBox* fill = ZUIPushBox(ctx, fill_key, (uint32_t)strlen(fill_key), ZUI_None);
            fill->Size[0] = ZFill();
            fill->Size[1] = ZPx(mh);
            ZUIPopBox(ctx);
        }
        ZUILabel(ctx, "ZEngine", ctx->Theme.TextDim);
        ZUISpacer(ctx, 12.f);

        ZUIEndRow(ctx);
    }

    // ---------------------------------------------------------------
    // BuildEmptySlot — placeholder for a floating panel's vacated dock spot
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildEmptySlot(ZUIContext* ctx, float rect[4], uint64_t key)
    {
        char sk[40];
        snprintf(sk, sizeof(sk), "##empty_%llx", (unsigned long long)key);
        ZUIBox* slot = ZUIBeginColumn(ctx, sk, ZPx(rect[2]-rect[0]), ZPx(rect[3]-rect[1]));
        slot->Flags      = slot->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                           ZUI_FloatX  | ZUI_FloatY;
        slot->FloatPos[0] = rect[0];
        slot->FloatPos[1] = rect[1];
        float empty_bg[4]  = { 0.08f, 0.08f, 0.09f, 1.f };
        float empty_bdr[4] = { 0.20f, 0.20f, 0.22f, 0.6f };
        ZUIBoxSetColorArr(slot, empty_bg);
        slot->BorderColor[0]=empty_bdr[0]; slot->BorderColor[1]=empty_bdr[1];
        slot->BorderColor[2]=empty_bdr[2]; slot->BorderColor[3]=empty_bdr[3];
        slot->BorderThickness = 1.f;
        slot->EdgeSoftness    = 0.f;
        // Centred hint label
        float hint_col[4] = { 0.35f, 0.35f, 0.38f, 1.f };
        ZUILabel(ctx, "Panel detached", hint_col);
        ZUIEndColumn(ctx);

        // Drop zone: dragging a floating panel title over this slot re-docks it
        if (Drag.Active)
        {
            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            if (mx >= rect[0] && mx <= rect[2] && my >= rect[1] && my <= rect[3])
                Drag.HoverNode = ZUIDockFindLeaf(DockTree, key);
        }
    }

    // ---------------------------------------------------------------
    // BuildDockedPanel
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDockedPanel(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        if (!p || p->ViewCount == 0) { return; }

        bool is_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx) { is_focused = true; break; }

        char panel_key[32];
        snprintf(panel_key, sizeof(panel_key), "##panel_%llx", (unsigned long long)p->DockKey);

        // ImGui-style: docked panels are square (no corner radius), border is subtle grey
        ZUIBox* panel = ZUIBeginColumn(ctx, panel_key,
                                       ZPx(rect[2]-rect[0]), ZPx(rect[3]-rect[1]));
        panel->Flags  = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                        ZUI_FloatX   | ZUI_FloatY;
        panel->FloatPos[0]  = rect[0];
        panel->FloatPos[1]  = rect[1];
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        // VS Code: 1px subtle border always, same color focused/unfocused
        panel->BorderColor[0] = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1] = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2] = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3] = ctx->Theme.PanelBorder[3];
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // VS Code panel title bar height
        bool  show_tabs     = (p->ViewCount > 1);
        float header_h      = kTabBarH + 2.f; // 28px
        bool  should_popout = false;

        if (show_tabs)
        {
            float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + header_h };
            BuildTabBar(ctx, p, tab_rect, false);
        }
        else
        {
            // VS Code-style panel title bar:
            // [■ icon]  Panel Title                   [×]  ← × hover-only
            const char* view_title = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "Panel";
            float btn_h = header_h * 0.60f;

            char hk[48]; snprintf(hk, sizeof(hk), "##tbar_%llx", (unsigned long long)p->DockKey);
            ZUIBox* strip = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
            // ZUI_Clickable so we can detect drag-to-detach gesture
            strip->Flags = strip->Flags | ZUI_DrawBackground | ZUI_Clickable;
            ZUIBoxSetColorArr(strip, ctx->Theme.MenuBarBg);
            strip->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 10.f);

            // Small colored dot icon (panel's TabColor)
            {
                const float* ic = (p->Views[0] && p->Views[0]->TabColor[3] > 0.01f)
                                 ? p->Views[0]->TabColor : ctx->Theme.PanelFocusBorder;
                char ik[48]; snprintf(ik, sizeof(ik), "##tic_%llx", (unsigned long long)p->DockKey);
                ZUIBox* icon = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                icon->Size[0] = ZPx(8.f); icon->Size[1] = ZPx(8.f);
                ZUIBoxSetColorArr(icon, ic);
                ZUIBoxSetCornerRadius(icon, 4.f); // full circle
                icon->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 7.f);

            // Panel title — always default text color (VS Code doesn't change title color on focus)
            ZUILabel(ctx, view_title, ctx->Theme.TextDefault);

            // Fill spacer
            {
                char fk[48]; snprintf(fk, sizeof(fk), "##tf_%llx", (unsigned long long)p->DockKey);
                ZUIBox* fill = ZUIPushBox(ctx, fk, (uint32_t)strlen(fk), ZUI_None);
                fill->Size[0] = ZFill(); fill->Size[1] = ZPx(header_h);
                ZUIPopBox(ctx);
            }

            // × close button — VS Code: hover-only, appears as soft × on hover
            bool should_close = false;
            bool panel_hovered = (ctx->MousePos[0] >= rect[0] && ctx->MousePos[0] <= rect[2] &&
                                  ctx->MousePos[1] >= rect[1] && ctx->MousePos[1] <= rect[1] + header_h);
            if (panel_hovered) // only render when strip is hovered
            {
                char xk[56]; snprintf(xk, sizeof(xk), "x##tx_%llx", (unsigned long long)p->DockKey);
                ZUIBox* xbtn = ZUIPushBox(ctx, xk, (uint32_t)strlen(xk), ZUI_DrawText | ZUI_Clickable);
                xbtn->Size[0] = ZPx(btn_h); xbtn->Size[1] = ZPx(btn_h);
                xbtn->TextAlign = ZUITextAlign::Center;
                bool xh = (ctx->HotKey == xbtn->Key);
                xbtn->TextColor[0] = xh ? 1.f : 0.55f;
                xbtn->TextColor[1] = xh ? 0.4f : 0.55f;
                xbtn->TextColor[2] = xh ? 0.4f : 0.55f;
                xbtn->TextColor[3] = 1.f;
                ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
                ZUIPopBox(ctx);
                if (xsig.Flags & ZUI_SignalClicked) should_close = true;
            }
            ZUISpacer(ctx, 6.f);

            // Signal on the strip for drag-to-detach
            ZUISignal strip_sig = ZUISignalFromBox(ctx, strip);
            ZUIEndRow(ctx);

            // Record press start for drag threshold
            if (strip_sig.Flags & ZUI_SignalPressed)
            {
                Drag.StartX = ctx->MousePos[0];
                Drag.StartY = ctx->MousePos[1];
            }

            // Drag > 8px → detach panel to floating (same direction as tab drag)
            if (!should_close && (strip_sig.Flags & ZUI_SignalHeld))
            {
                float dx = fabsf(ctx->MousePos[0] - Drag.StartX);
                float dy = fabsf(ctx->MousePos[1] - Drag.StartY);
                if (dx + dy > 8.f)
                {
                    // Position panel so cursor is on the title bar
                    float pw = rect[2] - rect[0], ph = rect[3] - rect[1];
                    float fx = ctx->MousePos[0] - pw * 0.3f; // cursor ~30% from left
                    float fy = ctx->MousePos[1] - header_h * 0.5f;
                    PopOutPanel(p, fx, fy, pw, ph);
                    p->DraggingTitle = true;
                    p->DragOffX = ctx->MousePos[0] - fx;
                    p->DragOffY = ctx->MousePos[1] - fy;
                }
            }

            if (should_close) should_popout = true;
        }

        // Content
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view && !should_popout)
        {
            ZUIBox* content = ZUIBeginColumn(ctx, "##dc", ZFill(), ZFill());
            content->Flags  = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            float content_rect[4] = { rect[0], rect[1] + header_h, rect[2], rect[3] };
            view->BuildContent(ctx, content_rect);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx); // panel

        // Deferred pop-out (after panel column is closed)
        if (should_popout)
        {
            float px = rect[0] + 20.f, py = rect[1] + 20.f;
            float pw = rect[2] - rect[0], ph = rect[3] - rect[1];
            PopOutPanel(p, px, py, pw, ph);
        }

        // VS Code focus indicator: 3px teal left strip on focused panel
        if (is_focused)
        {
            char fk[48]; snprintf(fk, sizeof(fk), "##pfocus_%llx", (unsigned long long)p->DockKey);
            uint32_t klen = (uint32_t)strlen(fk);
            ZUIBox* fb = ZUIPushBox(ctx, fk, klen, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            fb->Size[0]     = ZPx(3.f);
            fb->Size[1]     = ZPx(rect[3] - rect[1]);
            fb->FloatPos[0] = rect[0];
            fb->FloatPos[1] = rect[1];
            ZUIBoxSetColorArr(fb, ctx->Theme.PanelFocusBorder);
            fb->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        // Drop zones during drag
        if (Drag.Active && Drag.HoverNode && Drag.HoverNode->ContentKey == p->DockKey)
            BuildDropZones(ctx, p, rect);
    }

    // ---------------------------------------------------------------
    // BuildFloatingPanel — Level 1 in-app floating
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildFloatingPanel(ZUIContext* ctx, ZUIPanel* p)
    {
        if (!p || p->ViewCount == 0) { return; }

        float scale = ctx->UIScale;
        float sw = (float)ctx->ScreenW, sh = (float)ctx->ScreenH;
        float title_h = kTitleBarH;
        float mx = ctx->MousePos[0], my = ctx->MousePos[1];
        float dx = ctx->MousePos[0] - ctx->PrevMousePos[0];
        float dy = ctx->MousePos[1] - ctx->PrevMousePos[1];

        // --- Handle title bar drag (move) ---
        if (p->DraggingTitle)
        {
            if (ctx->MouseDown[0])
            {
                p->FloatX += dx;
                p->FloatY += dy;
                // Clamp to screen
                if (p->FloatX < 0.f)          p->FloatX = 0.f;
                if (p->FloatY < title_h)       p->FloatY = title_h;
                if (p->FloatX + p->FloatW > sw) p->FloatX = sw - p->FloatW;
                if (p->FloatY + p->FloatH > sh) p->FloatY = sh - p->FloatH;

                // Hover over a docked panel → show drop zones
                Drag.HoverNode = nullptr;
                for (uint32_t i = 0; i < PanelCount; ++i)
                {
                    ZUIPanel* dp = &Panels[i];
                    if (dp->Floating || dp == p) { continue; }
                    float r[4];
                    if (!ZUIDockRectForKey(DockTree, dp->DockKey, r)) { continue; }
                    if (mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                    {
                        Drag.HoverNode = ZUIDockFindLeaf(DockTree, dp->DockKey);
                        Drag.SrcPanel  = p;
                        Drag.SrcTabIdx = p->ActiveTab;
                        break;
                    }
                }
            }
            if (ctx->MouseReleased[0])
            {
                p->DraggingTitle = false;
                // Re-dock if hovering a drop zone
                if (Drag.HoverNode && Drag.DropZone != ZUIDropZone::None)
                {
                    RedockPanel(p, Drag.HoverNode, Drag.DropZone);
                    Drag.HoverNode = nullptr;
                    Drag.DropZone  = ZUIDropZone::None;
                    Drag.SrcPanel  = nullptr;
                    return; // panel is docked, skip floating render
                }
                Drag.HoverNode = nullptr;
                Drag.DropZone  = ZUIDropZone::None;
            }
        }

        // --- Handle resize ---
        if (p->Resizing)
        {
            if (ctx->MouseDown[0])
            {
                p->FloatW += dx;
                p->FloatH += dy;
                if (p->FloatW < 120.f) p->FloatW = 120.f;
                if (p->FloatH <  80.f) p->FloatH =  80.f;
            }
            if (ctx->MouseReleased[0]) p->Resizing = false;
        }

        float px = p->FloatX, py = p->FloatY;
        float pw = p->FloatW, ph = p->FloatH;

        // Outer floating window box
        char win_key[40];
        snprintf(win_key, sizeof(win_key), "##fp_%llx", (unsigned long long)p->DockKey);
        ZUIBox* win = ZUIBeginColumn(ctx, win_key, ZPx(pw), ZPx(ph));
        win->Flags      = win->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow |
                          ZUI_FloatX  | ZUI_FloatY;
        win->FloatPos[0] = px;
        win->FloatPos[1] = py;
        ZUIBoxSetColorArr(win, ctx->Theme.PanelBg);
        // Floating panels always get the accent border
        win->BorderColor[0] = ctx->Theme.PanelFocusBorder[0];
        win->BorderColor[1] = ctx->Theme.PanelFocusBorder[1];
        win->BorderColor[2] = ctx->Theme.PanelFocusBorder[2];
        win->BorderColor[3] = ctx->Theme.PanelFocusBorder[3];
        win->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(win, 4.f);
        win->EdgeSoftness = 0.f;

        // Focus on click
        if (ctx->MousePressed[0] && mx >= px && mx <= px+pw && my >= py && my <= py+ph)
        {
            for (uint32_t pi = 0; pi < PanelCount; ++pi)
                if (&Panels[pi] == p) { FocusPanel(pi); break; }
        }

        // --- Title bar ---
        {
            char tbar_key[48];
            snprintf(tbar_key, sizeof(tbar_key), "##fp_tbar_%llx", (unsigned long long)p->DockKey);
            ZUIBox* tbar = ZUIBeginRow(ctx, tbar_key, ZFill(), ZPx(title_h));
            tbar->Flags     = tbar->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
            float tbg[4]    = { ctx->Theme.TitleBarBg[0] + 0.03f,
                                ctx->Theme.TitleBarBg[1] + 0.03f,
                                ctx->Theme.TitleBarBg[2] + 0.03f, 1.f };
            ZUIBoxSetColorArr(tbar, tbg);
            tbar->BorderColor[0] = ctx->Theme.Separator[0];
            tbar->BorderColor[1] = ctx->Theme.Separator[1];
            tbar->BorderColor[2] = ctx->Theme.Separator[2];
            tbar->BorderColor[3] = ctx->Theme.Separator[3];
            tbar->BorderThickness = 1.f;
            tbar->EdgeSoftness    = 0.f;

            // Panel title
            ZUISpacer(ctx, 8.f);
            const char* panel_title = (p->ViewCount > 0 && p->ActiveTab < p->ViewCount && p->Views[p->ActiveTab])
                                     ? p->Views[p->ActiveTab]->Title : "Panel";
            ZUILabel(ctx, panel_title, ctx->Theme.TextDefault);

            // Re-dock button (→ restore to original dock position)
            ZUISpacer(ctx, 8.f);
            char dock_key[48];
            snprintf(dock_key, sizeof(dock_key), "⊟##fp_dock_%llx", (unsigned long long)p->DockKey);
            float dock_sz = title_h * 0.65f;
            ZUIBox* dock_btn = ZUIPushBox(ctx, dock_key, (uint32_t)strlen(dock_key),
                                          ZUI_DrawText | ZUI_Clickable);
            dock_btn->Size[0]   = ZPx(dock_sz);
            dock_btn->Size[1]   = ZPx(dock_sz);
            dock_btn->TextAlign = ZUITextAlign::Center;
            float dc[4] = { 0.5f, 0.7f, 0.5f, 0.9f };
            dock_btn->TextColor[0]=dc[0]; dock_btn->TextColor[1]=dc[1];
            dock_btn->TextColor[2]=dc[2]; dock_btn->TextColor[3]=dc[3];
            ZUISignal dsig = ZUISignalFromBox(ctx, dock_btn);
            ZUIPopBox(ctx);
            if (dsig.Flags & ZUI_SignalClicked)
            {
                // Restore to original dock position
                p->Floating = false;
                ZUIEndRow(ctx);
                ZUIEndColumn(ctx);
                return;
            }

            // Close button
            char xkey[48];
            snprintf(xkey, sizeof(xkey), "×##fp_close_%llx", (unsigned long long)p->DockKey);
            float close_sz = title_h * 0.65f;
            ZUIBox* xbtn = ZUIPushBox(ctx, xkey, (uint32_t)strlen(xkey),
                                       ZUI_DrawText | ZUI_Clickable);
            xbtn->Size[0]   = ZPx(close_sz);
            xbtn->Size[1]   = ZPx(close_sz);
            xbtn->TextAlign = ZUITextAlign::Center;
            float xc[4] = { 0.75f, 0.35f, 0.35f, 0.9f };
            xbtn->TextColor[0]=xc[0]; xbtn->TextColor[1]=xc[1];
            xbtn->TextColor[2]=xc[2]; xbtn->TextColor[3]=xc[3];
            ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
            ZUIPopBox(ctx);
            ZUISpacer(ctx, 4.f);

            // Title bar drag — detect via held + mouse movement
            ZUISignal tbar_sig = ZUISignalFromBox(ctx, tbar);
            if (!p->DraggingTitle && (tbar_sig.Flags & ZUI_SignalHeld))
            {
                float dist = fabsf(dx) + fabsf(dy);
                if (dist > 2.f) { p->DraggingTitle = true; }
            }

            ZUIEndRow(ctx); // tbar

            if (xsig.Flags & ZUI_SignalClicked)
            {
                // Restore to dock rather than destroying (Level 1 behavior)
                p->Floating = false;
                ZUIEndColumn(ctx);
                return;
            }
        }

        // Tab bar (only when > 1 view)
        bool show_tabs = (p->ViewCount > 1);
        float tab_h = show_tabs ? kTabBarH : 0.f;
        if (show_tabs)
        {
            float tab_rect[4] = { px, py + title_h, px + pw, py + title_h + tab_h };
            BuildTabBar(ctx, p, tab_rect, true);
        }

        // Content
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view)
        {
            ZUIBox* content = ZUIBeginColumn(ctx, "##fc", ZFill(), ZFill());
            content->Flags  = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            float cr[4] = { px, py + title_h + tab_h, px + pw, py + ph - kResizeGrip };
            view->BuildContent(ctx, cr);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx); // win

        // Resize handle (bottom-right corner)
        {
            float rg = kResizeGrip;
            char rk[48];
            snprintf(rk, sizeof(rk), "##fp_resize_%llx", (unsigned long long)p->DockKey);
            ZUIBox* grip = ZUIPushBox(ctx, rk, (uint32_t)strlen(rk),
                                      ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY | ZUI_Clickable);
            grip->Size[0]     = ZPx(rg);
            grip->Size[1]     = ZPx(rg);
            grip->FloatPos[0] = px + pw - rg;
            grip->FloatPos[1] = py + ph - rg;
            float grip_col[4] = { 0.35f, 0.35f, 0.40f, 0.5f };
            ZUIBoxSetColorArr(grip, grip_col);
            ZUIBoxSetCornerRadius(grip, 2.f);
            grip->EdgeSoftness = 0.f;
            ZUISignal gsig = ZUISignalFromBox(ctx, grip);
            ZUIPopBox(ctx);

            if (!p->Resizing && (gsig.Flags & ZUI_SignalHeld))
            {
                float dist = fabsf(dx) + fabsf(dy);
                if (dist > 1.f)
                {
                    p->Resizing      = true;
                    p->ResizeStartW  = p->FloatW;
                    p->ResizeStartH  = p->FloatH;
                }
            }
        }

        // Drop zones when dragging a tab over this floating panel
        if (Drag.Active && !p->DraggingTitle)
        {
            float fmx = ctx->MousePos[0], fmy = ctx->MousePos[1];
            if (fmx >= px && fmx <= px+pw && fmy >= py && fmy <= py+ph)
            {
                Drag.HoverNode = ZUIDockFindLeaf(DockTree, p->DockKey);
                if (Drag.HoverNode)
                {
                    float drop_rect[4] = { px, py + title_h + tab_h, px+pw, py+ph };
                    BuildDropZones(ctx, p, drop_rect);
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // BuildTabBar
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildTabBar(ZUIContext* ctx, ZUIPanel* p, float rect[4], bool in_float)
    {
        float tab_h = rect[3] - rect[1]; // full bar height (28px)

        char bar_key[40];
        snprintf(bar_key, sizeof(bar_key), "##tabbar_%llx", (unsigned long long)p->DockKey);

        // Bar background
        bool panel_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx) { panel_focused = true; break; }
        const float* bar_bg = panel_focused ? ctx->Theme.TitleBgActive : ctx->Theme.TitleBarBg;

        ZUIBox* bar = ZUIBeginRow(ctx, bar_key, ZFill(), ZPx(tab_h));
        // ZUI_Clickable: empty space in tab bar detects drag-to-detach
        bar->Flags  = bar->Flags | ZUI_DrawBackground | ZUI_Clickable;
        ZUIBoxSetColorArr(bar, bar_bg);
        bar->EdgeSoftness = 0.f;

        ZUISpacer(ctx, 4.f);

        for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
        {
            ZUIPanelView* view = p->Views[ti];
            if (!view) { continue; }
            bool is_active = (ti == p->ActiveTab);
            bool has_color = view->TabColor[3] > 0.01f;

            // Column container: gives each tab its own vertical space in the bar row
            char col_key[64];
            snprintf(col_key, sizeof(col_key), "##tcol_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* col = ZUIBeginColumn(ctx, col_key, ZFit(), ZPx(tab_h));
            col->Flags = col->Flags | ZUI_Clickable;
            col->EdgeSoftness = 0.f;
            // 2px top accent line: colored for active, transparent for inactive.
            // Same height for ALL tabs — no visual size inconsistency.
            {
                char ak[72]; snprintf(ak, sizeof(ak), "##taccent_%llx_%u", (unsigned long long)p->DockKey, ti);
                ZUIBox* accent = ZUIPushBox(ctx, ak, (uint32_t)strlen(ak), ZUI_DrawBackground);
                accent->Size[0] = ZFill(); accent->Size[1] = ZPx(2.f);
                if (is_active)
                {
                    const float* acc = has_color ? view->TabColor : ctx->Theme.TabActiveBorder;
                    ZUIBoxSetColorArr(accent, acc);
                }
                else
                {
                    ZUIBoxSetColor(accent, 0.f, 0.f, 0.f, 0.f); // invisible
                }
                accent->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            // Tab row fills the remaining height within the column
            char tab_key[64];
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* tab = ZUIBeginRow(ctx, tab_key, ZFit(), ZFill());
            tab->Flags = tab->Flags | ZUI_DrawBackground | ZUI_Clickable;
            tab->EdgeSoftness = 0.f;
            // Chrome style: only top corners rounded, flat bottom
            ZUIBoxSetTopRadius(tab, 5.f);

            if (is_active)
            {
                // Active: panel body color (merges with content). NO border on the tab box
                // itself — border inflates visual size. Instead, draw a separate 2px
                // top-accent overlay after the tab row (top-only, no side/bottom lines).
                ZUIBoxSetColorArr(tab, ctx->Theme.PanelBg);
            }
            else
            {
                // Inactive: smooth hover lerp
                float inactive[4] = {
                    bar_bg[0] + 0.07f, bar_bg[1] + 0.04f, bar_bg[2] + 0.02f, 0.90f
                };
                float hover_col[4] = {
                    bar_bg[0] + 0.14f, bar_bg[1] + 0.08f, bar_bg[2] + 0.04f, 1.00f
                };
                auto* st = ZUIStateGetOrInsert(&ctx->StateStore, tab->Key);
                float ht = st ? st->HotT : 0.f;
                float blended[4];
                for (int ch = 0; ch < 4; ++ch)
                    blended[ch] = inactive[ch] + (hover_col[ch] - inactive[ch]) * ht;
                ZUIBoxSetColorArr(tab, blended);
            }

            ZUISpacer(ctx, 10.f);
            // Use ZFill() height so renderer centers text vertically within the tab
            {
                uint32_t tlen = (uint32_t)strlen(view->Title);
                ZUIBox* lbl = ZUIPushBox(ctx, view->Title, tlen, ZUI_DrawText);
                lbl->Size[0] = ZText();
                lbl->Size[1] = ZFill(); // full height → renderer centers text
                lbl->TextColor[0] = is_active ? ctx->Theme.TextDefault[0] : ctx->Theme.TextDim[0];
                lbl->TextColor[1] = is_active ? ctx->Theme.TextDefault[1] : ctx->Theme.TextDim[1];
                lbl->TextColor[2] = is_active ? ctx->Theme.TextDefault[2] : ctx->Theme.TextDim[2];
                lbl->TextColor[3] = 1.f;
                ZUIPopBox(ctx);
            }

            bool tab_closed  = false;
            bool tab_popped  = false;

            // VS Code close button:
            //   Active   → × always visible
            //   Inactive → × on tab hover only
            {
                float btn_sz = 18.f; // fixed 18px — readable, not tiny
                bool tab_hovered = (ctx->HotKey == tab->Key);
                bool show_close  = is_active || tab_hovered;

                if (show_close)
                {
                    ZUISpacer(ctx, 4.f);
                    char xkey[64];
                    snprintf(xkey, sizeof(xkey), "x##x_%llx_%u", (unsigned long long)p->DockKey, ti);
                    ZUIBox* xbtn = ZUIPushBox(ctx, xkey, (uint32_t)strlen(xkey),
                                               ZUI_DrawText | ZUI_Clickable);
                    xbtn->Size[0]   = ZPx(btn_sz);
                    xbtn->Size[1]   = ZPx(btn_sz);
                    xbtn->TextAlign = ZUITextAlign::Center;
                    bool xh = (ctx->HotKey == xbtn->Key);
                    // Active + hovered: reddish; Active normal: dim; Inactive hover: slightly visible
                    if (xh)
                    { xbtn->TextColor[0]=1.f; xbtn->TextColor[1]=0.35f; xbtn->TextColor[2]=0.35f; xbtn->TextColor[3]=1.f; }
                    else
                    { float a = is_active ? 0.55f : 0.40f;
                      xbtn->TextColor[0]=a; xbtn->TextColor[1]=a; xbtn->TextColor[2]=a; xbtn->TextColor[3]=1.f; }
                    ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
                    ZUIPopBox(ctx);
                    if (xsig.Flags & ZUI_SignalClicked) { tab_closed = true; }
                    ZUISpacer(ctx, 4.f);
                }
                else
                {
                    ZUISpacer(ctx, 4.f); // keep width consistent with when × is shown
                }
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);    // close tab row
            ZUIEndColumn(ctx); // close column wrapper

            // --- Handle events ---

            if (tab_closed && p->ViewCount > 1)
            {
                for (uint32_t j = ti; j + 1 < p->ViewCount; ++j)
                    p->Views[j] = p->Views[j + 1];
                --p->ViewCount;
                if (p->ActiveTab >= p->ViewCount) p->ActiveTab = p->ViewCount - 1;
                break;
            }

            if (tab_popped)
            {
                // Tear this panel off into floating mode
                float fx = p->FloatX, fy = p->FloatY;
                if (!p->Floating)
                {
                    // First pop-out: position near the tab bar
                    float r[4] = {};
                    if (ZUIDockRectForKey(DockTree, p->DockKey, r))
                    {
                        fx = r[0] + 20.f;
                        fy = r[1] + 20.f;
                        p->FloatW = r[2] - r[0];
                        p->FloatH = r[3] - r[1];
                    }
                }
                PopOutPanel(p, fx, fy, p->FloatW, p->FloatH);
                break;
            }

            if (!tab_closed && !tab_popped && (sig.Flags & ZUI_SignalClicked))
            {
                p->ActiveTab = ti;
                for (uint32_t pi = 0; pi < PanelCount; ++pi)
                    if (&Panels[pi] == p) { FocusPanel(pi); break; }
            }

            // Record press position for drag threshold
            if (sig.Flags & ZUI_SignalPressed)
            {
                Drag.StartX = ctx->MousePos[0];
                Drag.StartY = ctx->MousePos[1];
                p->ReorderActive  = false;
                p->ReorderAccumX  = 0.f;
            }

            if (!tab_closed && !tab_popped && (sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float dx_abs = fabsf(ctx->MousePos[0] - Drag.StartX);
                float dy_abs = fabsf(ctx->MousePos[1] - Drag.StartY);
                float total  = dx_abs + dy_abs;

                if (total > 5.f)
                {
                    bool mostly_horiz = (dx_abs > dy_abs * 1.5f);

                    if (mostly_horiz && p->ViewCount > 1)
                    {
                        // --- Horizontal drag → reorder tabs within bar ---
                        if (!p->ReorderActive)
                        {
                            p->ReorderActive = true;
                            p->ReorderTabIdx = ti;
                            p->ReorderAccumX = 0.f;
                        }
                        if (p->ReorderActive && p->ReorderTabIdx == ti)
                        {
                            p->ReorderAccumX += sig.DragDelta[0];
                            // Approximate tab slot width from bar width / tab count
                            float tab_slot = (rect[2] - rect[0]) / (float)p->ViewCount;
                            if (tab_slot < 40.f) tab_slot = 40.f;
                            float threshold = tab_slot * 0.5f;

                            if (p->ReorderAccumX > threshold && ti + 1 < p->ViewCount)
                            {
                                // Swap right
                                ZUIPanelView* tmp = p->Views[ti];
                                p->Views[ti]     = p->Views[ti + 1];
                                p->Views[ti + 1] = tmp;
                                if (p->ActiveTab == ti)        p->ActiveTab = ti + 1;
                                else if (p->ActiveTab == ti+1) p->ActiveTab = ti;
                                p->ReorderTabIdx = ti + 1;
                                p->ReorderAccumX -= threshold;
                                break;
                            }
                            else if (p->ReorderAccumX < -threshold && ti > 0)
                            {
                                // Swap left
                                ZUIPanelView* tmp = p->Views[ti];
                                p->Views[ti]     = p->Views[ti - 1];
                                p->Views[ti - 1] = tmp;
                                if (p->ActiveTab == ti)        p->ActiveTab = ti - 1;
                                else if (p->ActiveTab == ti-1) p->ActiveTab = ti;
                                p->ReorderTabIdx = ti - 1;
                                p->ReorderAccumX += threshold;
                                break;
                            }
                        }
                    }
                    else if (!mostly_horiz || dy_abs > 12.f)
                    {
                        // --- Vertical drag → dock drag ---
                        p->ReorderActive = false;
                        Drag.Active    = true;
                        Drag.SrcPanel  = p;
                        Drag.SrcTabIdx = ti;
                        Drag.GhostX    = ctx->MousePos[0];
                        Drag.GhostY    = ctx->MousePos[1];
                    }
                }
            }

            // Clear reorder on mouse release
            if (ctx->MouseReleased[0] && p->ReorderActive)
            {
                p->ReorderActive = false;
                p->ReorderAccumX = 0.f;
            }

            ZUISpacer(ctx, 2.f); // gap between tabs
        }

        // Bar signal: dragging empty tab bar space detaches the whole panel
        ZUISignal bar_sig = ZUISignalFromBox(ctx, bar);
        if (bar_sig.Flags & ZUI_SignalPressed)
        {
            Drag.StartX = ctx->MousePos[0];
            Drag.StartY = ctx->MousePos[1];
        }
        if ((bar_sig.Flags & ZUI_SignalHeld) && !Drag.Active && !p->ReorderActive)
        {
            float dx = fabsf(ctx->MousePos[0] - Drag.StartX);
            float dy = fabsf(ctx->MousePos[1] - Drag.StartY);
            if (dx + dy > 8.f)
            {
                // Use full panel rect for correct float size
                float panel_r[4] = {};
                if (!ZUIDockRectForKey(DockTree, p->DockKey, panel_r))
                { panel_r[0]=rect[0]; panel_r[1]=rect[1]; panel_r[2]=rect[2]; panel_r[3]=rect[3]+200.f; }
                float pw = panel_r[2] - panel_r[0];
                float ph = panel_r[3] - panel_r[1];
                float fx = ctx->MousePos[0] - pw * 0.3f;
                float fy = ctx->MousePos[1] - tab_h * 0.5f;
                PopOutPanel(p, fx, fy, pw, ph);
                p->DraggingTitle = true;
                p->DragOffX = ctx->MousePos[0] - fx;
                p->DragOffY = ctx->MousePos[1] - fy;
            }
        }

        ZUIEndRow(ctx);
    }

    // ---------------------------------------------------------------
    // BuildDropZones
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float fs        = 13.f * ctx->UIScale;
        float major_dim = fs * 5.f;
        float minor_dim = fs * 3.f;
        float w = rect[2] - rect[0], h = rect[3] - rect[1];
        float cx = (rect[0] + rect[2]) * 0.5f;
        float cy = (rect[1] + rect[3]) * 0.5f;
        float fmx = ctx->MousePos[0], fmy = ctx->MousePos[1];

        struct Zone {
            ZUIDropZone zone;
            float x0, y0, x1, y1;
            float px0, py0, px1, py1;
        };

        Zone zones[5] = {
            { ZUIDropZone::Center,
              cx-major_dim*0.5f, cy-minor_dim*0.5f, cx+major_dim*0.5f, cy+minor_dim*0.5f,
              rect[0], rect[1], rect[2], rect[3] },
            { ZUIDropZone::Left,
              rect[0]+2.f, cy-major_dim*0.5f, rect[0]+minor_dim+2.f, cy+major_dim*0.5f,
              rect[0], rect[1], rect[0]+w*0.4f, rect[3] },
            { ZUIDropZone::Right,
              rect[2]-minor_dim-2.f, cy-major_dim*0.5f, rect[2]-2.f, cy+major_dim*0.5f,
              rect[2]-w*0.4f, rect[1], rect[2], rect[3] },
            { ZUIDropZone::Top,
              cx-major_dim*0.5f, rect[1]+2.f, cx+major_dim*0.5f, rect[1]+minor_dim+2.f,
              rect[0], rect[1], rect[2], rect[1]+h*0.4f },
            { ZUIDropZone::Bottom,
              cx-major_dim*0.5f, rect[3]-minor_dim-2.f, cx+major_dim*0.5f, rect[3]-2.f,
              rect[0], rect[3]-h*0.4f, rect[2], rect[3] },
        };

        ZUIDropZone hovered = ZUIDropZone::None;
        for (auto& z : zones)
        {
            if (fmx >= z.x0 && fmx <= z.x1 && fmy >= z.y0 && fmy <= z.y1)
            { hovered = z.zone; break; }
        }
        Drag.DropZone = hovered;

        // Split preview
        if (hovered != ZUIDropZone::None && hovered != ZUIDropZone::Center)
        {
            for (auto& z : zones)
            {
                if (z.zone != hovered) { continue; }
                char pk[40];
                snprintf(pk, sizeof(pk), "##dz_prev_%llx", (unsigned long long)p->DockKey);
                ZUIBox* prev = ZUIPushBox(ctx, pk, (uint32_t)strlen(pk),
                                          ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                prev->Size[0]     = ZPx(z.px1 - z.px0);
                prev->Size[1]     = ZPx(z.py1 - z.py0);
                prev->FloatPos[0] = z.px0;
                prev->FloatPos[1] = z.py0;
                float prevcol[4] = { 0.137f, 0.573f, 0.922f, 0.18f };
                ZUIBoxSetColorArr(prev, prevcol);
                prev->BorderColor[0]=0.137f; prev->BorderColor[1]=0.573f;
                prev->BorderColor[2]=0.922f; prev->BorderColor[3]=0.70f;
                prev->BorderThickness = 1.f;
                prev->Flags = prev->Flags | ZUI_DrawBorder;
                prev->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
                break;
            }
        }

        // Zone indicator boxes — plain colored rectangles, no labels
        for (auto& z : zones)
        {
            bool  over    = (z.zone == hovered);
            float col[4]  = { ctx->Theme.TabActiveBorder[0],
                              ctx->Theme.TabActiveBorder[1],
                              ctx->Theme.TabActiveBorder[2],
                              over ? 0.80f : 0.35f };

            char zkey[48];
            snprintf(zkey, sizeof(zkey), "##dz_%d_%llx", (int)z.zone, (unsigned long long)p->DockKey);
            ZUIBox* box = ZUIPushBox(ctx, zkey, (uint32_t)strlen(zkey),
                                     ZUI_DrawBackground | ZUI_DrawBorder |
                                     ZUI_FloatX | ZUI_FloatY);
            box->Size[0]      = ZPx(z.x1 - z.x0);
            box->Size[1]      = ZPx(z.y1 - z.y0);
            box->FloatPos[0]  = z.x0;
            box->FloatPos[1]  = z.y0;
            ZUIBoxSetColorArr(box, col);
            box->BorderColor[0] = ctx->Theme.TabActiveBorder[0];
            box->BorderColor[1] = ctx->Theme.TabActiveBorder[1];
            box->BorderColor[2] = ctx->Theme.TabActiveBorder[2];
            box->BorderColor[3] = over ? 1.f : 0.55f;
            box->BorderThickness = over ? 2.f : 1.f;
            ZUIBoxSetCornerRadius(box, 5.f);
            box->EdgeSoftness = 0.5f;
            ZUIPopBox(ctx);
        }

        // Commit on release
        if (ctx->MouseReleased[0] && hovered != ZUIDropZone::None && Drag.SrcPanel)
        {
            ZUIDockNode* dst_node = ZUIDockFindLeaf(DockTree, p->DockKey);
            if (dst_node)
                CommitDrop(Drag.SrcPanel, Drag.SrcTabIdx, dst_node, hovered);
            Drag.Active   = false;
            Drag.SrcPanel = nullptr;
        }
    }

    // ---------------------------------------------------------------
    // BuildDividers — dynamic from dock tree
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDividers(ZUIContext* ctx)
    {
        if (!DockTree) { return; }

        float mx = ctx->MousePos[0], my = ctx->MousePos[1];

        // Walk all split nodes in m_split_dividers (populated by SyncSplitDividers)
        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            ZUIDockNode* snode = m_split_dividers[di].Node;
            if (!snode || !snode->First || !snode->First->Next) { continue; }

            ZUIDockNode* child1 = snode->First;
            ZUIDockNode* child2 = child1->Next;

            bool horizontal = (snode->SplitAxis == ZUIAxis::Y);
            float dx0, dy0, dx1, dy1;

            if (!horizontal) // vertical divider (X split)
            {
                float ex = child1->RectMax[0]; // right edge of first child
                dx0 = ex - kDivGrabW*0.5f;  dy0 = snode->RectMin[1];
                dx1 = ex + kDivGrabW*0.5f;  dy1 = snode->RectMax[1];
            }
            else // horizontal divider (Y split)
            {
                float ey = child1->RectMax[1]; // bottom edge of first child
                dx0 = snode->RectMin[0];  dy0 = ey - kDivGrabW*0.5f;
                dx1 = snode->RectMax[0];  dy1 = ey + kDivGrabW*0.5f;
            }

            bool in_rect = (mx >= dx0 && mx <= dx1 && my >= dy0 && my <= dy1);
            bool& dragging = m_split_dividers[di].Dragging;

            if (ctx->MousePressed[0]  && in_rect) dragging = true;
            if (ctx->MouseReleased[0])             dragging = false;

            if (dragging && ctx->MouseDown[0])
            {
                float delta = horizontal
                    ? (ctx->MousePos[1] - ctx->PrevMousePos[1])
                    : (ctx->MousePos[0] - ctx->PrevMousePos[0]);
                if (delta != 0.f)
                    ZUIDockResize(DockTree, child1, delta);
            }

            // Track active resize cursor for the app to apply via GLFW
            if (in_rect || dragging)
                ctx->ResizeCursor = horizontal ? 2 : 1; // 1=H-resize 2=V-resize

            // Divider visual: thin dim line at rest, accent-colored 3px bar when active
            bool highlight = in_rect || dragging;
            float line_w   = highlight ? 3.f : 1.f;
            float alpha    = highlight ? 1.f : 0.35f;
            float vc[4] = { ctx->Theme.TabActiveBorder[0],
                            ctx->Theme.TabActiveBorder[1],
                            ctx->Theme.TabActiveBorder[2], alpha };
            if (!highlight)
            {
                // Rest state: very subtle grey
                vc[0] = 0.25f; vc[1] = 0.25f; vc[2] = 0.28f;
            }

            // Hover area highlight — semi-transparent fill behind the line
            if (highlight)
            {
                char hk[32]; snprintf(hk, sizeof(hk), "##sdivh_%u", di);
                ZUIBox* ha = ZUIPushBox(ctx, hk, (uint32_t)strlen(hk),
                                        ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                ha->Size[0]     = ZPx(dx1 - dx0);
                ha->Size[1]     = ZPx(dy1 - dy0);
                ha->FloatPos[0] = dx0;
                ha->FloatPos[1] = dy0;
                ZUIBoxSetColor(ha, ctx->Theme.TabActiveBorder[0],
                               ctx->Theme.TabActiveBorder[1],
                               ctx->Theme.TabActiveBorder[2], 0.25f);
                ha->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            char vk[32];
            snprintf(vk, sizeof(vk), "##sdiv_%u", di);
            ZUIBox* vis = ZUIPushBox(ctx, vk, (uint32_t)strlen(vk),
                                     ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            if (!horizontal)
            {
                vis->Size[0]     = ZPx(line_w);
                vis->Size[1]     = ZPx(dy1 - dy0);
                vis->FloatPos[0] = (dx0 + dx1) * 0.5f - line_w * 0.5f;
                vis->FloatPos[1] = dy0;
            }
            else
            {
                vis->Size[0]     = ZPx(dx1 - dx0);
                vis->Size[1]     = ZPx(line_w);
                vis->FloatPos[0] = dx0;
                vis->FloatPos[1] = (dy0 + dy1) * 0.5f - line_w * 0.5f;
            }
            vis->EdgeSoftness = 0.f;
            ZUIBoxSetColorArr(vis, vc);
            ZUIPopBox(ctx);
        }
    }

    // ---------------------------------------------------------------
    // PopOutPanel — move panel from docked to floating
    // ---------------------------------------------------------------

    void ZUIPanelManager::PopOutPanel(ZUIPanel* p, float x, float y, float w, float h)
    {
        p->Floating = true;
        p->FloatX   = x;
        p->FloatY   = y;
        p->FloatW   = w;
        p->FloatH   = h;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p) { FocusPanel(pi); break; }

        // Collapse the dock tree so the sibling fills the vacated space.
        // If the panel later re-docks, RedockPanel re-inserts it.
        if (DockTree)
        {
            ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, p->DockKey);
            if (leaf) ZUIDockCollapseLeaf(DockTree, leaf);
        }
    }

    // ---------------------------------------------------------------
    // RedockPanel — move floating panel back into the dock tree
    // ---------------------------------------------------------------

    void ZUIPanelManager::RedockPanel(ZUIPanel* p, ZUIDockNode* dst, ZUIDropZone zone)
    {
        if (!p || !dst) { return; }

        if (zone == ZUIDropZone::Center)
        {
            // Merge into destination panel
            ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
            if (dst_panel && dst_panel != p)
            {
                for (uint32_t v = 0; v < p->ViewCount; ++v)
                    AddView(dst_panel, p->Views[v]);
                p->ViewCount = 0;
            }
        }
        else
        {
            // Split destination, put floating panel's key on one side
            float split_pct = 0.5f;
            if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
            {
                bool left = (zone == ZUIDropZone::Left);
                ZUIDockSplitH(DockTree, dst,
                              left ? split_pct : 1.f - split_pct,
                              left ? p->DockKey : dst->ContentKey,
                              left ? dst->ContentKey : p->DockKey);
            }
            else
            {
                bool top = (zone == ZUIDropZone::Top);
                ZUIDockSplitV(DockTree, dst,
                              top ? split_pct : 1.f - split_pct,
                              top ? p->DockKey : dst->ContentKey,
                              top ? dst->ContentKey : p->DockKey);
            }
        }

        p->Floating = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p) { FocusPanel(pi); break; }
    }

    // ---------------------------------------------------------------
    // CommitDrop — tab drag-to-dock commit
    // ---------------------------------------------------------------

    void ZUIPanelManager::CommitDrop(ZUIPanel* src, uint32_t tab_idx,
                                      ZUIDockNode* dst, ZUIDropZone zone)
    {
        if (!src || tab_idx >= src->ViewCount || !dst) { return; }
        ZUIPanelView* view = src->Views[tab_idx];

        if (zone == ZUIDropZone::Center)
        {
            ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
            if (dst_panel && dst_panel != src)
            {
                AddView(dst_panel, view);
                for (uint32_t i = tab_idx; i + 1 < src->ViewCount; ++i)
                    src->Views[i] = src->Views[i+1];
                --src->ViewCount;
                if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0)
                    src->ActiveTab = src->ViewCount - 1;
            }
        }
        else
        {
            float split_pct = 0.5f;
            uint64_t new_key = view->Key ? view->Key
                             : ZUIDockHashName(view->Title ? view->Title : "new");

            if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
            {
                bool left = (zone == ZUIDropZone::Left);
                ZUIDockSplitH(DockTree, dst,
                              left ? split_pct : 1.f - split_pct,
                              left ? new_key : dst->ContentKey,
                              left ? dst->ContentKey : new_key);
            }
            else
            {
                bool top = (zone == ZUIDropZone::Top);
                ZUIDockSplitV(DockTree, dst,
                              top ? split_pct : 1.f - split_pct,
                              top ? new_key : dst->ContentKey,
                              top ? dst->ContentKey : new_key);
            }

            ZUIPanel* new_panel = AddPanel(new_key);
            if (new_panel)
            {
                AddView(new_panel, view);
                for (uint32_t i = tab_idx; i + 1 < src->ViewCount; ++i)
                    src->Views[i] = src->Views[i+1];
                --src->ViewCount;
                if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0)
                    src->ActiveTab = src->ViewCount - 1;
            }
        }
    }

} // namespace ZEngine::UI
