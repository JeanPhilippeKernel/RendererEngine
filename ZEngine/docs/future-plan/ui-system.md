# ZEngine — UI System

**Priority:** P1 — Required for menus, HUD, and any text displayed to the player
**Status:** Design
**Depends on:** `text-rendering.md`, `actor-ecs-architecture.md`, `render-resource-manager.md`
**Blocks:** All in-game UI, main menu, settings screen, HUD
**Approach:** Immediate-mode retained widget tree — no external UI library dependency

---

## 1. Overview

ZEngine needs a UI system that integrates with the existing Vulkan render pipeline, obeys
the no-new/delete rule, works without exceptions or RTTI, and is authored purely in C++20.
Dear ImGui is explicitly excluded: it uses `new`/`delete` internally, has its own renderer
backend that duplicates ZEngine's RenderGraph infrastructure, and pulls in ~15k LOC of
dependency.

This document specifies:

1. A lightweight **immediate-mode API** (`UIContext`) that game code calls each frame.
2. A **retained widget tree** that `UIContext` builds internally so layout is stable.
3. A **`UIDrawList`** produced by the retained tree and consumed by a dedicated **`UIRenderer`**
   render-graph pass.
4. A **`UIScreenStack`** for menu navigation.
5. Full arena-allocation strategy so no UI state survives frame boundaries on the heap.

---

## 2. Design Choice

### 2.1 Immediate-mode API, retained internal tree

Game code follows the immediate-mode style: call `ctx.Label(...)`, `ctx.Button(...)` each
frame. No persistent widget handles. No retained widget objects in game code.

Internally, `UIContext` builds a frame-local widget tree in a per-frame arena. The tree is
used to resolve layout, compute hover/press state, and sort draw calls by Z-order and
texture atlas. The arena is wiped at `UIContext::Begin()` so there is zero persistent heap
pressure.

This gives programmers the ergonomics of ImGui while keeping the allocation model consistent
with the rest of ZEngine.

### 2.2 Why no external library

- **No `new`/`delete`**: Dear ImGui, Qt, and similar libraries use the default allocator.
  Wrapping them behind an allocator shim is fragile and unsupported.
- **No virtual dispatch in hot path**: Custom widget types are tagged unions, not vtable
  hierarchies. The render pass iterates a flat `UIDrawList` — no per-widget virtual call.
- **No RTTI**: Widget type dispatch uses an explicit `UIWidgetType` enum.
- **RenderGraph integration**: The UI render pass is a first-class `IRenderGraphCallbackPass`,
  sharing the same command buffer management, synchronisation, and resource lifetime tracking
  as the 3D scene.

### 2.3 Render order

```
RenderGraph frame:
  ├── SceneGeometryPass          (3D opaque geometry)
  ├── TransparencyPass           (3D transparent geometry)
  ├── PostProcessPass            (bloom, tone-map, etc.)
  └── UIPass                     ← this document's render pass (orthographic, no depth test)
```

The UI pass runs last and reads the final scene colour attachment as input for any
background-blur effects.

---

## 3. Coordinate System

All UI coordinates are **screen-space pixels**, top-left origin, Y increasing downward.

```
(0,0) ──────────────────► X
  │
  │   UIRect { 10, 10, 200, 40 }   ← x=10, y=10, w=200, h=40
  │
  ▼ Y
```

`UIRect`:

```cpp
// ZEngine/UI/UITypes.h
#pragma once
#include <Core/Maths/Vec2f.h>
#include <Core/Maths/Vec4f.h>
#include <cstdint>

namespace ZEngine::UI {

    struct UIRect {
        float X{0.0f};
        float Y{0.0f};
        float Width{0.0f};
        float Height{0.0f};

        [[nodiscard]] float Right()  const noexcept { return X + Width; }
        [[nodiscard]] float Bottom() const noexcept { return Y + Height; }

        [[nodiscard]] bool Contains(float px, float py) const noexcept {
            return px >= X && px < Right() && py >= Y && py < Bottom();
        }

        [[nodiscard]] bool IsValid() const noexcept {
            return Width > 0.0f && Height > 0.0f;
        }
    };

    enum class UIAnchor : uint8_t {
        TopLeft,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
    };

    // Z-order for layering. Higher value renders on top.
    using UILayer = uint8_t;
    static constexpr UILayer kUILayerBackground = 0;
    static constexpr UILayer kUILayerDefault    = 128;
    static constexpr UILayer kUILayerOverlay    = 200;
    static constexpr UILayer kUILayerDebug      = 255;

} // namespace ZEngine::UI
```

---

## 4. Core Widget Types

All widget structs are value types. They are allocated from the per-frame arena inside
`UIContext`. Game code never holds a pointer to one past the frame boundary.

