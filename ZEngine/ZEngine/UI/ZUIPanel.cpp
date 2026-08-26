#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace ZEngine::UI
{

    // ---------------------------------------------------------------
    // Init / Registration
    // ---------------------------------------------------------------

    void ZUIPanelManager::Init(ArenaAllocator* arena)
    {
        DockTree              = ZUIDockTreeCreate(arena);
        PanelCount            = 0;
        m_split_divider_count = 0;
    }

    void ZUIPanelManager::Shutdown() {}

    void ZUIPanelManager::SetCentralPanel(uint64_t dock_key)
    {
        CentralPanelKey = dock_key;
        // Mark the corresponding leaf as central so it can be queried
        if (DockTree)
        {
            ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, dock_key);
            if (leaf) { leaf->IsCentral = true; }
        }
    }

    void ZUIPanelManager::SetLayoutPath(const char* path)
    {
        snprintf(LayoutPath, sizeof(LayoutPath), "%s", path ? path : "");
    }

    ZUIPanel* ZUIPanelManager::AddPanel(uint64_t dock_key)
    {
        if (PanelCount >= kMaxPanels) { return nullptr; }
        ZUIPanel* p  = &Panels[PanelCount++];
        p->DockKey   = dock_key;
        p->ViewCount = 0;
        p->ActiveTab = 0;
        p->Hidden    = false;
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

    static ZUIDockNode* FindLargestLeaf(ZUIDockNode* node)
    {
        if (!node) { return nullptr; }
        ZUIDockNode* best      = nullptr;
        float        best_area = -1.f;
        ZUIDockNode* stack[64]; int top = 0;
        stack[top++] = node;
        while (top > 0)
        {
            ZUIDockNode* n = stack[--top];
            if (n->ContentKey != 0)
            {
                float area = (n->RectMax[0]-n->RectMin[0]) * (n->RectMax[1]-n->RectMin[1]);
                if (area > best_area) { best_area = area; best = n; }
            }
            for (ZUIDockNode* c = n->First; c; c = c->Next)
                if (top < 64) stack[top++] = c;
        }
        return best;
    }

    void ZUIPanelManager::FocusPanel(uint32_t idx)
    {
        FocusedPanelIdx = idx;
    }

    // ---------------------------------------------------------------
    // SplitDivider state
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
            m_split_dividers[m_split_divider_count++] = { node, v };
    }

    void ZUIPanelManager::SyncSplitDividers()
    {
        if (!DockTree || !DockTree->Root) return;
        ZUIDockNode* stack[64];
        int top = 0;
        stack[top++] = DockTree->Root;
        ZUIDockNode* seen[kMaxSplitDividers];
        uint32_t seen_count = 0;
        while (top > 0)
        {
            ZUIDockNode* node = stack[--top];
            if (!node) { continue; }
            if (node->ContentKey == 0 && node->First)
            {
                seen[seen_count++] = node;
                bool found = false;
                for (uint32_t i = 0; i < m_split_divider_count; ++i)
                    if (m_split_dividers[i].Node == node) { found = true; break; }
                if (!found && m_split_divider_count < kMaxSplitDividers)
                    m_split_dividers[m_split_divider_count++] = { node, false };
            }
            for (ZUIDockNode* c = node->First; c; c = c->Next)
                if (top < 64) stack[top++] = c;
        }
        for (uint32_t i = 0; i < m_split_divider_count; )
        {
            bool alive = false;
            for (uint32_t j = 0; j < seen_count; ++j)
                if (seen[j] == m_split_dividers[i].Node) { alive = true; break; }
            if (!alive) m_split_dividers[i] = m_split_dividers[--m_split_divider_count];
            else ++i;
        }
    }

    // ---------------------------------------------------------------
    // BuildUI — top-level per-frame entry
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildUI(ZUIContext* ctx, float menu_h, float status_h)
    {
        float sw = (float)ctx->ScreenW;
        float sh = (float)ctx->ScreenH;

        // Flush pending layout save (triggered by drag/close events last frame)
        if (LayoutDirty && LayoutPath[0])
        {
            ZUIDockSave(this, LayoutPath);
            LayoutDirty = false;
        }

        if (DockTree)
        {
            float root_rect[4] = { 0.f, menu_h, sw, sh - status_h };
            ZUIDockLayout(DockTree, root_rect);
            SyncSplitDividers();
        }

        ZUIBox* bg = ZUIBeginColumn(ctx, "##pm_bg", ZPx(sw), ZPx(sh));
        bg->Flags  = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f; bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        bg->EdgeSoftness = 0.f;

        BuildMenuBar(ctx, sw, menu_h);

        Drag.HoverNode = nullptr;
        if (DockTree)
        {
            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->Hidden) { continue; }
                float r[4];
                if (!ZUIDockRectForKey(DockTree, p->DockKey, r)) { continue; }

                if (Drag.Active && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                {
                    if (!Drag.SrcPanel || Drag.SrcPanel != p)
                        Drag.HoverNode = ZUIDockFindLeaf(DockTree, p->DockKey);
                }

                BuildDockedPanel(ctx, p, r);

                if (ctx->MousePressed[0] && !Drag.Active &&
                    mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                    FocusPanel(i);
            }
        }

        // Status bar
        if (status_h > 0.f)
        {
            ZUIBox* sbar = ZUIBeginRow(ctx, "##pm_sbar", ZPx(sw), ZPx(status_h));
            sbar->Flags = sbar->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
            sbar->FloatPos[0] = 0.f;
            sbar->FloatPos[1] = sh - status_h;
            ZUIBoxSetColorArr(sbar, ctx->Theme.StatusBarBg);
            sbar->EdgeSoftness = 0.f;
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, "ZEngine Editor", ctx->Theme.TextDefault);
            { char fk[]="##sbf"; ZUIBox* sf=ZUIPushBox(ctx,fk,5,ZUI_None);
              sf->Size[0]=ZFill(); sf->Size[1]=ZPx(status_h); ZUIPopBox(ctx); }
            {
                char buf[48];
                float fps = (ctx->DeltaTime > 0.0005f && ctx->DeltaTime < 1.f)
                          ? (1.f / ctx->DeltaTime) : 0.f;
                if (fps > 0.f) snprintf(buf,sizeof(buf),"%.0f fps  |  UIScale %.1f",(double)fps,(double)ctx->UIScale);
                else           snprintf(buf,sizeof(buf),"UIScale %.1f",(double)ctx->UIScale);
                ZUILabel(ctx, buf, ctx->Theme.TextDefault);
                ZUISpacer(ctx, 12.f);
            }
            ZUIEndRow(ctx);
        }

        BuildDividers(ctx);

        // Drag ghost
        if (Drag.Active)
        {
            Drag.GhostX = ctx->MousePos[0];
            Drag.GhostY = ctx->MousePos[1];

            if (ctx->MouseReleased[0] && Drag.DropZone == ZUIDropZone::None)
            {
                Drag.Active = false;
                Drag.SrcPanel = nullptr;
            }

            if (Drag.Active)
            {
                ZUIPanel* sp = Drag.SrcPanel;
                const char* title = "Panel";
                if (Drag.SrcTabIdx == kWholePanel)
                { if (sp && sp->ViewCount > 0 && sp->Views[0]) title = sp->Views[0]->Title; }
                else if (sp && Drag.SrcTabIdx < sp->ViewCount && sp->Views[Drag.SrcTabIdx])
                { title = sp->Views[Drag.SrcTabIdx]->Title; }

                float gh = kTabBarH, gw = (float)(strlen(title) * 8 + 48);
                ZUIBox* ghost = ZUIBeginRow(ctx, "##drag_ghost", ZPx(gw), ZPx(gh));
                ghost->Flags = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
                ghost->FloatPos[0] = Drag.GhostX - gw * 0.3f;
                ghost->FloatPos[1] = Drag.GhostY - gh * 0.5f;
                ZUIBoxSetColorArr(ghost, ctx->Theme.TabActiveBg);
                ghost->BorderColor[0]=ctx->Theme.TabActiveBorder[0];
                ghost->BorderColor[1]=ctx->Theme.TabActiveBorder[1];
                ghost->BorderColor[2]=ctx->Theme.TabActiveBorder[2];
                ghost->BorderColor[3]=0.90f; ghost->BorderThickness=1.f;
                ZUIBoxSetCornerRadius(ghost, 3.f); ghost->EdgeSoftness=0.f;
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, title, ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
        }

        ZUIEndColumn(ctx);
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
        bar->BorderColor[0]=ctx->Theme.Separator[0]; bar->BorderColor[1]=ctx->Theme.Separator[1];
        bar->BorderColor[2]=ctx->Theme.Separator[2]; bar->BorderColor[3]=ctx->Theme.Separator[3];
        bar->BorderThickness=1.f; bar->Flags=bar->Flags|ZUI_DrawBorder;

        ZUISpacer(ctx, 10.f);
        if (ZUIBeginMenu(ctx, "File")) {
            ZUIMenuItem(ctx, "New Scene"); ZUIMenuItem(ctx, "Open Scene...");
            ZUIMenuItem(ctx, "Save Scene"); ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Quit"); ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Edit")) {
            ZUIMenuItem(ctx, "Undo"); ZUIMenuItem(ctx, "Redo");
            ZUISeparator(ctx); ZUIMenuItem(ctx, "Preferences..."); ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Window")) {
            ZUIMenuItem(ctx, "Reset Layout"); ZUISeparator(ctx);
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->ViewCount == 0) { continue; }
                const char* name = p->Views[0] ? p->Views[0]->Title : "Panel";
                char buf[80]; snprintf(buf, sizeof(buf), "%s##wm_%u", name, i);
                if (ZUIMenuItem(ctx, buf) && p->Hidden)
                {
                    p->Hidden = false;
                    if (DockTree)
                    {
                        ZUIDockNode* target = FindLargestLeaf(DockTree->Root);
                        if (target)
                            ZUIDockSplitH(DockTree, target, 0.75f,
                                          target->ContentKey, p->DockKey);
                    }
                    LayoutDirty = true;
                }
            }
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Help")) {
            ZUIMenuItem(ctx, "About ZEngine"); ZUIEndMenu(ctx);
        }
        { char fk[]="##mb_fill"; ZUIBox* f=ZUIPushBox(ctx,fk,8,ZUI_None);
          f->Size[0]=ZFill(); f->Size[1]=ZPx(mh); ZUIPopBox(ctx); }
        ZUILabel(ctx, "ZEngine", ctx->Theme.TextDim);
        ZUISpacer(ctx, 12.f);
        ZUIEndRow(ctx);
    }

    // ---------------------------------------------------------------
    // BuildDockedPanel
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDockedPanel(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        if (!p || p->ViewCount == 0) { return; }

        // Central panel (viewport): no tab bar, no border, no background — pure passthrough
        if (p->DockKey == CentralPanelKey)
        {
            ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
            if (view) { view->BuildContent(ctx, rect); }
            return;
        }

        bool is_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx) { is_focused = true; break; }

        char panel_key[32];
        snprintf(panel_key, sizeof(panel_key), "##panel_%llx", (unsigned long long)p->DockKey);

        ZUIBox* panel = ZUIBeginColumn(ctx, panel_key,
                                       ZPx(rect[2]-rect[0]), ZPx(rect[3]-rect[1]));
        panel->Flags = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = rect[0]; panel->FloatPos[1] = rect[1];
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->BorderColor[0]=ctx->Theme.PanelBorder[0]; panel->BorderColor[1]=ctx->Theme.PanelBorder[1];
        panel->BorderColor[2]=ctx->Theme.PanelBorder[2]; panel->BorderColor[3]=ctx->Theme.PanelBorder[3];
        panel->BorderThickness=1.f; panel->EdgeSoftness=0.f;

        bool  show_tabs = (p->ViewCount > 1);
        float header_h  = kTabBarH + 2.f; // 28px

        if (show_tabs)
        {
            float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + header_h };
            BuildTabBar(ctx, p, tab_rect);
        }
        else
        {
            // VS Code single-view title bar
            const char* view_title = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "Panel";
            float btn_h = header_h * 0.60f;

            char hk[48]; snprintf(hk, sizeof(hk), "##tbar_%llx", (unsigned long long)p->DockKey);
            ZUIBox* strip = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
            strip->Flags = strip->Flags | ZUI_DrawBackground | ZUI_Clickable;
            ZUIBoxSetColorArr(strip, ctx->Theme.MenuBarBg);
            strip->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 10.f);
            // Icon dot
            {
                const float* ic = (p->Views[0] && p->Views[0]->TabColor[3] > 0.01f)
                                 ? p->Views[0]->TabColor : ctx->Theme.PanelFocusBorder;
                char ik[48]; snprintf(ik, sizeof(ik), "##tic_%llx", (unsigned long long)p->DockKey);
                ZUIBox* icon = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                icon->Size[0]=ZPx(8.f); icon->Size[1]=ZPx(8.f);
                ZUIBoxSetColorArr(icon, ic); ZUIBoxSetCornerRadius(icon, 4.f); icon->EdgeSoftness=0.5f;
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 7.f);
            ZUILabel(ctx, view_title, ctx->Theme.TextDefault);

            // Fill
            { char fk[48]; snprintf(fk,sizeof(fk),"##tf_%llx",(unsigned long long)p->DockKey);
              ZUIBox* f=ZUIPushBox(ctx,fk,(uint32_t)strlen(fk),ZUI_None);
              f->Size[0]=ZFill(); f->Size[1]=ZPx(header_h); ZUIPopBox(ctx); }

            // × close (hover-only)
            bool should_close = false;
            bool ph = (ctx->MousePos[0] >= rect[0] && ctx->MousePos[0] <= rect[2] &&
                       ctx->MousePos[1] >= rect[1] && ctx->MousePos[1] <= rect[1]+header_h);
            if (ph)
            {
                char xk[56]; snprintf(xk,sizeof(xk),"x##tx_%llx",(unsigned long long)p->DockKey);
                ZUIBox* xb = ZUIPushBox(ctx, xk, (uint32_t)strlen(xk), ZUI_DrawText | ZUI_Clickable);
                xb->Size[0]=ZPx(btn_h); xb->Size[1]=ZPx(btn_h); xb->TextAlign=ZUITextAlign::Center;
                bool xh=(ctx->HotKey==xb->Key);
                xb->TextColor[0]=xh?1.f:0.55f; xb->TextColor[1]=xh?0.4f:0.55f;
                xb->TextColor[2]=xh?0.4f:0.55f; xb->TextColor[3]=1.f;
                ZUISignal xs = ZUISignalFromBox(ctx, xb); ZUIPopBox(ctx);
                if (xs.Flags & ZUI_SignalClicked) should_close = true;
            }
            ZUISpacer(ctx, 6.f);

            ZUISignal strip_sig = ZUISignalFromBox(ctx, strip);
            ZUIEndRow(ctx);

            // Record press start
            if (strip_sig.Flags & ZUI_SignalPressed)
            { Drag.StartX = ctx->MousePos[0]; Drag.StartY = ctx->MousePos[1]; }

            // Drag > 8px → start whole-panel drag
            if (!should_close && (strip_sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float dx = fabsf(ctx->MousePos[0]-Drag.StartX);
                float dy = fabsf(ctx->MousePos[1]-Drag.StartY);
                if (dx + dy > 8.f)
                {
                    Drag.Active    = true;
                    Drag.SrcPanel  = p;
                    Drag.SrcTabIdx = kWholePanel;
                    Drag.GhostX    = ctx->MousePos[0];
                    Drag.GhostY    = ctx->MousePos[1];
                }
            }

            if (should_close)
            {
                p->Hidden = true;
                ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, p->DockKey);
                if (leaf) ZUIDockCollapseLeaf(DockTree, leaf);
                LayoutDirty = true;
            }
        }

        // Content
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view)
        {
            ZUIBox* content = ZUIBeginColumn(ctx, "##dc", ZFill(), ZFill());
            content->Flags  = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            float cr[4] = { rect[0], rect[1]+header_h, rect[2], rect[3] };
            view->BuildContent(ctx, cr);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx);

        // VS Code focus: 3px teal left strip
        if (is_focused)
        {
            char fk[48]; snprintf(fk,sizeof(fk),"##pfocus_%llx",(unsigned long long)p->DockKey);
            ZUIBox* fb = ZUIPushBox(ctx,fk,(uint32_t)strlen(fk),ZUI_DrawBackground|ZUI_FloatX|ZUI_FloatY);
            fb->Size[0]=ZPx(3.f); fb->Size[1]=ZPx(rect[3]-rect[1]);
            fb->FloatPos[0]=rect[0]; fb->FloatPos[1]=rect[1];
            ZUIBoxSetColorArr(fb, ctx->Theme.PanelFocusBorder); fb->EdgeSoftness=0.f;
            ZUIPopBox(ctx);
        }

        // Drop zones
        if (Drag.Active && Drag.HoverNode && Drag.HoverNode->ContentKey == p->DockKey)
            BuildDropZones(ctx, p, rect);
    }

    // ---------------------------------------------------------------
    // BuildTabBar
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildTabBar(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float tab_h = rect[3] - rect[1];
        char bar_key[40];
        snprintf(bar_key, sizeof(bar_key), "##tabbar_%llx", (unsigned long long)p->DockKey);

        bool panel_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx) { panel_focused = true; break; }
        const float* bar_bg = panel_focused ? ctx->Theme.TitleBgActive : ctx->Theme.TitleBarBg;

        ZUIBox* bar = ZUIBeginRow(ctx, bar_key, ZFill(), ZPx(tab_h));
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

            char col_key[64];
            snprintf(col_key, sizeof(col_key), "##tcol_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* col = ZUIBeginColumn(ctx, col_key, ZFit(), ZPx(tab_h));
            col->Flags = col->Flags | ZUI_Clickable;
            col->EdgeSoftness = 0.f;

            // 2px accent-line box (active = colored, inactive = transparent)
            {
                char ak[72]; snprintf(ak,sizeof(ak),"##taccent_%llx_%u",(unsigned long long)p->DockKey,ti);
                ZUIBox* accent = ZUIPushBox(ctx,ak,(uint32_t)strlen(ak),ZUI_DrawBackground);
                accent->Size[0]=ZFill(); accent->Size[1]=ZPx(2.f);
                if (is_active)
                { const float* acc = has_color ? view->TabColor : ctx->Theme.TabActiveBorder;
                  ZUIBoxSetColorArr(accent, acc); }
                else { ZUIBoxSetColor(accent, 0.f,0.f,0.f,0.f); }
                accent->EdgeSoftness=0.f; ZUIPopBox(ctx);
            }

            char tab_key[64];
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* tab = ZUIBeginRow(ctx, tab_key, ZFit(), ZFill());
            tab->Flags = tab->Flags | ZUI_DrawBackground | ZUI_Clickable;
            tab->EdgeSoftness=0.f; ZUIBoxSetTopRadius(tab, 6.f);

            if (is_active)
                ZUIBoxSetColorArr(tab, ctx->Theme.PanelBg);
            else
            {
                float inactive[4]={bar_bg[0]+0.07f,bar_bg[1]+0.04f,bar_bg[2]+0.02f,0.90f};
                float hover_col[4]={bar_bg[0]+0.14f,bar_bg[1]+0.08f,bar_bg[2]+0.04f,1.00f};
                auto* st = ZUIStateGetOrInsert(&ctx->StateStore, tab->Key);
                float ht = st ? st->HotT : 0.f;
                float blended[4];
                for (int ch=0;ch<4;++ch) blended[ch]=inactive[ch]+(hover_col[ch]-inactive[ch])*ht;
                ZUIBoxSetColorArr(tab, blended);
            }

            ZUISpacer(ctx, 10.f);
            // Label ZFill height → renderer centers text vertically
            {
                uint32_t tlen = (uint32_t)strlen(view->Title);
                ZUIBox* lbl = ZUIPushBox(ctx, view->Title, tlen, ZUI_DrawText);
                lbl->Size[0]=ZText(); lbl->Size[1]=ZFill();
                lbl->TextColor[0]=is_active?ctx->Theme.TextDefault[0]:ctx->Theme.TextDim[0];
                lbl->TextColor[1]=is_active?ctx->Theme.TextDefault[1]:ctx->Theme.TextDim[1];
                lbl->TextColor[2]=is_active?ctx->Theme.TextDefault[2]:ctx->Theme.TextDim[2];
                lbl->TextColor[3]=1.f; ZUIPopBox(ctx);
            }

            bool tab_closed = false;
            {
                float btn_sz = 18.f;
                bool tab_hovered = (ctx->HotKey == tab->Key);
                bool show_close  = is_active || tab_hovered;
                if (show_close)
                {
                    ZUISpacer(ctx, 4.f);
                    char xkey[64];
                    snprintf(xkey,sizeof(xkey),"x##x_%llx_%u",(unsigned long long)p->DockKey,ti);
                    ZUIBox* xbtn = ZUIPushBox(ctx,xkey,(uint32_t)strlen(xkey),ZUI_DrawText|ZUI_Clickable);
                    xbtn->Size[0]=ZPx(btn_sz); xbtn->Size[1]=ZPx(btn_sz);
                    xbtn->TextAlign=ZUITextAlign::Center;
                    bool xh=(ctx->HotKey==xbtn->Key);
                    if (xh){xbtn->TextColor[0]=1.f;xbtn->TextColor[1]=0.35f;xbtn->TextColor[2]=0.35f;xbtn->TextColor[3]=1.f;}
                    else{float a=is_active?0.55f:0.40f;xbtn->TextColor[0]=a;xbtn->TextColor[1]=a;xbtn->TextColor[2]=a;xbtn->TextColor[3]=1.f;}
                    ZUISignal xsig = ZUISignalFromBox(ctx, xbtn); ZUIPopBox(ctx);
                    if (xsig.Flags & ZUI_SignalClicked) { tab_closed = true; }
                    ZUISpacer(ctx, 4.f);
                }
                else { ZUISpacer(ctx, 4.f); }
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);
            ZUIEndColumn(ctx);

            if (tab_closed && p->ViewCount > 1)
            {
                for (uint32_t j = ti; j+1 < p->ViewCount; ++j) p->Views[j] = p->Views[j+1];
                --p->ViewCount;
                if (p->ActiveTab >= p->ViewCount) p->ActiveTab = p->ViewCount-1;
                LayoutDirty = true;
                break;
            }

            if (!tab_closed && (sig.Flags & ZUI_SignalClicked))
            {
                if (p->ActiveTab != ti) { LayoutDirty = true; }
                p->ActiveTab = ti;
                for (uint32_t pi = 0; pi < PanelCount; ++pi)
                    if (&Panels[pi] == p) { FocusPanel(pi); break; }
            }

            if (sig.Flags & ZUI_SignalPressed)
            { Drag.StartX=ctx->MousePos[0]; Drag.StartY=ctx->MousePos[1];
              p->ReorderActive=false; p->ReorderAccumX=0.f; }

            if (!tab_closed && (sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float dx = fabsf(ctx->MousePos[0]-Drag.StartX);
                float dy = fabsf(ctx->MousePos[1]-Drag.StartY);
                float total = dx+dy;
                if (total > 5.f)
                {
                    bool mostly_horiz = (dx > dy*1.5f);
                    if (mostly_horiz && p->ViewCount > 1)
                    {
                        if (!p->ReorderActive) { p->ReorderActive=true; p->ReorderTabIdx=ti; p->ReorderAccumX=0.f; }
                        if (p->ReorderActive && p->ReorderTabIdx == ti)
                        {
                            p->ReorderAccumX += sig.DragDelta[0];
                            float tab_slot = (rect[2]-rect[0]) / (float)p->ViewCount;
                            if (tab_slot < 40.f) tab_slot = 40.f;
                            float threshold = tab_slot * 0.5f;
                            if (p->ReorderAccumX > threshold && ti+1 < p->ViewCount)
                            {
                                ZUIPanelView* tmp=p->Views[ti]; p->Views[ti]=p->Views[ti+1]; p->Views[ti+1]=tmp;
                                if (p->ActiveTab==ti) p->ActiveTab=ti+1;
                                else if (p->ActiveTab==ti+1) p->ActiveTab=ti;
                                p->ReorderTabIdx=ti+1; p->ReorderAccumX-=threshold; break;
                            }
                            else if (p->ReorderAccumX < -threshold && ti > 0)
                            {
                                ZUIPanelView* tmp=p->Views[ti]; p->Views[ti]=p->Views[ti-1]; p->Views[ti-1]=tmp;
                                if (p->ActiveTab==ti) p->ActiveTab=ti-1;
                                else if (p->ActiveTab==ti-1) p->ActiveTab=ti;
                                p->ReorderTabIdx=ti-1; p->ReorderAccumX+=threshold; break;
                            }
                        }
                    }
                    else if (!mostly_horiz || dy > 12.f)
                    {
                        p->ReorderActive=false;
                        Drag.Active=true; Drag.SrcPanel=p; Drag.SrcTabIdx=ti;
                        Drag.GhostX=ctx->MousePos[0]; Drag.GhostY=ctx->MousePos[1];
                    }
                }
            }

            if (ctx->MouseReleased[0] && p->ReorderActive)
            { p->ReorderActive=false; p->ReorderAccumX=0.f; }

            ZUISpacer(ctx, 2.f);
        }

        // Bar signal — drag empty space = whole-panel drag
        ZUISignal bar_sig = ZUISignalFromBox(ctx, bar);
        if (bar_sig.Flags & ZUI_SignalPressed)
        { Drag.StartX=ctx->MousePos[0]; Drag.StartY=ctx->MousePos[1]; }
        if ((bar_sig.Flags & ZUI_SignalHeld) && !Drag.Active)
        {
            float dx=fabsf(ctx->MousePos[0]-Drag.StartX), dy=fabsf(ctx->MousePos[1]-Drag.StartY);
            if (dx+dy > 8.f)
            {
                float panel_r[4]={};
                if (!ZUIDockRectForKey(DockTree, p->DockKey, panel_r))
                { panel_r[0]=rect[0];panel_r[1]=rect[1];panel_r[2]=rect[2];panel_r[3]=rect[3]+200.f; }
                Drag.Active=true; Drag.SrcPanel=p; Drag.SrcTabIdx=kWholePanel;
                Drag.GhostX=ctx->MousePos[0]; Drag.GhostY=ctx->MousePos[1];
            }
        }

        ZUIEndRow(ctx);

        // Chrome shelf: 1px PanelBg strip sealing the tab bar / content boundary.
        // Active tab BG == PanelBg → tab appears to merge seamlessly into the panel below.
        {
            char fk[48]; snprintf(fk, sizeof(fk), "##tbfloor_%llx", (unsigned long long)p->DockKey);
            ZUIBox* fl = ZUIPushBox(ctx, fk, (uint32_t)strlen(fk),
                                    ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            fl->Size[0]     = ZPx(rect[2] - rect[0]);
            fl->Size[1]     = ZPx(1.f);
            fl->FloatPos[0] = rect[0];
            fl->FloatPos[1] = rect[3] - 1.f;
            ZUIBoxSetColorArr(fl, ctx->Theme.PanelBg);
            fl->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }
    }

    // ---------------------------------------------------------------
    // BuildDropZones
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float fs=13.f*ctx->UIScale, major_dim=fs*5.f, minor_dim=fs*3.f;
        float w=rect[2]-rect[0], h=rect[3]-rect[1];
        float cx=(rect[0]+rect[2])*0.5f, cy=(rect[1]+rect[3])*0.5f;
        float fmx=ctx->MousePos[0], fmy=ctx->MousePos[1];

        struct Zone { ZUIDropZone zone; float x0,y0,x1,y1,px0,py0,px1,py1; };
        Zone zones[5]={
            {ZUIDropZone::Center, cx-major_dim*0.5f,cy-minor_dim*0.5f,cx+major_dim*0.5f,cy+minor_dim*0.5f, rect[0],rect[1],rect[2],rect[3]},
            {ZUIDropZone::Left,   rect[0]+2.f,cy-major_dim*0.5f,rect[0]+minor_dim+2.f,cy+major_dim*0.5f, rect[0],rect[1],rect[0]+w*0.4f,rect[3]},
            {ZUIDropZone::Right,  rect[2]-minor_dim-2.f,cy-major_dim*0.5f,rect[2]-2.f,cy+major_dim*0.5f, rect[2]-w*0.4f,rect[1],rect[2],rect[3]},
            {ZUIDropZone::Top,    cx-major_dim*0.5f,rect[1]+2.f,cx+major_dim*0.5f,rect[1]+minor_dim+2.f, rect[0],rect[1],rect[2],rect[1]+h*0.4f},
            {ZUIDropZone::Bottom, cx-major_dim*0.5f,rect[3]-minor_dim-2.f,cx+major_dim*0.5f,rect[3]-2.f, rect[0],rect[3]-h*0.4f,rect[2],rect[3]},
        };

        ZUIDropZone hovered = ZUIDropZone::None;
        for (auto& z : zones)
            if (fmx>=z.x0&&fmx<=z.x1&&fmy>=z.y0&&fmy<=z.y1) { hovered=z.zone; break; }
        Drag.DropZone = hovered;

        // Split preview
        if (hovered != ZUIDropZone::None && hovered != ZUIDropZone::Center)
        {
            for (auto& z : zones)
            {
                if (z.zone != hovered) continue;
                char pk[40]; snprintf(pk,sizeof(pk),"##dz_prev_%llx",(unsigned long long)p->DockKey);
                ZUIBox* prev=ZUIPushBox(ctx,pk,(uint32_t)strlen(pk),ZUI_DrawBackground|ZUI_DrawBorder|ZUI_FloatX|ZUI_FloatY);
                prev->Size[0]=ZPx(z.px1-z.px0); prev->Size[1]=ZPx(z.py1-z.py0);
                prev->FloatPos[0]=z.px0; prev->FloatPos[1]=z.py0;
                float pc[4]={ctx->Theme.TabActiveBorder[0],ctx->Theme.TabActiveBorder[1],ctx->Theme.TabActiveBorder[2],0.18f};
                ZUIBoxSetColorArr(prev,pc);
                prev->BorderColor[0]=ctx->Theme.TabActiveBorder[0];
                prev->BorderColor[1]=ctx->Theme.TabActiveBorder[1];
                prev->BorderColor[2]=ctx->Theme.TabActiveBorder[2];
                prev->BorderColor[3]=0.70f; prev->BorderThickness=1.f; prev->EdgeSoftness=0.f;
                ZUIPopBox(ctx); break;
            }
        }

        for (auto& z : zones)
        {
            bool over=(z.zone==hovered);
            float col[4]={ctx->Theme.TabActiveBorder[0],ctx->Theme.TabActiveBorder[1],ctx->Theme.TabActiveBorder[2],over?0.80f:0.35f};
            char zkey[48]; snprintf(zkey,sizeof(zkey),"##dz_%d_%llx",(int)z.zone,(unsigned long long)p->DockKey);
            ZUIBox* box=ZUIPushBox(ctx,zkey,(uint32_t)strlen(zkey),ZUI_DrawBackground|ZUI_DrawBorder|ZUI_FloatX|ZUI_FloatY);
            box->Size[0]=ZPx(z.x1-z.x0); box->Size[1]=ZPx(z.y1-z.y0);
            box->FloatPos[0]=z.x0; box->FloatPos[1]=z.y0;
            ZUIBoxSetColorArr(box,col);
            box->BorderColor[0]=ctx->Theme.TabActiveBorder[0]; box->BorderColor[1]=ctx->Theme.TabActiveBorder[1];
            box->BorderColor[2]=ctx->Theme.TabActiveBorder[2]; box->BorderColor[3]=over?1.f:0.55f;
            box->BorderThickness=over?2.f:1.f; ZUIBoxSetCornerRadius(box,5.f); box->EdgeSoftness=0.5f;
            ZUIPopBox(ctx);
        }

        if (ctx->MouseReleased[0] && hovered != ZUIDropZone::None && Drag.SrcPanel)
        {
            ZUIDockNode* dst_node = ZUIDockFindLeaf(DockTree, p->DockKey);
            if (dst_node) CommitDrop(Drag.SrcPanel, Drag.SrcTabIdx, dst_node, hovered);
            Drag.Active=false; Drag.SrcPanel=nullptr; Drag.DropZone=ZUIDropZone::None; Drag.HoverNode=nullptr;
        }
    }

    // ---------------------------------------------------------------
    // BuildDividers — dynamic from dock tree
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDividers(ZUIContext* ctx)
    {
        if (!DockTree) return;
        float mx=ctx->MousePos[0], my=ctx->MousePos[1];

        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            ZUIDockNode* snode = m_split_dividers[di].Node;
            if (!snode || !snode->First || !snode->First->Next) continue;
            ZUIDockNode* child1=snode->First;
            bool horizontal=(snode->SplitAxis==ZUIAxis::Y);
            float dx0,dy0,dx1,dy1;
            if (!horizontal)
            { float ex=child1->RectMax[0]; dx0=ex-kDivGrabW*0.5f;dy0=snode->RectMin[1];dx1=ex+kDivGrabW*0.5f;dy1=snode->RectMax[1]; }
            else
            { float ey=child1->RectMax[1]; dx0=snode->RectMin[0];dy0=ey-kDivGrabW*0.5f;dx1=snode->RectMax[0];dy1=ey+kDivGrabW*0.5f; }

            bool in_rect=(mx>=dx0&&mx<=dx1&&my>=dy0&&my<=dy1);
            bool& dragging=m_split_dividers[di].Dragging;
            if (ctx->MousePressed[0] && in_rect) dragging=true;
            if (ctx->MouseReleased[0] && dragging) { dragging=false; LayoutDirty=true; }
            else if (ctx->MouseReleased[0]) dragging=false;

            if (dragging && ctx->MouseDown[0])
            {
                float delta=horizontal?(ctx->MousePos[1]-ctx->PrevMousePos[1]):(ctx->MousePos[0]-ctx->PrevMousePos[0]);
                if (delta!=0.f) ZUIDockResize(DockTree,child1,delta);
            }

            if (in_rect || dragging) ctx->ResizeCursor = horizontal ? 2 : 1;

            bool highlight=in_rect||dragging;
            float lw=highlight?3.f:1.f;
            float vc[4]={ctx->Theme.TabActiveBorder[0],ctx->Theme.TabActiveBorder[1],ctx->Theme.TabActiveBorder[2],highlight?1.f:0.35f};
            if (!highlight){vc[0]=0.25f;vc[1]=0.25f;vc[2]=0.28f;}

            if (highlight)
            {
                char hk[32]; snprintf(hk,sizeof(hk),"##sdivh_%u",di);
                ZUIBox* ha=ZUIPushBox(ctx,hk,(uint32_t)strlen(hk),ZUI_DrawBackground|ZUI_FloatX|ZUI_FloatY);
                ha->Size[0]=ZPx(dx1-dx0); ha->Size[1]=ZPx(dy1-dy0);
                ha->FloatPos[0]=dx0; ha->FloatPos[1]=dy0;
                ZUIBoxSetColor(ha,ctx->Theme.TabActiveBorder[0],ctx->Theme.TabActiveBorder[1],ctx->Theme.TabActiveBorder[2],0.25f);
                ha->EdgeSoftness=0.f; ZUIPopBox(ctx);
            }

            char vk[32]; snprintf(vk,sizeof(vk),"##sdiv_%u",di);
            ZUIBox* vis=ZUIPushBox(ctx,vk,(uint32_t)strlen(vk),ZUI_DrawBackground|ZUI_FloatX|ZUI_FloatY);
            if (!horizontal){ vis->Size[0]=ZPx(lw);vis->Size[1]=ZPx(dy1-dy0);vis->FloatPos[0]=(dx0+dx1)*0.5f-lw*0.5f;vis->FloatPos[1]=dy0; }
            else             { vis->Size[0]=ZPx(dx1-dx0);vis->Size[1]=ZPx(lw);vis->FloatPos[0]=dx0;vis->FloatPos[1]=(dy0+dy1)*0.5f-lw*0.5f; }
            vis->EdgeSoftness=0.f; ZUIBoxSetColorArr(vis,vc); ZUIPopBox(ctx);
        }
    }

    // ---------------------------------------------------------------
    // CommitDrop — moves a tab (or whole panel) to a new dock position
    // ---------------------------------------------------------------

    void ZUIPanelManager::CommitDrop(ZUIPanel* src, uint32_t tab_idx,
                                      ZUIDockNode* dst, ZUIDropZone zone)
    {
        if (!src || !dst) return;
        LayoutDirty = true; // structural change → save next frame

        if (tab_idx == kWholePanel)
        {
            // Move entire panel
            if (zone == ZUIDropZone::Center)
            {
                // Merge all source views into destination as tabs
                ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
                if (dst_panel && dst_panel != src)
                {
                    for (uint32_t i = 0; i < src->ViewCount; ++i)
                        AddView(dst_panel, src->Views[i]);
                    src->ViewCount = 0;
                    // Collapse source dock slot
                    ZUIDockNode* src_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
                    if (src_leaf) ZUIDockCollapseLeaf(DockTree, src_leaf);
                }
            }
            else
            {
                // Split and move panel to new slot
                // First collapse source to free its slot
                ZUIDockNode* src_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
                if (src_leaf) ZUIDockCollapseLeaf(DockTree, src_leaf);
                // Re-find destination after potential tree change
                ZUIDockNode* new_dst = ZUIDockFindLeaf(DockTree, dst->ContentKey);
                if (new_dst)
                {
                    float split_pct = 0.5f;
                    if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
                    {
                        bool left = (zone == ZUIDropZone::Left);
                        ZUIDockSplitH(DockTree, new_dst,
                                      left ? split_pct : 1.f-split_pct,
                                      left ? src->DockKey : new_dst->ContentKey,
                                      left ? new_dst->ContentKey : src->DockKey);
                    }
                    else
                    {
                        bool top = (zone == ZUIDropZone::Top);
                        ZUIDockSplitV(DockTree, new_dst,
                                      top ? split_pct : 1.f-split_pct,
                                      top ? src->DockKey : new_dst->ContentKey,
                                      top ? new_dst->ContentKey : src->DockKey);
                    }
                }
            }
            return;
        }

        // Single tab move
        if (tab_idx >= src->ViewCount) return;
        ZUIPanelView* view = src->Views[tab_idx];

        if (zone == ZUIDropZone::Center)
        {
            ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
            if (dst_panel && dst_panel != src)
            {
                AddView(dst_panel, view);
                for (uint32_t i = tab_idx; i+1 < src->ViewCount; ++i) src->Views[i]=src->Views[i+1];
                --src->ViewCount;
                if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0) src->ActiveTab=src->ViewCount-1;
            }
        }
        else
        {
            float split_pct = 0.5f;
            uint64_t new_key = view->Key ? view->Key : ZUIDockHashName(view->Title ? view->Title : "new");
            if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
            {
                bool left=(zone==ZUIDropZone::Left);
                ZUIDockSplitH(DockTree,dst,left?split_pct:1.f-split_pct,left?new_key:dst->ContentKey,left?dst->ContentKey:new_key);
            }
            else
            {
                bool top=(zone==ZUIDropZone::Top);
                ZUIDockSplitV(DockTree,dst,top?split_pct:1.f-split_pct,top?new_key:dst->ContentKey,top?dst->ContentKey:new_key);
            }
            ZUIPanel* new_panel = AddPanel(new_key);
            if (new_panel)
            {
                AddView(new_panel, view);
                for (uint32_t i=tab_idx;i+1<src->ViewCount;++i) src->Views[i]=src->Views[i+1];
                --src->ViewCount;
                if (src->ActiveTab>=src->ViewCount&&src->ViewCount>0) src->ActiveTab=src->ViewCount-1;
            }
        }
    }

} // namespace ZEngine::UI
