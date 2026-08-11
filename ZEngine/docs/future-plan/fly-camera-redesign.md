# FlyCamera Redesign

**Replaces:** `FlyCamera.h/.cpp`, `FlyCameraController.h/.cpp`, `EditorCameraController.h/.cpp`
**Relates to:** `rendering-flow.md`, `input-system.md`
**Scope:** Ground-up redesign of the editor fly camera — input model, state machine, coordinate system, HiDPI correctness, InputManager integration.

---

## 1. What Is Wrong With the Current Design

**Input state is split across two objects.**
`FlyCamera` owns `m_keys[512]`, `m_rightMouseDown`, `m_middleMouseDown`, `m_leftMouseDown`,
`m_altDown`, `m_shiftDown`, `m_ctrlDown`. `FlyCameraController` owns `m_process_event`. A
focus-lost event goes to `FlyCameraController::PauseEventProcessing` which sets
`m_process_event = false` — but never calls `m_camera->OnFocusLost()`. Keys stay stuck.
`SceneViewportUIComponent` calls `PauseEventProcessing` but never `OnFocusLost`. The camera
flies in a direction forever after the user tabs away.

**Mouse deltas are in physical pixels on HiDPI.**
`FlyCameraController::OnMouseButtonMoved` passes `e.GetXOffset() / e.GetYOffset()` (GLFW
logical-pixel deltas, scale 1.0 on Retina) to `FlyCamera::OnMouseMove`, which divides by
`m_viewportWidth` (set from `SwapchainImageWidth` = physical pixels, scale 2.0 on Retina).
All mouse sensitivity is halved on every Apple Silicon Mac.

**Scroll-toward-cursor ray uses absolute screen coordinates.**
`FlyCameraController::OnMouseButtonWheelMoved` passes raw `IDevice::As<Mouse>()->GetMousePosition()`
to `FlyCamera::OnMouseScroll` → `GetRayFromScreen`. When the viewport panel is not at (0,0),
the ray direction is wrong.

**`EditorCameraController::Initialize` never sets `SceneRaycast` or `OnGetSelectionBounds`.**
Adaptive speed falls back to height-based estimation. F-to-frame uses world origin.

**`FlyCameraController` does not expose `SetViewport` on `ICameraController`.**
`SceneViewportUIComponent` casts to `EditorCameraControllerPtr` to call `SetViewport`.

**`CameraSetting::FastMoveSpeed` is dead** — never used.

---

## 2. Design Goals

1. Single source of truth for all input state — `FlyCameraInput`, one owner.
2. Controller reads from `InputManager` — no `IMouseEventCallback`/`IKeyboardEventCallback`.
3. HiDPI-correct — all internal math in logical pixels.
4. Viewport origin passed through scroll path so ray unprojection is always correct.
5. `ICameraController` exposes `SetViewport` and `SetViewportOrigin` — no cast needed.
6. `FlyCameraHooks` wired at construction time.
7. Explicit state machine: `Free`, `Orbit`, `Pan`, `Animating`.

---

## 3. Input State — `FlyCameraInput`

```cpp
// ZEngine/Rendering/Cameras/FlyCameraInput.h
#pragma once

namespace ZEngine::Rendering::Cameras
{
    enum class FlyCameraState : uint8_t
    {
        Free,
        Orbit,
        Pan,
        Animating,
    };

    struct FlyCameraInput
    {
        bool  RightDown      = false;
        bool  MiddleDown     = false;
        bool  LeftDown       = false;
        bool  AltDown        = false;
        bool  ShiftDown      = false;
        bool  CtrlDown       = false;
        bool  Keys[512]      = {};
        float MouseDeltaX    = 0.0f;
        float MouseDeltaY    = 0.0f;
        float ScrollDelta    = 0.0f;
        float MouseViewportX = 0.0f;
        float MouseViewportY = 0.0f;

        void FlushDeltas()
        {
            MouseDeltaX  = 0.0f;
            MouseDeltaY  = 0.0f;
            ScrollDelta  = 0.0f;
        }

        void Reset()
        {
            *this = FlyCameraInput{};
        }
    };
}
```

---

## 4. Camera State Machine

