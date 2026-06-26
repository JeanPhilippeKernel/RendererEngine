# ZEngine — Editor Play Mode (Play / Pause / Stop)

**Priority:** P1 — Required for Sprint 13; editor is unusable without it  
**Status:** Design  
**Depends on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `game-runtime-boundary.md`, `scene-serialization.md`, `engine-lifecycle.md`  
**Blocks:** Tetragrama editor usability, all gameplay iteration

---

## 1. Overview

The editor operates in three modes:

| Mode | Description |
|---|---|
| **Edit** | Default. Scene is static. Only editor ECS systems run. Physics paused. No game DLL systems. |
| **Play** | All systems run — editor + game together. Identical to the shipped game inside the editor window. |
| **Paused** | All systems frozen. Editor systems still run (can inspect, move camera). Single-step available. |

**Key invariant:** Entering Play preserves the scene state. Stop restores it exactly. This is done via a binary scene snapshot, separate from the Play/Stop mechanism — not via Undo/Redo.

---

## 2. `PlayModeState`

```cpp
// ZEngine/Editor/PlayMode/PlayModeState.h
#pragma once
#include <Core/Containers/Array.h>
#include <cstdint>

namespace ZEngine::Editor {

    struct PlayModeState {
        enum class Mode : uint8_t { Edit, Play, Paused };

        Mode    CurrentMode = Mode::Edit;
        bool    SingleStep  = false;  // advance exactly one fixed step then re-pause

        // Binary scene snapshot taken at the moment Play is pressed.
        // Restored when Stop is pressed.
        // Empty in Edit and Paused modes.
        Core::Containers::Array<uint8_t> SceneSnapshot;

        [[nodiscard]] bool IsEditing() const { return CurrentMode == Mode::Edit;   }
        [[nodiscard]] bool IsPlaying() const { return CurrentMode == Mode::Play;   }
        [[nodiscard]] bool IsPaused()  const { return CurrentMode == Mode::Paused; }
    };

}  // namespace ZEngine::Editor
```

---

## 3. System registration per mode

The editor registers two distinct sets of ECS systems. Game systems are only added when Play is pressed.

```
Edit mode — systems always registered:
  EditorHierarchySystem    reads TransformComponent + ParentComponent → syncs to UI tree
  EditorGizmoSystem        reads/writes TransformComponent → draws ImGuizmo handles
  EditorSelectionSystem    reads MeshComponent + TransformComponent → raycast selection

Play mode adds (registered on Play, removed on Stop):
  PhysicsSyncTransformToBodySystem
  PhysicsStepSystem
  PhysicsSyncBodyToTransformSystem
  CharacterControllerSystem
  AnimationSampleSystem
  SkinningUploadSystem
  AudioListenerSystem
  AudioSourceSystem
  LuaSystem                (if ZLuaHost plugin loaded)
  ReplicationSystem        (if multiplayer)
  NetRelevanceSystem       (if server)
  ...all systems from ZGame_RegisterSystems()
```

---

## 4. `EditorPlayModeSystem`

The coordinator. Owns `PlayModeState` and handles toolbar events. Registered as an ECS system that runs in both Edit and Play mode.

```cpp
// ZEngine/Editor/PlayMode/EditorPlayModeSystem.h
#pragma once
#include <Editor/PlayMode/PlayModeState.h>
#include <ECS/Scene.h>
#include <Scripting/GameDLLLoader.h>

namespace ZEngine::Editor {

    class EditorPlayModeSystem {
    public:
        void Initialize(EngineContext* ctx, GameDLLLoader* dll_loader,
                        UndoRedoStack* undo_stack);

        // Called every frame by MainThreadRun (outside WorldTick).
        void Update(float raw_dt);

        // Toolbar button callbacks (called from Tetragrama UI thread).
        void OnPlayPressed();
        void OnPausePressed();
        void OnStopPressed();
        void OnStepPressed();  // single-step while paused

        [[nodiscard]] PlayModeState::Mode GetMode()   const { return m_state.CurrentMode; }
        [[nodiscard]] bool               IsEditing()  const { return m_state.IsEditing(); }
        [[nodiscard]] bool               IsPlaying()  const { return m_state.IsPlaying(); }
        [[nodiscard]] bool               IsPaused()   const { return m_state.IsPaused();  }
        [[nodiscard]] bool               ShouldStep()  const { return m_state.SingleStep; }

    private:
        PlayModeState  m_state;
        EngineContext* m_ctx        = nullptr;
        GameDLLLoader* m_dll_loader = nullptr;
        UndoRedoStack* m_undo_stack = nullptr;

        void EnterPlay();
        void EnterPause();
        void EnterStop();
        void TakeSnapshot();
        void RestoreSnapshot();
    };

}  // namespace ZEngine::Editor
```

---

## 5. Play button — exact sequence

