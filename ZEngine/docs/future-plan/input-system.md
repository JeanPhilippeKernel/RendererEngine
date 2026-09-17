# ZEngine — Input System

**Status:** Keyboard, mouse-button, scroll, cursor, focus, and action-frame
sampling are implemented. Gamepad evaluation, binding persistence, fixed-step
input latching, and rollback integration are not.

## Current API and behavior

`InputManager` is allocated from the engine input arena and is owned through
`EngineContext::InputManager`. It supports up to `kMaxActions == 64`
runtime action slots. An action has a process-lifetime FNV-32 name hash, one
of `Button`, `Axis1D`, or `Axis2D` types, and up to four bindings.

The public registration and binding methods are:

```cpp
uint32_t RegisterAction(const char* name, InputActionType type);
void BindKey(uint32_t slot, int glfw_key, float scale = 1.0f);
void BindMouseButton(uint32_t slot, int glfw_mouse_button, float scale = 1.0f);
void BindScrollAxis(uint32_t slot, float scale = 1.0f);
void Poll(GLFWwindow* window);
```

The `const char*` argument is the current API. New engine-owned string APIs
should use `cstring` where the type is available, but this declaration should
not be silently documented as already changed.

`Poll()` copies the previous `InputFrame`, increments its frame number,
samples the current GLFW keyboard/mouse state, drains callback-accumulated
scroll and cursor movement, and derives `Held`, `JustDown`, and `JustUp`.
Button actions OR keyboard and mouse-button bindings. Axis1D chooses the
largest absolute keyboard, mouse-button, or scroll value after scale. Every
Axis2D action currently receives the accumulated mouse delta; per-action
Axis2D bindings are not evaluated.

Scroll and cursor motion come from callbacks accumulated between polls:
`AccumulateScroll`, `AccumulateCursorPosition`, and `ResetMouseDelta`.
`SetWindowFocused(false)` clears sampled input and pending motion so a control
cannot remain latched after focus loss. Raw mouse position, per-poll mouse
delta, and scroll delta are available through accessors.

`InputBinding::Device::Gamepad`, `Deadzone`, and `GamepadIdx` exist in
the data type, but the manager currently has no gamepad binding method and
`Poll()` does not evaluate gamepad input. Binding save/load functions and an
`InputSystem` ECS system do not exist. Action hashes/slots are therefore
runtime identifiers, not durable input-schema keys.

## Current loop position

`GameApplication::Update()` calls `InputManager::Poll()` once per
presentation/main-loop iteration, after the fixed `WorldTick` work in
`Engine::MainThreadRun`. Camera and application update code can use its
current values in that iteration. The fixed simulation does not currently
receive a latched input frame. See `game-loop.md` for the exact ordering.

ZUI may consume pointer/key input for hover, editing, and future viewport
gizmo capture. Editor tools must respect ZUI capture before interpreting a
pointer press as camera, selection, or gizmo input; action sampling alone is
not an editor interaction-routing policy.

## Production completion criteria

1. Define an input context/ownership layer: ZUI capture first, then viewport
   tools, then camera/game actions. Focus loss, window transitions, and cursor
   capture must cancel active tool gestures deterministically.
2. Latch an immutable `InputFrame` at the start of every fixed step. Define
   the zero-step and multi-step policies, and replay stored frames during
   rollback rather than re-polling the platform.
3. Implement and test gamepad bindings (including deadzone and disconnect
   semantics), or remove the currently unused gamepad fields from the public
   data model.
4. Persist bindings with versioned, named action identifiers and validation.
   Never persist registration slots or a raw process-local hash as the only
   durable identity.
5. Add input tests for focus loss, cursor-mode transitions, callback
   accumulation, UI capture, rebinding, and fixed-step replay.
