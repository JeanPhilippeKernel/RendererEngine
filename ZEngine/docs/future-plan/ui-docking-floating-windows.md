# ZUI — ImGui Floating-Window Docking Model

**Status:** Planning  
**Branch target:** `feature/zui-floating-dock`  
**Priority:** P2 — Major editor UX improvement  
**Depends on:**
- ZUI Style System (`ZUIStyle` struct) — **done** on `feature/zui`
- ZUI Docking v3 (split tree, tab metrics, central node) — **done** on `feature/zui`
- ZUI Panel close deferred queue (`PendingCloseKeys`) — **done** on `feature/zui`

**Estimated effort:** 6–7 engineering days  
**Author:** (assign when work begins)  
**Last updated:** 2026-08-27

---

## 1. Motivation

The current ZUI docking system is VS Code / RAD Debugger-style: panels live
permanently in a binary split tree. There is no concept of a "floating" panel.
The user cannot detach a panel to a free-floating window, rearrange it freely,
or have it overlap other panels.

ImGui's model is the opposite: every window starts floating and can be *optionally*
docked into a `DockSpace`. This gives the user full freedom to:

- Float any panel anywhere on screen
- Resize floating panels independently
- Stack floating panels (z-order)
- Re-dock floating panels into the split tree at any time
- Have a clean "3D viewport" central node that the floating layer never obscures

---

## 2. Architecture Overview

### 2.1 Conceptual Mapping

| ZUI concept | ImGui equivalent | Notes |
|---|---|---|
| `ZUIPanel` | `ImGuiWindow` | Holds views (tabs), can be docked or floating |
| `ZUIDockNode` | `ImGuiDockNode` | Node in split tree; leaf = one panel slot |
| `ZUIPanelView` | ImGui window content | The actual rendered content |
| `ZUIBeginDockSpace` (new) | `ImGui::DockSpace()` | Background area accepting docked panels |
| Central node (`IsCentral`) | `ImGuiDockNodeFlags_CentralNode` | Viewport passthrough |
| `BuildFloatingPanel` (new) | ImGui window render | Free-floating panel chrome + content |

### 2.2 Render Pass Model (new: 3 passes)

```
Frame N:
┌──────────────────────────────────────────────────────┐
│  Pass 1: Docked panels (split-tree order)            │
│    └─ BuildDockedPanel for each non-hidden docked p  │
│                                                      │
│  Pass 2: Floating panels (sorted by ZOrder asc)      │
│    └─ BuildFloatingPanel for each IsFloating panel   │
│       Highest ZOrder rendered last = visually on top │
│                                                      │
│  Pass 3: Popups (menus, combos, tooltips)            │
│    └─ Root-level float boxes added by ZUIBeginPopup  │
│       Must be LAST or floating windows cover menus   │
└──────────────────────────────────────────────────────┘
```

### 2.3 Panel State Machine

```
                  ┌──────────────┐
                  │   HIDDEN     │◄────────── Close (× button)
                  └──────────────┘
                        │ Restore (Window menu)
                        ▼
                  ┌──────────────┐  Undock gesture
  Initial ───────►│   DOCKED     │──────────────────►┌─────────────┐
                  │  (split tree)│◄──────────────────│  FLOATING   │
                  └──────────────┘  Drop on dockspace └─────────────┘
                                                            │
                                                      Close (× button)
                                                            │
                                                            ▼
                                                      ┌──────────────┐
                                                      │   HIDDEN     │
                                                      └──────────────┘
```

---

## 3. Data Model Changes

### 3.1 `ZUIPanel` additions (ZUIPanel.h)

```cpp
struct ZUIPanel
{
    // ── existing fields (unchanged) ──────────────────────────────
    uint64_t      DockKey   = 0;
    ZUIPanelView* Views[kMaxTabsPerPanel] = {};
    uint32_t      ViewCount = 0;
    uint32_t      ActiveTab = 0;
    bool          Hidden    = false;
    bool          ReorderActive = false;
    uint32_t      ReorderTabIdx = 0;
    float         ReorderAccumX = 0.f;

    // ── NEW: floating window state ────────────────────────────────
    bool     IsFloating          = false;   // true = not in split tree

    // Position and size in screen-space logical pixels
    float    FloatX              = 60.f;
    float    FloatY              = 60.f;
    float    FloatW              = 400.f;
    float    FloatH              = 300.f;

    // Z-ordering: higher value = rendered on top; updated on click
    uint32_t ZOrder              = 0;

    // Title-bar drag (move the floating window)
    bool     FloatDragging       = false;
    float    FloatDragOffX       = 0.f;    // offset from FloatX at drag start
    float    FloatDragOffY       = 0.f;

    // Bottom-right resize grip
    bool     FloatResizing       = false;
    float    FloatResizeOrigW    = 0.f;    // FloatW at resize start
    float    FloatResizeOrigH    = 0.f;
};
```