```
          ┌──────────────────────────────────────────┐
          │               Animating                   │
          │  Ignores all input. On done → prev state. │
          └───────────────────┬──────────────────────┘
                              │ timer >= duration
                              ▼
┌──────────┐   Alt+LMB   ┌──────────┐   RMB up    ┌──────────┐
│   Free   │ ──────────► │  Orbit   │ ──────────► │   Free   │
│ RMB look │             │ tumble   │              │          │
│ WASDQE   │ ◄────────── │ zoom     │              │          │
│ scroll   │  Alt up     └──────────┘              └──────────┘
└──────────┘
      │ MMB down              │ MMB down
      ▼                       ▼
  ┌──────────┐           ┌──────────┐
  │ Pan(Free)│           │Pan(Orbit)│
  └──────────┘           └──────────┘
```

---

## 5. `FlyCamera` — Full Redesign

### 5.1 Header

```cpp
// ZEngine/Rendering/Cameras/FlyCamera.h
#pragma once
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Quaternion.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Cameras/FlyCameraInput.h>
#include <functional>

namespace ZEngine::Rendering::Cameras
{
    struct FlyCameraHooks
    {
        std::function<float(Core::Maths::Vec3f, Core::Maths::Vec3f, float)> Raycast;
        std::function<std::pair<Core::Maths::Vec3f, float>()>               GetSelectionBounds;
    };

    struct FlyCamera : public Camera
    {
        FlyCameraInput Input  = {};
        FlyCameraHooks Hooks  = {};
        FlyCameraState State  = FlyCameraState::Free;

        FlyCamera() = default;
        explicit FlyCamera(float aspectRatio, const CameraSetting& settings);
        virtual ~FlyCamera() = default;

        void OnUpdate(float dt);
        void SetViewportSize(float logicalW, float logicalH);
        void SetPosition(Core::Maths::Vec3f position);
        void SetOrientation(float pitchDeg, float yawDeg);

        void FocusOn(Core::Maths::Vec3f center, float radius);
        void FocusOn(Core::Maths::Vec3f point);
        void FocusOn(Core::Maths::Vec3f aabbMin, Core::Maths::Vec3f aabbMax);

        void SaveBookmark(int slot);
        void RecallBookmark(int slot);

        struct Ray { Core::Maths::Vec3f Origin; Core::Maths::Vec3f Direction; };
        Ray GetRayFromViewport(float viewportX, float viewportY) const;

        virtual Core::Maths::Vec3f             GetPosition()    const override;
        virtual Core::Maths::Vec3f             GetForward()     const override;
        virtual Core::Maths::Vec3f             GetUp()          const override;
        virtual Core::Maths::Vec3f             GetRight()       const override;
        Core::Maths::Quaternion<float>         GetOrientation() const;

    private:
        void UpdateFree(float dt);
        void UpdateOrbit(float dt);
        void UpdatePan(float dt);
        void UpdateAnimation(float dt);
        void RecalculateView();
        void RecalculateProjection();
        Core::Maths::Vec3f KeyboardMoveDir() const;
        float              AdaptiveSpeed()   const;
        float              OrbitCollide(float desired) const;

        Core::Maths::Vec3f m_targetPos       = {0.0f, 5.0f, 10.0f};
        float              m_targetPitch     = 0.0f;
        float              m_targetYaw       = 0.0f;
        float              m_logicalW        = 1280.0f;
        float              m_logicalH        = 720.0f;

        Core::Maths::Vec3f m_orbitPivot      = {};
        float              m_orbitDist       = 10.0f;
        float              m_targetOrbitDist = 10.0f;

        FlyCameraState     m_stateBeforePan  = FlyCameraState::Free;
        FlyCameraState     m_stateBeforeAnim = FlyCameraState::Free;

        Core::Maths::Vec3f m_animStartPos    = {};
        Core::Maths::Vec3f m_animEndPos      = {};
        float              m_animStartPitch  = 0.0f;
        float              m_animStartYaw    = 0.0f;
        float              m_animEndPitch    = 0.0f;
        float              m_animEndYaw      = 0.0f;
        float              m_animTimer       = 0.0f;
        float              m_animDuration    = 0.25f;

        bool               m_projDirty       = true;
        bool               m_viewDirty       = true;

        struct BookmarkSlot {
            bool               Valid = false;
            Core::Maths::Vec3f Pos   = {};
            float              Pitch = 0.0f;
            float              Yaw   = 0.0f;
        };
        BookmarkSlot m_bookmarks[9] = {};
    };
    ZDEFINE_PTR(FlyCamera);
}
```

