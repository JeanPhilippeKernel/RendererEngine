# ZEngine — UI System (ZUI)

**Priority:** P1 — Editor shell, in-game HUD, menus, debug overlay
**Status:** Done — merged to `develop` via PR #679 (2026-08-31)
**Depends on:** `ArenaAllocator` (done), `Array<T>` (done), `UnorderedHashMap` (done), `VulkanDevice` (done), `RenderGraph` (done), `InputManager` (done), stb_truetype (vendored via stb FetchContent), rapidhash (vendored via FetchContent)
**Unblocks:** Gizmo 3D pass (`gizmo-3d-pass.md`), floating window docking (`ui-docking-floating-windows.md`), DebugOverlay, in-game HUD, main menu

---

## Architecture Overview

Every widget is the same struct: `ZUIBox`. No widget class hierarchy. No virtual dispatch.
The system follows the RAD Debugger UI architecture (Ryan Fleury) adapted to ZEngine primitives.

Key design choices made during implementation that diverged from the original plan:

- **Input**: feed-based model (`ZUIFeedMousePos`, `ZUIFeedKey`, etc.) rather than a snapshot `InputFrame`. The `ZUILayer` translates engine events to feed calls directly.
- **Renderer**: `ZUIDrawList` (CPU-side draw list mirroring ImDrawList) consumed by `ZUIRenderer` (standalone Vulkan pass), rather than writing geometry directly into an ImGui draw list.
- **Layout**: 2-pass solver (post-order intrinsic sizes, pre-order extrinsic + positions) rather than the planned 5-phase pass. Simpler and sufficient for current needs.
- **Docking**: a full panel docking system (`ZUIDockspace`, `ZUIDockSerial`) was built on top of the base layer — not in the original spec.

---

## What Was Built

### Core layer — `ZEngine/ZEngine/UI/`

| Component | File | What it does |
|---|---|---|
| Box | `ZUIBox.h` (170 lines) | Single widget primitive — flags, size spec, layout output, style fields |
| Context | `ZUIContext.h/.cpp` (940 lines) | Frame lifecycle, box tree, persistent state hash table, font/theme state |
| Input feed | `ZUIInput.h/.cpp` (147 lines) | `ZUIFeedMousePos/Button/Scroll/Text/Key` — engine-agnostic event intake |
| Key codes | `ZUIKey.h` (110 lines) | Engine-independent key enum |
| Interaction | `ZUIInteraction.h/.cpp` (298 lines) | `ZUIInteractionPass` (hit-test, hot/active routing), `ZUISignalFromBox` |
| Font atlas | `ZUIFont.h/.cpp` (334 lines) | `ZUIFontBake` via stb_truetype, `ZUIMeasureText`, `TextureHandle`-backed atlas |
| Layout | `ZUILayout.h/.cpp` (347 lines) | `ZUILayoutSolve` — 2-pass constraint solver (intrinsic post-order, extrinsic pre-order) |
| Draw list | `ZUIDrawList.h/.cpp` (858 lines) | CPU-side vector draw list: `ZUIDrawVtx/ZUIDrawListCmd`; lines, rects, triangles, text, images with AA fringe |
| Widgets | `ZUIWidgets.h/.cpp` (4,434 lines) | Full widget library (see below) |
| Panel | `ZUIPanel.h/.cpp` (1,679 lines) | Panel management, VS Code-style collapsing sections, drag-to-reorder |
| Dockspace | `ZUIDockspace.h/.cpp` (432 lines) | Panel docking — split, merge, pane sash resize |
| Dock serial | `ZUIDockSerial.h/.cpp` (502 lines) | Serialize and restore dock layout across sessions |

### Renderer — `ZEngine/ZEngine/Rendering/Renderers/ZUIRenderer`

Standalone Vulkan pass registered in `AppRenderPipeline`:
- `ZUICtx` and `ZUIRenderPayload` on `AppRenderPipeline`
- Shaders: `Resources/Shaders/zui_draw.vert` + `zui_draw.frag` (compiled SPV in `Cache/`)
- Vertex layout: `ZUIDrawVtx` (20 bytes — pos xy, uv, RGBA8 col)

### Widget inventory (`ZUIWidgets.h`)

Layout containers: `ZUIBeginColumn`, `ZUIBeginRow`, `ZUIBeginScrollRegion`

Display: `ZUILabel`, `ZUISeparator`, `ZUISpacing`, `ZUIImage`

Interactive: `ZUIButton`, `ZUISmallButton`, `ZUIInvisibleButton`, `ZUIImageButton`, `ZUICheckbox`, `ZUIToggle`, `ZUIRadioButton`

Input: `ZUIDragFloat`, `ZUIDragFloat3`, `ZUISliderFloat`, `ZUIInputText`, `ZUIColorEdit3`, `ZUIColorEdit4`

Selection: `ZUISelectable`, `ZUITreeNode`, `ZUICollapsingHeader`, `ZUIComboBox`, `ZUIBeginListBox`, `ZUIBeginTreeView`, `ZUIBeginGridView`