### 3.2 `ZUIPanelManager` additions (ZUIPanel.h)

```cpp
struct ZUIPanelManager
{
    // ... existing fields ...

    // NEW: z-order counter — increment on every click-to-raise
    uint32_t NextZOrder = 0;

    // NEW: helper — true if panel p can be undocked without leaving
    // the split tree empty (at least one non-floating non-hidden
    // non-central panel must remain docked).
    bool CanUndock(const ZUIPanel* p) const;
};
```

### 3.3 `ZUIStyle` additions (ZUIContext.h)

```cpp
// Add to ZUIStyle struct:

// ── Floating windows ────────────────────────────────────────────
float  FloatWindowMinSize[2]  = {120.f, 80.f};  // ImGui: WindowMinSize
float  ResizeGripSize         = 14.f;            // corner grip pixel size
float  ResizeGripRounding     = 4.f;             // corner grip rounding
```

Also update the `WindowRounding` default from `0.f` to `4.f` — floating windows
should have rounded corners (like popups) to visually distinguish them from the
flat docked panels.

---

## 4. Three-Pass Render (BuildUI)

### 4.1 Full BuildUI skeleton after changes

```cpp
void ZUIPanelManager::BuildUI(ZUIContext* ctx, float menu_h, float status_h)
{
    // ── Deferred closes (before layout) ─────────────────────────
    for (uint32_t ci = 0; ci < PendingCloseCount; ++ci)
    {
        ZUIPanel* cp = FindPanel(PendingCloseKeys[ci]);
        if (cp) { cp->Hidden = true; }
        if (!cp || !cp->IsFloating)  // only collapse if was docked
        {
            ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, PendingCloseKeys[ci]);
            if (leaf) ZUIDockCollapseLeaf(DockTree, leaf);
        }
        LayoutDirty = true;
    }
    PendingCloseCount = 0;

    // ── Layout ───────────────────────────────────────────────────
    if (DockTree)
    {
        float root_rect[4] = { 0.f, menu_h, sw, sh - status_h };
        ZUIDockLayout(DockTree, root_rect);
        SyncSplitDividers();
    }

    ZUIBox* bg = ZUIBeginColumn(ctx, "##pm_bg", ...);  // WindowBg

    BuildMenuBar(ctx, sw, menu_h);

    // ── PASS 1: Docked panels ────────────────────────────────────
    Drag.HoverNode = nullptr;
    Drag.DropZone  = ZUIDropZone::None;
    if (DockTree)
    {
        for (uint32_t i = 0; i < PanelCount; ++i)
        {
            ZUIPanel* p = &Panels[i];
            if (p->Hidden || p->IsFloating) { continue; }  // ← skip floats
            float r[4];
            if (!ZUIDockRectForKey(DockTree, p->DockKey, r)) { continue; }
            // hover detection for drop zones
            BuildDockedPanel(ctx, p, r);
            if (ctx->MousePressed[0] && hit_test(ctx, r))
                FocusPanel(i);
        }
    }

    BuildDividers(ctx);

    // ── Status bar ───────────────────────────────────────────────
    BuildStatusBar(ctx, sw, sh, status_h);

    // ── PASS 2: Floating panels (sorted by ZOrder) ───────────────
    {
        ZUIPanel* sorted[kMaxPanels]; uint32_t fc = 0;
        for (uint32_t i = 0; i < PanelCount; ++i)
            if (!Panels[i].Hidden && Panels[i].IsFloating)
                sorted[fc++] = &Panels[i];

        // Insertion-sort ascending by ZOrder (highest last = on top)
        for (uint32_t i = 1; i < fc; ++i)
            for (uint32_t j = i; j > 0 && sorted[j-1]->ZOrder > sorted[j]->ZOrder; --j)
            { auto* t = sorted[j]; sorted[j] = sorted[j-1]; sorted[j-1] = t; }

        for (uint32_t i = 0; i < fc; ++i)
            BuildFloatingPanel(ctx, sorted[i]);
    }

    // ── Drag ghost (both docked and floating drags) ──────────────
    BuildDragGhost(ctx);

    ZUIEndColumn(ctx);  // end bg

    // NOTE: Pass 3 (popups) happens automatically — ZUIBeginPopup
    // adds boxes as the last children of ctx->Root, so they are
    // rendered after bg and after all floating windows. No code
    // change needed here IF floating windows are built inside bg.
    // If floating windows escape to ctx->Root level, popup ordering
    // must be explicitly managed. See Gap #1.
}
```

