# ZEngine — Input System

**Priority:** P0 — Every game needs input; networking rollback requires deterministic, serializable input frames
**Status:** Implemented
**Depends on:** `actor-ecs-architecture.md`, `game-loop.md`, `networking.md` (InputFrame)
**Modifies:** `GameWindow.h`, `Engine.h` (EngineContext)

---

## 1. Design Philosophy

Input is processed exactly once per frame on the main thread, before `WorldTick::Tick` runs. Game code never polls raw GLFW state directly. All input is mediated through named actions registered with `InputManager`.

An action is an abstract, named trigger — `"MoveForward"`, `"Jump"`, `"LookX"`. Actions are identified at runtime by a 32-bit slot index assigned at registration. The slot index is stable for the lifetime of the process; it is the handle game code and ECS systems pass around. The human-readable name is only needed at registration and serialization time.

The same action can be bound to multiple physical inputs simultaneously. `"MoveForward"` can be bound to the `W` key and to the left stick Y axis of a gamepad; whichever has a larger magnitude wins, or they are summed for axis types.

Bindings are serializable to `settings.zsav` so players can rebind controls without recompiling. The serialization format stores action names (not slots, which are runtime-only) and binding descriptors.

`InputManager` produces one `InputFrame` per fixed step. The frame is a flat, POD-compatible snapshot of every registered action's state at that step. It has no pointers, no heap allocation, and no `std::function`. This makes it trivially serializable for networking rollback.

Rules that follow from this philosophy:
- No game system calls `glfwGetKey` or `glfwGetMouseButton` directly.
- No callbacks from GLFW land directly in gameplay code. `GameWindow` translates GLFW callbacks into raw state that `InputManager::Poll` samples.
- Mouse delta and raw screen-space position are available outside the action map as utility accessors, since they are needed by the camera and UI systems unconditionally.

---

## 2. Action Map

An action map is the set of `InputAction` records registered before gameplay starts, typically during engine initialization or level load.

```cpp
enum class InputActionType : uint8_t {
    Button,   // pressed / released / held
    Axis1D,   // single float [-1, 1], e.g. trigger squeeze, horizontal strafe
    Axis2D,   // Vec2f, e.g. analog stick, mouse delta
};

struct InputBinding {
    enum class Device : uint8_t { Keyboard, Mouse, Gamepad };

    Device   Source     = Device::Keyboard;
    uint32_t Code       = 0;       // GLFW key/button code, gamepad button index, or axis index
    float    Scale      = 1.f;     // multiply axis value by this; use -1.f to invert
    float    Deadzone   = 0.1f;    // axis only; values with magnitude below this map to 0
};

struct InputAction {
    uint32_t         NameHash    = 0;     // FNV-32 of the action name string
    InputActionType  Type        = InputActionType::Button;
    InputBinding     Bindings[4] = {};    // up to 4 bindings per action
    uint8_t          BindingCount = 0;    // Hard limit is 4 bindings per action. Additional bindings are rejected with ZENGINE_VALIDATE_ASSERT, not silently dropped.
};
```

**Name hashing.** The name hash (FNV-32) is computed once at registration and stored. It is used only for serialization lookups; all hot-path code uses the slot index.

**Multiple bindings.** For Button actions the result is the logical OR of all bound buttons. For Axis1D actions the binding with the largest absolute value wins. For Axis2D actions the binding with the largest magnitude wins. This prevents cancellation when both keyboard and gamepad are active simultaneously.

**Scale.** A `Scale` of `-1.f` inverts an axis. Binding `"MoveBackward"` separately with inverted scale is unnecessary; one `"MoveForward"` action can have `S` key bound with `Scale = -1.f`.

**Deadzone.** Applied per-binding for gamepad axes before the multi-binding selection. Keyboard and mouse button bindings ignore the deadzone field.

---

## 3. InputState

`InputFrame` is the single authoritative snapshot of all action states for one fixed step. It is a flat POD struct with no heap allocation.

```cpp
struct InputButtonState {
    bool Held     = false;  // true every frame the button is held
    bool JustDown = false;  // true only on the first frame the button is pressed
    bool JustUp   = false;  // true only on the first frame the button is released
};

struct InputAxisState {
    float Value   = 0.f;  // Axis1D result after deadzone and scale
    Vec2f Value2D = {};   // Axis2D result after deadzone and scale
};

struct InputFrame {
    uint32_t         FrameNumber  = 0;
    InputButtonState ButtonStates[64] = {};  // indexed by action slot
    InputAxisState   AxisStates[64]   = {};  // indexed by action slot
    uint32_t         ActionCount  = 0;   // must not exceed 64 (size of ButtonStates/AxisStates arrays)
};

static_assert(sizeof(InputFrame::ButtonStates) / sizeof(InputButtonState) == 64,
    "InputFrame array size must match the max action count");
```