```cpp
// ZEngine/UI/UIWidgets.h
#pragma once
#include <UI/UITypes.h>
#include <Core/Containers/String.h>
#include <Core/Containers/Array.h>
#include <Core/Maths/Vec4f.h>
#include <cstdint>

namespace ZEngine::UI {

    // ── UILabel ─────────────────────────────────────────────────────────────
    struct UILabel {
        Core::Containers::String Text;
        Core::Maths::Vec4f       Color{1.0f, 1.0f, 1.0f, 1.0f};
        float                    FontSize{16.0f};
        UIRect                   Rect;
        UILayer                  Layer{kUILayerDefault};
    };

    // ── UIButton ─────────────────────────────────────────────────────────────
    struct UIButton {
        Core::Containers::String     Label;
        UIRect                       Rect;
        Core::Maths::Vec4f           NormalColor{0.2f, 0.2f, 0.2f, 1.0f};
        Core::Maths::Vec4f           HoverColor {0.35f, 0.35f, 0.35f, 1.0f};
        Core::Maths::Vec4f           PressColor {0.1f, 0.1f, 0.1f, 1.0f};
        Core::Maths::Vec4f           TextColor  {1.0f, 1.0f, 1.0f, 1.0f};
        float                        FontSize{16.0f};
        UILayer                      Layer{kUILayerDefault};
        // State — written by UIContext::ProcessInput, read by UIContext::Button()
        bool                         Hovered{false};
        bool                         Pressed{false};
    };

    // ── UIImage ──────────────────────────────────────────────────────────────
    struct UIImage {
        uint32_t           TextureHandle{0};  // RenderResourceManager handle
        UIRect             Rect;
        Core::Maths::Vec4f Tint{1.0f, 1.0f, 1.0f, 1.0f};
        UILayer            Layer{kUILayerDefault};
    };

    // ── UIProgressBar ────────────────────────────────────────────────────────
    struct UIProgressBar {
        float              Value{0.0f};
        float              Max{1.0f};
        Core::Maths::Vec4f FillColor{0.2f, 0.8f, 0.2f, 1.0f};
        Core::Maths::Vec4f BackColor{0.15f, 0.15f, 0.15f, 1.0f};
        UIRect             Rect;
        UILayer            Layer{kUILayerDefault};

        [[nodiscard]] float Fraction() const noexcept {
            if (Max <= 0.0f) { return 0.0f; }
            return std::clamp(Value / Max, 0.0f, 1.0f);
        }
    };

    // Forward-declare UIWidget for use in UIPanel::Children
    struct UIWidget;

    // ── UIPanel ──────────────────────────────────────────────────────────────
    struct UIPanel {
        UIRect                               Rect;
        Core::Maths::Vec4f                   BackgroundColor{0.1f, 0.1f, 0.1f, 0.85f};
        UILayer                              Layer{kUILayerDefault};
        Core::Containers::Array<UIWidget*>   Children;   // arena-allocated, non-owning
    };

    // ── UIWidget — type-erased handle ────────────────────────────────────────
    enum class UIWidgetType : uint8_t {
        Label,
        Button,
        Image,
        ProgressBar,
        Panel,
    };

    // Tagged union over all widget types. Fits in a single arena allocation.
    // No virtual dispatch. Type dispatch is explicit via UIWidgetType tag.
    struct UIWidget {
        UIWidgetType Type;
        union {
            UILabel       Label;
            UIButton      Button;
            UIImage       Image;
            UIProgressBar ProgressBar;
            UIPanel       Panel;
        };

        explicit UIWidget(UILabel&& l)       : Type(UIWidgetType::Label),       Label(std::move(l))       {}
        explicit UIWidget(UIButton&& b)      : Type(UIWidgetType::Button),      Button(std::move(b))      {}
        explicit UIWidget(UIImage&& i)       : Type(UIWidgetType::Image),       Image(std::move(i))       {}
        explicit UIWidget(UIProgressBar&& p) : Type(UIWidgetType::ProgressBar), ProgressBar(std::move(p)) {}
        explicit UIWidget(UIPanel&& p)       : Type(UIWidgetType::Panel),       Panel(std::move(p))       {}

        // Non-copyable: widgets live in the arena; copy would silently duplicate arena pointers.
        UIWidget(const UIWidget&)            = delete;
        UIWidget& operator=(const UIWidget&) = delete;
        UIWidget(UIWidget&&)                 = default;
        UIWidget& operator=(UIWidget&&)      = default;

        ~UIWidget() {
            // Explicit destructor needed because of union with non-trivial members
            switch (Type) {
                case UIWidgetType::Label:       Label.~UILabel();             break;
                case UIWidgetType::Button:      Button.~UIButton();           break;
                case UIWidgetType::Image:       Image.~UIImage();             break;
                case UIWidgetType::ProgressBar: ProgressBar.~UIProgressBar(); break;
                case UIWidgetType::Panel:       Panel.~UIPanel();             break;
            }
        }
    };

} // namespace ZEngine::UI
```

---

## 5. `UIContext` — Top-Level State Machine

`UIContext` is the sole public API surface for game code. It owns a per-frame scratch arena
and exposes an immediate-mode API. It maintains a flat widget tree (arena-allocated `Array<UIWidget*>`)
and a panel stack for nested layouts.

