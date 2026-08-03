# ZEngine — UI System (RAD Debugger-Inspired)

**Priority:** P2 — Required for DebugOverlay, DebugConsole, in-game HUD, menus, and eventual editor migration  
**Status:** Design  
**Depends on:** `ArenaAllocator` (done), `Array<T>` (done), `UnorderedHashMap` (done), `VulkanDevice` (done), `RenderGraph` (done), stb (vendored), rapidhash (vendored)  
**Blocks:** `profiling.md` (DebugOverlay, DebugConsole), in-game HUD, main menu, settings screen

---

## 1. Architecture Overview

Every widget — button, label, panel, scroll area, slider — is the **same struct: `Box`**.
No widget class hierarchy. No virtual dispatch. No manual rect arithmetic.
The system is a direct adaptation of the RAD Debugger UI architecture (Ryan Fleury / Epic Games)
translated onto ZEngine's existing primitives (`ArenaAllocator`, `Array<T>`, `UnorderedHashMap`,
rapidhash, stb_truetype).

**Why this over the previous typed-union approach:**

| Concern | Typed-union (previous) | Box (this doc) |
|---|---|---|
| Layout | Manual `UIRect` per call site | Constraint solver (5 `SizeKind`s) |
| Widget identity | None — stateless | Key hash — persistent across frames |
| Hover/press animation | None | Implicit via `HotTransition` / `ActiveTransition` |
| Adding a new widget type | New struct + union slot + renderer branch | Compose existing `BoxFlags` |
| Style management | Args per call | Push/pop stacks on `UIContext` |
| Scroll areas | Manual offset math | `BoxFlag_Scrollable` + scroll state in anim map |

**Critical path — must be implemented in order:**

```
Step 1 → StringHash           ~0.5 days   box identity
Step 2 → InputFrame           ~1.5 days   per-frame input cache (replaces window-arg queries)
Step 3 → UIInput              ~1 day      hit-test + hot/active routing into UIContext
Step 4 → BitmapFontAtlas      ~6 days     THE blocker — no text = no widgets
Step 5 → Box + UIContext      ~5 days     core API, style stacks, persistent anim map
Step 6 → Layout pass          ~4 days     5-phase constraint solver
Step 7 → UIRenderer           ~4 days     RenderGraph node, quad batcher, ui.vert/frag
Step 8 → Widgets layer        ~3 days     named helpers (Button, Slider, Panel, etc.)
──────────────────────────────────────────
Total                         ~4–5 weeks
```

ImGui and ImGuizmo stay alive throughout. This system is additive. `SceneViewportUIComponent`
keeps ImGui + ImGuizmo permanently (gizmos depend on ImGui draw lists). Other Tetragrama panels
migrate one-by-one after the Widgets layer is stable. First milestone: DebugOverlay and
DebugConsole running via UIContext.

---

## 2. Coordinate System

All UI coordinates are **screen-space pixels**, top-left origin, Y increasing downward.
This matches GLFW cursor coordinates directly; no conversion needed in `InputFrame`.

```
(0,0) ──────────────────► X
  │
  │   pos = {10, 10},  size = {200, 40}
  │
  ▼ Y
```

The `UIRenderer` orthographic projection maps `[0, W] × [0, H]` to NDC:

```
P = ortho(0, W, H, 0, -1, 1)
```

Pushed as a 64-byte push constant each frame.

---

## 3. Step 1 — StringHash

### Why

`Box` is identified across frames by a `uint64_t` key hashed from its tag string. Without
a stable hash, hover/active state and animation cannot persist from frame N to frame N+1.

### Files

```
ZEngine/ZEngine/Core/Containers/StringHash.h    (header-only)
```

### Implementation

rapidhash is already vendored at `__externals/rapidhash/src/rapidhash.h`.

```cpp
// ZEngine/ZEngine/Core/Containers/StringHash.h
#pragma once
#include <cstdint>
#include <cstring>
#include <rapidhash/src/rapidhash.h>

namespace ZEngine::Core::Containers
{
    // 64-bit hash of a null-terminated string. Deterministic within a process.
    inline uint64_t StringHash(const char* str) noexcept
    {
        if (!str) return 0;
        return rapidhash(str, strlen(str));
    }

    // Hash a string + integer suffix — unique keys for list items.
    // StringHashN("item", 3) produces a stable key for the 3rd "item".
    inline uint64_t StringHashN(const char* str, uint32_t index) noexcept
    {
        return StringHash(str) ^ (uint64_t(index) * 0x9E3779B97F4A7C15ULL);
    }
}
```

### Deliverable

- [ ] `Core/Containers/StringHash.h` — `StringHash(const char*)` + `StringHashN`

---

## 4. Step 2 — InputFrame

### Why

The current input layer (`Keyboard`, `Mouse`) requires `CoreWindow*` at every query site and
has no per-frame delta accumulation, no edge-detect (just-pressed / just-released), and no
retained scroll state. `UIContext` needs all of these as zero-argument queries.

`InputFrame` implements `IMouseEventCallback`, `IKeyboardEventCallback`, and
`ITextInputEventCallback` and self-registers alongside the window's existing listeners. It is
the single source of per-frame input truth for the UI system and for game input consumers.

### Files

```
ZEngine/ZEngine/Windows/Inputs/InputFrame.h
ZEngine/ZEngine/Windows/Inputs/InputFrame.cpp
```

### Header

