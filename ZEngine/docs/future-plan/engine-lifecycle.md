# ZEngine — Engine Lifecycle

**Priority:** P0 — All subsystems depend on correct initialization order
**Status:** Partially implemented — core startup/shutdown sequence, VFS wiring, and logging/thread pool lifecycle implemented; ECS, animation, physics, audio, and network initialization steps pending (depend on those systems)
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
CrashHandler::Install("Obelisk", "1.0.0", "CrashDumps");   // Step 1 — first line

// Step 2 — CLI parsing (--projectConfigFile, --launchEditor)
// Must precede MemoryManager so the budget preset is known before Initialize is called.

MemoryManager manager = {};
manager.Initialize(                                           // Step 3 — MemoryManager on Obelisk's stack
    ZGiga(3u),
    launch_editor ? MemoryBudgetConfig::Editor()
                  : MemoryBudgetConfig::Default());

ThreadPoolHelper::Initialize();                               // Step 4 — ThreadPool before Logger

ArenaAllocator logger_arena = {};
manager.CreateBudgetedArena(manager.Budget.Logging, &logger_arena);
Logger::Initialize(&logger_arena, logger_cfg);               // Step 5 — Logger gets its own sub-arena

// create app (Tetragrama::Editor or game)
app->Initialize(&manager);   // ← passes MemoryManager*, not a raw ArenaAllocator*
app->Run();
app->Shutdown();
Logger::Dispose();
manager.Shutdown();
CrashHandler::Uninstall();   // last line — already implemented
```

This means:
- `MemoryManager` is **NOT** a singleton inside the engine. It lives on Obelisk's stack
  and a pointer to it is passed into `Engine::Initialize`.
- `Logger::Initialize` and `ThreadPoolHelper::Initialize` are already called by Obelisk
  **before** `Engine::Initialize`. The engine asserts they are live but must not call them again.
- Logger receives a dedicated sub-arena carved via `CreateBudgetedArena(Budget.Logging)`,
  not the main arena directly.
- `app->Initialize` receives a `MemoryManager*`. Engine subsystems access the budget and
  carve their own sub-arenas from it; they do not receive a raw `ArenaAllocator*` directly.
- Sub-arenas for all subsystems are carved from the manager and stored in `EngineContext`
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
    // Existing — Phase 1
    Hardwares::VulkanDevicePtr           Device          = nullptr;
    Windows::CoreWindowPtr               Window          = nullptr;

    // New — Phase 2 + new systems (migration-plan.md)
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

Step  1: CrashHandler::Install("Obelisk", "1.0.0", "CrashDumps")  [OBELISK — DONE]
         Owner: Obelisk/EntryPoint.cpp (first line of applicationEntryPoint)
         Rationale: must be live before malloc, before Logger, before any
                    system that could crash. Cannot be Engine's responsibility
                    because a crash during MemoryManager::Initialize must still
                    produce a minidump.

Step  2: CLI argument parsing + app selection             [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: must happen before MemoryManager so the budget preset (Default vs Editor)
                    is known when manager.Initialize is called. Sets app->ConfigFile from
                    --projectConfigFile and determines whether to create Tetragrama::Editor.

Step  3: MemoryManager::Initialize(ZGiga(3u), config)     [OBELISK — DONE]
         Owner: Obelisk/EntryPoint.cpp (stack-allocated; MemoryManager* passed into app)
         Rationale: all subsequent arenas carved from this block using CreateBudgetedArena.
                    The config selects Default(), Editor(), or Server() budget presets.
                    Obelisk owns the lifetime — MemoryManager is NOT a singleton in the engine.
                    Logger's sub-arena is carved here via CreateBudgetedArena(Budget.Logging)
                    before Logger::Initialize is called.

Step  4: ThreadPoolHelper::Initialize()                   [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: VulkanDevice::Initialize dispatches background GPU work.
                    Must precede Logger so the thread pool is ready before any subsystem
                    that might dispatch to it during initialization.
                    Already called by Obelisk before Engine::Initialize.
                    Engine::Initialize must NOT call it again — only assert it is live.

Step  5: Logger::Initialize(&logger_arena, cfg)           [OBELISK — EXISTS]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: every subsequent step emits ZENGINE_CORE_* calls.
                    Logger receives a dedicated sub-arena carved from the budget
                    via CreateBudgetedArena(Budget.Logging) — not the main arena directly.
                    Must come after MemoryManager (needs the budget arena) and after
                    ThreadPool (logger may dispatch flush work to thread pool workers).
                    Already called by Obelisk before Engine::Initialize.
                    Engine::Initialize must NOT call it again — only assert it is live.

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
                    Depends on Step 9 (surface), Step 4 (thread pool), Step 3 (arena).

Step 11: Core::VFS::VFSContext::Initialize()
         Rationale: path resolution and asset registry required by AssetManager and
                    AudioEngine before they attempt any file I/O.
                    Depends on Step 3 (MemoryManager) — VFS sub-arena carved via
                    CreateBudgetedArena(Budget.VirtualFS). Already implemented.

Step 12: AssetManager::Initialize()
         Rationale: mesh, texture, and material loading required before scene population.
                    Depends on Step 10 (GPU uploads), Step 11 (VFS), Step 2 (sub-arena).

Step 13: ECS::Scene::Initialize()
         Rationale: entity and component storage required by all game systems.
                    ActorManager, WorldTick, and AnimationManager all write into the scene.
                    Depends on Step 2 (ECS sub-arena).

Step 14: ECS::WorldTick::Initialize()
         Rationale: system registration and dependency declaration requires a live scene.
                    Game DLL registers its own systems in OnInitialized() (Step 19),
                    so Commit() must not be called here — only registration infra is set up.
                    Depends on Step 13.

Step 15: ECS::ActorManager::Initialize()
         Rationale: actor lifecycle management requires a live scene and WorldTick.
                    Depends on Steps 13 and 14.

Step 16: Animation::AnimationManager::Initialize()
         Rationale: skeleton and clip data requires live ECS scene for component queries.
                    Depends on Steps 10 (GPU skinning buffers), 13 (scene components),
                    12 (asset loading for animation clips).

Step 17: Physics::PhysicsWorld::Initialize()
         Rationale: Jolt body data requires live ECS scene for transform components.
                    Has no GPU dependency — CPU-only.
                    Depends on Step 13.

Step 18: Audio::AudioEngine::Initialize()
         Rationale: miniaudio state and clip pool require VFS for asset path resolution.
                    Has no GPU dependency — CPU-only.
                    Depends on Step 11 (VFS).

Step 19: Network::NetworkSession::Initialize()   [conditional: multiplayer only]
         Rationale: peer state and snapshot ring buffers require ECS scene for rollback.
                    Skipped entirely in single-player builds.
                    Depends on Step 13.

         Contract: Games optionally override `GameApplication::RequiresNetworking()` which
         returns false by default. If it returns true, Engine::Initialize() allocates the
         network arena and creates the NetworkSession. If network initialization fails, a
         CRITICAL log is emitted and PanicShutdown() is invoked. Single-player games leave
         context->NetworkSession null. All engine code that uses NetworkSession must guard:
         `if (context->NetworkSession) { ... }`

Step 20: GameApplication::OnInitialized()
         Rationale: game DLL entry point (ZGame_Initialize). Registers custom ECS systems,
                    loads the initial scene, sets up UI. All engine subsystems are live.
                    Must precede WorldTick::Commit() so game systems are included in the DAG.

Step 21: ECS::WorldTick::Commit()
         Rationale: builds the execution DAG from all registered systems and their
                    declared Read/Write/After/Before dependencies.
                    Must occur after all system registration (Steps 14 and 20).
                    Must occur before MainThreadRun() dispatches any tick.

Step 22: AppRenderPipeline::Initialize()
         Rationale: render graph compilation references scene components, resource manager
                    handles, and GPU device state. Built after scene and resources are ready.
                    Depends on Steps 10, 13, 21.

Step 23: AssetManager::Run()
         Rationale: starts the async import queue. Placed after AppRenderPipeline so the
                    pipeline can consume freshly imported assets on the first frame.

Step 24: spawn g_render_thread (RenderThreadRun)
         Rationale: render thread accesses AppRenderPipeline, VulkanDevice, GameWindow.
                    All three must be fully initialized.

Step 25: MainThreadRun()   [blocks until shutdown is requested]
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

Step 12: Core::VFS::VFSContext::Shutdown()
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

Step 15: ThreadPoolHelper::Shutdown()                     [OBELISK-OWNED — DONE]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: joins all worker threads.
                    Must occur after app->Shutdown() so all dispatched work is complete.

Step 16: Logger::Flush() + Logger::Dispose()              [OBELISK-OWNED — DONE]
         Owner: Obelisk/EntryPoint.cpp
         Rationale: flushes all pending log entries to disk.
                    Must occur after all engine subsystems have shut down.

Step 17: MemoryManager::Shutdown()                        [OBELISK-OWNED]
         Owner: Obelisk/EntryPoint.cpp (stack variable — destructs at scope exit)
         Rationale: calls free() on the 3 GB block.
                    Must be last; all arena-backed objects are already logically destroyed.

Step 18: CrashHandler::Uninstall()                        [OBELISK-OWNED — DONE]
         Owner: Obelisk/EntryPoint.cpp — last line of applicationEntryPoint
         Rationale: removes signal/exception handlers.
                    Must be absolute last — no crash reporting is possible after this point.
```

