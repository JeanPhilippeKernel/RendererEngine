# ZEngine — Memory Budget

**Priority:** P0 — Required before any sub-arena allocation to prevent OOM
**Status:** In Progress
**Modifies:** `MemoryManager.h`, `Engine::Initialize`

---

## 1. Current State

### What MemoryManager.h provides today

`MemoryManager` is a singleton that wraps a single `ArenaAllocator` and exposes it as the engine's global backing store. The entire 2 GB block is allocated in one `malloc` call inside `ArenaAllocator::Initialize(2 * 1024 * 1024 * 1024ULL)`. There is no sub-arena concept at the manager level: callers obtain a region by calling `arena->CreateSubArena(size, &out)` directly on the global allocator, passing whatever size they choose. The manager records neither the names of sub-arenas nor their high-water marks, making it impossible to detect which subsystem is consuming unexpected memory.

The method name `MemoryManager::Shutdowm` contains a typo — it is spelled `Shutdowm` not `Shutdown`. This means any call site that writes the correct spelling (`Shutdown()`) fails to compile, and the wrong spelling is never called at all. The backing 2 GB `malloc` block leaks at process exit unless the `ArenaAllocator` destructor has been fixed per Bug 4 of the memory-allocator-audit.

### What existing call sites already allocate

The following sub-arenas are already carved from the global arena in the current codebase. They are the baseline that the budget table in Section 2 must accommodate:

| Call site | Requested size | Notes |
|---|---|---|
| `AssetManager` backing store | 100 MB | UUID maps, mesh/material/texture arrays |
| `AssetManager` importer scratch | 350 MB | Assimp import scratch; cleared per import job |
| `DeviceSwapchain` | 3 MB | Swapchain-specific objects |
| Shader cache | 5 MB | Current; increased to 16 MB in budget below |
| Scene serializer scratch | 150 MB | Scene save/load temporary buffers |

These five allocations total approximately 608 MB. The remaining ~1,416 MB of the 2 GB block is currently unbudgeted and available to any caller that reaches in with `CreateSubArena`. The budget table below assigns this headroom to concrete subsystems and reserves a safety margin.

---

## 2. Global Arena Budget Table

All sizes are at process startup, allocated once, and never grown. The 2 GB total is a hard ceiling that must not be exceeded. Sub-arenas for optional systems (Network) are only carved when that system is initialized.

| Subsystem | Sub-arena size | Notes |
|---|---|---|
| VulkanDevice | 512 MB | VMA metadata, descriptor pool backing, command pool objects, synchronization primitives |
| AssetManager | 100 MB | UUID maps, mesh/material/texture handle arrays (matches existing) |
| Importer | 350 MB | Assimp import scratch; cleared on `AssetManager::ImportComplete()` (matches existing) |
| ECS::Scene | 128 MB | ComponentStorage dense arrays, EntityRegistry free-list, archetype metadata |
| Animation::AnimationManager | 64 MB | SkeletonData, AnimationClip arrays, per-frame pose pools (double-buffered) |
| Physics::PhysicsWorld | 64 MB | Jolt body data, broad-phase cell grid, constraint cache, contact manifold pool |
| Audio::AudioEngine | 32 MB | miniaudio state, decoded clip pool, stream decode ring buffers |
| VFS (context + registry) | 32 MB | Path cache, mount table, asset UUID-to-path registry, watcher event queue |
| Shader cache | 16 MB | SPIR-V bytecode cache; increased from current 5 MB to accommodate compute shaders |
| UI::UIContext (per-frame) | 8 MB | Widget tree, draw list, retained-mode diff buffers; cleared each frame |
| Network (session + rollback) | 32 MB | Peer state, snapshot ring buffers for rollback; only allocated in multiplayer builds |
| Serializer scratch | 150 MB | Scene save/load temporary buffers (matches existing) |
| Swapchain | 3 MB | Swapchain-specific allocations (matches existing) |
| Logging | 4 MB | Logger ring buffer, event handler list, category filter table |
| Scratch / general | 48 MB | Engine-internal scratch arenas, temporary string processing, debug overlay |
| **Total** | **~1,496 MB** | **~552 MB headroom within the 2 GB block** |

### Sizing rationale

**VulkanDevice (512 MB).** VMA allocates its own GPU-side pools through the driver, but the CPU-side VMA metadata, descriptor pool backing arrays, and per-frame command buffer objects all live in the engine allocator. 512 MB is a conservative upper bound for a scene with 10,000 draw calls, 64 descriptor sets, and 8 frames of latency.