`InputFrame` is allocated in the arena. `InputManager` maintains two frames: the current frame being filled by `Poll` and the previous frame used to derive `JustDown`/`JustUp` via comparison.

`JustDown` is `true` when the button is `Held` this frame and was not `Held` last frame. `JustUp` is `true` when the button was `Held` last frame and is not `Held` this frame. This logic is O(ActionCount) and runs entirely inside `InputManager::Poll`.

`InputFrame` is the type that the networking rollback module stores in its ring buffer (see Section 7). It must remain POD-compatible and serializable to a flat byte buffer.

---

## 4. InputManager

`InputManager` is a plain struct with no virtual functions, no inheritance, and no `std::function`. It owns its storage via an `ArenaAllocator` pointer provided at initialization.

```cpp
namespace ZEngine::Input {

struct InputManager {
    // Lifecycle
    void Initialize(Core::Memory::ArenaAllocator* arena, uint32_t max_actions = 64);
    void Dispose();

    // Registration — call before first Poll
    // Returns the slot index [0, max_actions). Asserts if max_actions is exceeded.
    // max_actions is set in Initialize() (default 64).
    // InputFrame arrays are sized to max_actions at compile time.
    // Changing max_actions requires updating InputTypes.h and recompiling.
    [[nodiscard]] uint32_t RegisterAction(cstring name, InputActionType type);

    // Binding — may be called at any time; takes effect on next Poll
    // In InputManager::BindKey (and all Bind* variants):
    //   ZENGINE_VALIDATE_ASSERT(m_actions[slot].BindingCount < 4,
    //       "InputManager: action slot %u already has 4 bindings. "
    //       "Increase MAX_BINDINGS_PER_ACTION if more are needed.", slot);
    void BindKey(uint32_t slot, int glfw_key, float scale = 1.f);
    void BindMouseButton(uint32_t slot, int glfw_mouse_button, float scale = 1.f);
    void BindGamepadButton(uint32_t slot, int glfw_gamepad_button, int gamepad_index = 0);
    void BindGamepadAxis(
        uint32_t slot,
        int      glfw_gamepad_axis,
        float    scale     = 1.f,
        float    deadzone  = 0.1f,
        int      gamepad_index = 0
    );

    // Bind the mouse scroll wheel Y axis to an Axis1D action slot.
    // `scale` = 1.f means scroll up is positive. Use -1.f to invert.
    // Scroll accumulates between Poll calls and is reset to 0 at the start of each Poll.
    void BindScrollAxis(uint32_t slot, float scale = 1.f);

    // THREAD SAFETY: InputManager is NOT thread-safe.
    // Poll() and all Get* accessors must be called from the main thread only.
    // Do not call GetButton()/GetAxis()/GetCurrentFrame() from physics, audio,
    // or rendering threads. If another thread needs input state, copy the
    // InputFrame after Poll() completes and pass the copy.

    // Per-frame update — call once per fixed step, before WorldTick
    void Poll(GLFWwindow* window);

    // Query — valid after Poll returns; undefined before first Poll
    const InputButtonState& GetButton(uint32_t slot) const;
    float                   GetAxis(uint32_t slot)   const;
    Vec2f                   GetAxis2D(uint32_t slot) const;

    // Raw frame access — used by networking rollback
    const InputFrame& GetCurrentFrame() const;

    // Serialization
    // Bindings are serialized to GameSaveData under the key "input_bindings" as a
    // binary blob: [uint8_t version=1][uint8_t action_count][action_count x
    // (uint32_t NameHash + uint8_t BindingCount + BindingCount x InputBinding)].
    // The format is versioned; if InputBinding fields change, increment version and
    // add a migration path in LoadBindings.
    void SaveBindings(Persistence::GameSaveData& out) const;
    void LoadBindings(const Persistence::GameSaveData& in);

private:
    Core::Memory::ArenaAllocator* m_arena        = nullptr;
    InputAction*                  m_actions       = nullptr;
    uint32_t                      m_max_actions   = 0;
    uint32_t                      m_action_count  = 0;

    InputFrame                    m_current_frame = {};
    InputFrame                    m_prev_frame    = {};

    // Mouse state carried between Poll calls
    Vec2f                         m_last_mouse_pos    = {};
    Vec2f                         m_mouse_delta       = {};
    Vec2f                         m_mouse_pos         = {};

    // Scroll accumulator — written by glfwSetScrollCallback, drained by Poll.
    float                         m_scroll_accumulator = 0.0f;
    float                         m_current_scroll     = 0.0f;
};

} // namespace ZEngine::Input
```