---

## 6. `FlyCameraController` — Full Redesign

### 6.1 Integration with `InputManager`

`FlyCameraController` does NOT implement `IMouseEventCallback` or `IKeyboardEventCallback`.
All input state is read from `InputManager` once per frame in `Update`, written into
`camera->Input`, then `camera->OnUpdate` is called.

Camera actions registered with `InputManager` during `Initialize`:

| Action name | Type | Default binding |
|---|---|---|
| `"CameraForward"` | Axis1D | W (+1), S (-1) |
| `"CameraRight"` | Axis1D | D (+1), A (-1) |
| `"CameraUp"` | Axis1D | E (+1), Q (-1) |
| `"CameraScroll"` | Axis1D | scroll wheel Y |
| `"CameraRMB"` | Button | mouse right |
| `"CameraMMB"` | Button | mouse middle |
| `"CameraLMB"` | Button | mouse left |
| `"CameraAlt"` | Button | Left Alt |
| `"CameraShift"` | Button | Left Shift |
| `"CameraCtrl"` | Button | Left Ctrl |
| `"CameraFocus"` | Button | F |
| `"CameraBookmark0"`...`"CameraBookmark8"` | Button | Keys 1–9 |

Mouse delta from `InputManager::GetMouseDelta()`. Mouse position for scroll-toward-cursor
from `InputManager::GetMousePosition()` minus stored viewport origin.

### 6.2 Header

```cpp
// ZEngine/Controllers/FlyCameraController.h
#pragma once
#include <ZEngine/Controllers/ICameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>

namespace ZEngine::Controllers
{
    struct FlyCameraController : public ICameraController
    {
        FlyCameraController()          = default;
        virtual ~FlyCameraController() = default;

        void Initialize(Input::InputManager* input_manager,
                        Core::Memory::ArenaAllocator* arena);

        void                          Update(Core::TimeStep dt)              override;
        bool                          OnEvent(Core::CoreEvent&)              override;
        Rendering::Cameras::CameraPtr GetCamera()                      const override;
        Core::Maths::Vec3f            GetPosition()                    const override;
        void                          SetPosition(const Core::Maths::Vec3f&) override;
        void                          SetViewport(float logicalW, float logicalH) override;
        void                          SetViewportOrigin(float x, float y)    override;
        void                          ResumeEventProcessing()                override;
        void                          PauseEventProcessing()                 override;

    protected:
        Input::InputManager*             m_input           = nullptr;
        PaddedAtomic<bool>               m_active          = {.value = false};
        float                            m_viewportOriginX = 0.0f;
        float                            m_viewportOriginY = 0.0f;
        Rendering::Cameras::FlyCameraPtr m_camera          = nullptr;

        uint32_t m_slot_forward     = 0;
        uint32_t m_slot_right       = 0;
        uint32_t m_slot_up          = 0;
        uint32_t m_slot_scroll      = 0;
        uint32_t m_slot_rmb         = 0;
        uint32_t m_slot_mmb         = 0;
        uint32_t m_slot_lmb         = 0;
        uint32_t m_slot_alt         = 0;
        uint32_t m_slot_shift       = 0;
        uint32_t m_slot_ctrl        = 0;
        uint32_t m_slot_focus       = 0;
        uint32_t m_slot_bookmark[9] = {};
    };
    ZDEFINE_PTR(FlyCameraController);
}
```

### 6.3 `Update`