**ECS::Scene (128 MB).** A ComponentStorage dense array for a 16-byte component with 65,536 entities costs 1 MB. With 30–40 component types and sparse archetypes, 128 MB provides comfortable room. The entity registry free-list and archetype metadata add another few MB.

**Importer (350 MB).** Assimp inflates mesh data to approximately 4–8x the on-disk size during processing (vertex deduplication, tangent generation, normal smoothing). A 30 MB mesh file can consume up to 240 MB mid-import. The scratch is cleared after each job completes, so this budget covers the single largest in-flight import, not the cumulative total.

**AnimationManager (64 MB).** A 256-bone skeleton with 60 fps at 10 minutes of animation data (float keyframes) costs roughly 12 MB. With 5 simultaneous skeletons and double-buffered pose pools for interpolation, 64 MB is adequate.

**Serializer scratch (150 MB).** Scene save serializes all component data into a flat binary buffer before writing. A large scene (10,000 actors × average 300 bytes of component data) requires ~3 MB. 150 MB provides headroom for the text-format intermediate representation used by the editor.

---

## 3. GPU Memory Budget

GPU memory is managed entirely by VMA (Vulkan Memory Allocator) and is separate from the CPU arena. The table below is a planning reference for `vmaSetBudget` hints and for flagging when a given resource class is over-allocated at runtime. All sizes assume 4K output resolution.

| Resource class | Estimated budget | Notes |
|---|---|---|
| Shadow maps — CSM (4 cascades × 2048×2048 × D32_SFLOAT) | 64 MB | 4 × (2048 × 2048 × 4 bytes) = 64 MB |
| Shadow maps — spot lights (4 lights × 1024×1024 × D32_SFLOAT) | 16 MB | 4 × (1024 × 1024 × 4 bytes) = 16 MB |
| Shadow maps — point lights (2 lights × 512×512 cubemap × D32_SFLOAT) | 12 MB | 2 × 6 faces × (512 × 512 × 4 bytes) ≈ 12 MB |
| Post-process render targets (HDR color + bloom mips + SSAO + LUT) | 48 MB | 4K HDR (R16G16B16A16) = 32 MB; 5 bloom mips ≈ 8 MB; SSAO half-res ≈ 4 MB; LUT 32x32x32 ≈ 4 MB |
| Thumbnail cache (LRU, 512 slots × 128×128×RGBA8) | 32 MB | 512 × (128 × 128 × 4 bytes) = 32 MB |
| GPU skinning bone matrices (max 10,000 entities × 256 bones) | 40 MB | 10,000 × 256 × 16 bytes (mat4 row) = 40 MB |
| Vertex and index buffers | 512 MB | Scene geometry; budget covers a dense open-world scene |
| Texture atlas | 512 MB | All game textures compressed (BC7/BC5); covers ~1,000 unique 2K textures |
| **Total GPU estimate** | **~1,296 MB** | Well within a 4 GB VRAM target; leaves ~2.7 GB for driver overhead and OS |

### Notes on GPU budget enforcement

VMA does not enforce per-category budgets automatically. The engine should call `vmaGetHeapBudgets()` each frame in debug builds and compare current usage per resource type against these targets. When a category exceeds 90% of its budget, log a WARNING via `MemoryProfiler`. VMA budget hints (`VmaPool` with `blockSize`) can be set per resource category to produce hard allocation failures rather than silent overruns.

---

### When Initialize is called

`MemoryManager::Initialize()` is called by the application entry point BEFORE
`GameApplication::Initialize()` and BEFORE `Engine::Initialize()`. The typical
call site is in `main()` or the platform entry point:

```cpp
    Core::Memory::MemoryManager memory_manager;
    memory_manager.Initialize(Core::Memory::MemoryBudgetConfig::Default());
    
    MyGame app;
    app.Initialize(&memory_manager.Allocator);
```

This ensures the 2GB block is allocated and the budget is validated before any
subsystem attempts to carve a sub-arena. If total committed bytes exceed the
arena size, `Initialize()` asserts and exits before any GPU or window work starts.

---

## 4. MemoryBudgetConfig Struct

`MemoryBudgetConfig` is the central definition of all CPU sub-arena sizes. It is passed to `Engine::Initialize()` and used to carve every sub-arena from the global allocator. Callers may construct a custom config to override individual sizes (e.g., a server build that omits audio and reduces the animation budget).