```cpp
// ZEngine/ZEngine/Windows/Inputs/InputFrame.h
#pragma once
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>
#include <ZEngine/Windows/Inputs/KeyCode.h>
#include <ZEngine/Core/Maths/Vec.h>

namespace ZEngine::Windows::Inputs
{
    // Per-frame snapshot of all input state.
    // Call BeginFrame() at the top of each main-loop tick before event polling.
    // Events are fed automatically via the IXxxEventCallback interfaces.
    struct InputFrame
        : public IMouseEventCallback
        , public IKeyboardEventCallback
        , public ITextInputEventCallback
    {
        // ── Frame lifecycle ──────────────────────────────────────────────────
        // Rotate cur→prev, zero scroll delta and text buffer.
        // Called at the top of Engine::MainThreadRun, before PollEvents().
        void BeginFrame();

        // ── Mouse position ───────────────────────────────────────────────────
        Core::Maths::Vec2f MousePos()   const noexcept { return m_mouse_pos; }
        Core::Maths::Vec2f MouseDelta() const noexcept { return m_mouse_delta; }

        // ── Mouse buttons ────────────────────────────────────────────────────
        bool IsMouseDown(ZENGINE_KEYCODE btn)         const noexcept;
        bool IsMouseJustPressed(ZENGINE_KEYCODE btn)  const noexcept;
        bool IsMouseJustReleased(ZENGINE_KEYCODE btn) const noexcept;

        // ── Scroll ────────────────────────────────────────────────────────────
        // Accumulated across all wheel events in this frame. Cleared by BeginFrame.
        Core::Maths::Vec2f ScrollDelta() const noexcept { return m_scroll_delta; }

        // ── Keyboard ─────────────────────────────────────────────────────────
        bool IsKeyDown(ZENGINE_KEYCODE key)         const noexcept;
        bool IsKeyJustPressed(ZENGINE_KEYCODE key)  const noexcept;
        bool IsKeyJustReleased(ZENGINE_KEYCODE key) const noexcept;

        // ── Text input ────────────────────────────────────────────────────────
        // UTF-8 codepoints typed this frame. Cleared by BeginFrame.
        const char* TextInput()    const noexcept { return m_text_buf; }
        uint32_t    TextInputLen() const noexcept { return m_text_len; }

        // ── IMouseEventCallback ───────────────────────────────────────────────
        bool OnMouseButtonPressed(Events::MouseButtonPressedEvent&)   override;
        bool OnMouseButtonReleased(Events::MouseButtonReleasedEvent&) override;
        bool OnMouseButtonMoved(Events::MouseButtonMovedEvent&)       override;
        bool OnMouseButtonWheelMoved(Events::MouseButtonWheelEvent&)  override;

        // ── IKeyboardEventCallback ────────────────────────────────────────────
        bool OnKeyPressed(Events::KeyPressedEvent&)   override;
        bool OnKeyReleased(Events::KeyReleasedEvent&) override;

        // ── ITextInputEventCallback ───────────────────────────────────────────
        bool OnTextInputRaised(Events::TextInputEvent&) override;

        // Process-global singleton. Initialised in Engine::Initialize().
        static InputFrame& Get();

    private:
        static constexpr uint32_t k_MaxKeys    = 512;
        static constexpr uint32_t k_MaxButtons = 8;
        static constexpr uint32_t k_TextBufLen = 64;

        Core::Maths::Vec2f m_mouse_pos      = {};
        Core::Maths::Vec2f m_mouse_prev_pos = {};
        Core::Maths::Vec2f m_mouse_delta    = {};
        Core::Maths::Vec2f m_scroll_delta   = {};

        bool m_keys_cur[k_MaxKeys]        = {};
        bool m_keys_prev[k_MaxKeys]       = {};
        bool m_buttons_cur[k_MaxButtons]  = {};
        bool m_buttons_prev[k_MaxButtons] = {};

        char     m_text_buf[k_TextBufLen] = {};
        uint32_t m_text_len               = 0;
    };
} // namespace ZEngine::Windows::Inputs
```

### Implementation notes

- `BeginFrame()`: `memcpy(m_keys_prev, m_keys_cur, sizeof m_keys_cur)`,
  `memcpy(m_buttons_prev, m_buttons_cur, sizeof m_buttons_cur)`,
  `m_mouse_prev_pos = m_mouse_pos`, `m_mouse_delta = m_mouse_pos - m_mouse_prev_pos`,
  zero `m_scroll_delta`, zero `m_text_buf`, `m_text_len = 0`.
- `IsKeyJustPressed(k)` = `m_keys_cur[k] && !m_keys_prev[k]`
- `IsKeyJustReleased(k)` = `!m_keys_cur[k] && m_keys_prev[k]`
- `OnMouseButtonWheelMoved` accumulates into `m_scroll_delta` (trackpads send multiple
  events per frame).
- `OnTextInputRaised` appends the UTF-8 string into `m_text_buf` up to `k_TextBufLen - 1`.

### Registration

`CoreWindow` registers `InputFrame::Get()` alongside its existing listeners:

```cpp
// CoreWindow (after existing callback registration):
RegisterInputCallback(&InputFrame::Get());
```

`InputFrame::BeginFrame()` is called at the top of `Engine::MainThreadRun` before
`PollEvents()`.

### Input consumption contract

When `UIContext` captures the mouse (a `Clickable` box is hot or active), it sets a
`m_input_consumed` flag. Game systems check `UIContext::IsInputConsumed()` before reading
mouse state — prevents clicks "passing through" UI panels into the 3D scene.

### Deliverables

- [ ] `Windows/Inputs/InputFrame.h/.cpp` — `BeginFrame`, all query methods, all callback implementations
- [ ] `CoreWindow` registers `InputFrame::Get()` as a listener
- [ ] `Engine::MainThreadRun` calls `InputFrame::Get().BeginFrame()` before `PollEvents()`

---

## 5. Step 3 — UIInput

### Why

Translates `InputFrame` state into `Box` hot (hovered) and active (held) state on the
laid-out box tree. Owns hit-testing. Called once per frame in `UIContext::EndFrame()`.

### Files

```
ZEngine/ZEngine/UI/UIInput.h
ZEngine/ZEngine/UI/UIInput.cpp
```

### Header

```cpp
// ZEngine/ZEngine/UI/UIInput.h
#pragma once
#include <ZEngine/Core/Maths/Vec.h>

namespace ZEngine::UI
{
    struct Box;

    // Depth-first search returning the deepest Clickable box whose
    // ComputedAbsPos / ComputedSize rect contains `point`.
    // Children are tested in reverse order (last child is visually on top).
    Box* HitTest(Box* root, Core::Maths::Vec2f point);

    // UpdateInteraction — call once per frame after the layout pass, before render.
    // Reads InputFrame::Get() and updates Hot/Active/Focused on boxes.
    // hot_box:    deepest box under the cursor (updated every frame).
    // active_box: box that owns mouse capture (set on press, cleared on release).
    // focused_box: box with keyboard focus (Tab cycles through Focusable boxes).
    // input_consumed_out: set true if any Clickable box is hot or active.
    void UpdateInteraction(Box*  root,
                           Box** hot_box,
                           Box** active_box,
                           Box** focused_box,
                           bool* input_consumed_out);
}
```

### Interaction rules

```
new_hot = HitTest(root, InputFrame::Get().MousePos())

if new_hot != hot_box:
    old hot_box  → Hot = false
    new_hot      → Hot = true
    hot_box = new_hot

if IsMouseJustPressed(LeftButton):
    active_box = hot_box
    if active_box: active_box->Active = true

if IsMouseJustReleased(LeftButton):
    if active_box: active_box->Active = false
    active_box = nullptr

if IsKeyJustPressed(Tab):
    AdvanceFocus(root, focused_box)

*input_consumed_out = (hot_box != nullptr || active_box != nullptr)
```

`UIContext::Clicked(box)` = `box == active_box && IsMouseJustReleased(LeftButton)`

### Deliverables