```cpp
// ZEngine/UI/UIContext.h
#pragma once
#include <UI/UITypes.h>
#include <UI/UIWidgets.h>
#include <UI/UIDrawList.h>
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/String.h>
#include <Core/Maths/Vec4f.h>
#include <cstdint>

namespace ZEngine::UI {

    struct UIInputState {
        float MouseX{0.0f};
        float MouseY{0.0f};
        bool  LeftButtonDown{false};
        bool  LeftButtonJustPressed{false};   // edge-detect: down this frame, up last frame
        bool  ConsumedByUI{false};            // set true when UI captures input
    };

    class UIContext {
    public:
        // arena_size_bytes: size of the per-frame scratch arena.
        // 2 MiB is sufficient for a typical HUD + main menu with font glyphs.
        explicit UIContext(size_t arena_size_bytes = 2 * 1024 * 1024);
        ~UIContext();

        // Deleted copy/move — UIContext is a singleton per-frame state machine.
        UIContext(const UIContext&)            = delete;
        UIContext& operator=(const UIContext&) = delete;

        // ── Frame lifecycle ──────────────────────────────────────────────────

        // Begin a new UI frame. Resets the scratch arena; invalidates all
        // previously returned UIRect/widget pointers.
        void Begin(float screen_width, float screen_height);

        // Finalize the widget tree, resolve hit-testing, produce m_DrawList.
        // Call after all widget-submission calls. After End(), the draw list
        // is valid until the next Begin().
        void End();

        // ── Input ────────────────────────────────────────────────────────────

        // Must be called exactly once per frame, before any widget submission.
        // Calling multiple times per frame causes input state to flip unpredictably.
        // Called after OS event poll, before UIContext::Begin() widget submissions.
        void ProcessInput(float mouse_x, float mouse_y,
                          bool left_button_down,
                          bool left_button_just_pressed);

        [[nodiscard]] const UIInputState& GetInputState() const noexcept { return m_Input; }

        // Explicitly mark UI input as consumed for this frame.
        // Use when a screen transition or animation should block game input
        // without needing a visible widget to capture the click.
        void SetInputConsumed(bool consumed) noexcept { m_InputState.ConsumedByUI = consumed; }

        // Returns true if any UI element consumed mouse input this frame.
        [[nodiscard]] bool IsInputConsumed() const noexcept { return m_InputState.ConsumedByUI; }

        // Returns true if the given rect is currently hovered by the mouse.
        // Does not check whether input was already consumed; call after ProcessInput.
        [[nodiscard]] bool IsHovered(const UIRect& rect) const noexcept;

        // ── Widget submission ────────────────────────────────────────────────

        // Render a text label.
        void Label(const UIRect& rect, const char* text,
                   Core::Maths::Vec4f color  = {1.0f, 1.0f, 1.0f, 1.0f},
                   float font_size           = 16.0f,
                   UILayer layer             = kUILayerDefault);

        // Render a clickable button. Returns true on click (left-button released
        // while hovered this frame).
        [[nodiscard]] bool Button(const UIRect& rect, const char* label,
                    UILayer layer = kUILayerDefault);

        // Render a textured image quad.
        void Image(const UIRect& rect, uint32_t texture_handle,
                   Core::Maths::Vec4f tint = {1.0f, 1.0f, 1.0f, 1.0f},
                   UILayer layer           = kUILayerDefault);

        // Render a horizontal progress bar.
        void ProgressBar(const UIRect& rect, float value, float max,
                         Core::Maths::Vec4f fill_color = {0.2f, 0.8f, 0.2f, 1.0f},
                         Core::Maths::Vec4f back_color = {0.15f, 0.15f, 0.15f, 1.0f},
                         UILayer layer                 = kUILayerDefault);

        // Begin/End a panel (clipping rect + background + nested children).
        // Widgets submitted between BeginPanel/EndPanel are children of the panel.
        void BeginPanel(const UIRect& rect,
                        Core::Maths::Vec4f background = {0.1f, 0.1f, 0.1f, 0.85f},
                        UILayer layer                 = kUILayerDefault);
        // Implementation must assert nesting depth after push:
        //   ZENGINE_VALIDATE_ASSERT(m_PanelStack.Size() <= 16,
        //       "UIContext: panel nesting depth exceeded 16. Use fewer nested panels.");
        void EndPanel();

        // ── Output ───────────────────────────────────────────────────────────

        // Draw list produced by End(). Valid until next Begin().
        [[nodiscard]] const UIDrawList& GetDrawList() const noexcept { return m_DrawList; }

        [[nodiscard]] float ScreenWidth()  const noexcept { return m_ScreenWidth; }
        [[nodiscard]] float ScreenHeight() const noexcept { return m_ScreenHeight; }

    private:
        Core::Memory::ArenaAllocator m_Arena;
        UIDrawList                   m_DrawList;
        UIInputState                 m_Input;
        float                        m_ScreenWidth{0.0f};
        float                        m_ScreenHeight{0.0f};

        // Root widget list and panel stack (arena-allocated)
        Core::Containers::Array<UIWidget*> m_Widgets;
        Core::Containers::Array<UIPanel*>  m_PanelStack;  // depth-limited to 16

        bool m_InFrame{false};

        // Internal: allocate a widget from the arena
        template<typename T>
        UIWidget* AllocWidget(T&& data);

        // Internal: resolve hover/press state for all UIButton widgets
        void ResolveInputState();

        // Internal: flatten widget tree to draw commands
        void BuildDrawList();
    };

} // namespace ZEngine::UI
```

