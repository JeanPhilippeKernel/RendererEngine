# ZEngine — Game / Runtime Boundary

**Priority:** P1 — Must be resolved before the first external game ships  
**Status:** Design  
**Depends on:** `scripting.md`, `engine-lifecycle.md`, `panzerfaust.md`  
**Blocks:** External game development, plugin SDK stability

---

## 1. The Full System Picture

Before designing the boundary, here is the complete chain from user launch to game
running — including Panzerfaust which was missing from earlier docs.

```
┌──────────────────────────────────────────────────────────────┐
│  Panzerfaust  (.NET 8 / Avalonia)                            │
│  What it is:  Project manager and launcher GUI               │
│  Written in:  C# — zero C++ dependency                       │
│                                                              │
│  - List, create, delete .pzf project entries                 │
│  - Scaffold: Scenes/, SceneData/, Imported/Textures|Sounds/  │
│  - Write projectConfig.json                                  │
│  - Launch Obelisk via Process.Start(...)                     │
└──────────────────────────────┬───────────────────────────────┘
                               │
   Process.Start("Obelisk --launchEditor 1
                           --projectConfigFile /path/projectConfig.json")
                               │
┌──────────────────────────────▼───────────────────────────────┐
│  Obelisk  (C++ .exe)                                         │
│  What it is:  Engine entry point — owns pre-engine init      │
│                                                              │
│  - CrashHandler::Install()  ← absolute first                 │
│  - MemoryManager::Initialize(2 GB)                           │
│  - Logger, ThreadPool                                        │
│  - Parse CLI: --launchEditor, --projectConfigFile            │
│  - Create Tetragrama::Editor (editor) or MyGame (game)       │
│  - app->Initialize(arena) → hands off to engine              │
└──────────────────────────────┬───────────────────────────────┘
                               │ arena* + projectConfig
┌──────────────────────────────▼───────────────────────────────┐
│  ZEngine Runtime  (C++ static library)                       │
│  What it is:  Self-contained engine — knows nothing of game  │
│                                                              │
│  - ECS, WorldTick, Physics, Audio, Animation                 │
│  - VFS (mounts project assets or cooked .pak)                │
│  - Rendering (Vulkan, RenderGraph, shadows, post-process)    │
│  - Networking, Input, Save system                            │
│  - GameDLLLoader → loads game DLL via C ABI                  │
│  - ZLuaHost plugin → loads Scripts/*.lua                     │
└──────────────┬───────────────────────────────────────────────┘
               │                            │
   C ABI (ZGame_*)                    ZLuaHost plugin
   ZGameContext* (opaque)             Lua 5.4 VM
               │                            │
┌──────────────▼─────────────┐  ┌───────────▼──────────────────┐
│  MyGame.dll  (C++ DLL)     │  │  Scripts/*.lua               │
│  Engine programmer layer   │  │  Designer / scripter layer   │
│                            │  │                              │
│  ZGame_Initialize()        │  │  function OnEnemyDie(...)    │
│  ZGame_RegisterSystems()   │  │  function quest_check(...)   │
│  Actor subclasses          │  │  function dialogue_tree(...) │
│  ECS components            │  │                              │
│  Physics callbacks         │  │  Hot-reloads on .lua save    │
│  AI systems                │  │  No compile needed           │
│                            │  │                              │
│  Hot-reloads on .dll save  │  │  Calls: zengine.get_pos()    │
│  No engine source needed   │  │         zengine.raycast()    │
└────────────────────────────┘  └──────────────────────────────┘
```

The editor (Tetragrama) sits alongside the game inside the runtime — it registers
its own editor systems with `WorldTick` and uses the same VFS, ECS, and RenderGraph
as the game. There is no separate "editor version" of the engine.

---

## 2. Current State — The Coupling Problem

Today the boundary is drawn in the wrong place. `GameApplication` is a virtual base
class that both Tetragrama and any game must subclass directly:

```cpp
// TODAY — subclassing requires including internal engine headers
class MyGame : public ZEngine::Applications::GameApplication {
    void OnInitializing() override { /* ... */ }  // must include VulkanDevice.h etc.
    void OnUpdate(float dt) override { /* gameplay — bypasses WorldTick */ }
    void OnPreRender()      override { /* ... */ }
    void OnPostRender()     override { /* ... */ }
    void OnRenderUI()       override { /* ... */ }
    void OnClosing()        override { /* ... */ }
    void OnClosed()         override { /* ... */ }
};
```

**Three concrete problems:**

1. **Game code must include internal engine headers.** `GameApplication.h` pulls in
   `AppRenderPipeline.h`, `GraphicScene.h`, `VulkanDevice.h`. Any internal engine change
   breaks game compilation. Engine programmers cannot iterate without game rebuilds.