**Memory.** `Initialize` allocates `m_actions` from the arena as a flat array of `InputAction[max_actions]`. No further heap allocation occurs after initialization.

**Thread safety.** `Poll` must be called on the main thread. All `Get*` accessors are read-only and safe to call from the main thread after `Poll` returns. They are not safe to call concurrently with `Poll`.

**Slot stability.** Slots are assigned sequentially starting from 0 in registration order. Slot 0 is always the first registered action. This ordering must be stable across save/load; it is the game's responsibility to always register actions in the same order before loading saved bindings.

---

## 5. Gamepad Support

GLFW 3.3+ provides `glfwGetGamepadState(int jid, GLFWgamepadstate* state)` which maps hardware buttons and axes to a standard Xbox-style layout on all platforms (XInput on Windows, HID with SDL mapping on Linux/macOS).

`glfwGetGamepadState` returns `GLFW_TRUE` if the gamepad is connected and the state was filled. `InputManager::Poll` checks return values and treats a disconnected gamepad as all buttons released and all axes at zero. This means gameplay code does not need to handle disconnection explicitly — actions simply stop firing.

**Deadzone application.** Raw gamepad axis values have hardware noise near the center. The following normalization maps the dead zone to exactly zero and linearly rescales the remaining range to `[-1, 1]`:

```cpp
inline float ApplyDeadzone(float raw, float deadzone)
{
    if (Core::Maths::abs(raw) < deadzone) return 0.f;
    const float sign = raw > 0.f ? 1.f : -1.f;
    return sign * (Core::Maths::abs(raw) - deadzone) / (1.f - deadzone);
}
```

For Axis2D bindings (analog stick), deadzone is applied radially on the Vec2f magnitude rather than per-component, to avoid diagonal artifacts:

```cpp
inline Vec2f ApplyDeadzone2D(Vec2f raw, float deadzone)
{
    const float mag = Core::Maths::length(raw);
    if (mag < deadzone) return Vec2f{};
    return raw * ((mag - deadzone) / (mag * (1.f - deadzone)));
}
```

**Multi-gamepad.** Each `InputBinding` stores a `gamepad_index` (GLFW joystick ID, 0–15). Most games will only use gamepad 0. The binding API accepts the index explicitly so split-screen games can route different players' gamepads to different action maps.

---

## 6. Integration with the Game Loop

`InputManager::Poll` is called once per fixed step on the main thread, after `GameWindow::PollEvents` and before `WorldTick::Tick`. This ordering ensures that all GLFW window events (including key/button state changes accumulated since the previous step) are flushed before `Poll` samples them.

```cpp
// Pseudocode — MainThreadRun fixed-step body
void Engine::MainThreadRun()
{
    while (!m_should_quit)
    {
        // 1. Collect OS/window events into GLFW state
        m_window->PollEvents();

        // 2. Sample GLFW state into InputFrame for this tick
        m_input_manager.Poll(m_window->GetGLFWWindow());

        // 3. Advance the world with the new input frame available
        m_world_tick.Tick(m_ecs_scene, m_delta_time);

        // 4. Submit render work
        m_render_graph.Execute();

        // 5. Networking rollback: store the frame after tick (see Section 7)
        m_rollback.EndFrame(m_input_manager.GetCurrentFrame());
    }
}
```

`InputManager` retains the previous frame internally. `Poll` must always be called exactly once per tick. Calling it zero or more than once per tick produces incorrect `JustDown`/`JustUp` state.

---

## 7. Integration with Networking Rollback

The rollback module maintains a ring buffer of `InputFrame` records — one per step — for both local and remote players. At the end of each tick the current frame is written into the ring buffer. When a misprediction is detected the simulation re-ticks from the divergence point, re-feeding the stored frames.

```cpp
// In rollback module — called at end of each tick
void RollbackModule::EndFrame(const InputFrame& local_input)
{
    // Write local input into the ring buffer at current_tick
    m_local_input_buffer[m_current_tick % k_rollback_buffer_size] = local_input;

    // Send local input to remote peer (serialized as flat byte buffer)
    m_network_session.SendInputFrame(local_input);

    // Advance tick counter
    ++m_current_tick;
}

// During rollback re-simulation
void RollbackModule::Resimulate(uint32_t from_tick, uint32_t to_tick)
{
    for (uint32_t tick = from_tick; tick < to_tick; ++tick)
    {
        const InputFrame& frame = m_local_input_buffer[tick % k_rollback_buffer_size];
        // Inject frame back into the ECS scene without re-polling GLFW
        m_world_tick.TickWithInputFrame(m_ecs_scene, frame, m_fixed_dt);
    }
}
```

