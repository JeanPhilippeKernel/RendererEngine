# ZEngine — Game Loop

**Priority:** P1 — Required for deterministic simulation and correct frame pacing
**Status:** In Progress
**Depends on:** `actor-ecs-architecture.md` (WorldTick), `physics-system.md` (fixed step)
**Modifies:** `Engine.cpp` (MainThreadRun), `CoreWindow.h/.cpp`

---

## 1. Overview

ZEngine already separates CPU-side simulation from GPU submission via two long-running
threads: `Engine::MainThreadRun` and `Engine::RenderThreadRun`. What is currently missing
is a disciplined timestep strategy. Without one:

- Physics and gameplay logic produce different results at different frame rates.
- Fast machines over-simulate; slow machines under-simulate.
- Camera interpolation is jerky because render cadence is decoupled from simulation cadence
  without an alpha factor.

This document specifies:

1. A **fixed-timestep accumulator** that drives simulation at a constant `FIXED_DT` regardless
   of raw frame duration.
2. A **variable-timestep render path** that reads an interpolation `alpha` to smooth positions
   between simulation ticks.
3. A **frame rate cap** for uncapped displays (vsync off).
4. A **FrameTimer** for robust delta measurement and spike clamping.
5. An enhanced **`Core::TimeStep`** struct that carries all timing state downstream.
6. A rewritten **`Engine::MainThreadRun`** integrating all the above.

---

## 2. Two-Loop Architecture

### 2.1 Existing thread split

ZEngine's entry point spawns two long-running functions:

```
Main OS Thread
  └─ Engine::MainThreadRun()     ← simulation, input, audio, game logic
       spawns ──►  Engine::RenderThreadRun()  ← GPU command recording and submission
```

`MainThreadRun` owns the simulation clock. `RenderThreadRun` owns the GPU timeline.
The two threads synchronise through a **frame packet** — a snapshot of renderable state
written by `MainThreadRun` and consumed by `RenderThreadRun` each frame.

### 2.2 Where new logic inserts

```
MainThreadRun (main thread)
│
│  FrameTimer::Begin()
│  raw_dt = FrameTimer::End()          ← wall-clock measurement
│
│  FixedTimestepAccumulator::Accumulate(raw_dt)
│  while ShouldStep():
│    WorldTick(FIXED_DT)               ← deterministic simulation tick
│    ActorManager::Tick(FIXED_DT)
│    ConsumeStep()
│
│  alpha = Accumulator::Alpha()        ← interpolation factor [0,1)
│
│  BuildFramePacket(alpha)             ← snapshot transforms for renderer
│  SubmitFramePacket()                 ← wake render thread
│
│  FrameRateCap::Wait(max_fps)         ← sleep if ahead of cap
│
RenderThreadRun (render thread)
│
│  WaitForFramePacket()
│  InterpolateTransforms(alpha)        ← smooth between prev and curr positions
│  RenderGraph::Execute()
```

The render thread never mutates simulation state. It only reads the frame packet.
The frame packet is double-buffered so simulation can advance while the previous frame
renders.

---

## 3. Fixed Timestep Accumulator

### 3.1 The standard pattern

The "fix your timestep" pattern (Gaffer on Games, 2006) ensures that `simulate(FIXED_DT)`
is always called with the same `dt`, making physics and gameplay frame-rate-independent.

```
accumulator += raw_delta
while accumulator >= FIXED_DT:
    simulate(FIXED_DT)
    accumulator -= FIXED_DT
alpha = accumulator / FIXED_DT        // interpolation factor in [0, 1)
```

`alpha` tells the renderer: "the current visual frame is `alpha` of the way from the last
simulation state to the next one." Transforms are linearly interpolated using `alpha`.

### 3.2 Spiral of death

If a single frame takes longer than `MAX_STEPS * FIXED_DT` to render, the accumulator
grows unboundedly. The loop would try to simulate more steps than the machine can handle,
making the problem worse. The fix: cap the number of simulation steps per frame.