- [ ] `UI/UIInput.h/.cpp` — `HitTest`, `UpdateInteraction`, `AdvanceFocus`

---

## 6. Step 4 — BitmapFontAtlas

### Why

This is the critical-path item. No text means no widget captions, no debug values, no console
output. The entire UI is blocked here. stb_truetype is already vendored in `__externals/stb`.

### Approach: bitmap atlas (not MSDF)

MSDF gives better quality at arbitrary sizes but requires a distance-field generation step and
a custom shader. A **bitmap atlas** (stb_truetype `stbtt_BakeFontBitmap`) is sufficient for
debug/overlay/console use at fixed sizes and is buildable in 5–6 days. MSDF can replace it
later without changing the calling API — the `GlyphInfo` struct and `BitmapFontAtlas` interface
are stable.

### Files

```
ZEngine/ZEngine/UI/FontAtlas.h
ZEngine/ZEngine/UI/FontAtlas.cpp
Resources/Engine/Fonts/Inter-Regular.ttf   (OFL licensed, redistributable)
```

### Header

```cpp
// ZEngine/ZEngine/UI/FontAtlas.h
#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <cstdint>

namespace ZEngine::UI
{
    // UV region and metrics for one glyph in the atlas texture.
    struct GlyphInfo
    {
        Core::Maths::Vec2f UV0;      // top-left UV (0..1)
        Core::Maths::Vec2f UV1;      // bottom-right UV (0..1)
        Core::Maths::Vec2f Size;     // pixel size of the glyph rect
        Core::Maths::Vec2f Offset;   // left-bearing + ascent offset (pixels)
        float              Advance;  // horizontal advance (pixels)
    };

    using FontHandle = uint32_t;
    static constexpr FontHandle k_InvalidFont = UINT32_MAX;

    // Single-channel R8 GPU texture atlas for one typeface at one size.
    // Baked at startup from a .ttf file. Covers printable ASCII (codepoints 32–126).
    struct BitmapFontAtlas
    {
        // Load .ttf, rasterize glyphs, upload VK_FORMAT_R8_UNORM texture.
        // ttf_path  — VFS path to the .ttf file
        // font_size — pixel height (e.g. 16.f)
        // atlas_dim — texture dimension (power-of-2; 512 recommended for 16px)
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        Hardwares::VulkanDevice*       device,
                        const char*                    ttf_path,
                        float                          font_size,
                        uint32_t                       atlas_dim = 512);

        void Destroy(Hardwares::VulkanDevice* device);

        // Query glyph info for a UTF-32 codepoint.
        // Writes a '?' fallback rect for unknown codepoints.
        bool GetGlyph(uint32_t codepoint, GlyphInfo* out) const noexcept;

        // Measure the pixel width of a null-terminated UTF-8 string.
        float MeasureText(const char* text) const noexcept;

        // Height of a line of text (ascent + descent + line gap).
        float LineHeight() const noexcept { return m_line_height; }

        // VkImageView handle — passed to UIRenderer for descriptor binding.
        VkImageView AtlasView() const noexcept { return m_atlas_view; }

        float FontSize() const noexcept { return m_font_size; }

    private:
        static constexpr uint32_t k_FirstChar = 32;
        static constexpr uint32_t k_CharCount = 95;  // codepoints 32–126

        // One entry per codepoint (stbtt_bakedchar layout).
        struct BakedChar { uint16_t x0,y0,x1,y1; float xoff,yoff,xadvance; };

        BakedChar*  m_glyphs      = nullptr;  // arena-allocated
        VkImage     m_atlas_img   = VK_NULL_HANDLE;
        VkImageView m_atlas_view  = VK_NULL_HANDLE;
        VkDeviceMemory m_atlas_mem = VK_NULL_HANDLE;
        float       m_font_size   = 0.f;
        float       m_line_height = 0.f;
        uint32_t    m_atlas_dim   = 0;
    };

    // Registry: up to 8 named fonts loaded at startup.
    struct FontRegistry
    {
        static void             Register(const char* name, BitmapFontAtlas* atlas);
        static BitmapFontAtlas* Get(FontHandle handle) noexcept;
        static FontHandle       Find(const char* name) noexcept;
        static FontHandle       Default() noexcept;  // handle 0 = first registered
    };
}
```

### Implementation steps

1. Read .ttf bytes into a temp arena scratch buffer via `VFSContext::Open(ttf_path)`.
2. Call `stbtt_BakeFontBitmap(ttf_data, 0, font_size, bitmap, atlas_dim, atlas_dim, k_FirstChar, k_CharCount, baked_chars)`.
3. Upload the single-channel R8 bitmap to a `VK_FORMAT_R8_UNORM` texture via the existing
   `VulkanDevice` staging upload path.
4. Free the CPU bitmap (it was in a scratch arena that is cleared after upload).
5. `GetGlyph(cp)`: index into `m_glyphs[cp - k_FirstChar]` and compute `UV0/UV1` by
   dividing pixel coords by `m_atlas_dim`. For `cp` outside `[32, 126]`, return the `?` glyph.
6. `MeasureText`: iterate UTF-8 bytes, decode codepoints, sum `xadvance` values.

### Registration at engine startup

```cpp
// Engine::Initialize()
auto* default_font = ZPushStructCtor(m_ui_arena, UI::BitmapFontAtlas);
default_font->Initialize(m_ui_arena, m_vulkan_device,
                         "Engine/Fonts/Inter-Regular.ttf", 16.f, 512);
UI::FontRegistry::Register("default", default_font);
```

### Deliverables

- [ ] `UI/FontAtlas.h/.cpp` — `BitmapFontAtlas`, `GlyphInfo`, `FontRegistry`
- [ ] `Inter-Regular.ttf` in `Resources/Engine/Fonts/`
- [ ] Default font registered in `Engine::Initialize()`

---

## 7. Step 5 — Box and UIContext

### Files

```
ZEngine/ZEngine/UI/Box.h
ZEngine/ZEngine/UI/UIContext.h
ZEngine/ZEngine/UI/UIContext.cpp
```

### Box.h

The single widget primitive. Every visible element — label, button, scroll area, separator —
is a `Box`. No subclasses. Type is implied by `BoxFlags`.

