# ZEngine — Lua Scripting (v2)

**Priority:** Next-year plan — enables designers to write gameplay logic without C++ recompilation
**Status:** Design — extends scripting.md §7 (read that section first)
**Depends on:** `scripting.md` (C++ DLL hot-reload must be stable), `vfs-ticket4-filewatcher.md`, `actor-ecs-architecture.md`

---

## 1. When to Use Lua vs C++ DLL

This is a hard division. Both mechanisms exist; choosing the wrong one for a use case either wastes authoring time (C++ for trivial quest logic) or produces unacceptable frame cost (Lua for a physics callback invoked 10,000 times per tick).

| Criterion | C++ DLL | Lua Script |
|---|---|---|
| Call frequency | Any — no overhead per call | Less than ~1000 calls per frame total |
| Author | Programmer | Designer or programmer |
| Iteration speed | Slow (recompile, hot-reload DLL) | Fast (save .lua, instant hot-reload) |
| Access to engine internals | Full | Binding surface only |
| Physics callbacks | Yes | No |
| Rendering integration | Yes | No |
| Networking / protocol | Yes | No |
| Quest logic | Possible, but heavyweight | Yes |
| NPC dialogue and branching | Possible, but heavyweight | Yes |
| UI event handlers | Possible | Yes |
| Game rules (score, win conditions) | Possible | Yes |
| Cutscene sequences | Possible | Yes — coroutine model fits perfectly |
| Error isolation | Crash propagates | Lua error caught; entity script disabled |

The dividing line is roughly 1000 invocations per frame. Above that, the ~50ns per Lua FFI call begins to accumulate. Any system called every frame for every entity in a large world (100+ entities) should be a C++ system. Scripts are for per-entity logic called at human-scale frequency.

---

## 2. `LuaVM`

`LuaVM` is a singleton-ish class, one per application. It owns the `lua_State`, the loaded scripts table, and all binding registrations. It does not allocate internally with `new`; all growth uses `ArenaAllocator` for the script ref array.

```cpp
class LuaVM {
public:
    void     Initialize(ArenaAllocator* arena, IVFSContext* vfs);
    void     Shutdown();

    // Compile script at path, cache bytecode, return a stable handle.
    // Returns UINT32_MAX on compile failure; logs error to SCRIPT channel.
    uint32_t LoadScript(const VFSPath& path);

    // Release Lua registry reference for the given handle.
    void     UnloadScript(uint32_t handle);

    // Recompile script from VFS (triggered by file watcher).
    // Existing coroutine threads for this handle are invalidated; see §7.
    void     ReloadScript(uint32_t handle);

    // Call a named global function on the script identified by handle.
    // fn_name must exist in the script's environment; returns false on error.
    bool     CallGlobal(uint32_t handle, cstring fn_name, float dt);

    // Expose the main lua_State for binding registration at startup.
    lua_State* GetState() const;

private:
    lua_State*      m_L           = nullptr;
    Array<int>      m_script_refs;   // Lua registry refs indexed by handle
    IVFSContext*    m_vfs         = nullptr;
    ArenaAllocator* m_arena       = nullptr;
};
```

**Handle stability:** Handles are indices into `m_script_refs`. They are never reused within a session (the array grows monotonically). This means a `LuaScriptComponent` holding a `ScriptHandle` does not need to be updated when other scripts load or unload.

**Compilation:** `LoadScript` calls `luaL_loadbuffer` (or `luaL_loadfile` via VFS adapter). On success, the function is stored in the Lua registry via `luaL_ref`. The returned int is stored in `m_script_refs[handle]`.

**Error handling in `CallGlobal`:** Calls `lua_pcall`. On error, calls `lua_tostring(-1)`, logs to the SCRIPT log channel, pops the error object, and returns false. Does not propagate exceptions (there are none).

---

## 3. `LuaScriptComponent`

```cpp
struct LuaScriptComponent {
    uint32_t   ScriptHandle  = UINT32_MAX;  // index into LuaVM::m_script_refs
    lua_State* Thread        = nullptr;     // per-entity coroutine; created by LuaSystem on first tick
    bool       HasOnCreate   = false;       // set by LuaSystem after first call attempt
    bool       HasOnUpdate   = false;
    bool       HasOnDestroy  = false;
};
```