### 5.1 `UIDrawList` — GPU-ready command buffer

```cpp
// ZEngine/UI/UIDrawList.h
#pragma once
#include <UI/UITypes.h>
#include <Core/Containers/Array.h>
#include <Core/Maths/Vec2f.h>
#include <Core/Maths/Vec4f.h>
#include <cstdint>

namespace ZEngine::UI {

    // A single UI vertex on the GPU.
    // Matches the UIPass vertex shader input layout.
    struct UIVertex {
        Core::Maths::Vec2f Position;   // screen-space pixels
        Core::Maths::Vec2f TexCoord;   // [0,1] UV; (0,0) if untextured
        Core::Maths::Vec4f Color;
        uint32_t           TextureIndex;  // 0 = solid color; >0 = texture array index
    };

    // A contiguous range of UIVertex that shares one texture/pipeline state.
    struct UIDrawCmd {
        uint32_t VertexOffset{0};
        uint32_t VertexCount{0};
        uint32_t TextureHandle{0};  // 0 = white 1x1 fallback (solid color)
        UILayer  Layer{kUILayerDefault};
        bool     IsTextGlyph{false};  // true → sample from font atlas
    };

    // The full draw list for one frame.
    // Produced by UIContext::End(), consumed by UIRenderer.
    struct UIDrawList {
        Core::Containers::Array<UIVertex>  Vertices;
        Core::Containers::Array<UIDrawCmd> Commands;

        void Clear() {
            Vertices.Clear();
            Commands.Clear();
        }

        // Append a solid-color quad (two triangles = 6 vertices).
        void PushColoredQuad(const UIRect& rect, Core::Maths::Vec4f color, UILayer layer);

        // Append a textured quad.
        void PushTexturedQuad(const UIRect& rect, Core::Maths::Vec4f tint,
                              uint32_t texture_handle, UILayer layer);

        // Append a text glyph quad (from font atlas).
        void PushGlyphQuad(const UIRect& rect, Core::Maths::Vec2f uv_min,
                           Core::Maths::Vec2f uv_max, Core::Maths::Vec4f color, UILayer layer);

        // Sort commands by layer then by texture to minimise state changes.
        void SortCommands();
    };

} // namespace ZEngine::UI
```

---

## 6. Layout Utilities

`UILayout` is a stateless namespace of helper functions. No allocation. No state.

```cpp
// ZEngine/UI/UILayout.h
#pragma once
#include <UI/UITypes.h>
#include <Core/Containers/Array.h>
#include <Core/Maths/Vec2f.h>

namespace ZEngine::UI::UILayout {

    enum class StackDirection : uint8_t { Vertical, Horizontal };

    // Divide `container` into N equally-sized rects, separated by `spacing`,
    // with `padding` applied inside each edge of the container.
    // out_rects must have capacity >= count.
    void Stack(const UIRect& container,
               int count,
               float padding,
               float spacing,
               StackDirection direction,
               Core::Containers::Array<UIRect>& out_rects);

    // Return a rect of `size` positioned inside `parent` according to `anchor`,
    // with an additional `offset` applied after anchoring.
    [[nodiscard]] UIRect Anchor(const UIRect& parent,
                                UIAnchor anchor,
                                Core::Maths::Vec2f size,
                                Core::Maths::Vec2f offset = {0.0f, 0.0f});

    // Shrink `rect` by `amount` on all four sides (positive = inset).
    [[nodiscard]] UIRect Inset(const UIRect& rect, float amount);

    // Return the intersection of two rects. Width/Height will be 0 if no overlap.
    [[nodiscard]] UIRect Intersect(const UIRect& a, const UIRect& b);

    // Split `rect` horizontally at `x_fraction` (0..1). Left goes to `out_left`, right to `out_right`.
    void SplitHorizontal(const UIRect& rect, float x_fraction,
                         UIRect& out_left, UIRect& out_right);

    // Split `rect` vertically at `y_fraction` (0..1).
    void SplitVertical(const UIRect& rect, float y_fraction,
                       UIRect& out_top, UIRect& out_bottom);

} // namespace ZEngine::UI::UILayout
```

### 6.1 Stack example

```cpp
// Lay out 3 buttons vertically inside a panel:
Core::Containers::Array<UIRect> btn_rects;
btn_rects.Resize(3);
UILayout::Stack(panel_rect, 3, /*padding=*/8.0f, /*spacing=*/4.0f,
                UILayout::StackDirection::Vertical, btn_rects);

ctx.Button(btn_rects[0], "Resume");
ctx.Button(btn_rects[1], "Settings");
ctx.Button(btn_rects[2], "Quit");
```

### 6.2 Anchor example

```cpp
// Health bar: anchored bottom-left, 200x20 px, offset 10px from edge
UIRect hbar = UILayout::Anchor(screen_rect, UIAnchor::BottomLeft, {200.0f, 20.0f}, {10.0f, -30.0f});
ctx.ProgressBar(hbar, player_health, player_max_health);
```

---

## 7. Input Routing

