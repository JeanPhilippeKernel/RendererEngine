#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ZEngine::Core::Memory;

namespace ZEngine::UI
{
    namespace
    {
        float DockPixelScale(const ZUIContext* ctx)
        {
            return std::isfinite(ctx->UIScale) && ctx->UIScale > 0.f ? ctx->UIScale : 1.f;
        }

        float SnapDockPixel(float value, float scale)
        {
            return roundf(value * scale) / scale;
        }

        float DockGap(const ZUIContext* ctx, float scale)
        {
            return SnapDockPixel(fmaxf(0.f, ctx->Style.DockGap), scale);
        }

        // Use the same snapped edges for panels and splitters. Only interior
        // right/bottom edges give up space to the backdrop gutter.
        void DockSurfaceRect(const ZUIContext* ctx, const ZUIDockNode* root, float rect[4])
        {
            const float scale = DockPixelScale(ctx);
            const float gap   = DockGap(ctx, scale);
            for (uint32_t axis = 0; axis < 2; ++axis)
            {
                const bool interior = rect[axis + 2] < root->RectMax[axis] - 0.5f;
                rect[axis]          = SnapDockPixel(rect[axis], scale);
                rect[axis + 2]      = fmaxf(rect[axis], SnapDockPixel(rect[axis + 2], scale) - (interior ? gap : 0.f));
            }
        }

        struct DockDividerGeometry
        {
            uint32_t Axis   = 0;
            float    Center = 0.f;
            float    Start  = 0.f;
            float    End    = 0.f;
            float    Gap    = 0.f;
        };

        bool GetDockDividerGeometry(const ZUIContext* ctx, const ZUIDockNode* root, const ZUIDockNode* node, DockDividerGeometry& geometry)
        {
            if (!node || !node->First || !node->First->Next)
                return false;
            float rect[4] = {node->RectMin[0], node->RectMin[1], node->RectMax[0], node->RectMax[1]};
            DockSurfaceRect(ctx, root, rect);
            const float scale    = DockPixelScale(ctx);
            geometry.Axis        = node->SplitAxis == ZUIAxis::Y ? 1u : 0u;
            const uint32_t cross = 1u - geometry.Axis;
            const float    split = SnapDockPixel(node->First->RectMax[geometry.Axis], scale);
            geometry.Gap         = fminf(DockGap(ctx, scale), fmaxf(0.f, split - rect[geometry.Axis]));
            geometry.Center      = split - geometry.Gap * 0.5f;
            geometry.Start       = rect[cross];
            geometry.End         = rect[cross + 2];
            return geometry.End > geometry.Start;
        }

        bool DockDividerContains(const DockDividerGeometry& geometry, float width, const float point[2])
        {
            const uint32_t cross = 1u - geometry.Axis;
            return width > 0.f && fabsf(point[geometry.Axis] - geometry.Center) <= width * 0.5f && point[cross] >= geometry.Start && point[cross] < geometry.End;
        }

        uint64_t TabIdentity(const ZUIPanelView* view)
        {
            return view->Key ? view->Key : ZUIHashStr(view->Title, (uint32_t) ZEngine::Helpers::secure_strlen(view->Title));
        }