2. **`OnUpdate`, `OnRenderUI`, `OnPreRender` bypass `WorldTick` entirely.** They are
   called directly from `Engine::MainThreadRun()` as virtual dispatch — outside the
   ECS system DAG, with no dependency tracking, no parallelism, no scheduler oversight.

3. **`Engine.h` includes `GameApplication.h`.** The engine library itself depends on
   a game-facing type. `ZRuntime` cannot link without a concrete subclass.

---

## 3. The Target Boundary

### 3.1 What Obelisk owns (never changes)

Obelisk owns `main()` / `WinMain()`. It owns pre-engine initialization. It reads
`projectConfig.json` and decides which app to create. This does NOT change — Obelisk
is the stable glue between Panzerfaust and the engine.

```cpp
// Obelisk/EntryPoint.cpp — stable, minimal
int applicationEntryPoint(int argc, char* argv[]) {
    CrashHandler::Install();
    MemoryManager manager{};
    manager.Initialize({.DefaultSize = ZGiga(3u)});
    auto* arena = &manager.Allocator;
    Logger::Initialize(arena, {});
    ThreadPoolHelper::Initialize();

    // CLI: --launchEditor flag decides which app to create
    if (launch_editor) {
        auto* app = ZPushStructCtor(arena, Tetragrama::Editor);
        app->ConfigFile        = config_file.c_str();
        app->WorkingSpacePath  = working_space.c_str();
        app->Initialize(arena);
        app->Run();
        app->Shutdown();
    } else {
        // SHIPPING MODE: no --launchEditor flag means the game runs without editor systems.
        // This is the mode players experience. The game DLL is loaded, Lua scripts are
        // read from the cooked .pak, and no Tetragrama systems are registered.
        auto* app = ZPushStructCtor(arena, GameBootstrap);
        app->ConfigFile        = config_file.c_str();
        app->WorkingSpacePath  = working_space.c_str();
        app->Initialize(arena);
        app->Run();
        app->Shutdown();
    }
    Logger::Dispose();
    manager.Shutdown();
    CrashHandler::Uninstall();
}
```

**Obelisk operating modes — determined by CLI flags:**

| Mode | CLI flags | What loads | Used by |
|---|---|---|---|
| Editor | `--launchEditor 1 --projectConfigFile path` | Engine + Tetragrama editor systems + game DLL + Lua scripts from disk | Developers |
| Shipping | `--projectConfigFile path` (no launchEditor) | Engine + game DLL + Lua scripts from cooked .pak | Players |
| Headless server | `--headless 1 --projectConfigFile path` | Engine + game DLL, no window, no renderer | Dedicated multiplayer servers |

The same `Obelisk` binary handles all three modes. The shipped game installer includes
only the `Obelisk` binary, `MyGame.dll`, and `output.pak` — none of the editor tooling.

### 3.2 What `GameApplication` becomes

`GameApplication` is narrowed to the minimum needed by Obelisk. All gameplay callbacks
are removed — they move into the ECS system DAG.

```cpp
// GameApplication after migration — only what Obelisk needs
struct GameApplication {
    cstring                      ConfigFile         = nullptr;
    cstring                      WorkingSpacePath   = nullptr;
    Windows::WindowConfiguration WindowCfg          = {};
    bool                         EnableRenderOverlay = false;

    // Only three virtual hooks survive:
    virtual void OverrideWindowConfiguration() {}  // set window title/size before creation
    virtual void OnInitialized(EngineContext* ctx) = 0; // load game DLL, first scene
    virtual void OnClosing(EngineContext* ctx)      {}  // flush saves

    // Engine-implemented:
    void Initialize(Core::Memory::ArenaAllocator* arena);
    void Run();
    void Shutdown();
};
```

### 3.3 What happens inside `OnInitialized`

For Tetragrama (the editor):
```cpp
void Tetragrama::Editor::OnInitialized(EngineContext* ctx) {
    // Register editor-specific ECS systems
    ctx->WorldTick->RegisterSystem(EditorSceneHierarchySystem, {...});
    ctx->WorldTick->RegisterSystem(EditorSelectionSystem, {...});
    ctx->WorldTick->RegisterSystem(EditorGizmoSystem, {...});
    // Load default empty scene
    BinarySceneSerializer::Load("Scenes/Default.zscene", *ctx->Scene);
}
```

For a shipped game:
```cpp
// SHIPPING MODE — called when --launchEditor flag is absent.
// GameBootstrap is a minimal GameApplication subclass that loads the game DLL
// and the first scene. It has no editor systems.
void GameBootstrap::OnInitialized(EngineContext* ctx) {
    // Load the game DLL — registers all game systems
    GameDLLLoader::Load("MyGame.dll", ctx);
    // Load the first scene (from cooked .pak via VFSPakBackend)
    BinarySceneSerializer::Load("Scenes/MainMenu.zscene", *ctx->Scene);
}
```