```
accumulated_dt = min(accumulated_dt + raw_dt, MAX_STEPS * FIXED_DT)
```

This caps max catch-up to 5 × 16.67 ms = 83 ms. Frames that fall behind that threshold
appear to slow down in simulation time rather than locking up.

### 3.3 `FixedTimestepAccumulator` declaration

```cpp
// ZEngine/Engine/FixedTimestepAccumulator.h
#pragma once
#include <cstdint>

namespace ZEngine::Timing {

    struct FixedTimestepAccumulatorConfig {
        float FixedDt      = 1.0f / 60.0f;   // 16.6̄ ms
        int   MaxStepsPerFrame = 5;            // spiral-of-death guard
    };

    struct FixedTimestepAccumulator {
    public:
        explicit FixedTimestepAccumulator(
            const FixedTimestepAccumulatorConfig& config = {}) noexcept
            : m_Config(config)
            , m_Accumulator(0.0f)
        {}

        // Feed a new raw frame delta. Clamps to prevent spiral of death.
        void Accumulate(float raw_dt) noexcept {
            const float max_dt = m_Config.FixedDt * static_cast<float>(m_Config.MaxStepsPerFrame);
            m_Accumulator += raw_dt;
            if (m_Accumulator > max_dt) {
                // Throttle: warn at most once per 5 seconds to avoid log spam.
                static int64_t s_last_warn_ns = INT64_MIN;
                int64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
                if (now_ns - s_last_warn_ns > 5'000'000'000LL) {
                    ZENGINE_CORE_WARN("FixedTimestepAccumulator: spiral-of-death guard triggered "
                                      "({:.2f}s accumulated, clamping to {:.2f}s)",
                                      m_Accumulator, max_dt);
                    s_last_warn_ns = now_ns;
                }
                m_Accumulator = max_dt;
            }
        }

        // Returns true while there are pending simulation steps.
        [[nodiscard]] bool ShouldStep() const noexcept {
            return m_Accumulator >= m_Config.FixedDt;
        }

        // Consume one fixed step. Call inside the simulation loop.
        void ConsumeStep() noexcept {
            ZENGINE_VALIDATE_ASSERT(ShouldStep(),
                "ConsumeStep called when no step is pending");
            m_Accumulator -= m_Config.FixedDt;
            ++m_StepCount;
        }

        // Interpolation factor in [0, 1). Pass to renderer after the step loop.
        [[nodiscard]] float Alpha() const noexcept {
            return m_Accumulator / m_Config.FixedDt;
        }

        [[nodiscard]] float FixedDt() const noexcept { return m_Config.FixedDt; }
        [[nodiscard]] uint64_t StepCount() const noexcept { return m_StepCount; }

        // Reset the accumulator to zero.
        // Call after:
        //   - Loading a new scene (prevents a burst of fixed steps on first frame)
        //   - Unpausing (prevents accumulated time from causing a simulation burst)
        //   - After any operation that blocks the main thread > 100ms
        void Reset() noexcept { m_Accumulator = 0.f; }

    private:
        FixedTimestepAccumulatorConfig m_Config;
        float    m_Accumulator{0.0f};
        uint64_t m_StepCount{0};
    };

} // namespace ZEngine::Timing
```

### 3.4 Integration contract

`FixedTimestepAccumulator` has no allocations. It is stored inline in `Engine` (or on a
scratch arena). Simulation callbacks receive a `Core::TimeStep` with `FixedDeltaSeconds`
set to `FIXED_DT` and `DeltaSeconds` set to the raw frame delta.

---

## 4. Variable Timestep for Rendering

The renderer does **not** tick at `FIXED_DT`. It ticks once per frame at whatever rate
the display demands. Transform interpolation is the bridge:

```
// Each renderable entity stores two snapshots:
struct TransformSnapshot {
    Core::Maths::Vec3f PreviousPosition;
    Core::Maths::Vec3f CurrentPosition;
    // rotation, scale omitted for brevity — same pattern
};

// Renderer reads alpha from frame packet and interpolates:
Core::Maths::Vec3f visual_pos =
    Math::Lerp(snapshot.PreviousPosition, snapshot.CurrentPosition, alpha);
```

