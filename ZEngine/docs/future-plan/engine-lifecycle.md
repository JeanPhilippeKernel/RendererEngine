# ZEngine — Engine Lifecycle

**Priority:** P0 — All subsystems depend on correct initialization order
**Status:** Design — extends existing Engine.h/.cpp and GameApplication
**Modifies:** `Engine.h`, `Engine.cpp`, `GameApplication.h`, `EngineContext`

---

## 1. Current State Analysis

### What the existing Engine.cpp does correctly

**Window before Device.** `GameWindow::Initialize()` is called before `VulkanDevice::Initialize()`. This is correct: Vulkan surface creation (`vkCreateWin32SurfaceKHR` / `vkCreateXlibSurfaceKHR` etc.) requires a valid native window handle. Reversing this order would cause a null surface handle when the device queries surface capabilities.

**Device before AssetManager.** `VulkanDevice::Initialize()` runs before `AssetManager::Initialize()`. This is correct: the asset manager performs GPU-side uploads (staging buffers, `vkQueueSubmit` for texture mips, mesh vertex/index uploads). These require a live logical device, command pools, and transfer queues.

**Render thread after all init.** `g_render_thread` is spawned inside `Engine::Run()` after the full `Initialize()` call chain completes. This is correct: the render thread accesses `AppRenderPipeline`, `VulkanDevice`, and `GameWindow`, all of which must be fully initialized before the thread starts issuing Vulkan calls.

### Ownership model: Obelisk owns pre-engine initialization

**IMPORTANT — Obelisk architecture constraint:**
`Obelisk/EntryPoint.cpp` is the actual entry point (`main()` / `WinMain()`). It owns
the process-level setup that must happen before `Engine::Initialize()` is called:

```cpp
// Obelisk/EntryPoint.cpp — actual execution order today:
MemoryManager manager = {};
manager.Initialize({.DefaultSize = ZGiga(3u)});   // MemoryManager on Obelisk's stack
auto arena = &manager.Allocator;

Logger::Initialize(arena, logger_cfg);             // Logger before Engine
ThreadPoolHelper::Initialize();                    // ThreadPool before Engine

app->Initialize(arena);   // ← calls GameApplication::Initialize → Engine::Initialize
app->Run();
app->Shutdown();
Logger::Dispose();
manager.Shutdown();       // fix typo before shipping: Shutdowm → Shutdown
```

This means:
- `MemoryManager` is **NOT** a singleton inside the engine. It lives on Obelisk's stack
  and its arena pointer is passed into `Engine::Initialize`.
- `Logger::Initialize` and `ThreadPoolHelper::Initialize` are already called by Obelisk
  **before** `Engine::Initialize`. The engine asserts they are live but must not call them again.
- `Engine::Initialize` accepts and validates the pre-initialized arena; it does not create one.
- Sub-arenas for all subsystems are carved from the passed arena and stored in `EngineContext`
  fields — never as local variables inside `Engine::Initialize`.

### Requirements for the new implementation

The following constraints are addressed in §§2–7 of this document:
- `CrashHandler::Install()` must be Obelisk's first line, before any allocation.
- `Engine::Initialize()` asserts pre-conditions (Logger live, ThreadPool live, arena non-null).
- `EngineContext` owns all subsystem pointers and their sub-arenas (§2).
- `WorldTick::Commit()` is called after `OnInitialized()` so game systems are included in the DAG (§3 Steps 18–19).
- Shutdown proceeds in strict reverse initialization order with all subsystems properly torn down (§4).
- `PanicShutdown()` unwinds exactly as far as initialization progressed (§7).

---

## 2. Extended EngineContext

`EngineContext` is the authoritative owner of all engine-level subsystem pointers. It does not own the memory for these objects (they are arena-allocated); it holds non-owning pointers for access and for lifecycle control during shutdown.

