# ZEngine — System Scheduler

**Priority:** P1 — Implement alongside ECS core (Phase 1 of migration-plan.md)  
**Status:** Implemented  
**Depends on:** `actor-ecs-architecture.md`  
**Blocks:** `animation-system.md`

---

## 1. Why this exists

Without a scheduler, the game loop calls systems sequentially:

```cpp
while (running) {
    InputSystem(scene, dt);
    PhysicsSystem(scene, dt);
    AnimationSystem(scene, dt);
    RenderCullSystem(scene, dt);
    AudioSystem(scene, dt);
}
```

This works on a single thread. The problem appears as the scene grows. Say your systems take:

```
InputSystem        0.1 ms
PhysicsSystem      4.0 ms
AnimationSystem    3.0 ms
RenderCullSystem   2.0 ms
AudioSystem        1.0 ms
Total:            10.1 ms  (16 ms budget at 60 fps — 5.9 ms headroom)
```

`PhysicsSystem` writes `TransformComponent`. `AnimationSystem` writes `AnimatorComponent`.
They touch completely different data — there is no reason they cannot run simultaneously on
two threads. The wasted 3ms is free performance sitting on cores that are doing nothing.

You could parallelize manually:

```cpp
auto f1 = ThreadPool::Submit([&]{ PhysicsSystem(scene, dt); });
auto f2 = ThreadPool::Submit([&]{ AnimationSystem(scene, dt); });
f1.wait(); f2.wait();
RenderCullSystem(scene, dt);
```

But this hardcodes dependency knowledge in the game loop. Every new system requires
reasoning about all existing systems, manually reordering calls, and hoping the analysis
is correct. It does not scale and produces silent data races when wrong.

**The scheduler externalizes that reasoning.** Each system declares what it reads and
writes. The scheduler detects conflicts, groups independent systems into parallel waves,
enforces ordering where conflicts exist, and asserts when you forget to declare an
ordering — rather than silently running incorrectly.

It is not strictly necessary for a small game with few systems. It pays off when you have
5+ systems that could run in parallel and want that parallelism to be provably correct
rather than ad-hoc. For ZEngine's scope (physics, animation, culling, audio, gameplay
systems), the scheduler is the right model.

---

## 2. Overview

The system scheduler sits on top of `WorldTick` and `ThreadPoolHelper`. Its job is to
dispatch ECS systems in parallel where safe, and serialize them where a data conflict exists.

Safety is determined by component read/write masks declared at registration. Two systems
that conflict (one writes what the other reads or writes) must not run concurrently. Two
systems with no overlap can run in parallel.

Conflicts without an explicit ordering edge are a programming error — the scheduler asserts
in debug builds rather than silently serializing.

---

## 3. System Registration

Every system declares exactly what it reads and writes at registration time:

```cpp
world.RegisterSystem(PhysicsSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()),
});

world.RegisterSystem(AnimationSampleSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<AnimatorComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<AnimatorComponent>()),
});

world.RegisterSystem(RenderCullSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<TransformComponent>())
               | MaskBit(ComponentTypeOf<MeshComponent>()),
    .WriteMask = 0,  // read-only
});
```

If a system touches a component that is not declared in its masks, that is a programmer
error — there is no enforcement at the data level, but it means the DAG is wrong and the
scheduler may run conflicting systems in parallel incorrectly. Document and enforce via
code review.

---

## 4. Conflict Rules

Two systems A and B **conflict** (must not run concurrently) if:

```
(A.WriteMask & B.ReadMask)  != 0   // A writes something B reads
(A.WriteMask & B.WriteMask) != 0   // A and B both write the same component
(A.ReadMask  & B.WriteMask) != 0   // A reads something B writes
```

Simplified: any overlap between `(A.WriteMask)` and `(B.ReadMask | B.WriteMask)`, or
between `(B.WriteMask)` and `(A.ReadMask | A.WriteMask)`.

Two systems that only read the same component (both `WriteMask = 0` for that type)
do **not** conflict and may run in parallel.