```cpp
// ZEngine/ZEngine/UI/Box.h
#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/UI/FontAtlas.h>
#include <cstdint>

namespace ZEngine::UI
{
    // ── Size kinds ────────────────────────────────────────────────────────────
    enum SizeKind : uint8_t
    {
        SizeKind_Null,            // 0 — no preference; stays 0 unless parent forces
        SizeKind_Pixels,          // exact pixel count
        SizeKind_TextContent,     // fit to DisplayString extent + padding
        SizeKind_PercentOfParent, // Value is 0..1 fraction of parent's resolved size
        SizeKind_ChildrenSum,     // grow to contain all children along the layout axis
    };

    struct Size
    {
        SizeKind Kind       = SizeKind_Null;
        float    Value      = 0.f;
        float    Strictness = 1.f;  // 1 = hard, 0 = freely violated by violation pass
    };

    enum Axis2 : uint8_t { Axis2_X = 0, Axis2_Y = 1, Axis2_COUNT = 2 };

    // ── BoxFlags ───────────────────────────────────────────────────────────────
    using BoxFlags = uint32_t;
    enum BoxFlag : BoxFlags
    {
        BoxFlag_DrawBackground = 1 << 0,
        BoxFlag_DrawBorder     = 1 << 1,
        BoxFlag_DrawText       = 1 << 2,
        BoxFlag_Clip           = 1 << 3,   // scissor to ComputedSize
        BoxFlag_Clickable      = 1 << 4,
        BoxFlag_Scrollable     = 1 << 5,
        BoxFlag_Focusable      = 1 << 6,
        BoxFlag_FloatingX      = 1 << 7,   // exempt from parent layout cursor (X axis)
        BoxFlag_FloatingY      = 1 << 8,   // exempt from parent layout cursor (Y axis)
        BoxFlag_LayoutAxisX    = 1 << 9,   // children laid out horizontally (default: vertical)
        BoxFlag_AnimateHot     = 1 << 10,
        BoxFlag_AnimateActive  = 1 << 11,
        BoxFlag_NoInput        = 1 << 12,  // invisible to hit-testing; passes through
        BoxFlag_DrawShadow     = 1 << 13,
    };

    // ── Box ───────────────────────────────────────────────────────────────────
    struct Box
    {
        // Identity
        uint64_t    Key = 0;         // StringHash(tag); persistent across frames
        const char* Tag = nullptr;

        // Tree (within the current frame's arena)
        Box* Parent      = nullptr;
        Box* FirstChild  = nullptr;
        Box* LastChild   = nullptr;
        Box* NextSibling = nullptr;
        Box* PrevSibling = nullptr;

        // Layout input — set by caller each frame
        BoxFlags Flags                     = 0;
        Size     SemanticSize[Axis2_COUNT] = {};

        // Layout output — written by Layout pass
        Core::Maths::Vec2f ComputedRelPos = {};   // relative to parent's top-left
        Core::Maths::Vec2f ComputedAbsPos = {};   // absolute screen position
        Core::Maths::Vec2f ComputedSize   = {};

        // Style — snapshotted from UIContext stacks at BoxMake time
        Core::Maths::Vec4f BackgroundColor = { 0.f, 0.f, 0.f, 0.f };
        Core::Maths::Vec4f BorderColor     = { 1.f, 1.f, 1.f, 1.f };
        Core::Maths::Vec4f TextColor       = { 1.f, 1.f, 1.f, 1.f };
        float              BorderThickness = 1.f;
        float              CornerRadius    = 0.f;
        Core::Maths::Vec4f Padding         = {};  // left, right, top, bottom
        FontHandle         Font            = k_InvalidFont;
        float              FontSizePx      = 16.f;

        // Content
        const char* DisplayString = nullptr;  // frame-arena copy; null if no text

        // Interaction (read by caller after BoxMake)
        bool Hot     = false;
        bool Active  = false;
        bool Focused = false;

        // Animation — lerped each frame via the persistent anim map
        float HotTransition    = 0.f;   // 0 = cold, 1 = fully hot
        float ActiveTransition = 0.f;

        // Frame stamp — box is stale if LastFrameTouched != UIContext::CurrentFrame()
        uint64_t LastFrameTouched = 0;

        // Scroll state (Scrollable boxes only)
        float ScrollOffsetY = 0.f;
    };
}
```

### UIContext.h

```cpp
// ZEngine/ZEngine/UI/UIContext.h
#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/UI/Box.h>

namespace ZEngine::UI
{
    class UIContext
    {
    public:
        // persistent_arena: owns the animation state map (lives for UIContext lifetime).
        // per_frame_arena_size: frame arena reset each BeginFrame (default 4 MiB).
        void Initialize(Core::Memory::ArenaAllocator* persistent_arena,
                        uint32_t per_frame_arena_size = 4 * 1024 * 1024);
        void Destroy();

        // ── Frame lifecycle ───────────────────────────────────────────────────
        void BeginFrame(float dt, Core::Maths::Vec2f viewport_size);
        // EndFrame: runs layout, hit-testing, animation update, stale box pruning.
        void EndFrame();

        // ── Box construction ──────────────────────────────────────────────────
        // Retrieve-or-create a box for this frame. Style stacks are snapshotted here.
        // `tag` must be unique among siblings (or use ## suffix to disambiguate).
        Box* BoxMake(BoxFlags flags, const char* tag);

        // BoxMake with a printf-style display string (copied into frame arena).
        Box* BoxMakeF(BoxFlags flags, const char* tag, const char* fmt, ...);

        // ── Interaction queries ───────────────────────────────────────────────
        // Call after BoxMake and before the next PopParent.
        bool Clicked(const Box* box)  const noexcept;  // just-released while active
        bool Hovered(const Box* box)  const noexcept;  // is the hot box
        bool IsActive(const Box* box) const noexcept;  // owns mouse capture
        bool IsFocused(const Box* box)const noexcept;  // has keyboard focus
        bool IsInputConsumed()        const noexcept { return m_input_consumed; }

        // ── Style stacks ──────────────────────────────────────────────────────
        void PushBackgroundColor(Core::Maths::Vec4f c);    void PopBackgroundColor();
        void PushBorderColor(Core::Maths::Vec4f c);        void PopBorderColor();
        void PushTextColor(Core::Maths::Vec4f c);          void PopTextColor();
        void PushBorderThickness(float t);                 void PopBorderThickness();
        void PushCornerRadius(float r);                    void PopCornerRadius();
        void PushPadding(Core::Maths::Vec4f p);            void PopPadding();
        void PushFont(FontHandle f, float size_px);        void PopFont();
        // PushParent is implicit: BoxMake appends to the current parent.
        // Call these to create an explicit parent scope.
        void PushParent(Box* b);                           void PopParent();

        // ── Accessors ─────────────────────────────────────────────────────────
        Box*     Root()         const noexcept { return m_root; }
        uint64_t CurrentFrame() const noexcept { return m_frame_index; }
        float    DeltaTime()    const noexcept { return m_dt; }
        Core::Maths::Vec2f ViewportSize() const noexcept { return m_viewport_size; }

    private:
        // Persistent animation state, keyed by Box::Key.
        struct AnimState { float Hot = 0.f; float Active = 0.f; float ScrollY = 0.f; };
        Core::Containers::UnorderedHashMap<uint64_t, AnimState> m_anim_states;

        Core::Memory::ArenaAllocator  m_frame_arena;
        Core::Memory::ArenaAllocator* m_persistent_arena = nullptr;

        Box*  m_root        = nullptr;
        Box*  m_hot_box     = nullptr;
        Box*  m_active_box  = nullptr;
        Box*  m_focused_box = nullptr;
        bool  m_input_consumed = false;

        float m_dt = 0.f;
        uint64_t m_frame_index = 0;
        Core::Maths::Vec2f m_viewport_size = {};

        // Style stacks (arrays used as stacks — Top() returns last element)
        Core::Containers::Array<Core::Maths::Vec4f> m_stack_bg;
        Core::Containers::Array<Core::Maths::Vec4f> m_stack_border_color;
        Core::Containers::Array<Core::Maths::Vec4f> m_stack_text_color;
        Core::Containers::Array<float>               m_stack_border_thickness;
        Core::Containers::Array<float>               m_stack_corner_radius;
        Core::Containers::Array<Core::Maths::Vec4f> m_stack_padding;
        Core::Containers::Array<FontHandle>          m_stack_font;
        Core::Containers::Array<float>               m_stack_font_size;
        Core::Containers::Array<Box*>                m_stack_parent;

        void SnapshotStyle(Box* box);
        void PruneStaleBoxes();
        void UpdateAnimations();
    };
}
```