        bool HasDocumentTabs(const ZUIPanel* panel)
        {
            for (uint32_t i = 0; i < panel->ViewCount; ++i)
                if (panel->Views[i] && panel->Views[i]->Visible && panel->Views[i]->Chromeless)
                    return true;
            return false;
        }
        uint32_t DockNavOrder(const ZUIDockTree* tree, const ZUIDockNode* leaf)
        {
            uint32_t order = 1;
            for (auto* node = tree ? tree->Root : nullptr; node;)
            {
                // Visit top dock rows left-to-right before lower rows. A bottom
                // panel nested under the center split must not precede the right dock.
                if (leaf && !node->First && node != leaf && (node->RectMin[1] < leaf->RectMin[1] || (node->RectMin[1] == leaf->RectMin[1] && node->RectMin[0] < leaf->RectMin[0])))
                    ++order;
                if (node->First)
                    node = node->First;
                else
                {
                    while (node && !node->Next)
                        node = node->Parent;
                    if (node)
                        node = node->Next;
                }
            }
            return order;
        }
    } // namespace

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
        // A merged-away slot owns no views or leaf, so it can be reused without losing
        // hidden/restorable views or exhausting the fixed pool after repeated drags.
        ZUIPanel* p = nullptr;
        for (uint32_t i = 0; i < PanelCount; ++i)
            if (Panels[i].Hidden && Panels[i].ViewCount == 0 && !ZUIDockFindLeaf(DockTree, Panels[i].DockKey))
            {
                p = &Panels[i];
                break;
            }
        if (!p)
        {
            if (PanelCount >= kMaxPanels)
                return nullptr;
            p = &Panels[PanelCount++];
        }
        *p         = {};
        p->DockKey = dock_key;
        return p;
    }

    void ZUIPanelManager::AddView(ZUIPanel* panel, ZUIPanelView* view)
    {
        if (!panel || !view || panel->ViewCount >= kMaxTabsPerPanel)
        {
            return;
        }
        view->DefaultVisible             = view->Visible;
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
        ZUIDockNode* stack[64] = {};
        int          top       = 0;
        stack[top++]           = node;
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
        {
            m_split_divider_count = 0;
            return;
        }
        // Track occupied slots, not every split node: oversized/restored trees
        // must not overrun a fixed pointer list. Parent links avoid a DFS stack.
        bool seen[kMaxSplitDividers] = {};
        for (auto* node = DockTree->Root; node;)
        {
            if (node->ContentKey == 0 && node->First && node->First->Next)
            {
                uint32_t slot = 0;
                while (slot < m_split_divider_count && m_split_dividers[slot].Node != node)
                    ++slot;
                if (slot == m_split_divider_count && slot < kMaxSplitDividers)
                    m_split_dividers[m_split_divider_count++] = {node};
                if (slot < m_split_divider_count)
                    seen[slot] = true;
            }
            if (node->First)
                node = node->First;
            else
            {
                while (node && !node->Next)
                    node = node->Parent;
                if (node)
                    node = node->Next;
            }
        }
        for (uint32_t i = 0; i < m_split_divider_count;)
        {
            if (!seen[i])
            {
                m_split_dividers[i]                     = m_split_dividers[--m_split_divider_count];
                seen[i]                                 = seen[m_split_divider_count];
                m_split_dividers[m_split_divider_count] = {};
            }
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
            if (p->Hidden || p->ViewCount == 0)
            {
                continue;
            }

            // Tab bar close buttons
            for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
            {
                if (!p->Views[ti] || !p->Views[ti]->Closeable || !p->Views[ti]->Visible)
                    continue;
                char xkey[64] = {};
                snprintf(xkey, sizeof(xkey), "x##x_%llx_%llx", (unsigned long long) p->DockKey, (unsigned long long) TabIdentity(p->Views[ti]));
                uint64_t xhash = ZUIHashStr(xkey, (uint32_t) ZEngine::Helpers::secure_strlen(xkey));

                if (ctx->ActiveKey == xhash && ctx->HotKey == xhash)
                {
                    if (p->Views[ti])
                        p->Views[ti]->Visible = false;
                    // Advance ActiveTab to the next visible tab
                    if (p->ActiveTab == ti)
                    {
                        for (uint32_t ni = 0; ni < p->ViewCount; ++ni)
                        {
                            if (p->Views[ni] && p->Views[ni]->Visible)
                            {
                                p->ActiveTab = ni;
                                break;
                            }
                        }
                    }
                    // If no views remain visible, collapse the dock node this frame.
                    bool any_visible = false;
                    for (uint32_t vi2 = 0; vi2 < p->ViewCount; ++vi2)
                        if (p->Views[vi2] && p->Views[vi2]->Visible)
                        {
                            any_visible = true;
                            break;
                        }
                    if (!any_visible && PendingCloseCount < kMaxPanels)
                    {
                        p->Hidden                             = true;
                        PendingCloseKeys[PendingCloseCount++] = p->DockKey;
                    }
                    LayoutDirty = true;
                    goto next_panel;
                }
            }

            // Single-view title strip close button
            {
                ZUIPanelView* av              = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                bool          strip_closeable = av && av->Closeable && av->Visible;
                char          txk[56]         = {};
                snprintf(txk, sizeof(txk), "x##tx_%llx", (unsigned long long) p->DockKey);
                uint64_t txhash = ZUIHashStr(txk, (uint32_t) ZEngine::Helpers::secure_strlen(txk));

                if (strip_closeable && ctx->ActiveKey == txhash && ctx->HotKey == txhash)
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
        // HOT PATH — runs every frame, no heap allocation allowed.
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

        float root_rect[4] = {0.f, menu_h, sw, sh - status_h};
        if (DockTree)
        {
            ZUIDockLayout(DockTree, root_rect);
            SyncSplitDividers();
        }

        ZUIBox* bg      = ZUIBeginColumn(ctx, "##pm_bg", ZPx(sw), ZPx(sh));
        bg->Flags       = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBackdrop);
        bg->EdgeSoftness = 0.f;

        if (DrawMenuBar)
            BuildMenuBar(ctx, sw, menu_h);

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
                float r[4] = {};
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

                DockSurfaceRect(ctx, DockTree->Root, r);
                BuildDockedPanel(ctx, p, r);

                if (ctx->MousePressed[0] && !Drag.Active && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3])
                {
                    FocusPanel(i);
                    ctx->PreferredFocusScope = 0;
                }
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
                    char scale_buf[24] = {};
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
                    const float* fps_col     = (fps >= 55.f) ? ctx->Theme.TextDefault : (fps >= 30.f) ? ctx->Theme.TextWarn : ctx->Theme.TextError;
                    char         fps_buf[24] = {};
                    snprintf(fps_buf, sizeof(fps_buf), "%.0f fps", (double) fps);
                    ZUILabel(ctx, fps_buf, fps_col);
                }
            }
            ZUISpacer(ctx, 14.f);
            ZUIEndRow(ctx);
        }

        // Later siblings win interaction priority. Keep the invisible grab lanes
        // above panel content, but confine the visible indicators to the gutters.
        BuildDividerHitZones(ctx);
        BuildDividerVisuals(ctx);

        // The shell builds its document strip after BuildUI. Use its prior laid-out
        // bounds here so a release over that strip is committed before drag cleanup.
        if (Drag.Active)
            for (uint32_t i = 0; i < PanelCount && Drag.Active; ++i)
            {
                ZUIPanel* p = &Panels[i];
                if (p->Hidden || p == Drag.SrcPanel || !HasDocumentTabs(p))
                    continue;
                char key[40] = {};
                snprintf(key, sizeof(key), "##tabbar_%llx", (unsigned long long) p->DockKey);
                const auto* state = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key)));
                if (state && state->ScreenMaxX > state->ScreenMinX && state->ScreenMaxY > state->ScreenMinY && ctx->MousePos[0] >= state->ScreenMinX && ctx->MousePos[0] <= state->ScreenMaxX && ctx->MousePos[1] >= state->ScreenMinY && ctx->MousePos[1] <= state->ScreenMaxY)
                {
                    float r[4]     = {state->ScreenMinX, state->ScreenMinY, state->ScreenMaxX, state->ScreenMaxY};
                    Drag.HoverNode = ZUIDockFindLeaf(DockTree, p->DockKey);
                    BuildDropZones(ctx, p, r, true);
                }
            }

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
                ZUIPanel*           sp    = Drag.SrcPanel;
                const char*         title = "Panel";
                const ZUIPanelView* view  = nullptr;
                if (Drag.SrcTabIdx == kWholePanel)
                {
                    if (sp && sp->ViewCount > 0 && sp->Views[0])
                        view = sp->Views[0];
                }
                else if (sp && Drag.SrcTabIdx < sp->ViewCount && sp->Views[Drag.SrcTabIdx])
                {
                    view = sp->Views[Drag.SrcTabIdx];
                }

                if (view)
                    title = view->Title;
                ZUIDockGhostHeader(ctx, "##drag_ghost", title, Drag.GhostX, Drag.GhostY, view ? view->Icon : -1, view ? view->Closeable : true);
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
                ResetLayout();

            ZUISeparator(ctx);

            // Panels submenu — fly-out to the right (popup stack supports nested menus)
            if (ZUIBeginSubMenu(ctx, "Panels"))
            {
                static const char*        kPanelNames[]   = {"Outliner", "Console", "Details", "Viewport"};
                static constexpr uint32_t kPanelNameCount = 4;

                for (uint32_t ni = 0; ni < kPanelNameCount; ++ni)
                {
                    const char* target_name = kPanelNames[ni];

                    ZUIPanel*   match       = nullptr;
                    for (uint32_t i = 0; i < PanelCount; ++i)
                    {
                        ZUIPanel*   p = &Panels[i];
                        const char* t = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "";
                        if (ZEngine::Helpers::secure_strcmp(t, target_name) == 0)
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
        // HOT PATH — runs every frame, no heap allocation allowed.
        if (!p || p->ViewCount == 0)
        {
            return;
        }
        float        header_h   = ctx->Style.TabBarHeight;

        // Central node check: read IsCentral from the dock node, not a manager key
        ZUIDockNode* leaf       = ZUIDockFindLeaf(DockTree, p->DockKey);
        bool         is_central = leaf && leaf->IsCentral;

        if (is_central)
        {
            // Central panels have no visual chrome, but still need a container
            // constrained to their dock rect. Without it, ZFill() content becomes
            // a child of the full editor background while the viewport render target
            // is sized to this dock rect, permanently mismatching their aspects.
            char central_key[40] = {};
            snprintf(central_key, sizeof(central_key), "##central_%llx", (unsigned long long) p->DockKey);
            ZUIBox* central       = ZUIBeginColumn(ctx, central_key, ZPx(rect[2] - rect[0]), ZPx(rect[3] - rect[1]));
            central->Flags        = central->Flags | ZUI_FloatX | ZUI_FloatY | ZUI_ClipChildren;
            central->FloatPos[0]  = rect[0];
            central->FloatPos[1]  = rect[1];
            central->EdgeSoftness = 0.f;
            central->Flags        = central->Flags | ZUI_NavScope;
            central->NavOrder     = DockNavOrder(DockTree, leaf);
            if (ctx->PreferredFocusScope == central->Key)
                for (uint32_t i = 0; i < PanelCount; ++i)
                    if (&Panels[i] == p)
                        FocusPanel(i);
            if (!ctx->PreferredFocusScope)
                for (uint32_t i = 0; i < PanelCount; ++i)
                    if (&Panels[i] == p && i == FocusedPanelIdx)
                        ctx->PreferredFocusScope = central->Key;

            // Pure passthrough — multi-tab gets a minimal tab bar, single gets none.
            if (p->ViewCount > 1 && !HasDocumentTabs(p))
            {
                float tab_rect[4] = {rect[0], rect[1], rect[2], rect[1] + header_h};
                BuildTabBar(ctx, p, tab_rect);
                float         cr[4] = {rect[0], rect[1] + header_h, rect[2], rect[3]};
                ZUIPanelView* view  = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                if (view && view->Visible)
                {
                    view->BuildContent(ctx, cr);
                }
            }
            else
            {
                ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
                if (view && view->Visible)
                {
                    view->BuildContent(ctx, rect);
                }
            }
            ZUIEndColumn(ctx);
            return;
        }

        bool is_focused = false;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p && pi == FocusedPanelIdx)
            {
                is_focused = true;
                break;
            }

        char panel_key[32] = {};
        snprintf(panel_key, sizeof(panel_key), "##panel_%llx", (unsigned long long) p->DockKey);

        ZUIBox* panel      = ZUIBeginColumn(ctx, panel_key, ZPx(rect[2] - rect[0]), ZPx(rect[3] - rect[1]));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = rect[0];
        panel->FloatPos[1] = rect[1];
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->EdgeSoftness = 0.f;
        panel->Flags        = panel->Flags | ZUI_NavScope;
        panel->NavOrder     = DockNavOrder(DockTree, leaf);
        if (ctx->PreferredFocusScope == panel->Key)
            for (uint32_t i = 0; i < PanelCount; ++i)
                if (&Panels[i] == p)
                    FocusPanel(i);
        if (!ctx->PreferredFocusScope && is_focused)
            ctx->PreferredFocusScope = panel->Key;

        // AutoHideTabBar: use node flag, defaulting to false if no node found
        // (false = ImGui default: always show tab bar even for single-view nodes)
        bool     auto_hide     = leaf ? leaf->AutoHideTabBar : false;
        uint32_t visible_views = 0;
        for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
            if (p->Views[vi] && p->Views[vi]->Visible)
                ++visible_views;
        // A panel holding a chromeless view (the viewport slot) draws no tab strip at all: its tabs
        // (viewport, importer, profiler, asset editors) are shown in the editor's document tab bar.
        bool chromeless = HasDocumentTabs(p);
        if (chromeless)
            header_h = 0.f;
        bool show_tabs = !chromeless && ((p->ViewCount > 1) || !auto_hide);

        if (chromeless)
        {
        }
        else if (show_tabs)
        {
            float tab_rect[4] = {rect[0], rect[1], rect[2], rect[1] + header_h};
            BuildTabBar(ctx, p, tab_rect);
        }
        else
        {
            // VS Code single-view title strip — style-driven
            const char* view_title = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "Panel";
            float       btn_h      = ctx->Style.FontSize; // was header_h * 0.60

            char        hk[48]     = {};
            snprintf(hk, sizeof(hk), "##tbar_%llx", (unsigned long long) p->DockKey);
            ZUIBox* strip = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
            strip->Flags  = strip->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_NoKeyboardFocus;
            // Background: focus-aware
            ZUIBoxSetColorArr(strip, is_focused ? ctx->Theme.TitleBgActive : ctx->Theme.TitleBarBg);
            strip->EdgeSoftness = 0.f;

            // Left padding from style
            ZUISpacer(ctx, ZUIGetFramePadX(ctx));
            // Icon dot — size from style
            {
                const float* ic     = (p->Views[0] && p->Views[0]->TabColor[3] > 0.01f) ? p->Views[0]->TabColor : ctx->Theme.PanelFocusBorder;
                char         ik[48] = {};
                snprintf(ik, sizeof(ik), "##tic_%llx", (unsigned long long) p->DockKey);
                float   icon_sz = ctx->Style.TabIconSize;
                ZUIBox* icon    = ZUIPushBox(ctx, ik, (uint32_t) ZEngine::Helpers::secure_strlen(ik), ZUI_DrawBackground);
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
                char fk[48] = {};
                snprintf(fk, sizeof(fk), "##tf_%llx", (unsigned long long) p->DockKey);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) ZEngine::Helpers::secure_strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZPx(header_h);
                ZUIPopBox(ctx);
            }

            // x close (hover-only) — suppressed for non-closeable panels (e.g. main viewport)
            bool should_close          = false;
            bool active_view_closeable = (p->ActiveTab < p->ViewCount && p->Views[p->ActiveTab]) ? p->Views[p->ActiveTab]->Closeable : true;
            bool ph                    = active_view_closeable && (ctx->MousePos[0] >= rect[0] && ctx->MousePos[0] <= rect[2] && ctx->MousePos[1] >= rect[1] && ctx->MousePos[1] <= rect[1] + header_h);
            if (ph)
            {
                char xk[56] = {};
                snprintf(xk, sizeof(xk), "x##tx_%llx", (unsigned long long) p->DockKey);
                ZUIBox* xb       = ZUIPushBox(ctx, xk, (uint32_t) ZEngine::Helpers::secure_strlen(xk), ZUI_DrawText | ZUI_Clickable);
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
        }

        // Content — no WindowPadding for docked panels (editor panels fill edge-to-edge).
        // WindowPadding will be applied when floating windows are implemented.
        ZUIPanelView* view = (p->ActiveTab < p->ViewCount) ? p->Views[p->ActiveTab] : nullptr;
        if (view && view->Visible)
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
            char fk[48] = {};
            snprintf(fk, sizeof(fk), "##pfocus_%llx", (unsigned long long) p->DockKey);
            ZUIBox* fb      = ZUIPushBox(ctx, fk, (uint32_t) ZEngine::Helpers::secure_strlen(fk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
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

    void ZUIPanelManager::BuildDocumentTabBar(ZUIContext* ctx, const char* anchor_title, const char* anchor_label, float height)
    {
        if (!ctx || !anchor_title)
            return;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
        {
            auto* p = &Panels[pi];
            if (p->Hidden)
                continue;
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
                if (p->Views[vi] && p->Views[vi]->Visible && ZEngine::Helpers::secure_strcmp(p->Views[vi]->Title, anchor_title) == 0)
                {
                    float rect[4] = {0.f, 0.f, (float) ctx->ScreenW, height};
                    BuildTabBar(ctx, p, rect, anchor_title, anchor_label);
                    return;
                }
        }
    }

    void ZUIPanelManager::BuildTabBar(ZUIContext* ctx, ZUIPanel* p, float rect[4], const char* anchor_title, const char* anchor_label)
    {
        // HOT PATH — runs every frame, no heap allocation allowed.
        const bool document    = anchor_title != nullptr;
        float      tab_h       = document ? rect[3] - rect[1] : ctx->Style.TabBarHeight;
        char       bar_key[40] = {};
        snprintf(bar_key, sizeof(bar_key), "##tabbar_%llx", (unsigned long long) p->DockKey);

        const float leading                  = document ? 42.f : 8.f;
        float       widths[kMaxTabsPerPanel] = {};
        float       content_width            = leading + 8.f;
        float       active_left              = leading;
        float       active_width             = 0.f;
        for (uint32_t i = 0; i < p->ViewCount; ++i)
        {
            const auto* view = p->Views[i];
            if (!view || !view->Visible)
                continue;
            widths[i]               = document && ZEngine::Helpers::secure_strcmp(view->Title, anchor_title) == 0 ? 224.f : ctx->Style.TabWidth;
            const bool  is_anchor   = document && ZEngine::Helpers::secure_strcmp(view->Title, anchor_title) == 0;
            const char* text        = is_anchor && anchor_label ? anchor_label : view->Title;
            float       measured[2] = {};
            if (auto* font = ctx->GetFont(document ? ZUIFontSize::Large : ZUIFontSize::Body))
                ZUIMeasureText(font, text, (uint32_t) Helpers::secure_strlen(text), measured);
            widths[i] = fmaxf(widths[i], 18.f + measured[0] + 18.f + ((is_anchor || view->Icon >= 0) ? 21.f : 0.f));
            if (i == p->ActiveTab)
            {
                active_left  = content_width - 8.f;
                active_width = widths[i];
            }
            content_width += widths[i] + ZUIGetInnerSpac(ctx);
        }
        const float strip_width = fmaxf(0.f, rect[2] - rect[0]);
        const bool  overflow    = content_width > strip_width;
        const float arrow_width = fminf(tab_h, strip_width * 0.25f);
        const float bar_width   = fmaxf(0.f, strip_width - (overflow ? 2.f * arrow_width : 0.f));
        auto*       bps         = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr(bar_key, (uint32_t) ZEngine::Helpers::secure_strlen(bar_key)));
        if (bps)
        {
            bps->MaxScrollX = fmaxf(0.f, content_width - bar_width);
            auto* active    = p->ActiveTab < p->ViewCount ? p->Views[p->ActiveTab] : nullptr;
            if (active_width > 0.f && (active != p->LastActiveView || active_left != p->LastActiveTabOffset || bar_width != p->LastTabBarWidth))
            {
                if (active_left < bps->ScrollXTarget)
                    bps->ScrollXTarget = active_left;
                else if (active_left + active_width > bps->ScrollXTarget + bar_width)
                    bps->ScrollXTarget = active_left + active_width - bar_width;
            }
            p->LastActiveView      = active;
            p->LastActiveTabOffset = active_left;
            p->LastTabBarWidth     = bar_width;
            bps->ScrollXTarget     = fminf(bps->MaxScrollX, fmaxf(0.f, bps->ScrollXTarget));
        }
        // Reserve arrow space outside the clipped scroll region: arrows never
        // overlap tabs or their close buttons, in rendering OR hit testing.
        char strip_key[40] = {};
        snprintf(strip_key, sizeof(strip_key), "##tabstrip_%llx", (unsigned long long) p->DockKey);
        ZUIBeginRow(ctx, strip_key, ZFill(), ZPx(tab_h));
        const auto scroll_button = [&](bool left) {
            char key[40] = {};
            snprintf(key, sizeof(key), left ? "##tsl_%llx" : "##tsr_%llx", (unsigned long long) p->DockKey);
            auto* button      = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_KeyboardActivate);
            button->Size[0]   = ZPx(arrow_width);
            button->Size[1]   = ZPx(tab_h);
            button->TextAlign = ZUITextAlign::Center;
            button->Label     = ZUIPushStr(&ctx->FrameArena, left ? "<" : ">", 1);
            ZUIBoxSetColorArr(button, ctx->Theme.TitleBgActive);
            for (int c = 0; c < 4; ++c)
                button->TextColor[c] = ctx->Theme.TextDim[c];
            const auto signal = ZUISignalFromBox(ctx, button);
            ZUIPopBox(ctx);
            if (bps && (signal.Flags & ZUI_SignalClicked))
                bps->ScrollXTarget = fminf(bps->MaxScrollX, fmaxf(0.f, bps->ScrollXTarget + (left ? -ctx->Style.TabWidth : ctx->Style.TabWidth)));
        };
        if (overflow)
            scroll_button(true);
        ZUIBox* bar     = ZUIBeginScrollRegion(ctx, bar_key, ZFill(), ZPx(tab_h));
        bar->LayoutAxis = ZUIAxis::X;
        bar->Flags      = bar->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_Scrollable | ZUI_ClipChildren;
        ZUIBoxSetColorArr(bar, ctx->Theme.TitleBarBg);
        bar->EdgeSoftness = 0.f;

        ZUISpacer(ctx, leading);
        uint32_t      reorder_from = kWholePanel;
        uint32_t      reorder_to   = kWholePanel;
        ZUIPanelView* close_view   = nullptr;

        for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
        {
            ZUIPanelView* view = p->Views[ti];
            if (!view || !view->Visible)
            {
                continue;
            }
            bool           is_active   = (ti == p->ActiveTab);
            bool           tab_hovered = false; // determined after box push (uses previous-frame HotKey)
            const bool     anchor      = document && ZEngine::Helpers::secure_strcmp(view->Title, anchor_title) == 0;
            const float    tab_width   = widths[ti];
            const uint64_t identity    = TabIdentity(view);

            char           tab_key[64] = {};
            snprintf(tab_key, sizeof(tab_key), "##tab_%llx_%llx", (unsigned long long) p->DockKey, (unsigned long long) identity);
            // Figma "Active panel tab": 180 x 30, 8 px leading pad, 13 px icon, 8 px gap, 10 px close.
            ZUIBox* tab       = ZUIBeginRow(ctx, tab_key, ZPx(tab_width), ZPx(tab_h));
            tab->Flags        = tab->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_ClipChildren | ZUI_KeyboardActivate;
            tab->EdgeSoftness = 0.f;
            tab->Padding[0]   = 8.f;
            tab->Padding[2]   = 10.f;
            tab->Gap          = 8.f;
            tab->CrossAlign   = ZUIAlign::Center;

            tab_hovered       = (ctx->HotKey == tab->Key);

            if (is_active)
            {
                ZUIBoxSetColorArr(tab, ctx->Theme.TabActiveBg);
                char ind_key[64] = {};
                snprintf(ind_key, sizeof(ind_key), "##tabind_%llx_%llx", (unsigned long long) p->DockKey, (unsigned long long) identity);
                ZUIActiveTabIndicator(ctx, ind_key, tab_width);
            }
            else
                ZUIBoxSetColorArr(tab, tab_hovered ? ctx->Theme.TabHoveredBg : ctx->Theme.TabInactiveBg);

            const float* label_col = is_active ? ctx->Theme.TextDefault : ctx->Theme.TextDim;
            if (anchor || view->Icon >= 0)
            {
                ZUIIconBox(ctx, anchor ? ZUIIcon::Map : (ZUIIcon) view->Icon, 13.f, label_col);
            }
            {
                const char* label         = anchor && anchor_label ? anchor_label : view->Title;
                uint32_t    tlen          = (uint32_t) ZEngine::Helpers::secure_strlen(label);
                char        label_key[64] = {};
                snprintf(label_key, sizeof(label_key), "##tablbl_%llx_%llx", (unsigned long long) p->DockKey, (unsigned long long) identity);
                ZUIBox* lbl       = ZUIPushBox(ctx, label_key, (uint32_t) ZEngine::Helpers::secure_strlen(label_key), ZUI_DrawText);
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, label, tlen);
                lbl->FontSize     = document ? ZUIFontSize::Large : ZUIFontSize::Body;
                lbl->Size[0]      = ZFill();
                lbl->Size[1]      = ZPx(13.f);
                lbl->TextColor[0] = label_col[0];
                lbl->TextColor[1] = label_col[1];
                lbl->TextColor[2] = label_col[2];
                lbl->TextColor[3] = 1.f;
                ZUIPopBox(ctx);
            }

            // Close button — space always reserved so tab width is stable on hover.
            bool tab_closed = false;
            {
                bool view_closeable = view->Closeable;

                char xkey[64]       = {};
                snprintf(xkey, sizeof(xkey), "x##x_%llx_%llx", (unsigned long long) p->DockKey, (unsigned long long) identity);
                uint64_t xhash      = ZUIHashStr(xkey, (uint32_t) ZEngine::Helpers::secure_strlen(xkey));
                bool     show_close = view_closeable && (is_active || tab_hovered || ctx->FocusKey == xhash || ctx->HotKey == xhash);
                bool     xh         = show_close && (ctx->HotKey == xhash || ctx->ActiveKey == xhash);

                ZUIBox*  xbtn       = ZUIPushBox(ctx, xkey, (uint32_t) ZEngine::Helpers::secure_strlen(xkey), (show_close ? ZUI_DrawIcon : ZUI_None) | (view_closeable ? ZUI_Clickable | ZUI_KeyboardActivate : ZUI_None));
                xbtn->MinHitSize[0] = xbtn->MinHitSize[1] = 16.f;
                xbtn->Size[0]                             = ZPx(10.f);
                xbtn->Size[1]                             = ZPx(10.f);
                xbtn->IconId                              = (uint16_t) ZUIIcon::X;
                const float* xc                           = xh ? ctx->Theme.TextDefault : ctx->Theme.TextDim;
                for (int c = 0; c < 4; ++c)
                    xbtn->TextColor[c] = xc[c];
                ZUISignal xsig = ZUISignalFromBox(ctx, xbtn);
                ZUIPopBox(ctx);
                if (show_close && (xsig.Flags & ZUI_SignalClicked))
                {
                    tab_closed = true;
                }
            }

            ZUISignal sig = ZUISignalFromBox(ctx, tab);
            ZUIEndRow(ctx);
            ZUISetTooltip(ctx, sig, anchor && anchor_label ? anchor_label : view->Title);

            // Defer edits until every tab is built: no one-frame gaps or stale loop indices.
            if (tab_closed)
                close_view = view;

            if (!tab_closed && !p->ReorderActive && !Drag.Active && (sig.Flags & ZUI_SignalClicked))
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
                    if (mostly_horiz && dy <= ctx->Style.DockingUndockVertical && p->ViewCount > 1)
                    {
                        if (!p->ReorderActive)
                        {
                            p->ReorderActive = true;
                            p->ReorderTabIdx = ti;
                            p->ReorderAccumX = 0.f;
                        }
                        if (p->ReorderActive && p->ReorderTabIdx == ti)
                        {
                            p->ReorderAccumX      += sig.DragDelta[0];
                            const float threshold  = fmaxf(tab_width, ctx->Style.DockingMinTabWidth) * 0.5f;
                            if (fabsf(p->ReorderAccumX) > threshold)
                            {
                                const int step = p->ReorderAccumX > 0.f ? 1 : -1;
                                for (int next = (int) ti + step; next >= 0 && next < (int) p->ViewCount; next += step)
                                    if (p->Views[next] && p->Views[next]->Visible)
                                    {
                                        reorder_from      = ti;
                                        reorder_to        = (uint32_t) next;
                                        p->ReorderAccumX -= threshold * (float) step;
                                        break;
                                    }
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

            // Inter-tab spacing from style
            ZUISpacer(ctx, ZUIGetInnerSpac(ctx));
        }

        if (reorder_from != kWholePanel)
        {
            auto* moved            = p->Views[reorder_from];
            p->Views[reorder_from] = p->Views[reorder_to];
            p->Views[reorder_to]   = moved;
            if (p->ActiveTab == reorder_from)
                p->ActiveTab = reorder_to;
            else if (p->ActiveTab == reorder_to)
                p->ActiveTab = reorder_from;
            p->ReorderTabIdx = reorder_to;
            LayoutDirty      = true;
        }
        if (ctx->MouseReleased[0] || !ctx->MouseDown[0])
        {
            p->ReorderActive = false;
            p->ReorderAccumX = 0.f;
        }
        if (close_view)
            SetPanelVisible(close_view->Title, false);
        ZUISpacer(ctx, 8.f);

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
                Drag.Active    = true;
                Drag.SrcPanel  = p;
                Drag.SrcTabIdx = kWholePanel;
                Drag.GhostX    = ctx->MousePos[0];
                Drag.GhostY    = ctx->MousePos[1];
            }
        }

        // This horizontal scroll region uses the reserved arrow buttons instead
        // of ZUIEndScrollRegion's overlaid scrollbar.
        ZUIPopBox(ctx);
        if (overflow)
            scroll_button(false);
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
    //   Dividers / focus strip      → ##pm_bg (absolute)
    //   Drag ghost / drop previews  → foreground canvas (absolute, above shell chrome)
    //   Overline                    → panel column (panel-relative, inside BuildTabBar before ZUIEndRow)
    //
    // BuildDropZones is called AFTER ZUIEndColumn for the panel box, so
    // ctx->Current == ##pm_bg here. All FloatPos values must be absolute screen coords.
    void ZUIPanelManager::BuildDropZones(ZUIContext* ctx, ZUIPanel* p, float rect[4], bool document_bar)
    {
        float       w            = rect[2] - rect[0];
        float       h            = rect[3] - rect[1];
        float       mx           = ctx->MousePos[0];
        float       my           = ctx->MousePos[1];
        float       rx           = (w > 0.f) ? (mx - rect[0]) / w : 0.5f; // 0..1 across width
        float       ry           = (h > 0.f) ? (my - rect[1]) / h : 0.5f; // 0..1 down height

        float       tab_h        = document_bar ? h : ctx->Style.TabBarHeight;
        bool        over_tab_bar = document_bar || (!HasDocumentTabs(p) && my >= rect[1] && my <= rect[1] + tab_h);
        // When dragging over the source panel itself, only edge splits are valid —
        // center zone would merge with self. Tab bar on source panel also uses edge detection.
        bool        is_src       = (Drag.SrcPanel == p);

        ZUIDropZone zone         = {};
        if (over_tab_bar && !is_src)
        {
            // Dropping on another panel's tab bar: tab merge with insertion index.
            zone                = ZUIDropZone::Center;

            uint32_t insert_idx = p->ViewCount; // default: append at end
            float    insert_x   = rect[2];      // default: right edge of tab bar

            for (uint32_t ti = 0; ti < p->ViewCount; ++ti)
            {
                char tk[64] = {};
                if (!p->Views[ti] || !p->Views[ti]->Visible)
                    continue;
                snprintf(tk, sizeof(tk), "##tab_%llx_%llx", (unsigned long long) p->DockKey, (unsigned long long) TabIdentity(p->Views[ti]));
                uint64_t            thash = ZUIHashStr(tk, (uint32_t) ZEngine::Helpers::secure_strlen(tk));
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

            // Figma: opaque 2 px accent/blue insertion bar.
            auto* saved_parent    = ZUIBeginForeground(ctx);
            char  ik[48]          = {};
            snprintf(ik, sizeof(ik), "##dz_ins_%llx", (unsigned long long) p->DockKey);
            ZUIBox* ins      = ZUIPushBox(ctx, ik, (uint32_t) ZEngine::Helpers::secure_strlen(ik), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            ins->Size[0]     = ZPx(2.f);
            ins->Size[1]     = ZPx(tab_h);
            ins->FloatPos[0] = fminf(rect[2], fmaxf(rect[0], insert_x)) - 1.f;
            ins->FloatPos[1] = rect[1];
            ZUIBoxSetColorArr(ins, ctx->Theme.TabActiveBorder);
            ins->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
            ZUIEndForeground(ctx, saved_parent);
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
            char pk[40] = {};
            snprintf(pk, sizeof(pk), "##dz_prev_%llx", (unsigned long long) p->DockKey);
            auto* saved_parent = ZUIBeginForeground(ctx);
            ZUIDropZoneFill(ctx, pk, px0, py0, px1 - px0, py1 - py0);
            ZUIEndForeground(ctx, saved_parent);
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

    // Input pass — added after panels so their contents cannot steal the grab lanes.
    void ZUIPanelManager::BuildDividerHitZones(ZUIContext* ctx)
    {
        // HOT PATH — runs every frame, no heap allocation allowed.
        if (!DockTree || !DockTree->Root)
            return;

        DockDividerGeometry geometries[kMaxSplitDividers] = {};
        uint32_t            hovered                       = kMaxSplitDividers;
        float               nearest                       = INFINITY;
        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            m_split_dividers[di].Hovered = false;
            auto& geometry               = geometries[di];
            if (!GetDockDividerGeometry(ctx, DockTree->Root, m_split_dividers[di].Node, geometry))
                continue;
            const float width    = fmaxf(geometry.Gap, ctx->Style.DockingGrabWidth);
            const float distance = fabsf(ctx->MousePos[geometry.Axis] - geometry.Center);
            if (DockDividerContains(geometry, width, ctx->MousePos) && distance < nearest)
            {
                hovered = di;
                nearest = distance;
            }
        }

        uint32_t owner = hovered;
        if (ctx->MouseDown[0])
            for (uint32_t di = 0; di < m_split_divider_count; ++di)
                if (m_split_dividers[di].Dragging)
                    owner = di;
        for (uint32_t step = 0; step < m_split_divider_count; ++step)
        {
            // The nearest divider is built last: at junctions the indicator,
            // cursor and actual click target must all have the same owner.
            uint32_t di = step;
            if (owner < m_split_divider_count)
                di = step + 1 == m_split_divider_count ? owner : (step >= owner ? step + 1 : step);
            const auto& geometry = geometries[di];
            if (!(geometry.End > geometry.Start))
            {
                if (m_split_dividers[di].Dragging)
                {
                    m_split_dividers[di].Dragging = false;
                    LayoutDirty                   = true;
                }
                continue;
            }
            ZUIDockNode*   snode  = m_split_dividers[di].Node;
            const uint32_t axis   = geometry.Axis;
            const uint32_t cross  = 1u - axis;
            const float    band_w = fmaxf(geometry.Gap, ctx->Style.DockingGrabWidth);
            ZUISignal      ha_sig = {};

            if (band_w > 0.f)
            {
                char hk[32] = {};
                snprintf(hk, sizeof(hk), "##sdivh_%u", di);
                // An explicit transparent background suppresses the renderer's
                // generic clickable hover fill: the grab lane must stay invisible.
                ZUIBox* ha        = ZUIPushBox(ctx, hk, (uint32_t) ZEngine::Helpers::secure_strlen(hk), ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY | ZUI_NoKeyboardFocus);
                ha->MinHitSize[0] = ha->MinHitSize[1] = 0.f;
                ha->Size[axis]                        = ZPx(band_w);
                ha->Size[cross]                       = ZPx(geometry.End - geometry.Start);
                ha->FloatPos[axis]                    = geometry.Center - band_w * 0.5f;
                ha->FloatPos[cross]                   = geometry.Start;
                ha->EdgeSoftness                      = 0.f;
                ZUIBoxSetColor(ha, 0.f, 0.f, 0.f, 0.f);
                ha_sig = ZUISignalFromBox(ctx, ha);
                ZUIPopBox(ctx);
            }

            bool& dragging = m_split_dividers[di].Dragging;
            if (ha_sig.Flags & ZUI_SignalPressed)
                dragging = true;
            if (dragging && (ctx->MouseReleased[0] || !ctx->MouseDown[0] || ctx->Disabled || !(ha_sig.Flags & (ZUI_SignalPressed | ZUI_SignalHeld))))
            {
                dragging    = false;
                LayoutDirty = true;
            }
            if (dragging && (ha_sig.Flags & ZUI_SignalHeld))
            {
                float delta = ha_sig.DragDelta[axis];
                if (delta != 0.f)
                    ZUIDockResize(DockTree, snode->First, delta);
            }

            const float hover_width      = fminf(fmaxf(0.f, ctx->Style.DockingHoverBandWidth), band_w);
            m_split_dividers[di].Hovered = di == hovered && !Drag.Active && !ctx->Disabled && DockDividerContains(geometry, hover_width, ctx->MousePos);
            // Use current geometry for immediate cursor feedback (signals lag one frame).
            if ((di == hovered && !Drag.Active && !ctx->Disabled) || dragging)
                ctx->ResizeCursor = axis == 1 ? 2 : 1;
        }
    }

    // Render pass — the indicator stays inside the backdrop gutter, never over a panel.
    void ZUIPanelManager::BuildDividerVisuals(ZUIContext* ctx)
    {
        // HOT PATH — runs every frame, no heap allocation allowed.
        if (!DockTree || !DockTree->Root)
            return;
        uint32_t    hovered = kMaxSplitDividers, dragging = kMaxSplitDividers;
        const float scale = DockPixelScale(ctx);

        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            if (m_split_dividers[di].Dragging)
                dragging = di;
            if (m_split_dividers[di].Hovered)
                hovered = di;
        }

        const uint32_t highlighted = dragging < kMaxSplitDividers ? dragging : hovered;
        for (uint32_t di = 0; di < m_split_divider_count; ++di)
        {
            const float requested = di == highlighted ? ctx->Style.DockingSeparatorSize : ctx->Style.DockingSeparatorSizeRest;
            if (!(requested > 0.f))
                continue;
            DockDividerGeometry geometry = {};
            if (!GetDockDividerGeometry(ctx, DockTree->Root, m_split_dividers[di].Node, geometry))
                continue;
            const float width = fminf(geometry.Gap, SnapDockPixel(fmaxf(0.f, requested), scale));
            if (!(width > 0.f) || !(geometry.End > geometry.Start))
                continue;
            const uint32_t axis = geometry.Axis, cross = 1u - axis;
            const float    start  = fminf(geometry.Center + geometry.Gap * 0.5f - width, fmaxf(geometry.Center - geometry.Gap * 0.5f, SnapDockPixel(geometry.Center - width * 0.5f, scale)));
            char           vk[32] = {};
            snprintf(vk, sizeof(vk), "##sdiv_%u", di);
            ZUIBox* vis          = ZUIPushBox(ctx, vk, (uint32_t) ZEngine::Helpers::secure_strlen(vk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            vis->Size[axis]      = ZPx(width);
            vis->Size[cross]     = ZPx(geometry.End - geometry.Start);
            vis->FloatPos[axis]  = start;
            vis->FloatPos[cross] = geometry.Start;
            vis->EdgeSoftness    = 0.f;
            ZUIBoxSetColorArr(vis, di == highlighted ? ctx->Theme.TabActiveBorder : ctx->Theme.Separator);
            ZUIPopBox(ctx);
        }
    }

    // CommitDrop — moves a tab (or whole panel) to a new dock position

    void ZUIPanelManager::CommitDrop(ZUIPanel* src, uint32_t tab_idx, ZUIDockNode* dst, ZUIDropZone zone)
    {
        if (!DockTree || !src || !dst || dst->First || src->ViewCount == 0 || src->ViewCount > kMaxTabsPerPanel || zone < ZUIDropZone::Center || zone > ZUIDropZone::Bottom)
            return;
        const bool whole = tab_idx == kWholePanel;
        if (!whole && (tab_idx >= src->ViewCount || !src->Views[tab_idx]))
            return;
        const uint64_t dst_key     = dst->ContentKey;
        ZUIPanel*      destination = FindPanel(dst_key);
        auto*          source_leaf = ZUIDockFindLeaf(DockTree, src->DockKey);
        if (!source_leaf || !destination || (destination == src && (whole || src->ViewCount == 1)))
            return;

        if (zone == ZUIDropZone::Center)
        {
            const uint32_t count = whole ? src->ViewCount : 1;
            // Reject the entire operation before touching either panel. A partial
            // insertion followed by source removal would silently lose views.
            if (destination == src || destination->ViewCount > kMaxTabsPerPanel || count > kMaxTabsPerPanel - destination->ViewCount)
                return;
            const uint32_t ins = Drag.DropTabInsertIdx < destination->ViewCount ? Drag.DropTabInsertIdx : destination->ViewCount;
            for (uint32_t i = 0; i < count; ++i)
                InsertViewAt(destination, src->Views[whole ? i : tab_idx], ins + i);
            destination->ActiveTab = ins + (whole && src->ActiveTab < count ? src->ActiveTab : 0);
            destination->Hidden    = false;
            if (!destination->Views[destination->ActiveTab] || !destination->Views[destination->ActiveTab]->Visible)
                for (uint32_t i = 0; i < destination->ViewCount; ++i)
                    if (destination->Views[i] && destination->Views[i]->Visible)
                    {
                        destination->ActiveTab = i;
                        break;
                    }
        }
        else
        {
            // A one-tab panel can move directly; otherwise reserve a slot BEFORE
            // mutating the tree. Keys are unique even after loading a saved layout.
            const bool move_panel = whole || src->ViewCount == 1;
            uint64_t   new_key    = src->DockKey;
            if (!move_panel)
            {
                do
                {
                    new_key = ++DragKeySeq;
                } while (FindPanel(new_key));
                destination = AddPanel(new_key);
                if (!destination)
                    return;
                InsertViewAt(destination, src->Views[tab_idx], destination->ViewCount);
            }
            const bool central   = dst->IsCentral;
            const bool auto_hide = dst->AutoHideTabBar;
            const bool before    = zone == ZUIDropZone::Left || zone == ZUIDropZone::Top;
            if (zone == ZUIDropZone::Left || zone == ZUIDropZone::Right)
                ZUIDockSplitH(DockTree, dst, 0.5f, before ? new_key : dst_key, before ? dst_key : new_key);
            else
                ZUIDockSplitV(DockTree, dst, 0.5f, before ? new_key : dst_key, before ? dst_key : new_key);
            // Preserve the old destination's chrome policy on its new leaf.
            auto* retained           = before ? dst->Last : dst->First;
            retained->IsCentral      = central;
            retained->AutoHideTabBar = auto_hide;
            dst->IsCentral           = false;
            dst->AutoHideTabBar      = false;
            if (move_panel)
            {
                // Split first, then collapse the captured original leaf. This
                // keeps both panels reachable even when they were siblings.
                auto* inserted           = before ? dst->First : dst->Last;
                inserted->IsCentral      = source_leaf->IsCentral;
                inserted->AutoHideTabBar = source_leaf->AutoHideTabBar;
                ZUIDockCollapseLeaf(DockTree, source_leaf);
                FocusPanel((uint32_t) (src - Panels));
                LayoutDirty = true;
                return;
            }
        }

        auto* active = src->ActiveTab < src->ViewCount ? src->Views[src->ActiveTab] : nullptr;
        if (whole)
        {
            for (auto*& view : src->Views)
                view = nullptr;
            src->ViewCount = 0;
        }
        else
        {
            for (uint32_t i = tab_idx; i + 1 < src->ViewCount; ++i)
                src->Views[i] = src->Views[i + 1];
            src->Views[--src->ViewCount] = nullptr;
        }
        src->ActiveTab = 0;
        src->Hidden    = true;
        for (uint32_t i = 0; i < src->ViewCount; ++i)
            if (src->Views[i] && src->Views[i]->Visible)
            {
                if (src->Hidden || src->Views[i] == active)
                    src->ActiveTab = i;
                src->Hidden = false;
            }
        if (src->Hidden)
            if (auto* leaf = ZUIDockFindLeaf(DockTree, src->DockKey))
                ZUIDockCollapseLeaf(DockTree, leaf);
        FocusPanel((uint32_t) (destination - Panels));
        LayoutDirty = true;
    }

    void ZUIPanelManager::ResetLayout()
    {
        // A reset may run before BuildUI flushes a close requested this frame.
        // Do not let that stale close remove a restored panel on the next frame.
        PendingCloseCount = 0;
        Drag              = {};
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel* p  = &Panels[i];
            p->Hidden    = true;
            p->ActiveTab = 0;
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
            {
                auto* view = p->Views[vi];
                if (!view)
                    continue;
                view->Visible = view->DefaultVisible;
                if (view->Visible && p->Hidden)
                {
                    p->ActiveTab = vi;
                    p->Hidden    = false;
                }
            }
            if (DockTree)
            {
                auto* leaf = ZUIDockFindLeaf(DockTree, p->DockKey);
                if (p->Hidden)
                {
                    if (leaf)
                        ZUIDockCollapseLeaf(DockTree, leaf);
                }
                else if (!leaf)
                {
                    if (auto* target = FindLargestLeaf(DockTree->Root))
                        ZUIDockSplitH(DockTree, target, 0.5f, target->ContentKey, p->DockKey);
                    else if (DockTree->Root && !DockTree->Root->First)
                        DockTree->Root->ContentKey = p->DockKey;
                }
            }
        }
        if (LayoutPath[0])
            remove(LayoutPath);
        LayoutDirty = true;
    }

    void ZUIPanelManager::SetPanelVisible(const char* name, bool visible, bool focus)
    {
        if (!name)
            return;
        // First search per-view visibility (overlay tabs like Profiler/Importer).
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel* p = &Panels[i];
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
            {
                ZUIPanelView* v = p->Views[vi];
                if (!v || !v->MatchesTitle(name))
                    continue;
                // A closed (hidden) panel must still be re-docked when its view is asked to show,
                // even though the view itself already reads as visible.
                if (v->Visible == visible && !(visible && p->Hidden))
                {
                    if (visible && focus)
                    {
                        if (p->ActiveTab != vi)
                        {
                            p->ActiveTab = vi;
                            LayoutDirty  = true;
                        }
                        FocusPanel(i);
                    }
                    return;
                }
                v->Visible = visible;
                if (!visible)
                {
                    // Advance ActiveTab to the next visible tab.
                    if (p->ActiveTab == vi)
                    {
                        for (uint32_t ni = 0; ni < p->ViewCount; ++ni)
                        {
                            if (p->Views[ni] && p->Views[ni]->Visible)
                            {
                                p->ActiveTab = ni;
                                break;
                            }
                        }
                    }
                    // If no views remain visible, collapse the panel dock node.
                    bool any_visible = false;
                    for (uint32_t vi2 = 0; vi2 < p->ViewCount; ++vi2)
                        if (p->Views[vi2] && p->Views[vi2]->Visible)
                        {
                            any_visible = true;
                            break;
                        }
                    if (!any_visible && PendingCloseCount < kMaxPanels)
                    {
                        p->Hidden                             = true;
                        PendingCloseKeys[PendingCloseCount++] = p->DockKey;
                    }
                }
                else if (visible)
                {
                    // Bring the newly shown tab into focus.
                    p->ActiveTab = vi;
                    if (focus)
                        FocusPanel(i);
                    if (p->Hidden)
                    {
                        // Re-dock the panel when the first view becomes visible again.
                        p->Hidden = false;
                        // A close and reopen can occur before the next BuildUI consumes
                        // its close queue. Cancel that close instead of removing the new leaf.
                        for (uint32_t pending = 0; pending < PendingCloseCount;)
                        {
                            if (PendingCloseKeys[pending] == p->DockKey)
                                PendingCloseKeys[pending] = PendingCloseKeys[--PendingCloseCount];
                            else
                                ++pending;
                        }
                        if (DockTree && !ZUIDockFindLeaf(DockTree, p->DockKey))
                        {
                            ZUIDockNode* tgt = FindLargestLeaf(DockTree->Root);
                            if (tgt)
                                ZUIDockSplitH(DockTree, tgt, 0.5f, tgt->ContentKey, p->DockKey);
                        }
                    }
                }
                LayoutDirty = true;
                return;
            }
        }

        // Fall back: whole-panel visibility (Hierarchy, Console, Inspector, Project).
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel*   p = &Panels[i];
            const char* t = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "";
            if (ZEngine::Helpers::secure_strcmp(t, name) != 0)
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
            return;
        }
    }

    uint32_t ZUIPanelManager::GetDocumentTabs(const char* anchor_title, ZUIDocumentTab* out, uint32_t cap) const
    {
        if (!anchor_title || !out)
            return 0;
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            const ZUIPanel* p = &Panels[i];
            if (p->Hidden)
                continue;
            bool found = false;
            for (uint32_t vi = 0; vi < p->ViewCount && !found; ++vi)
                found = p->Views[vi] && ZEngine::Helpers::secure_strcmp(p->Views[vi]->Title, anchor_title) == 0;
            if (!found)
                continue;
            uint32_t n = 0;
            for (uint32_t vi = 0; vi < p->ViewCount && n < cap; ++vi)
            {
                const ZUIPanelView* v = p->Views[vi];
                if (!v || !v->Visible)
                    continue;
                out[n++] = {v->Title, v->Icon, vi == p->ActiveTab, v->Closeable};
            }
            return n;
        }
        return 0;
    }

    bool ZUIPanelManager::IsPanelVisible(const char* name) const
    {
        // First: per-view visibility.
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            const ZUIPanel* p = &Panels[i];
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
            {
                const ZUIPanelView* v = p->Views[vi];
                if (v && v->MatchesTitle(name))
                    return v->Visible && !p->Hidden;
            }
        }
        // Fall back: whole-panel visibility.
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            const ZUIPanel* p = &Panels[i];
            const char*     t = (p->ViewCount > 0 && p->Views[0]) ? p->Views[0]->Title : "";
            if (ZEngine::Helpers::secure_strcmp(t, name) == 0)
                return !p->Hidden;
        }
        return false;
    }

} // namespace ZEngine::UI