---

## 5. Explicit Ordering

When two systems conflict, the programmer must declare an explicit ordering edge:

```cpp
// PhysicsSystem must complete before RenderCullSystem
SystemID physics_id = world.RegisterSystem(PhysicsSystem, {...});
SystemID cull_id    = world.RegisterSystem(RenderCullSystem, {...});
world.OrderBefore(physics_id, cull_id);
```

`OrderBefore(A, B)` adds a directed edge A → B in the DAG. This is in addition to any
edges implied by conflict detection. You may add ordering edges between non-conflicting
systems too — for example, to enforce a logical sequence even when there is no data
dependency.

If two systems conflict and no ordering edge exists between them, `Commit()` (see Section 6)
asserts in debug:

```
[ECS Scheduler] ASSERT: PhysicsSystem and RenderCullSystem both write TransformComponent
but no ordering edge exists between them. Call world.OrderBefore() to resolve.
```

---

## 6. DAG Construction

After all systems are registered and ordered, the scheduler builds a directed acyclic graph.

### 5.1 Nodes

One node per registered system. Each node stores:

```cpp
struct SystemNode {
    SystemFn      Fn;
    SystemDeps    Deps;          // ReadMask, WriteMask
    uint32_t      Index;         // stable index into node array
    Array<uint32_t> Successors;  // indices of nodes that must run after this one
    uint32_t      InDegree;      // number of predecessors (used during execution)
};
```

### 5.2 Edge Insertion

For every pair (A, B) where A was registered before B:
1. Check conflict rules (Section 3).
2. If no conflict: no implicit edge. They may run in parallel.
3. If conflict: check that an explicit `OrderBefore(A, B)` or `OrderBefore(B, A)` edge
   exists. If neither exists → assert.
4. Insert the declared ordering edge into the adjacency list.

### 5.3 Cycle Detection

After all edges are inserted, run a depth-first topological sort. If a cycle is detected
(back edge found), assert:

```
[ECS Scheduler] ASSERT: Cycle detected in system dependency graph.
Involved systems: PhysicsSystem → AnimationSampleSystem → PhysicsSystem
```

Cycles are always programmer errors — there is no resolution at runtime.

### 5.4 Topological Layers

Group nodes into parallel waves using Kahn's algorithm:

```
Wave 0: all nodes with InDegree == 0          (no predecessors — run first)
Wave 1: nodes whose InDegree becomes 0 after Wave 0 completes
Wave 2: ...
```

Each wave is a set of systems that can run in parallel. Systems in the same wave have no
conflict with each other and no ordering edge between them.

Example:

```
Registered:
  AnimationSampleSystem  writes: AnimatorComponent
  PhysicsSystem          writes: TransformComponent, reads: RigidBodyComponent
  RenderCullSystem       reads:  TransformComponent, MeshComponent
  AudioSystem            reads:  TransformComponent

OrderBefore(PhysicsSystem, RenderCullSystem)
OrderBefore(PhysicsSystem, AudioSystem)

Resulting waves:
  Wave 0: AnimationSampleSystem, PhysicsSystem     (independent — different write masks)
  Wave 1: RenderCullSystem, AudioSystem            (both depend on PhysicsSystem)
```

---

## 7. `WorldTick` Updated API