```cpp
// Forward declarations (add to EngineContext.h or Engine.h header)
namespace ZEngine::Input { class InputManager; }

struct EngineContext
{
    // ----------------------------------------------------------------
    // Existing — Phase 1
    // ----------------------------------------------------------------
    Hardwares::VulkanDevicePtr           Device          = nullptr;
    Windows::CoreWindowPtr               Window          = nullptr;

    // ----------------------------------------------------------------
    // New — Phase 2 + new systems (migration-plan.md)
    // ----------------------------------------------------------------
    ECS::Scene*                          Scene           = nullptr;
    ECS::WorldTick*                      WorldTick       = nullptr;
    ECS::ActorManager*                   ActorManager    = nullptr;
    Core::VFS::IVFSContext*              VFS             = nullptr;
    Animation::AnimationManager*         AnimMgr         = nullptr;
    Physics::PhysicsWorld*               PhysicsWorld    = nullptr;
    Audio::AudioEngine*                  AudioEngine     = nullptr;
    Input::InputManager*                 InputManager    = nullptr;
    Network::NetworkSession*             NetworkSession  = nullptr;  // may be null for single-player

    // Sub-arenas — stored here so their lifetime matches EngineContext.
    // Never store sub-arenas on the stack inside Engine::Initialize().
    Core::Memory::ArenaAllocator         VFSArena            = {};
    Core::Memory::ArenaAllocator         ECSArena            = {};
    Core::Memory::ArenaAllocator         AnimationArena      = {};
    Core::Memory::ArenaAllocator         PhysicsArena        = {};
    Core::Memory::ArenaAllocator         AudioArena          = {};
    Core::Memory::ArenaAllocator         NetworkArena        = {};
    Core::Memory::ArenaAllocator         InputArena          = {};
};
```

All fields that may be absent in a given configuration (e.g. `NetworkSession` in a single-player build) must be null-checked by callers before use. The canonical test is `ZENGINE_VALIDATE_ASSERT(ctx.Scene != nullptr, ...)` at the top of any function that requires the field.

---

## 3. Initialization Order

Each step lists the constraint that forces it to occur at that position. Steps that have no ordering dependency on each other within a group could in principle run concurrently, but are kept sequential here to simplify error recovery.

