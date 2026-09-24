# Memory Budget

This is the current reference for the CPU arena profile. The production work remaining around enforcement and GPU accounting is tracked in [`future-plan/memory-budget.md`](future-plan/memory-budget.md).

## Root reservation and profiles

`Obelisk/EntryPoint.cpp` initializes `MemoryManager` with an 8 GiB root arena:

```cpp
manager.Initialize(ZGiga(8ULL),
                   launch_editor ? MemoryBudgetConfig::Editor()
                                 : MemoryBudgetConfig::Default());
```

On every platform, the allocator reserves this address range without making it writable, then
promotes only allocation pages. Windows uses `VirtualAlloc(MEM_COMMIT)` and macOS/Linux use
`mprotect` from an initial `PROT_NONE` mapping. The allocator tracks promoted pages across parent
and child arenas, so a root allocation after a child does not make the child's unused range
writable. The 8 GiB root is still virtual address space: a process with an `RLIMIT_AS` below that
reservation cannot start until the tracked ownership model is evolved to independently reserved
owners. This is a CPU-address-space concern, not a GPU-memory requirement.

`MemoryBudgetConfig` has these exact configured totals:

| Profile | Configured total | Difference from root reservation |
|---|---:|---:|
| `Default()` | 7,604 MiB | 588 MiB |
| `Editor()` | 7,732 MiB | 460 MiB |
| `Server()` | 6,324 MiB | 1,868 MiB |

`Editor()` changes `AudioEngine` and `Network` to zero, raises `UIContext` from 64 to 128 MiB, and adds the 256 MiB `EditorContext` owner. `Server()` zeroes `AudioEngine`, `UIContext`, `VulkanDevice`, and `Network`. `MemoryManager::Initialize` validates the selected total before it initializes `MainArena`.

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
| `Swapchain` | 8 MiB | 8 MiB |
| `ShaderCache` | 64 MiB | 64 MiB |
| `Serializer` | 256 MiB | 256 MiB |
| `Network` | 64 MiB | 0 |
| `Input` | 4 MiB | 4 MiB |

The slots are a validated profile, not evidence that every subsystem has already been isolated. Startup creates the bounded `Bootstrap` owner for process-lifetime application/engine state, then creates budgeted arenas for logging, Vulkan device state, VFS, asset management, input, ECS scene data, import pipeline, and UI context. The import-pipeline arena also owns the renderer's bounded worker decode slabs. The editor creates its `EditorContext` arena before configuration loading; it owns the editor scene, tools, and panel layer.

Every production `CreateSubArena` call has an explicit diagnostic owner. Nested owners use a qualified name such as `ImportPipeline/GltfImporter/RuntimeScratch` or `VulkanDevice/RenderGraphFrame`, so allocator failures name both the top-level budget and the local consumer. The four 128 MiB VFS scanner slots are children of `AssetManager`, rather than independently reserved roots. Texture decoding has four 128 MiB leased slabs independent of worker count; each lease is reclaimed after its upload succeeds or is discarded. The only standalone owners are `EditorSceneDeserialized` (a scene must survive the serializer worker's scratch reset) and `EditorDroppedMeshTask` (released by its main-thread completion callback); neither is a declared profile slot and both remain candidates for migration to a bounded owner.

`CreateBudgetedArena` validates a nonzero size, creates the child arena, and registers it with `MemoryProfiler` in profiling builds. `MemoryProfiler` tracks current and peak offsets and emits an 80% watermark warning with a 60-second cooldown. It only sees arenas explicitly registered this way.

## Separate renderer policy

The CPU profile is independent of GPU memory. The persistent IBL/environment resource gate defaults to 384 MiB in `Rendering/EnvironmentLighting.h` and can be supplied by the generated project configuration key `rendering.environment_lighting_budget_mb`. This budget is not CPU arena space, is not serialized scene data, and is not a total-VRAM requirement. Its source and lifecycle are documented in `future-plan/sky-rendering.md`.

Do not update generated `project.json` merely to change this default; make that policy change in ZodiacEngineHub or in the appropriate project-generation flow.