`GameApplication::OnClosed()` is called after Step 17 and before Step 18. At that point no arena memory is valid; `OnClosed()` may only use stack-local or OS-allocated resources.

---

## 5. GameApplication Virtual Hooks

The table below maps each virtual hook to the initialization/shutdown step at which it fires and states what the game DLL is permitted to do at that point.

| Hook | Fires at | Engine state at call time | Permitted operations |
|---|---|---|---|
| `OnInitializing()` | Before Step 9 (window) | Logger + CrashHandler + ThreadPool + MemoryManager live | Configure `WindowCfg` fields; set arena size overrides |
| `OverrideWindowConfiguration()` | Between `OnInitializing` and Step 9 | Same as above | Override title, resolution, MSAA, fullscreen flags |
| `OnInitialized()` | After Step 19 (NetworkSession, before Commit) | All subsystems live except WorldTick DAG not yet committed | Register ECS systems; load initial scene; set up UI; bind input |
| `OnUpdate(const Core::TimeStep& ts)` | Each fixed-step iteration inside `MainThreadRun()` | Full engine live | `ts.FixedDeltaSeconds` is the fixed timestep (1/60 s by default). `ts.DeltaSeconds` is the raw frame delta. Do NOT call `WorldTick::Tick()` yourself — the engine calls it before `OnUpdate()`. `Actor::OnTick` receives the same `TimeStep` (use `ts.Alpha` for interpolation). |
| `OnEvent(CoreEvent&)` | On each OS event in `MainThreadRun()` | Full engine live | Handle window resize, key/mouse events, close request |
| `OnPreRender()` | Each frame in `RenderThreadRun()`, before render graph execution | AppRenderPipeline live | Update per-frame uniform buffers; push debug geometry |
| `OnPostRender()` | Each frame in `RenderThreadRun()`, after render graph execution | Swapchain image ready for present | Read-back queries; overlay debug stats |
| `OnRenderUI()` | Each frame in `RenderThreadRun()`, after `OnPostRender()` | UIContext live | Issue UIContext draw calls |
| `OnClosing()` | Before shutdown Step 1 (before terminate flag) | Full engine live | Save game state; flush network; close sockets |
| `OnClosed()` | After shutdown Step 17, before shutdown Step 18 | MemoryManager freed; Logger flushing | OS-level resource release only; no arena use |