Rules:
- `PreviousPosition` is written from `CurrentPosition` at the **start** of each fixed step.
- `CurrentPosition` is written by the physics/transform system at the **end** of each fixed step.
- `alpha` is computed after the step loop exits and packed into the frame packet.
- If the simulation has not stepped this frame (`alpha` == 0), the renderer shows the
  last known `CurrentPosition` exactly (alpha == 0 → lerp returns `PreviousPosition`,
  which equals `CurrentPosition` from the prior step — correct).

This means cameras, particles, and any visual-only objects can update at display frequency
while physics objects are correctly interpolated. Do **not** interpolate positions that
are not physics-driven (e.g., UI elements, skybox); they should always read `CurrentPosition`
directly.

### 4.1 Frame packet layout (relevant fields)

```cpp
// ZEngine/Engine/FramePacket.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Maths/Vec3f.h>
#include <ECS/EntityID.h>

namespace ZEngine::Timing {

    struct RenderableTransform {
        ECS::EntityID   Entity;
        Core::Maths::Vec3f PreviousWorldPosition;
        Core::Maths::Vec3f CurrentWorldPosition;
        // quaternion rotation fields follow same pattern
    };

    struct FramePacket {
        float    Alpha{0.0f};          // interpolation factor [0,1)
        float    RawDeltaSeconds{0.0f};
        uint64_t FrameIndex{0};
        Core::Containers::Array<RenderableTransform> Transforms;
        // other render data (lights, camera matrix, etc.)
    };

    // FramePacket double-buffering:
    // Engine maintains two FramePackets: m_FramePackets[2].
    // Main thread writes to m_FramePackets[m_WriteIdx % 2].
    // Render thread reads from m_FramePackets[(m_WriteIdx + 1) % 2].
    // After SubmitFramePacket():
    //   m_WriteIdx.fetch_add(1, std::memory_order_release);
    // Render thread waits on m_WriteIdx change before reading.
    // This is a single-producer single-consumer pattern — no mutex needed.
    // Pre-allocate Transforms array at scene load time to avoid per-frame growth.

} // namespace ZEngine::Timing
```

The frame packet is allocated from a per-frame arena. `RenderThreadRun` reads from the
committed packet slot; `MainThreadRun` writes to the in-progress slot. Slot swap happens
after `SubmitFramePacket()`.

---

## 5. Frame Rate Cap

When vsync is disabled (`!CoreWindow::IsVSyncEnable()`), the GPU can present frames
faster than the display refresh. Uncapped frames waste power, inflate GPU temperatures,
and can cause tearing artefacts even with vsync off. A frame-rate cap limits presentation
to `max_fps` by sleeping the remainder of the frame budget on the main thread.

### 5.1 Algorithm

```
frame_budget  = 1.0 / max_fps                    // e.g. 1/300 ≈ 3.33 ms
frame_start   = high_resolution_clock::now()
... simulate and submit ...
frame_end     = high_resolution_clock::now()
elapsed       = duration<float>(frame_end - frame_start).count()
if elapsed < frame_budget:
    sleep_for(nanoseconds(frame_budget - elapsed))
```

`sleep_for` has OS scheduler granularity (~1 ms on Windows, ~100 µs on macOS/Linux).
For sub-millisecond accuracy, spin-wait the last ~500 µs rather than sleeping.

### 5.2 `FrameRateCap` declaration