### UIContext::BoxMake internals

```
1.  key = StringHash(tag)
2.  Lookup key in m_anim_states (persistent map)
3.  Allocate Box from m_frame_arena (ZPushStructCtor)
4.  Fill Box::Key, Box::Tag, Box::LastFrameTouched = m_frame_index
5.  SnapshotStyle(box) — copies top of every style stack into box fields
6.  Link box under m_stack_parent.Top() as new last child
7.  If anim entry found: copy old Hot/Active/ScrollY transition values into box
8.  Lerp animation:
      box->HotTransition    = Lerp(old.Hot,    box->Hot    ? 1.f : 0.f, m_dt * 10.f)
      box->ActiveTransition = Lerp(old.Active, box->Active ? 1.f : 0.f, m_dt * 20.f)
9.  Return box
```

### UIContext::BeginFrame / EndFrame

```
BeginFrame:
  m_frame_arena.Clear()
  m_frame_index++
  Create root box (full viewport, no flags) from m_frame_arena
  Push root onto m_stack_parent
  Push default style values onto all stacks (bg=transparent, text=white, etc.)

EndFrame:
  Pop root from m_stack_parent
  ZENGINE_VALIDATE_ASSERT(m_stack_parent.IsEmpty(), "Mismatched PushParent/PopParent")
  Layout::Solve(m_root)
  UIInput::UpdateInteraction(m_root, &m_hot_box, &m_active_box, &m_focused_box, &m_input_consumed)
  PruneStaleBoxes()   — remove anim_states entries not touched this frame
  UpdateAnimations()  — write back HotTransition/ActiveTransition to m_anim_states
```

### Deliverables

- [ ] `UI/Box.h` — `Box`, `Size`, `SizeKind`, `BoxFlags`, `Axis2`
- [ ] `UI/UIContext.h/.cpp` — `BeginFrame`, `EndFrame`, `BoxMake`, `BoxMakeF`, style stacks, interaction queries

---

## 8. Step 6 — Layout Pass

### Files

```
ZEngine/ZEngine/UI/Layout.h
ZEngine/ZEngine/UI/Layout.cpp
```

### API

```cpp
// ZEngine/ZEngine/UI/Layout.h
#pragma once
namespace ZEngine::UI { struct Box; }

namespace ZEngine::UI::Layout
{
    // Run all 5 phases on the tree rooted at `root`.
    // Writes ComputedSize, ComputedRelPos, and ComputedAbsPos on every box.
    // All allocations use the box's owning arena (passed via root).
    void Solve(Box* root);
}
```

### Five phases (all tree walks on the frame-arena box tree, zero heap allocation)

**Phase 1 — Standalone sizes (post-order)**

For each box, for each axis:
- `SizeKind_Pixels`      → `ComputedSize[axis] = Value`
- `SizeKind_TextContent` → X: `FontAtlas::MeasureText(DisplayString) + Padding.left + Padding.right`;
                           Y: `FontAtlas::LineHeight() + Padding.top + Padding.bottom`
- `SizeKind_Null`        → `ComputedSize[axis] = 0` (may be updated later)

**Phase 2 — ChildrenSum (post-order)**

For each box where `SemanticSize[axis].Kind == SizeKind_ChildrenSum`:
- Along the layout axis: sum all children's `ComputedSize[axis]`.
- Along the cross axis: take the max of all children's `ComputedSize[cross]`.

Post-order ensures children are resolved before their parent reads their sizes.

**Phase 3 — PercentOfParent (pre-order)**

For each box where `SemanticSize[axis].Kind == SizeKind_PercentOfParent`:
- `ComputedSize[axis] = parent->ComputedSize[axis] * Value`

Pre-order ensures the parent's size is resolved before children read it.

**Phase 4 — Violation fixing**

For each box on the layout axis, if the sum of children sizes exceeds the parent's size:
- Collect children with `Strictness < 1.0` on that axis.
- Reduce each such child proportionally:
  `child->ComputedSize[axis] -= overflow * child->SemanticSize[axis].Strictness_complement`
- Children with `Strictness == 1.0` are never shrunk.

**Phase 5 — Position (pre-order)**

For each box, walk children maintaining a layout cursor:
- Cursor starts at `(Padding.left, Padding.top)` for the parent.
- `BoxFlag_LayoutAxisX`: advance cursor in X after each non-floating child.
- Default (vertical): advance cursor in Y.
- `BoxFlag_FloatingX` / `BoxFlag_FloatingY`: skip cursor for that axis (absolute positioning).
- Set `ComputedRelPos = cursor_position`.
- Set `ComputedAbsPos = parent->ComputedAbsPos + ComputedRelPos`.

### Deliverables

- [ ] `UI/Layout.h/.cpp` — `Layout::Solve(Box*)` implementing all 5 phases with no heap allocation

---

## 9. Step 7 — UIRenderer

### Files

```
ZEngine/ZEngine/UI/UIRenderer.h
ZEngine/ZEngine/UI/UIRenderer.cpp
Resources/Shaders/ui.vert
Resources/Shaders/ui.frag
```

### Header