---

## 6. Engine::Initialize Rewrite

> **PHASED IMPLEMENTATION — Sprint 1 scope is Steps 6–12 only.**
>
> This code block is the **final target** for `Engine::Initialize`. Not all
> subsystems exist yet. Do not add a step until its subsystem is implemented:
>
> | Steps | Available from |
> |---|---|
> | 6–10 (pre-conditions, Window, Device) | Sprint 1 — implement now |
> | 11 (VFS) | DONE — VFSContext wired in Engine::Initialize |
> | 12 (AssetManager) | Sprint 1 — implement now |
> | 13–15 (Scene, WorldTick, ActorManager) | Sprint 2–3 |
> | 16 (AnimationManager) | Sprint 7 |
> | 17 (PhysicsWorld) | Sprint 6 |
> | 18 (AudioEngine) | Sprint 6 |
> | 19 (NetworkSession) | Sprint 11 |
> | 20–21 (OnInitialized, WorldTick::Commit) | Sprint 3 |
> | 22 (AppRenderPipeline) | Sprint 1 — implement now (already exists) |
> | 23–25 (AssetManager::Run, RenderThread, MainThreadRun) | Sprint 1 — implement now |
>
> Until a subsystem exists, leave its step slot as a commented placeholder so
> `g_init_step` numbering stays stable and `PanicShutdown` case numbers never shift.
>
> **Sprint 1 deliverable:** Steps 6–10, 11, 12, 22–25 wired. Steps 13–21 are
> placeholder comments incrementing `g_init_step` with no-op bodies.