```cpp
namespace ZEngine::Memory
{

struct SubArenaConfig
{
    cstring  Name      = nullptr;   // used by MemoryProfiler for tracking
    uint64_t SizeBytes = 0;
};

struct MemoryBudgetConfig
{
    SubArenaConfig VulkanDevice;
    SubArenaConfig ECSScene;
    SubArenaConfig AssetManager;
    SubArenaConfig Importer;
    SubArenaConfig AnimationManager;
    SubArenaConfig PhysicsWorld;
    SubArenaConfig AudioEngine;
    SubArenaConfig VirtualFS;
    SubArenaConfig ShaderCache;
    SubArenaConfig UIContext;
    SubArenaConfig Network;
    SubArenaConfig Serializer;
    SubArenaConfig Swapchain;
    SubArenaConfig Logging;
    SubArenaConfig Input;

    // Returns the budget matching Section 2 of memory-budget.md.
    static MemoryBudgetConfig Default();

    // Direct member modification is the intended API for custom configs:
    //   auto cfg = MemoryBudgetConfig::Default();
    //   cfg.AudioEngine.SizeBytes = 0;      // server build: no audio
    //   cfg.Network.SizeBytes    = ZMega(64); // larger network buffer
    //   MemoryManager::Initialize(cfg);
    //
    // All modifications must happen before passing to Initialize().
    // cfg.Validate() asserts if total committed exceeds 2GB.

    // Returns a reduced budget for dedicated server builds (no GPU, no audio, no UI).
    static MemoryBudgetConfig Server();

    // Returns a reduced budget for tool / editor builds (no audio, no network).
    static MemoryBudgetConfig Editor();

    // Validates that the sum of all SizeBytes fields does not exceed total_available_bytes.
    // Returns false and logs the overage if the budget is exceeded.
    bool Validate(uint64_t total_available_bytes) const;

    // Returns the total bytes committed by all SubArenaConfig entries.
    uint64_t TotalCommitted() const;
};

inline MemoryBudgetConfig MemoryBudgetConfig::Default()
{
    MemoryBudgetConfig cfg = {};
    cfg.VulkanDevice       = { "VulkanDevice",       512ULL * 1024 * 1024 };
    cfg.AssetManager       = { "AssetManager",       100ULL * 1024 * 1024 };
    cfg.Importer           = { "Importer",           350ULL * 1024 * 1024 };
    cfg.ECSScene           = { "ECSScene",           128ULL * 1024 * 1024 };
    cfg.AnimationManager   = { "AnimationManager",    64ULL * 1024 * 1024 };
    cfg.PhysicsWorld       = { "PhysicsWorld",        64ULL * 1024 * 1024 };
    cfg.AudioEngine        = { "AudioEngine",         32ULL * 1024 * 1024 };
    cfg.VirtualFS          = { "VirtualFS",           32ULL * 1024 * 1024 };
    cfg.ShaderCache        = { "ShaderCache",         16ULL * 1024 * 1024 };
    cfg.UIContext          = { "UIContext",             8ULL * 1024 * 1024 };
    cfg.Network            = { "Network",             32ULL * 1024 * 1024 };
    cfg.Serializer         = { "Serializer",         150ULL * 1024 * 1024 };
    cfg.Swapchain          = { "Swapchain",            3ULL * 1024 * 1024 };
    cfg.Logging            = { "Logging",              4ULL * 1024 * 1024 };
    cfg.Input              = { "Input",                1ULL * 1024 * 1024 };
    return cfg;
    // Total: ~1,496 MB out of 2,048 MB; ~552 MB headroom
}

} // namespace ZEngine::Memory
```

`MemoryManager::CreateBudgetedArena` carves a sub-arena of `config.SizeBytes` from the global allocator and registers it with `MemoryProfiler`:

```cpp
void MemoryManager::CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* out)
{
    ZENGINE_VALIDATE_ASSERT(config.SizeBytes > 0,
        "MemoryManager::CreateBudgetedArena: SizeBytes must be > 0")
    ZENGINE_VALIDATE_ASSERT(out != nullptr,
        "MemoryManager::CreateBudgetedArena: out must not be null")

    Allocator.CreateSubArena(config.SizeBytes, out);

#if ZENGINE_PROFILING
    MemoryProfiler::TrackArena(config.Name, out);
#endif
}
```

---

## 5. Watermark Tracking