```cpp
// ZEngine/ECS/WorldTick.h
#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>

namespace ZEngine::ECS {

    // SystemFn is the canonical type for registered systems.
    // The third parameter is WorldCommands& so systems can enqueue deferred
    // mutations (spawn, destroy, add/remove component) without calling Scene
    // methods directly during parallel execution.
    // Using a raw function pointer avoids std::function heap allocation and
    // virtual dispatch. Systems are called once per wave per frame — not
    // per-entity — so indirect call cost is negligible.
    using SystemFn = void (*)(Scene&, float, WorldCommands&);
    using SystemID = uint32_t;

    struct SystemDeps {
        ArchetypeMask ReadMask  = 0;
        ArchetypeMask WriteMask = 0;
    };

    class WorldTick {
    public:
        // Register a system with its component dependencies.
        // Returns a stable SystemID used for OrderBefore. MUST NOT be discarded —
        // ignoring the returned ID makes OrderBefore unusable for this system.
        [[nodiscard]] SystemID RegisterSystem(SystemFn fn, SystemDeps deps);

        // Declare that system A must complete before system B starts.
        // Required whenever A and B conflict (see Section 3).
        // Use the SystemID returned by RegisterSystem — SystemFn has no equality operator.
        void OrderBefore(SystemID a, SystemID b);

        // Build the DAG. Must be called once after all RegisterSystem/OrderBefore calls
        // and before the first Tick. Asserts on conflicts without ordering and on cycles.
        void Commit();

        // Execute all systems. Single-system waves run inline; multi-system waves
        // dispatch to ThreadPoolHelper with a spin-yield + cv barrier.
        // Blocks until all waves have completed.
        // WorldCommands receives deferred mutations from systems; caller must call
        // commands.Flush(scene) after Tick() returns.
        void Tick(Scene& scene, float delta_time, WorldCommands& commands);

    private:
        struct SystemNode {
            SystemFn              Fn;
            SystemDeps            Deps;
            uint32_t              Index      = 0;
            uint32_t              InDegree   = 0;
            Array<uint32_t>       Successors;
        };

        Array<SystemNode>         m_nodes;
        Array<Array<uint32_t>>    m_waves;     // topological layers, built by Commit()
        bool                      m_committed = false;

        void BuildEdges();
        void TopologicalSort();
        bool HasConflict(const SystemNode& a, const SystemNode& b) const;
    };

}  // namespace ZEngine::ECS
```

---

## 8. Execution

`WorldTick::Tick` walks the wave list produced by `Commit()`.

### Single-system waves — inline path

If a wave contains exactly one system, it runs **inline on the main thread**. There is no
thread pool submission, no barrier, and no synchronization cost. This is not a v2
optimization — it is included in v1 because most DAGs have several single-system waves
(input, physics, animation each typically form their own wave), and dispatching each to
the thread pool at 60 fps would waste 10–20 μs per wave per frame with zero parallelism
benefit.

```
Wave 0: InputSystem           ← single → inline on main thread, zero overhead
Wave 1: PhysicsSystem         ← single → inline on main thread, zero overhead
Wave 2: RenderCull, Audio     ← two systems → dispatch to thread pool, parallel
```

### Multi-system waves — spin-yield barrier

For waves with more than one system, all systems are dispatched to `ThreadPoolHelper` and
the main thread waits for the wave to complete before starting the next.

**Barrier strategy**: spin-yield first (user space, fast for sub-millisecond waves), fall
back to `condition_variable::wait` only if the wave takes longer than ~100 spin iterations
(expensive systems — physics, skinning). A pure `mutex` + `cv` barrier makes a syscall
immediately and is too heavy for fast waves. A pure spin wastes a CPU core when waiting
for a genuinely long system. The two-phase approach handles both.