```
═══════════════════════════════════════════════════════════════════
OBELISK SCOPE — applicationEntryPoint() in Obelisk/EntryPoint.cpp
These steps happen BEFORE Engine::Initialize() is called.
═══════════════════════════════════════════════════════════════════

Step  1: CrashHandler::Install()                          [OBELISK — NEW]
         Owner: Obelisk/EntryPoint.cpp (first line of applicationEntryPoint)
         Rationale: must be live before malloc, before Logger, before any
                    system that could crash. Cannot be Engine's responsibility
                    because a crash during MemoryManager::Initialize must still
                    produce a minidump.

Step  2: MemoryManager::Initialize(ZGiga(3u))             [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp (stack-allocated, arena pointer passed down)
         Rationale: all subsequent arenas carved from this block. Obelisk owns
                    the lifetime — MemoryManager is NOT a singleton in the engine.

Step  3: Logger::Initialize(arena, cfg)                   [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: every subsequent step emits ZENGINE_CORE_* calls.
                    Already called by Obelisk before Engine::Initialize.
                    Engine::Initialize must NOT call it again — only assert it is live.

Step  4: ThreadPoolHelper::Initialize()                   [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: VulkanDevice::Initialize dispatches background GPU work.
                    Already called by Obelisk before Engine::Initialize.
                    Engine::Initialize must NOT call it again — only assert it is live.

Step  5: CLI argument parsing + app selection             [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: decides whether to create Tetragrama::Editor or a game app,
                    and sets app->ConfigFile from --projectConfigFile argument.

═══════════════════════════════════════════════════════════════════
ENGINE SCOPE — Engine::Initialize(), called from GameApplication::Initialize()
═══════════════════════════════════════════════════════════════════

Step  6: Assert pre-conditions                            [ENGINE — NEW]
         ZENGINE_VALIDATE_ASSERT(arena != nullptr)
         ZENGINE_VALIDATE_ASSERT(Logger::IsInitialized())
         ZENGINE_VALIDATE_ASSERT(ThreadPoolHelper::IsInitialized())

Step  7: GameApplication::OnInitializing()
         Rationale: game DLL configures window parameters (resolution, title, fullscreen).
                    Must precede window creation so config is known.

Step  8: GameApplication::OverrideWindowConfiguration()
         Rationale: secondary hook for window config; runs immediately after OnInitializing.

Step  9: GameWindow::Initialize()
         Rationale: Vulkan surface creation requires a valid native window handle.
                    Must precede VulkanDevice::Initialize().

Step 10: VulkanDevice::Initialize()
         Rationale: GPU context required by all render systems, AssetManager uploads,
                    and AppRenderPipeline graph compilation.
                    Depends on Step 9 (surface), Step 4 (thread pool), Step 2 (arena).

Step 11: VFS::VFSDiskContext::Initialize()
         Rationale: path resolution and asset registry required by AssetManager and
                    AudioEngine before they attempt any file I/O.
                    Depends on Step 2 (VFS sub-arena carved from arena).

Step 12: AssetManager::Initialize()
         Rationale: mesh, texture, and material loading required before scene population.
                    Depends on Step 10 (GPU uploads), Step 11 (VFS), Step 2 (sub-arena).

Step 11: ECS::Scene::Initialize()
         Rationale: entity and component storage required by all game systems.
                    ActorManager, WorldTick, and AnimationManager all write into the scene.
                    Depends on Step 4 (ECS sub-arena).

Step 12: ECS::WorldTick::Initialize()
         Rationale: system registration and dependency declaration requires a live scene.
                    Game DLL registers its own systems in OnInitialized() (Step 15),
                    so Commit() must not be called here — only registration infra is set up.
                    Depends on Step 11.

Step 13: ECS::ActorManager::Initialize()
         Rationale: actor lifecycle management requires a live scene and WorldTick.
                    Depends on Steps 11 and 12.

Step 14: Animation::AnimationManager::Initialize()
         Rationale: skeleton and clip data requires live ECS scene for component queries.
                    Depends on Steps 8 (GPU skinning buffers), 11 (scene components),
                    10 (asset loading for animation clips).

Step 15: Physics::PhysicsWorld::Initialize()
         Rationale: Jolt body data requires live ECS scene for transform components.
                    Has no GPU dependency — CPU-only.
                    Depends on Step 11.

Step 16: Audio::AudioEngine::Initialize()
         Rationale: miniaudio state and clip pool require VFS for asset path resolution.
                    Has no GPU dependency — CPU-only.
                    Depends on Step 9 (VFS).

Step 17: Network::NetworkSession::Initialize()   [conditional: multiplayer only]
         Rationale: peer state and snapshot ring buffers require ECS scene for rollback.
                    Skipped entirely in single-player builds.
                    Depends on Step 11.

         Contract: Games optionally override `GameApplication::RequiresNetworking()` which
         returns false by default. If it returns true, Engine::Initialize() allocates the
         network arena and creates the NetworkSession. If network initialization fails, a
         CRITICAL log is emitted and PanicShutdown() is invoked. Single-player games leave
         context->NetworkSession null. All engine code that uses NetworkSession must guard:
         `if (context->NetworkSession) { ... }`

Step 18: GameApplication::OnInitialized()
         Rationale: game DLL entry point (ZGame_Initialize). Registers custom ECS systems,
                    loads the initial scene, sets up UI. All engine subsystems are live.
                    Must precede WorldTick::Commit() so game systems are included in the DAG.

Step 19: ECS::WorldTick::Commit()
         Rationale: builds the execution DAG from all registered systems and their
                    declared Read/Write/After/Before dependencies.
                    Must occur after all system registration (Steps 12 and 18).
                    Must occur before MainThreadRun() dispatches any tick.

Step 20: AppRenderPipeline::Initialize()
         Rationale: render graph compilation references scene components, resource manager
                    handles, and GPU device state. Built after scene and resources are ready.
                    Depends on Steps 8, 10, 11, 19.

Step 21: AssetManager::Run()
         Rationale: starts the async import queue. Placed after AppRenderPipeline so the
                    pipeline can consume freshly imported assets on the first frame.

Step 22: spawn g_render_thread (RenderThreadRun)
         Rationale: render thread accesses AppRenderPipeline, VulkanDevice, GameWindow.
                    All three must be fully initialized.

Step 23: MainThreadRun()   [blocks until shutdown is requested]
         Rationale: game loop starts only after all systems and the render thread are live.
```

---

## 4. Shutdown Order

Shutdown proceeds in strict reverse initialization order. The constraint is that no subsystem may be destroyed while another subsystem that depends on it is still running.