Platform input arrives as raw mouse coordinates and button state. The UI system must
consume mouse events that land on active UI widgets so game code does not accidentally
react to "clicks through" UI panels.

### 7.1 Processing order

```
Platform::PollEvents()
  └─► UIContext::ProcessInput(mouse_x, mouse_y, left_down, left_just_pressed)
        sets UIInputState::ConsumedByUI = true if any widget hovered/active
  └─► if (!ctx.GetInputState().ConsumedByUI)
          GameInputSystem::ProcessMouse(...)
```

This is enforced by the ordering in `Engine::MainThreadRun`. Game systems never read
raw mouse state if `ConsumedByUI` is true.

### 7.2 `ProcessInput` contract

```cpp
void UIContext::ProcessInput(float mouse_x, float mouse_y,
                             bool left_button_down,
                             bool left_button_just_pressed) {
    m_Input.MouseX              = mouse_x;
    m_Input.MouseY              = mouse_y;
    m_Input.LeftButtonDown      = left_button_down;
    m_Input.LeftButtonJustPressed = left_button_just_pressed;
    m_Input.ConsumedByUI        = false;

    // Walk all active widget rects and check containment.
    // If any panel or button is hovered, mark input as consumed.
    for (UIWidget* w : m_Widgets) {
        if (!w) { continue; }
        switch (w->Type) {
            case UIWidgetType::Button:
                if (w->Button.Rect.Contains(mouse_x, mouse_y)) {
                    w->Button.Hovered = true;
                    w->Button.Pressed = left_button_down;
                    m_Input.ConsumedByUI = true;
                }
                break;
            case UIWidgetType::Panel:
                if (w->Panel.Rect.Contains(mouse_x, mouse_y)) {
                    m_Input.ConsumedByUI = true;
                }
                break;
            default:
                break;
        }
    }
}
```

Click-through prevention is per-layer: the front-most widget that contains the cursor
consumes input. Panels without `OnClick` still block input so the scene behind them
does not receive clicks.

---

## 8. Rendering — `UIRenderer` and the UI Render Pass

### 8.1 `UIRenderer`

`UIRenderer` owns the GPU-side resources for the UI pass (vertex buffer, pipeline,
descriptor sets for textures and the font atlas). It is initialised once at engine startup
and re-uses the same vertex buffer (mapped persistently with VMA) across frames.

```cpp
// ZEngine/UI/UIRenderer.h
#pragma once
#include <UI/UIDrawList.h>
#include <Rendering/RenderGraph.h>
#include <Core/Memory/ArenaAllocator.h>
#include <Helpers/IntrusivePtr.h>

namespace ZEngine::UI {

    class UIRenderer : public Helpers::RefCounted {
    public:
        // Initialize GPU resources (pipeline, descriptor sets, persistent vertex buffer).
        // Must be called after the Vulkan device is available.
        void Init(Rendering::RenderGraph& graph,
                  uint32_t max_vertices = 65536);

        void Shutdown();

        // Upload UIDrawList to GPU vertex buffer and record the UI render pass.
        // Called from the render thread each frame.
        void SubmitDrawList(const UIDrawList& draw_list,
                            float screen_width,
                            float screen_height);

        // Returns the IRenderGraphCallbackPass handle for registration with RenderGraph.
        [[nodiscard]] Rendering::IRenderGraphCallbackPass* GetRenderPass() const noexcept;

    private:
        // Vulkan/VMA handles — opaque here
        struct Impl;
        Impl* m_Impl{nullptr};   // arena-allocated in Init()
    };

} // namespace ZEngine::UI
```

### 8.2 UI RenderGraph pass specification

The UI pass is registered as an `IRenderGraphCallbackPass` with the following properties:

| Property | Value |
|---|---|
| Input attachments | Final scene colour (read-only) |
| Output attachments | Swapchain colour (write) |
| Depth attachment | None (no depth read or write) |
| Pipeline | Orthographic projection, alpha blending `(src_alpha, one_minus_src_alpha)`, no depth test, no face culling |
| Vertex format | `UIVertex` (position 2×f32, texcoord 2×f32, color 4×f32, texture_index 1×u32) |
| Descriptor set 0 | Orthographic projection matrix (push constant, 64 bytes) |
| Descriptor set 1 | Texture array (sampled; slot 0 = white 1×1, slot 1 = font atlas, slots 2+ = game textures) |
| Vertex buffer | Persistently mapped host-visible VMA buffer; uploaded from UIDrawList each frame |