### 4.2 Popup z-order issue (Gap #1)

`BuildFloatingPanel` uses `ZUI_FloatX | ZUI_FloatY` boxes. If these are added
as children of `##pm_bg`, they render in bg's subtree. Popups use
`ctx->Current = ctx->Root` → added to Root directly → rendered AFTER bg's
entire subtree → popups are always on top. ✓

**Constraint:** floating panel boxes MUST be built inside `##pm_bg` (current
`ZUIBeginColumn`), NOT escaped to `ctx->Root`. Verify this in `BuildFloatingPanel`.

---

## 5. `BuildFloatingPanel` (new function)

### 5.1 Full implementation spec

```cpp
void ZUIPanelManager::BuildFloatingPanel(ZUIContext* ctx, ZUIPanel* p)
{
    const float header_h = ZUIGetFrameHeight(ctx);
    const float grip     = ctx->Style.ResizeGripSize;
    const float grip_r   = ctx->Style.ResizeGripRounding;

    // ── Clamp to screen ──────────────────────────────────────────
    // Allow partially off-screen but keep title bar always on-screen
    const float min_visible = header_h + 4.f;
    p->FloatW = fmaxf(p->FloatW, ctx->Style.FloatWindowMinSize[0]);
    p->FloatH = fmaxf(p->FloatH, ctx->Style.FloatWindowMinSize[1]);
    p->FloatX = fmaxf(-(p->FloatW - min_visible),
                fminf(p->FloatX, ctx->ScreenW - min_visible));
    p->FloatY = fmaxf(0.f,
                fminf(p->FloatY, ctx->ScreenH - min_visible));

    bool focused = (&Panels[FocusedPanelIdx] == p);
    float rect[4] = { p->FloatX, p->FloatY,
                      p->FloatX + p->FloatW, p->FloatY + p->FloatH };

    // ── Outer window box ─────────────────────────────────────────
    char wk[48]; snprintf(wk, sizeof(wk), "##fw_%llx", (ull)p->DockKey);
    ZUIBox* win = ZUIPushBox(ctx, wk, (uint32_t)strlen(wk),
        ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow |
        ZUI_FloatX | ZUI_FloatY | ZUI_Clickable);
    win->Size[0]         = ZPx(p->FloatW);
    win->Size[1]         = ZPx(p->FloatH);
    win->FloatPos[0]     = p->FloatX;
    win->FloatPos[1]     = p->FloatY;
    win->LayoutAxis      = ZUIAxis::Y;
    win->BorderThickness = ctx->Style.WindowBorderSize;
    win->EdgeSoftness    = 0.5f;
    ZUIBoxSetCornerRadius(win, ctx->Style.WindowRounding);
    ZUIBoxSetColorArr(win, ctx->Theme.PanelBg);
    SetBdrArr(win, focused ? ctx->Theme.PanelFocusBorder
                           : ctx->Theme.PanelBorder);

    // ── Tab bar or title strip (same as docked) ──────────────────
    // NOTE: ZUIDockFindLeaf returns nullptr for floating panels.
    // BuildTabBar must handle nullptr gracefully everywhere.
    bool show_tabs = p->ViewCount > 1;  // floating: always auto-hide for single
    if (show_tabs)
    {
        float tab_rect[4] = { rect[0], rect[1], rect[2], rect[1] + header_h };
        BuildTabBar(ctx, p, tab_rect);  // uses rect, not split-tree rect
    }
    else
    {
        BuildFloatingTitleStrip(ctx, p, rect, header_h);  // dedicated function
    }

    // ── Content ──────────────────────────────────────────────────
    ZUIPanelView* view = p->ActiveTab < p->ViewCount
                        ? p->Views[p->ActiveTab] : nullptr;
    if (view)
    {
        ZUIBox* content = ZUIBeginColumn(ctx, "##fwc", ZFill(), ZFill());
        content->Flags  = content->Flags | ZUI_ClipChildren;
        content->EdgeSoftness = 0.f;
        float cr[4] = {
            rect[0] + ctx->Style.WindowPadding[0],
            rect[1] + header_h + ctx->Style.WindowPadding[1],
            rect[2] - ctx->Style.WindowPadding[0],
            rect[3] - grip - ctx->Style.WindowPadding[1]
        };
        view->BuildContent(ctx, cr);
        ZUIEndColumn(ctx);
    }

    // ── Resize grip ───────────────────────────────────────────────
    {
        char gk[48]; snprintf(gk, sizeof(gk), "##fwg_%llx", (ull)p->DockKey);
        ZUIBox* grp = ZUIPushBox(ctx, gk, (uint32_t)strlen(gk),
            ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY);
        grp->Size[0]     = ZPx(grip * 2.f);
        grp->Size[1]     = ZPx(grip * 2.f);
        grp->FloatPos[0] = rect[2] - grip * 2.f;
        grp->FloatPos[1] = rect[3] - grip * 2.f;
        ZUIBoxSetCornerRadius(grp, grip_r);
        ZUIBoxSetColor(grp,
            ctx->Theme.ScrollbarGrab[0], ctx->Theme.ScrollbarGrab[1],
            ctx->Theme.ScrollbarGrab[2], 0.60f);
        ZUISignal gs = ZUISignalFromBox(ctx, grp);
        ZUIPopBox(ctx);

        if (gs.Flags & ZUI_SignalPressed)
        {
            p->FloatResizing    = true;
            p->FloatResizeOrigW = p->FloatW;
            p->FloatResizeOrigH = p->FloatH;
        }
        if (ctx->MouseReleased[0]) p->FloatResizing = false;
        if (p->FloatResizing && (gs.Flags & ZUI_SignalHeld))
        {
            p->FloatW = fmaxf(ctx->Style.FloatWindowMinSize[0],
                              p->FloatResizeOrigW + gs.DragDelta[0]);
            p->FloatH = fmaxf(ctx->Style.FloatWindowMinSize[1],
                              p->FloatResizeOrigH + gs.DragDelta[1]);
        }
    }

    // ── Window-level signal (click to raise) ─────────────────────
    ZUISignal win_sig = ZUISignalFromBox(ctx, win);
    ZUIPopBox(ctx);

    if (win_sig.Flags & ZUI_SignalClicked)
    {
        p->ZOrder = ++NextZOrder;
        for (uint32_t pi = 0; pi < PanelCount; ++pi)
            if (&Panels[pi] == p) { FocusPanel(pi); break; }
    }

    // ── Drop zones when this floating panel is being dragged ──────
    if (p->FloatDragging && Drag.HoverNode)
    {
        ZUIPanel* dst = FindPanel(Drag.HoverNode->ContentKey);
        if (dst && dst != p)
        {
            float dr[4];
            if (ZUIDockRectForKey(DockTree, dst->DockKey, dr))
                BuildDropZones(ctx, dst, dr);
        }
    }
}
```