```cpp
void WorldTick::Tick(Scene& scene, float dt, WorldCommands& commands) {
    ZENGINE_VALIDATE_ASSERT(m_committed,
        "WorldTick::Tick called before Commit()")

    for (uint32_t w = 0; w < m_waves.size(); ++w) {
        const auto& wave = m_waves[w];

        // Fast path: single-system wave runs inline — no thread pool overhead.
        if (wave.size() == 1) {
            SystemFn fn = m_nodes[wave[0]].Fn;
            ZENGINE_VALIDATE_ASSERT(fn != nullptr,
                "WorldTick: null system function pointer in wave")
            fn(scene, dt, commands);
            continue;
        }

        // Multi-system wave: dispatch all systems to the thread pool.
        std::atomic<uint32_t>   remaining{static_cast<uint32_t>(wave.size())};
        std::mutex              mtx;
        std::condition_variable cv;

        for (uint32_t idx : wave) {
            SystemFn fn = m_nodes[idx].Fn;
            ZENGINE_VALIDATE_ASSERT(fn != nullptr,
                "WorldTick: null system function pointer")
            ThreadPoolHelper::Submit([&scene, dt, &commands, fn, &remaining, &cv]() {
                struct Guard {
                    std::atomic<uint32_t>&   r;
                    std::condition_variable& cv;
                    ~Guard() {
                        if (r.fetch_sub(1, std::memory_order_acq_rel) == 1)
                            cv.notify_one();
                    }
                } guard{remaining, cv};
                fn(scene, dt, commands);
            });
        }

        // Phase 1: spin-yield — stays in user space for fast waves (< ~100 μs).
        // Avoids the syscall cost of mutex lock when workers finish quickly.
        for (int spin = 0; spin < 100; ++spin) {
            if (remaining.load(std::memory_order_acquire) == 0) break;
            std::this_thread::yield();
        }

        // Phase 2: fall back to cv.wait for slow waves (physics, skinning, etc.).
        // LIFETIME GUARANTEE: Tick() blocks here until all worker tasks complete.
        // Stack-local variables remain valid for the entire duration of the wait.
        if (remaining.load(std::memory_order_acquire) != 0) {
            std::unique_lock<std::mutex> lock(mtx);
            static constexpr int kWaveTimeoutSeconds = 30;
            bool completed = cv.wait_for(lock,
                std::chrono::seconds(kWaveTimeoutSeconds),
                [&remaining] { return remaining.load() == 0; });
            ZENGINE_VALIDATE_ASSERT(completed,
                "WorldTick::Tick: wave timed out — a system has hung or crashed")
        }
    }
}
```

**Thread safety note**: Each system in a wave reads/writes disjoint component types
(verified by `Commit()`). No `Scene`-level lock is needed — `ComponentStorage<T>` arrays
are partitioned by type and workers never share a storage. `EntityRegistry::ForEachAlive`
is read-only during `Tick`; mutations must go through `WorldCommands`, not called
directly from within a system.

---

## 9. Usage Example

```cpp
// Systems follow the SystemFn signature: void(Scene&, float, WorldCommands&)
void AnimationSampleSystem(Scene& scene, float dt, WorldCommands& commands) {
    scene.ForEach<AnimatorComponent>([dt](EntityID, AnimatorComponent& a) {
        a.Time += dt;
    });
}

void PhysicsSystem(Scene& scene, float dt, WorldCommands& commands) {
    scene.ForEach<TransformComponent, RigidBodyComponent>(
        [dt](EntityID, TransformComponent& t, RigidBodyComponent& rb) {
            t.Position.x += rb.Velocity.x * dt;
            t.Position.y += rb.Velocity.y * dt;
            t.Position.z += rb.Velocity.z * dt;
        });
}

void SpawnProjectileSystem(Scene& scene, float dt, WorldCommands& commands) {
    scene.ForEach<WeaponComponent, TransformComponent>(
        [&](EntityID, WeaponComponent& w, TransformComponent& t) {
            if (w.ShouldFire) {
                // Safe to enqueue — WorldCommands is not applied until after Tick
                commands.SpawnEntity({});
                w.ShouldFire = false;
            }
        });
}

// App startup
WorldTick    world;
WorldCommands commands;
commands.Initialize(&ecs_arena);

SystemID anim_id = world.RegisterSystem(AnimationSampleSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<AnimatorComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<AnimatorComponent>()),
});

SystemID physics_id = world.RegisterSystem(PhysicsSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<RigidBodyComponent>()),
    .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()),
});

SystemID cull_id = world.RegisterSystem(RenderCullSystem, {
    .ReadMask  = MaskBit(ComponentTypeOf<TransformComponent>())
               | MaskBit(ComponentTypeOf<MeshComponent>()),
    .WriteMask = 0,
});

// PhysicsSystem writes Transform, RenderCullSystem reads Transform → conflict
world.OrderBefore(physics_id, cull_id);

world.Commit();  // builds DAG, asserts on any unresolved conflicts or cycles

// Game loop
while (running) {
    float dt = timer.Delta();
    world.Tick(scene, dt, commands);  // Wave 0: AnimationSampleSystem + PhysicsSystem (parallel)
                                      // Wave 1: RenderCullSystem (inline if alone in wave)
    commands.Flush(scene);            // apply deferred spawns/destroys/mutations
    actor_manager.Tick(dt);           // Actor OnTick — sees post-flush entity state
    scene.SnapshotTransforms();       // copy Position → PreviousPosition for interpolation
    renderer.Draw(scene);
}
```