```cpp
// UIPass callback — wired up in UIRenderer::Init()
struct UIPassCallback : public Rendering::IRenderGraphCallbackPass {

    UIRenderer* Renderer{nullptr};
    const UIDrawList* DrawList{nullptr};
    float ScreenWidth{0.0f};
    float ScreenHeight{0.0f};

    void Setup(Rendering::RenderGraphBuilder& builder) override {
        builder.SetColorOutput("UIOutput", Rendering::AttachmentLoadOp::Load);
        builder.SetDepthOutput("", Rendering::AttachmentLoadOp::DontCare);
    }

    void Execute(Rendering::RenderGraphContext& ctx,
                 VkCommandBuffer cmd) override {
        ZENGINE_VALIDATE_ASSERT(DrawList != nullptr,
            "UIPassCallback: DrawList must be set before Execute");

        // Upload vertices
        Renderer->UploadVertices(*DrawList, cmd);

        // Bind pipeline and descriptor sets
        Renderer->BindUIPass(ctx, cmd, ScreenWidth, ScreenHeight);

        // Issue draw calls sorted by layer/texture (DrawList is pre-sorted)
        for (const UIDrawCmd& dc : DrawList->Commands) {
            if (dc.VertexCount == 0) { continue; }
            vkCmdDraw(cmd, dc.VertexCount, 1, dc.VertexOffset, 0);
        }
    }
};
```

### 8.3 Orthographic projection

The projection matrix maps `[0, screen_width] × [0, screen_height]` to NDC `[-1, 1]`.
It is pushed as a 64-byte push constant:

```
P = ortho(0, W, H, 0, -1, 1)
```

Top row: `[ 2/W,    0,  0, -1 ]`
Row 2:   `[   0, -2/H,  0,  1 ]`   ← Y flipped: top-left = (0,0) in screen space
Row 3:   `[   0,    0, -1,  0 ]`
Row 4:   `[   0,    0,  0,  1 ]`

---

## 9. HUD Integration

Game code calls `UIContext` methods from a dedicated `HUDSystem` (an ECS system registered
with `SystemScheduler`) or from an `Actor::OnTick` override. Either is valid; the
`HUDSystem` approach is preferred because it separates UI logic from gameplay logic.

### 9.1 HUDSystem example

```cpp
// ZEngine/Game/HUDSystem.cpp
#include <UI/UIContext.h>
#include <UI/UILayout.h>
#include <ECS/Components/HealthComponent.h>
#include <ECS/Components/AmmoComponent.h>

void HUDSystem::OnTick(ECS::Scene& scene, UI::UIContext& ctx, const Core::TimeStep& ts) {
    const float W = ctx.ScreenWidth();
    const float H = ctx.ScreenHeight();
    const UIRect screen{0, 0, W, H};

    // ── Health bar ───────────────────────────────────────────────────────────
    UIRect health_rect = UILayout::Anchor(screen, UIAnchor::BottomLeft,
                                          {200.0f, 22.0f}, {12.0f, -40.0f});
    auto& health = scene.GetComponent<HealthComponent>(m_PlayerEntity);
    ctx.ProgressBar(health_rect, health.Current, health.Max,
                    {0.8f, 0.15f, 0.15f, 1.0f},   // fill: red
                    {0.2f, 0.2f,  0.2f,  0.9f});   // back: dark grey

    // ── Ammo counter ─────────────────────────────────────────────────────────
    UIRect ammo_rect = UILayout::Anchor(screen, UIAnchor::BottomRight,
                                        {100.0f, 22.0f}, {-12.0f, -40.0f});
    auto& ammo = scene.GetComponent<AmmoComponent>(m_PlayerEntity);
    char ammo_buf[32];
    snprintf(ammo_buf, sizeof(ammo_buf), "%d / %d", ammo.Current, ammo.Max);
    ctx.Label(ammo_rect, ammo_buf, {1.0f, 1.0f, 1.0f, 1.0f}, 18.0f);

    // ── Crosshair ────────────────────────────────────────────────────────────
    const float CH_HALF = 8.0f;
    UIRect ch_h{W * 0.5f - CH_HALF, H * 0.5f - 1.0f, CH_HALF * 2.0f, 2.0f};
    UIRect ch_v{W * 0.5f - 1.0f, H * 0.5f - CH_HALF, 2.0f, CH_HALF * 2.0f};
    ctx.Image(ch_h, m_WhitePixelHandle, {1.0f, 1.0f, 1.0f, 0.8f});
    ctx.Image(ch_v, m_WhitePixelHandle, {1.0f, 1.0f, 1.0f, 0.8f});
}
```

`HUDSystem::OnTick` is called inside the **render preparation** phase, not the fixed-step
simulation loop. It runs once per rendered frame, after `UIContext::Begin()` and before
`UIContext::End()`.

---

## 10. Screen Stack and Menus

### 10.1 `UIScreen` base

```cpp
// ZEngine/UI/UIScreen.h
#pragma once
#include <UI/UIContext.h>
#include <Core/TimeStep.h>

namespace ZEngine::UI {

    class UIScreen {
    public:
        virtual ~UIScreen() = default;

        // Called every rendered frame while this screen is on top of the stack.
        virtual void Draw(UIContext& ctx, const Core::TimeStep& ts) = 0;

        // Optional: called when the screen is pushed onto the stack.
        virtual void OnEnter() {}

        // Optional: called when the screen is popped.
        virtual void OnExit() {}

        // If returns false, the screen below in the stack is also drawn
        // (useful for transparent pause overlays).
        [[nodiscard]] virtual bool IsOpaque() const { return true; }
    };

} // namespace ZEngine::UI
```

### 10.2 `UIScreenStack`