```
Step  1: set s_request_terminate = true
         Rationale: signals MainThreadRun and RenderThreadRun to exit their loops.
         Note: GameApplication::OnClosing() is called before this flag is set,
               giving the game DLL a chance to save state while all subsystems are live.

Step  2: render thread: signal done; join g_render_thread
         Rationale: render thread must stop issuing Vulkan calls before any GPU resource
                    is destroyed. Join guarantees the thread has fully exited.

Step  3: AssetManager::Shutdown()
         Rationale: drains the async import queue. Import jobs may hold GPU staging buffers.
                    Must complete before AppRenderPipeline or VulkanDevice are torn down.

Step  4: AppRenderPipeline::Shutdown()
         Rationale: destroys framebuffers, render passes, and descriptor sets.
                    Must occur before VulkanDevice::Deinitialize() which frees the device.

Step  5: Network::NetworkSession::Shutdown()   [if initialized]
         Rationale: closes sockets and flushes snapshot buffers before ECS scene is destroyed.

Step  6: Audio::AudioEngine::Shutdown()
         Rationale: stops all audio streams before the VFS that backs asset loading is closed.

Step  7: Physics::PhysicsWorld::Shutdown()
         Rationale: destroys Jolt bodies and constraint cache before ECS scene is destroyed.

Step  8: Animation::AnimationManager::Shutdown()
         Rationale: frees GPU skinning buffers before VulkanDevice shutdown.
                    Destroys skeleton/clip references before ECS scene shutdown.

Step  9: ECS::ActorManager::Shutdown()
         Rationale: calls Actor::Detach() on all live actors, removing component ownership.
                    Must occur before WorldTick and Scene are destroyed.

Step 10: ECS::WorldTick::Shutdown()
         Rationale: destroys the execution DAG and system registration table.
                    Must occur after ActorManager (actors may hold system references).
                    Must occur before Scene (systems may hold component iterators).

Step 11: ECS::Scene::Shutdown()
         Rationale: destroys all entity records and component storage dense arrays.
                    Must occur after all systems that hold component pointers.

Step 12: VFS::VFSDiskContext::Shutdown()
         Rationale: closes file handles and flushes the asset registry.
                    Must occur after all subsystems that perform file I/O (Audio, AssetManager).

Step 13: VulkanDevice::Deinitialize()
         Rationale: destroys the logical device, queues, command pools, and VMA allocator.
                    Must occur after all Vulkan resource holders (pipeline, animation, asset mgr).

Step 14: GameWindow::Deinitialize()
         Rationale: destroys the OS window and Vulkan surface.
                    Must occur after VulkanDevice which holds a reference to the surface.

═══════════════════════════════════════════════════════════════════
OBELISK SCOPE — after app->Shutdown() returns to applicationEntryPoint
═══════════════════════════════════════════════════════════════════

Step 15: ThreadPoolHelper::Shutdown()                     [OBELISK-OWNED]
         Owner: Obelisk/EntryPoint.cpp (implicit today; must be made explicit)
         Rationale: joins all worker threads.
                    Must occur after app->Shutdown() so all dispatched work is complete.

Step 16: Logger::Flush() + Logger::Dispose()              [OBELISK-OWNED]
         Owner: Obelisk/EntryPoint.cpp (exists today as Logger::Dispose())
         Rationale: flushes all pending log entries to disk.
                    Must occur after all engine subsystems have shut down.

Step 17: MemoryManager::Shutdown()                        [OBELISK-OWNED]
         Owner: Obelisk/EntryPoint.cpp (stack variable — destructs at scope exit)
         Rationale: calls free() on the 2 GB block.
                    Must be last; all arena-backed objects are already logically destroyed.
                    Note: today Obelisk calls manager.Shutdowm() (typo) — fix to Shutdown().

Step 18: CrashHandler::Uninstall()                        [OBELISK-OWNED — NEW]
         Owner: Obelisk/EntryPoint.cpp — add after MemoryManager::Shutdown()
         Rationale: removes signal/exception handlers.
                    Must be absolute last — no crash reporting is possible after this point.
```

`GameApplication::OnClosed()` is called after Step 17 and before Step 18. At that point no arena memory is valid; `OnClosed()` may only use stack-local or OS-allocated resources.

---

## 5. GameApplication Virtual Hooks

The table below maps each virtual hook to the initialization/shutdown step at which it fires and states what the game DLL is permitted to do at that point.

