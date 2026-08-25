#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstdio>
#include <cstring>

namespace ZEngine::UI
{

    // Forward decl
    static void SetTextColorOnBox(ZUIBox* box, const float c[4])
    {
        box->TextColor[0] = c[0]; box->TextColor[1] = c[1];
        box->TextColor[2] = c[2]; box->TextColor[3] = c[3];
    }
    // ---------------------------------------------------------------
    // Init / Shutdown
    // ---------------------------------------------------------------

    void ZUIPanelManager::Init(ArenaAllocator* arena)
    {
        DockTree   = ZUIDockTreeCreate(arena);
        PanelCount = 0;
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
    // BuildUI — top-level frame entry point
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildUI(ZUIContext* ctx, float menu_h, float status_h)
    {
        float sw = (float)ctx->ScreenW;
        float sh = (float)ctx->ScreenH;
        float scale = ctx->UIScale;

        float real_menu_h   = menu_h   * scale;
        float real_status_h = status_h * scale;

        // Recompute dock layout every frame (handles window resize + divider changes)
        if (DockTree)
        {
            float root_rect[4] = { 0.f, real_menu_h, sw, sh - real_status_h };
            ZUIDockLayout(DockTree, root_rect);
        }

        // --- Full-screen opaque background ---
        ZUIBox* bg = ZUIBeginColumn(ctx, "##pm_bg", ZPx(sw), ZPx(sh));
        bg->Flags  = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f; bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        bg->EdgeSoftness = 0.f;

        // --- Menu bar ---
        BuildMenuBar(ctx, sw, real_menu_h);

        // --- Each panel + hover detection for drag-to-dock ---
        // RAD: for each panel, check contains_2f32(panel_rect, mouse) during drag
        Drag.HoverNode = nullptr;
        if (DockTree)
        {
            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                float r[4];
                if (!ZUIDockRectForKey(DockTree, p->DockKey, r)) { continue; }
                BuildPanel(ctx, p, r);
                if (Drag.Active && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                    Drag.HoverNode = ZUIDockFindLeaf(DockTree, p->DockKey);
            }
        }

        // --- Status bar — full-width fixed strip at window bottom (RAD/VS Code style).
        // Floated outside the dock tree. Blue brand-color background.
        // Left: status message. Right: context info.
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

        // --- Workspace dividers (RAD-style direct mouse tracking) ---
        BuildDividers(ctx, sw, sh, real_menu_h, real_status_h);

        // --- Drag ghost — follows mouse, shows tab title ---
        if (Drag.Active)
        {
            // Update ghost position every frame
            Drag.GhostX = ctx->MousePos[0];
            Drag.GhostY = ctx->MousePos[1];

            // Cancel drag on release (without drop zone = abandon)
            if (ctx->MouseReleased[0] && Drag.DropZone == ZUIDropZone::None)
                Drag.Active = false;

            if (Drag.Active)
            {
                ZUIPanel* sp = Drag.SrcPanel;
                const char* title = (sp && Drag.SrcTabIdx < sp->ViewCount && sp->Views[Drag.SrcTabIdx])
                                   ? sp->Views[Drag.SrcTabIdx]->Title : "Tab";

                float ghost_h = kTabBarH * ctx->UIScale;
                float ghost_w = (float)(strlen(title) * 9 + 36) * ctx->UIScale; // approx

                float ghost_col[4] = { ctx->Theme.TabActiveBg[0],
                                       ctx->Theme.TabActiveBg[1],
                                       ctx->Theme.TabActiveBg[2] + 0.10f, 0.85f };
                float ghost_bdr[4] = { 0.40f, 0.60f, 0.90f, 1.f };

                ZUIBox* ghost = ZUIBeginRow(ctx, "##drag_ghost", ZPx(ghost_w), ZPx(ghost_h));
                ghost->Flags      = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                                    ZUI_FloatX   | ZUI_FloatY;
                ghost->FloatPos[0] = Drag.GhostX - ghost_w * 0.3f;
                ghost->FloatPos[1] = Drag.GhostY - ghost_h * 0.5f;
                ZUIBoxSetColorArr(ghost, ghost_col);
                ghost->BorderColor[0]=ghost_bdr[0]; ghost->BorderColor[1]=ghost_bdr[1];
                ghost->BorderColor[2]=ghost_bdr[2]; ghost->BorderColor[3]=ghost_bdr[3];
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
            // List all registered panels + toggle visibility
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->ViewCount == 0) continue;
                const char* name = p->Views[0] ? p->Views[0]->Title : "Panel";
                char buf[80];
                snprintf(buf, sizeof(buf), "%s##pm_panel_%u", name, i);
                if (ZUIMenuItem(ctx, buf)) {} // toggle
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
    // BuildPanel — renders one panel: tab bar + active view content
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildPanel(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        if (!p || p->ViewCount == 0) { return; }

        float scale = ctx->UIScale;
        float tab_h = kTabBarH * scale;

        char panel_key[32];
        snprintf(panel_key, sizeof(panel_key), "##panel_%llx", (unsigned long long)p->DockKey);

        // Panel border box
        ZUIBox* panel = ZUIBeginColumn(ctx, panel_key, ZPx(rect[2]-rect[0]), ZPx(rect[3]-rect[1]));
        panel->Flags  = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder |
                        ZUI_FloatX   | ZUI_FloatY;
        panel->FloatPos[0]  = rect[0];
        panel->FloatPos[1]  = rect[1];
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->BorderColor[0] = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1] = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2] = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3] = ctx->Theme.PanelBorder[3];
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // Tab bar
        float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + tab_h };
        BuildTabBar(ctx, p, tab_rect);