`Thread` is a Lua coroutine thread created from the main state via `lua_newthread`. It is owned by the Lua GC (the main state holds a reference in the registry). `LuaSystem` resumes the thread each tick by calling `lua_resume` instead of `lua_call`. This is what makes the coroutine model work (see §6).

`HasOn*` flags are set to false initially. On the first attempted call, `LuaSystem` checks whether the function exists in the script's environment and sets the flag accordingly. This avoids a `lua_getglobal` lookup every tick for scripts that do not implement optional callbacks.

---

## 4. `LuaSystem`

`LuaSystem` is a standard ECS system that iterates all entities with `LuaScriptComponent`.

**SystemDeps:**

```cpp
SystemDeps LuaSystem::GetDeps() {
    return SystemDeps{}
        .Reads<TransformComponent>()
        .Reads<PhysicsBodyComponent>()
        .Writes<LuaScriptComponent>()
        .After<PhysicsSystem>()
        .Before<RenderSystem>();
}
```

Lua scripts can call engine bindings that read transform and physics data. `LuaSystem` runs after `PhysicsSystem` (so physics state is current) and before `RenderSystem` (so any transform modifications are visible this frame).

**Tick loop:**

```cpp
void LuaSystem::Tick(WorldTick& tick) {
    float dt = tick.DeltaSeconds;

    tick.Scene.ForEach<LuaScriptComponent>([&](EntityID id, LuaScriptComponent& sc) {
        if (sc.ScriptHandle == UINT32_MAX) return;
        if (!sc.HasOnUpdate)               return;

        // Push entity ID into Lua global for bindings to read
        lua_pushinteger(m_vm->GetState(), (lua_Integer)id.Value);
        lua_setglobal(m_vm->GetState(), "_current_entity");

        int status = lua_resume(sc.Thread, nullptr, 1 /*nargs: dt*/, &m_nresults);

        if (status == LUA_YIELD) {
            // Script yielded — resume next tick; this is normal
        } else if (status == LUA_OK) {
            // Script's OnUpdate coroutine returned (not typical; reset for next tick)
            sc.Thread = lua_newthread(m_vm->GetState());
            // re-load OnUpdate function into the new thread
        } else {
            // Error: log and disable
            cstring err = lua_tostring(sc.Thread, -1);
            ZENGINE_LOG_ERROR(SCRIPT, "Lua error on entity %u: %s", id.Value, err);
            lua_pop(sc.Thread, 1);
            sc.HasOnUpdate = false;
        }
    });
}
```

`OnCreate` is called once when the component is first ticked (not in `Tick` — in a separate `OnEntityCreated` callback registered with `Scene`). `OnDestroy` is called in the scene entity destruction callback.

---

## 5. Engine Bindings

All bindings are registered at `LuaVM::Initialize` time via `lua_register` (or equivalent). Bindings are plain C functions with signature `int fn(lua_State* L)`. No sol2 in v1; sol2 is optional if the binding surface grows enough to warrant it.

### 5.1 Entity Bindings

```
entity_get_component(entity_id: integer, type_name: string) -> table | nil
```
Returns a Lua table populated from the component data. The table is a copy, not a reference; modifications to the table do not modify the ECS component. Use the matching setter bindings.

```
entity_add_component(entity_id: integer, type_name: string) -> boolean
entity_destroy(entity_id: integer)
```

### 5.2 Transform Bindings

```
transform_get_position(entity_id: integer) -> x: number, y: number, z: number
transform_set_position(entity_id: integer, x: number, y: number, z: number)
transform_get_rotation(entity_id: integer) -> x: number, y: number, z: number, w: number
transform_set_rotation(entity_id: integer, x: number, y: number, z: number, w: number)
transform_get_scale(entity_id: integer)    -> x: number, y: number, z: number
transform_set_scale(entity_id: integer, x: number, y: number, z: number)
```

