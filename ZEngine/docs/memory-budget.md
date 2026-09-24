# Memory Budget

This is the current reference for the CPU arena profile. The production work remaining around enforcement and GPU accounting is tracked in [`future-plan/memory-budget.md`](future-plan/memory-budget.md).

## Reservation policy and profiles

`Obelisk/EntryPoint.cpp` initializes `MemoryManager` with an 8 GiB configured-capacity limit:

```cpp
manager.Initialize(ZGiga(8ULL),
                   launch_editor ? MemoryBudgetConfig::Editor()
                                 : MemoryBudgetConfig::Default());
```

For a configured run, that value validates the sum of the declared profile but is **not** mapped as
one root arena. Each owner requested through `CreateBudgetedArena` receives its own virtual
reservation. Windows reserves it with `PAGE_NOACCESS`; macOS/Linux use `PROT_NONE`; an allocation
then promotes only its pages with `VirtualAlloc(MEM_COMMIT)` or `mprotect`. Child arenas remain
slices of their named owner, and the owner-local page tracker prevents an allocation following a
child from making that child's unused range writable. `MainArena` exists only for unconfigured
low-level/unit-test use. This is CPU address space, not GPU memory.

`MemoryBudgetConfig` has these exact configured totals:

| Profile | Configured total | Headroom below the configured cap |
|---|---:|---:|
| `Default()` | 7,604 MiB | 588 MiB |
| `Editor()` | 8,132 MiB | 60 MiB |
| `Server()` | 6,324 MiB | 1,868 MiB |

`Editor()` changes `AudioEngine` and `Network` to zero, raises `UIContext` from 64 to 128 MiB, and adds the 256 MiB `EditorContext` owner. `Server()` zeroes `AudioEngine`, `UIContext`, `VulkanDevice`, and `Network`. `MemoryManager::Initialize` validates the selected total before any named owner is reserved.

## Slot definitions

| Slot | Default | Editor |
|---|---:|---:|
| `Bootstrap` | 32 MiB | 32 MiB |
| `AudioEngine` | 128 MiB | 0 |
| `AnimationManager` | 256 MiB | 256 MiB |
| `AssetManager` | 1,024 MiB | 1,024 MiB |
| `ECSScene` | 512 MiB | 512 MiB |
| `Logging` | 8 MiB | 8 MiB |
| `VirtualFS` | 64 MiB | 64 MiB |
| `VulkanDevice` | 1,024 MiB | 1,024 MiB |
| `ImportPipeline` | 4,096 MiB | 4,096 MiB |
| `UIContext` | 64 MiB | 128 MiB |
| `EditorContext` | 0 MiB | 256 MiB |
| `EditorSceneLoadA` | 0 MiB | 200 MiB |
| `EditorSceneLoadB` | 0 MiB | 200 MiB |
| `Swapchain` | 8 MiB | 8 MiB |
| `ShaderCache` | 64 MiB | 64 MiB |
| `Serializer` | 256 MiB | 256 MiB |
| `Network` | 64 MiB | 0 |
| `Input` | 4 MiB | 4 MiB |

The slots are a validated profile, not evidence that every subsystem has already been isolated. Startup creates the bounded `Bootstrap` owner for process-lifetime application/engine state, then creates budgeted arenas for logging, Vulkan device state, VFS, asset management, input, ECS scene data, import pipeline, and UI context. The import-pipeline arena also owns the renderer's bounded worker decode slabs. The editor creates its `EditorContext` arena before configuration loading; it owns the editor scene, tools, and panel layer.

Every production `CreateSubArena` call has an explicit diagnostic owner. Nested owners use a qualified name such as `ImportPipeline/GltfImporter/RuntimeScratch` or `VulkanDevice/RenderGraphFrame`, so allocator failures name both the top-level budget and the local consumer. The four 128 MiB VFS scanner slots are children of `AssetManager`, rather than independently reserved roots. Texture decoding has four 128 MiB leased slabs independent of worker count; each lease is reclaimed after its upload succeeds or is discarded. A dropped-mesh import has one 256 MiB `ImportPipeline/EditorDroppedMeshTask` lease; another drop is rejected until its main-thread completion callback releases it. Scene deserialization leases one of two 200 MiB profiled owners (`EditorSceneLoadA`/`EditorSceneLoadB`) until its `EditorScene` is destroyed, so loading a replacement never invalidates the active scene.

`CreateBudgetedArena` validates a nonzero size, independently reserves the named owner, and registers it with `MemoryProfiler` in profiling builds. `MemoryProfiler` tracks current and peak offsets and emits an 80% watermark warning with a 60-second cooldown. It only sees arenas explicitly registered this way. `MemoryManager` shuts down independently reserved owners in reverse creation order after application, worker, and logger shutdown.

## Separate renderer policy

The CPU profile is independent of GPU memory. The persistent IBL/environment resource gate defaults to 384 MiB in `Rendering/EnvironmentLighting.h` and can be supplied by the generated project configuration key `rendering.environment_lighting_budget_mb`. This budget is not CPU arena space, is not serialized scene data, and is not a total-VRAM requirement. Its source and lifecycle are documented in `future-plan/sky-rendering.md`.

Do not update generated `project.json` merely to change this default; make that policy change in ZodiacEngineHub or in the appropriate project-generation flow.