```cpp
// ZEngine/Engine/FrameRateCap.h
#pragma once
#include <chrono>
#include <thread>
#include <cstdint>

namespace ZEngine::Timing {

    struct FrameRateCap {
    public:
        static constexpr int DefaultMaxFps = 300;

        explicit FrameRateCap(int max_fps = DefaultMaxFps) noexcept
            : m_FrameBudgetNs(static_cast<int64_t>(1'000'000'000LL / max_fps))
        {}

        void SetMaxFps(int max_fps) noexcept {
            ZENGINE_VALIDATE_ASSERT(max_fps > 0, "max_fps must be positive");
            m_FrameBudgetNs = 1'000'000'000LL / max_fps;
        }

        // Call at the start of each main-loop iteration.
        void MarkFrameStart() noexcept {
            m_FrameStart = std::chrono::high_resolution_clock::now();
        }

        // Call at the end of the frame. Sleeps/spins to maintain the cap.
        void WaitForFrameBudget() noexcept {
            using namespace std::chrono;
            constexpr int64_t kSpinThresholdNs = 500'000LL; // spin last 0.5 ms
            auto deadline = m_FrameStart + nanoseconds(m_FrameBudgetNs);
            auto now      = high_resolution_clock::now();
            auto remaining_ns = duration_cast<nanoseconds>(deadline - now).count();

            // Guard: if frame ran over-budget, remaining_ns is negative.
            // Negative nanoseconds cast to size_t wraps to a huge value → very long sleep.
            if (remaining_ns <= 0) return;  // frame already exceeded budget; do not sleep

            if (remaining_ns > kSpinThresholdNs) {
                std::this_thread::sleep_for(
                    nanoseconds(remaining_ns - kSpinThresholdNs));
            }
            // Spin-wait for the final window
            while (high_resolution_clock::now() < deadline) {
                // busy wait — yield once per iteration to avoid 100 % core
                std::this_thread::yield();
            }
        }

    private:
        std::chrono::high_resolution_clock::time_point m_FrameStart;
        int64_t m_FrameBudgetNs;
    };

} // namespace ZEngine::Timing
```

### 5.3 Interaction with vsync

When vsync is on, the display driver already enforces a presentation cadence and the
main thread blocks in `vkQueuePresentKHR`. The frame cap is meaningless in that case and
should be bypassed:

```cpp
if (!CoreWindow::IsVSyncEnable()) {
    m_FrameCap.WaitForFrameBudget();
}
```

---

## 6. Frame Time Measurement

### 6.1 Requirements

- Measure wall-clock time between successive frames.
- Provide a smoothed delta (moving average over 8 frames) for HUD display and camera
  systems that react poorly to single-frame spikes.
- Clamp raw delta to 0.25 s maximum so a debugger breakpoint or OS sleep doesn't
  inject a catastrophically large delta.

### 6.2 `FrameTimer` declaration

```cpp
// ZEngine/Engine/FrameTimer.h
#pragma once
#include <chrono>
#include <cstdint>
#include <algorithm>

namespace ZEngine::Timing {

    struct FrameTimer {
    public:
        // Maximum raw delta time accepted per frame (250 ms = 4 FPS minimum).
        // Frames slower than this are clamped — simulation appears to slow down
        // rather than spiral out of control. Side effects:
        //   - Physics: objects may appear to pause briefly during a hitch
        //   - Animation: clips may desync from audio if hitch > 250ms
        //   - Acceptable trade-off: prevents spiral-of-death in all cases
        static constexpr float kMaxRawDelta = 0.25f;
        static constexpr int   kSmoothingSamples = 8;

        FrameTimer() noexcept {
            m_Last = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < kSmoothingSamples; ++i) { m_Samples[i] = 1.0f / 60.0f; }
        }

        // Call once at the beginning of each frame iteration.
        void Begin() noexcept {
            m_FrameBegin = std::chrono::high_resolution_clock::now();
        }

        // Call after Begin(). Returns raw clamped delta since last End() call.
        float End() noexcept {
            using namespace std::chrono;
            auto now      = high_resolution_clock::now();
            float raw_dt  = duration<float>(now - m_Last).count();
            m_Last        = now;

            // Spike clamp
            raw_dt = std::min(raw_dt, kMaxRawDelta);

            // Rolling average
            m_Samples[m_SampleIndex] = raw_dt;
            m_SampleIndex = (m_SampleIndex + 1) % kSmoothingSamples;
            float sum = 0.0f;
            for (int i = 0; i < kSmoothingSamples; ++i) { sum += m_Samples[i]; }
            m_SmoothedDt = sum / static_cast<float>(kSmoothingSamples);

            return raw_dt;
        }

        // Smoothed delta (moving average). Safe for UI display and camera lerp speed.
        [[nodiscard]] float SmoothedDelta() const noexcept { return m_SmoothedDt; }

        // Raw (clamped) delta. Use for the fixed-step accumulator.
        [[nodiscard]] float RawDelta()      const noexcept { return m_Samples[(m_SampleIndex + kSmoothingSamples - 1) % kSmoothingSamples]; }

    private:
        std::chrono::high_resolution_clock::time_point m_Last;
        std::chrono::high_resolution_clock::time_point m_FrameBegin;
        float    m_Samples[kSmoothingSamples]{};
        float    m_SmoothedDt{1.0f / 60.0f};
        int      m_SampleIndex{0};
    };

} // namespace ZEngine::Timing
```