---

## 10. Integration with Actor OnTick

Actor `OnTick` runs after `WorldTick::Tick` completes (see `actor-ecs-architecture.md`
Section 6). It is not a registered system and is not part of the DAG. This is intentional:
Actor gameplay logic reads the fully-updated component state produced by ECS systems.

If an Actor needs to write back to a component during `OnTick`, that write is outside the
scheduler's awareness. The rule is: Actor `OnTick` may write to components that no ECS
system reads in the same frame after `OnTick`. If that rule is violated, the programmer
must restructure — either move the logic into a proper system, or ensure the conflicting
system runs in the next frame.

---

## 11. File Layout

```
ZEngine/
  ECS/
    WorldTick.h
    WorldTick.cpp        (RegisterSystem, OrderBefore, Commit, Tick, BuildEdges, TopologicalSort)
```

No new files beyond what `actor-ecs-architecture.md` already lists.

---

## 12. Deliverables Checklist

- [x] `SystemDeps` struct in `WorldTick.h`
- [x] `WorldTick::RegisterSystem(fn, deps)` — returns `SystemID`
- [x] `WorldTick::OrderBefore(SystemID a, SystemID b)`
- [x] `RegisterSystem` returns distinct IDs per system (monotonically increasing)
- [x] `WorldTick::Commit()` — edge insertion, cycle detection, topological sort into waves
- [x] `WorldTick::Tick()` — wave-by-wave dispatch via `ThreadPoolHelper`, barrier between waves
- [x] Assert on conflict with no ordering edge (debug builds)
- [x] Assert on cycle detected
- [x] Assert on `Tick()` called before `Commit()`
- [x] `tests/ECS/SchedulerTest.cpp`:
  - [x] Two independent systems run in the same wave
  - [x] Two conflicting systems with `OrderBefore` run in separate waves
  - [x] Two conflicting systems with no `OrderBefore` assert in debug
  - [x] Cycle detection asserts
  - [x] Systems execute in correct order (write before read verified via component state)

---

## 13. Commit convention

All commits on this feature must follow the project's conventional commit rules enforced
by commitlint (`.commitlintrc.json` at repo root).

**Allowed types:**

| Type | When to use |
|---|---|
| `feat` | New system, new capability (e.g. `feat(ecs): implement WorldTick with wave dispatch`) |
| `fix` | Correctness bug in scheduler or wave barrier |
| `perf` | Performance improvement (e.g. spin-yield barrier, inline single-system wave) |
| `test` | Adding or updating `SchedulerTest.cpp` |
| `docs` | Changes to this document |
| `refactor` | Internal restructure with no behaviour change |

**Rules enforced by commitlint:**

- Type must be lowercase and one of the list above — `build`, `ci`, `chore`, `revert`, `style` are also valid for non-ECS work
- Scope must be lowercase — use `ecs` for scheduler work (e.g. `feat(ecs): ...`)
- Subject must not be empty and must not end with a period
- Header (type + scope + subject) must not exceed 100 characters
- Body and footer must each begin with a blank line if present

**Example commit messages:**

```
feat(ecs): implement WorldTick with Kahn wave dispatch and conflict detection

fix(ecs): single-system wave skips thread pool submission

perf(ecs): replace mutex/cv barrier with spin-yield fallback

test(ecs): add SchedulerTest covering conflict assert and cycle detection
```