### 3.4 How `Engine::MainThreadRun` changes

```cpp
// AFTER — no game callbacks, everything through WorldTick
void Engine::MainThreadRun() {
    while (!s_request_terminate) {
        context->InputManager->Poll(window->GetGLFWWindow());

        while (accumulator.ShouldStep()) {
            context->WorldTick->Tick(*context->Scene,
                                     accumulator.FixedDt(),
                                     world_commands);
            world_commands.Flush(*context->Scene);
            context->Scene->SnapshotTransforms();
            accumulator.ConsumeStep();
        }
        context->ActorManager->Tick(timestep);

        // Submit to render thread — no game callbacks here
        auto* packet = frame_pool.AcquireWriteSlot();
        context->Scene->FillRenderableTransforms(accumulator.Alpha(),
                                                  packet->Transforms);
        frame_pool.SubmitWriteSlot(packet);
    }
}
```

The editor's `EditorSceneHierarchySystem`, `EditorSelectionSystem`, and `EditorGizmoSystem`
are registered ECS systems — they run in the scheduler just like physics and animation.
`OnPreRender`, `OnPostRender`, `OnRenderUI` are gone; their work is done by systems.

---

## 4. Public API surface — what each layer may include

| Layer | May include | May NOT include |
|---|---|---|
| Panzerfaust (.NET) | Nothing — launches Obelisk as a process | Everything |
| Obelisk (C++) | `GameApplication.h`, `Allocator.h`, `ZEngineDef.h` | All internal engine headers |
| Tetragrama (C++) | `GameApplication.h` + `PluginSDK.h` for editor systems | `VulkanDevice.h`, `RenderGraph.h`, etc. |
| Game DLL (C++) | `PluginSDK.h` only | Everything internal |
| Lua scripts | `zengine.*` bindings via ZLuaHost | Nothing C++ |

**Enforcement in CMakeLists.txt:**
```cmake
# Game DLL — only PluginSDK headers visible
target_include_directories(ZEngineGame PRIVATE
    ${ZENGINE_PLUGIN_SDK_DIR}/include)
# NOT: ${ZENGINE_SOURCE_DIR}/ZEngine

# Editor — PluginSDK + a narrow GameApplication shim
target_include_directories(Tetragrama PRIVATE
    ${ZENGINE_PLUGIN_SDK_DIR}/include
    ${ZENGINE_SOURCE_DIR}/ZEngine/Applications)
# NOT: VulkanDevice, RenderGraph, etc.
```

---

## 5. Where `projectConfig.json` fits

Panzerfaust writes `projectConfig.json`. Obelisk reads it. The engine uses it to
set `GameApplication::WorkingSpacePath`, which the VFS mounts as the asset root.

```
Panzerfaust creates:          Obelisk reads:
  projectConfig.json    →     app->WorkingSpacePath = config["workingSpace"]
  Scenes/               →     VFS mounts WorkingSpacePath/Scenes/
  Imported/Textures/    →     AssetManager imports from WorkingSpacePath/Imported/
  Scripts/              →     ZLuaHost scans WorkingSpacePath/Scripts/*.lua
```