### 6.3 Usage sequence

```
timer.Begin()          // record start of frame
raw_dt = timer.End()   // wall-clock delta; spike-clamped
accumulator.Accumulate(raw_dt)
while (accumulator.ShouldStep()) { ... }
// present to renderer using timer.SmoothedDelta() for HUD fps counter
```

---

## 7. Enhanced `Core::TimeStep`

The existing `Core::TimeStep` carries a single `float DeltaSeconds`. The game loop
now needs all timing state bundled for downstream consumers (physics, animation,
gameplay systems, HUD). Extend it:

```cpp
// ZEngine/Core/TimeStep.h
#pragma once
#include <cstdint>

namespace ZEngine::Core {

    struct TimeStep {
        // Variable timestep — raw (clamped) wall-clock delta for this frame.
        float DeltaSeconds{0.0f};

        // Fixed timestep used by the simulation accumulator.
        // Always 1.0/60.0 (or whatever FIXED_DT is configured to).
        float FixedDeltaSeconds{1.0f / 60.0f};

        // Interpolation factor in [0, 1). Pass to renderer for transform lerp.
        float Alpha{0.0f};

        // Monotonically increasing frame counter. Never resets unless the engine restarts.
        uint64_t FrameCount{0};

        // Total elapsed simulation seconds (sum of all FixedDeltaSeconds steps taken).
        double TotalSeconds{0.0};

        // Smoothed delta (moving average). Use for camera lerp, HUD display.
        float SmoothedDeltaSeconds{0.0f};

        bool     VsyncEnabled{false};   // set each frame from CoreWindow::IsVSyncEnable()
        bool     IsFixedStep{false};    // true when current frame ran at least one fixed step
        uint8_t  _pad[6]{};             // explicit padding to 8-byte alignment
    };

    static_assert(sizeof(TimeStep) % 8 == 0,
        "TimeStep must be 8-byte aligned (padded to multiple of 8 bytes). "
        "Update _pad[] if fields are added.");

} // namespace ZEngine::Core
```

Rules:
- `DeltaSeconds` is set once per frame to the raw clamped delta.
- `FixedDeltaSeconds` is constant across the lifetime of the accumulator config.
- `Alpha` is set after the step loop exits.
- `TotalSeconds` increments by `FixedDeltaSeconds` each `ConsumeStep()`.
- `FrameCount` increments once per rendered frame, not per simulation step.

`TimeStep` is passed by value (it is 32 bytes — cheap to copy). No heap allocation.

---

## 8. `Engine::MainThreadRun` Rewrite

Below is the full updated pseudocode for `MainThreadRun`. Concrete Vulkan/platform types
are omitted; the focus is the timing skeleton.