`MemoryProfiler` (defined in `profiling.md`) integrates with the memory budget via arena registration. The following behavior applies in debug and profiling builds (`ZENGINE_ENABLE_PROFILING` defined):

### Registration

Each sub-arena is registered immediately after `CreateBudgetedArena` returns:

```cpp
MemoryProfiler::TrackArena(config.Name, out_arena);
```

`TrackArena` stores a pointer to the arena alongside its name. It does not copy the arena — it observes it.

### Per-frame sampling

`MemoryProfiler::SampleArenas()` is called once per frame from `MainThreadRun()`. For each registered arena it reads `m_current_offset` and `m_total_size` and computes the utilization ratio.

### Warning threshold

When `current_offset > 0.80 * total_size` in a debug or profiling build, `MemoryProfiler` emits:

```
[MEMORY] Arena 'ECSScene' at 82.4% capacity (105 MB / 128 MB)
```

This warning fires at most once per 60-second window per arena to avoid log spam during scene loading spikes.

The watermark warning fires at most once per 60-second wall-clock window per arena
to prevent log spam during scene loading spikes. Each tracked arena stores:
```cpp
    bool    m_has_warned           = false;
    int64_t m_last_warning_time_ns = 0;
```
On each sample, use a bool flag to avoid INT64_MIN subtraction overflow (signed UB):
```cpp
// Use a bool flag to avoid INT64_MIN subtraction overflow (signed UB).
if (current_offset > 0.8f * total_size &&
    (!m_has_warned || (now_ns - m_last_warning_time_ns) > 60'000'000'000LL)) {
    ZENGINE_CORE_WARN("Arena '%s': watermark at %.0f%% (%zu / %zu bytes)",
                      name, 100.0f * current_offset / total_size,
                      current_offset, total_size);
    m_last_warning_time_ns = now_ns;
    m_has_warned = true;
}
```
Use `std::chrono::steady_clock::now().time_since_epoch().count()` for `now_ns`.

### Hard assert threshold

When `current_offset >= total_size`, `MemoryProfiler` calls `ZENGINE_VALIDATE_ASSERT(false, ...)` immediately — before the allocator itself would fail. This surfaces the OOM condition with a named arena in the message:

```
ZENGINE_VALIDATE_ASSERT: Arena 'ECSScene' is full (128 MB / 128 MB) — increase budget
```

In release builds, the watermark warning is omitted but the `ZENGINE_VALIDATE_ASSERT` in `ArenaAllocator::Allocate` (Bug 1 fix from memory-allocator-audit.md) still fires, producing a crash with context.

### Profiling UI

The `profiling.md` overlay renders a horizontal bar per arena showing current utilization and the 80% warning threshold line. Arenas above 80% are shown in orange; the bar turns red when the arena is full.

---

## 6. MemoryManager Fixes

The following changes to `MemoryManager.h` are required before the budget system can be integrated. Each item references the memory-allocator-audit.md bug number where applicable.

### Fix 1: Typo `Shutdowm` -> `Shutdown` (audit Bug 14)

```cpp
// Before:
void Shutdowm()
{
    Allocator.Shutdown();
}

// After:
void Shutdown()
{
    Allocator.Shutdown();
}
```

All call sites that used the typo spelling must be updated. Any call site that used the correct spelling `Shutdown()` was already failing to compile — those call sites become valid after this fix.

### Fix 2: Add `CreateBudgetedArena` helper

```cpp
// MemoryManager.h

void CreateBudgetedArena(const Memory::SubArenaConfig& config, ArenaAllocator* out);
```

```cpp
// MemoryManager.cpp

void MemoryManager::CreateBudgetedArena(const Memory::SubArenaConfig& config, ArenaAllocator* out)
{
    ZENGINE_VALIDATE_ASSERT(config.SizeBytes > 0,
        "MemoryManager::CreateBudgetedArena: SizeBytes must be > 0")
    ZENGINE_VALIDATE_ASSERT(out != nullptr,
        "MemoryManager::CreateBudgetedArena: out must not be null")
    Allocator.CreateSubArena(config.SizeBytes, out);
#if ZENGINE_PROFILING
    MemoryProfiler::TrackArena(config.Name, out);
#endif
}
```

### Fix 3: Add `m_owns_memory` flag to `ArenaAllocator` (audit Bug 9)

This is a prerequisite for `CreateBudgetedArena` to be safe. Sub-arenas carved by `CreateSubArena` point into the middle of the parent's `malloc` block. If the sub-arena's destructor or `Shutdown()` calls `free()` on that pointer, the heap is corrupted.