```cpp
// ZEngine/UI/UIScreenStack.h
#pragma once
#include <UI/UIScreen.h>
#include <Core/Containers/Array.h>
#include <Helpers/IntrusivePtr.h>
#include <cstddef>

namespace ZEngine::UI {

    // Owns UIScreen lifetime via Helpers::Ref<>.
    // Max stack depth is 16 — enforced with ZENGINE_VALIDATE_ASSERT.
    class UIScreenStack {
    public:
        static constexpr int kMaxDepth = 16;

        // Push a new screen on top. Calls OnEnter().
        void Push(Helpers::Ref<UIScreen> screen);

        // Pop and destroy the top screen. Calls OnExit(). No-op on empty stack.
        void Pop();

        // Peek the current top screen. Returns nullptr if empty.
        [[nodiscard]] UIScreen* Peek() const noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept { return m_Stack.IsEmpty(); }
        [[nodiscard]] int  Depth()   const noexcept { return static_cast<int>(m_Stack.Size()); }

        // Draw all visible screens (respects UIScreen::IsOpaque).
        void Draw(UIContext& ctx, const Core::TimeStep& ts);

    private:
        Core::Containers::Array<Helpers::Ref<UIScreen>> m_Stack;
    };

} // namespace ZEngine::UI
```

### 10.3 Pause menu example

```cpp
// ZEngine/Game/PauseMenuScreen.h
#include <UI/UIScreen.h>
#include <UI/UILayout.h>

class PauseMenuScreen : public ZEngine::UI::UIScreen {
public:
    bool IsOpaque() const override { return false; }  // show blurred scene behind

    void Draw(ZEngine::UI::UIContext& ctx, const ZEngine::Core::TimeStep&) override {
        using namespace ZEngine::UI;
        const float W = ctx.ScreenWidth();
        const float H = ctx.ScreenHeight();

        // Semi-transparent overlay
        UIRect overlay{0.0f, 0.0f, W, H};
        ctx.BeginPanel(overlay, {0.0f, 0.0f, 0.0f, 0.5f}, kUILayerOverlay - 1);

        // Centred button panel
        UIRect panel = UILayout::Anchor({0, 0, W, H}, UIAnchor::MiddleCenter,
                                         {240.0f, 160.0f});
        ctx.BeginPanel(panel, {0.12f, 0.12f, 0.12f, 0.95f}, kUILayerOverlay);

        Core::Containers::Array<UIRect> btns;
        btns.Resize(3);
        UILayout::Stack(UILayout::Inset(panel, 12.0f), 3, 4.0f, 8.0f,
                        UILayout::StackDirection::Vertical, btns);

        if (ctx.Button(btns[0], "Resume", kUILayerOverlay))    { m_OnResume(); }
        if (ctx.Button(btns[1], "Settings", kUILayerOverlay))  { m_OnSettings(); }
        if (ctx.Button(btns[2], "Quit to Menu", kUILayerOverlay)) { m_OnQuit(); }

        ctx.EndPanel();
        ctx.EndPanel();
    }

    // Plain function pointer + context — no std::function, no heap allocation.
    // Pass a pointer to a game-side struct as Ctx; lifetime managed by caller.
    using MenuActionFn = void (*)(void* ctx);

    MenuActionFn m_OnResume   = nullptr;
    MenuActionFn m_OnSettings = nullptr;
    MenuActionFn m_OnQuit     = nullptr;
    void*        m_ActionCtx  = nullptr;  // shared context for all three callbacks
};
```

The screen stack is updated in `Engine::MainThreadRun` alongside `HUDSystem::OnTick`.
Both happen between `UIContext::Begin()` and `UIContext::End()`:

```cpp
// In MainThreadRun, after fixed-step loop:
m_UIContext->Begin(screen_w, screen_h);
m_UIContext->ProcessInput(mouse_x, mouse_y, left_down, left_just);
m_ScreenStack->Draw(*m_UIContext, timestep);    // menus (screen stack)
m_HUDSystem->OnTick(*m_Scene, *m_UIContext, timestep);  // HUD (always drawn)
m_UIContext->End();
m_UIRenderer->SubmitDrawList(m_UIContext->GetDrawList(), screen_w, screen_h);
```

---

## 11. Arena Allocation

All per-frame UI state — widget tree nodes, draw commands, vertex arrays, string copies —
is allocated from `UIContext::m_Arena`, a `Core::Memory::ArenaAllocator` with a fixed
backing store set at construction time (default 2 MiB).

```
UIContext::Begin()
  │
  └─► m_Arena.Reset()           ← wipes all per-frame allocations in O(1)
      m_Widgets.Clear()          ← Array still points to arena memory
      m_PanelStack.Clear()
      m_DrawList.Clear()

UIContext::Label / Button / ... 
  └─► AllocWidget<UIButton>(...)
        ← placement-new into arena memory
        ← no malloc, no free

UIContext::End()
  └─► ResolveInputState()        ← iterates m_Widgets (arena memory)
      BuildDrawList()            ← populates m_DrawList (arena memory)
```

Widget strings (button labels, label text) are shallow-copied into the arena via
`Core::Containers::String` allocating from the arena allocator. Strings do not persist
past `Begin()`.

**Arena exhaustion policy:**

