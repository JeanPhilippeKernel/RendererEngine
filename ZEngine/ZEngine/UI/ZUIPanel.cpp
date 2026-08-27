#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIFont.h>
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
    // PreDetectCloseEvents — Clay-style pre-pass
    // Detects close button clicks using ctx->ActiveKey + ctx->MouseReleased
    // BEFORE any panel box is built, so ZUIDockLayout sees the correct tree.
    // ctx->ActiveKey is set by the previous frame's ZUIInteractionPass and is
    // still valid during the current frame's build phase.
    // ---------------------------------------------------------------

    void ZUIPanelManager::PreDetectCloseEvents(ZUIContext* ctx)
    {
        if (!ctx->MouseReleased[0] || ctx->ActiveKey == 0) { return; }

        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel* p = &Panels[i];
            if (p->Hidden || p->ViewCount == 0) { continue; }

            // ── Tab bar close buttons ─────────────────────────────────────
            for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
            {
                char xkey[64];
                snprintf(xkey, sizeof(xkey), "x##x_%llx_%u",
                         (unsigned long long)p->DockKey, ti);
                uint64_t xhash = ZUIHashStr(xkey, (uint32_t)strlen(xkey));

                if (ctx->ActiveKey == xhash && ctx->HotKey == xhash)
                {
                    if (p->ViewCount > 1)
                    {
                        // Remove this tab — panel keeps rendering with fewer tabs
                        for (uint32_t j = ti; j+1 < p->ViewCount; ++j)
                            p->Views[j] = p->Views[j+1];
                        --p->ViewCount;
                        if (p->ActiveTab >= p->ViewCount && p->ViewCount > 0)
                            p->ActiveTab = p->ViewCount - 1;
                    }
                    else
                    {
                        // Last tab — panel disappears this frame
                        if (PendingCloseCount < kMaxPanels)
                            PendingCloseKeys[PendingCloseCount++] = p->DockKey;
                    }
                    LayoutDirty = true;
                    goto next_panel; // only one close per panel per frame
                }
            }

            // ── Single-view title strip close button ──────────────────────
            {
                char txk[56];
                snprintf(txk, sizeof(txk), "x##tx_%llx",
                         (unsigned long long)p->DockKey);
                uint64_t txhash = ZUIHashStr(txk, (uint32_t)strlen(txk));

                if (ctx->ActiveKey == txhash && ctx->HotKey == txhash)
                {
                    if (PendingCloseCount < kMaxPanels)
                        PendingCloseKeys[PendingCloseCount++] = p->DockKey;
                    LayoutDirty = true;
                }
            }

            next_panel:;
        }
    }

    // ---------------------------------------------------------------
    // BuildUI — top-level per-frame entry
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildUI(ZUIContext* ctx, float menu_h, float status_h)
    {
        float sw = (float)ctx->ScreenW;
        float sh = (float)ctx->ScreenH;

        // Clay-style pre-pass: detect close events BEFORE layout so the
        // sibling fills the space in the same frame the panel disappears.
        PreDetectCloseEvents(ctx);

        // Flush pending layout save (triggered by drag/close events last frame)
        if (LayoutDirty && LayoutPath[0])
        {
            ZUIDockSave(this, LayoutPath);
            LayoutDirty = false;
        }

        // Flush deferred close queue — must run BEFORE ZUIDockLayout so
        // the sibling panel gets the freed space in the same frame the
        // closed panel disappears (no 1-frame black gap).
        for (uint32_t ci = 0; ci < PendingCloseCount; ++ci)
        {
            ZUIPanel* cp = FindPanel(PendingCloseKeys[ci]);
            if (cp) { cp->Hidden = true; }
            if (DockTree) {
                ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, PendingCloseKeys[ci]);
                if (leaf) ZUIDockCollapseLeaf(DockTree, leaf);
            }
            LayoutDirty = true;
        }
        PendingCloseCount = 0;

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

        Drag.HoverNode  = nullptr;
        Drag.DropZone   = ZUIDropZone::None; // reset each frame; set by BuildDropZones below
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

                float gh = ZUIGetFrameHeight(ctx);
                float ghost_cnt_h = ctx->Style.TabGhostContentH;
                float text_w = 80.f;  // fallback
                if (ctx->GetFont(ZUIFontSize::Body) && title)
                {
                    float ts[2] = {0.f, 0.f};
                    ZUIMeasureText(ctx->GetFont(ZUIFontSize::Body), title, (uint32_t)strlen(title), ts);
                    text_w = ts[0];
                }
                float gw = fmaxf(text_w + ZUIGetFramePadX(ctx) * 4.f, 120.f);

                // Content box first (lower z-order):
                ZUIBox* cnt = ZUIPushBox(ctx, "##dg_cnt", 7,
                    ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
                cnt->Size[0] = ZPx(gw); cnt->Size[1] = ZPx(ghost_cnt_h);
                cnt->FloatPos[0] = Drag.GhostX - gw * 0.3f;
                cnt->FloatPos[1] = Drag.GhostY - gh * 0.5f + gh;
                ZUIBoxSetColorArr(cnt, ctx->Theme.PanelBg);
                cnt->BorderColor[0]=ctx->Theme.TabActiveBorder[0];
                cnt->BorderColor[1]=ctx->Theme.TabActiveBorder[1];
                cnt->BorderColor[2]=ctx->Theme.TabActiveBorder[2];
                cnt->BorderColor[3]=0.70f;
                cnt->BorderThickness=1.f; cnt->EdgeSoftness=0.f;
                ZUIPopBox(ctx);

                // Tab bar row (on top):
                ZUIBox* ghost = ZUIBeginRow(ctx, "##drag_ghost", ZPx(gw), ZPx(gh));
                ghost->Flags = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
                ghost->FloatPos[0] = Drag.GhostX - gw * 0.3f;
                ghost->FloatPos[1] = Drag.GhostY - gh * 0.5f;
                ZUIBoxSetColorArr(ghost, ctx->Theme.TitleBgActive);
                ZUIBoxSetTopRadius(ghost, ctx->Style.TabRounding);
                ghost->BorderColor[0]=ctx->Theme.TabActiveBorder[0];
                ghost->BorderColor[1]=ctx->Theme.TabActiveBorder[1];
                ghost->BorderColor[2]=ctx->Theme.TabActiveBorder[2];
                ghost->BorderColor[3]=0.90f;
                ghost->BorderThickness=1.f; ghost->EdgeSoftness=0.f;
                ZUISpacer(ctx, ZUIGetFramePadX(ctx));
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
            // ── Reset Layout ──────────────────────────────────────────
            if (ZUIMenuItem(ctx, "Reset Layout"))
            {
                // Unhide all hidden panels and re-insert them into the tree
                for (uint32_t i = 0; i < PanelCount; ++i)
                {
                    ZUIPanel* p = &Panels[i];
                    if (!p->Hidden || p->ViewCount == 0) { continue; }
                    p->Hidden = false;
                    if (DockTree)
                    {
                        ZUIDockNode* target = FindLargestLeaf(DockTree->Root);
                        if (target)
                            ZUIDockSplitH(DockTree, target, 0.75f,
                                          target->ContentKey, p->DockKey);
                    }
                }
                // Delete saved layout so it restarts from default next launch
                if (LayoutPath[0])
                {
                    remove(LayoutPath);
                }
                LayoutDirty = true;
            }

            ZUISeparator(ctx);
            ZUISeparatorText(ctx, "Panels");

            // ── Panel visibility — toggle individual panels ───────────
            // Note: ZUIBeginMenu nesting not yet supported (single ActivePopupKey).
            // Panels listed directly in the Window menu under a "Panels" group header.
            {
                static const char* kPanelNames[] = {
                    "Hierarchy", "Console", "Inspector", "Viewport"
                };
                static constexpr uint32_t kPanelNameCount = 4;

                for (uint32_t ni = 0; ni < kPanelNameCount; ++ni)
                {
                    const char* target_name = kPanelNames[ni];

                    ZUIPanel* match     = nullptr;
                    for (uint32_t i = 0; i < PanelCount; ++i)
                    {
                        ZUIPanel* p = &Panels[i];
                        if (p->ViewCount == 0) continue;
                        const char* t = p->Views[0] ? p->Views[0]->Title : "";
                        if (strcmp(t, target_name) == 0) { match = p; break; }
                    }

                    // "* Name" = visible   "  Name" = hidden
                    char label[96];
                    bool visible = match && !match->Hidden;
                    snprintf(label, sizeof(label), "%s%s##pmn_%u",
                             visible ? "* " : "  ", target_name, ni);

                    if (ZUIMenuItem(ctx, label) && match)
                    {
                        if (match->Hidden)
                        {
                            match->Hidden = false;
                            if (DockTree)
                            {
                                ZUIDockNode* tgt = FindLargestLeaf(DockTree->Root);
                                if (tgt)
                                    ZUIDockSplitH(DockTree, tgt, 0.75f,
                                                  tgt->ContentKey, match->DockKey);
                            }
                        }
                        else if (PendingCloseCount < kMaxPanels)
                        {
                            PendingCloseKeys[PendingCloseCount++] = match->DockKey;
                        }
                        LayoutDirty = true;
                    }
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

        // header_h from style (was kTabBarH + 2.f = 28)
        float header_h = ZUIGetFrameHeight(ctx);  // 19px

        // Central node check: read IsCentral from the dock node, not a manager key
        ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, p->DockKey);
        bool is_central = leaf && leaf->IsCentral;

        if (is_central)
        {
            // Pure passthrough — multi-tab gets minimal tab bar, single gets nothing
            if (p->ViewCount > 1)
            {
                float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + header_h };
                BuildTabBar(ctx, p, tab_rect);
                float cr[4] = { rect[0], rect[1] + header_h, rect[2], rect[3] };
                ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                if (view) { view->BuildContent(ctx, cr); }
            }
            else
            {
                ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                if (view) { view->BuildContent(ctx, rect); }
            }
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

        // AutoHideTabBar: use node flag, defaulting to false if no node found
        // (false = ImGui default: always show tab bar even for single-view nodes)
        bool auto_hide = leaf ? leaf->AutoHideTabBar : false;
        bool show_tabs = (p->ViewCount > 1) || !auto_hide;

        if (show_tabs)
        {
            float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + header_h };
            BuildTabBar(ctx, p, tab_rect);
        }
        else
        {
            // VS Code single-view title strip — style-driven
            const char* view_title = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "Panel";
            float btn_h = ctx->Style.FontSize;  // was header_h * 0.60

            char hk[48]; snprintf(hk, sizeof(hk), "##tbar_%llx", (unsigned long long)p->DockKey);
            ZUIBox* strip = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
            strip->Flags = strip->Flags | ZUI_DrawBackground | ZUI_Clickable;
            // Background: focus-aware
            ZUIBoxSetColorArr(strip, is_focused ? ctx->Theme.TitleBgActive : ctx->Theme.TitleBarBg);
            strip->EdgeSoftness = 0.f;

            // Left padding from style
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));  // was 10px
            // Icon dot — size from style
            {
                const float* ic = (p->Views[0] && p->Views[0]->TabColor[3] > 0.01f)
                                 ? p->Views[0]->TabColor : ctx->Theme.PanelFocusBorder;
                char ik[48]; snprintf(ik, sizeof(ik), "##tic_%llx", (unsigned long long)p->DockKey);
                float icon_sz = ctx->Style.TabIconSize;  // was 8.f hardcoded
                ZUIBox* icon = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                icon->Size[0]=ZPx(icon_sz); icon->Size[1]=ZPx(icon_sz);
                ZUIBoxSetColorArr(icon, ic);
                ZUIBoxSetCornerRadius(icon, icon_sz * 0.5f);
                icon->EdgeSoftness=0.5f;
                ZUIPopBox(ctx);
            }
            // Icon-to-label gap from style
            ZUISpacer(ctx, ZUIGetInnerSpac(ctx));  // was 7px
            ZUILabel(ctx, view_title, ctx->Theme.TextDefault);

            // Fill
            { char fk[48]; snprintf(fk,sizeof(fk),"##tf_%llx",(unsigned long long)p->DockKey);
              ZUIBox* f=ZUIPushBox(ctx,fk,(uint32_t)strlen(fk),ZUI_None);
              f->Size[0]=ZFill(); f->Size[1]=ZPx(header_h); ZUIPopBox(ctx); }

            // x close (hover-only)
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
            // Right spacer from style
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));  // was 6px

            ZUISignal strip_sig = ZUISignalFromBox(ctx, strip);
            ZUIEndRow(ctx);

            // Record press start
            if (strip_sig.Flags & ZUI_SignalPressed)
            { Drag.StartX = ctx->MousePos[0]; Drag.StartY = ctx->MousePos[1]; }

            // Drag threshold from style
            if (!should_close && (strip_sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float dx = fabsf(ctx->MousePos[0]-Drag.StartX);
                float dy = fabsf(ctx->MousePos[1]-Drag.StartY);
                if (dx + dy > ctx->Style.DockingDragThreshold)  // was 8.f
                {
                    Drag.Active    = true;
                    Drag.SrcPanel  = p;
                    Drag.SrcTabIdx = kWholePanel;
                    Drag.GhostX    = ctx->MousePos[0];
                    Drag.GhostY    = ctx->MousePos[1];
                }
            }

            // Close already handled by PreDetectCloseEvents pre-pass.
            (void)should_close;
        }

        // Content — no WindowPadding for docked panels (editor panels fill edge-to-edge).
        // WindowPadding will be applied when floating windows are implemented.
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view)
        {
            ZUIBox* content = ZUIBeginColumn(ctx, "##dc", ZFill(), ZFill());
            content->Flags  = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            float cr[4] = {
                rect[0],
                rect[1] + header_h,
                rect[2],
                rect[3]
            };
            view->BuildContent(ctx, cr);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx);

        // VS Code focus: left strip, width from style
        if (is_focused)
        {
            char fk[48]; snprintf(fk,sizeof(fk),"##pfocus_%llx",(unsigned long long)p->DockKey);
            ZUIBox* fb = ZUIPushBox(ctx,fk,(uint32_t)strlen(fk),ZUI_DrawBackground|ZUI_FloatX|ZUI_FloatY);
            fb->Size[0]=ZPx(ctx->Style.DockingFocusBorderWidth);  // was 3.f
            fb->Size[1]=ZPx(rect[3]-rect[1]);
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
        float tab_h = ZUIGetFrameHeight(ctx);  // 19px from style
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

        // Tab leading spacer from style
        ZUISpacer(ctx, ZUIGetFramePadX(ctx));  // was 4px, now from style

        for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
        {
            ZUIPanelView* view = p->Views[ti];
            if (!view) { continue; }
            bool is_active = (ti == p->ActiveTab);

            char tab_key[64];
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%u", (unsigned long long)p->DockKey, ti);
            ZUIBox* tab = ZUIBeginRow(ctx, tab_key, ZFit(), ZPx(tab_h));
            tab->Flags = tab->Flags | ZUI_DrawBackground | ZUI_Clickable;
            tab->EdgeSoftness = 0.f;
            ZUIBoxSetTopRadius(tab, ctx->Style.TabRounding);  // was 6px

            // 4-STATE COLOR MACHINE
            if (is_active && panel_focused)
                ZUIBoxSetColorArr(tab, ctx->Theme.TabActiveBg);
            else if (is_active && !panel_focused)
                ZUIBoxSetColorArr(tab, ctx->Theme.TabDimmedSelectedBg);
            else
            {
                bool tab_hov = (ctx->HotKey == tab->Key);
                const float* rest = panel_focused ? ctx->Theme.TabInactiveBg : ctx->Theme.TabDimmedBg;
                ZUIBoxSetColorArr(tab, tab_hov ? ctx->Theme.TabHoveredBg : rest);
            }

            // Inner padding from style
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));  // was 10px
            // Label
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
                float btn_sz = ctx->Style.FontSize;  // was 18px hardcoded
                bool tab_hovered = (ctx->HotKey == tab->Key);
                bool show_close  = is_active || tab_hovered;
                if (show_close)
                {
                    ZUISpacer(ctx, ZUIGetInnerSpac(ctx));
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
                    ZUISpacer(ctx, ZUIGetFramePadX(ctx));  // after close button
                }
                else
                {
                    ZUISpacer(ctx, ZUIGetFramePadX(ctx));
                }
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);

            // Close already handled by PreDetectCloseEvents pre-pass.
            // Nothing to do here — just break to exit the tab loop cleanly.
            if (tab_closed) { break; }

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
                if (total > ctx->Style.DockingTabReorderThreshold)  // was 5.f
                {
                    bool mostly_horiz = (dx > dy*1.5f);
                    if (mostly_horiz && p->ViewCount > 1)
                    {
                        if (!p->ReorderActive) { p->ReorderActive=true; p->ReorderTabIdx=ti; p->ReorderAccumX=0.f; }
                        if (p->ReorderActive && p->ReorderTabIdx == ti)
                        {
                            p->ReorderAccumX += sig.DragDelta[0];
                            float tab_slot = (rect[2]-rect[0]) / (float)p->ViewCount;
                            if (tab_slot < ctx->Style.DockingMinTabWidth) tab_slot = ctx->Style.DockingMinTabWidth;
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
                    else if (!mostly_horiz || dy > ctx->Style.DockingUndockVertical)  // was 12.f
                    {
                        p->ReorderActive=false;
                        Drag.Active=true; Drag.SrcPanel=p; Drag.SrcTabIdx=ti;
                        Drag.GhostX=ctx->MousePos[0]; Drag.GhostY=ctx->MousePos[1];
                    }
                }
            }

            if (ctx->MouseReleased[0] && p->ReorderActive)
            { p->ReorderActive=false; p->ReorderAccumX=0.f; }

            // Inter-tab spacing from style
            ZUISpacer(ctx, ZUIGetInnerSpac(ctx));  // was 2px
        }

        // Bar signal — drag empty space = whole-panel drag
        ZUISignal bar_sig = ZUISignalFromBox(ctx, bar);
        if (bar_sig.Flags & ZUI_SignalPressed)
        { Drag.StartX=ctx->MousePos[0]; Drag.StartY=ctx->MousePos[1]; }
        if ((bar_sig.Flags & ZUI_SignalHeld) && !Drag.Active)
        {
            float dx=fabsf(ctx->MousePos[0]-Drag.StartX), dy=fabsf(ctx->MousePos[1]-Drag.StartY);
            if (dx+dy > ctx->Style.DockingDragThreshold)  // was 8.f
            {
                float panel_r[4]={};
                if (!ZUIDockRectForKey(DockTree, p->DockKey, panel_r))
                { panel_r[0]=rect[0];panel_r[1]=rect[1];panel_r[2]=rect[2];panel_r[3]=rect[3]+200.f; }
                Drag.Active=true; Drag.SrcPanel=p; Drag.SrcTabIdx=kWholePanel;
                Drag.GhostX=ctx->MousePos[0]; Drag.GhostY=ctx->MousePos[1];
            }
        }

        ZUIEndRow(ctx);

        // OVERLINE: floating 2px teal strip above the active tab (above tab bar top edge)
        {
            float x_cursor = rect[0] + ZUIGetFramePadX(ctx);
            float active_x = 0.f, active_w = 0.f;
            for (uint32_t ti2 = 0; ti2 < p->ViewCount; ++ti2)
            {
                ZUIPanelView* v = p->Views[ti2]; if (!v) continue;
                // tab width = FramePadX + text_measure + inner_spacing + close_btn + FramePadX
                float tw = ZUIGetFramePadX(ctx);
                if (ctx->GetFont(ZUIFontSize::Body) && v->Title)
                {
                    float ts[2] = {0.f, 0.f};
                    ZUIMeasureText(ctx->GetFont(ZUIFontSize::Body), v->Title, (uint32_t)strlen(v->Title), ts);
                    tw += ts[0];
                }
                tw += ZUIGetInnerSpac(ctx) + ctx->Style.FontSize + ZUIGetFramePadX(ctx);
                if (ti2 == p->ActiveTab) { active_x = x_cursor; active_w = tw; break; }
                x_cursor += tw + ZUIGetInnerSpac(ctx);
            }
            if (active_w > 0.f)
            {
                char ok[64]; snprintf(ok, sizeof(ok), "##tover_%llx", (unsigned long long)p->DockKey);
                ZUIBox* ov = ZUIPushBox(ctx, ok, (uint32_t)strlen(ok),
                    ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                ov->Size[0] = ZPx(active_w);
                ov->Size[1] = ZPx(ctx->Style.TabBarOverlineSize);
                ov->FloatPos[0] = active_x;
                ov->FloatPos[1] = rect[1] - ctx->Style.TabBarBorderSize;  // above tab bar top
                const float* ovc = panel_focused ? ctx->Theme.TabActiveBorder : ctx->Theme.TitleBarBg;
                ZUIBoxSetColorArr(ov, ovc);
                ov->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }
        }

    }

    // ---------------------------------------------------------------
    // BuildDropZones — position-based ImGui-style subdivision
    // Zone is determined by WHERE the mouse is within the target panel,
    // not by hovering explicit indicator widgets.  Only a split preview
    // overlay is drawn — no directional arrows, no target boxes.
    // ---------------------------------------------------------------

    void ZUIPanelManager::BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float w   = rect[2] - rect[0];
        float h   = rect[3] - rect[1];
        float mx  = ctx->MousePos[0];
        float my  = ctx->MousePos[1];
        float rx  = (w > 0.f) ? (mx - rect[0]) / w : 0.5f; // 0..1 across width
        float ry  = (h > 0.f) ? (my - rect[1]) / h : 0.5f; // 0..1 down height

        // Edge bands from style
        float kEdge = ctx->Style.DockingDropZoneEdge;  // was static constexpr 0.25f

        ZUIDropZone zone;
        if      (rx < kEdge)         zone = ZUIDropZone::Left;
        else if (rx > 1.f - kEdge)   zone = ZUIDropZone::Right;
        else if (ry < kEdge)         zone = ZUIDropZone::Top;
        else if (ry > 1.f - kEdge)   zone = ZUIDropZone::Bottom;
        else                         zone = ZUIDropZone::Center;

        Drag.DropZone = zone;

        // Compute the preview rect (where the dropped panel will land)
        float px0 = rect[0], py0 = rect[1], px1 = rect[2], py1 = rect[3];
        switch (zone)
        {
            case ZUIDropZone::Left:   px1 = rect[0] + w * 0.5f; break;
            case ZUIDropZone::Right:  px0 = rect[0] + w * 0.5f; break;
            case ZUIDropZone::Top:    py1 = rect[1] + h * 0.5f; break;
            case ZUIDropZone::Bottom: py0 = rect[1] + h * 0.5f; break;
            default: break; // Center: full panel preview
        }

        // Draw preview overlay — tinted fill + 2px border
        {
            const float* ac = ctx->Theme.TabActiveBorder;
            char pk[40]; snprintf(pk, sizeof(pk), "##dz_prev_%llx", (unsigned long long)p->DockKey);
            ZUIBox* prev = ZUIPushBox(ctx, pk, (uint32_t)strlen(pk),
                ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
            prev->Size[0]     = ZPx(px1 - px0);
            prev->Size[1]     = ZPx(py1 - py0);
            prev->FloatPos[0] = px0;
            prev->FloatPos[1] = py0;
            // Preview fill alpha from style
            ZUIBoxSetColor(prev, ac[0], ac[1], ac[2], ctx->Style.DockingDropPreviewAlpha);  // was 0.16f
            prev->BorderColor[0]  = ac[0]; prev->BorderColor[1] = ac[1];
            prev->BorderColor[2]  = ac[2]; prev->BorderColor[3] = 0.80f;
            prev->BorderThickness = 2.f;
            prev->EdgeSoftness    = 0.f;
            ZUIPopBox(ctx);
        }

        if (ctx->MouseReleased[0] && zone != ZUIDropZone::None && Drag.SrcPanel)
        {
            ZUIDockNode* dst_node = ZUIDockFindLeaf(DockTree, p->DockKey);
            if (dst_node) CommitDrop(Drag.SrcPanel, Drag.SrcTabIdx, dst_node, zone);
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
            float grab_half = ctx->Style.DockingGrabWidth * 0.5f;  // was kDivGrabW * 0.5f
            float dx0,dy0,dx1,dy1;
            if (!horizontal)
            { float ex=child1->RectMax[0]; dx0=ex-grab_half;dy0=snode->RectMin[1];dx1=ex+grab_half;dy1=snode->RectMax[1]; }
            else
            { float ey=child1->RectMax[1]; dx0=snode->RectMin[0];dy0=ey-grab_half;dx1=snode->RectMax[0];dy1=ey+grab_half; }

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
            float lw = highlight ? ctx->Style.DockingSeparatorSize : ctx->Style.DockingSeparatorSizeRest;  // was 3.f : 1.f
            float vc[4]={ctx->Theme.TabActiveBorder[0],ctx->Theme.TabActiveBorder[1],ctx->Theme.TabActiveBorder[2],highlight?1.f:0.35f};
            // Rest-state color from theme
            if (!highlight)
            {
                vc[0]=ctx->Theme.Separator[0];
                vc[1]=ctx->Theme.Separator[1];
                vc[2]=ctx->Theme.Separator[2];
            }

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