Each binding looks up the `TransformComponent` on the entity, reads or writes the relevant field, and returns. The `set` bindings mark the transform dirty.

### 5.3 Physics Bindings

```
physics_raycast(ox, oy, oz, dx, dy, dz, max_dist)
    -> hit: boolean, nx: number, ny: number, nz: number, dist: number
```
Cast a ray from origin (ox,oy,oz) in direction (dx,dy,dz) up to max_dist world units. Returns hit status, the hit normal, and the distance to the hit point. Direction does not need to be normalized (the binding normalizes it internally).

```
physics_set_velocity(entity_id: integer, vx: number, vy: number, vz: number)
physics_get_velocity(entity_id: integer) -> vx: number, vy: number, vz: number
```

### 5.4 Audio Bindings

```
audio_play(entity_id: integer, clip_name: string)
audio_stop(entity_id: integer)
audio_set_volume(entity_id: integer, volume: number)
```

`clip_name` is a VFS path relative to the audio asset root. The binding resolves it through `IVFSContext` and delegates to `AudioSystem`.

### 5.5 Input Bindings

```
input_button_pressed(action_name: string) -> boolean
input_button_held(action_name: string)    -> boolean
input_axis(action_name: string)           -> number
```

`action_name` maps to the input action table defined in the project input config. These bindings read from the current-frame input snapshot (safe to call from any system that runs after `InputSystem`).

### 5.6 Log Binding

```
zengine_log(msg: string)
```

Routes the message to the GAME log channel with level INFO. Useful for debugging scripts without a full Lua debugger. In release builds the binding is a no-op (the log channel is compiled out).

---

## 6. Coroutine Model

Each entity with a `LuaScriptComponent` owns one `lua_State` coroutine thread. The thread is created by `lua_newthread` from the main `lua_State` and is not the main state.

On each tick, `LuaSystem` calls `lua_resume(sc.Thread, ...)` rather than `lua_call`. This distinction is fundamental:

- `lua_call` / `lua_pcall` call a function and return when the function returns. Scripts that need to spread work across frames cannot use this.
- `lua_resume` resumes execution at the last `yield` point. When the script calls `coroutine.yield()`, execution returns to `LuaSystem`. On the next tick, the script continues from the statement after `yield`.

This enables sequential scripted sequences without callbacks or state machines:

```lua
function OnUpdate(dt)
    -- Move toward target over 3 seconds
    for i = 1, 180 do
        local x, y, z = transform_get_position(_current_entity)
        transform_set_position(_current_entity, x + 0.05, y, z)
        coroutine.yield()  -- wait one frame
    end
    audio_play(_current_entity, "sounds/arrive.wav")
    -- Script ends; coroutine returns LUA_OK; LuaSystem resets thread for next cycle
end
```

This is the primary reason per-entity coroutine threads are used instead of simple `CallGlobal` calls.

---

## 7. Hot-Reload

The VFS file watcher (implemented in `vfs-ticket4-filewatcher.md`) monitors all `.lua` files loaded by `LuaVM`. On a `Modified` event for a file:

1. `LuaVM::ReloadScript(handle)` is called.
2. The script is recompiled from the VFS into a new Lua function object.
3. The registry ref for the handle is updated to point to the new function.
4. All entities with `LuaScriptComponent::ScriptHandle == handle` have their `Thread` reset to a new coroutine created from the reloaded function.

**State loss on reload:** The old coroutine thread is abandoned. Any Lua-side state stored in upvalues or local variables in the coroutine is lost. Entity component data (C++ side) is fully preserved — `TransformComponent`, `PhysicsBodyComponent`, etc. are unaffected. Only the Lua execution state resets.

This is a documented limitation. Scripts intended for hot-reload should store persistent state in the `Blackboard` (if a `BehaviorTreeComponent` is present) or in ECS components exposed via bindings, not in Lua local variables.

---

## 8. Security and Sandboxing

The following Lua standard library functions and modules are removed or replaced at `LuaVM::Initialize`:

| Removed / Replaced | Reason |
|---|---|
| `os.execute` | Would allow arbitrary shell execution |
| `os.exit` | Would terminate the engine process |
| `io.open`, `io.read`, `io.write` | Raw filesystem access bypasses VFS |
| `require` | Replaced with `zengine_require` (see below) |
| `load`, `loadstring` | Allows loading arbitrary untrusted bytecode |
| `loadfile`, `dofile` | Raw filesystem access |
| `debug.*` | Allows reading/writing arbitrary Lua state |

Modules left intact: `math`, `string`, `table`, `coroutine`.

`zengine_require(path: string)` is a VFS-aware replacement for `require`. It resolves the path through `IVFSContext`, checks a whitelist of allowed directories (the project's `scripts/` directory), and calls `LuaVM::LoadScript` on the resolved path. Circular dependency detection is via a `loaded` table maintained in Lua's registry (same approach as standard `require`).

Sandboxing is enforced in `LuaVM::Initialize` by setting `os`, `io`, `debug`, `load`, `loadstring`, `loadfile`, and `dofile` to `nil` in the global environment before any user scripts are loaded.

---

## 9. Performance Notes

**Call overhead:** A round-trip Lua-to-C binding call (push args, call C function, read return values) costs approximately 50–100ns on a modern CPU. For `LuaSystem` iterating 1000 entities each calling one binding, that is 50–100µs of pure Lua dispatch overhead per frame. This is acceptable at 60fps (16.7ms budget). It is not acceptable at 10,000 entities.

**Per-entity coroutine cost:** Each `lua_newthread` allocation is small (a few hundred bytes). 1000 entity threads = roughly 300KB Lua heap. This is within budget.

**Profiling:** Wrap the `LuaSystem::Tick` body:

```cpp
ZENGINE_PROFILE_SCOPE("LuaSystem");
```

Additionally, per-entity Lua error state should be tracked. An entity whose `HasOnUpdate` has been set to false after a runtime error adds zero CPU cost to subsequent ticks.

**Do not call Lua from inner loops.** Any per-vertex, per-fragment, or per-physics-contact path must remain in C++. Lua is for per-entity, per-event, human-frequency logic.

---

## 10. File Layout

```
ZEngine/Scripting/Lua/
    LuaVM.h
    LuaVM.cpp
    LuaScriptComponent.h
    LuaSystem.h
    LuaSystem.cpp
    Bindings/
        EntityBindings.h
        EntityBindings.cpp
        TransformBindings.h
        TransformBindings.cpp
        PhysicsBindings.h
        PhysicsBindings.cpp
        AudioBindings.h
        AudioBindings.cpp
        InputBindings.h
        InputBindings.cpp
        LogBindings.h
        LogBindings.cpp
    LuaSandbox.h         -- sandboxing helpers (remove/replace stdlib functions)
    LuaSandbox.cpp
```

Third-party dependency: Lua 5.4 source included as a CMake `INTERFACE` target in `ZEngine/ThirdParty/lua54/`. Not LuaJIT (ARM64 compatibility issues documented in scripting.md §7).

---

## 11. Deliverables Checklist

- [ ] `LuaVM.h` / `LuaVM.cpp` — full implementation including `LoadScript`, `ReloadScript`, `CallGlobal`
- [ ] `LuaScriptComponent.h` — component struct with `HasOn*` flags
- [ ] `LuaSystem.h` / `LuaSystem.cpp` — ECS system, coroutine resume loop, error handling
- [ ] All six binding groups implemented and registered in `LuaVM::Initialize`
- [ ] `LuaSandbox.cpp` — stdlib removal applied before any user script loads
- [ ] `zengine_require` binding with VFS path resolution and whitelist
- [ ] VFS watcher integration: `Modified` event triggers `LuaVM::ReloadScript`
- [ ] Hot-reload test: edit a .lua file at runtime, verify entity resumes with new script
- [ ] Error isolation test: intentional Lua runtime error, verify `HasOnUpdate` set false, other entities unaffected
- [ ] Performance test: 1000 entities with trivial Lua scripts, measure LuaSystem tick time against 200µs budget
- [ ] Coroutine test: multi-frame sequential logic (yield loop), verify correct frame counts