```cpp
// ZEngine/ZEngine/UI/UIRenderer.h
#pragma once
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/UI/UIContext.h>
#include <ZEngine/Core/Maths/Vec.h>

namespace ZEngine::UI
{
    // One vertex emitted by BuildDrawList.
    struct UIVertex
    {
        Core::Maths::Vec2f Pos;      // screen-space pixels
        Core::Maths::Vec2f UV;       // atlas UV; (0,0) = use solid color
        Core::Maths::Vec4f Color;    // pre-multiplied alpha
        uint32_t           TexID;    // 0 = white 1x1 (solid), 1 = font atlas
    };

    // IRenderGraphCallbackPass registered as the last node in the RenderGraph.
    class UIRenderer : public Rendering::Renderers::IRenderGraphCallbackPass
    {
    public:
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        Hardwares::VulkanDevice*       device,
                        uint32_t                       max_vertices = 65536);
        void Destroy();

        void SetContext(UIContext* ctx) { m_ctx = ctx; }

        // IRenderGraphCallbackPass
        void        Setup(Rendering::Renderers::RenderGraphResourceBuilder&) override;
        void        Compile(Rendering::Renderers::RenderGraphResourceInspector&) override;
        void        Execute(VkCommandBuffer cmd,
                            const Rendering::Renderers::RenderGraph& rg) override;
        const char* GetName() const override { return "UIPass"; }

    private:
        void BuildDrawList(Box* box);
        void FlushBatch(VkCommandBuffer cmd);
        void EmitQuad(Core::Maths::Vec2f pos,  Core::Maths::Vec2f size,
                      Core::Maths::Vec2f uv0,  Core::Maths::Vec2f uv1,
                      Core::Maths::Vec4f color, uint32_t tex_id);
        void EmitText(const char* text, Core::Maths::Vec2f origin,
                      Core::Maths::Vec4f color, BitmapFontAtlas* font);
        void EmitRoundedRect(Core::Maths::Vec2f pos, Core::Maths::Vec2f size,
                             float radius, Core::Maths::Vec4f color);

        UIContext*              m_ctx        = nullptr;
        Hardwares::VulkanDevice* m_device    = nullptr;
        Core::Memory::ArenaAllocator* m_arena = nullptr;

        UIVertex* m_vertices   = nullptr;   // frame-arena; reset each Execute
        uint32_t  m_vert_count = 0;
        uint32_t  m_max_verts  = 0;

        VkBuffer       m_vb       = VK_NULL_HANDLE;
        VkDeviceMemory m_vb_mem   = VK_NULL_HANDLE;

        VkPipeline            m_pipeline        = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipeline_layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_desc_layout     = VK_NULL_HANDLE;
        VkDescriptorSet       m_desc_set        = VK_NULL_HANDLE;

        VkImage        m_white_img  = VK_NULL_HANDLE;  // 1x1 white RGBA
        VkImageView    m_white_view = VK_NULL_HANDLE;
        VkSampler      m_sampler    = VK_NULL_HANDLE;
    };
}
```

### Shaders

**ui.vert**
```glsl
layout(push_constant) uniform PC { vec2 inv_viewport; } pc;
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in uint aTexID;
layout(location=0) out vec2 vUV;
layout(location=1) out vec4 vColor;
layout(location=2) out flat uint vTexID;
void main() {
    gl_Position = vec4(aPos * pc.inv_viewport * 2.0 - 1.0, 0.0, 1.0);
    vUV    = aUV;
    vColor = aColor;
    vTexID = aTexID;
}
```

**ui.frag**
```glsl
layout(binding=0) uniform sampler2D uWhite;   // 1x1 white
layout(binding=1) uniform sampler2D uFont;    // font atlas (R8_UNORM)
layout(location=0) in  vec2 vUV;
layout(location=1) in  vec4 vColor;
layout(location=2) in flat uint vTexID;
layout(location=0) out vec4 outColor;
void main() {
    float alpha = (vTexID == 1u) ? texture(uFont, vUV).r : 1.0;
    outColor = vColor * alpha;
    if (outColor.a < 0.01) discard;
}
```

Pipeline blend state: `VK_BLEND_FACTOR_ONE` / `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA`
(pre-multiplied alpha). No depth test. No face culling.

### BuildDrawList traversal (pre-order)

```
For each box (pre-order):
  if BoxFlag_Clip:        vkCmdSetScissor to ComputedAbsPos / ComputedSize
  if BoxFlag_DrawShadow:  EmitQuad (offset +2,+4; dark semi-transparent)
  if BoxFlag_DrawBackground:
    if CornerRadius > 0:  EmitRoundedRect (approximate with fan triangles)
    else:                 EmitQuad (solid)
  if BoxFlag_DrawBorder:  4 thin quads (top/bottom/left/right edges)
  if BoxFlag_DrawText && DisplayString:
    EmitText(DisplayString, ComputedAbsPos + padding, TextColor, font)
  recurse children
  if BoxFlag_Clip:        restore scissor
```

### Deliverables

- [ ] `UI/UIRenderer.h/.cpp` — `UIRenderer` implementing `IRenderGraphCallbackPass`
- [ ] `Resources/Shaders/ui.vert` + `ui.frag`
- [ ] Compiled `.spv` cached in `Resources/Shaders/Cache/`
- [ ] UIRenderer registered as the last pass in `RenderGraph` in `Engine::Initialize()`

---

## 10. Step 8 — Widgets Layer

### Files

```
ZEngine/ZEngine/UI/Widgets.h
ZEngine/ZEngine/UI/Widgets.cpp
```

### API

```cpp
// ZEngine/ZEngine/UI/Widgets.h
#pragma once
#include <ZEngine/UI/UIContext.h>

namespace ZEngine::UI::Widgets
{
    // ── Text ──────────────────────────────────────────────────────────────────
    Box* Label(UIContext* ctx, const char* text);
    Box* LabelF(UIContext* ctx, const char* tag, const char* fmt, ...);

    // ── Buttons ───────────────────────────────────────────────────────────────
    bool Button(UIContext* ctx, const char* label);
    bool IconButton(UIContext* ctx, const char* tag,
                    Core::Maths::Vec2f icon_uv0, Core::Maths::Vec2f icon_uv1);

    // ── Controls ──────────────────────────────────────────────────────────────
    bool Checkbox(UIContext* ctx, const char* label, bool* value);
    bool SliderFloat(UIContext* ctx, const char* label,
                     float* v, float v_min, float v_max);
    bool InputText(UIContext* ctx, const char* label,
                   char* buf, uint32_t buf_len);
    bool ColorPicker(UIContext* ctx, const char* label, Core::Maths::Vec4f* color);

    // ── Layout helpers ─────────────────────────────────────────────────────────
    void Spacer(UIContext* ctx, float pixels);    // fixed-size empty box
    void Separator(UIContext* ctx);               // 1px horizontal rule

    // Horizontal row scope: children are laid out left-to-right.
    // BeginRow pushes a LayoutAxisX parent; EndRow pops it.
    void BeginRow(UIContext* ctx, const char* tag);
    void EndRow(UIContext* ctx);

    // ── Containers ────────────────────────────────────────────────────────────
    Box* BeginPanel(UIContext* ctx, const char* tag, Core::Maths::Vec2f size,
                    Core::Maths::Vec4f bg = {0.12f, 0.12f, 0.12f, 0.95f});
    void EndPanel(UIContext* ctx);

    Box* BeginScrollArea(UIContext* ctx, const char* tag,
                         Core::Maths::Vec2f size, float* scroll_y);
    void EndScrollArea(UIContext* ctx);

    bool BeginCollapsible(UIContext* ctx, const char* label, bool* open);
    void EndCollapsible(UIContext* ctx);
}
```