```cpp
void FlyCameraController::Update(Core::TimeStep dt)
{
    if (m_active.value.load(std::memory_order_acquire))
    {
        auto& inp = m_camera->Input;

        inp.Keys[GLFW_KEY_W] = m_input->GetAxis(m_slot_forward)  >  0.5f;
        inp.Keys[GLFW_KEY_S] = m_input->GetAxis(m_slot_forward)  < -0.5f;
        inp.Keys[GLFW_KEY_D] = m_input->GetAxis(m_slot_right)    >  0.5f;
        inp.Keys[GLFW_KEY_A] = m_input->GetAxis(m_slot_right)    < -0.5f;
        inp.Keys[GLFW_KEY_E] = m_input->GetAxis(m_slot_up)       >  0.5f;
        inp.Keys[GLFW_KEY_Q] = m_input->GetAxis(m_slot_up)       < -0.5f;

        inp.RightDown  = m_input->GetButton(m_slot_rmb).Held;
        inp.MiddleDown = m_input->GetButton(m_slot_mmb).Held;
        inp.LeftDown   = m_input->GetButton(m_slot_lmb).Held;
        inp.AltDown    = m_input->GetButton(m_slot_alt).Held;
        inp.ShiftDown  = m_input->GetButton(m_slot_shift).Held;
        inp.CtrlDown   = m_input->GetButton(m_slot_ctrl).Held;

        inp.Keys[GLFW_KEY_F] = m_input->GetButton(m_slot_focus).JustUp;
        for (int i = 0; i < 9; ++i)
            inp.Keys[GLFW_KEY_1 + i] = m_input->GetButton(m_slot_bookmark[i]).JustUp;

        auto delta        = m_input->GetMouseDelta();
        inp.MouseDeltaX   = delta.x;
        inp.MouseDeltaY   = delta.y;
        inp.ScrollDelta   = m_input->GetAxis(m_slot_scroll);

        auto pos          = m_input->GetMousePosition();
        inp.MouseViewportX = pos.x - m_viewportOriginX;
        inp.MouseViewportY = pos.y - m_viewportOriginY;
    }

    m_camera->OnUpdate(dt.GetSeconds());
}
```

### 6.4 `PauseEventProcessing`

```cpp
void FlyCameraController::PauseEventProcessing()
{
    m_active.value.store(false, std::memory_order_release);
    m_camera->Input.Reset();
}
```

---

## 7. `EditorCameraController` — Full Redesign

```cpp
// Tetragrama/Controllers/EditorCameraController.h
#pragma once
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Input/InputManager.h>

namespace Tetragrama::Controllers
{
    struct EditorCameraController : public ZEngine::Controllers::FlyCameraController
    {
        EditorCameraController()          = default;
        virtual ~EditorCameraController() = default;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena,
                        ZEngine::Windows::CoreWindow*          window,
                        ZEngine::Input::InputManager*          input_manager);
    };
    ZDEFINE_PTR(EditorCameraController);
}
```

`Initialize` constructs `FlyCamera` with explicit `CameraSetting`, calls
`FlyCameraController::Initialize(input_manager, arena)`, and wires default hooks.

After scene load, `HierarchyViewUIComponent` replaces the default hooks with real
raycasting and selection bounds.

---

## 8. `ICameraController` — Changes

Add to the pure virtual interface:

```cpp
virtual void SetViewport(float logicalW, float logicalH) = 0;
virtual void SetViewportOrigin(float x, float y)         = 0;
virtual void ResumeEventProcessing()                     = 0;
virtual void PauseEventProcessing()                      = 0;
```

Remove `IMouseEventCallback`/`IKeyboardEventCallback` from `FlyCameraController` inheritance.

---

## 9. HiDPI Coordinate Contract

All camera internals use logical pixels:
- `m_logicalW`, `m_logicalH` — set from ImGui content region
- `Input.MouseDeltaX/Y` — from `InputManager::GetMouseDelta()` (GLFW logical)
- `Input.MouseViewportX/Y` — `InputManager::GetMousePosition()` minus viewport origin

Physical pixels only at Vulkan allocation boundary (`SwapchainImageWidth`).

---

## 10. `OnUpdate` Internal Flow