| Hook | Fires at | Engine state at call time | Permitted operations |
|---|---|---|---|
| `OnInitializing()` | Before Step 7 (window) | Logger + CrashHandler + ThreadPool + MemoryManager live | Configure `WindowCfg` fields; set arena size overrides |
| `OverrideWindowConfiguration()` | Between `OnInitializing` and Step 7 | Same as above | Override title, resolution, MSAA, fullscreen flags |
| `OnInitialized()` | After Step 18 (ActorManager initialized, before Commit) | All subsystems live except WorldTick DAG not yet committed | Register ECS systems; load initial scene; set up UI; bind input |
| `OnUpdate(float dt)` | Each iteration of `MainThreadRun()` | Full engine live | `dt` is the FIXED timestep duration (1/60 sec by default). Do NOT call `WorldTick::Tick()` yourself — the engine calls it before `OnUpdate()`. Use `dt` for gameplay logic that must remain frame-rate independent. `Actor::OnTick` receives the full `Core::TimeStep` (with Alpha for interpolation). |
| `OnEvent(CoreEvent&)` | On each OS event in `MainThreadRun()` | Full engine live | Handle window resize, key/mouse events, close request |
| `OnPreRender()` | Each frame in `RenderThreadRun()`, before render graph execution | AppRenderPipeline live | Update per-frame uniform buffers; push debug geometry |
| `OnPostRender()` | Each frame in `RenderThreadRun()`, after render graph execution | Swapchain image ready for present | Read-back queries; overlay debug stats |
| `OnRenderUI()` | Each frame in `RenderThreadRun()`, after `OnPostRender()` | UIContext live | Issue UIContext draw calls |
| `OnClosing()` | Before Step 1 (before terminate flag) | Full engine live | Save game state; flush network; close sockets |
| `OnClosed()` | After Step 17, before Step 18 | MemoryManager freed; Logger flushing | OS-level resource release only; no arena use |

---

## 6. Engine::Initialize Rewrite