This is the only coupling between Panzerfaust (C#) and the engine (C++): the JSON
file format. Changing the JSON schema requires updating both sides.

---

## 6. The two-layer game development model

Engine programmer and gameplay designer work independently, in the same running editor:

```
Engine programmer                    Gameplay designer
─────────────────                    ──────────────────
Writes C++:                          Writes Lua:
  PhysicsCallbacks.cpp                 quest_chapter1.lua
  PlayerSystem.cpp                     boss_phase2.lua
  InventoryComponent.h                 npc_dialogue.lua
  AISystem.cpp

Compiles → MyGame.dll                Saves .lua file

VFS file watcher fires               VFS file watcher fires
→ GameDLLLoader hot-reload           → LuaVM::ReloadScript()
→ WorldTick::BeginRebuild/Commit     → per-entity coroutines reset
→ systems re-registered              → reloads in <1 second
→ reloads in ~5 seconds

Neither needs engine source.
Neither needs to restart the editor.
Both iterate simultaneously.
```

The engine programmer gets the PluginSDK headers. The designer gets the editor and
a Lua API reference document. Neither gets engine source.

---

## 7. What Tetragrama is and is not

Tetragrama is the **editor application**, not a game. It runs the same engine binary
as a shipped game. The distinction is:

- `--launchEditor 1` → Obelisk creates `Tetragrama::Editor`, which registers editor
  ECS systems (hierarchy view, asset browser, play mode, gizmos, inspectors)
- `--launchEditor 0` (or absent) → Obelisk creates a minimal bootstrap that loads the
  game DLL directly and runs headless (for dedicated servers or CI/CD testing)

Play mode in the editor is not a separate mode — it is simply the same game DLL and
Lua scripts running inside the editor process alongside the editor systems. Stop
unregisters the game systems and resets the scene.

---

## 8. Migration steps (integrated into migration-plan.md phases)

| Phase | Change |
|---|---|
| Phase 2 | Add `EngineStartConfig` struct. `Engine::Initialize` accepts it. `GameApplication::Initialize` fills it as a shim. |
| Phase 2 | Delete `OnUpdate`, `OnPreRender`, `OnPostRender`, `OnRenderUI`, `OnEvent`, `OnInitializing`, `OnClosed` from `GameApplication`. |
| Phase 2 | Rewrite `Engine::MainThreadRun` with no `g_app->*` virtual calls. |
| Phase 3 | Migrate Tetragrama's 8 hooks to registered ECS systems. |
| Phase 5 | Delete `GameApplication::Update()`, `PrepareScene()`, `RenderPipeline` public field. |
| Phase 5 | Remove `#include <Applications/GameApplication.h>` from `Engine.h`. |
| After scripting DLL | Enforce CMakeLists include boundary for `ZEngineGame` and `Tetragrama` targets. |
| Panzerfaust v2 | Add `Scripts/` and `Source/` to scaffolded directory structure. |

---

## 8. Build Button — Editor Integration

The editor (Tetragrama) exposes a **Build** button in the toolbar. It builds the game
DLL by running the project's `buildCommand` from `projectConfig.json`.

```
Tetragrama toolbar:  [▶ Play]  [■ Stop]  [⚙ Build]  [📦 Cook]

User clicks Build:
  1. Editor reads projectConfig.json["buildCommand"]
  2. Runs command as a subprocess (same pattern as Obelisk launches from Panzerfaust)
  3. Streams stdout/stderr to the editor's log panel in real time
  4. On success: gameDllPath file changes on disk
                 → VFS file watcher fires
                 → GameDLLLoader::Reload() triggered automatically
                 → WorldTick::BeginRebuild() → Commit()
                 → systems re-registered
  5. On failure: compiler errors appear in log panel with file:line links
```

**The editor does not know how to compile C++.** It only runs a shell command.
The `buildCommand` is configured in `projectConfig.json` and generated by Panzerfaust's
first-launch setup wizard based on the detected compiler and bundled CMake.

**Example `buildCommand` values:**

```json
// Windows + MSVC + bundled CMake:
"buildCommand": "bin/cmake/bin/cmake.exe --build Source/build --config Release --target ZEngineGame"

// macOS + Clang + bundled CMake:
"buildCommand": "bin/cmake/bin/cmake --build Source/build --config Release --target ZEngineGame"

// Linux + GCC + bundled CMake (parallel build):
"buildCommand": "bin/cmake/bin/cmake --build Source/build -j4 --target ZEngineGame"
```

Where `bin/cmake/` is resolved relative to the SDK directory (next to Obelisk and Panzerfaust).

**First build (configure step):**

The first time a project is built, CMake must configure the build directory before
building. Panzerfaust handles this at project creation time:

```
Panzerfaust "New Project" → "3D Starter" template:
  1. Extracts template to project directory
  2. Runs CMake configure:
     cmake -S Source/ -B Source/build
           -DZENGINE_PLUGIN_SDK_DIR=SDK/PluginSDK
           -DCMAKE_BUILD_TYPE=Release
  3. Writes buildCommand to projectConfig.json
  4. Launches editor

Subsequent builds (user clicks Build button):
  cmake --build Source/build --config Release  ← incremental, fast
```

**Compile times (approximate, single-file change):**

| Compiler | Clean build | Incremental change |
|---|---|---|
| MSVC (Release) | ~30s | ~5s |
| Clang (Release) | ~20s | ~3s |
| GCC -O2 | ~25s | ~4s |

The "~5 seconds" hot-reload time cited in other docs includes compile + DLL swap.
Lua scripts skip compilation entirely — they reload in under 1 second.

---

## 9. Deliverables Checklist

- [ ] `EngineStartConfig` struct — replaces `GameApplicationPtr` in `Engine.h`
- [ ] `GameApplication` narrowed to 3 hooks: `OverrideWindowConfiguration`, `OnInitialized(ctx*)`, `OnClosing(ctx*)`
- [ ] `Engine::MainThreadRun()` rewritten — no `g_app->*` virtual calls
- [ ] `Engine.h` does not include `GameApplication.h`
- [ ] CMakeLists include enforcement: `ZEngineGame` and `Tetragrama` see only their allowed headers
- [ ] Tetragrama hooks migrated to ECS systems
- [ ] `panzerfaust.md` updated: scaffold `Scripts/` and `Source/` directories
- [ ] `projectConfig.json` schema versioned (add `"schemaVersion": 1`)
- [ ] `migration-plan.md` Phase 2 updated with above steps