### 5.2 `BuildFloatingTitleStrip` (new helper)

A single-view floating panel shows a title strip with:
- Drag handle (moves window when held)
- Icon dot
- Title text
- Close button (always visible, not hover-only like docked)

```cpp
void ZUIPanelManager::BuildFloatingTitleStrip(ZUIContext* ctx,
    ZUIPanel* p, float rect[4], float header_h)
{
    // ... similar to existing single-view strip but:
    //   - drag updates FloatX/Y (not Drag.Active)
    //   - close button always visible (not hover-only)
    //   - NO drag-to-dock trigger (floating move is different from docked drag)
}
```

---

## 6. Undock Gesture (docked → floating)

### 6.1 Gesture state machine in `BuildTabBar`

```
Tab held + mouse moved:
│
├─ |total_drag| < DockingTabReorderThreshold
│    → nothing (wait)
│
├─ dx > dy * 1.5  AND  |total_drag| > DockingTabReorderThreshold
│    → tab REORDER within bar (existing behavior, unchanged)
│
├─ dy > DockingUndockVertical  (e.g. 24px downward)
│    → UNDOCK: create floating panel
│
└─ |total_drag| > DockingDragThreshold  AND  !mostly_horizontal
     → whole-panel drag / dock-tree drag (existing Drag.Active path)
```