```cpp
void EditorPlayModeSystem::EnterPlay() {
    ZENGINE_VALIDATE_ASSERT(m_state.IsEditing(),
        "EnterPlay called from non-Edit mode");

    // Step 1: Clear undo stack (play simulation is not undoable)
    m_undo_stack->Clear();

    // Step 2: Take scene snapshot (binary serialization into memory buffer)
    TakeSnapshot();

    // Step 3: Load game DLL if not already loaded
    if (!m_dll_loader->IsLoaded()) {
        m_dll_loader->Load(m_ctx->ConfigFile, m_ctx);
        ZGame_Initialize(m_ctx);
    }

    // Step 4: Register game systems into WorldTick
    m_ctx->WorldTick->BeginRebuild();
    ZGame_RegisterSystems(m_ctx->WorldTick, m_ctx->Scene);
    ZGame_OnSceneLoad(m_ctx->Scene);
    m_ctx->WorldTick->Commit();  // rebuild DAG including game systems

    // Step 5: Resume physics (bodies were frozen in edit mode)
    m_ctx->PhysicsWorld->Resume();

    // Step 6: Resume audio
    m_ctx->AudioEngine->Resume();

    // Step 7: Route input to game (not editor camera)
    m_ctx->InputManager->SetMode(InputMode::Game);

    // Step 8: Set mode
    m_state.CurrentMode = PlayModeState::Mode::Play;

    // Toolbar: [▶ Play] → [■ Stop] [⏸ Pause]
    ZENGINE_CORE_INFO("Editor: entered Play mode");
}

void EditorPlayModeSystem::TakeSnapshot() {
    m_state.SceneSnapshot.Clear();
    BinarySceneSerializer::Serialize(*m_ctx->Scene, m_state.SceneSnapshot);
    ZENGINE_CORE_INFO("Editor: scene snapshot taken (%zu bytes)",
                      m_state.SceneSnapshot.Size());
}
```

---

## 6. Pause button — exact sequence

```cpp
void EditorPlayModeSystem::EnterPause() {
    ZENGINE_VALIDATE_ASSERT(m_state.IsPlaying(),
        "EnterPause called from non-Play mode");

    // Step 1: Pause physics (Jolt bodies freeze in place)
    m_ctx->PhysicsWorld->Pause();

    // Step 2: Pause audio (all sounds freeze)
    m_ctx->AudioEngine->Pause();

    // Step 3: Release input back to editor camera
    m_ctx->InputManager->SetMode(InputMode::Editor);

    // Step 4: Set mode
    // WorldTick::Tick still called but accumulator is frozen — see §9.
    m_state.CurrentMode = PlayModeState::Mode::Paused;

    // Toolbar: [■ Stop] [▶ Resume] [▶▶ Step]
    ZENGINE_CORE_INFO("Editor: paused");
}
```

---

## 7. Single-step (Step button while Paused)

Advances exactly one fixed simulation step then re-pauses. For frame-by-frame debugging.

```cpp
void EditorPlayModeSystem::OnStepPressed() {
    ZENGINE_VALIDATE_ASSERT(m_state.IsPaused(), "Step only available while Paused");
    m_state.SingleStep = true;
    // Resume physics and audio for one tick only.
    // MainThreadRun checks ShouldStep() and executes one accumulator step.
    // EditorPlayModeSystem::Update() clears SingleStep after the step completes.
    m_ctx->PhysicsWorld->Resume();
    m_ctx->AudioEngine->Resume();
}
```

---

## 8. Stop button — exact sequence

```cpp
void EditorPlayModeSystem::EnterStop() {
    ZENGINE_VALIDATE_ASSERT(m_state.IsPlaying() || m_state.IsPaused(),
        "EnterStop called from Edit mode");

    // Step 1: Stop audio cleanly
    m_ctx->AudioEngine->Shutdown();
    m_ctx->AudioEngine->Initialize(m_ctx->AudioArena, m_ctx->VFS);

    // Step 2: Reset physics (destroy all simulation bodies)
    m_ctx->PhysicsWorld->Reset();

    // Step 3: Unregister game systems from WorldTick
    m_ctx->WorldTick->BeginRebuild();
    ZGame_Shutdown();
    m_dll_loader->Unload();
    // Re-register only editor systems
    m_ctx->WorldTick->Commit();

    // Step 4: Clear scene
    m_ctx->Scene->Clear();

    // Step 5: Restore scene from snapshot
    RestoreSnapshot();

    // Step 6: Release input back to editor camera
    m_ctx->InputManager->SetMode(InputMode::Editor);

    // Step 7: Free snapshot buffer
    m_state.SceneSnapshot.Clear();

    // Step 8: Set mode
    m_state.CurrentMode = PlayModeState::Mode::Edit;

    // Toolbar returns to: [▶ Play] [⚙ Build] [📦 Cook]
    ZENGINE_CORE_INFO("Editor: returned to Edit mode");
}

void EditorPlayModeSystem::RestoreSnapshot() {
    ZENGINE_VALIDATE_ASSERT(!m_state.SceneSnapshot.IsEmpty(),
        "RestoreSnapshot: snapshot is empty");
    BinarySceneSerializer::Deserialize(m_state.SceneSnapshot, *m_ctx->Scene);
    ZENGINE_CORE_INFO("Editor: scene restored from snapshot");
}
```

---

## 9. `FixedTimestepAccumulator` integration