Table/data: `ZUIBeginDataTable`, `ZUIDataTableHeader`, `ZUIDataTableGetSortSpecs`

Popup/menu: `ZUIBeginPopup`, `ZUIOpenPopup`, `ZUIClosePopup`, `ZUIBeginMenu`, `ZUIMenuItem`, `ZUIBeginContextMenu`

Progress/misc: `ZUIProgressBar`, `ZUITooltip`, `ZUIBeginDisabled`, `ZUIEndDisabled`

---

## Migrated Editor Panels — `Tetragrama/Components/ZUI/`

All panels below use `ZUIContext` directly. `ZUILayer` wires engine events to the context.

| Panel | File | Notes |
|---|---|---|
| Dockspace | `ZUIDockspaceComponent.h/.cpp` | Main editor layout |
| Hierarchy view | `ZUIHierarchyViewComponent.h/.cpp` | Actor outliner |
| Inspector | `ZUIInspectorViewComponent.h/.cpp` | Property editor |
| Project view | `ZUIProjectViewComponent.h/.cpp` | Content browser |
| Log | `ZUILogComponent.h/.cpp` | Log output |
| Status bar | `ZUIStatusBarComponent.h/.cpp` | Bottom bar |
| Scene viewport | `ZUISceneViewportComponent.h/.cpp` | 3D view (ImGuizmo still in ImGui) |

`ZUILayer` (`Tetragrama/Layers/ZUILayer.h/.cpp`) — engine layer; implements `IMouseEventCallback`, `IKeyboardEventCallback`, `ITextInputEventCallback`; routes to `ZUIFeed*` calls; owns the `ZUIContext*`.

---

## What Remains

### DebugOverlay

A runtime overlay (F3 toggle) showing FPS, frame time, memory arena bars. Should be a `ZUIComponent` that reads from `MemoryProfiler`. First milestone after current panel migration is complete.

Target files:
- `Tetragrama/Components/ZUI/ZUIDebugOverlayComponent.h/.cpp`
- Toggle wired through `ZUIFeedKey` in `ZUILayer`

### DebugConsole

Tilde-toggle console (Debug builds only). Input field + scrollable log. A `ZUIComponent` with `ZUIInputText` + `ZUIBeginScrollRegion`.

Target files:
- `Tetragrama/Components/ZUI/ZUIDebugConsoleComponent.h/.cpp`

### UIScreenStack (in-game menus)

For shipping a game: main menu, pause menu, settings screen. Not needed for the editor. Design is unchanged from the original spec — a stack of `ZUIScreen` objects each calling `ZUIBeginFrame` / `ZUIEndFrame`.

Target files when needed:
- `ZEngine/ZEngine/UI/ZUIScreen.h`
- `ZEngine/ZEngine/UI/ZUIScreenStack.h/.cpp`

---

## ImGui Coexistence

ImGui and ImGuizmo remain in the editor permanently. Both renderers register as separate passes:
- `ImGuiPass` runs before `ZUIPass`
- `SceneViewportUIComponent` keeps ImGui + ImGuizmo (gizmos depend on ImGui draw lists)
- The ZUI system is additive — it does not replace ImGui in the editor

---

## File Layout

```
ZEngine/ZEngine/UI/
├── ZUIBox.h                 Box struct, flags, size kinds
├── ZUIContext.h/.cpp        Frame lifecycle, box tree, persistent state
├── ZUIInput.h/.cpp          Feed-based input (engine-agnostic)
├── ZUIKey.h                 Key code enum
├── ZUIInteraction.h/.cpp    Hit-test, hot/active routing, ZUISignal
├── ZUIFont.h/.cpp           Font atlas baking (stb_truetype)
├── ZUILayout.h/.cpp         2-pass constraint solver
├── ZUIDrawList.h/.cpp       CPU-side draw list → ZUIRenderer
├── ZUIWidgets.h/.cpp        Full widget library
├── ZUIPanel.h/.cpp          Panel management, section drag-to-reorder
├── ZUIDockspace.h/.cpp      Panel docking
└── ZUIDockSerial.h/.cpp     Dock layout persistence

ZEngine/ZEngine/Rendering/Renderers/
└── ZUIRenderer.h/.cpp       Vulkan pass consuming ZUIDrawList output

Resources/Shaders/
├── zui_draw.vert            Screen-space vertex transform
├── zui_draw.frag            Texture + solid color fragment
└── Cache/
    ├── zui_draw_vertex.spv
    └── zui_draw_fragment.spv

Tetragrama/
├── Layers/ZUILayer.h/.cpp   Engine layer — event routing, context ownership
└── Components/ZUI/
    ├── ZUIComponent.h       Base class
    ├── ZUIDockspaceComponent.h/.cpp
    ├── ZUIHierarchyViewComponent.h/.cpp
    ├── ZUIInspectorViewComponent.h/.cpp
    ├── ZUIProjectViewComponent.h/.cpp
    ├── ZUILogComponent.h/.cpp
    ├── ZUIStatusBarComponent.h/.cpp
    └── ZUISceneViewportComponent.h/.cpp
```