`InputFrame` is suitable as a flat serialization target because it contains no pointers. `sizeof(InputFrame)` is deterministic and compile-time known. The network layer can send it as a raw byte span.

`WorldTick::TickWithInputFrame` is a variant of `WorldTick::Tick` that accepts an `InputFrame` directly instead of querying `InputManager`, used exclusively during rollback re-simulation.

---

## 8. ECS Integration

Entities that consume input declare which action slots they observe through `InputComponent`. This decouples specific gameplay systems from `InputManager` while keeping data flow explicit.

```cpp
struct InputComponent {
    uint32_t ActionSlots[8] = {};  // action slot indices this entity observes
    uint8_t  SlotCount      = 0;
};
```

`InputSystem` runs early in the tick wave, reads `InputComponent` alongside the current `InputFrame`, and writes results to downstream components such as `CharacterControllerComponent` or `VehicleInputComponent`.

```cpp
// System dependency masks (conceptual — actual masks are bit flags in SystemDeps)
struct InputSystemDeps {
    // Reads
    static constexpr SystemDeps::Mask ReadComponents  =
        SystemDeps::Component<InputComponent>     |
        SystemDeps::Resource<InputManager>;

    // Writes
    static constexpr SystemDeps::Mask WriteComponents =
        SystemDeps::Component<CharacterControllerComponent> |
        SystemDeps::Component<VehicleInputComponent>;
};
```

`InputSystem::Tick` iterates entities with `InputComponent`, queries each declared slot from `InputManager::GetCurrentFrame()`, and writes the results:

```cpp
void InputSystem::Tick(Scene& scene, const WorldTick& tick)
{
    const InputFrame& frame = m_input_manager.GetCurrentFrame();

    scene.ForEach<InputComponent, CharacterControllerComponent>(
        [&](EntityID, const InputComponent& ic, CharacterControllerComponent& cc)
        {
            // Example: slot 0 = MoveForward, slot 1 = MoveRight, slot 2 = Jump
            cc.DesiredVelocity.x = frame.AxisStates[ic.ActionSlots[1]].Value;
            cc.DesiredVelocity.z = frame.AxisStates[ic.ActionSlots[0]].Value;
            cc.JumpRequested     = frame.ButtonStates[ic.ActionSlots[2]].JustDown;
        }
    );
}
```

`InputSystem` must be scheduled in a wave that runs before any movement or physics systems that consume `CharacterControllerComponent::DesiredVelocity`.

---

## 9. Mouse Delta, Screen-Space Coordinates, and Scroll

Mouse delta, screen-space position, and scroll delta are available as direct accessors on `InputManager`, independent of the action map. They are always valid after the first `Poll` call.

```cpp
// Free-look camera rotation — call from FlyCameraController::Update
Vec2f InputManager::GetMouseDelta() const;

// UI hit testing, scroll-toward-cursor ray origin
Vec2f InputManager::GetMousePosition() const;

// Raw scroll wheel Y accumulated this frame — positive = scroll up.
// Reset to 0.f at the start of each Poll. Also available as a registered
// Axis1D action via BindScrollAxis for rebindable scroll.
float InputManager::GetScrollDelta() const;
```

`GetMouseDelta` returns the pixel-space delta in logical pixels (GLFW convention) from the previous frame to the current frame. The delta is computed inside `Poll` by comparing `glfwGetCursorPos` results between successive calls.

`GetMousePosition` returns the current cursor position in logical screen pixels, origin at top-left, consistent with GLFW's coordinate convention.

`GetScrollDelta` returns the raw scroll wheel Y accumulated since the last `Poll`. It is stored in `m_scroll_delta` and set via `glfwSetScrollCallback`. The callback accumulates values between `Poll` calls; `Poll` reads and resets the accumulator. Scroll registered as an Axis1D action via `BindScrollAxis` uses the same accumulator — binding and raw accessor return the same value.

Camera integration: `FlyCameraController` reads scroll via the registered `"CameraScroll"` Axis1D slot (bound with `BindScrollAxis`) and reads mouse delta via `GetMouseDelta()`. The camera never calls `glfwGetCursorPos` or `glfwSetScrollCallback` directly.

Both `GetMouseDelta` and `GetScrollDelta` may return zero before the first `Poll` call. Camera systems should clamp or filter the first frame to avoid a large initial jump.