```cpp
// Allocator.h
struct ArenaAllocator
{
    // ... existing fields ...
    bool m_owns_memory = false;   // true only when Initialize() called malloc
};
```

Full implementation is in memory-allocator-audit.md Bug 9.

### Fix 4: Validate budget before carving (new)

`MemoryManager::Initialize` should validate that `MemoryBudgetConfig::Default().TotalCommitted()` is less than the total arena size before any sub-arena is carved:

```cpp
void MemoryManager::Initialize(uint64_t size, const Memory::MemoryBudgetConfig& budget)
{
    ZENGINE_VALIDATE_ASSERT(budget.Validate(size),
        "MemoryManager::Initialize: budget exceeds total arena size")
    Allocator.Initialize(size);
}
```

If `budget.Validate()` fails, `Engine::Initialize` exits at Step 4 before any Vulkan or window work has started, producing a clean error with the overage amount.

---

## 7. Deliverables Checklist

> **PREREQUISITE:** The following bugs from `memory-allocator-audit.md` MUST be fixed
> before this system is implemented. Without them, sub-arena allocation is unsafe:
>   - Bug 1:  `assert` → `ZENGINE_VALIDATE_ASSERT` in `Allocate` (no-op in release)
>   - Bug 3:  null-check on `malloc` in `Initialize`
>   - Bug 4:  double-free guard in `Shutdown` + safe destructor
>   - Bug 9:  `m_owns_memory` flag (sub-arenas must not call `free()`)
>   - Bug 13: `is_power_of_two(0)` returns true
>
> Verify `memory-allocator-audit.md` exists and these bugs are landed before proceeding.

### Prerequisites from memory-allocator-audit.md (must land first)

All bugs fixed — see PRs #497 and #531.

- [x] **Bug 1** — `assert` replaced with `ZENGINE_VALIDATE_ASSERT` in `ArenaAllocator::Allocate`
- [x] **Bug 3** — Switched from `malloc` to `mmap`/`VirtualAlloc` with null-check in `Initialize`
- [x] **Bug 4** — `Shutdown` guarded against double-call; destructor calls `Shutdown`
- [x] **Bug 9** — `m_is_sub_arena` flag added; sub-arenas skip `free` in `Shutdown`
- [x] **Bug 13** — `is_power_of_two` fixed: `(x != 0) && ((x & (x-1)) == 0)`

### Memory budget deliverables

- [x] Fix typo `Shutdowm` -> `Shutdown` in `MemoryManager.h` (Section 6, Fix 1)
- [x] Add `CreateBudgetedArena(const SubArenaConfig&, ArenaAllocator* out)` to `MemoryManager` (Section 6, Fix 2)
- [x] Add `MemoryBudgetConfig` struct with `Default()`, `Server()`, `Editor()`, `Validate()`, `TotalCommitted()` (Section 4)
- [x] Add `SubArenaConfig` struct (Section 4)
- [x] `MemoryManager::Initialize` accepts `MemoryBudgetConfig` and calls `config.Validate(buffer_size)` before carving any sub-arena (Section 6, Fix 4)
- [x] Integrate `MemoryProfiler::TrackArena` inside `CreateBudgetedArena` — called automatically via `#if ZENGINE_PROFILING` guard (Section 5)
- [x] Implement 80% watermark warning in `MemoryProfiler::Update()` with 60s cooldown (Section 5)
- [x] Implement full-arena `ZENGINE_VALIDATE_ASSERT` in `MemoryProfiler::Update()` (Section 5)
- [x] `MemoryProfiler::Initialize` called from `MemoryManager::Initialize` — wires arena allocator into `s_arenas` before any `TrackArena` call
- [x] `MemoryBudgetConfig::Server()` zeroes out `AudioEngine`, `UIContext`, `VulkanDevice`, `Network` fields
- [x] `MemoryBudgetConfig::Editor()` zeroes out `AudioEngine`, `Network`; increases `UIContext` to 32 MB
- [ ] Replace all ad-hoc `arena->CreateSubArena(hardcoded_size, &out)` call sites with `MemoryManager::CreateBudgetedArena(cfg.FieldName, &out)` (engine-lifecycle.md Section 6)
- [ ] Add arena utilization bars to profiling overlay (profiling.md integration point)
- [ ] Document arena lifetime rules: no pointer into any sub-arena is valid after `MemoryManager::Shutdown()`