```cpp
// ZEngine/Engine/Engine.cpp  (MainThreadRun section)

void Engine::MainThreadRun() {
    // one-time setup
    Engine::FrameTimer                    frame_timer;
    Engine::FixedTimestepAccumulator      accumulator;   // default config: 60 Hz, max 5 steps
    Engine::FrameRateCap                  frame_cap(300);
    Core::TimeStep                        timestep;

    ZENGINE_CORE_INFO("Engine main loop starting. FixedDT={:.4f}s", accumulator.FixedDt());

    while (!m_ShouldQuit) {

        // ── 1. Frame start bookkeeping ──────────────────────────────────────
        frame_timer.Begin();
        frame_cap.MarkFrameStart();

        // ── 2. Platform event pump ──────────────────────────────────────────
        m_Window->PollEvents();
        if (m_Window->ShouldClose()) { m_ShouldQuit = true; break; }

        // ── 3. Measure raw delta ─────────────────────────────────────────────
        float raw_dt = frame_timer.End();         // clamped to 0.25 s
        timestep.DeltaSeconds        = raw_dt;
        timestep.SmoothedDeltaSeconds = frame_timer.SmoothedDelta();
        timestep.FrameCount          += 1;

        // ── 4. Feed accumulator ─────────────────────────────────────────────
        accumulator.Accumulate(raw_dt);

        // ── 5. Fixed simulation steps ───────────────────────────────────────
        timestep.FixedDeltaSeconds = accumulator.FixedDt();
        while (accumulator.ShouldStep()) {
            // Tick physics (external physics-system.md)
            m_PhysicsWorld->Step(accumulator.FixedDt());

            // Tick all Actor virtual OnTick()
            m_ActorManager->Tick(timestep);

            // Tick pure ECS systems (system-scheduler.md)
            m_SystemScheduler->Tick(timestep);

            // Tick game application (existing hook)
            m_GameApplication->Update(timestep);

            accumulator.ConsumeStep();
            timestep.TotalSeconds += static_cast<double>(accumulator.FixedDt());

            // IMPORTANT: SnapshotTransforms must be called AFTER WorldTick::Tick completes.
            // Order: WorldTick::Tick writes CurrentPosition → SnapshotTransforms copies it to PreviousPosition.
            // This ensures interpolation lerps between frame N-1 and frame N (correct).
            // Calling SnapshotTransforms BEFORE Tick would lerp between frame N-2 and N-1 (one step behind).
            m_world_tick.Tick(m_ecs_scene, accumulator.FixedDt(), world_commands);
            world_commands.Flush(m_ecs_scene);
            m_ecs_scene.SnapshotTransforms();  // ← must be AFTER Tick, not before
        }

        // ── 5b. Per-frame background work ───────────────────────────────────
        ImportCoordinator::Tick();       // dispatches up to N import jobs per frame
        MainThreadScheduler::Drain();    // executes callbacks posted by background threads

        // ── 6. Interpolation alpha ──────────────────────────────────────────
        timestep.Alpha = accumulator.Alpha();

        // ── 7. Build and submit frame packet ────────────────────────────────
        FramePacket* packet = m_FramePacketPool->AcquireWriteSlot();
        packet->Alpha            = timestep.Alpha;
        packet->RawDeltaSeconds  = raw_dt;
        packet->FrameIndex       = timestep.FrameCount;
        m_Scene->FillRenderableTransforms(timestep.Alpha, packet->Transforms);
        m_FramePacketPool->SubmitWriteSlot(packet);   // wakes render thread

        // ── 8. Frame rate cap ───────────────────────────────────────────────
        if (!m_Window->IsVSyncEnable()) {
            frame_cap.WaitForFrameBudget();
        }
    }

    ZENGINE_CORE_INFO("Engine main loop exited after {} frames ({:.2f}s)",
        timestep.FrameCount, timestep.TotalSeconds);
}
```

Key invariants:
- `SnapshotTransforms()` is called **after** `WorldTick::Tick` completes so that
  `CurrentPosition` (just written by systems) is captured into `PreviousPosition` for
  the next frame's interpolation. Calling it before `Tick` would lerp one step behind.
- `ActorManager::Tick` and `SystemScheduler::Tick` both receive the same `timestep`
  with `FixedDeltaSeconds` set, not `DeltaSeconds`. Systems that need the raw frame
  delta (e.g., audio fade, camera spring) should use `DeltaSeconds`.
