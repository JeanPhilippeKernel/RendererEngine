# ZEngine — Memory Budget

**Status:** Partially implemented; this document describes the current CPU-arena contract and the remaining enforcement work.

`MemoryBudgetConfig` lives in `ZEngine/Core/Memory/MemoryManager.h`. The application supplies an 8 GiB configured-capacity limit in `Obelisk/EntryPoint.cpp`, then selects `Default()` for a game run or `Editor()` for `--launchEditor`. The entries below are CPU virtual-address / allocator budgets. They are not GPU-VRAM budgets.

## Current implementation

`MemoryManager::Initialize(buffer_size, config)` stores the selected config and validates `config.TotalCapacity() <= buffer_size`. On an overrun, validation prints the requested/allowed/overage values and every named configuration slot before terminating; this works before the logger starts and is included in the assertion report. Configured application owners are independently reserved: Windows uses `PAGE_NOACCESS`, POSIX uses `PROT_NONE`, and an allocation promotes only its page range (`VirtualAlloc(MEM_COMMIT)` or `mprotect`). Each owner owns a page bitmap, keeping its parent and child commitments discontiguous. `CreateBudgetedArena(config, result)` reserves a named owner independently and registers it with `MemoryProfiler` in profiling builds. `MainArena` is now used only by unconfigured low-level/unit-test callers. `Shutdown()` releases independent owners in reverse creation order after application, worker, and logger shutdown.

The current profiles total the following maximum reservations:

| Slot | Default | Editor | Current consumer status |
|---|---:|---:|---|
| `Bootstrap` | 32 MiB | 32 MiB | carved by `MemoryManager::Initialize`; owns process-lifetime application and engine bootstrap state |
| `AudioEngine` | 128 MiB | 0 | declared; not carved during current startup |
| `AnimationManager` | 256 MiB | 256 MiB | declared; not carved during current startup |
| `AssetManager` | 1 GiB | 1 GiB | carved by `Engine::Initialize` |
| `ECSScene` | 512 MiB | 512 MiB | carved by `Engine::Initialize` |
| `Logging` | 8 MiB | 8 MiB | carved in `Obelisk/EntryPoint.cpp` |
| `VirtualFS` | 64 MiB | 64 MiB | carved by `Engine::Initialize` |
| `VulkanDevice` | 1 GiB | 1 GiB | carved by `Engine::Initialize` |
| `ImportPipeline` | 4 GiB | 4 GiB | carved by `Engine::Initialize`; owns importer arenas and renderer decode slabs |
| `UIContext` | 64 MiB | 128 MiB | carved by `Engine::Initialize` |
| `EditorContext` | 0 | 256 MiB | carved by `Tetragrama::Editor::OnInitializing`; owns the editor scene and tools |
| `EditorSceneLoadA` | 0 | 200 MiB | carved by `Engine::Initialize`; first bounded deserialization lease |
| `EditorSceneLoadB` | 0 | 200 MiB | carved by `Engine::Initialize`; second bounded deserialization lease |
| `Swapchain` | 8 MiB | 8 MiB | declared; not carved through this config |
| `ShaderCache` | 64 MiB | 64 MiB | declared; shader currently makes its own sub-arena |
| `Serializer` | 256 MiB | 256 MiB | declared; serializer currently makes its own sub-arena |
| `Network` | 64 MiB | 0 | declared; not carved during current startup |
| `Input` | 4 MiB | 4 MiB | carved by `Engine::Initialize` |

The exact totals are **7,604 MiB** for `Default()` and **8,132 MiB** for `Editor()`. Both fit below the 8 GiB configured capacity limit. The earlier 3 GiB root-arena and 1.5 GiB profile figures in this document were obsolete.

`ImportPipeline` is a parent arena. Its present importer allocations include 64 MiB for glTF, 128 MiB for Assimp, 512 MiB for FBX, and 128 MiB for the serialized environment-map import slab. `RenderResourceManager` owns four 128 MiB texture-decode slabs, a 2 MiB task slab, and 4 MiB of synchronous LUT scratch. A decode lease remains occupied until its pixels upload or are discarded, so this 512 MiB bound does not vary with CPU worker count. Importers may create temporary child arenas and clear them between jobs; their individual allocations do not make the whole 4 GiB physically resident by themselves.

## What this does and does not enforce

The startup validation limits the *declared profile*. Process-lifetime application and engine state use the `Bootstrap` owner; Vulkan device state and editor state use their named slots. Every nested production arena now carries a qualified owner name, making allocation failures attributable to its parent budget and local consumer. The VFS scanner's four slots are children of `AssetManager`; a dropped mesh uses one 256 MiB `ImportPipeline/EditorDroppedMeshTask` lease until its main-thread callback releases it. Deserialization leases one of two profiled 200 MiB scene owners until its `EditorScene` is destroyed. No code should treat unused declared slots as already materialized allocations.

All platforms reserve top-level and child address space without making it writable. POSIX promotes
allocation pages with `mprotect`. Configured startup no longer creates a single 8 GiB mapping, so
an `RLIMIT_AS` below 8 GiB can admit the concrete owners that fit beneath it. The Linux test suite
spawns a child with `RLIMIT_AS` limited to its existing address space plus 128 MiB, then starts a
manager with an 8 GiB configured capacity limit and independently reserves a 4 MiB bootstrap and
32 MiB named owner. This exercises the `mmap` and `mprotect` path under a meaningful limit without
relying on `MAP_NORESERVE`.

`MemoryProfiler::Update()` samples registered arenas at the main-thread frame boundary, maintains their peak offset, warns above 80% at most once per 60 seconds, and validates a full arena. Only arenas passed to `CreateBudgetedArena` after profiler initialization are tracked. The current warning identifies the arena generically; it does not yet report a per-allocation call stack or impose category-specific GPU limits.

## GPU budgets are separate

Vulkan allocations and render-graph transient resources are not governed by `MemoryBudgetConfig`. In particular, `Rendering::DefaultEnvironmentLightingMemoryBudget` is a separate persistent environment-texture cap of **384 MiB**. `Engine::Initialize` reads the project-level `rendering.environment_lighting_budget_mb` value and supplies it to `VulkanDevice`; invalid or missing values fall back to 384 MiB. This is renderer/project policy, not scene data and not CPU arena capacity. Do not edit generated `project.json` to change the engine default; change the generator or project policy at its owning layer.

The 384 MiB gate covers the persistent environment bake/update peak. It does not represent total GPU memory required by a scene, swapchain, geometry, textures, or render-graph transients. See `sky-rendering.md` for its exact reservation policy.

## Production completion criteria

1. Make profile sizes data-backed: record arena peaks from representative editor and game workloads, then set headroom from those measurements rather than speculative tables.
2. The editor profiler reports named CPU arenas, VMA/driver samples, persistent environment resources, and render-graph transients in separate sections. Configured runs have no shared root/direct arena; Bootstrap is reported as a named CPU owner. Never combine these sections as though they share one enforced ceiling.
3. Startup reports a useful slot-by-slot diagnostic when a profile exceeds the configured capacity limit; tests cover all built-in totals and the diagnostic's named owners.
4. Establish a GPU-budget policy separately using VMA heap-budget telemetry and allocation-class accounting. A per-category hard cap must be designed and implemented rather than inferred from this CPU configuration.