Mouse capture (hiding the cursor and enabling unlimited movement) is managed by `GameWindow::SetCursorMode(CursorMode::Captured)`. `InputManager` does not manage cursor mode; it only reads position.

---

## 10. File Layout

```
ZEngine/Input/
    InputManager.h          // InputManager struct, InputFrame, InputAction, all public API
    InputManager.cpp        // Poll, FNV-32 hash, deadzone helpers, serialization
    InputTypes.h            // InputActionType, InputBinding, InputButtonState, InputAxisState,
                            // InputComponent (ECS component)
    InputSystem.h           // ECS system declaration
    InputSystem.cpp         // InputSystem::Tick implementation
```

`InputTypes.h` has no dependencies outside of `ZEngine/Core/Maths/` and `ZEngine/ECS/EntityID.h`. It can be included anywhere without pulling in GLFW headers. GLFW headers are confined to `InputManager.cpp`.

The `InputComponent` struct lives in `InputTypes.h` rather than a separate file because it is tightly coupled to the slot indexing scheme defined there.

`InputManager` is stored in `EngineContext` (engine-lifecycle.md §2):
```cpp
    Input::InputManager* InputManager = nullptr;
```

It is initialized in `Engine::Initialize()` between the Window and VFS steps,
carved from a dedicated `InputArena` in `EngineContext`.

Game code accesses it via the engine context or a helper:
```cpp
    auto& input = *g_engine_ctx->InputManager;
    input.GetButton(m_jump_slot).JustDown
```

---

## 11. Deliverables Checklist

**Implementation tasks:**

- [ ] `InputTypes.h` — all enums and POD structs (no dependencies on GLFW)
- [ ] `InputManager.h` — full struct declaration
- [ ] `InputManager.cpp` — `Initialize`, `Dispose`, `RegisterAction`, all `Bind*` variants
- [ ] `InputManager.cpp` — `Poll`: keyboard, mouse button, gamepad button, gamepad axis
- [ ] `InputManager.cpp` — `JustDown`/`JustUp` derivation via prev/current frame comparison
- [ ] `InputManager.cpp` — `ApplyDeadzone` and `ApplyDeadzone2D`
- [ ] `InputManager.cpp` — `GetMouseDelta`, `GetMousePosition`, `GetScrollDelta`
- [ ] `InputManager.cpp` — `BindScrollAxis`: registers a `glfwSetScrollCallback` (once, on first call), stores scale; `Poll` reads `m_scroll_accumulator`, multiplies by scale, stores in the bound action slot and in `m_current_scroll`, then resets accumulator to 0
- [ ] `InputManager.cpp` — `SaveBindings`/`LoadBindings` using `GameSaveData` API
- [ ] `InputSystem.h/.cpp` — ECS system with correct `SystemDeps` masks
- [ ] `Engine.h` — add `InputManager` to `EngineContext`
- [ ] `GameWindow.h` — expose `GetGLFWWindow()` if not already present
- [ ] `game-loop.cpp` — wire `Poll` call before `WorldTick::Tick`
- [ ] `RollbackModule` — add `WorldTick::TickWithInputFrame` variant and ring buffer wiring

**Test cases:**

- [ ] Register two actions, bind both to keyboard keys, poll with both held — verify `Held = true`, `JustDown = true` on first frame, `JustDown = false` on second frame
- [ ] Press and release a key over two frames — verify `JustDown` frame 1, `Held` frame 1, `JustUp` frame 2, neither on frame 3
- [ ] Bind `"MoveForward"` to `W` (scale 1) and `S` (scale -1); poll with `S` held — verify axis value is -1
- [ ] Gamepad axis binding with deadzone 0.1: input raw 0.05 produces 0.0; input raw 0.5 produces the normalized value
- [ ] Axis2D radial deadzone: stick at (0.05, 0.05), magnitude ~0.07, below deadzone 0.1 — both components zero
- [ ] `InputFrame` serializes to and deserializes from a flat byte buffer with no data loss
- [ ] `SaveBindings`/`LoadBindings` round-trip: register actions, bind, save, clear bindings, load — verify restored bindings produce the same `InputFrame` output
- [ ] `InputSystem` ECS test: entity with `InputComponent` slots [0, 1]; simulate a `Poll` with slot 0 held; verify `CharacterControllerComponent` is updated correctly
- [ ] Multi-gamepad: bind two actions to gamepad 0 and gamepad 1 respectively; mock `glfwGetGamepadState` for each; verify correct isolation
- [ ] Disconnected gamepad: `glfwGetGamepadState` returns `GLFW_FALSE`; verify all gamepad-bound actions read as released, no crash
