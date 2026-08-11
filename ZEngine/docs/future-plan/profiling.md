# ZEngine — Profiling and Instrumentation

**Priority:** P3 — Required to diagnose performance issues in shipped builds
**Status:** Partially implemented — memory profiling, CPU profiling macros, and profiler buffer introduced; full profiling UI pending
**Depends on:** Nothing (can be implemented first, improves all other work)
**Blocks:** Nothing critical, but without it performance regressions are invisible

**Goal**: Implement a zero-overhead-when-disabled profiling layer for ZEngine that covers
CPU zones, GPU timestamp queries, per-frame memory watermarks, and an in-game debug overlay.
Tracy is the primary profiler backend; ZEngine provides its own lightweight `ScopedTimer`
fallback when Tracy is not linked. All instrumentation macros expand to nothing in Release
builds. No exceptions. No `new`/`delete` in instrumented paths. No virtual dispatch in the
hot path.

---

## 1. Design Philosophy

### Zero-cost when disabled

Every instrumentation point is guarded by `ZENGINE_PROFILING`. When `ZENGINE_PROFILING == 0`
(the Release configuration), every profiling macro expands to a no-op at the preprocessor
level — the compiler sees nothing, the linker strips nothing, and there is literally zero
overhead. No dead branches, no disabled virtual calls, no flag checks.

```
Debug / RelWithDebInfo → ZENGINE_PROFILING = 1
Release                → ZENGINE_PROFILING = 0
```

### Tracy as the primary backend