The game loop must not advance fixed steps while Paused (unless single-stepping).

```cpp
// In Engine::MainThreadRun — updated logic:
auto& play = *editor_play_mode_system;

if (!play.IsPaused() || play.ShouldStep()) {
    accumulator.Accumulate(raw_dt);

    while (accumulator.ShouldStep()) {
        ctx->InputManager->Poll(window->GetGLFWWindow());

        ctx->WorldTick->Tick(*ctx->Scene, accumulator.FixedDt(), world_cmds);
        world_cmds.Flush(*ctx->Scene);
        ctx->Scene->SnapshotTransforms();
        accumulator.ConsumeStep();

        if (play.ShouldStep()) {
            // Single-step: execute once then re-pause
            m_state.SingleStep = false;
            ctx->PhysicsWorld->Pause();
            ctx->AudioEngine->Pause();
            ctx->InputManager->SetMode(InputMode::Editor);
            m_state.CurrentMode = PlayModeState::Mode::Paused;
            break;
        }
    }
} else {
    // Paused: editor systems still need to run each frame for inspector/gizmos
    ctx->InputManager->Poll(window->GetGLFWWindow());
    ctx->WorldTick->TickEditorOnly(*ctx->Scene, raw_dt);  // runs only editor systems
}
```

`WorldTick::TickEditorOnly` runs only the systems marked as editor systems (those registered before Play and never unregistered).

---

## 10. `InputMode` — editor vs game routing

```cpp
// ZEngine/Input/InputManager.h — InputMode enum
enum class InputMode : uint8_t {
    Editor,  // WASD moves editor camera; LMB selects entities
    Game,    // all input flows through InputFrame to game systems
};
```

| Mode | WASD | LMB | RMB | Escape |
|---|---|---|---|---|
| Edit | Editor camera fly | Select entity | Camera orbit | No-op |
| Play | Game system reads | Game system reads | Game system reads | Opens pause menu (game-defined) |
| Paused | Editor camera fly | Select entity | Camera orbit | No-op |

---

## 11. What persists across Play / Stop

| State | Persists? | Reason |
|---|---|---|
| Scene entities + components | No — restored from snapshot | Must be reproducible |
| Editor camera position/rotation | Yes | Quality of life |
| Selected entity | No — EntityIDs change on restore | EntityID is unstable across Clear/Deserialize |
| Console log | Yes | Debugging value |
| Undo/Redo stack | No — cleared on Play | Simulation is not undoable |
| Loaded game DLL | No — unloaded on Stop | Clean slate for next Play |
| VFS mounts | Yes | Expensive to remount; pak is not modified |
| Audio engine clips | No — re-init on Stop | Clean audio state |

---

## 12. Toolbar state machine

```
Edit mode:     [▶ Play]        [⚙ Build]  [📦 Cook]  [🚀 Ship]
                  ↓ OnPlay
Play mode:     [■ Stop] [⏸ Pause]
                  ↓ OnPause
Paused mode:   [■ Stop] [▶ Resume] [▶▶ Step]
                  ↓ OnStop                ↓ OnPlay (resume)
Edit mode:     [▶ Play]        ...        Play mode
```

The toolbar buttons are owned by `DockspaceUIComponent` and call into
`EditorPlayModeSystem` via message bus (existing Tetragrama messenger pattern).

---

## 13. File Layout

```
ZEngine/
  Editor/
    PlayMode/
      PlayModeState.h
      EditorPlayModeSystem.h/.cpp
```

---

## 14. Deliverables Checklist

- [ ] `PlayModeState` struct — Mode enum, SceneSnapshot buffer, SingleStep flag
- [ ] `EditorPlayModeSystem` — Initialize, Update, OnPlay/Pause/Stop/Step handlers
- [ ] `EnterPlay()` — snapshot + DLL load + system registration + physics/audio resume
- [ ] `EnterPause()` — physics/audio pause + input release + accumulator freeze
- [ ] `EnterStop()` — audio reset + physics reset + DLL unload + scene restore
- [ ] `TakeSnapshot()` — BinarySceneSerializer into in-memory buffer
- [ ] `RestoreSnapshot()` — BinarySceneSerializer from buffer
- [ ] `WorldTick::TickEditorOnly()` — runs only non-game systems when paused
- [ ] `FixedTimestepAccumulator` integration — frozen when Paused, single-step support
- [ ] `InputMode` enum + `InputManager::SetMode()` — editor vs game routing
- [ ] `DockspaceUIComponent` toolbar wiring — button callbacks to EditorPlayModeSystem
- [ ] `UndoRedoStack::Clear()` called on Play
- [ ] `tests/Editor/PlayModeTest.cpp`:
  - [ ] EnterPlay takes snapshot, systems registered, scene unchanged
  - [ ] EnterStop restores scene to pre-Play state exactly
  - [ ] EnterPause freezes accumulator (no fixed steps dispatched)
  - [ ] Single-step advances exactly one fixed step
  - [ ] Scene snapshot round-trip: serialize → clear → deserialize → matches original
  - [ ] Play with no game DLL: graceful error, does not enter Play mode
