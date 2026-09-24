# ZEngine — Memory Budget

**Status:** Partially implemented; this document describes the current CPU-arena contract and the remaining enforcement work.

`MemoryBudgetConfig` lives in `ZEngine/Core/Memory/MemoryManager.h`. The application reserves an 8 GiB root arena in `Obelisk/EntryPoint.cpp`, then selects `Default()` for a game run or `Editor()` for `--launchEditor`. The arena reservation and the entries below are CPU virtual-address / allocator budgets. They are not GPU-VRAM budgets.

## Current implementation

`MemoryManager::Initialize(buffer_size, config)` stores the selected config, validates `config.TotalCommitted() <= buffer_size`, and initializes the root `MainArena`. Windows reserves that range with `PAGE_NOACCESS`; POSIX reserves it with `PROT_NONE`. An allocation promotes only its page range (`VirtualAlloc(MEM_COMMIT)` or `mprotect`), and a root-owned page bitmap keeps parent and child commitments discontiguous. `CreateBudgetedArena(config, result)` carves a named sub-arena and registers it with `MemoryProfiler` in profiling builds. `Shutdown()` is implemented and releases the root arena after application, worker, and logger shutdown.

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
| `Swapchain` | 8 MiB | 8 MiB | declared; not carved through this config |
| `ShaderCache` | 64 MiB | 64 MiB | declared; shader currently makes its own sub-arena |
| `Serializer` | 256 MiB | 256 MiB | declared; serializer currently makes its own sub-arena |
| `Network` | 64 MiB | 0 | declared; not carved during current startup |
| `Input` | 4 MiB | 4 MiB | carved by `Engine::Initialize` |

The exact totals are **7,604 MiB** for `Default()` and **7,732 MiB** for `Editor()`. Both fit inside the 8 GiB root reservation. The earlier 3 GiB root-arena and 1.5 GiB profile figures in this document were obsolete.

`ImportPipeline` is a parent arena. Its present importer allocations include 64 MiB for glTF, 128 MiB for Assimp, 512 MiB for FBX, and 32 MiB for environment-map import. It also owns one 128 MiB TLSF decode slab per worker (up to 16), a 128 MiB fallback slab, and a 2 MiB texture-task slab for `RenderResourceManager`. Importers may create temporary child arenas and clear them between jobs; their individual allocations do not make the whole 4 GiB physically resident by themselves.

## What this does and does not enforce

The startup validation limits the *declared profile*. Process-lifetime application and engine state use the `Bootstrap` owner; Vulkan device state and editor state use their named slots. Every nested production arena now carries a qualified owner name, making allocation failures attributable to its parent budget and local consumer. The VFS scanner's four slots are children of `AssetManager`. Two standalone, non-profile owners remain: `EditorSceneDeserialized` and `EditorDroppedMeshTask`; their lifetimes are respectively the loaded scene and a single dropped-mesh callback. No code should treat unused declared slots as already materialized allocations.

All platforms reserve child address space without making it writable. POSIX no longer requires a
single writable 8 GiB mapping before startup; it promotes allocation pages with `mprotect`.
The root reservation is still address space, so a process with an `RLIMIT_AS` below 8 GiB cannot
admit this root-owner model. That limitation requires the separately tracked decision between a
single root reservation and independently reserved named owners; it is not hidden by
`MAP_NORESERVE`.

`MemoryProfiler::Update()` samples registered arenas, maintains their peak offset, warns above 80% at most once per 60 seconds, and validates a full arena. Only arenas passed to `CreateBudgetedArena` after profiler initialization are tracked. The current warning identifies the arena generically; it does not yet report a per-allocation call stack or impose category-specific GPU limits.

## GPU budgets are separate

Vulkan allocations and render-graph transient resources are not governed by `MemoryBudgetConfig`. In particular, `Rendering::DefaultEnvironmentLightingMemoryBudget` is a separate persistent environment-texture cap of **384 MiB**. `Engine::Initialize` reads the project-level `rendering.environment_lighting_budget_mb` value and supplies it to `VulkanDevice`; invalid or missing values fall back to 384 MiB. This is renderer/project policy, not scene data and not CPU arena capacity. Do not edit generated `project.json` to change the engine default; change the generator or project policy at its owning layer.

The 384 MiB gate covers the persistent environment bake/update peak. It does not represent total GPU memory required by a scene, swapchain, geometry, textures, or render-graph transients. See `sky-rendering.md` for its exact reservation policy.

## Production completion criteria

1. Add Linux coverage under a constrained address-space or commit limit and exercise the
   `mmap`/`mprotect` diagnostics. The allocator now reserves the root with `PROT_NONE`, promotes
   only allocation page ranges, and tracks discontiguous parent/child commitments.
2. Migrate the two documented standalone editor owners (`EditorSceneDeserialized` and `EditorDroppedMeshTask`) to a bounded profile owner, or retain a measured, explicit independent-reservation policy for them.
3. Make profile sizes data-backed: record arena peaks from representative editor and game workloads, then set headroom from those measurements rather than speculative tables.
4. Add a report which distinguishes root/direct allocations, tracked CPU arenas, VMA/driver allocations, persistent environment resources, and render-graph transients. Never combine these as though they share one enforced ceiling.
5. Fail startup with a useful slot-by-slot diagnostic when a profile exceeds the root arena, and add tests for `Default()`, `Editor()`, and `Server()` totals.
6. Establish a GPU-budget policy separately using VMA heap-budget telemetry and allocation-class accounting. A per-category hard cap must be designed and implemented rather than inferred from this CPU configuration.
