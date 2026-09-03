# ZEngine — UI System (ZUI)

**Priority:** P1 — Editor shell, in-game HUD, menus, debug overlay
**Status:** Done — merged to `develop` via PR #679 (2026-08-31)
**Depends on:** `ArenaAllocator` (done), `Array<T>` (done), `UnorderedHashMap` (done), `VulkanDevice` (done), `RenderGraph` (done), `InputManager` (done), FreeType (font rasterization) + stb_rect_pack (atlas layout only, vendored via stb FetchContent), rapidhash (vendored via FetchContent)
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
| Font atlas | `ZUIFont.h/.cpp` (334 lines) | `ZUIFontAtlasBake` via FreeType rasterization (stb_rect_pack for atlas layout only — stb_truetype was replaced), `ZUIMeasureText`, `TextureHandle`-backed atlas |
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

Progress/misc: `ZUIProgressBar`, `ZUISetTooltip`, `ZUIBeginDisabled`, `ZUIEndDisabled`

**Correction**: several widget names in this table drifted from the doc's original naming during implementation: `ZUITooltip` → `ZUISetTooltip`, `ZUIComboBox` → `ZUIBeginCombo`/`ZUIComboItem`/`ZUIEndCombo`, `ZUISpacing` → `ZUISpacer`. `ZUIColorEdit3` does not exist — only `ZUIColorEdit4` was implemented.

---

## Migrated Editor Panels

**Correction**: this section described an intermediate development snapshot (phases 7–9) that was
later superseded before the PR #679 merge. The panel classes named below
(`ZUIHierarchyViewComponent`, `ZUIInspectorViewComponent`, `ZUIProjectViewComponent`,
`ZUILogComponent`, `ZUISceneViewportComponent`) do not exist in the shipped code. They were
consolidated into a single `Tetragrama/Panels/ZUIPanelManagerComponent` that owns the dock
tree/tabs and delegates to plain panel classes under `Tetragrama/Panels/`.

Only `ZUIDockspaceComponent` and `ZUIStatusBarComponent` remain under
`Tetragrama/Components/ZUI/` as originally described; every other panel moved to
`Tetragrama/Panels/`.

| Panel | File | Notes |
|---|---|---|
| Panel manager | `Tetragrama/Panels/ZUIPanelManagerComponent.h/.cpp` | Owns dock tree/tabs; delegates to the panels below — not in the original migration plan |
| Dockspace | `Tetragrama/Components/ZUI/ZUIDockspaceComponent.h/.cpp` | Main editor layout |
| Hierarchy view | `Tetragrama/Panels/HierarchyPanel.h/.cpp` | Actor outliner |
| Inspector | `Tetragrama/Panels/InspectorPanel.h/.cpp` | Property editor |
| Project view | `Tetragrama/Panels/ProjectViewPanel.h/.cpp` | Content browser |
| Console/log | `Tetragrama/Panels/ConsolePanel.h/.cpp` | Log output |
| Status bar | `Tetragrama/Components/ZUI/ZUIStatusBarComponent.h/.cpp` | Bottom bar |
| Scene viewport | `Tetragrama/Panels/ViewportPanel.h/.cpp` | 3D view — no ImGui/ImGuizmo references; gizmo integration is still the `gizmo-3d-pass.md` follow-up |
| Memory profiler | `Tetragrama/Panels/MemoryProfilerPanel.h/.cpp` | Not in the original plan |
| Asset importer | `Tetragrama/Panels/AssetImporterPanel.h/.cpp` | Not in the original plan |

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

**Correction**: this section describes a plan that was not carried out. There is no `ImGuiPass`
class or registration anywhere in the tracked source, and `ViewportPanel.cpp` (the actual
scene-viewport panel — `SceneViewportUIComponent` does not exist) has zero ImGui/ImGuizmo
references. Repo-wide, the only mention of `ImGui::` is inside a comment in `HierarchyPanel.cpp`.
ImGui and ImGuizmo remain as CMake `FetchContent` dependencies and cached shaders but are not
wired into the active editor render path — the ZUI system fully replaced ImGui in the editor,
contrary to the "permanent coexistence" plan below. Gizmo integration is tracked separately as a
ZUI-native follow-up in `gizmo-3d-pass.md`, not as an ImGui dependency.

---

## File Layout

```
ZEngine/ZEngine/UI/
├── ZUIBox.h                 Box struct, flags, size kinds
├── ZUIContext.h/.cpp        Frame lifecycle, box tree, persistent state
├── ZUIInput.h/.cpp          Feed-based input (engine-agnostic)
├── ZUIKey.h                 Key code enum
├── ZUIInteraction.h/.cpp    Hit-test, hot/active routing, ZUISignal
├── ZUIFont.h/.cpp           Font atlas baking (FreeType rasterization + stb_rect_pack layout)
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
├── Components/ZUI/
│   ├── ZUIComponent.h       Base class
│   ├── ZUIDockspaceComponent.h/.cpp
│   └── ZUIStatusBarComponent.h/.cpp
└── Panels/
    ├── ZUIPanelManagerComponent.h/.cpp   Owns dock tree/tabs, delegates below
    ├── HierarchyPanel.h/.cpp
    ├── InspectorPanel.h/.cpp
    ├── ProjectViewPanel.h/.cpp
    ├── ConsolePanel.h/.cpp
    ├── ViewportPanel.h/.cpp
    ├── MemoryProfilerPanel.h/.cpp
    └── AssetImporterPanel.h/.cpp
```
