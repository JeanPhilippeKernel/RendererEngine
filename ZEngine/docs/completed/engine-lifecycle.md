# ZEngine — Engine Lifecycle

**Status:** Core lifecycle is implemented. This document is the current lifecycle
contract and identifies the remaining production hardening; it is not a proposal
for a separate application bootstrap API.

## Process bootstrap

`Obelisk/EntryPoint.cpp` owns process-scoped state. Its current order is:

1. Install the crash handler.
2. Parse `--projectConfigFile` and `--launchEditor`.
3. Initialize the stack-owned `MemoryManager` with an 8 GiB configured-capacity
   limit and `MemoryBudgetConfig::Default()` or `Editor()`. Configured owners are
   independently reserved; startup does not map an 8 GiB root arena.
4. Initialize `MemoryProfiler` and track the `Bootstrap` owner in profiling builds.
5. Initialize the thread pool.
6. Create the budgeted logging arena and initialize the logger.
7. Create the application, initialize it, run it, then shut it down.
8. Join worker threads; flush/dispose the logger; call `OnClosed()`; release
   the memory manager; uninstall the crash handler.

`MemoryManager` is not an engine singleton. The entry point owns it and passes
a pointer through `GameApplication::Initialize` to `Engine::Initialize`.
The 8 GiB number is a configured-capacity limit, not immediate physical memory
or a GPU-memory budget. Each configured owner reserves its own address range
without making it writable, which permits a process below an 8 GiB `RLIMIT_AS`
when its concrete owners fit; see the [memory-budget reference](../memory-budget.md).

## Application and engine initialization

`GameApplication::Initialize(memory)` first stores the memory manager and
allocates `ApplicationState` from its `Bootstrap` owner. It then calls:

```text
OnInitializing()
OverrideWindowConfiguration()
Engine::Initialize(memory, &WindowCfg, this)
mount project VFS backend at /
scan project files when that mount succeeds
AppRenderPipeline::Initialize(device)
OnInitialized()
```

`Engine::Initialize` requires initialized memory, logger, and thread pool. Its
current dependency order is:

```text
EngineContext / GameWindow
  -> VulkanDevice
  -> VFS and engine-assets mount
  -> optional writable /cache/pso mount and cache load
  -> AssetManager
  -> InputManager
  -> ECS Scene, ActorManager, WorldCommands, WorldTick
  -> component reflection registration
  -> import and UI arenas; importer registration
  -> renderer project settings (geometry and environment-lighting budgets)
  -> RenderResourceManager and fallback texture
  -> project watcher setup
  -> GLFW scroll callback and MainThreadScheduler
```

The currently materialized engine-context arenas are VFS, asset, input, ECS,
import pipeline, and UI context. Do not claim that every
`MemoryBudgetConfig` slot is initialized here; several slots remain declared
profile capacity only.

The engine mounts its own asset backend at `/ZodiacEngine` with lower priority
than the project backend. The PSO cache is enabled only when a writable project
workspace can be mounted at `/cache/pso`; otherwise startup continues with
disk-cache persistence disabled.

## Run and shutdown

`Engine::Run` starts the render thread, runs the main thread, calls
`GameApplication::OnClosing()` while the engine resources still exist, then
calls `Engine::Deinitialize()`.

`Deinitialize()` sets the termination flag and joins the render thread before
tearing down render-owned resources. It then shuts down the main-thread
scheduler, actor manager, scene, render resource manager, asset manager, render
pipeline, VFS (after persisting PSO caches when available), Vulkan device, and
window. After `Run()` returns, `GameApplication::Shutdown()` pauses the
camera controller and calls `Engine::Dispose()`; process-level thread, logger,
and arena teardown remains the entry point's responsibility.

The exact order matters:

- the render thread stops before the resource manager, pipeline, device, or
  window are destroyed;
- PSO persistence is saved while VFS and the device cache are still live;
- the device outlives render resources and the window/surface teardown;
- `OnClosing()` is the last application hook allowed to use live engine state,
  while `OnClosed()` must use only stack/OS resources.

## Production hardening still required

1. Make initialization failure-safe. Every successfully initialized subsystem
   needs a recorded cleanup stage, and a failure must unwind only the stages
   that completed.
2. Remove remaining direct root-arena allocations or make their ownership and
   budget exceptions explicit.
3. Add lifecycle tests for normal exit, failed device initialization, disabled
   disk-cache persistence, and a device-lost render-loop exit.
4. Guard optional pointers consistently during teardown. In particular,
   application-owned pipeline and controller assumptions must be validated
   before a partial-startup path reaches them.
5. Keep editor scene transactions, selection state, and immutable render
   snapshots above this lifecycle: they are application/editor state, not new
   global engine singletons.
