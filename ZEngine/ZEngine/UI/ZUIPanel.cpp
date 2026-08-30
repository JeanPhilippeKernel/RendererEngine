#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // Init / Registration

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
        if (PanelCount >= kMaxPanels)
        {
            return nullptr;
        }
        ZUIPanel* p  = &Panels[PanelCount++];
        p->DockKey   = dock_key;
        p->ViewCount = 0;
        p->ActiveTab = 0;
        p->Hidden    = false;
        return p;
    }

    void ZUIPanelManager::AddView(ZUIPanel* panel, ZUIPanelView* view)
    {
        if (!panel || panel->ViewCount >= kMaxTabsPerPanel)
        {
            return;
        }
        panel->Views[panel->ViewCount++] = view;
    }

    // Insert a view at a specific index; idx >= ViewCount appends at end.
    static void InsertViewAt(ZUIPanel* panel, ZUIPanelView* view, uint32_t idx)
    {
        if (!panel || panel->ViewCount >= kMaxTabsPerPanel)
        {
            return;
        }
        if (idx > panel->ViewCount)
            idx = panel->ViewCount;
        for (uint32_t i = panel->ViewCount; i > idx; --i)
            panel->Views[i] = panel->Views[i - 1];
        panel->Views[idx] = view;
        ++panel->ViewCount;
    }

    ZUIPanel* ZUIPanelManager::FindPanel(uint64_t dock_key)
    {
        for (uint32_t i = 0; i < PanelCount; ++i)
            if (Panels[i].DockKey == dock_key)
                return &Panels[i];
        return nullptr;
    }

    static ZUIDockNode* FindLargestLeaf(ZUIDockNode* node)
    {
        if (!node)
        {
            return nullptr;
        }
        ZUIDockNode* best      = nullptr;
        float        best_area = -1.f;
        ZUIDockNode* stack[64];
        int          top = 0;
        stack[top++]     = node;
        while (top > 0)
        {
            ZUIDockNode* n = stack[--top];
            if (n->ContentKey != 0)
            {
                float area = (n->RectMax[0] - n->RectMin[0]) * (n->RectMax[1] - n->RectMin[1]);
                if (area > best_area)
                {
                    best_area = area;
                    best      = n;
                }
            }
            for (ZUIDockNode* c = n->First; c; c = c->Next)
                if (top < 64)
                    stack[top++] = c;
        }
        return best;
    }

    void ZUIPanelManager::FocusPanel(uint32_t idx)
    {
        FocusedPanelIdx = idx;
    }

    // SplitDivider state

    bool ZUIPanelManager::GetSplitDividerDragging(ZUIDockNode* node) const
    {
        for (uint32_t i = 0; i < m_split_divider_count; ++i)
            if (m_split_dividers[i].Node == node)
                return m_split_dividers[i].Dragging;
        return false;
    }

    void ZUIPanelManager::SetSplitDividerDragging(ZUIDockNode* node, bool v)
    {
        for (uint32_t i = 0; i < m_split_divider_count; ++i)
        {
            if (m_split_dividers[i].Node == node)
            {
                m_split_dividers[i].Dragging = v;
                return;
            }
        }
        if (m_split_divider_count < kMaxSplitDividers)
            m_split_dividers[m_split_divider_count++] = {node, v};
    }

    void ZUIPanelManager::SyncSplitDividers()
    {
        if (!DockTree || !DockTree->Root)
            return;
        ZUIDockNode* stack[64];
        int          top = 0;
        stack[top++]     = DockTree->Root;
        ZUIDockNode* seen[kMaxSplitDividers];
        uint32_t     seen_count = 0;
        while (top > 0)
        {
            ZUIDockNode* node = stack[--top];
            if (!node)
            {
                continue;
            }
            if (node->ContentKey == 0 && node->First)
            {
                seen[seen_count++] = node;
                bool found         = false;
                for (uint32_t i = 0; i < m_split_divider_count; ++i)
                    if (m_split_dividers[i].Node == node)
                    {
                        found = true;
                        break;
                    }
                if (!found && m_split_divider_count < kMaxSplitDividers)
                    m_split_dividers[m_split_divider_count++] = {node, false};
            }
            for (ZUIDockNode* c = node->First; c; c = c->Next)
                if (top < 64)
                    stack[top++] = c;
        }
        for (uint32_t i = 0; i < m_split_divider_count;)
        {
            bool alive = false;
            for (uint32_t j = 0; j < seen_count; ++j)
                if (seen[j] == m_split_dividers[i].Node)
                {
                    alive = true;
                    break;
                }
            if (!alive)
                m_split_dividers[i] = m_split_dividers[--m_split_divider_count];
            else
                ++i;
        }
    }

    // PreDetectCloseEvents — Clay-style pre-pass
    // Detects close button clicks using ctx->ActiveKey + ctx->MouseReleased
    // BEFORE any panel box is built, so ZUIDockLayout sees the correct tree.
    // ctx->ActiveKey is set by the previous frame's ZUIInteractionPass and is
    // still valid during the current frame's build phase.

    void ZUIPanelManager::PreDetectCloseEvents(ZUIContext* ctx)
    {
        if (!ctx->MouseReleased[0] || ctx->ActiveKey == 0)
        {
            return;
        }

        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel* p = &Panels[i];
            if (p->Hidden || p->ViewCount == 0 || !p->Closeable)
            {
                continue;
            }

            // Tab bar close buttons
            for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
            {
                char xkey[64];
                snprintf(xkey, sizeof(xkey), "x##x_%llx_%u", (unsigned long long) p->DockKey, ti);
                uint64_t xhash = ZUIHashStr(xkey, (uint32_t) strlen(xkey));

                if (ctx->ActiveKey == xhash && ctx->HotKey == xhash)
                {
                    if (p->ViewCount > 1)
                    {
                        // Remove this tab — panel keeps rendering with fewer tabs
                        for (uint32_t j = ti; j + 1 < p->ViewCount; ++j)
                            p->Views[j] = p->Views[j + 1];
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

            // Single-view title strip close button
            {
                char txk[56];
                snprintf(txk, sizeof(txk), "x##tx_%llx", (unsigned long long) p->DockKey);
                uint64_t txhash = ZUIHashStr(txk, (uint32_t) strlen(txk));

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

    // BuildUI — top-level per-frame entry

    void ZUIPanelManager::BuildUI(ZUIContext* ctx, float menu_h, float status_h)
    {
        float sw = (float) ctx->ScreenW;
        float sh = (float) ctx->ScreenH;

        // Clay-style pre-pass: detect close events BEFORE layout so the
        // sibling fills the space in the same frame the panel disappears.
        PreDetectCloseEvents(ctx);

        // Flush deferred close queue FIRST — must run before ZUIDockLayout so the
        // sibling fills freed space in the same frame, and before ZUIDockSave so the
        // saved tree reflects the actual post-close state (not one frame stale).
        for (uint32_t ci = 0; ci < PendingCloseCount; ++ci)
        {
            ZUIPanel* cp = FindPanel(PendingCloseKeys[ci]);
            if (cp)
            {
                cp->Hidden = true;
            }
            if (DockTree)
            {
                ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, PendingCloseKeys[ci]);
                if (leaf)
                    ZUIDockCollapseLeaf(DockTree, leaf);
            }
            LayoutDirty = true;
        }
        PendingCloseCount = 0;

        // Save layout AFTER close flush so the file always reflects the current state.
        if (LayoutDirty && LayoutPath[0])
        {
            ZUIDockSave(this, LayoutPath);
            LayoutDirty = false;
        }

        if (DockTree)
        {
            float root_rect[4] = {0.f, menu_h, sw, sh - status_h};
            ZUIDockLayout(DockTree, root_rect);
            SyncSplitDividers();
        }

        ZUIBox* bg      = ZUIBeginColumn(ctx, "##pm_bg", ZPx(sw), ZPx(sh));
        bg->Flags       = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        bg->EdgeSoftness = 0.f;

        if (DrawMenuBar)
            BuildMenuBar(ctx, sw, menu_h);

        // Input pass — hit zones added first (early children of ##pm_bg).
        // LIFO traversal processes early children last → win ctx->HotKey over panel content.
        BuildDividerHitZones(ctx);

        Drag.HoverNode = nullptr;
        Drag.DropZone  = ZUIDropZone::None; // reset each frame; set by BuildDropZones below
        if (DockTree)
        {
            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            for (uint32_t i = 0; i < PanelCount; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->Hidden)
                {
                    continue;
                }
                float r[4];
                if (!ZUIDockRectForKey(DockTree, p->DockKey, r))
                {
                    continue;
                }

                if (Drag.Active && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                {
                    // Always update HoverNode — including source panel so edge-zone splits
                    // are proposed even when all views are merged into one tab group.
                    Drag.HoverNode = ZUIDockFindLeaf(DockTree, p->DockKey);
                }

                BuildDockedPanel(ctx, p, r);

                if (ctx->MousePressed[0] && !Drag.Active && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                    FocusPanel(i);
            }
        }

        // Status bar
        if (status_h > 0.f && DrawBuiltinStatusBar)
        {
            ZUIBox* sbar      = ZUIBeginRow(ctx, "##pm_sbar", ZPx(sw), ZPx(status_h));
            sbar->Flags       = sbar->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
            sbar->FloatPos[0] = 0.f;
            sbar->FloatPos[1] = sh - status_h;
            ZUIBoxSetColorArr(sbar, ctx->Theme.StatusBarBg);
            sbar->EdgeSoftness = 0.f;

            // Left — engine identity
            ZUISpacer(ctx, 12.f);
            ZUILabel(ctx, "ZEngine", ctx->Theme.TextDefault);
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "Editor", ctx->Theme.TextDim);

            // Fill
            {
                char    fk[] = "##sbf";
                ZUIBox* sf   = ZUIPushBox(ctx, fk, 5, ZUI_None);
                sf->Size[0]  = ZFill();
                sf->Size[1]  = ZPx(status_h);
                ZUIPopBox(ctx);
            }

            // Right — performance + scale
            {
                float fps = (ctx->DeltaTime > 0.0005f && ctx->DeltaTime < 1.f) ? (1.f / ctx->DeltaTime) : 0.f;

                // UIScale
                {
                    char scale_buf[24];
                    snprintf(scale_buf, sizeof(scale_buf), "UIScale %.1f", (double) ctx->UIScale);
                    ZUILabel(ctx, scale_buf, ctx->Theme.TextDim);
                }

                ZUISpacer(ctx, 10.f);
                static const float kSep[4] = {1.f, 1.f, 1.f, 0.25f};
                ZUILabel(ctx, "|", kSep);
                ZUISpacer(ctx, 10.f);

                // FPS — color-coded: normal / warn / error
                if (fps > 0.f)
                {
                    const float* fps_col = (fps >= 55.f) ? ctx->Theme.TextDefault : (fps >= 30.f) ? ctx->Theme.TextWarn : ctx->Theme.TextError;
                    char         fps_buf[24];
                    snprintf(fps_buf, sizeof(fps_buf), "%.0f fps", (double) fps);
                    ZUILabel(ctx, fps_buf, fps_col);
                }
            }
            ZUISpacer(ctx, 14.f);
            ZUIEndRow(ctx);
        }

        // Render pass — visual lines added last (late children of ##pm_bg → drawn on top of panels).
        BuildDividerVisuals(ctx);

        // Drag ghost
        if (Drag.Active)
        {
            Drag.GhostX = ctx->MousePos[0];
            Drag.GhostY = ctx->MousePos[1];

            if (ctx->MouseReleased[0] && Drag.DropZone == ZUIDropZone::None)
            {
                Drag.Active   = false;
                Drag.SrcPanel = nullptr;
            }

            if (Drag.Active)
            {
                ZUIPanel*   sp    = Drag.SrcPanel;
                const char* title = "Panel";
                if (Drag.SrcTabIdx == kWholePanel)
                {
                    if (sp && sp->ViewCount > 0 && sp->Views[0])
                        title = sp->Views[0]->Title;
                }
                else if (sp && Drag.SrcTabIdx < sp->ViewCount && sp->Views[Drag.SrcTabIdx])
                {
                    title = sp->Views[Drag.SrcTabIdx]->Title;
                }

                ZUIDockGhostHeader(ctx, "##drag_ghost", title, Drag.GhostX, Drag.GhostY);
            }
        }

        ZUIEndColumn(ctx);
    }

    // BuildMenuBar

    void ZUIPanelManager::BuildMenuBar(ZUIContext* ctx, float sw, float mh)
    {
        ZUIBox* bar = ZUIBeginRow(ctx, "##pm_menubar", ZPx(sw), ZPx(mh));
        bar->Flags  = bar->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bar, ctx->Theme.MenuBarBg);
        bar->EdgeSoftness    = 0.f;
        bar->BorderColor[0]  = ctx->Theme.Separator[0];
        bar->BorderColor[1]  = ctx->Theme.Separator[1];
        bar->BorderColor[2]  = ctx->Theme.Separator[2];
        bar->BorderColor[3]  = ctx->Theme.Separator[3];
        bar->BorderThickness = 1.f;
        bar->Flags           = bar->Flags | ZUI_DrawBorder;

        ZUISpacer(ctx, 10.f);
        if (ZUIBeginMenu(ctx, "File"))
        {
            ZUIMenuItem(ctx, "New Scene");
            ZUIMenuItem(ctx, "Open Scene...");
            ZUIMenuItem(ctx, "Save Scene");
            ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Quit");
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Edit"))
        {
            ZUIMenuItem(ctx, "Undo");
            ZUIMenuItem(ctx, "Redo");
            ZUISeparator(ctx);
            ZUIMenuItem(ctx, "Preferences...");
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Window"))
        {
            // Reset Layout
            if (ZUIMenuItem(ctx, "Reset Layout"))
            {
                // Unhide all hidden panels and re-insert them into the tree
                for (uint32_t i = 0; i < PanelCount; ++i)
                {
                    ZUIPanel* p = &Panels[i];
                    if (!p->Hidden || p->ViewCount == 0)
                    {
                        continue;
                    }
                    p->Hidden = false;
                    if (DockTree)
                    {
                        ZUIDockNode* target = FindLargestLeaf(DockTree->Root);
                        if (target)
                            ZUIDockSplitH(DockTree, target, 0.5f, target->ContentKey, p->DockKey);
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

            // Panels submenu — fly-out to the right (popup stack supports nested menus)
            if (ZUIBeginSubMenu(ctx, "Panels"))
            {
                static const char*        kPanelNames[]   = {"Hierarchy", "Console", "Inspector", "Viewport"};
                static constexpr uint32_t kPanelNameCount = 4;

                for (uint32_t ni = 0; ni < kPanelNameCount; ++ni)
                {
                    const char* target_name = kPanelNames[ni];

                    ZUIPanel*   match       = nullptr;
                    for (uint32_t i = 0; i < PanelCount; ++i)
                    {
                        ZUIPanel*   p = &Panels[i];
                        const char* t = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "";
                        if (strcmp(t, target_name) == 0)
                        {
                            match = p;
                            break;
                        }
                    }

                    bool visible = match && !match->Hidden;
                    if (ZUIMenuItemEx(ctx, target_name, nullptr, visible) && match)
                    {
                        if (match->Hidden)
                        {
                            match->Hidden = false;
                            if (DockTree)
                            {
                                ZUIDockNode* tgt = FindLargestLeaf(DockTree->Root);
                                if (tgt)
                                    ZUIDockSplitH(DockTree, tgt, 0.5f, tgt->ContentKey, match->DockKey);
                            }
                        }
                        else if (PendingCloseCount < kMaxPanels)
                            PendingCloseKeys[PendingCloseCount++] = match->DockKey;
                        LayoutDirty = true;
                    }
                }
                ZUIEndSubMenu(ctx);
            }
            ZUIEndMenu(ctx);
        }
        ZUISpacer(ctx, 2.f);
        if (ZUIBeginMenu(ctx, "Help"))
        {
            ZUIMenuItem(ctx, "About ZEngine");
            ZUIEndMenu(ctx);
        }
        {
            char    fk[] = "##mb_fill";
            ZUIBox* f    = ZUIPushBox(ctx, fk, 8, ZUI_None);
            f->Size[0]   = ZFill();
            f->Size[1]   = ZPx(mh);
            ZUIPopBox(ctx);
        }
        ZUILabel(ctx, "ZEngine", ctx->Theme.TextDim);
        ZUISpacer(ctx, 12.f);
        ZUIEndRow(ctx);
    }

    // BuildDockedPanel

    void ZUIPanelManager::BuildDockedPanel(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        if (!p || p->ViewCount == 0)
        {
            return;
        }
        float        header_h   = ZUIGetFrameHeight(ctx); // 19px

        // Central node check: read IsCentral from the dock node, not a manager key
        ZUIDockNode* leaf       = ZUIDockFindLeaf(DockTree, p->DockKey);
        bool         is_central = leaf && leaf->IsCentral;

        if (is_central)
        {
            // Pure passthrough — multi-tab gets minimal tab bar, single gets nothing
            if (p->ViewCount > 1)
            {
                float tab_rect[4] = {rect[0], rect[1], rect[2], rect[1] + header_h};
                BuildTabBar(ctx, p, tab_rect);
                float         cr[4] = {rect[0], rect[1] + header_h, rect[2], rect[3]};
                ZUIPanelView* view  = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                if (view)
                {
                    view->BuildContent(ctx, cr);
                }
            }
            else
            {
                ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                if (view)
                {
                    view->BuildContent(ctx, rect);
                }
            }
            return;
        }

        bool is_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx)
            {
                is_focused = true;
                break;
            }

        char panel_key[32];
        snprintf(panel_key, sizeof(panel_key), "##panel_%llx", (unsigned long long) p->DockKey);

        ZUIBox* panel      = ZUIBeginColumn(ctx, panel_key, ZPx(rect[2] - rect[0]), ZPx(rect[3] - rect[1]));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = rect[0];
        panel->FloatPos[1] = rect[1];
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->BorderColor[0]  = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1]  = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2]  = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3]  = ctx->Theme.PanelBorder[3];
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // AutoHideTabBar: use node flag, defaulting to false if no node found
        // (false = ImGui default: always show tab bar even for single-view nodes)
        bool auto_hide         = leaf ? leaf->AutoHideTabBar : false;
        bool show_tabs         = (p->ViewCount > 1) || !auto_hide;

        if (show_tabs)
        {
            float tab_rect[4] = {rect[0], rect[1], rect[2], rect[1] + header_h};
            BuildTabBar(ctx, p, tab_rect);
        }
        else
        {
            // VS Code single-view title strip — style-driven
            const char* view_title = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "Panel";
            float       btn_h      = ctx->Style.FontSize; // was header_h * 0.60

            char        hk[48];
            snprintf(hk, sizeof(hk), "##tbar_%llx", (unsigned long long) p->DockKey);
            ZUIBox* strip = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
            strip->Flags  = strip->Flags | ZUI_DrawBackground | ZUI_Clickable;
            // Background: focus-aware
            ZUIBoxSetColorArr(strip, is_focused ? ctx->Theme.TitleBgActive : ctx->Theme.TitleBarBg);
            strip->EdgeSoftness = 0.f;

            // Left padding from style
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));
            // Icon dot — size from style
            {
                const float* ic = (p->Views[0] && p->Views[0]->TabColor[3] > 0.01f) ? p->Views[0]->TabColor : ctx->Theme.PanelFocusBorder;
                char         ik[48];
                snprintf(ik, sizeof(ik), "##tic_%llx", (unsigned long long) p->DockKey);
                float   icon_sz = ctx->Style.TabIconSize;
                ZUIBox* icon    = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawBackground);
                icon->Size[0]   = ZPx(icon_sz);
                icon->Size[1]   = ZPx(icon_sz);
                ZUIBoxSetColorArr(icon, ic);
                ZUIBoxSetCornerRadius(icon, icon_sz * 0.5f);
                icon->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
            }
            // Icon-to-label gap from style
            ZUISpacer(ctx, ZUIGetInnerSpac(ctx));
            ZUILabel(ctx, view_title, ctx->Theme.TextDefault);

            // Fill
            {
                char fk[48];
                snprintf(fk, sizeof(fk), "##tf_%llx", (unsigned long long) p->DockKey);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZPx(header_h);
                ZUIPopBox(ctx);
            }

            // x close (hover-only) — suppressed for non-closeable panels (e.g. main viewport)
            bool should_close = false;
            bool ph           = p->Closeable && (ctx->MousePos[0] >= rect[0] && ctx->MousePos[0] <= rect[2] && ctx->MousePos[1] >= rect[1] && ctx->MousePos[1] <= rect[1] + header_h);
            if (ph)
            {
                char xk[56];
                snprintf(xk, sizeof(xk), "x##tx_%llx", (unsigned long long) p->DockKey);
                ZUIBox* xb       = ZUIPushBox(ctx, xk, (uint32_t) strlen(xk), ZUI_DrawText | ZUI_Clickable);
                xb->Size[0]      = ZPx(btn_h);
                xb->Size[1]      = ZPx(btn_h);
                xb->TextAlign    = ZUITextAlign::Center;
                bool xh          = (ctx->HotKey == xb->Key);
                xb->TextColor[0] = xh ? 1.f : 0.55f;
                xb->TextColor[1] = xh ? 0.4f : 0.55f;
                xb->TextColor[2] = xh ? 0.4f : 0.55f;
                xb->TextColor[3] = 1.f;
                ZUISignal xs     = ZUISignalFromBox(ctx, xb);
                ZUIPopBox(ctx);
                if (xs.Flags & ZUI_SignalClicked)
                    should_close = true;
            }
            // Right spacer from style
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));

            ZUISignal strip_sig = ZUISignalFromBox(ctx, strip);
            ZUIEndRow(ctx);

            // Record press start
            if (strip_sig.Flags & ZUI_SignalPressed)
            {
                Drag.StartX = ctx->MousePos[0];
                Drag.StartY = ctx->MousePos[1];
            }

            // Drag threshold from style
            if (!should_close && (strip_sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float dx = fabsf(ctx->MousePos[0] - Drag.StartX);
                float dy = fabsf(ctx->MousePos[1] - Drag.StartY);
                if (dx + dy > ctx->Style.DockingDragThreshold)
                {
                    Drag.Active    = true;
                    Drag.SrcPanel  = p;
                    Drag.SrcTabIdx = kWholePanel;
                    Drag.GhostX    = ctx->MousePos[0];
                    Drag.GhostY    = ctx->MousePos[1];
                }
            }

            // Close already handled by PreDetectCloseEvents pre-pass.
            (void) should_close;
        }

        // Content — no WindowPadding for docked panels (editor panels fill edge-to-edge).
        // WindowPadding will be applied when floating windows are implemented.
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view)
        {
            ZUIBox* content       = ZUIBeginColumn(ctx, "##dc", ZFill(), ZFill());
            content->Flags        = content->Flags | ZUI_ClipChildren;
            content->EdgeSoftness = 0.f;
            float cr[4]           = {rect[0], rect[1] + header_h, rect[2], rect[3]};
            view->BuildContent(ctx, cr);
            ZUIEndColumn(ctx);
        }

        ZUIEndColumn(ctx);

        // Optional left-edge focus strip (ZUIStyle.ShowFocusBorder).
        // Off by default — the active tab's overline already indicates focus.
        if (is_focused && ctx->Style.ShowFocusBorder)
        {
            char fk[48];
            snprintf(fk, sizeof(fk), "##pfocus_%llx", (unsigned long long) p->DockKey);
            ZUIBox* fb      = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            fb->Size[0]     = ZPx(ctx->Style.DockingFocusBorderWidth);
            fb->Size[1]     = ZPx(rect[3] - rect[1]);
            fb->FloatPos[0] = rect[0];
            fb->FloatPos[1] = rect[1];
            ZUIBoxSetColorArr(fb, ctx->Theme.PanelFocusBorder);
            fb->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        // Drop zones — edge zones shown even on source panel (allows splitting a merged tab group)
        if (Drag.Active && Drag.HoverNode && Drag.HoverNode->ContentKey == p->DockKey)
            BuildDropZones(ctx, p, rect);
    }

    // BuildTabBar

    void ZUIPanelManager::BuildTabBar(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float tab_h = ZUIGetFrameHeight(ctx);
        char  bar_key[40];
        snprintf(bar_key, sizeof(bar_key), "##tabbar_%llx", (unsigned long long) p->DockKey);

        bool panel_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx)
            {
                panel_focused = true;
                break;
            }
        const float* bar_bg = panel_focused ? ctx->Theme.TitleBgActive : ctx->Theme.TitleBarBg;

        ZUIBox*      bar    = ZUIBeginRow(ctx, bar_key, ZFill(), ZPx(tab_h));
        bar->Flags          = bar->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_Scrollable | ZUI_ClipChildren;
        ZUIBoxSetColorArr(bar, bar_bg);
        bar->EdgeSoftness = 0.f;

        ZUISpacer(ctx, ZUIGetFramePadX(ctx));

        for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
        {
            ZUIPanelView* view = p->Views[ti];
            if (!view)
            {
                continue;
            }
            bool is_active   = (ti == p->ActiveTab);
            bool tab_hovered = false; // determined after box push (uses previous-frame HotKey)

            char tab_key[64];
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%u", (unsigned long long) p->DockKey, ti);
            ZUIBox* tab       = ZUIBeginRow(ctx, tab_key, ZFit(), ZPx(tab_h));
            tab->Flags        = tab->Flags | ZUI_DrawBackground | ZUI_Clickable;
            tab->EdgeSoftness = 0.f;
            // Vertical padding: centers label + close button (FramePadding.y top and bottom)
            float fpy         = ZUIGetFramePadY(ctx);
            tab->Padding[1]   = fpy;
            tab->Padding[3]   = fpy;
            ZUIBoxSetTopRadius(tab, ctx->Style.TabRounding);

            tab_hovered = (ctx->HotKey == tab->Key);

            // 4-state color
            if (is_active && panel_focused)
                ZUIBoxSetColorArr(tab, ctx->Theme.TabActiveBg);
            else if (is_active && !panel_focused)
                ZUIBoxSetColorArr(tab, ctx->Theme.TabDimmedSelectedBg);
            else
            {
                const float* rest = panel_focused ? ctx->Theme.TabInactiveBg : ctx->Theme.TabDimmedBg;
                ZUIBoxSetColorArr(tab, tab_hovered ? ctx->Theme.TabHoveredBg : rest);
            }

            // Label
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));
            {
                uint32_t tlen     = (uint32_t) strlen(view->Title);
                ZUIBox*  lbl      = ZUIPushBox(ctx, view->Title, tlen, ZUI_DrawText);
                lbl->Size[0]      = ZText();
                lbl->Size[1]      = ZFill();
                lbl->TextColor[0] = is_active ? ctx->Theme.TextDefault[0] : ctx->Theme.TextDim[0];
                lbl->TextColor[1] = is_active ? ctx->Theme.TextDefault[1] : ctx->Theme.TextDim[1];
                lbl->TextColor[2] = is_active ? ctx->Theme.TextDefault[2] : ctx->Theme.TextDim[2];
                lbl->TextColor[3] = 1.f;
                ZUIPopBox(ctx);
            }

            // Close button — space ALWAYS reserved so tab width is stable on hover.
            // Pre-compute hash for hover detection before box creation.
            bool tab_closed = false;
            {
                float btn_sz     = ctx->Style.FontSize;
                bool  show_close = p->Closeable && (is_active || tab_hovered);
                ZUISpacer(ctx, ZUIGetInnerSpac(ctx));

                char xkey[64];
                snprintf(xkey, sizeof(xkey), "x##x_%llx_%u", (unsigned long long) p->DockKey, ti);
                uint64_t    xhash  = ZUIHashStr(xkey, (uint32_t) strlen(xkey));
                bool        xh     = show_close && (ctx->HotKey == xhash || ctx->ActiveKey == xhash);

                ZUIBoxFlags xflags = ZUI_None;
                if (show_close)
                    xflags = (ZUIBoxFlags) (ZUI_DrawText | ZUI_Clickable | (xh ? ZUI_DrawBackground : 0));
                ZUIBox* xbtn    = ZUIPushBox(ctx, xkey, (uint32_t) strlen(xkey), xflags);
                xbtn->Size[0]   = ZPx(btn_sz);
                xbtn->Size[1]   = ZFill();
                xbtn->TextAlign = ZUITextAlign::Center;
                if (show_close)
                {
                    if (xh)
                    {
                        // Hovered: red fill + white ×
                        ZUIBoxSetColor(xbtn, 0.65f, 0.15f, 0.15f, 0.80f);
                        ZUIBoxSetCornerRadius(xbtn, ctx->Style.FrameRounding);
                        xbtn->TextColor[0] = 1.f;
                        xbtn->TextColor[1] = 1.f;
                        xbtn->TextColor[2] = 1.f;
                        xbtn->TextColor[3] = 1.f;
                    }
                    else
                    {
                        // Visible default: brighter than before
                        float a            = is_active ? 0.70f : 0.50f;
                        xbtn->TextColor[0] = a;
                        xbtn->TextColor[1] = a;
                        xbtn->TextColor[2] = a;
                        xbtn->TextColor[3] = 1.f;
                    }
                }
                ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
                ZUIPopBox(ctx);
                if (show_close && (xsig.Flags & ZUI_SignalClicked))
                {
                    tab_closed = true;
                }
                ZUISpacer(ctx, ZUIGetFramePadX(ctx));
            }

            // Overline — 2px accent strip built INSIDE the active+focused tab so it
            // inherits the exact tab width/position with no separate approximation.
            // FloatPos is relative to the tab box's ScreenMin (parent-relative per convention).
            if (is_active && panel_focused)
            {
                char ok[64];
                snprintf(ok, sizeof(ok), "##tov_%llx_%u", (unsigned long long) p->DockKey, ti);
                ZUIBox* ov      = ZUIPushBox(ctx, ok, (uint32_t) strlen(ok), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                ov->Size[0]     = {ZUISizeKind::ParentPercent, 1.f, 1.f}; // full tab width
                ov->Size[1]     = ZPx(ctx->Style.TabBarOverlineSize);     // 2px
                ov->FloatPos[0] = 0.f;                                    // tab-relative: left edge
                ov->FloatPos[1] = 0.f;                                    // tab-relative: top edge (outside 3px padding)
                ZUIBoxSetColorArr(ov, ctx->Theme.TabActiveBorder);
                ov->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);

            // Close already handled by PreDetectCloseEvents pre-pass.
            // Nothing to do here — just break to exit the tab loop cleanly.
            if (tab_closed)
            {
                break;
            }

            if (!tab_closed && (sig.Flags & ZUI_SignalClicked))
            {
                if (p->ActiveTab != ti)
                {
                    LayoutDirty = true;
                }
                p->ActiveTab = ti;
                for (uint32_t pi = 0; pi < PanelCount; ++pi)
                    if (&Panels[pi] == p)
                    {
                        FocusPanel(pi);
                        break;
                    }
            }

            if (sig.Flags & ZUI_SignalPressed)
            {
                Drag.StartX      = ctx->MousePos[0];
                Drag.StartY      = ctx->MousePos[1];
                p->ReorderActive = false;
                p->ReorderAccumX = 0.f;
            }

            if (!tab_closed && (sig.Flags & ZUI_SignalHeld) && !Drag.Active)
            {
                float dx    = fabsf(ctx->MousePos[0] - Drag.StartX);
                float dy    = fabsf(ctx->MousePos[1] - Drag.StartY);
                float total = dx + dy;
                if (total > ctx->Style.DockingTabReorderThreshold)
                {
                    bool mostly_horiz = (dx > dy * 1.5f);
                    if (mostly_horiz && p->ViewCount > 1)
                    {
                        if (!p->ReorderActive)
                        {
                            p->ReorderActive = true;
                            p->ReorderTabIdx = ti;
                            p->ReorderAccumX = 0.f;
                        }
                        if (p->ReorderActive && p->ReorderTabIdx == ti)
                        {
                            p->ReorderAccumX += sig.DragDelta[0];
                            float tab_slot    = (rect[2] - rect[0]) / (float) p->ViewCount;
                            if (tab_slot < ctx->Style.DockingMinTabWidth)
                                tab_slot = ctx->Style.DockingMinTabWidth;
                            float threshold = tab_slot * 0.5f;
                            if (p->ReorderAccumX > threshold && ti + 1 < p->ViewCount)
                            {
                                ZUIPanelView* tmp = p->Views[ti];
                                p->Views[ti]      = p->Views[ti + 1];
                                p->Views[ti + 1]  = tmp;
                                if (p->ActiveTab == ti)
                                    p->ActiveTab = ti + 1;
                                else if (p->ActiveTab == ti + 1)
                                    p->ActiveTab = ti;
                                p->ReorderTabIdx  = ti + 1;
                                p->ReorderAccumX -= threshold;
                                break;
                            }
                            else if (p->ReorderAccumX < -threshold && ti > 0)
                            {
                                ZUIPanelView* tmp = p->Views[ti];
                                p->Views[ti]      = p->Views[ti - 1];
                                p->Views[ti - 1]  = tmp;
                                if (p->ActiveTab == ti)
                                    p->ActiveTab = ti - 1;
                                else if (p->ActiveTab == ti - 1)
                                    p->ActiveTab = ti;
                                p->ReorderTabIdx  = ti - 1;
                                p->ReorderAccumX += threshold;
                                break;
                            }
                        }
                    }
                    else if (!mostly_horiz || dy > ctx->Style.DockingUndockVertical)
                    {
                        p->ReorderActive = false;
                        Drag.Active      = true;
                        Drag.SrcPanel    = p;
                        Drag.SrcTabIdx   = ti;
                        Drag.GhostX      = ctx->MousePos[0];
                        Drag.GhostY      = ctx->MousePos[1];
                    }
                }
            }

            if (ctx->MouseReleased[0] && p->ReorderActive)
            {
                p->ReorderActive = false;
                p->ReorderAccumX = 0.f;
            }

            // Inter-tab spacing from style
            ZUISpacer(ctx, ZUIGetInnerSpac(ctx));
        }

        // Bar signal — drag empty space = whole-panel drag
        // Scroll arrows — appear when tabs overflow the bar width.
        // Floated children of the bar so they pin to bar edges regardless of ScrollX.
        {
            ZUIPersistentState* bps = ZUIStateGetOrInsert(&ctx->StateStore, bar->Key);
            if (bps && bps->MaxScrollX > 1.f)
            {
                float        bar_w   = rect[2] - rect[0];
                float        arrow_w = tab_h; // square arrow button
                const float* dim     = ctx->Theme.TextDim;

                // Left arrow
                if (bps->ScrollX > 0.5f)
                {
                    char lk[64];
                    snprintf(lk, sizeof(lk), "##tsl_%llx", (unsigned long long) p->DockKey);
                    ZUIBox* la      = ZUIPushBox(ctx, lk, (uint32_t) strlen(lk), ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY);
                    la->Size[0]     = ZPx(arrow_w);
                    la->Size[1]     = ZPx(tab_h);
                    la->FloatPos[0] = 0.f;
                    la->FloatPos[1] = 0.f;
                    la->TextAlign   = ZUITextAlign::Center;
                    ZUIBoxSetColorArr(la, ctx->Theme.TitleBgActive);
                    la->Label        = ZUIPushStr(&ctx->FrameArena, "<", 1);
                    la->TextColor[0] = dim[0];
                    la->TextColor[1] = dim[1];
                    la->TextColor[2] = dim[2];
                    la->TextColor[3] = 1.f;
                    ZUISignal lsig   = ZUISignalFromBox(ctx, la);
                    ZUIPopBox(ctx);
                    if (lsig.Flags & ZUI_SignalClicked)
                    {
                        bps->ScrollX -= tab_h * 3.f;
                        if (bps->ScrollX < 0.f)
                            bps->ScrollX = 0.f;
                    }
                }

                // Right arrow
                if (bps->ScrollX < bps->MaxScrollX - 0.5f)
                {
                    char rk[64];
                    snprintf(rk, sizeof(rk), "##tsr_%llx", (unsigned long long) p->DockKey);
                    ZUIBox* ra      = ZUIPushBox(ctx, rk, (uint32_t) strlen(rk), ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY);
                    ra->Size[0]     = ZPx(arrow_w);
                    ra->Size[1]     = ZPx(tab_h);
                    ra->FloatPos[0] = bar_w - arrow_w;
                    ra->FloatPos[1] = 0.f;
                    ra->TextAlign   = ZUITextAlign::Center;
                    ZUIBoxSetColorArr(ra, ctx->Theme.TitleBgActive);
                    ra->Label        = ZUIPushStr(&ctx->FrameArena, ">", 1);
                    ra->TextColor[0] = dim[0];
                    ra->TextColor[1] = dim[1];
                    ra->TextColor[2] = dim[2];
                    ra->TextColor[3] = 1.f;
                    ZUISignal rsig   = ZUISignalFromBox(ctx, ra);
                    ZUIPopBox(ctx);
                    if (rsig.Flags & ZUI_SignalClicked)
                    {
                        bps->ScrollX += tab_h * 3.f;
                        if (bps->ScrollX > bps->MaxScrollX)
                            bps->ScrollX = bps->MaxScrollX;
                    }
                }
            }
        }

        ZUISignal bar_sig = ZUISignalFromBox(ctx, bar);
        if (bar_sig.Flags & ZUI_SignalPressed)
        {
            Drag.StartX = ctx->MousePos[0];
            Drag.StartY = ctx->MousePos[1];
        }
        if ((bar_sig.Flags & ZUI_SignalHeld) && !Drag.Active)
        {
            float dx = fabsf(ctx->MousePos[0] - Drag.StartX), dy = fabsf(ctx->MousePos[1] - Drag.StartY);
            if (dx + dy > ctx->Style.DockingDragThreshold)
            {
                float panel_r[4] = {};
                if (!ZUIDockRectForKey(DockTree, p->DockKey, panel_r))
                {
                    panel_r[0] = rect[0];
                    panel_r[1] = rect[1];
                    panel_r[2] = rect[2];
                    panel_r[3] = rect[3] + 200.f;
                }
                Drag.Active    = true;
                Drag.SrcPanel  = p;
                Drag.SrcTabIdx = kWholePanel;
                Drag.GhostX    = ctx->MousePos[0];
                Drag.GhostY    = ctx->MousePos[1];
            }
        }

        ZUIEndRow(ctx);
    }

    // BuildDropZones — position-based ImGui-style subdivision
    // Zone is determined by WHERE the mouse is within the target panel,
    // not by hovering explicit indicator widgets.  Only a split preview
    // overlay is drawn — no directional arrows, no target boxes.

    // Coordinate convention for all overlay boxes in ZUIPanelManager:
    //
    //   Parent         FloatPos convention
    //   ##pm_bg        absolute screen coords  (ScreenMin=0,0 → FloatPos == screen pos)
    //   panel column   panel-relative coords   (ScreenMin=rect[0,1] → FloatPos offset from panel origin)
    //
    // Elements and their parent:
    //   Drop zone preview  → ##pm_bg  (absolute)
    //   Dividers           → ##pm_bg  (absolute)
    //   Focus strip        → ##pm_bg  (absolute)
    //   Drag ghost         → ##pm_bg  (absolute)
    //   Overline           → panel column (panel-relative, inside BuildTabBar before ZUIEndRow)
    //
    // BuildDropZones is called AFTER ZUIEndColumn for the panel box, so
    // ctx->Current == ##pm_bg here. All FloatPos values must be absolute screen coords.
    void ZUIPanelManager::BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4])
    {
        float       w            = rect[2] - rect[0];
        float       h            = rect[3] - rect[1];
        float       mx           = ctx->MousePos[0];
        float       my           = ctx->MousePos[1];
        float       rx           = (w > 0.f) ? (mx - rect[0]) / w : 0.5f; // 0..1 across width
        float       ry           = (h > 0.f) ? (my - rect[1]) / h : 0.5f; // 0..1 down height

        float       tab_h        = ZUIGetFrameHeight(ctx);
        bool        over_tab_bar = (my >= rect[1] && my <= rect[1] + tab_h);
        // When dragging over the source panel itself, only edge splits are valid —
        // center zone would merge with self. Tab bar on source panel also uses edge detection.
        bool        is_src       = (Drag.SrcPanel == p);

        ZUIDropZone zone;
        if (over_tab_bar && !is_src)
        {
            // Dropping on another panel's tab bar: tab merge with insertion index.
            zone                = ZUIDropZone::Center;

            uint32_t insert_idx = p->ViewCount; // default: append at end
            float    insert_x   = rect[2];      // default: right edge of tab bar

            for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
            {
                char tk[64];
                snprintf(tk, sizeof(tk), "##tab_%llx_%u", (unsigned long long) p->DockKey, ti);
                uint64_t            thash = ZUIHashStr(tk, (uint32_t) strlen(tk));
                ZUIPersistentState* ps    = ZUIStateGetOrInsert(&ctx->StateStore, thash);

                // ps valid and has been laid out (non-zero width from a previous frame)
                if (ps && ps->ScreenMaxX > ps->ScreenMinX)
                {
                    float tab_mid = (ps->ScreenMinX + ps->ScreenMaxX) * 0.5f;
                    if (mx < tab_mid)
                    {
                        insert_idx = ti;
                        insert_x   = ps->ScreenMinX;
                        break;
                    }
                    // Gap between this tab and the next
                    insert_x = ps->ScreenMaxX + ZUIGetInnerSpac(ctx) * 0.5f;
                }
            }
            Drag.DropTabInsertIdx = insert_idx;

            // Draw VS Code-style 2px white vertical insertion bar.
            char ik[48];
            snprintf(ik, sizeof(ik), "##dz_ins_%llx", (unsigned long long) p->DockKey);
            ZUIBox* ins      = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            ins->Size[0]     = ZPx(2.f);
            ins->Size[1]     = ZPx(tab_h);
            ins->FloatPos[0] = insert_x - 1.f; // absolute screen coords (##pm_bg parent)
            ins->FloatPos[1] = rect[1];
            ZUIBoxSetColor(ins, 1.f, 1.f, 1.f, 0.90f);
            ins->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }
        else
        {
            Drag.DropTabInsertIdx = kWholePanel;
            float kEdge           = ctx->Style.DockingDropZoneEdge;
            if (rx < kEdge)
                zone = ZUIDropZone::Left;
            else if (rx > 1.f - kEdge)
                zone = ZUIDropZone::Right;
            else if (ry < kEdge)
                zone = ZUIDropZone::Top;
            else if (ry > 1.f - kEdge)
                zone = ZUIDropZone::Bottom;
            else
                zone = is_src ? ZUIDropZone::None // no self-center merge
                              : ZUIDropZone::Center;

            // Teal overlay for split/merge preview (skipped when center zone blocked on src panel)
            float px0 = rect[0], py0 = rect[1], px1 = rect[2], py1 = rect[3];
            if (zone == ZUIDropZone::None)
            {
                Drag.DropZone = zone;
                return;
            }
            switch (zone)
            {
                case ZUIDropZone::Left:
                    px1 = rect[0] + w * 0.5f;
                    break;
                case ZUIDropZone::Right:
                    px0 = rect[0] + w * 0.5f;
                    break;
                case ZUIDropZone::Top:
                    py1 = rect[1] + h * 0.5f;
                    break;
                case ZUIDropZone::Bottom:
                    py0 = rect[1] + h * 0.5f;
                    break;
                default:
                    break;
            }
            char pk[40];
            snprintf(pk, sizeof(pk), "##dz_prev_%llx", (unsigned long long) p->DockKey);
            ZUIDropZoneFill(ctx, pk, px0, py0, px1 - px0, py1 - py0);
        }

        Drag.DropZone = zone;

        if (ctx->MouseReleased[0] && zone != ZUIDropZone::None && Drag.SrcPanel)
        {
            ZUIDockNode* dst_node = ZUIDockFindLeaf(DockTree, p->DockKey);
            if (dst_node)
                CommitDrop(Drag.SrcPanel, Drag.SrcTabIdx, dst_node, zone);
            Drag.Active           = false;
            Drag.SrcPanel         = nullptr;
            Drag.DropZone         = ZUIDropZone::None;
            Drag.HoverNode        = nullptr;
            Drag.DropTabInsertIdx = kWholePanel;
        }
    }

    // BuildDividers — dynamic from dock tree

    // Input pass — added FIRST to ##pm_bg so hit zones are last-processed by the LIFO
    // interaction traversal → always win ctx->HotKey over any panel content underneath.
    void ZUIPanelManager::BuildDividerHitZones(ZUIContext* ctx)
    {
        if (!DockTree)
            return;
        float grab_half = ctx->Style.DockingGrabWidth * 0.5f;
        float band_w    = ctx->Style.DockingHoverBandWidth;

        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            ZUIDockNode* snode = m_split_dividers[di].Node;
            if (!snode || !snode->First || !snode->First->Next)
                continue;
            ZUIDockNode* child1     = snode->First;
            bool         horizontal = (snode->SplitAxis == ZUIAxis::Y);
            float        dx0, dy0, dx1, dy1;
            if (!horizontal)
            {
                float ex = child1->RectMax[0];
                dx0      = ex - grab_half;
                dy0      = snode->RectMin[1];
                dx1      = ex + grab_half;
                dy1      = snode->RectMax[1];
            }
            else
            {
                float ey = child1->RectMax[1];
                dx0      = snode->RectMin[0];
                dy0      = ey - grab_half;
                dx1      = snode->RectMax[0];
                dy1      = ey + grab_half;
            }
            ZUISignal ha_sig = {};

            if (band_w > 0.f)
            {
                float bx0, by0, bx1, by1;
                if (!horizontal)
                {
                    float cx = (dx0 + dx1) * 0.5f;
                    bx0      = cx - band_w * 0.5f;
                    by0      = dy0;
                    bx1      = cx + band_w * 0.5f;
                    by1      = dy1;
                }
                else
                {
                    float cy = (dy0 + dy1) * 0.5f;
                    bx0      = dx0;
                    by0      = cy - band_w * 0.5f;
                    bx1      = dx1;
                    by1      = cy + band_w * 0.5f;
                }
                char hk[32];
                snprintf(hk, sizeof(hk), "##sdivh_%u", di);
                ZUIBox* ha       = ZUIPushBox(ctx, hk, (uint32_t) strlen(hk), ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY);
                ha->Size[0]      = ZPx(bx1 - bx0);
                ha->Size[1]      = ZPx(by1 - by0);
                ha->FloatPos[0]  = bx0;
                ha->FloatPos[1]  = by0;
                ha->EdgeSoftness = 0.f;
                ZUIBoxSetColor(ha, 0.f, 0.f, 0.f, 0.f);
                ha_sig = ZUISignalFromBox(ctx, ha);
                ZUIPopBox(ctx);
            }

            bool& dragging = m_split_dividers[di].Dragging;
            if (ha_sig.Flags & ZUI_SignalPressed)
                dragging = true;
            if (ctx->MouseReleased[0] && dragging)
            {
                dragging    = false;
                LayoutDirty = true;
            }
            if (ha_sig.Flags & ZUI_SignalHeld)
            {
                float delta = horizontal ? ha_sig.DragDelta[1] : ha_sig.DragDelta[0];
                if (delta != 0.f)
                    ZUIDockResize(DockTree, child1, delta);
            }

            // Cursor — use in_rect for immediate feedback (signal has 1-frame lag)
            float mx = ctx->MousePos[0], my = ctx->MousePos[1];
            if ((mx >= dx0 && mx <= dx1 && my >= dy0 && my <= dy1) || dragging)
                ctx->ResizeCursor = horizontal ? 2 : 1;
        }
    }

    // Render pass — added LAST to ##pm_bg so the visual line is always drawn on top of panels.
    void ZUIPanelManager::BuildDividerVisuals(ZUIContext* ctx)
    {
        if (!DockTree)
            return;
        float grab_half = ctx->Style.DockingGrabWidth * 0.5f;
        float mx = ctx->MousePos[0], my = ctx->MousePos[1];

        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            ZUIDockNode* snode = m_split_dividers[di].Node;
            if (!snode || !snode->First || !snode->First->Next)
                continue;
            ZUIDockNode* child1     = snode->First;
            bool         horizontal = (snode->SplitAxis == ZUIAxis::Y);
            float        dx0, dy0, dx1, dy1;
            if (!horizontal)
            {
                float ex = child1->RectMax[0];
                dx0      = ex - grab_half;
                dy0      = snode->RectMin[1];
                dx1      = ex + grab_half;
                dy1      = snode->RectMax[1];
            }
            else
            {
                float ey = child1->RectMax[1];
                dx0      = snode->RectMin[0];
                dy0      = ey - grab_half;
                dx1      = snode->RectMax[0];
                dy1      = ey + grab_half;
            }
            (void) child1;

            bool  dragging  = m_split_dividers[di].Dragging;
            bool  in_rect   = (mx >= dx0 && mx <= dx1 && my >= dy0 && my <= dy1);
            bool  highlight = in_rect || dragging;

            float lw        = highlight ? ctx->Style.DockingSeparatorSize : ctx->Style.DockingSeparatorSizeRest;
            float vc[4]     = {ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1], ctx->Theme.TabActiveBorder[2], highlight ? 1.f : 0.35f};
            if (!highlight)
            {
                vc[0] = ctx->Theme.Separator[0];
                vc[1] = ctx->Theme.Separator[1];
                vc[2] = ctx->Theme.Separator[2];
            }

            char vk[32];
            snprintf(vk, sizeof(vk), "##sdiv_%u", di);
            ZUIBox* vis = ZUIPushBox(ctx, vk, (uint32_t) strlen(vk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            if (!horizontal)
            {
                vis->Size[0]     = ZPx(lw);
                vis->Size[1]     = ZPx(dy1 - dy0);
                vis->FloatPos[0] = (dx0 + dx1) * 0.5f - lw * 0.5f;
                vis->FloatPos[1] = dy0;
            }
            else
            {
                vis->Size[0]     = ZPx(dx1 - dx0);
                vis->Size[1]     = ZPx(lw);
                vis->FloatPos[0] = dx0;
                vis->FloatPos[1] = (dy0 + dy1) * 0.5f - lw * 0.5f;
            }
            vis->EdgeSoftness = 0.f;
            ZUIBoxSetColorArr(vis, vc);
            ZUIPopBox(ctx);
        }
    }

    // CommitDrop — moves a tab (or whole panel) to a new dock position

    void ZUIPanelManager::CommitDrop(ZUIPanel* src, uint32_t tab_idx, ZUIDockNode* dst, ZUIDropZone zone)
    {
        if (!src || !dst)
            return;
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
                    uint32_t ins = Drag.DropTabInsertIdx;
                    for (uint32_t i = 0; i < src->ViewCount; ++i)
                        InsertViewAt(dst_panel, src->Views[i], ins == kWholePanel ? ins : ins + i);
                    src->ViewCount        = 0;
                    src->Hidden           = true;
                    ZUIDockNode* src_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
                    if (src_leaf)
                        ZUIDockCollapseLeaf(DockTree, src_leaf);
                }
            }
            else
            {
                if (dst->ContentKey == src->DockKey)
                    return; // cannot dock panel into itself
                // Split and move panel to new slot
                // First collapse source to free its slot
                ZUIDockNode* src_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
                if (src_leaf)
                    ZUIDockCollapseLeaf(DockTree, src_leaf);
                // Re-find destination after potential tree change
                ZUIDockNode* new_dst = ZUIDockFindLeaf(DockTree, dst->ContentKey);
                if (!new_dst)
                {
                    src->Hidden = true;
                    return;
                } // tree changed under us — orphan safely
                {
                    float split_pct = 0.5f;
                    if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
                    {
                        bool left = (zone == ZUIDropZone::Left);
                        ZUIDockSplitH(DockTree, new_dst, left ? split_pct : 1.f - split_pct, left ? src->DockKey : new_dst->ContentKey, left ? new_dst->ContentKey : src->DockKey);
                    }
                    else
                    {
                        bool top = (zone == ZUIDropZone::Top);
                        ZUIDockSplitV(DockTree, new_dst, top ? split_pct : 1.f - split_pct, top ? src->DockKey : new_dst->ContentKey, top ? new_dst->ContentKey : src->DockKey);
                    }
                }
            }
            return;
        }

        // Single tab move
        if (tab_idx >= src->ViewCount)
            return;
        ZUIPanelView* view = src->Views[tab_idx];

        if (zone == ZUIDropZone::Center)
        {
            ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
            if (dst_panel && dst_panel != src)
            {
                uint32_t ins = Drag.DropTabInsertIdx;
                InsertViewAt(dst_panel, view, ins);
                // Focus the newly inserted tab in the destination panel.
                dst_panel->ActiveTab = (ins == kWholePanel || ins >= dst_panel->ViewCount) ? dst_panel->ViewCount - 1 : ins;
                for (uint32_t i = tab_idx; i + 1 < src->ViewCount; ++i)
                    src->Views[i] = src->Views[i + 1];
                --src->ViewCount;
                if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0)
                    src->ActiveTab = src->ViewCount - 1;
                if (src->ViewCount == 0)
                {
                    ZUIDockNode* src_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
                    if (src_leaf)
                        ZUIDockCollapseLeaf(DockTree, src_leaf);
                    src->Hidden = true;
                }
            }
        }
        else
        {
            float     split_pct = 0.5f;
            // Use a session-unique key: avoids collisions with existing panels that
            // share the same title (e.g. dragging "Hierarchy" tab to an edge zone
            // would otherwise collide with the existing Hierarchy panel's DockKey).
            uint64_t  new_key   = ++DragKeySeq;

            // Check capacity BEFORE mutating the tree — orphaned leaf otherwise.
            ZUIPanel* new_panel = AddPanel(new_key);
            if (!new_panel)
                return;

            // Now it's safe to split dst.
            if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
            {
                bool left = (zone == ZUIDropZone::Left);
                ZUIDockSplitH(DockTree, dst, left ? split_pct : 1.f - split_pct, left ? new_key : dst->ContentKey, left ? dst->ContentKey : new_key);
            }
            else
            {
                bool top = (zone == ZUIDropZone::Top);
                ZUIDockSplitV(DockTree, dst, top ? split_pct : 1.f - split_pct, top ? new_key : dst->ContentKey, top ? dst->ContentKey : new_key);
            }

            AddView(new_panel, view);
            for (uint32_t i = tab_idx; i + 1 < src->ViewCount; ++i)
                src->Views[i] = src->Views[i + 1];
            --src->ViewCount;
            if (src->ActiveTab >= src->ViewCount && src->ViewCount > 0)
                src->ActiveTab = src->ViewCount - 1;
            if (src->ViewCount == 0)
            {
                ZUIDockNode* src_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
                if (src_leaf)
                    ZUIDockCollapseLeaf(DockTree, src_leaf);
                src->Hidden = true;
            }
        }
    }

    void ZUIPanelManager::ResetLayout()
    {
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel* p = &Panels[i];
            if (!p->Hidden || p->ViewCount == 0)
                continue;
            p->Hidden = false;
            if (DockTree)
            {
                ZUIDockNode* t = FindLargestLeaf(DockTree->Root);
                if (t)
                    ZUIDockSplitH(DockTree, t, 0.5f, t->ContentKey, p->DockKey);
            }
        }
        if (LayoutPath[0])
            remove(LayoutPath);
        LayoutDirty = true;
    }

    void ZUIPanelManager::SetPanelVisible(const char* name, bool visible)
    {
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel*   p = &Panels[i];
            const char* t = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "";
            if (strcmp(t, name) != 0)
                continue;
            if (visible && p->Hidden)
            {
                p->Hidden = false;
                if (DockTree)
                {
                    ZUIDockNode* tgt = FindLargestLeaf(DockTree->Root);
                    if (tgt)
                        ZUIDockSplitH(DockTree, tgt, 0.5f, tgt->ContentKey, p->DockKey);
                }
            }
            else if (!visible && !p->Hidden)
            {
                if (PendingCloseCount < kMaxPanels)
                    PendingCloseKeys[PendingCloseCount++] = p->DockKey;
            }
            LayoutDirty = true;
            break;
        }
    }

    bool ZUIPanelManager::IsPanelVisible(const char* name) const
    {
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            const ZUIPanel* p = &Panels[i];
            const char*     t = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "";
            if (strcmp(t, name) == 0)
                return !p->Hidden;
        }
        return false;
    }

} // namespace ZEngine::UI