The rewrite below is the complete final form. Lines marked `// EXISTING` are kept verbatim. Lines marked `// NEW` are additions. Lines marked `// PLACEHOLDER — <Sprint N>` must not be implemented until that sprint.

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Obelisk/EntryPoint.cpp owns Steps 1–5 (crash handler, CLI, memory,
// thread pool, logger). These run BEFORE GameApplication::Initialize()
// and BEFORE Engine::Initialize() is called.
//
// Obelisk entry point (actual implementation):
//
//   CrashHandler::Install("Obelisk", "1.0.0", "CrashDumps");   // Step 1 — first line
//   // Step 2 — CLI parsing (--projectConfigFile, --launchEditor)
//   MemoryManager manager = {};
//   manager.Initialize(                                          // Step 3
//       ZGiga(3u),
//       launch_editor ? MemoryBudgetConfig::Editor()
//                     : MemoryBudgetConfig::Default());
//   ThreadPoolHelper::Initialize();                              // Step 4
//   ArenaAllocator logger_arena = {};
//   manager.CreateBudgetedArena(manager.Budget.Logging, &logger_arena);
//   Logger::Initialize(&logger_arena, logger_cfg);              // Step 5
//   // ... app creation ...
//   app->Initialize(&manager);   // → GameApplication::Initialize → Engine::Initialize
//   app->Run();
//   app->Shutdown();
//   Logger::Dispose();
//   manager.Shutdown();
//   CrashHandler::Uninstall();   // last line
// ─────────────────────────────────────────────────────────────────────────────

void Engine::Initialize(MemoryManager* memory, WindowConfiguration* window_cfg, GameApplication* app)
{
    auto& arena = memory->MainArena;

    // Step 6 — Assert pre-conditions (Obelisk must have completed Steps 1–5)
    ZENGINE_VALIDATE_ASSERT(memory != nullptr,
        "Engine::Initialize: memory is null — Obelisk must initialize MemoryManager first")
    ZENGINE_VALIDATE_ASSERT(Logger::IsInitialized(),
        "Engine::Initialize: Logger not initialized — Obelisk must call Logger::Initialize first")
    ZENGINE_VALIDATE_ASSERT(ThreadPoolHelper::IsInitialized(),
        "Engine::Initialize: ThreadPool not initialized — Obelisk must call ThreadPoolHelper::Initialize first")
    // DO NOT call CrashHandler, Logger, or ThreadPool here — Obelisk owns them.

    // Steps 7 + 8 — OnInitializing + OverrideWindowConfiguration (EXISTING — in GameApplication::Initialize)
    // Called by GameApplication::Initialize before Engine::Initialize is invoked.

    // Step 9 — GameWindow (EXISTING)
    auto context    = ZPushStruct(&arena, EngineContext);
    context->Window = ZPushStructCtor(&arena, Windows::GameWindow);
    ZENGINE_VALIDATE_ASSERT(
        context->Window->Initialize(&arena, *window_cfg),
        "Engine::Initialize: GameWindow::Initialize failed")

    // Step 10 — VulkanDevice (EXISTING)
    context->Device = ZPushStructCtor(&arena, Hardwares::VulkanDevice);
    ZENGINE_VALIDATE_ASSERT(
        context->Device->Initialize(&arena),
        "Engine::Initialize: VulkanDevice::Initialize failed")

    // CRITICAL: All sub-arenas MUST be stored as EngineContext fields (context->VFSArena, etc.)
    // NEVER declare sub-arenas as local variables inside Engine::Initialize —
    // they would be destroyed when the function returns, leaving dangling pointers
    // in all subsystems that were initialized from them.

    // Step 11 — VFS (DONE — VFSContext arena-allocated into context->VFSArena)
    memory->CreateBudgetedArena(memory->Budget.VirtualFS, &context->VFSArena);
    auto vfs_ctx = ZPushStructCtor(&context->VFSArena, Core::VFS::VFSContext);
    vfs_ctx->Initialize(&context->VFSArena);
    context->VFS = vfs_ctx;

    // Step 12 — AssetManager (EXISTING)
    ZENGINE_VALIDATE_ASSERT(
        Managers::AssetManager::Initialize(&arena, context->Device, app->WorkingSpacePath),
        "Engine::Initialize: AssetManager::Initialize failed")

    // Step 13 — ECS::Scene (NEW)
    memory->CreateBudgetedArena(memory->Budget.ECSScene, &context->ECSArena);
    context->Scene = ECS::Scene::Create(&context->ECSArena);
    ZENGINE_VALIDATE_ASSERT(
        context->Scene->Initialize(),
        "Engine::Initialize: ECS::Scene::Initialize failed")

    // Step 14 — WorldTick (NEW)
    context->WorldTick = ECS::WorldTick::Create(context->Scene);
    ZENGINE_VALIDATE_ASSERT(
        context->WorldTick->Initialize(),
        "Engine::Initialize: WorldTick::Initialize failed")

    // Step 15 — ActorManager (NEW)
    context->ActorManager = ECS::ActorManager::Create(context->Scene, context->WorldTick);
    ZENGINE_VALIDATE_ASSERT(
        context->ActorManager->Initialize(),
        "Engine::Initialize: ActorManager::Initialize failed")

    // Step 16 — AnimationManager (NEW)
    memory->CreateBudgetedArena(memory->Budget.AnimationManager, &context->AnimationArena);
    context->AnimMgr = Animation::AnimationManager::Create(&context->AnimationArena, context->Device, context->Scene);
    ZENGINE_VALIDATE_ASSERT(
        context->AnimMgr->Initialize(),
        "Engine::Initialize: AnimationManager::Initialize failed")

    // Step 17 — PhysicsWorld (NEW)
    // Note: add PhysicsWorld to MemoryBudgetConfig when Physics subsystem lands (Sprint 6)
    memory->CreateBudgetedArena(memory->Budget.PhysicsWorld, &context->PhysicsArena);
    context->PhysicsWorld = Physics::PhysicsWorld::Create(&context->PhysicsArena, context->Scene);
    ZENGINE_VALIDATE_ASSERT(
        context->PhysicsWorld->Initialize(),
        "Engine::Initialize: PhysicsWorld::Initialize failed")

    // Step 18 — AudioEngine (NEW)
    memory->CreateBudgetedArena(memory->Budget.AudioEngine, &context->AudioArena);
    context->AudioEngine = Audio::AudioEngine::Create(&context->AudioArena, context->VFS);
    ZENGINE_VALIDATE_ASSERT(
        context->AudioEngine->Initialize(),
        "Engine::Initialize: AudioEngine::Initialize failed")

    // Step 19 — NetworkSession (conditional) (NEW)
    if (app->RequiresNetworking())
    {
        memory->CreateBudgetedArena(memory->Budget.Network, &context->NetworkArena);
        context->NetworkSession = Network::NetworkSession::Create(&context->NetworkArena, context->Scene);
        ZENGINE_VALIDATE_ASSERT(
            context->NetworkSession->Initialize(),
            "Engine::Initialize: NetworkSession::Initialize failed")
    }

    // Expose context to app (EXISTING pattern, extended)
    app->CurrentWindow = context->Window;

    // Step 20 — OnInitialized (EXISTING hook — called in GameApplication::Initialize after this returns)

    // Step 21 — WorldTick::Commit (NEW — called in GameApplication::Initialize after OnInitialized)
    // See GameApplication::Initialize for the precise call site.

    // Step 22 — AppRenderPipeline (EXISTING, kept in place)
    app->RenderPipeline = ZPushStructCtor(&arena, Applications::AppRenderPipeline);
    ZENGINE_VALIDATE_ASSERT(
        app->RenderPipeline->Initialize(context->Device),
        "Engine::Initialize: AppRenderPipeline::Initialize failed")

    g_engine_ctx = context;
}
```

`GameApplication::Initialize()` is updated to call `WorldTick::Commit()` after `OnInitialized()` returns:

```cpp
void GameApplication::Initialize(MemoryManager* memory)
{
    Memory = memory;
    OnInitializing();                              // Step 7
    OverrideWindowConfiguration();                 // Step 8
    Engine::Initialize(Memory, &WindowCfg, this);  // Steps 6–22 above
    OnInitialized();                               // Step 20: game registers systems
    Context->WorldTick->Commit();                  // Step 21: DAG built
    // Step 22 (AppRenderPipeline) is inside Engine::Initialize, before OnInitialized
    // — see note below for why it must be moved.
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

Steps 1–5 (CrashHandler, CLI, MemoryManager, ThreadPoolHelper, Logger) are preconditions for all error reporting. A failure in any of these steps cannot be reported through the normal log pipeline. The correct response is an immediate fatal exit with a last-ditch platform write (OutputDebugStringA / write(2)) followed by `_Exit(1)`. No cleanup is attempted because no subsystem has been initialized yet.

Steps 6–27 each have error reporting available (Logger is live). Failure in any of these steps must:
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
    case 24: /* render thread not yet spawned in Initialize; handled in Deinitialize */
             // If render thread was already spawned, join it before releasing GPU resources
             if (g_render_thread.joinable()) {
                 s_request_terminate.store(true, std::memory_order_release);
                 g_render_thread.join();
             }
    case 23: Managers::AssetManager::Shutdown();                                              [[fallthrough]];
    case 22: if (g_app && g_app->RenderPipeline) g_app->RenderPipeline->Shutdown();          [[fallthrough]];
    case 21: /* WorldTick::Commit is not reversible; Scene handles cleanup */                 [[fallthrough]];
    case 20: /* OnInitialized: game DLL cleanup — call OnClosing if reachable */              [[fallthrough]];
    case 19: if (g_engine_ctx->NetworkSession) g_engine_ctx->NetworkSession->Shutdown();     [[fallthrough]];
    case 18: if (g_engine_ctx->AudioEngine)    g_engine_ctx->AudioEngine->Shutdown();        [[fallthrough]];
    case 17: if (g_engine_ctx->PhysicsWorld)   g_engine_ctx->PhysicsWorld->Shutdown();       [[fallthrough]];
    case 16: if (g_engine_ctx->AnimMgr)        g_engine_ctx->AnimMgr->Shutdown();            [[fallthrough]];
    case 15: if (g_engine_ctx->ActorManager)   g_engine_ctx->ActorManager->Shutdown();       [[fallthrough]];
    case 14: if (g_engine_ctx->WorldTick)      g_engine_ctx->WorldTick->Shutdown();          [[fallthrough]];
    case 13: if (g_engine_ctx->Scene)          g_engine_ctx->Scene->Shutdown();              [[fallthrough]];
    case 12: /* AssetManager::Initialize has no individual teardown — Shutdown() at case 23 covers it */ [[fallthrough]];
    case 11: if (g_engine_ctx->VFS)            g_engine_ctx->VFS->Shutdown();                [[fallthrough]];
    case 10: if (g_engine_ctx->Device)         g_engine_ctx->Device->Deinitialize();         [[fallthrough]];
    case  9: if (g_engine_ctx->Window)         g_engine_ctx->Window->Deinitialize();         [[fallthrough]];
    case  8: /* Assert pre-conditions: no cleanup */                              [[fallthrough]];
    case  7: /* OverrideWindowConfiguration: no cleanup */                        [[fallthrough]];
    case  6: /* OnInitializing: no cleanup */                                     [[fallthrough]];
    // Steps 5 (Logger), 4 (ThreadPool), 3 (MemoryManager), 2 (CLI), 1 (CrashHandler)
    // are owned by Obelisk — cleaned up in applicationEntryPoint after app->Shutdown().
    // PanicShutdown cannot touch them; they may not be in a valid state.
    // Obelisk detects crash via CrashHandler and handles its own teardown.
    case  5: /* Logger     — Obelisk-owned, not touched here */                   [[fallthrough]];
    case  4: /* ThreadPool — Obelisk-owned, not touched here */                   [[fallthrough]];
    case  3: /* Memory     — Obelisk-owned, not touched here */                   [[fallthrough]];
    case  2: /* CLI parsing — Obelisk-owned, not touched here */                  [[fallthrough]];
    case  1: /* CrashHandler installed by Obelisk — remains live */               [[fallthrough]];
    default: break;
    }
}
```

Usage:

```cpp
// NOTE: Steps 1–5 (CrashHandler, CLI, MemoryManager, ThreadPool, Logger) are owned by
// Obelisk/EntryPoint.cpp and run BEFORE Engine::Initialize is called.
// g_init_step tracking begins at Step 6 inside Engine::Initialize.