The rewrite below inserts the missing initialization steps (Steps 1–4, 9, 11–17, 19) around the existing working code. Lines marked `// EXISTING` are kept verbatim. Lines marked `// NEW` are additions.

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Obelisk/EntryPoint.cpp owns Steps 1–5 (crash handler, memory, logger,
// thread pool, CLI parsing). These run BEFORE GameApplication::Initialize()
// and BEFORE Engine::Initialize() is called.
//
// Obelisk entry point (existing code + CrashHandler addition):
//
//   CrashHandler::Install();                      // NEW — add as first line
//   MemoryManager manager = {};
//   manager.Initialize({.DefaultSize = ZGiga(3u)});
//   auto arena = &manager.Allocator;
//   Logger::Initialize(arena, logger_cfg);
//   ThreadPoolHelper::Initialize();
//   // ... CLI parsing, app creation ...
//   app->Initialize(arena);   // → GameApplication::Initialize → Engine::Initialize
//   app->Run();
//   app->Shutdown();
//   Logger::Dispose();
//   manager.Shutdown();       // fix typo: Shutdowm → Shutdown
//   CrashHandler::Uninstall();
// ─────────────────────────────────────────────────────────────────────────────

bool Engine::Initialize(ArenaAllocator* arena, WindowConfiguration* window_cfg, GameApplication* app)
{
    // Step 6 — Assert pre-conditions (Obelisk must have completed Steps 1–5)
    ZENGINE_VALIDATE_ASSERT(arena != nullptr,
        "Engine::Initialize: arena is null — Obelisk must initialize MemoryManager first")
    ZENGINE_VALIDATE_ASSERT(Logger::IsInitialized(),
        "Engine::Initialize: Logger not initialized — Obelisk must call Logger::Initialize first")
    ZENGINE_VALIDATE_ASSERT(ThreadPoolHelper::IsInitialized(),
        "Engine::Initialize: ThreadPool not initialized — Obelisk must call ThreadPoolHelper::Initialize first")
    // DO NOT call CrashHandler, Logger, or ThreadPool here — Obelisk owns them.

    // Steps 5 + 6 — OnInitializing + OverrideWindowConfiguration (EXISTING — in GameApplication::Initialize)
    // Called by GameApplication::Initialize before Engine::Initialize is invoked.

    // Step 7 — GameWindow (EXISTING)
    auto context       = CreateRef<EngineContext>();
    context->Window    = CreateRef<GameWindow>();
    ZENGINE_VALIDATE_ASSERT(
        context->Window->Initialize(arena, *window_cfg),
        "Engine::Initialize: GameWindow::Initialize failed")

    // Step 8 — VulkanDevice (EXISTING)
    context->Device = CreateRef<VulkanDevice>();
    ZENGINE_VALIDATE_ASSERT(
        context->Device->Initialize(arena),
        "Engine::Initialize: VulkanDevice::Initialize failed")

    // CRITICAL: All sub-arenas MUST be stored as EngineContext fields (context->VFSArena, etc.)
    // NEVER declare sub-arenas as local variables inside Engine::Initialize —
    // they would be destroyed when the function returns, leaving dangling pointers
    // in all subsystems that were initialized from them.

    // Step 9 — VFS (NEW)
    // VFS sub-arena (stored in EngineContext — lives as long as context):
    arena->CreateSubArena(budget.VFS, &context->VFSArena);
    context->VFS = VFS::VFSDiskContext::Create(&context->VFSArena);
    ZENGINE_VALIDATE_ASSERT(
        context->VFS->Initialize(),
        "Engine::Initialize: VFSDiskContext::Initialize failed")

    // Step 10 — AssetManager (EXISTING, updated to store in context)
    g_asset_manager = CreateRef<AssetManager>(context->Device);
    ZENGINE_VALIDATE_ASSERT(
        g_asset_manager->Initialize(arena),
        "Engine::Initialize: AssetManager::Initialize failed")

    // Step 11 — ECS::Scene (NEW)
    arena->CreateSubArena(budget.ECSScene, &context->ECSArena);
    context->Scene = ECS::Scene::Create(&context->ECSArena);
    ZENGINE_VALIDATE_ASSERT(
        context->Scene->Initialize(),
        "Engine::Initialize: ECS::Scene::Initialize failed")

    // Step 12 — WorldTick (NEW)
    context->WorldTick = ECS::WorldTick::Create(context->Scene);
    ZENGINE_VALIDATE_ASSERT(
        context->WorldTick->Initialize(),
        "Engine::Initialize: WorldTick::Initialize failed")

    // Step 13 — ActorManager (NEW)
    context->ActorManager = ECS::ActorManager::Create(context->Scene, context->WorldTick);
    ZENGINE_VALIDATE_ASSERT(
        context->ActorManager->Initialize(),
        "Engine::Initialize: ActorManager::Initialize failed")

    // Step 14 — AnimationManager (NEW)
    arena->CreateSubArena(budget.AnimationManager, &context->AnimationArena);
    context->AnimMgr = Animation::AnimationManager::Create(&context->AnimationArena, context->Device, context->Scene);
    ZENGINE_VALIDATE_ASSERT(
        context->AnimMgr->Initialize(),
        "Engine::Initialize: AnimationManager::Initialize failed")

    // Step 15 — PhysicsWorld (NEW)
    arena->CreateSubArena(budget.PhysicsWorld, &context->PhysicsArena);
    context->PhysicsWorld = Physics::PhysicsWorld::Create(&context->PhysicsArena, context->Scene);
    ZENGINE_VALIDATE_ASSERT(
        context->PhysicsWorld->Initialize(),
        "Engine::Initialize: PhysicsWorld::Initialize failed")

    // Step 16 — AudioEngine (NEW)
    arena->CreateSubArena(budget.AudioEngine, &context->AudioArena);
    context->AudioEngine = Audio::AudioEngine::Create(&context->AudioArena, context->VFS);
    ZENGINE_VALIDATE_ASSERT(
        context->AudioEngine->Initialize(),
        "Engine::Initialize: AudioEngine::Initialize failed")

    // Step 17 — NetworkSession (conditional) (NEW)
    if (app->RequiresNetworking())
    {
        arena->CreateSubArena(budget.Network, &context->NetworkArena);
        context->NetworkSession = Network::NetworkSession::Create(&context->NetworkArena, context->Scene);
        ZENGINE_VALIDATE_ASSERT(
            context->NetworkSession->Initialize(),
            "Engine::Initialize: NetworkSession::Initialize failed")
    }

    // Expose context to app (EXISTING pattern, extended)
    app->Context           = context;
    app->CurrentWindow     = context->Window;

    // Step 18 — OnInitialized (EXISTING hook — called in GameApplication::Initialize after this returns)

    // Step 19 — WorldTick::Commit (NEW — called in GameApplication::Initialize after OnInitialized)
    // See GameApplication::Initialize for the precise call site.

    // Step 20 — AppRenderPipeline (EXISTING, kept in place)
    app->RenderPipeline = CreateRef<AppRenderPipeline>(context->Device, context->Window);
    ZENGINE_VALIDATE_ASSERT(
        app->RenderPipeline->Initialize(arena),
        "Engine::Initialize: AppRenderPipeline::Initialize failed")

    g_engine_context = context;
    return true;
}
```

`GameApplication::Initialize()` is updated to call `WorldTick::Commit()` after `OnInitialized()` returns:

```cpp
void GameApplication::Initialize(ArenaAllocator* arena)
{
    Arena = arena;
    OnInitializing();
    OverrideWindowConfiguration();
    Engine::Initialize(arena, &WindowCfg, this);   // Steps 1–20 above
    OnInitialized();                               // Step 18: game registers systems
    Context->WorldTick->Commit();                  // Step 19: DAG built
    // Step 20 (AppRenderPipeline) is inside Engine::Initialize, after OnInitialized
    // via the existing placement — see note below.
}
```

Note: `AppRenderPipeline::Initialize()` currently lives inside `Engine::Initialize()`, which runs before `OnInitialized()`. If the render pipeline needs scene data that the game registers in `OnInitialized()`, the pipeline init must be moved to after the `Commit()` call. This is tracked as a deliverable below.

> **IMPORTANT:** `AppRenderPipeline::Initialize()` must be called AFTER `WorldTick::Commit()`.
> The render pipeline builds render graph nodes that may query scene or system state.
> If `Initialize()` is called before `Commit()`, the DAG is incomplete and pass execution
> order is undefined. In the current `Engine::Initialize()` the pipeline init is inside
> the function body; it must be moved to `GameApplication::Initialize()` after the
> `WorldTick::Commit()` call that follows `OnInitialized()`.

---

## 7. Panic / Partial Initialization

### Failure classification

Steps 1–4 (CrashHandler, Logger, ThreadPoolHelper, MemoryManager) are preconditions for all error reporting. A failure in any of these steps cannot be reported through the normal log pipeline. The correct response is an immediate fatal exit with a last-ditch platform write (OutputDebugStringA / write(2)) followed by `_Exit(1)`. No cleanup is attempted because no subsystem has been initialized yet.

Steps 5–22 each have error reporting available (Logger is live). Failure in any of these steps must:
1. Log the error at CRITICAL level.
2. Call the partial-initialization shutdown sequence (all successfully completed steps, in reverse order).
3. Call `_Exit(1)` after cleanup.

### Tracking variable

```cpp
// Engine.cpp (file scope)
static int32_t g_init_step = 0;   // last successfully completed init step (1-indexed)
```

After each successful step, `g_init_step` is incremented. The panic handler reads this value to determine how far to unwind.

```cpp
static void PanicShutdown()
{
    // Unwind in reverse from g_init_step.
    // Each case falls through to the one below it (reverse cascade).
    switch (g_init_step)
    {
    case 22: /* render thread not yet spawned in Initialize; handled in Deinitialize */
             // If render thread was already spawned, join it before releasing GPU resources
             if (g_render_thread.joinable()) {
                 s_request_terminate.store(true, std::memory_order_release);
                 g_render_thread.join();
             }
    case 21: if (g_asset_manager) g_asset_manager->Shutdown();                   [[fallthrough]];
    case 20: if (g_engine_context.RenderPipeline) g_engine_context.RenderPipeline->Shutdown(); [[fallthrough]];
    case 19: /* WorldTick::Commit is not reversible; Scene handles cleanup */     [[fallthrough]];
    case 18: /* OnInitialized: game DLL cleanup — call OnClosing if reachable */  [[fallthrough]];
    case 17: if (g_engine_context.NetworkSession) g_engine_context.NetworkSession->Shutdown(); [[fallthrough]];
    case 16: if (g_engine_context.AudioEngine)    g_engine_context.AudioEngine->Shutdown();    [[fallthrough]];
    case 15: if (g_engine_context.PhysicsWorld)   g_engine_context.PhysicsWorld->Shutdown();   [[fallthrough]];
    case 14: if (g_engine_context.AnimMgr)        g_engine_context.AnimMgr->Shutdown();        [[fallthrough]];
    case 13: if (g_engine_context.ActorManager)   g_engine_context.ActorManager->Shutdown();   [[fallthrough]];
    case 12: if (g_engine_context.WorldTick)      g_engine_context.WorldTick->Shutdown();      [[fallthrough]];
    case 11: if (g_engine_context.Scene)          g_engine_context.Scene->Shutdown();          [[fallthrough]];
    case 10: if (g_asset_manager)                 g_asset_manager->Shutdown();                 [[fallthrough]];
    case  9: if (g_engine_context.VFS)            g_engine_context.VFS->Shutdown();            [[fallthrough]];
    case  8: if (g_engine_context.Device)         g_engine_context.Device->Deinitialize();     [[fallthrough]];
    case  7: if (g_engine_context.Window)         g_engine_context.Window->Deinitialize();     [[fallthrough]];
    case  6: /* OverrideWindowConfiguration: no cleanup */                        [[fallthrough]];
    case  5: /* OnInitializing: no cleanup */                                     [[fallthrough]];
    // Steps 4 (ThreadPool), 3 (Logger), 2 (MemoryManager), 1 (CrashHandler)
    // are owned by Obelisk — cleaned up in applicationEntryPoint after app->Shutdown().
    // PanicShutdown cannot touch them; they may not be in a valid state.
    // Obelisk detects crash via CrashHandler and handles its own teardown.
    case  4: /* ThreadPool — Obelisk-owned, not touched here */                   [[fallthrough]];
    case  3: /* Logger     — Obelisk-owned, not touched here */                   [[fallthrough]];
    case  2: /* Memory     — Obelisk-owned, not touched here */                   [[fallthrough]];
    case  1: /* CrashHandler installed by Obelisk — remains live */               [[fallthrough]];
    default: break;
    }
}
```

Usage:

```cpp
bool Engine::Initialize(ArenaAllocator* arena, WindowConfiguration* window_cfg, GameApplication* app)
{
    CrashHandler::Install();
    g_init_step = 1;

    if (!Logger::Initialize())
    {
        // Logger not live; use platform write
        ZENGINE_PLATFORM_WRITE("Engine::Initialize: Logger::Initialize failed\n");
        PanicShutdown();
        _Exit(1);
    }
    g_init_step = 2;

    // ... subsequent steps follow the same pattern ...
}
```

---

## 8. Deliverables Checklist

- [ ] Add `CrashHandler::Install()` as Step 1 in `Engine::Initialize()`
- [ ] Add `Logger::Initialize()` as Step 2 in `Engine::Initialize()`
- [ ] Add `ThreadPoolHelper::Initialize()` as Step 3 in `Engine::Initialize()`
- [ ] Extend `EngineContext` with all new subsystem pointers (Section 2)
- [ ] Add `VFS::VFSDiskContext` initialization (Step 9)
- [ ] Add `ECS::Scene` initialization (Step 11)
- [ ] Add `ECS::WorldTick` initialization (Step 12)
- [ ] Add `ECS::ActorManager` initialization (Step 13)
- [ ] Add `Animation::AnimationManager` initialization (Step 14)
- [ ] Add `Physics::PhysicsWorld` initialization (Step 15)
- [ ] Add `Audio::AudioEngine` initialization (Step 16)
- [ ] Add `Network::NetworkSession` initialization (Step 17, conditional)
- [ ] Move `WorldTick::Commit()` to `GameApplication::Initialize()` after `OnInitialized()` (Step 19)
- [ ] Confirm `AppRenderPipeline::Initialize()` runs after `WorldTick::Commit()` — move if needed
- [ ] Implement `PanicShutdown()` with `g_init_step` tracking (Section 7)
- [ ] Implement full shutdown sequence in `Engine::Deinitialize()` (Section 4)
- [ ] Add `OnClosing()` call before Step 1 of shutdown
- [ ] Add `OnClosed()` call after Step 17 of shutdown, before Step 18
- [ ] Update `GameApplication::Shutdown()` to call `WorldTick::Shutdown()`, `ActorManager::Shutdown()`, `Scene::Shutdown()` in correct order
- [ ] Memory budget prerequisites: land memory-allocator-audit.md Bugs 1, 3, 4, 9, 13 (P0 fixes) before any new arena carving
