# ZEngine — Profiling and Instrumentation

**Status:** CPU instrumentation facilities, arena tracking, and render-graph timestamp collection exist. Frame integration, presentation, and validation coverage are incomplete.

## Current facilities

`Profiling.h` selects one of three compile-time paths:

| Condition | Scope/value/alloc behavior |
|---|---|
| `ZENGINE_PROFILING && ZENGINE_TRACY` | Tracy zones, plots, and allocations |
| `ZENGINE_PROFILING` | `ScopeGuard`/`ProfilerBuffer` fallback and `MemoryProfiler` alloc hooks |
| neither | profiling macros expand to nothing |

`ZENGINE_PROFILING` is currently supplied for Debug and RelWithDebInfo through the engine CMake target. `ZENGINE_TRACY` is an optional root CMake option that defaults to OFF; it is not implicitly enabled for Debug.

`ProfilerBuffer` is a fixed-capacity (4096) static CPU sample/value buffer. It permits concurrent writers and exposes the previous-frame snapshot through `DumpLastFrame` and `DumpLastFrameValues`. Samples are silently dropped at capacity. Its `BeginFrame()` transition and its dump APIs are facilities, not evidence that the main loop currently calls or presents them.

`MemoryProfiler` records named arenas registered through `MemoryManager::CreateBudgetedArena`, tracks current/peak offsets, emits profile values, and warns above 80% with a 60-second cooldown. It is initialized with the `Bootstrap` owner before tracking. `Engine::MainThreadRun()` calls `MemoryProfiler::Update()` at its main-thread frame boundary, so the editor panel reflects live named-owner current and peak data. The panel publishes separate render-thread snapshots for VMA/driver heap samples, persistent environment resources, and render-graph transients; it intentionally does not collapse these into a CPU-arena total.

`RenderGraph` owns per-frame Vulkan timestamp query pools when supported by the selected queue. It records `RGPassTiming` values and exposes `GetLatestPassTimings()` / `IsTimestampProfilingEnabled()`. Query-pool creation failure disables the feature with a warning; the graph timing path is not a UI/profiling overlay by itself.

## Production completion criteria

1. Define one non-blocking frame boundary that advances CPU profiling and memory sampling on the correct owning thread.
2. Publish a copied telemetry snapshot to ZUI/debug tooling; never let UI read a live mutable profiler buffer concurrently with its writers.
3. Surface dropped CPU samples, query-pool capacity exhaustion, unavailable queue timestamps, and arena registrations as explicit telemetry.
4. Verify CPU and GPU timing under all supported backends and device-lost, resize, and no-timestamp-feature cases. Timing must never add a GPU wait to normal presentation.
5. Establish build/release policy for profiling symbols, Tracy enablement, capture privacy, and retention. Do not claim “zero overhead” for code paths that still compile instrumentation under `ZENGINE_PROFILING`.