[Tracy Profiler](https://github.com/wolfpld/tracy) (MIT license) is the industry-standard
frame profiler for C++ game engines (used by id Software, Remedy, Bungie, and others). It
provides:

- Zone-based CPU profiling with microsecond resolution.
- Frame timeline visualization.
- Memory allocation tracking.
- GPU timestamp zones integrated with Vulkan.
- A network-connected viewer that connects to a live process.
- Single-header instrumentation (`tracy/Tracy.hpp`).

Tracy is vendored under `ZEngine/vendor/tracy`. It is an opt-in dependency controlled by
`ZENGINE_TRACY` (default ON in Debug/RelWithDebInfo, OFF in Release).

### Lightweight fallback (`ScopedTimer`)

When `ZENGINE_PROFILING == 1` but Tracy is absent (`ZENGINE_TRACY == 0`), ZEngine falls back
to its own `ScopedTimer` — a stack-allocated RAII timer that records `(name, duration_ns)`
into a ring buffer called `ProfilerBuffer`. This fallback is the source of data for the
in-game debug overlay and requires no external dependency.

### No virtual dispatch, no allocation in hot paths

- All macro implementations use concrete types resolved at compile time.
- `ScopedTimer` is stored entirely on the stack.
- `ProfilerBuffer` is a fixed-size ring written with a simple atomic index increment.
- GPU queries use a pre-allocated `VkQueryPool`.

---

## 2. Profiling Macros

### Header

```cpp
// ZEngine/Profiling/Profiling.h
#pragma once

// ---------------------------------------------------------------------------
// Configuration: set by CMake based on build type.
//   ZENGINE_PROFILING  — 1 in Debug/RelWithDebInfo, 0 in Release
//   ZENGINE_TRACY      — 1 when Tracy client is linked (requires ZENGINE_PROFILING=1)
// ---------------------------------------------------------------------------

#if ZENGINE_PROFILING && ZENGINE_TRACY
    #include <tracy/Tracy.hpp>
    #include <tracy/TracyVulkan.hpp>
#endif

namespace ZEngine::Profiling {
    class GPUProfiler;   // forward-declared; used by ZENGINE_PROFILE_GPU_SCOPE
}

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_SCOPE(name)
//   CPU zone from the current line to the end of the enclosing scope.
//   `name` must be a string literal.
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    #define ZENGINE_PROFILE_SCOPE(name)  ZoneScopedN(name)
#elif ZENGINE_PROFILING
    #define ZENGINE_PROFILE_SCOPE(name)  \
        ZEngine::Profiling::ScopedTimer _ze_timer_##__LINE__(name)
#else
    #define ZENGINE_PROFILE_SCOPE(name)
#endif

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_FUNCTION()
//   Zone named after the current function (__FUNCTION__).
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    #define ZENGINE_PROFILE_FUNCTION()   ZoneScoped
#elif ZENGINE_PROFILING
    #define ZENGINE_PROFILE_FUNCTION()   \
        ZEngine::Profiling::ScopedTimer _ze_fn_timer_##__LINE__(__FUNCTION__)
#else
    #define ZENGINE_PROFILE_FUNCTION()
#endif

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_FRAME(name)
//   Mark a frame boundary. Call exactly once per main loop iteration.
//   `name` is the frame label shown in the Tracy timeline.
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    #define ZENGINE_PROFILE_FRAME(name)  FrameMarkNamed(name)
#elif ZENGINE_PROFILING
    #define ZENGINE_PROFILE_FRAME(name)  ZEngine::Profiling::ProfilerBuffer::BeginFrame()
#else
    #define ZENGINE_PROFILE_FRAME(name)
#endif

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_THREAD(name)
//   Name the current thread. Call once at thread start.
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    #define ZENGINE_PROFILE_THREAD(name) tracy::SetThreadName(name)
#elif ZENGINE_PROFILING
    // Lightweight fallback: no thread naming (not visible in ProfilerBuffer).
    #define ZENGINE_PROFILE_THREAD(name)
#else
    #define ZENGINE_PROFILE_THREAD(name)
#endif

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_VALUE(name, value)
//   Plot a float value. Useful for entity counts, memory, draw calls, etc.
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    #define ZENGINE_PROFILE_VALUE(name, value)  TracyPlot(name, static_cast<double>(value))
#elif ZENGINE_PROFILING
    #define ZENGINE_PROFILE_VALUE(name, value)  \
        ZEngine::Profiling::ProfilerBuffer::RecordValue(name, static_cast<float>(value))
#else
    #define ZENGINE_PROFILE_VALUE(name, value)
#endif

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_ALLOC(ptr, size)
// ZENGINE_PROFILE_FREE(ptr)
//   Notify the profiler of heap allocations and frees (for memory tracking).
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    #define ZENGINE_PROFILE_ALLOC(ptr, size)  TracyAlloc(ptr, size)
    #define ZENGINE_PROFILE_FREE(ptr)         TracyFree(ptr)
#elif ZENGINE_PROFILING
    // Lightweight fallback: allocation tracking via MemoryProfiler.
    #define ZENGINE_PROFILE_ALLOC(ptr, size)  \
        ZEngine::Profiling::MemoryProfiler::RecordAlloc(ptr, size)
    #define ZENGINE_PROFILE_FREE(ptr)         \
        ZEngine::Profiling::MemoryProfiler::RecordFree(ptr)
#else
    #define ZENGINE_PROFILE_ALLOC(ptr, size)
    #define ZENGINE_PROFILE_FREE(ptr)
#endif

// ---------------------------------------------------------------------------
// ZENGINE_PROFILE_GPU_SCOPE(cmd, name)
//   GPU timestamp zone on a Vulkan command buffer.
//   `cmd`  — VkCommandBuffer currently being recorded.
//   `name` — string literal label for the zone.
// ---------------------------------------------------------------------------
#if ZENGINE_PROFILING && ZENGINE_TRACY
    // TracyVkZone requires a TracyVkCtx created at device init.
    // ZEngine stores the context in GPUProfiler::GetTracyContext().
    #define ZENGINE_PROFILE_GPU_SCOPE(cmd, name) \
        TracyVkZone(ZEngine::Profiling::GPUProfiler::GetTracyContext(), cmd, name)
#elif ZENGINE_PROFILING
    #define ZENGINE_PROFILE_GPU_SCOPE(cmd, name) \
        ZEngine::Profiling::GPUScopedZone _ze_gpu_##__LINE__(cmd, name)
#else
    #define ZENGINE_PROFILE_GPU_SCOPE(cmd, name)
#endif
```

### Usage examples

```cpp
// Frame boundary — call once per main loop tick:
ZENGINE_PROFILE_FRAME("MainThread");

// Scope zone — named literal:
void RenderGraph::Execute() {
    ZENGINE_PROFILE_SCOPE("RenderGraph::Execute");
    // ...
}

// Function zone — auto-named:
void ECS::Scene::ForEach() {
    ZENGINE_PROFILE_FUNCTION();
    // ...
}

// Value plot — useful for monitoring:
ZENGINE_PROFILE_VALUE("ECS/EntityCount", float(m_entity_count));
ZENGINE_PROFILE_VALUE("Memory/ArenaECS", float(m_ecs_arena.m_current_offset));

// GPU zone — inside command buffer recording:
void ShadowPass::Execute(VkCommandBuffer cmd) {
    ZENGINE_PROFILE_GPU_SCOPE(cmd, "ShadowPass");
    // vkCmdDraw...
}
```

---

## 3. Tracy Integration

### CMakeLists snippet

```cmake
# ZEngine/CMakeLists.txt

option(ZENGINE_TRACY "Enable Tracy profiler" OFF)

# Auto-enable Tracy in Debug and RelWithDebInfo:
if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
    set(ZENGINE_TRACY ON CACHE BOOL "Tracy enabled in debug builds" FORCE)
endif()

if(ZENGINE_TRACY)
    # Tracy requires exactly one translation unit to include TracyClient.cpp.
    # We add it as a static library to avoid ODR issues.
    add_library(TracyClient STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tracy/public/TracyClient.cpp
    )
    target_include_directories(TracyClient PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tracy/public
    )
    target_compile_definitions(TracyClient PUBLIC
        TRACY_ENABLE        # Enable Tracy instrumentation macros
        TRACY_ON_DEMAND     # Connect only when the viewer is attached; otherwise no-op
    )
    # Tracy requires pthreads on Linux/macOS
    if(UNIX)
        target_link_libraries(TracyClient PUBLIC pthread dl)
    endif()

    target_link_libraries(ZEngine PRIVATE TracyClient)
    target_compile_definitions(ZEngine PRIVATE
        ZENGINE_TRACY=1
        ZENGINE_PROFILING=1
    )
else()
    target_compile_definitions(ZEngine PRIVATE
        ZENGINE_TRACY=0
    )
endif()

# Always set ZENGINE_PROFILING based on build type, regardless of Tracy:
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(ZEngine PRIVATE ZENGINE_PROFILING=0)
elseif(NOT ZENGINE_TRACY)
    # Lightweight fallback profiling in non-Tracy debug builds
    target_compile_definitions(ZEngine PRIVATE ZENGINE_PROFILING=1)
endif()
```

### `TRACY_ON_DEMAND` behaviour

With `TRACY_ON_DEMAND`, Tracy instruments the program but only transmits data when the
Tracy viewer is connected over the network. When no viewer is connected, the overhead is
a single atomic check per zone entry — unmeasurable in practice. This is the recommended
setting for development builds that ship to QA or other team members who may not run the
viewer on every session.

### Tracy Vulkan context initialisation

```cpp
// ZEngine/Profiling/GPUProfiler.cpp (Tracy path)
#if ZENGINE_PROFILING && ZENGINE_TRACY

#include <tracy/TracyVulkan.hpp>

static tracy::VkCtx* s_tracy_vk_ctx = nullptr;

void GPUProfiler::InitTracy(VkPhysicalDevice physical_device,
                             VkDevice         device,
                             VkQueue          graphics_queue,
                             VkCommandBuffer  init_cmd) {
    s_tracy_vk_ctx = TracyVkContext(physical_device, device,
                                    graphics_queue, init_cmd);
    ZENGINE_VALIDATE_ASSERT(s_tracy_vk_ctx != nullptr,
        "TracyVkContext creation failed");
}

tracy::VkCtx* GPUProfiler::GetTracyContext() {
    return s_tracy_vk_ctx;
}

void GPUProfiler::DestroyTracy() {
    if (s_tracy_vk_ctx) {
        TracyVkDestroy(s_tracy_vk_ctx);
        s_tracy_vk_ctx = nullptr;
    }
}

#endif  // ZENGINE_PROFILING && ZENGINE_TRACY
```

---

## 4. ZEngine Lightweight Fallback — `ScopedTimer` and `ProfilerBuffer`

Used when `ZENGINE_PROFILING == 1` and Tracy is not linked. All types live in
`ZEngine/Profiling/`.

### `ProfilerSample`

```cpp
// ZEngine/Profiling/ProfilerBuffer.h
#pragma once
#include <cstdint>
#include <Core/Containers/Array.h>

namespace ZEngine::Profiling {

    struct ProfilerSample {
        const char* Name;          // string literal; not heap-allocated
        uint64_t    DurationNs;    // nanosecond duration of the zone
        uint64_t    FrameIndex;    // which frame this sample belongs to
    };

    struct ProfilerValue {
        const char* Name;
        float       Value;
        uint64_t    FrameIndex;
    };

}  // namespace ZEngine::Profiling
```

### `ProfilerBuffer`

```cpp
// ZEngine/Profiling/ProfilerBuffer.h (continued)
#pragma once
#include <atomic>

namespace ZEngine::Profiling {

    // Fixed-capacity ring buffer for CPU profiling samples.
    // Thread-safe for concurrent writers (multiple threads can call Record()).
    // Single-reader: DumpLastFrame is called from the render thread only.
    class ProfilerBuffer {
    public:
        static constexpr uint32_t CAPACITY = 4096;  // max samples per frame

        // Write a completed sample. Thread-safe via atomic index.
        // Drops the sample silently if the buffer is full (rare in practice).
        static void Record(const char* name, uint64_t duration_ns);

        // Write a float value plot. Thread-safe.
        static void RecordValue(const char* name, float value);

        // Advance the frame counter. Call once per frame from the main thread
        // before any profiling work for the new frame.
        static void BeginFrame();

        // Copy all samples recorded during the *previous* frame into `out`.
        // Safe to call from any thread as long as it does not race with BeginFrame.
        static void DumpLastFrame(Core::Containers::Array<ProfilerSample>& out);

        // Same as DumpLastFrame but for value plots.
        static void DumpLastFrameValues(Core::Containers::Array<ProfilerValue>& out);

        static uint64_t CurrentFrameIndex();

    private:
        static ProfilerSample       s_samples[CAPACITY];
        static ProfilerValue        s_values[CAPACITY];
        static std::atomic<uint32_t> s_sample_write_pos;
        static std::atomic<uint32_t> s_value_write_pos;
        static std::atomic<uint64_t> s_frame_index;
        static uint32_t              s_last_frame_sample_count;
        static uint32_t              s_last_frame_value_count;
        // Snapshot of previous frame's data (copied atomically at BeginFrame).
        static ProfilerSample        s_last_samples[CAPACITY];
        static ProfilerValue         s_last_values[CAPACITY];
    };

}  // namespace ZEngine::Profiling
```

### `ProfilerBuffer` implementation notes

- `Record` uses `s_sample_write_pos.fetch_add(1, std::memory_order_relaxed)` to claim a
  slot. If the claimed index exceeds `CAPACITY`, the write is silently dropped.
- `BeginFrame` copies the write-pos snapshot to `s_last_frame_sample_count`, resets the
  write-pos to 0 with `std::memory_order_seq_cst`, increments `s_frame_index`, and bulk-
  copies `s_samples[0..last_count]` into `s_last_samples`. This is the only point where
  the last-frame snapshot is updated.
- `DumpLastFrame` reads only from `s_last_samples` — the snapshot. It never reads from
  the live ring while new samples are being written.

### `ScopedTimer`

```cpp
// ZEngine/Profiling/ScopedTimer.h
#pragma once
#include <cstdint>
#include <Profiling/ProfilerBuffer.h>

namespace ZEngine::Profiling {

    // RAII timer. Constructed at the start of a zone, destructed at the end.
    // Measures wall-clock time via std::chrono::steady_clock.
    // Zero heap allocation — lives entirely on the stack.
    struct ScopedTimer {
        const char* Name;
        uint64_t    StartNs;

        explicit ScopedTimer(const char* name) noexcept
            : Name(name)
            , StartNs(NowNs())
        {}

        ~ScopedTimer() noexcept {
            uint64_t duration = NowNs() - StartNs;
            ProfilerBuffer::Record(Name, duration);
        }

        ScopedTimer(const ScopedTimer&)            = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        static uint64_t NowNs() noexcept;
        // Implementation: std::chrono::steady_clock::now().time_since_epoch().count()
        // Cast to uint64_t nanoseconds. Inlined.
    };

}  // namespace ZEngine::Profiling
```

### `NowNs` implementation

```cpp
// ZEngine/Profiling/ScopedTimer.cpp
#include <Profiling/ScopedTimer.h>
#include <chrono>

namespace ZEngine::Profiling {

    uint64_t ScopedTimer::NowNs() noexcept {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            steady_clock::now().time_since_epoch().count()
        );
        // steady_clock::period is nanoseconds on all major platforms
        // (GCC/Clang/MSVC with std::chrono). If not, a static_assert should guard this.
    }

}  // namespace ZEngine::Profiling
```

---

## 5. Required Instrumentation Points

The following call sites **must** have `ZENGINE_PROFILE_SCOPE` or the appropriate macro
inserted before the first implementation commit that touches each subsystem.

### Engine main loop

```cpp
// ZEngine/Engine/Engine.cpp
void Engine::MainThreadRun() {
    ZENGINE_PROFILE_THREAD("MainThread");
    while (!m_should_quit) {
        ZENGINE_PROFILE_FRAME("MainThread");
        {
            ZENGINE_PROFILE_SCOPE("Engine::PollEvents");
            PollEvents();
        }
        {
            ZENGINE_PROFILE_SCOPE("Engine::Update");
            Update();
        }
        {
            ZENGINE_PROFILE_SCOPE("Engine::Render");
            Render();
        }
    }
}
```

### WorldTick / ECS scheduler

```cpp
// ZEngine/ECS/WorldTick.cpp
void WorldTick::Tick(float dt) {
    ZENGINE_PROFILE_SCOPE("WorldTick::Tick");
    for (uint32_t wave = 0; wave < m_wave_count; ++wave) {
        ZENGINE_PROFILE_SCOPE("WorldTick::Wave");  // each wave is a parallel dispatch
        for (auto& system : m_waves[wave]) {
            ZENGINE_PROFILE_SCOPE(system->GetName());
            system->Tick(dt);
        }
    }
}

// ZEngine/ECS/Scene.h
template<typename... Components, typename Func>
void Scene::ForEach(Func&& fn) {
    ZENGINE_PROFILE_SCOPE("ECS::Scene::ForEach");
    // iteration body...
}
```

### Vulkan device submission

```cpp
// ZEngine/Hardwares/VulkanDevice.cpp
void VulkanDevice::Submit(VkSubmitInfo* infos, uint32_t count, VkFence fence) {
    ZENGINE_PROFILE_SCOPE("VulkanDevice::Submit");
    ZENGINE_VALIDATE_ASSERT(infos != nullptr, "VulkanDevice::Submit: null submit info");
    vkQueueSubmit(m_graphics_queue, count, infos, fence);
}
```

### RenderGraph

```cpp
// ZEngine/Rendering/Graph/RenderGraph.cpp
void RenderGraph::Compile() {
    ZENGINE_PROFILE_SCOPE("RenderGraph::Compile");
    // dependency sort, resource aliasing...
}

void RenderGraph::Execute(VkCommandBuffer cmd) {
    ZENGINE_PROFILE_SCOPE("RenderGraph::Execute");
    for (auto& pass : m_sorted_passes) {
        ZENGINE_PROFILE_SCOPE(pass->GetName());
        pass->Execute(cmd, *this);
    }
}
```

### Asset manager

```cpp
// ZEngine/Managers/AssetManager.cpp
AssetHandle AssetManager::Load(const VFS::VFSPath& path) {
    ZENGINE_PROFILE_SCOPE("AssetManager::Load");
    // ...
}

void AssetManager::FlushPendingLoads() {
    ZENGINE_PROFILE_SCOPE("AssetManager::FlushPendingLoads");
    // ...
}
```

### Arena allocator

```cpp
// ZEngine/Core/Memory/ArenaAllocator.cpp
void* ArenaAllocator::Allocate(uint64_t size, uint64_t alignment) {
    ZENGINE_PROFILE_ALLOC(ptr, size);  // after the pointer is determined
    // ... bump logic ...
    return ptr;
}
```

**Note**: `ZENGINE_PROFILE_ALLOC` tracks arena allocations in Tracy's memory view. In the
lightweight fallback, `MemoryProfiler` aggregates these into per-arena watermark data. The
macro is placed _after_ the pointer is computed so `ptr` is valid.

### Post-process passes

Post-process passes use the DOD free-function pattern — `Execute` is a standalone function,
not a class method. The profiling macros follow the same one-CPU-scope + one-GPU-scope rule:

```cpp
// BloomPass_Execute — free function registered in PostProcessPassVtable.
void BloomPass_Execute(PostProcessPassData& data,
                       VkCommandBuffer      cmd,
                       const Graph::RenderGraph& rg)
{
    ZENGINE_PROFILE_SCOPE("BloomPass_Execute");
    ZENGINE_PROFILE_GPU_SCOPE(cmd, "GPU::BloomPass");
    // ...
}
```

All post-process Execute free functions follow the same pattern: one CPU scope and one GPU
scope per call. The scope name matches the free-function name for easy Tracy lookup.

### Shadow passes

Shadow passes use the same DOD free-function pattern:

```cpp
void ShadowPassDir_Execute(PostProcessPassData& data,
                           VkCommandBuffer      cmd,
                           const Graph::RenderGraph& rg)
{
    ZENGINE_PROFILE_SCOPE("ShadowPassDir_Execute");
    ZENGINE_PROFILE_GPU_SCOPE(cmd, "GPU::ShadowPassDir");
    for (uint32_t cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade) {
        ZENGINE_PROFILE_SCOPE("ShadowPassDir_Cascade");
        ZENGINE_PROFILE_GPU_SCOPE(cmd, "GPU::ShadowCascade");
        // ...
    }
}
```

### Audio engine

```cpp
void AudioEngine::Tick(float dt) {
    ZENGINE_PROFILE_SCOPE("AudioEngine::Tick");
    // ...
}
```

### Complete instrumentation requirement table

| Call site | Macro | Notes |
|-----------|-------|-------|
| `Engine::MainThreadRun` — frame | `ZENGINE_PROFILE_FRAME` | Once per loop |
| `Engine::MainThreadRun` — per-phase | `ZENGINE_PROFILE_SCOPE` | PollEvents, Update, Render |
| `WorldTick::Tick` — whole tick | `ZENGINE_PROFILE_SCOPE` | |
| `WorldTick::Tick` — per wave | `ZENGINE_PROFILE_SCOPE` | |
| `WorldTick::Tick` — per system | `ZENGINE_PROFILE_SCOPE(system->GetName())` | |
| `ECS::Scene::ForEach` | `ZENGINE_PROFILE_SCOPE` | Template — only once |
| `VulkanDevice::Submit` | `ZENGINE_PROFILE_SCOPE` | |
| `RenderGraph::Compile` | `ZENGINE_PROFILE_SCOPE` | |
| `RenderGraph::Execute` — whole | `ZENGINE_PROFILE_SCOPE` | |
| `RenderGraph::Execute` — per pass | `ZENGINE_PROFILE_SCOPE(pass->GetName())` | |
| `AssetManager::Load` | `ZENGINE_PROFILE_SCOPE` | |
| `AssetManager::FlushPendingLoads` | `ZENGINE_PROFILE_SCOPE` | |
| `ArenaAllocator::Allocate` | `ZENGINE_PROFILE_ALLOC` | After ptr is valid |
| Each `*Pass_Execute` free function (post-process) | `ZENGINE_PROFILE_SCOPE` + `ZENGINE_PROFILE_GPU_SCOPE` | |
| Each shadow pass `*Pass_Execute` free function | `ZENGINE_PROFILE_SCOPE` + `ZENGINE_PROFILE_GPU_SCOPE` | |
| `AudioEngine::Tick` | `ZENGINE_PROFILE_SCOPE` | |

---

## 6. GPU Timestamp Queries — `GPUProfiler`

### Purpose

`ZENGINE_PROFILE_GPU_SCOPE` needs a Vulkan `VkQueryPool` to record timestamps at the
start and end of each GPU zone. `GPUProfiler` manages this pool, maps query indices to
zone names, and reads back results after the frame fence signals.

### Header

```cpp
// ZEngine/Profiling/GPUProfiler.h
#pragma once
#include <vulkan/vulkan.h>
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Profiling {

    struct GPUZone {
        const char* Name;         // string literal label
        float       DurationMs;   // millisecond duration, computed from timestamp delta
    };

    class GPUProfiler {
    public:
        // Call once after VkDevice is created.
        // max_queries_per_frame: total timestamp queries per frame. Each GPU zone uses 2
        // (begin + end). Typical: 512 (= 256 zones per frame).
        void Initialize(Hardwares::VulkanDevice* device,
                        uint32_t max_queries_per_frame);

        void Destroy();

        // Record a begin-timestamp into `cmd`. Returns the query index pair base.
        // The caller stores this index; EndZone takes the same index.
        // Call within a command buffer before the work to be measured.
        uint32_t BeginZone(VkCommandBuffer cmd, const char* name);

        // Record the end-timestamp for the zone started at `query_index`.
        void EndZone(VkCommandBuffer cmd, uint32_t query_index);

        // Reset the query pool for the current frame. Call at the start of each
        // frame's command buffer recording (before any BeginZone calls).
        void ResetFrame(VkCommandBuffer cmd);

        // Read timestamp results back from the GPU. Call after the frame fence signals
        // (i.e., the GPU has finished all commands for that frame). Blocks only if the
        // fence has not signalled yet — caller must ensure it has.
        void CollectResults(VkDevice device);

        // Retrieve the collected zones from the last CollectResults call.
        const Core::Containers::Array<GPUZone>& GetLastFrameZones() const;

        // Tracy Vulkan context accessor (no-op when Tracy is disabled).
        static tracy::VkCtx* GetTracyContext();
        static void InitTracy(VkPhysicalDevice physical, VkDevice device,
                              VkQueue queue, VkCommandBuffer init_cmd);
        static void DestroyTracy();

    private:
        // Whether GPU timestamp queries are supported on this device.
        // Set to false in Initialize() if timestampComputeAndGraphics is not supported.
        // All BeginZone/EndZone/CollectResults calls are no-ops when m_enabled is false.
        bool                     m_enabled             = false;
        VkQueryPool              m_query_pool          = VK_NULL_HANDLE;
        VkDevice                 m_device              = VK_NULL_HANDLE;
        double                   m_timestamp_period_ns = 1.0;  // from VkPhysicalDeviceLimits
        uint32_t                 m_max_queries         = 0;
        uint32_t                 m_next_query_index    = 0;

        struct ZoneEntry {
            const char* Name;
            uint32_t    QueryBegin;
            uint32_t    QueryEnd;
        };

        Core::Containers::Array<ZoneEntry> m_pending_zones;   // being recorded this frame
        Core::Containers::Array<GPUZone>   m_last_zones;      // results from last frame

        Core::Memory::ArenaAllocator*      m_arena = nullptr;
    };

    // Process-global singleton. Initialised at engine startup.
    GPUProfiler& GetGPUProfiler();

}  // namespace ZEngine::Profiling
```

### `Initialize`

```cpp
void GPUProfiler::Initialize(Hardwares::VulkanDevice* device,
                              uint32_t max_queries_per_frame) {
    ZENGINE_VALIDATE_ASSERT(device != nullptr, "GPUProfiler: null device");
    m_max_queries = max_queries_per_frame;
    m_device      = device->GetVkDevice();

    // Retrieve nanoseconds-per-timestamp-tick from device limits.
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device->GetVkPhysicalDevice(), &props);

    // Verify timestamp query support before creating the pool.
    // On some platforms (certain mobile GPUs, integrated graphics),
    // vkCmdWriteTimestamp is not supported in compute/graphics pipelines.
    if (!props.limits.timestampComputeAndGraphics) {
        ZENGINE_CORE_WARN("GPUProfiler: GPU does not support timestamp queries "
                          "in compute/graphics pipelines. GPU profiling disabled.");
        m_enabled = false;
        return;
    }
    m_timestamp_period_ns = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo pool_info{};
    pool_info.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    pool_info.queryCount = max_queries_per_frame;

    VkResult result = vkCreateQueryPool(m_device, &pool_info, nullptr, &m_query_pool);
    ZENGINE_VALIDATE_ASSERT(result == VK_SUCCESS, "GPUProfiler: failed to create query pool");

    m_enabled = true;
    m_pending_zones.Reserve(max_queries_per_frame / 2);
    m_last_zones.Reserve(max_queries_per_frame / 2);
}
```

### `BeginZone` / `EndZone`

```cpp
uint32_t GPUProfiler::BeginZone(VkCommandBuffer cmd, const char* name) {
    if (!m_enabled) return 0;  // GPU profiling not supported on this device
    ZENGINE_VALIDATE_ASSERT(m_next_query_index + 1 < m_max_queries,
        "GPUProfiler: query pool exhausted — increase max_queries_per_frame");

    uint32_t idx = m_next_query_index;
    m_next_query_index += 2;  // reserve begin + end

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_query_pool, idx);

    m_pending_zones.PushBack({ name, idx, idx + 1 });
    return idx;
}

void GPUProfiler::EndZone(VkCommandBuffer cmd, uint32_t query_index) {
    if (!m_enabled) return;  // GPU profiling not supported on this device
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        m_query_pool, query_index + 1);
}
```

### `GPUScopedZone` (lightweight fallback RAII wrapper)

```cpp
// ZEngine/Profiling/GPUProfiler.h (inline)
struct GPUScopedZone {
    VkCommandBuffer cmd;
    uint32_t        query_index;

    GPUScopedZone(VkCommandBuffer c, const char* name) noexcept
        : cmd(c)
        , query_index(GetGPUProfiler().BeginZone(c, name))
    {}

    ~GPUScopedZone() noexcept {
        GetGPUProfiler().EndZone(cmd, query_index);
    }
};
```

### `CollectResults`

```cpp
void GPUProfiler::CollectResults(VkDevice device) {
    if (!m_enabled) return;  // GPU profiling not supported on this device
    if (m_pending_zones.IsEmpty()) return;

    uint32_t query_count = m_next_query_index;
    Core::Containers::Array<uint64_t> timestamps;
    timestamps.Resize(query_count);

    VkResult result = vkGetQueryPoolResults(
        device, m_query_pool, 0, query_count,
        timestamps.Size() * sizeof(uint64_t), timestamps.Data(),
        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    if (result == VK_NOT_READY) {
        // GPU work still in flight — skip this frame's profile data rather than stalling.
        // This can occur if CollectResults is called before the frame fence signals.
        // Next frame will have valid data.
        ZENGINE_CORE_WARN("GPUProfiler: query results not ready — skipping frame %llu", m_frame_index);
        return;
    }
    ZENGINE_VALIDATE_ASSERT(result == VK_SUCCESS,
        "GPUProfiler: vkGetQueryPoolResults failed with unexpected error");

    m_last_zones.Clear();
    for (auto& zone : m_pending_zones) {
        uint64_t begin = timestamps[zone.QueryBegin];
        uint64_t end   = timestamps[zone.QueryEnd];
        float    ms    = float(end - begin) * float(m_timestamp_period_ns) * 1e-6f;
        m_last_zones.PushBack({ zone.Name, ms });
    }

    m_pending_zones.Clear();
    m_next_query_index = 0;
}
```

---

## 7. Memory Profiler — `MemoryProfiler`

### Purpose

Tracks `ArenaAllocator` watermarks and reports current and peak usage per named arena.
Data is read by the in-game debug overlay without requiring Tracy to be running.

### Header

```cpp
// ZEngine/Profiling/MemoryProfiler.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>

namespace ZEngine::Profiling {

    struct ArenaStats {
        const char* Name;
        uint64_t    CurrentOffset;   // bytes currently allocated
        uint64_t    PeakOffset;      // high-watermark since last reset
        uint64_t    Capacity;        // total arena size in bytes
    };

    class MemoryProfiler {
    public:
        // Register an arena for watermark tracking. `name` must be a string literal.
        // Call once per arena at engine startup (before the first frame).
        static void TrackArena(const char* name,
                               Core::Memory::ArenaAllocator* arena);

        // Called once per frame. Updates CurrentOffset and PeakOffset for all
        // tracked arenas. Emits ZENGINE_PROFILE_VALUE for each arena.
        static void Update();

        // Fill `out` with a snapshot of all tracked arena stats.
        // Called by the debug overlay once per frame.
        static void GetStats(Core::Containers::Array<ArenaStats>& out);

        // Notify of a raw allocation (used by ZENGINE_PROFILE_ALLOC fallback).
        static void RecordAlloc(const void* ptr, uint64_t size);

        // Notify of a raw free.
        static void RecordFree(const void* ptr);

        // Reset all peak watermarks (e.g. after a level load).
        static void ResetPeaks();

    private:
        struct TrackedArena {
            const char*                    Name;
            Core::Memory::ArenaAllocator*  Arena;
            uint64_t                       PeakOffset;
        };

        static Core::Containers::Array<TrackedArena> s_arenas;
    };

}  // namespace ZEngine::Profiling
```

### `Update` implementation

```cpp
void MemoryProfiler::Update() {
    for (auto& tracked : s_arenas) {
        uint64_t current = tracked.Arena->m_current_offset;
        if (current > tracked.PeakOffset) {
            tracked.PeakOffset = current;
        }
        // Emit value plots (no-op when profiling disabled)
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "Arena/%s", tracked.Name);
        ZENGINE_PROFILE_VALUE(name_buf, float(current));
    }
}
```

### Typical registration at engine startup

```cpp
// ZEngine/Engine/Engine.cpp — inside Initialize()
Profiling::MemoryProfiler::TrackArena("ECS",         &m_ecs_arena);
Profiling::MemoryProfiler::TrackArena("RenderGraph", &m_render_graph_arena);
Profiling::MemoryProfiler::TrackArena("AssetImport", &m_asset_import_arena);
Profiling::MemoryProfiler::TrackArena("Audio",       &m_audio_arena);
Profiling::MemoryProfiler::TrackArena("Physics",     &m_physics_arena);
Profiling::MemoryProfiler::TrackArena("Scratch",     &m_scratch_arena);
```

---

## 8. In-Game Debug Overlay — `DebugOverlay`

### Purpose

A lightweight, always-available frame statistics overlay rendered via `UIContext`. Unlike
Tracy, it requires no external viewer and works in any build configuration where the engine
is running. It reads from `ProfilerBuffer` and `MemoryProfiler` — not from Tracy.

### Toggle

Toggled by pressing **F3**. The overlay state is stored in a `bool m_visible` inside
`DebugOverlay`. When invisible, `Render()` returns immediately without building any UI
geometry.

### Displayed information

| Row | Data source | Format |
|-----|------------|--------|
| Frame time | `ProfilerBuffer` (last "Engine::Render" zone) | `Frame: 16.7 ms` |
| FPS | Computed from frame time | `FPS: 60` |
| CPU frame time | Sum of all top-level CPU zones | `CPU: 4.2 ms` |
| GPU frame time | `GPUProfiler::GetLastFrameZones()` sum | `GPU: 11.8 ms` |
| Entity count | `ZENGINE_PROFILE_VALUE("ECS/EntityCount", ...)` | `Entities: 1842` |
| Draw calls | `ZENGINE_PROFILE_VALUE("Renderer/DrawCalls", ...)` | `Draws: 312` |
| Memory watermarks | `MemoryProfiler::GetStats()` | Coloured bar per arena |
| Top 5 CPU zones | Sorted `ProfilerBuffer::DumpLastFrame()` | Bar chart |
| Top 5 GPU zones | Sorted `GPUProfiler::GetLastFrameZones()` | Bar chart |

### Class declaration

```cpp
// ZEngine/Profiling/DebugOverlay.h
#pragma once
#include <UI/UIContext.h>
#include <Profiling/ProfilerBuffer.h>
#include <Profiling/GPUProfiler.h>
#include <Profiling/MemoryProfiler.h>
#include <Core/Containers/Array.h>

namespace ZEngine::Profiling {

    class DebugOverlay {
    public:
        void Initialize(UI::UIContext* ui_ctx);
        void Destroy();

        // Call once per frame after ProfilerBuffer::BeginFrame() and
        // MemoryProfiler::Update(). Renders the overlay if visible.
        void Render();

        // Toggle overlay visibility.
        void Toggle();

        bool IsVisible() const { return m_visible; }

    private:
        void RenderFrameTimeRow();
        void RenderMemoryBars();
        void RenderCPUZoneChart();
        void RenderGPUZoneChart();

        UI::UIContext*                        m_ui_ctx  = nullptr;
        bool                                  m_visible = false;

        // Reused scratch buffers (no allocation per frame)
        Core::Containers::Array<ProfilerSample> m_cpu_samples;
        Core::Containers::Array<GPUZone>        m_gpu_zones;
        Core::Containers::Array<ArenaStats>     m_arena_stats;
    };

}  // namespace ZEngine::Profiling
```

### Rendering approach

`DebugOverlay::Render()` calls `ProfilerBuffer::DumpLastFrame(m_cpu_samples)`,
`GPUProfiler::GetGPUProfiler().GetLastFrameZones()`, and `MemoryProfiler::GetStats()`.
It then submits textured quads and colored bars to `UIContext` using the engine's existing
immediate-mode UI path. No separate render pass is required — the overlay is drawn as the
last submission in the UI pass.

### Memory bar colour coding

| Usage | Bar colour |
|-------|-----------|
| < 50% of capacity | Green `(0.2, 0.8, 0.2)` |
| 50–80% | Yellow `(0.9, 0.8, 0.1)` |
| > 80% | Red `(0.9, 0.2, 0.1)` |

Bars are normalized against each arena's `Capacity`. A thin white vertical tick marks the
peak watermark position.

---

## 9. Console / Debug Command System — `DebugConsole`

### Purpose

A developer console for issuing commands at runtime without recompiling. Toggled with the
tilde (`` ` ``) key. Rendered as a full-width panel at the top of the screen using
`UIContext`. Available in Debug builds only.

### Class declaration

```cpp
// ZEngine/Profiling/DebugConsole.h
#pragma once
#include <UI/UIContext.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <functional>

namespace ZEngine::Profiling {

    // A command handler receives the tokenized argument list.
    // argv[0] is the command name; argv[1..n] are arguments.
    using CommandFn = std::function<void(Core::Containers::Array<String>&)>;

    struct ConsoleCommand {
        const char* Name;
        const char* HelpText;
        CommandFn   Handler;
    };

    class DebugConsole {
    public:
        void Initialize(UI::UIContext* ui_ctx);
        void Destroy();

        // Register a command. `name` must be a unique string literal.
        // Duplicate names assert in debug.
        void RegisterCommand(const char* name,
                             const char* help_text,
                             CommandFn fn);

        // Called once per frame. Processes input and renders if visible.
        void Update();
        void Render();

        void Toggle();
        bool IsVisible() const { return m_visible; }

        // Execute a command string programmatically (e.g. from a startup script).
        // `line` is tokenized on whitespace.
        void Execute(const char* line);

    private:
        void RegisterBuiltins();
        void ParseAndDispatch(const char* input);
        void AppendLog(const char* text);

        UI::UIContext* m_ui_ctx  = nullptr;
        bool           m_visible = false;

        Core::Containers::UnorderedHashMap<StringHash, ConsoleCommand> m_commands;

        char   m_input_buf[256]     = {};
        char   m_log_buf[8192]      = {};  // circular log text
        uint32_t m_log_write_pos    = 0;
    };

    // Process-global accessor.
    DebugConsole& GetDebugConsole();

}  // namespace ZEngine::Profiling
```

### Built-in commands

Registered by `RegisterBuiltins()` called from `Initialize`:

| Command | Signature | Description |
|---------|-----------|-------------|
| `help` | `help [command]` | List all commands, or describe a specific command |
| `quit` | `quit` | Request a clean engine shutdown |
| `set_fps_cap` | `set_fps_cap <n>` | Cap the frame rate to `n` FPS (0 = uncapped) |
| `toggle_vsync` | `toggle_vsync` | Toggle V-Sync on/off |
| `spawn` | `spawn <actor_type>` | Spawn a registered actor type at the camera position |
| `kill_all` | `kill_all` | Destroy all spawned actors in the current scene |
| `reload_shaders` | `reload_shaders` | Trigger a hot-reload of all shaders on disk |

### `RegisterBuiltins` excerpt

```cpp
void DebugConsole::RegisterBuiltins() {
    RegisterCommand("help", "List commands or describe one",
        [this](Core::Containers::Array<String>& argv) {
            if (argv.Size() > 1) {
                StringHash h = StringHash(argv[1].Data());
                auto* cmd = m_commands.Find(h);
                if (cmd) AppendLog(cmd->HelpText);
                else     AppendLog("Unknown command.");
            } else {
                for (auto& [hash, cmd] : m_commands) {
                    AppendLog(cmd.Name);
                }
            }
        });

    RegisterCommand("quit", "Shutdown the engine",
        [](Core::Containers::Array<String>&) {
            Engine::Get().RequestQuit();
        });

    RegisterCommand("set_fps_cap", "set_fps_cap <n>",
        [](Core::Containers::Array<String>& argv) {
            ZENGINE_VALIDATE_ASSERT(argv.Size() >= 2, "set_fps_cap requires an argument");
            int n = atoi(argv[1].Data());
            Engine::Get().SetFpsCap(n);
        });

    RegisterCommand("toggle_vsync", "Toggle vertical sync",
        [](Core::Containers::Array<String>&) {
            Engine::Get().ToggleVsync();
        });

    RegisterCommand("reload_shaders", "Hot-reload all shaders from disk",
        [](Core::Containers::Array<String>&) {
            Rendering::Shaders::ShaderImporter::TriggerFullReload();
        });
}
```

### `Execute` implementation

```cpp
void DebugConsole::Execute(const char* line) {
    AppendLog(line);  // echo input to log

    Core::Containers::Array<String> tokens;
    // Tokenize `line` on whitespace. ZEngine string utilities used here.
    StringUtils::Tokenize(line, ' ', tokens);
    if (tokens.IsEmpty()) return;

    StringHash cmd_hash = StringHash(tokens[0].Data());
    auto* cmd = m_commands.Find(cmd_hash);
    if (!cmd) {
        char err[128];
        snprintf(err, sizeof(err), "Unknown command: %s", tokens[0].Data());
        AppendLog(err);
        return;
    }
    cmd->Handler(tokens);
}
```

---

## 10. Shipping vs Development Configuration

### Build configuration matrix

| Build | `ZENGINE_PROFILING` | `ZENGINE_TRACY` | Debug Overlay | Console | GPU Timestamps |
|-------|--------------------|-----------------|--------------|---------|----|
| Debug | 1 | 1 | Available, hidden by default | Available | Enabled |
| RelWithDebInfo | 1 | 1 | Available, hidden by default | Not available | Enabled |
| Release | 0 | 0 | Available (reads from no-op buffer) | Not available | Disabled |

### Debug overlay in Release

The `DebugOverlay` class is compiled into Release builds but reads from `ProfilerBuffer`,
which is a static ring of zeroes when `ZENGINE_PROFILING == 0`. The overlay will show
`0 ms` for all zones. This is acceptable: the overlay exists to show approximate frame
timing in shipped builds and its presence does not affect game logic. Frame time can also
be derived independently from wall-clock measurements at the main loop level if needed.

### Console availability

`DebugConsole` is excluded from RelWithDebInfo and Release builds via a `ZENGINE_DEBUG_CONSOLE`
define that is set to `1` only in Debug:

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(ZEngine PRIVATE ZENGINE_DEBUG_CONSOLE=1)
else()
    target_compile_definitions(ZEngine PRIVATE ZENGINE_DEBUG_CONSOLE=0)
endif()
```

In non-Debug builds, `DebugConsole::Initialize` is a no-op, `Toggle` is a no-op, and
`RegisterCommand` is a no-op. All console-related code compiles to nothing.

### Performance cost summary

| Configuration | Per-zone overhead | Total overhead |
|---------------|------------------|----------------|
| Release | 0 (macros are empty) | 0 |
| RelWithDebInfo + Tracy (viewer not connected) | ~2 ns (atomic check) | Negligible |
| RelWithDebInfo + Tracy (viewer connected) | ~10–20 ns | < 0.1 ms total |
| Debug + lightweight fallback | ~25 ns (clock + atomic) | ~0.2 ms at 8000 zones/frame |

---

## 11. File Layout

```
ZEngine/Profiling/
├── Profiling.h              — all ZENGINE_PROFILE_* macros; include this everywhere
├── ScopedTimer.h/.cpp       — lightweight RAII CPU timer (fallback backend)
├── ProfilerBuffer.h/.cpp    — fixed-capacity ring buffer for CPU samples and value plots
├── GPUProfiler.h/.cpp       — VkQueryPool management; Tracy Vulkan context init
├── MemoryProfiler.h/.cpp    — ArenaAllocator watermark tracking
├── DebugOverlay.h/.cpp      — in-game F3 frame statistics overlay
└── DebugConsole.h/.cpp      — developer console (Debug builds only)
```

All headers are self-contained. Including `Profiling/Profiling.h` pulls in only the macro
definitions. Individual subsystem headers (`GPUProfiler.h`, `MemoryProfiler.h`, etc.) are
included only by their direct users, not transitively.

---

## 12. Deliverables Checklist

### Macro API
- [x] `ZEngine/Profiling/Profiling.h` — 7 of 8 macros implemented (`ZENGINE_PROFILE_SCOPE`, `ZENGINE_PROFILE_FUNCTION`, `ZENGINE_PROFILE_FRAME`, `ZENGINE_PROFILE_THREAD`, `ZENGINE_PROFILE_VALUE`, `ZENGINE_PROFILE_ALLOC`, `ZENGINE_PROFILE_FREE`); `ZENGINE_PROFILE_GPU_SCOPE` pending
- [x] All implemented macros expand to nothing when `ZENGINE_PROFILING == 0`
- [x] Tracy path: delegates to `ZoneScoped`, `ZoneScopedN`, `FrameMarkNamed`, `TracyPlot`, `TracyAlloc`, `TracyFree`, `tracy::SetThreadName`; `TracyVkZone` pending (`ZENGINE_PROFILE_GPU_SCOPE`)
- [x] Fallback path: delegates to `ScopeGuard` (inline in `ProfilerBuffer.h`), `ProfilerBuffer`, `MemoryProfiler`; `GPUScopedZone` pending

### Tracy integration
- [x] Tracy fetched via `FetchContent` at tag v0.13.1; `ZENGINE_TRACY` CMake option added
- [x] `TRACY_ENABLE` and `TRACY_ON_DEMAND` set as CMake cache variables before `FetchContent_MakeAvailable`
- [x] `TracyClient` linked to `External_libs` when `ZENGINE_TRACY` is ON; `tracy/Tracy.hpp` included in `Profiling.h`
- [ ] Auto-enable `ZENGINE_TRACY` in Debug/RelWithDebInfo (currently manual opt-in only)
- [ ] `GPUProfiler::InitTracy` called after `vkCreateDevice`; `GPUProfiler::DestroyTracy` called before `vkDestroyDevice`

### Lightweight fallback
- [x] `ScopeGuard` (equivalent of `ScopedTimer`) — inline in `ProfilerBuffer.h`; stack-allocated RAII; `steady_clock` timing; calls `ProfilerBuffer::Record` on destruction
- [x] `ZEngine/Profiling/ProfilerBuffer.h/.cpp` — `CAPACITY = 4096` ring; `PaddedAtomic` write-pos; `BeginFrame` snapshots and resets; `DumpLastFrame` reads snapshot only; `RecordValue` for float plots

### GPU timestamps
- [ ] `ZEngine/Profiling/GPUProfiler.h/.cpp` — `Initialize` creates `VkQueryPool`; `BeginZone` / `EndZone` call `vkCmdWriteTimestamp`; `CollectResults` calls `vkGetQueryPoolResults`; `GPUZone` list with millisecond durations; `GPUScopedZone` RAII wrapper

### Memory profiler
- [x] `ZEngine/Profiling/MemoryProfiler.h/.cpp` — `Initialize` wires arena allocator; `TrackArena` registers arenas; `Update` reads `m_current_offset`, updates peak, emits `ZENGINE_PROFILE_VALUE`, warns at 80%, asserts at 100%; `GetStats` fills `ArenaStats` array; `ResetPeaks` implemented
- [x] `MemoryManager::Initialize` calls `MemoryProfiler::Initialize` and `CreateBudgetedArena` calls `MemoryProfiler::TrackArena` — all sub-arenas auto-registered at startup
- [ ] All engine arenas registered in `Engine::Initialize`: ECS, RenderGraph, AssetImport, Audio, Physics, Scratch

### Instrumentation coverage
- [ ] `Engine::MainThreadRun` — `ZENGINE_PROFILE_FRAME` + per-phase scopes
- [ ] `WorldTick::Tick` — whole tick + per-wave + per-system
- [ ] `ECS::Scene::ForEach` — iteration scope
- [ ] `VulkanDevice::Submit` — queue submit scope
- [ ] `RenderGraph::Compile` and `Execute` — per-pass scopes in Execute
- [ ] `AssetManager::Load` and `FlushPendingLoads`
- [ ] `ArenaAllocator::Allocate` — `ZENGINE_PROFILE_ALLOC`
- [ ] All post-process pass `Execute` methods — CPU + GPU scopes
- [ ] All shadow pass `Execute` methods — CPU + GPU scopes
- [ ] `AudioEngine::Tick`

### Debug overlay
- [ ] `ZEngine/Profiling/DebugOverlay.h/.cpp` — F3 toggle; frame time, FPS, CPU/GPU time, entity count, draw calls; memory bars (green/yellow/red by %-used); top-5 CPU and GPU zone charts; reads from `ProfilerBuffer` and `GPUProfiler` only
- [ ] Overlay compiled into all build configurations; data sources produce zeroes in Release (no crash)

### Debug console
- [ ] `ZEngine/Profiling/DebugConsole.h/.cpp` — tilde toggle; `RegisterCommand`; `Execute` tokenizes + dispatches; circular log buffer; `ZENGINE_DEBUG_CONSOLE` compile guard
- [ ] Built-in commands implemented: `help`, `quit`, `set_fps_cap`, `toggle_vsync`, `spawn`, `kill_all`, `reload_shaders`
- [ ] Console compiled to no-op in RelWithDebInfo and Release via `ZENGINE_DEBUG_CONSOLE=0`

### Build configuration
- [x] `ZENGINE_PROFILING=1` in Debug and RelWithDebInfo, `0` in Release (set in `ZEngine/ZEngine/CMakeLists.txt`)
- [x] `ZENGINE_TRACY` CMake option present; `ZENGINE_TRACY=1` propagated to `zEngineLib` when enabled
- [ ] Auto-enable `ZENGINE_TRACY` in Debug/RelWithDebInfo builds
- [ ] `ZENGINE_DEBUG_CONSOLE=1` in Debug only
- [x] Tracy `TRACY_ON_DEMAND` enabled via CMake cache vars; `pthread`/`dl` linkage handled by Tracy's own CMakeLists

### Testing
- [ ] Manual smoke test: connect Tracy viewer to a Debug build; verify zones appear for `Engine::MainThreadRun`, `WorldTick`, `RenderGraph::Execute`, and all post-process passes
- [ ] Manual smoke test: press F3 in a running build; verify overlay shows non-zero frame time and memory bars
- [ ] Manual smoke test: open console (Debug build); run `help`, `set_fps_cap 30`, `reload_shaders`; verify each command executes without crash
- [ ] Build Release configuration; verify `ZENGINE_PROFILING=0` means zero-diff in assembly for a known instrumented function (checked via Compiler Explorer or `objdump`)