- `m_GameApplication->Update(timestep)` replaces the existing call; the signature
  changes from `Update(float dt)` to `Update(const Core::TimeStep& ts)`, matching
  the `OnUpdate(const Core::TimeStep& ts)` contract in engine-lifecycle.md §5.

---

## 9. Vsync and Platform Sync

### 9.1 How vsync interacts with the cap

With vsync on, `vkQueuePresentKHR` blocks until the display's vertical blank. The OS
driver throttles presentation to the refresh rate. In this regime:

- `raw_dt` is approximately `1 / refresh_rate` (e.g., 16.6 ms at 60 Hz).
- The accumulator typically fires exactly one fixed step per frame at 60 Hz.
- The frame cap sleep is skipped (see section 5.3).
- `alpha` stays close to 0 or 1, so interpolation jitter is minimal.

With vsync off, presentation is immediate. `raw_dt` is dictated entirely by CPU/GPU
workload. Frame cap sleep enforces an upper bound.

### 9.2 macOS — CVDisplayLink

`sleep_for` on macOS is subject to Timer Coalescing and may sleep longer than requested.
On macOS, the preferred approach is `CVDisplayLinkCreateWithActiveCGDisplays`, which fires
a callback precisely on vertical blank. The integration path:

```
CVDisplayLink callback (display-link thread):
  signal_semaphore(g_DisplayLinkSemaphore)

MainThreadRun (after render submit, instead of frame cap sleep):
  wait_semaphore(g_DisplayLinkSemaphore, timeout = 33ms)
```

This replaces the `FrameRateCap::WaitForFrameBudget()` call on macOS. The display link
is created in `CoreWindow::Init()` and torn down in `CoreWindow::Shutdown()`. The
semaphore is a `std::binary_semaphore` (C++20) or a Mach semaphore.

When vsync is explicitly disabled, fall back to the `FrameRateCap` spin-sleep path even
on macOS to permit profiling at uncapped rates.

### 9.3 Windows — `timeBeginPeriod(1)`

On Windows, `std::this_thread::sleep_for` defaults to a 15 ms timer resolution. Call
`timeBeginPeriod(1)` at engine startup (and `timeEndPeriod(1)` at shutdown) to bring
resolution down to 1 ms. This is already standard practice in game engines.

---

## 10. Deliverables Checklist

| # | Item | File | Status |
|---|------|------|--------|
| 1 | `FixedTimestepAccumulator` struct | `ZEngine/Engine/FixedTimestepAccumulator.h` | Done |
| 2 | `FrameTimer` struct | `ZEngine/Engine/FrameTimer.h` | Done |
| 3 | `FrameRateCap` struct | `ZEngine/Engine/FrameRateCap.h` | Done |
| 4 | Enhanced `Core::TimeStep` | `ZEngine/Core/TimeStep.h` | Todo (modify existing) |
| 5 | `FramePacket` struct | `ZEngine/Engine/FramePacket.h` | Done |
| 6 | `Engine::MainThreadRun` rewrite | `ZEngine/Engine/Engine.cpp` | Done |
| 7 | `Scene::SnapshotTransforms()` | `ZEngine/ECS/Scene.cpp` | Done |
| 8 | `Scene::FillRenderableTransforms(alpha, …)` | `ZEngine/ECS/Scene.cpp` | Done |
| 9 | `GameApplication::Update` signature update | `ZEngine/Application/GameApplication.h` | Todo (modify existing) |
| 10 | CVDisplayLink integration (macOS) | `ZEngine/Platform/macOS/CoreWindow.mm` | Todo (optional, P2) |
| 11 | `timeBeginPeriod(1)` (Windows) | `ZEngine/Platform/Windows/CoreWindow.cpp` | Todo (optional, P2) |

All new structs must:
- Allocate nothing from the heap.
- Use `ZENGINE_VALIDATE_ASSERT` for precondition checks.
- Have no constructors that throw.
- Be forward-declared in a single umbrella header `ZEngine/Engine/TimingTypes.h` for convenience.