Note: `DockingUndockVertical` is increased from `12.f` to `24.f` to prevent
accidental undocks during diagonal reorder drags.

### 6.2 Undock implementation

```cpp
// In BuildTabBar, inside tab drag handling:
if (dy > ctx->Style.DockingUndockVertical && !Drag.Active && CanUndock(p))
{
    ZUIPanelView* view = p->Views[ti];

    // Determine the panel that will float:
    // If this is the only tab, float the existing panel (retain DockKey).
    // If this is one of multiple tabs, create a new floating panel.
    ZUIPanel* float_p = nullptr;
    if (p->ViewCount == 1)
    {
        // Float the panel itself — retain DockKey, leaf will be collapsed
        p->IsFloating        = true;
        p->FloatX            = ctx->MousePos[0] - ZUIGetFramePadX(ctx) * 2.f;
        p->FloatY            = ctx->MousePos[1] - ZUIGetFrameHeight(ctx) * 0.5f;
        p->FloatW            = fmaxf(ctx->Style.FloatWindowMinSize[0], 320.f);
        p->FloatH            = fmaxf(ctx->Style.FloatWindowMinSize[1], 240.f);
        p->ZOrder            = ++NextZOrder;
        p->FloatDragging     = true;
        p->FloatDragOffX     = ctx->MousePos[0] - p->FloatX;
        p->FloatDragOffY     = ctx->MousePos[1] - p->FloatY;
        float_p              = p;
        // Collapse the leaf (panel leaves the split tree)
        if (PendingCloseCount < kMaxPanels)
            PendingCloseKeys[PendingCloseCount++] = p->DockKey;
    }
    else
    {
        // Create a new floating panel with a derived key
        // Use original DockKey XOR'd with view index for uniqueness
        uint64_t new_key = p->DockKey ^ ZUIDockHashName(view->Title ? view->Title : "panel");
        ZUIPanel* new_p = AddPanel(new_key);
        if (new_p)
        {
            AddView(new_p, view);
            // Remove view from source
            for (uint32_t j = ti; j+1 < p->ViewCount; ++j) p->Views[j] = p->Views[j+1];
            --p->ViewCount;
            if (p->ActiveTab >= p->ViewCount && p->ViewCount > 0)
                p->ActiveTab = p->ViewCount - 1;

            new_p->IsFloating    = true;
            new_p->FloatX        = ctx->MousePos[0] - ZUIGetFramePadX(ctx) * 2.f;
            new_p->FloatY        = ctx->MousePos[1] - ZUIGetFrameHeight(ctx) * 0.5f;
            new_p->FloatW        = 320.f;
            new_p->FloatH        = 240.f;
            new_p->ZOrder        = ++NextZOrder;
            new_p->FloatDragging = true;
            new_p->FloatDragOffX = ctx->MousePos[0] - new_p->FloatX;
            new_p->FloatDragOffY = ctx->MousePos[1] - new_p->FloatY;
            float_p = new_p;
        }
    }
    LayoutDirty = true;
    break; // exit tab loop
}
```

### 6.3 `CanUndock()` implementation

```cpp
bool ZUIPanelManager::CanUndock(const ZUIPanel* p) const
{
    // Must leave at least one docked, non-hidden, non-central panel
    uint32_t remaining_docked = 0;
    for (uint32_t i = 0; i < PanelCount; ++i)
    {
        const ZUIPanel* q = &Panels[i];
        if (q == p || q->Hidden || q->IsFloating) continue;
        ZUIDockNode* leaf = ZUIDockFindLeaf(DockTree, q->DockKey);
        if (leaf && !leaf->IsCentral) remaining_docked++;
    }
    return remaining_docked >= 1;
}
```

---

## 7. Floating Window Move (title-bar drag)

### 7.1 Move via title strip drag

In `BuildFloatingTitleStrip`:

```cpp
ZUISignal strip_sig = ZUISignalFromBox(ctx, strip);
ZUIEndRow(ctx);

if (strip_sig.Flags & ZUI_SignalPressed)
{
    p->FloatDragging = true;
    p->FloatDragOffX = ctx->MousePos[0] - p->FloatX;
    p->FloatDragOffY = ctx->MousePos[1] - p->FloatY;
    p->ZOrder        = ++NextZOrder;
}

if (ctx->MouseReleased[0]) p->FloatDragging = false;

if (p->FloatDragging && ctx->MouseDown[0])
{
    p->FloatX = ctx->MousePos[0] - p->FloatDragOffX;
    p->FloatY = ctx->MousePos[1] - p->FloatDragOffY;

    // Check if hovering over a docked panel → enable drop zones
    for (uint32_t i = 0; i < PanelCount; ++i)
    {
        ZUIPanel* q = &Panels[i];
        if (q->Hidden || q->IsFloating) continue;
        float r[4];
        if (!ZUIDockRectForKey(DockTree, q->DockKey, r)) continue;
        if (ctx->MousePos[0] >= r[0] && ctx->MousePos[0] <= r[2] &&
            ctx->MousePos[1] >= r[1] && ctx->MousePos[1] <= r[3])
        {
            // Set Drag state so BuildDropZones fires in BuildFloatingPanel
            Drag.HoverNode  = ZUIDockFindLeaf(DockTree, q->DockKey);
            Drag.SrcPanel   = p;
            Drag.SrcTabIdx  = kWholePanel;
            Drag.GhostX     = ctx->MousePos[0];
            Drag.GhostY     = ctx->MousePos[1];
            // Note: Drag.Active intentionally NOT set here —
            // floating move and docked-tab drag must not conflict
            break;
        }
    }
    if (!ctx->MouseDown[0]) Drag.HoverNode = nullptr;
}
```

---

## 8. Redock Gesture (floating → docked)

### 8.1 Trigger in BuildFloatingPanel

Drop zones are shown when `p->FloatDragging && Drag.HoverNode`. On
`ctx->MouseReleased[0]` with a valid `Drag.DropZone`:

```cpp
// At bottom of BuildFloatingPanel:
if (ctx->MouseReleased[0] && p->FloatDragging && Drag.HoverNode
    && Drag.DropZone != ZUIDropZone::None)
{
    CommitDrop(p, kWholePanel, Drag.HoverNode, Drag.DropZone);
    p->FloatDragging = false;
    Drag.HoverNode   = nullptr;
    Drag.DropZone    = ZUIDropZone::None;
}
```

### 8.2 `CommitDrop` changes for floating source

```cpp
// In CommitDrop, at the top — handle floating source:
if (src->IsFloating)
{
    // Floating panel re-enters the split tree
    src->IsFloating = false;
    // No ZUIDockCollapseLeaf needed — wasn't in tree
    // Insert into tree at dst via existing split logic
    // ... (existing Left/Right/Top/Bottom/Center code) ...
    LayoutDirty = true;
    return;
}
// ... existing docked-source logic follows ...
```

### 8.3 Docked tab dropped onto floating panel (Gap #13 fix)

```cpp
// In CommitDrop, when dst node belongs to a floating panel:
ZUIPanel* dst_panel = FindPanel(dst->ContentKey);
if (dst_panel && dst_panel->IsFloating && zone == ZUIDropZone::Center)
{
    // Merge source views into floating panel's tab list
    for (uint32_t i = 0; i < src->ViewCount; ++i)
        AddView(dst_panel, src->Views[i]);
    src->ViewCount = 0;
    if (PendingCloseCount < kMaxPanels)
        PendingCloseKeys[PendingCloseCount++] = src->DockKey;
    dst_panel->ZOrder = ++NextZOrder;
    return;
}
// Edge drops onto floating panel: not supported in v1 — ignore
if (dst_panel && dst_panel->IsFloating)
    return;
```

---

## 9. Central Node (Viewport passthrough)

Re-enable central node for Viewport in `ZUIPanelManagerComponent.h`:

```cpp
// After all panels registered and layout loaded:
ZUIDockMarkCentral(Manager.DockTree, ZUIDockHashName("Viewport"));
```

The Viewport panel is docked (not floating) and marked central. Its
`BuildDockedPanel` path skips chrome and passes the full rect to
`view->BuildContent`. Mouse events in the central area that are not
captured by any floating window reach the 3D scene (`ctx->ViewportHovered`).

---

## 10. `ZUIDockSerial` v4

### 10.1 Save format additions

