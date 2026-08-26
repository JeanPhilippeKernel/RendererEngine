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
                    // Show placeholder in the vacated dock slot
                    BuildEmptySlot(ctx, r, p->DockKey);
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
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Ready", ctx->Theme.TextDefault);
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
                ZUIPanel* sp = Drag.SrcPanel;
                const char* title = (sp && Drag.SrcTabIdx < sp->ViewCount && sp->Views[Drag.SrcTabIdx])
                                   ? sp->Views[Drag.SrcTabIdx]->Title : "Tab";

                float ghost_h = kTabBarH;
                float ghost_w = (float)(strlen(title) * 9 + 36);
                float ghost_col[4] = { ctx->Theme.TabActiveBg[0],
                                       ctx->Theme.TabActiveBg[1],
                                       ctx->Theme.TabActiveBg[2] + 0.10f, 0.85f };

                ZUIBox* ghost = ZUIBeginRow(ctx, "##drag_ghost", ZPx(ghost_w), ZPx(ghost_h));
                ghost->Flags      = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                                    ZUI_FloatX   | ZUI_FloatY;
                ghost->FloatPos[0] = Drag.GhostX - ghost_w * 0.3f;
                ghost->FloatPos[1] = Drag.GhostY - ghost_h * 0.5f;
                ZUIBoxSetColorArr(ghost, ghost_col);
                ghost->BorderColor[0] = 0.40f; ghost->BorderColor[1] = 0.60f;
                ghost->BorderColor[2] = 0.90f; ghost->BorderColor[3] = 1.f;
                ghost->BorderThickness = 1.f;
                ZUIBoxSetCornerRadius(ghost, 4.f);
                ghost->EdgeSoftness = 0.5f;
                ZUISpacer(ctx, 8.f);
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
        bar->Flags  = bar->Flags | ZUI_DrawBackground | ZUI_DrawBorder;
        ZUIBoxSetColorArr(bar, ctx->Theme.MenuBarBg);
        bar->BorderColor[0] = ctx->Theme.Separator[0];
        bar->BorderColor[1] = ctx->Theme.Separator[1];
        bar->BorderColor[2] = ctx->Theme.Separator[2];
        bar->BorderColor[3] = ctx->Theme.Separator[3];
        bar->BorderThickness = 1.f;
        bar->EdgeSoftness    = 0.f;

        ZUISpacer(ctx, 6.f);
        if (ZUIBeginMenu(ctx, "File")) {
            ZUIMenuItem(ctx, "Save Layout");
            ZUIMenuItem(ctx, "Load Layout");
            ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Quit");
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 4.f);
        if (ZUIBeginMenu(ctx, "Window")) {
            ZUIMenuItem(ctx, "Reset Layout");
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 4.f);
        if (ZUIBeginMenu(ctx, "Panel")) {
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->ViewCount == 0) { continue; }
                const char* name = p->Views[0] ? p->Views[0]->Title : "Panel";
                char buf[80];
                snprintf(buf, sizeof(buf), "%s##pm_panel_%u", name, i);
                if (ZUIMenuItem(ctx, buf)) {}
            }
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 4.f);
        if (ZUIBeginMenu(ctx, "View")) {
            ZUIMenuItem(ctx, "Hierarchy");
            ZUIMenuItem(ctx, "Inspector");
            ZUIMenuItem(ctx, "Viewport");
            ZUIMenuItem(ctx, "Output");
            ZUIMenuItem(ctx, "Project");
            ZUIEndMenu(ctx);
        }

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

        float scale = ctx->UIScale;
        bool is_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx) { is_focused = true; break; }

        char panel_key[32];
        snprintf(panel_key, sizeof(panel_key), "##panel_%llx", (unsigned long long)p->DockKey);

        ZUIBox* panel = ZUIBeginColumn(ctx, panel_key,
                                       ZPx(rect[2]-rect[0]), ZPx(rect[3]-rect[1]));
        panel->Flags  = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                        ZUI_FloatX   | ZUI_FloatY;
        panel->FloatPos[0]  = rect[0];
        panel->FloatPos[1]  = rect[1];
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        const float* bcol = is_focused ? ctx->Theme.PanelFocusBorder : ctx->Theme.PanelBorder;
        panel->BorderColor[0] = bcol[0]; panel->BorderColor[1] = bcol[1];
        panel->BorderColor[2] = bcol[2]; panel->BorderColor[3] = bcol[3];
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // Tab bar (hidden when single view — same as ImGui/RAD)
        bool show_tabs = (p->ViewCount > 1);
        float tab_h = show_tabs ? kTabBarH : 0.f;
        if (show_tabs)
        {
            float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + tab_h };
            BuildTabBar(ctx, p, tab_rect, false);
        }

        // Content
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view)
        {
            ZUIBox* content = ZUIBeginColumn(ctx, "##dc", ZFill(), ZFill());
            content->Flags  = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            float content_rect[4] = { rect[0], rect[1] + tab_h, rect[2], rect[3] };
            view->BuildContent(ctx, content_rect);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx); // panel

        // Unfocused dim overlay
        if (!is_focused)
        {
            char ov_key[40];
            snprintf(ov_key, sizeof(ov_key), "##pov_%llx", (unsigned long long)p->DockKey);
            uint32_t klen = (uint32_t)strlen(ov_key);
            ZUIBox* ov = ZUIPushBox(ctx, ov_key, klen, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            ov->Size[0]     = ZPx(rect[2] - rect[0]);
            ov->Size[1]     = ZPx(rect[3] - rect[1]);
            ov->FloatPos[0] = rect[0];
            ov->FloatPos[1] = rect[1];
            ZUIBoxSetColorArr(ov, ctx->Theme.PanelInactiveOverlay);
            ov->EdgeSoftness = 0.f;
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
        win->Flags      = win->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
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
        float scale = ctx->UIScale;
        float tab_h = rect[3] - rect[1];

        char bar_key[40];
        snprintf(bar_key, sizeof(bar_key), "##tabbar_%llx", (unsigned long long)p->DockKey);

        ZUIBox* bar = ZUIBeginRow(ctx, bar_key, ZFill(), ZPx(tab_h));
        bar->Flags  = bar->Flags | ZUI_DrawBackground | ZUI_DrawBorder;
        ZUIBoxSetColorArr(bar, ctx->Theme.TitleBarBg);
        bar->BorderColor[0] = ctx->Theme.Separator[0];
        bar->BorderColor[1] = ctx->Theme.Separator[1];
        bar->BorderColor[2] = ctx->Theme.Separator[2];
        bar->BorderColor[3] = ctx->Theme.Separator[3];
        bar->BorderThickness = 1.f;
        bar->EdgeSoftness    = 0.f;

        ZUISpacer(ctx, 4.f);

        for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
        {
            ZUIPanelView* view = p->Views[ti];
            if (!view) { continue; }
            bool is_active = (ti == p->ActiveTab);

            char col_key[64];
            snprintf(col_key, sizeof(col_key), "##tcol_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* col = ZUIBeginColumn(ctx, col_key, ZFit(), ZPx(tab_h));
            col->Flags = col->Flags | ZUI_Clickable;
            col->EdgeSoftness = 0.f;
            ZUISpacer(ctx, 1.f);

            char tab_key[64];
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* tab = ZUIBeginRow(ctx, tab_key, ZFit(), ZFill());
            tab->Flags = tab->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
            tab->EdgeSoftness = 0.5f;
            ZUIBoxSetCornerRadius(tab, 3.f);

            if (is_active)
            {
                ZUIBoxSetColorArr(tab, ctx->Theme.TabActiveBg);
                tab->BorderColor[0] = ctx->Theme.TabActiveBorder[0];
                tab->BorderColor[1] = ctx->Theme.TabActiveBorder[1];
                tab->BorderColor[2] = ctx->Theme.TabActiveBorder[2];
                tab->BorderColor[3] = ctx->Theme.TabActiveBorder[3];
                tab->BorderThickness = 1.f;
            }
            else
            {
                ZUIBoxSetColorArr(tab, ctx->Theme.TabInactiveBg);
                tab->BorderColor[0] = ctx->Theme.TabInactiveBorder[0];
                tab->BorderColor[1] = ctx->Theme.TabInactiveBorder[1];
                tab->BorderColor[2] = ctx->Theme.TabInactiveBorder[2];
                tab->BorderColor[3] = ctx->Theme.TabInactiveBorder[3];
                tab->BorderThickness = 1.f;
            }

            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, view->Title,
                     is_active ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

            bool tab_closed  = false;
            bool tab_popped  = false;

            if (is_active)
            {
                float btn_sz = tab_h * 0.55f;

                // Pop-out button (tear off to floating)
                if (!in_float)
                {
                    ZUISpacer(ctx, 4.f);
                    char popkey[64];
                    snprintf(popkey, sizeof(popkey), "⊞##pop_%llx_%u", (unsigned long long)p->DockKey, ti);
                    ZUIBox* pb = ZUIPushBox(ctx, popkey, (uint32_t)strlen(popkey),
                                            ZUI_DrawText | ZUI_Clickable);
                    pb->Size[0]   = ZPx(btn_sz);
                    pb->Size[1]   = ZPx(btn_sz);
                    pb->TextAlign = ZUITextAlign::Center;
                    float pc[4]   = { 0.5f, 0.7f, 0.9f, 0.9f };
                    pb->TextColor[0]=pc[0]; pb->TextColor[1]=pc[1];
                    pb->TextColor[2]=pc[2]; pb->TextColor[3]=pc[3];
                    ZUISignal psig = ZUISignalFromBox(ctx, pb);
                    ZUIPopBox(ctx);
                    if (psig.Flags & ZUI_SignalClicked) { tab_popped = true; }
                }

                // Close button
                ZUISpacer(ctx, 4.f);
                char xkey[64];
                snprintf(xkey, sizeof(xkey), "x##x_%llx_%u", (unsigned long long)p->DockKey, ti);
                ZUIBox* xbtn = ZUIPushBox(ctx, xkey, (uint32_t)strlen(xkey),
                                           ZUI_DrawText | ZUI_Clickable);
                xbtn->Size[0]   = ZPx(btn_sz);
                xbtn->Size[1]   = ZPx(btn_sz);
                xbtn->TextAlign = ZUITextAlign::Center;
                float xc[4] = { 0.75f, 0.40f, 0.40f, 0.90f };
                xbtn->TextColor[0]=xc[0]; xbtn->TextColor[1]=xc[1];
                xbtn->TextColor[2]=xc[2]; xbtn->TextColor[3]=xc[3];
                ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
                ZUIPopBox(ctx);
                if (xsig.Flags & ZUI_SignalClicked) { tab_closed = true; }
                ZUISpacer(ctx, 4.f);
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);
            ZUIEndColumn(ctx);

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

            // Drag-to-dock: start when held AND mouse has moved beyond threshold
            // Uses mouse delta instead of ScreenMin/ScreenMax (which are not set during build time)
            if (!tab_closed && !tab_popped && (sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float total_moved = fabsf(ctx->MousePos[0] - Drag.StartX) +
                                    fabsf(ctx->MousePos[1] - Drag.StartY);
                if (total_moved > 8.f * ctx->UIScale)
                {
                    Drag.Active    = true;
                    Drag.SrcPanel  = p;
                    Drag.SrcTabIdx = ti;
                    Drag.GhostX    = ctx->MousePos[0];
                    Drag.GhostY    = ctx->MousePos[1];
                }
            }
            // Record press position for drag threshold
            if (sig.Flags & ZUI_SignalPressed)
            {
                Drag.StartX = ctx->MousePos[0];
                Drag.StartY = ctx->MousePos[1];
            }

            ZUISpacer(ctx, 2.f);
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

        // Zone indicators
        for (auto& z : zones)
        {
            bool over = (z.zone == hovered);
            float col[4] = { 0.137f, 0.573f, 0.922f, over ? 0.90f : 0.50f };
            char zkey[40];
            snprintf(zkey, sizeof(zkey), "##dz_%d_%llx", (int)z.zone, (unsigned long long)p->DockKey);
            ZUIBox* box = ZUIPushBox(ctx, zkey, (uint32_t)strlen(zkey),
                                     ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
            box->Size[0]     = ZPx(z.x1 - z.x0);
            box->Size[1]     = ZPx(z.y1 - z.y0);
            box->FloatPos[0] = z.x0;
            box->FloatPos[1] = z.y0;
            ZUIBoxSetColorArr(box, col);
            box->BorderColor[0]=1.f; box->BorderColor[1]=1.f; box->BorderColor[2]=1.f;
            box->BorderColor[3]=over ? 0.9f : 0.4f;
            box->BorderThickness = 1.f;
            ZUIBoxSetCornerRadius(box, 4.f);
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

            // Visual 1px divider line
            bool highlight = in_rect || dragging;
            float vc[4] = { highlight ? 0.45f : 0.22f,
                            highlight ? 0.55f : 0.28f,
                            highlight ? 0.70f : 0.35f, 1.f };
            char vk[32];
            snprintf(vk, sizeof(vk), "##sdiv_%u", di);
            ZUIBox* vis = ZUIPushBox(ctx, vk, (uint32_t)strlen(vk),
                                     ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            if (!horizontal)
            {
                vis->Size[0]     = ZPx(1.f);
                vis->Size[1]     = ZPx(dy1 - dy0);
                vis->FloatPos[0] = (dx0 + dx1) * 0.5f;
                vis->FloatPos[1] = dy0;
            }
            else
            {
                vis->Size[0]     = ZPx(dx1 - dx0);
                vis->Size[1]     = ZPx(1.f);
                vis->FloatPos[0] = dx0;
                vis->FloatPos[1] = (dy0 + dy1) * 0.5f;
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
        p->Floating  = true;
        p->FloatX    = x;
        p->FloatY    = y;
        p->FloatW    = w;
        p->FloatH    = h;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p) { FocusPanel(pi); break; }
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