```
dt = min(dt, 0.1f)

// Pan transitions (checked every frame regardless of current state)
if Input.MiddleDown && State != Pan:
    m_stateBeforePan = State; State = Pan
if !Input.MiddleDown && State == Pan:
    State = m_stateBeforePan

// Orbit entry/exit
if Input.AltDown && Input.LeftDown && State == Free:
    EnterOrbit()
if !Input.AltDown && State == Orbit && !Input.LeftDown && !Input.RightDown:
    ExitOrbit()

switch State:
    Animating → UpdateAnimation(dt)
    Pan       → UpdatePan(dt)
    Orbit     → UpdateOrbit(dt)
    Free      → UpdateFree(dt)

// F key (JustUp pre-filled by controller)
if Input.Keys[KEY_F]:
    [center, radius] = Hooks.GetSelectionBounds()
    FocusOn(center, radius)
    Input.Keys[KEY_F] = false

// Bookmarks
for i in 0..8:
    if Input.Keys[KEY_1+i]:
        if CtrlDown: SaveBookmark(i)
        else:        RecallBookmark(i)
        Input.Keys[KEY_1+i] = false

if m_projDirty: RecalculateProjection()
if m_viewDirty: RecalculateView()

Input.FlushDeltas()
```

`UpdateFree`:
- RMB held: accumulate WASDQE movement, accumulate yaw/pitch from mouse delta
- Scroll: zoom toward cursor via `GetRayFromViewport(MouseViewportX, MouseViewportY)`
- Smooth position and angles with `SmoothT(SmoothingFactor, dt)`

`UpdateOrbit`:
- Alt+(LMB or RMB): tumble yaw/pitch from mouse delta
- Scroll: zoom orbit distance
- Smooth and recompute `Position = m_orbitPivot - GetForward() * OrbitCollide(dist)`

`UpdatePan`:
- Compute focal-plane dimensions from FOV + focal distance
- `pan = right * (-dx/logicalW * planeW) + screenUp * (dy/logicalH * planeH)`
- Apply to `m_targetPos` and `m_orbitPivot`

---

## 11. `CameraSetting` Cleanup

Remove `FastMoveSpeed` and `MoveSpeed` — both replaced by `AdaptiveSpeed()` with
`MinMoveSpeed`/`MaxMoveSpeed`/`FastSpeedMultiplier`.

---

## 12. File Layout

```
ZEngine/ZEngine/Rendering/Cameras/
├── FlyCameraInput.h   — NEW: FlyCameraInput, FlyCameraState
├── FlyCamera.h/.cpp   — redesigned
└── Camera.h           — CameraSetting: remove FastMoveSpeed, MoveSpeed

ZEngine/ZEngine/Controllers/
├── ICameraController.h        — add SetViewport, SetViewportOrigin,
│                                ResumeEventProcessing, PauseEventProcessing
├── FlyCameraController.h/.cpp — reads InputManager, no event callbacks

Tetragrama/Controllers/
└── EditorCameraController.h/.cpp — Initialize with InputManager*
```

---

## 13. Deliverables Checklist

- [ ] `FlyCameraInput.h` — `FlyCameraInput` with `Reset()`/`FlushDeltas()`; `FlyCameraState` enum
- [ ] `FlyCamera.h/.cpp` — `Input`/`Hooks`/`State` public; single `OnUpdate` entry; explicit state machine; `Input.FlushDeltas()` at end; all mouse math divided by `m_logicalW/H`; `AdaptiveSpeed` uses `Hooks.Raycast`
- [ ] `ICameraController.h` — add 4 pure virtuals; `FlyCameraController` no longer inherits event callbacks
- [ ] `FlyCameraController.h/.cpp` — `Initialize(InputManager*, ArenaAllocator*)`; `Update` fills `FlyCameraInput` from `InputManager`; `PauseEventProcessing` calls `Input.Reset()`; `OnEvent` is no-op
- [ ] `EditorCameraController.h/.cpp` — `Initialize` gains `InputManager*`; explicit `CameraSetting`; hooks wired with defaults
- [ ] `SceneViewportUIComponent.cpp` — `SetViewportOrigin` every frame; `SetViewport` on resize; no cast to concrete controller
- [ ] `CameraSetting` — remove `FastMoveSpeed` and `MoveSpeed`
- [ ] Tests: state machine transitions; `PauseEventProcessing` resets input; `Update` fills `FlyCameraInput` from mocked `InputManager`