```
# ZUI Layout v4
node ...  (unchanged from v3 — only docked panels have nodes)
panel <key_hex> <active_tab> <hidden> <view_count>
view <title>
floating <is_floating> <float_x> <float_y> <float_w> <float_h> <zorder>
```

`floating` line is only written when `p->IsFloating == true`. Floating panels
have NO corresponding `node` line.

### 10.2 Load changes

```cpp
// When parsing a panel record:
if (is_floating)
{
    p->IsFloating = true;
    p->FloatX = float_x; p->FloatY = float_y;
    p->FloatW = float_w; p->FloatH = float_h;
    p->ZOrder = zorder;
    // Do NOT try to find a leaf or collapse anything
}
else
{
    // Docked: find leaf in rebuilt tree, collapse if hidden (existing logic)
}
```

---

## 11. Known Gaps

Each gap has a severity, phase, and resolution strategy.

| # | Gap | Severity | Phase | Resolution |
|---|---|---|---|---|
| 1 | Popup z-ordering: floating windows could cover popups | **Critical** | Phase 2 | Build floating panels inside `##pm_bg`, not escaped to Root. Popups use Root → always rendered last. Verify and add a note/assert. |
| 2 | `ZUIDockFindLeaf` nullptr in `BuildTabBar` | **Critical** | Phase 3 | Audit all 5+ `ZUIDockFindLeaf` calls in `BuildTabBar`/`BuildDockedPanel`. Add `if (!leaf) { /* fallback */ }` at each site. |
| 3 | `PendingCloseKeys` calls `ZUIDockCollapseLeaf` for floating panels | **Critical** | Phase 1 | Add `if (!cp || !cp->IsFloating)` guard before `ZUIDockCollapseLeaf` in the flush loop. |
| 4 | `Drag.Active` not set during floating window move | **Critical** | Phase 4 | Explicitly set `Drag.HoverNode` (but NOT `Drag.Active`) when floating panel hovers over docked panel. `BuildDropZones` check updated to also fire when `p->FloatDragging && Drag.HoverNode`. |
| 5 | Stable `DockKey` for new floating panels | **Critical** | Phase 5 | Single-tab undock: retain original `DockKey`. Multi-tab undock: XOR original key with `ZUIDockHashName(view->Title)`. |
| 6 | `rect[4]` for floating `BuildTabBar` | **Critical** | Phase 3 | `BuildFloatingPanel` passes `{ FloatX, FloatY, FloatX+FloatW, FloatY+header_h }` explicitly. Document this clearly in the function contract. |
| 7 | Gesture disambiguation: reorder vs undock | Important | Phase 5 | Full state machine documented in §6.1. `DockingUndockVertical` raised to 24px. |
| 8 | 1-frame undock visual flash | Important | Phase 5 | Use immediate float creation (not deferred). Floating panel appears frame N; source collapse is deferred to frame N+1. Net: 1-frame overlap acceptable at 60fps. |
| 9 | `CanUndock()` guard implementation | Important | Phase 5 | Implementation in §6.3. Count non-floating non-hidden non-central docked panels ≥ 1. |
| 10 | `FloatWindowMinSize` / `ResizeGripSize` in `ZUIStyle` | Important | Phase 1 | Added to §3.3. |
| 11 | Serial v4: floating panels have no node entry | Important | Phase 8 | Loader handles `floating` line before `view` lines. Skip tree insertion for floating panels. |
| 12 | Window menu does not list floating panels | Important | Phase 9 | Show `"Panel (floating)##wm_i"` entries; add "Dock all" menu item. |
| 13 | Docked tab dropped onto floating panel | Important | Phase 6 | Handled in `CommitDrop` — Center drop merges into float; Edge drops ignored in v1. |
| 14 | `WindowRounding = 0` makes floating windows look like docked panels | Style | Phase 1 | Change `ZUIStyle.WindowRounding` default to `4.f`. |
| 15 | Off-screen recovery: floating panel fully off-screen | Style | Phase 9 | Clamp in `BuildFloatingPanel`: allow partial off-screen but title bar always accessible. |

---

## 12. Verification Checklist

### Floating window basics
- [ ] Hierarchy, Inspector, Output can each be undocked to float independently
- [ ] Floating panel can be dragged to any on-screen position via title bar
- [ ] Resize via bottom-right grip: width and height change, minimum enforced
- [ ] Click on floating panel raises it to the top (z-order)
- [ ] Two overlapping floating panels: click correct one raises it
- [ ] Floating panel partially dragged off-screen: title bar still accessible
- [ ] Floating panel dragged to bottom of screen: clamp fires correctly