### Example: Button

```cpp
bool Widgets::Button(UIContext* ctx, const char* label)
{
    ctx->PushBackgroundColor({0.20f, 0.40f, 0.80f, 1.f});
    ctx->PushCornerRadius(4.f);
    ctx->PushPadding({8.f, 8.f, 4.f, 4.f});

    Box* btn = ctx->BoxMakeF(
        BoxFlag_DrawBackground | BoxFlag_DrawBorder |
        BoxFlag_DrawText | BoxFlag_Clickable |
        BoxFlag_AnimateHot | BoxFlag_AnimateActive,
        label, "%s", label);

    btn->SemanticSize[Axis2_X] = { SizeKind_TextContent, 0.f, 1.f };
    btn->SemanticSize[Axis2_Y] = { SizeKind_TextContent, 0.f, 1.f };

    // Brighten on hover using the smooth HotTransition value
    float h = btn->HotTransition;
    btn->BackgroundColor = { 0.20f + h * 0.15f, 0.40f + h * 0.10f, 0.80f + h * 0.10f, 1.f };

    bool clicked = ctx->Clicked(btn);

    ctx->PopPadding();
    ctx->PopCornerRadius();
    ctx->PopBackgroundColor();
    return clicked;
}
```

### Example: SliderFloat

```cpp
bool Widgets::SliderFloat(UIContext* ctx, const char* label,
                          float* v, float v_min, float v_max)
{
    BeginRow(ctx, label);
      Label(ctx, label);

      Box* track = ctx->BoxMakeF(
          BoxFlag_DrawBackground | BoxFlag_Clickable | BoxFlag_AnimateHot,
          label, "##track");
      track->SemanticSize[Axis2_X] = { SizeKind_PercentOfParent, 0.6f, 0.8f };
      track->SemanticSize[Axis2_Y] = { SizeKind_Pixels, 6.f, 1.f };

      if (ctx->IsActive(track)) {
          // Map drag X into [v_min, v_max]
          Core::Maths::Vec2f delta = InputFrame::Get().MouseDelta();
          float range = v_max - v_min;
          *v = Core::Maths::Clamp(*v + delta.X / track->ComputedSize.X * range,
                                  v_min, v_max);
      }

      float t = (*v - v_min) / (v_max - v_min);
      // Emit fill as a child floating box
      ctx->PushParent(track);
      Box* fill = ctx->BoxMake(BoxFlag_DrawBackground | BoxFlag_FloatingY, "##fill");
      fill->SemanticSize[Axis2_X] = { SizeKind_PercentOfParent, t, 1.f };
      fill->SemanticSize[Axis2_Y] = { SizeKind_PercentOfParent, 1.f, 1.f };
      fill->BackgroundColor = { 0.3f, 0.6f, 1.f, 1.f };
      ctx->PopParent();

    EndRow(ctx);
    return ctx->IsActive(track);
}
```

### Deliverables

- [ ] `UI/Widgets.h/.cpp` — Label, LabelF, Button, IconButton, Checkbox, SliderFloat, InputText, ColorPicker, Spacer, Separator, BeginRow/EndRow, BeginPanel/EndPanel, BeginScrollArea/EndScrollArea, BeginCollapsible/EndCollapsible

---

## 11. DebugOverlay and DebugConsole Integration

Once all 8 steps are done, `DebugOverlay` and `DebugConsole` (specified in `profiling.md`)
use `UIContext` directly:

```cpp
// DebugOverlay::Render
void DebugOverlay::Render() {
    if (!m_visible) return;
    using namespace UI::Widgets;

    BeginPanel(m_ui_ctx, "debug_overlay", {320.f, 0.f}); // height = ChildrenSum
    m_ui_ctx->Root()->SemanticSize[Axis2_Y] = { SizeKind_ChildrenSum, 0.f, 1.f };

      LabelF(m_ui_ctx, "fps",    "FPS:      %.0f",  m_fps);
      LabelF(m_ui_ctx, "cpu_ms", "CPU:      %.2f ms", m_cpu_ms);
      LabelF(m_ui_ctx, "gpu_ms", "GPU:      %.2f ms", m_gpu_ms);
      LabelF(m_ui_ctx, "draws",  "Draws:    %u",     m_draw_calls);
      LabelF(m_ui_ctx, "ents",   "Entities: %u",     m_entity_count);
      // Memory bars (arena stats from MemoryProfiler)
      for (uint32_t i = 0; i < m_arena_stats.Size(); ++i) {
          auto& a = m_arena_stats[i];
          float t = float(a.CurrentOffset) / float(a.Capacity);
          Core::Maths::Vec4f fill = t < 0.5f ? Vec4f{0.2f,0.8f,0.2f,1.f}
                                 : t < 0.8f ? Vec4f{0.9f,0.8f,0.1f,1.f}
                                            : Vec4f{0.9f,0.2f,0.1f,1.f};
          Widgets::SliderFloat(m_ui_ctx, a.Name,
                               (float*)&a.CurrentOffset,   // read-only display
                               0.f, float(a.Capacity));
      }
    EndPanel(m_ui_ctx);
}
```

F3 toggle wired to `InputFrame::Get().IsKeyJustPressed(ZENGINE_KEY_F3)`.
Tilde toggle for DebugConsole: `ZENGINE_KEY_GRAVE_ACCENT`.

---

## 12. UIScreenStack (Menus)

Game menus use `UIScreenStack` — screens push/pop onto a stack; each draws via `UIContext`
each frame.

```cpp
// ZEngine/ZEngine/UI/UIScreen.h
namespace ZEngine::UI {
    class UIScreen {
    public:
        virtual ~UIScreen() = default;
        virtual void Draw(UIContext& ctx, float dt) = 0;
        virtual void OnEnter() {}
        virtual void OnExit()  {}
        virtual bool IsOpaque() const { return true; }  // false = draw screen below too
    };
}
```