In debug builds: `ZENGINE_VALIDATE_ASSERT` fires immediately so the developer can
increase `arena_size_bytes`.

In release builds: arena exhaustion must NOT crash. The fallback strategy is:
1. Log `ZENGINE_CORE_ERROR("UIContext: frame arena exhausted (%zu bytes used of %zu)", ...)`
2. Skip submitting any widget that requires an allocation — the frame renders with
   partial UI rather than crashing.
3. On the NEXT frame, `Begin()` resets the arena and the UI renders normally.

To prevent exhaustion: enable `ZENGINE_UI_ARENA_STATS` in dev builds to log peak
arena usage per frame. Set `arena_size_bytes` to 2× the peak before shipping.

**There are no calls to `new`, `delete`, `malloc`, or `free` anywhere in the UI system.**
All callbacks use plain function pointers (`void (*)(void* ctx)`) with a non-owning
`void* Ctx` for user data. This eliminates `std::function`'s heap allocation for captures
larger than the SSO buffer and removes the indirect virtual call it imposes.

For screens that need to call back into game systems, pass a pointer to a game-side
context struct as `Ctx`:
```cpp
struct MenuCtx { GameStateManager* State; AudioEngine* Audio; };
// arena-allocated in UIScreenStack or GameApplication
MenuCtx* ctx = ZPushStructCtor(arena, MenuCtx);
ctx->State   = &game_state;
ctx->Audio   = &audio;

screen->m_OnResume = [](void* c) { ((MenuCtx*)c)->State->ResumeGame(); };
screen->m_ActionCtx = ctx;
```

---

## 12. File Layout

All new files live under `ZEngine/UI/`:

```
ZEngine/UI/
  UITypes.h            — UIRect, UIAnchor, UILayer, UIVertex
  UIWidgets.h          — UILabel, UIButton, UIImage, UIProgressBar, UIPanel, UIWidget
  UIDrawList.h/.cpp    — UIDrawCmd, UIDrawList (PushColoredQuad, PushTexturedQuad, etc.)
  UIContext.h/.cpp     — UIContext (Begin/End/Label/Button/Image/ProgressBar/Panel)
  UILayout.h/.cpp      — UILayout::Stack, Anchor, Inset, Intersect, Split*
  UIRenderer.h/.cpp    — UIRenderer, UIPassCallback (IRenderGraphCallbackPass)
  UIScreen.h           — UIScreen (abstract base)
  UIScreenStack.h/.cpp — UIScreenStack (Push/Pop/Peek/Draw)
```

No files are added to `ZEngine/Core/` or `ZEngine/Rendering/`. The UI pass is
self-contained and wires into `RenderGraph` via the existing `IRenderGraphCallbackPass`
interface defined in `render-resource-manager.md`.

---

## 13. Deliverables Checklist

| # | Item | File | Status |
|---|------|------|--------|
| 1 | `UIRect`, `UIAnchor`, `UILayer` | `ZEngine/UI/UITypes.h` | Todo |
| 2 | `UIVertex`, `UIDrawCmd`, `UIDrawList` | `ZEngine/UI/UIDrawList.h/.cpp` | Todo |
| 3 | `UILabel`, `UIButton`, `UIImage`, `UIProgressBar`, `UIPanel`, `UIWidget` | `ZEngine/UI/UIWidgets.h` | Todo |
| 4 | `UIContext` (Begin/End/Label/Button/Image/ProgressBar/BeginPanel/EndPanel/ProcessInput) | `ZEngine/UI/UIContext.h/.cpp` | Todo |
| 5 | `UILayout::Stack`, `Anchor`, `Inset`, `Intersect`, `Split*` | `ZEngine/UI/UILayout.h/.cpp` | Todo |
| 6 | `UIRenderer` + `UIPassCallback` (IRenderGraphCallbackPass) | `ZEngine/UI/UIRenderer.h/.cpp` | Todo |
| 7 | `UIScreen` abstract base | `ZEngine/UI/UIScreen.h` | Todo |
| 8 | `UIScreenStack` | `ZEngine/UI/UIScreenStack.h/.cpp` | Todo |
| 9 | `HUDSystem` (health bar, ammo, crosshair) | `ZEngine/Game/HUDSystem.h/.cpp` | Todo |
| 10 | `PauseMenuScreen` example | `ZEngine/Game/PauseMenuScreen.h` | Todo |
| 11 | `UIContext::m_Arena` backed by `Core::Memory::ArenaAllocator` | `ZEngine/UI/UIContext.cpp` | Todo |
| 12 | Register `UIPassCallback` with `RenderGraph` in `Engine::Init()` | `ZEngine/Engine/Engine.cpp` | Todo (modify existing) |
| 13 | Wire `UIContext::ProcessInput` before `GameInputSystem` in `MainThreadRun` | `ZEngine/Engine/Engine.cpp` | Todo (modify existing) |

All new types must:
- Allocate nothing from the global heap.
- Use `ZENGINE_VALIDATE_ASSERT` for precondition checks.
- Have no constructors that throw.
- Use `ZENGINE_CORE_WARN` / `ZENGINE_CORE_INFO` for diagnostics (never `printf`/`std::cout`).
