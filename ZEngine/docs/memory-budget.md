# Memory Budget

This is the current reference for the CPU arena profile. The production work remaining around enforcement and GPU accounting is tracked in [`future-plan/memory-budget.md`](future-plan/memory-budget.md).

## Root reservation and profiles

`Obelisk/EntryPoint.cpp` initializes `MemoryManager` with an 8 GiB root arena:

```cpp
manager.Initialize(ZGiga(8ULL),
                   launch_editor ? MemoryBudgetConfig::Editor()
                                 : MemoryBudgetConfig::Default());
```

On Windows, the allocator reserves this address range and commits child-arena pages lazily. The
current macOS/Linux backend instead creates one writable anonymous `mmap` for the whole range and
marks it fully committed in allocator bookkeeping. Permissive overcommit kernels normally back
physical pages only when touched, so this is not an 8 GiB immediate RSS allocation. It can still
fail at startup on Linux under strict overcommit, an address-space limit, or a container memory
limit because the 8 GiB writable mapping is charged against the applicable commit limit. This is
a current portability gap, not a GPU-memory requirement; its required reserve/commit redesign is
tracked by [`future-plan/memory-budget.md`](future-plan/memory-budget.md).

`MemoryBudgetConfig` has these exact configured totals:

| Profile | Configured total | Difference from root reservation |
|---|---:|---:|
| `Default()` | 7,060 MiB | 1,132 MiB |
| `Editor()` | 6,932 MiB | 1,260 MiB |
| `Server()` | 5,780 MiB | 2,412 MiB |

`Editor()` changes `AudioEngine` and `Network` to zero and raises `UIContext` from 64 to 128 MiB. `Server()` zeroes `AudioEngine`, `UIContext`, `VulkanDevice`, and `Network`. `MemoryManager::Initialize` validates the selected total before it initializes `MainArena`.

## Slot definitions

| Slot | Default | Editor |
|---|---:|---:|
| `AudioEngine` | 128 MiB | 0 |
| `AnimationManager` | 256 MiB | 256 MiB |
| `AssetManager` | 1,024 MiB | 1,024 MiB |
| `ECSScene` | 512 MiB | 512 MiB |
| `Logging` | 8 MiB | 8 MiB |
| `VirtualFS` | 64 MiB | 64 MiB |
| `VulkanDevice` | 1,024 MiB | 1,024 MiB |
| `ImportPipeline` | 3,584 MiB | 3,584 MiB |
| `UIContext` | 64 MiB | 128 MiB |
| `Swapchain` | 8 MiB | 8 MiB |
| `ShaderCache` | 64 MiB | 64 MiB |
| `Serializer` | 256 MiB | 256 MiB |
| `Network` | 64 MiB | 0 |
| `Input` | 4 MiB | 4 MiB |

The slots are a validated profile, not evidence that every subsystem has already been isolated. Startup currently creates budgeted arenas for logging, VFS, asset management, input, ECS scene data, import pipeline, and UI context. Several remaining owners still allocate from `MainArena` or create their own child arena; do not describe those as enforced slots until they are migrated.

`CreateBudgetedArena` validates a nonzero size, creates the child arena, and registers it with `MemoryProfiler` in profiling builds. `MemoryProfiler` tracks current and peak offsets and emits an 80% watermark warning with a 60-second cooldown. It only sees arenas explicitly registered this way.

## Separate renderer policy

The CPU profile is independent of GPU memory. The persistent IBL/environment resource gate defaults to 384 MiB in `Rendering/EnvironmentLighting.h` and can be supplied by the generated project configuration key `rendering.environment_lighting_budget_mb`. This budget is not CPU arena space, is not serialized scene data, and is not a total-VRAM requirement. Its source and lifecycle are documented in `future-plan/sky-rendering.md`.

Do not update generated `project.json` merely to change this default; make that policy change in ZodiacEngineHub or in the appropriate project-generation flow.