```cpp
// ZEngine/ZEngine/UI/UIScreenStack.h
namespace ZEngine::UI {
    class UIScreenStack {
    public:
        static constexpr int k_MaxDepth = 16;
        void Push(Helpers::Ref<UIScreen> screen);
        void Pop();
        UIScreen* Peek() const noexcept;
        bool IsEmpty()   const noexcept;
        void Draw(UIContext& ctx, float dt);
    private:
        Core::Containers::Array<Helpers::Ref<UIScreen>> m_stack;
    };
}
```

Main loop integration:

```cpp
// Engine::MainThreadRun — between BeginFrame and EndFrame:
m_ui_ctx->BeginFrame(dt, viewport_size);
m_screen_stack->Draw(*m_ui_ctx, dt);    // menus
m_hud_system->Draw(*m_ui_ctx, dt);      // always-on HUD
m_debug_overlay->Render();              // F3 toggle
m_debug_console->Render();              // tilde toggle (debug only)
m_ui_ctx->EndFrame();
```

---

## 13. File Layout

```
ZEngine/ZEngine/
├── Core/Containers/
│   └── StringHash.h                   Step 1
│
├── Windows/Inputs/
│   ├── InputFrame.h                   Step 2
│   └── InputFrame.cpp
│
└── UI/
    ├── Box.h                          Step 5
    ├── UIContext.h                    Step 5
    ├── UIContext.cpp
    ├── UIInput.h                      Step 3
    ├── UIInput.cpp
    ├── FontAtlas.h                    Step 4
    ├── FontAtlas.cpp
    ├── Layout.h                       Step 6
    ├── Layout.cpp
    ├── UIRenderer.h                   Step 7
    ├── UIRenderer.cpp
    ├── Widgets.h                      Step 8
    ├── Widgets.cpp
    ├── UIScreen.h                     §12
    ├── UIScreenStack.h
    └── UIScreenStack.cpp

Resources/
├── Shaders/
│   ├── ui.vert                        Step 7
│   └── ui.frag
│   └── Cache/ (ui.vert.spv, ui.frag.spv)
└── Engine/Fonts/
    └── Inter-Regular.ttf              Step 4
```

---

## 14. ImGui Coexistence

- `ImGUIRenderer` stays untouched. Both renderers register as separate `IRenderGraphCallbackPass` nodes.
- UIPass is the final node; ImGuiPass runs before it.
- `SceneViewportUIComponent` keeps ImGui + ImGuizmo permanently — gizmos depend on ImGui draw lists.
- Other Tetragrama panels migrate one-by-one after Widgets layer is stable: `LogUIComponent` first (simplest), then `InspectorViewUIComponent`, `HierarchyViewUIComponent`, `ProjectViewUIComponent`.
- No migration timeline pressure. First milestone is DebugOverlay + DebugConsole working via UIContext.

---

## 15. Deliverables Checklist

### Step 1 — StringHash
- [ ] `Core/Containers/StringHash.h` — `StringHash(const char*)` + `StringHashN`

### Step 2 — InputFrame
- [ ] `Windows/Inputs/InputFrame.h/.cpp` — `BeginFrame`, all query methods, all callbacks
- [ ] `CoreWindow` registers `InputFrame::Get()` as a listener
- [ ] `Engine::MainThreadRun` calls `InputFrame::Get().BeginFrame()` before `PollEvents()`

### Step 3 — UIInput
- [ ] `UI/UIInput.h/.cpp` — `HitTest`, `UpdateInteraction`, `AdvanceFocus`

### Step 4 — BitmapFontAtlas
- [ ] `UI/FontAtlas.h/.cpp` — `BitmapFontAtlas`, `GlyphInfo`, `FontRegistry`
- [ ] `Resources/Engine/Fonts/Inter-Regular.ttf` present
- [ ] Default font registered in `Engine::Initialize()`

### Step 5 — Box + UIContext
- [ ] `UI/Box.h` — `Box`, `Size`, `SizeKind`, `BoxFlags`
- [ ] `UI/UIContext.h/.cpp` — `BeginFrame`, `EndFrame`, `BoxMake`, `BoxMakeF`, all stacks, all queries

### Step 6 — Layout
- [ ] `UI/Layout.h/.cpp` — `Layout::Solve(Box*)` with all 5 phases; zero heap allocation

### Step 7 — UIRenderer
- [ ] `UI/UIRenderer.h/.cpp` — `IRenderGraphCallbackPass`; quad batcher; text glyph emitter
- [ ] `Resources/Shaders/ui.vert` + `ui.frag` (plus compiled `.spv` cache)
- [ ] UIRenderer registered as last RenderGraph pass in `Engine::Initialize()`

### Step 8 — Widgets
- [ ] `UI/Widgets.h/.cpp` — Label, LabelF, Button, IconButton, Checkbox, SliderFloat, InputText, ColorPicker, Spacer, Separator, BeginRow/EndRow, BeginPanel/EndPanel, BeginScrollArea/EndScrollArea, BeginCollapsible/EndCollapsible

### Screen stack
- [ ] `UI/UIScreen.h` — abstract base
- [ ] `UI/UIScreenStack.h/.cpp` — Push/Pop/Peek/Draw (max depth 16)

### Integration
- [ ] `DebugOverlay` reads from `UIContext` instead of ImGui stubs
- [ ] `DebugConsole` reads from `UIContext` (Debug builds only)
- [ ] F3 and tilde toggle wired through `InputFrame::Get()`

---

## 16. Verification

- [ ] `StringHash("hello") == StringHash("hello")` across multiple calls (deterministic)
- [ ] `InputFrame::IsKeyJustPressed` true only on the one frame of key-down transition
- [ ] `InputFrame::MouseDelta()` is zero when cursor does not move
- [ ] `BitmapFontAtlas::MeasureText("Hello")` returns a positive non-zero float
- [ ] Layout test (no GPU): parent 400px wide, two children `SizeKind_PercentOfParent = 0.5` each → `ComputedSize.X == 200` for both
- [ ] `Button` returns `true` exactly once on click, not on hold
- [ ] `SliderFloat` responds to drag; clamps to [min, max]
- [ ] `BeginScrollArea` clips children and scrolls on mouse wheel
- [ ] F3 shows DebugOverlay with non-zero FPS and frame time
- [ ] Tilde shows DebugConsole (Debug build); `help` outputs command list
- [ ] UIRenderer draws correctly with 0 boxes (empty frame = no crash)
- [ ] `ZENGINE_PROFILE_SCOPE("UIContext::EndFrame")` reads < 0.2 ms for 500 boxes in Debug