### Dock / undock gestures
- [ ] Horizontal tab drag → tab reorder (existing, not broken)
- [ ] Downward tab drag > 24px → undock, panel floats at cursor
- [ ] Single-tab panel undocked: panel retains original DockKey
- [ ] Multi-tab panel: one tab undocked creates new floating panel
- [ ] Source panel collapses correctly after undock (sibling fills space)
- [ ] Last docked panel cannot be undocked (guard fires, drag ignored)
- [ ] Floating panel dragged over docked panel → teal drop zones appear
- [ ] Drop on Left/Right/Top/Bottom/Center → redocks with correct split
- [ ] Docked tab dragged onto floating panel (Center) → merges into float

### Central node + viewport
- [ ] Viewport panel in central node: no chrome visible
- [ ] Mouse over viewport with no floating windows: camera controller active
- [ ] Mouse over floating window covering viewport: camera inactive (float captures)
- [ ] Float dragged off viewport: camera re-activates

### Popup z-ordering (critical)
- [ ] Open combo inside floating panel → dropdown appears on top of all windows
- [ ] Open menu bar → menu appears on top of all floating panels
- [ ] Click floating panel while menu open → menu closes, panel raised

### Persistence (v4 serial)
- [ ] Layout saved with floating state in `# ZUI Layout v4` format
- [ ] Relaunch: floating panels restore at correct positions and sizes
- [ ] Relaunch: floating z-order higher than docked panels
- [ ] Relaunch: mix of docked and floating panels restores correctly
- [ ] Empty dockspace (all floating): `ZUIDockLayout` no crash, layout recovers

### Edge cases
- [ ] Close a floating panel: disappears (no tree collapse needed)
- [ ] Reopen hidden floating panel from Window menu: appears floating
- [ ] "Dock all" menu item: all floating panels re-inserted into split tree
- [ ] All panels floating: Window menu "Dock all" restores to default layout
- [ ] `FloatW/H` minimum enforced after resize attempts to go below minimum

---

## 13. Sequencing + Time Estimate

| Step | File(s) | Est. time |
|---|---|---|
| 1. `ZUIStyle` + `ZUIPanel` floating fields | `ZUIContext.h`, `ZUIPanel.h` | 2 h |
| 2. Fix `PendingCloseKeys` + `CanUndock()` | `ZUIPanel.cpp` | 1 h |
| 3. Audit + fix nullptr for floating in `BuildTabBar` | `ZUIPanel.cpp` | 2 h |
| 4. `BuildFloatingPanel` (chrome, tabs, resize) | `ZUIPanel.cpp` | 6 h |
| 5. Three-pass render in `BuildUI` | `ZUIPanel.cpp` | 2 h |
| 6. Floating title strip drag (move window) | `ZUIPanel.cpp` | 2 h |
| 7. Undock gesture + `CanUndock` | `ZUIPanel.cpp` | 4 h |
| 8. `CommitDrop` for floating sources + float→float merge | `ZUIPanel.cpp` | 3 h |
| 9. `ZUIDockSerial` v4 | `ZUIDockSerial.cpp` | 3 h |
| 10. Central node reinstatement | `ZUIPanelManagerComponent.h` | 0.5 h |
| 11. Window menu updates + "Dock all" | `ZUIPanel.cpp` | 2 h |
| 12. Edge cases + clamping + off-screen | `ZUIPanel.cpp` | 2 h |
| 13. Full verification pass | — | 4 h |
| **Total** | | **~33 h (~7 days)** |

---

## 14. Deferred (post-v1)

These are intentionally out of scope for the first implementation:

- **Remember original split percentage** before undock — ImGui does this with `SavedDocksizeVec2`. Adds ~1 day complexity.
- **Ctrl+drag to force-float** — keyboard modifier to undock via drag even from within tab bar.
- **Double-click title bar** to toggle float/dock instantly.
- **Snap to screen edges** and "magnetism" near screen corners.
- **Multi-monitor / OS-level windows** — requires per-window Vulkan swapchain. Major effort (2–3 weeks).
- **Floating window animations** — fade-in on undock, spring physics on resize.