void Engine::Initialize(MemoryManager* manager, WindowConfiguration* window_cfg, GameApplication* app)
{
    // Step 6 — assert pre-conditions (Obelisk owns Steps 1–5)
    ZENGINE_VALIDATE_ASSERT(manager != nullptr, "...");
    ZENGINE_VALIDATE_ASSERT(Logger::IsInitialized(), "...");
    ZENGINE_VALIDATE_ASSERT(ThreadPoolHelper::IsInitialized(), "...");
    g_init_step = 6;

    // Step 7 — OnInitializing (fires in GameApplication::Initialize before this call)
    // Step 8 — OverrideWindowConfiguration (fires in GameApplication::Initialize before this call)

    // Step 9 — GameWindow
    if (!g_engine_ctx->Window->Initialize(&manager->MainArena, *window_cfg))
    {
        ZENGINE_CORE_CRITICAL("Engine::Initialize: GameWindow::Initialize failed");
        PanicShutdown();
        _Exit(1);
    }
    g_init_step = 9;

    // ... subsequent steps follow the same pattern ...
}
```

---

## 8. Deliverables Checklist

- [x] Add `CrashHandler::Install("Obelisk", "1.0.0", "CrashDumps")` as the first line of
      `Obelisk/EntryPoint.cpp` (DONE)
- [x] Add `Logger::Initialize()` in `Obelisk/EntryPoint.cpp` with a dedicated sub-arena
      from `CreateBudgetedArena(Budget.Logging)` (DONE)
- [x] Add `ThreadPoolHelper::Initialize()` in `Obelisk/EntryPoint.cpp` before
      `app->Initialize` (DONE)
- [ ] Extend `EngineContext` with all new subsystem pointers (Section 2)
- [x] Add `Core::VFS::VFSContext` initialization (Step 11) (DONE — wired in Engine::Initialize)
- [x] Add `VFSContext::Shutdown()` call in Engine::Deinitialize() (Step 12 of shutdown) (DONE)
- [x] Fix shutdown order: join render thread before Device/Window teardown (Section 4, Steps 1–14) (DONE)
- [x] Add `OnClosing()` call in `Engine::Run()` before `Deinitialize()`, while all subsystems are live (DONE)
- [x] Add `OnClosed()` call in `EntryPoint.cpp` after `manager.Shutdown()`, before `CrashHandler::Uninstall()` (DONE)
- [x] Add `ThreadPoolHelper::Shutdown()` in `EntryPoint.cpp` (Step 15) (DONE)
- [x] Add `Logger::Flush()` before `Logger::Dispose()` in `EntryPoint.cpp` (Step 16) (DONE)
- [ ] Add `ECS::Scene` initialization (Step 13)
- [ ] Add `ECS::WorldTick` initialization (Step 14)
- [ ] Add `ECS::ActorManager` initialization (Step 15)
- [ ] Add `Animation::AnimationManager` initialization (Step 16)
- [ ] Add `Physics::PhysicsWorld` initialization (Step 17)
- [ ] Add `Audio::AudioEngine` initialization (Step 18)
- [ ] Add `Network::NetworkSession` initialization (Step 19, conditional)
- [ ] Move `WorldTick::Commit()` to `GameApplication::Initialize()` after `OnInitialized()` (Step 21)
- [ ] Confirm `AppRenderPipeline::Initialize()` runs after `WorldTick::Commit()` — move if needed
- [ ] Implement `PanicShutdown()` with `g_init_step` tracking (Section 7)
- [ ] Update `GameApplication::Shutdown()` to call `WorldTick::Shutdown()`, `ActorManager::Shutdown()`, `Scene::Shutdown()` in correct order
- [x] Memory budget prerequisites: land memory-allocator-audit.md Bugs 1, 3, 4, 9, 13 (P0 fixes)
      before any new arena carving (DONE — PRs #497, #531)