        // Content area
        float content_rect[4] = { rect[0], rect[1] + tab_h, rect[2], rect[3] };
        ZUIPanelView* active_view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;

        if (active_view)
        {
            ZUIBox* content = ZUIBeginColumn(ctx, "##content", ZFill(), ZFill());
            content->Flags  = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            active_view->BuildContent(ctx, content_rect);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx);

        // Drop zones (show during active drag)
        if (Drag.Active && Drag.HoverNode)
        {
            // Check if this panel's dock key matches the hover node
            if (Drag.HoverNode->ContentKey == p->DockKey)
                BuildDropZones(ctx, p, rect);
        }
    }

    // ---------------------------------------------------------------
    // BuildTabBar — tab row at top of panel
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildTabBar(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float scale = ctx->UIScale;
        float tab_h = rect[3] - rect[1];

        char bar_key[40];
        snprintf(bar_key, sizeof(bar_key), "##tabbar_%llx", (unsigned long long)p->DockKey);

        // Tab bar: TitleBarBg + bottom separator
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

        // RAD Debugger tab structure (raddbg_core.c ~8903):
        //   tab_column (Y-layout, ZFit width × full tab_h)
        //     spacer(1px)                    ← subtle raise from bar top
        //     tab_box (X-layout, ZFill×rest) ← DrawBackground|DrawBorder|(DrawDropShadow if active)
        //       spacer(0.5em) | label | [close_btn if active+focused]
        for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
        {
            ZUIPanelView* view = p->Views[ti];
            if (!view) { continue; }
            bool is_active = (ti == p->ActiveTab);

            // --- Outer column (Y-axis, ZFit width) ---
            char col_key[64];
            snprintf(col_key, sizeof(col_key), "##tcol_%llx_%u",
                     (unsigned long long)p->DockKey, ti);
            ZUIBox* col = ZUIBeginColumn(ctx, col_key, ZFit(), ZPx(tab_h));
            col->Flags = col->Flags | ZUI_Clickable; // col captures drag
            col->EdgeSoftness = 0.f;

            // 1px spacer at top (RAD line 8927: ui_spacer(ui_px(1.f, 1.f)))
            ZUISpacer(ctx, 1.f);

            // --- Tab box (X-axis, fills remaining height = tab_h - 1) ---
            char tab_key[64];
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%u",
                     (unsigned long long)p->DockKey, ti);

            ZUIBoxFlags tab_flags = ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
            ZUIBox* tab = ZUIBeginRow(ctx, tab_key, ZFill(), ZFill());
            tab->Flags = tab->Flags | tab_flags;
            tab->EdgeSoftness = 0.5f;
            ZUIBoxSetCornerRadius(tab, 3.f);

            // Colors: RAD default dark theme (#333333 active, transparent inactive)
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

            // Tab contents: spacer | label | [close if active] (RAD line 8941-8966)
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, view->Title,
                     is_active ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

            // Close button — only on focused+active tab (RAD line 8948)
            bool tab_closed = false;
            if (is_active)
            {
                ZUISpacer(ctx, 6.f);
                char xkey[64];
                snprintf(xkey, sizeof(xkey), "×##x_%llx_%u",
                         (unsigned long long)p->DockKey, ti);
                float close_sz = tab_h * 0.55f;
                ZUIBox* xbtn = ZUIPushBox(ctx, xkey, (uint32_t)strlen(xkey),
                                           ZUI_DrawText | ZUI_Clickable);
                xbtn->Size[0] = ZPx(close_sz); xbtn->Size[1] = ZPx(close_sz);
                xbtn->TextAlign = ZUITextAlign::Center;
                float xc[4] = {0.75f, 0.40f, 0.40f, 0.90f};
                SetTextColorOnBox(xbtn, xc);
                ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
                ZUIPopBox(ctx);
                if (xsig.Flags & ZUI_SignalClicked) { tab_closed = true; }
                ZUISpacer(ctx, 4.f);
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);  // close tab_box
            ZUIEndColumn(ctx); // close col

            // Tab close — remove view, adjust active tab
            if (tab_closed && p->ViewCount > 1)
            {
                for (uint32_t j = ti; j + 1 < p->ViewCount; ++j)
                    p->Views[j] = p->Views[j + 1];
                --p->ViewCount;
                if (p->ActiveTab >= p->ViewCount) p->ActiveTab = p->ViewCount - 1;
                break; // iterator invalidated — tab_h bar rebuilt next frame
            }

            if (!tab_closed && (sig.Flags & ZUI_SignalClicked)) { p->ActiveTab = ti; }

            // Drag-to-dock: start when tab is held + mouse moves
            if (!tab_closed && (sig.Flags & ZUI_SignalHeld) &&
                (sig.DragDelta[0] != 0.f || sig.DragDelta[1] != 0.f) &&
                !Drag.Active)
            {
                Drag.Active    = true;
                Drag.SrcPanel  = p;
                Drag.SrcTabIdx = ti;
                Drag.GhostX    = ctx->MousePos[0];
                Drag.GhostY    = ctx->MousePos[1];
            }

            // Tiny gap between tabs
            ZUISpacer(ctx, 2.f);
        }

        ZUIEndRow(ctx);
    }

    // ---------------------------------------------------------------
    // BuildDropZones — overlaid when dragging
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float mx = ctx->MousePos[0], my = ctx->MousePos[1];
        float w = rect[2] - rect[0], h = rect[3] - rect[1];
        float cx = (rect[0] + rect[2]) * 0.5f;
        float cy = (rect[1] + rect[3]) * 0.5f;
        float zw = 48.f * ctx->UIScale, zh = 48.f * ctx->UIScale;

        struct { ZUIDropZone zone; float x, y; } zones[] = {
            { ZUIDropZone::Center, cx - zw*0.5f, cy - zh*0.5f },
            { ZUIDropZone::Left,   rect[0] + 4.f, cy - zh*0.5f },
            { ZUIDropZone::Right,  rect[2] - zw - 4.f, cy - zh*0.5f },
            { ZUIDropZone::Top,    cx - zw*0.5f, rect[1] + 4.f },
            { ZUIDropZone::Bottom, cx - zw*0.5f, rect[3] - zh - 4.f },
        };

        ZUIDropZone hovered = ZUIDropZone::None;
        for (auto& z : zones)
        {
            bool over = (mx >= z.x && mx <= z.x+zw && my >= z.y && my <= z.y+zh);
            if (over) hovered = z.zone;
        }
        Drag.DropZone = hovered;

        // Render drop zone indicators
        for (auto& z : zones)
        {
            bool over = (z.zone == hovered);
            float col[4] = { 0.26f, 0.59f, 0.98f, over ? 0.75f : 0.35f };
            char zkey[32];
            snprintf(zkey, sizeof(zkey), "##dz_%d_%llx", (int)z.zone,
                     (unsigned long long)p->DockKey);
            ZUIBox* box = ZUIPushBox(ctx, zkey, (uint32_t)strlen(zkey),
                                     ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            box->Size[0]     = ZPx(zw);
            box->Size[1]     = ZPx(zh);
            box->FloatPos[0] = z.x;
            box->FloatPos[1] = z.y;
            ZUIBoxSetColorArr(box, col);
            ZUIBoxSetCornerRadius(box, 6.f);
            box->EdgeSoftness = 1.f;
            ZUIPopBox(ctx);
        }

        // Commit drop on mouse release
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
    // BuildDividers — RAD-style workspace resize
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDividers(ZUIContext* ctx, float sw, float sh,
                                         float menu_h, float status_h)
    {
        if (!DockTree) { return; }
        static constexpr float kGrabW = 6.f;

        for (int di = 0; di < 4; ++di)
        {
            Divider& div = m_dividers[di];
            float lr[4] = {};
            if (!ZUIDockRectForKey(DockTree, ZUIDockHashName(div.leaf_name), lr))
                continue;

            float dx0, dy0, dx1, dy1;
            if (!div.horizontal)
            {
                float ex = div.use_near ? lr[0] : lr[2];
                dx0 = ex - kGrabW*0.5f; dy0 = lr[1];
                dx1 = ex + kGrabW*0.5f; dy1 = lr[3];
            }
            else
            {
                float ey = div.use_near ? lr[1] : lr[3];
                dx0 = lr[0];            dy0 = ey - kGrabW*0.5f;
                dx1 = lr[2];            dy1 = ey + kGrabW*0.5f;
            }

            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            bool  in_rect = (mx >= dx0 && mx <= dx1 && my >= dy0 && my <= dy1);

            if (ctx->MousePressed[0] && in_rect)  div.dragging = true;
            if (ctx->MouseReleased[0])             div.dragging = false;

            if (div.dragging && ctx->MouseDown[0])
            {
                float delta = div.horizontal
                    ? (ctx->MousePos[1] - ctx->PrevMousePos[1])
                    : (ctx->MousePos[0] - ctx->PrevMousePos[0]);
                if (delta != 0.f)
                {
                    ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, ZUIDockHashName(div.leaf_name));
                    if (leaf) ZUIDockResize(DockTree, leaf, delta);
                }
            }

            // Visual 1px divider line
            bool highlight = in_rect || div.dragging;
            float vc[4] = { highlight ? 0.45f : 0.22f,
                            highlight ? 0.55f : 0.28f,
                            highlight ? 0.70f : 0.35f, 1.f };
            char vk[32]; snprintf(vk, sizeof(vk), "##div_%d", di);
            ZUIBox* vis = ZUIPushBox(ctx, vk, (uint32_t)strlen(vk),
                                     ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            if (!div.horizontal) {
                vis->Size[0] = ZPx(1.f);
                vis->Size[1] = ZPx(dy1 - dy0);
                vis->FloatPos[0] = (dx0+dx1)*0.5f;
                vis->FloatPos[1] = dy0;
            } else {
                vis->Size[0] = ZPx(dx1 - dx0);
                vis->Size[1] = ZPx(1.f);
                vis->FloatPos[0] = dx0;
                vis->FloatPos[1] = (dy0+dy1)*0.5f;
            }
            vis->EdgeSoftness = 0.f;
            ZUIBoxSetColorArr(vis, vc);
            ZUIPopBox(ctx);
        }
    }

    // ---------------------------------------------------------------
    // CommitDrop — modify dock tree when a tab is dropped on a zone
    // ---------------------------------------------------------------

    void ZUIPanelManager::CommitDrop(ZUIPanel* src, uint32_t tab_idx,
                                      ZUIDockNode* dst, ZUIDropZone zone)
    {
        if (!src || tab_idx >= src->ViewCount || !dst) { return; }
        ZUIPanelView* view = src->Views[tab_idx];

        if (zone == ZUIDropZone::Center)
        {
            // Add tab to destination panel
            ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
            if (dst_panel && dst_panel != src)
            {
                AddView(dst_panel, view);
                // Remove from source
                for (uint32_t i = tab_idx; i + 1 < src->ViewCount; ++i)
                    src->Views[i] = src->Views[i+1];
                --src->ViewCount;
                if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0)
                    src->ActiveTab = src->ViewCount - 1;
            }
        }
        else
        {
            // Split destination node and create a new panel
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

            // Register new panel
            ZUIPanel* new_panel = AddPanel(new_key);
            if (new_panel)
            {
                AddView(new_panel, view);
                // Remove from source
                for (uint32_t i = tab_idx; i+1 < src->ViewCount; ++i)
                    src->Views[i] = src->Views[i+1];
                --src->ViewCount;
                if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0)
                    src->ActiveTab = src->ViewCount - 1;
            }
        }
    }

} // namespace ZEngine::UI
