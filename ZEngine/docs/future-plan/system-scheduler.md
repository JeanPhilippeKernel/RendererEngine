# ZEngine — System Scheduler

**Priority:** P1 — Implement alongside ECS core (Phase 1 of migration-plan.md)  
**Status:** Design  
**Depends on:** `actor-ecs-architecture.md`  
**Blocks:** `animation-system.md`

---

## 1. Overview

The system scheduler sits on top of `WorldTick` and `ThreadPoolHelper`. Its job is to
dispatch ECS systems in parallel where safe, and serialize them where a data conflict exists.

Safety is determined by component read/write masks declared at registration. Two systems
that conflict (one writes what the other reads or writes) must not run concurrently. Two
systems with no overlap can run in parallel.

Conflicts without an explicit ordering edge are a programming error — the scheduler asserts
in debug builds rather than silently serializing.

---

## 2. System Registration

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

## 3. Conflict Rules

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

## 4. Explicit Ordering

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

## 5. DAG Construction

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

## 6. `WorldTick` Updated API

```cpp
// ZEngine/ECS/WorldTick.h
#pragma once
#include <ECS/Scene.h>
#include <ECS/ArchetypeMask.h>
#include <Core/Containers/Array.h>
#include <functional>

namespace ZEngine::ECS {

    // SystemFn is the canonical type for registered systems.
    // Using a raw function pointer avoids std::function's heap allocation and
    // virtual dispatch overhead. Systems are called once per wave per frame —
    // not per-entity — so indirect call cost is minimal, but heap-free registration
    // keeps the system list in a flat array without indirection.
    using SystemFn = void (*)(Scene&, float);
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

        // Execute all systems. Dispatches parallel waves onto ThreadPoolHelper.
        // Blocks until all systems in all waves have completed.
        void Tick(Scene& scene, float delta_time);

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

## 7. Execution

`WorldTick::Tick` walks the wave list produced by `Commit()`. Each wave is dispatched to
`ThreadPoolHelper` and the main thread waits for all systems in the wave before starting
the next.

```cpp
void WorldTick::Tick(Scene& scene, float dt) {
    ZENGINE_VALIDATE_ASSERT(m_committed, "WorldTick::Tick() called before Commit() — call Commit() once after all RegisterSystem/OrderBefore calls")

    for (const auto& wave : m_waves) {
        std::atomic<uint32_t> remaining{(uint32_t)wave.Size()};
        std::mutex            mtx;
        std::condition_variable cv;

        for (uint32_t idx : wave) {
            SystemFn fn = m_nodes[idx].Fn;
            ZENGINE_VALIDATE_ASSERT(fn != nullptr,
                "WorldTick: system %u has a null function pointer — was it registered correctly?", idx);
            ThreadPoolHelper::Submit([&scene, dt, fn, &remaining, &cv]() {
                // RAII guard ensures remaining is decremented even if fn asserts/aborts.
                // Note: if fn throws (impossible in -fno-exceptions builds), std::terminate fires.
                struct Guard {
                    std::atomic<uint32_t>& r;
                    std::condition_variable& cv;
                    ~Guard() {
                        if (r.fetch_sub(1, std::memory_order_acq_rel) == 1)
                            cv.notify_one();
                    }
                } guard{remaining, cv};
                fn(scene, dt);
            });
        }

        // Wait for wave to complete before starting the next.
        // THREAD SAFETY: Each system in this wave reads/writes disjoint component
        // types (verified by Commit()). No Scene-level lock is needed — component
        // storage arrays are partitioned by type and workers never share a storage.
        // EntityRegistry::ForEachAlive is read-only during Tick; DestroyEntity and
        // AddComponent must be deferred to a WorldCommands buffer, not called from
        // within a system (see actor-ecs-architecture.md §WorldCommands).
        //
        // cv.wait with predicate re-checks before blocking — safe even if all
        // workers finish and call notify_one() before this line is reached.
        // Bounded wait detects hung or crashed systems.
        std::unique_lock<std::mutex> lock(mtx);
        // LIFETIME GUARANTEE: Tick() blocks here until ALL worker tasks complete.
        // Stack-local variables (scene, remaining, cv, mtx) remain valid for the
        // entire duration of the wait. The lambda's reference captures are safe.
        // If this assert fires, a system has hung — check for infinite loops or
        // deadlocks in the flagged system, not in the engine infrastructure.
        static constexpr int kWaveTimeoutSeconds = 30;  // dev: 30s; disable in release
        bool completed = cv.wait_for(lock, std::chrono::seconds(kWaveTimeoutSeconds),
            [&remaining] { return remaining.load() == 0; });
        ZENGINE_VALIDATE_ASSERT(completed,
            "WorldTick::Tick: wave timed out after 30 seconds — a system has hung or crashed");
    }
}
```

For waves with a single system, `ThreadPoolHelper::Submit` still dispatches to the thread
pool. If a wave has exactly one system and that system is cheap, the overhead of
submission + sync outweighs the benefit. An optimization for v2: run single-system waves
inline on the main thread.

---

## 8. Usage Example

```cpp
// App startup
WorldTick world;

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
    world.Tick(scene, dt);        // Wave 0: AnimationSampleSystem + PhysicsSystem (parallel)
                                  // Wave 1: RenderCullSystem
    actor_manager.Tick(dt);       // Actor OnTick — after all ECS systems
    renderer.Draw(scene);
}
```

---

## 9. Integration with Actor OnTick

Actor `OnTick` runs after `WorldTick::Tick` completes (see `actor-ecs-architecture.md`
Section 6). It is not a registered system and is not part of the DAG. This is intentional:
Actor gameplay logic reads the fully-updated component state produced by ECS systems.

If an Actor needs to write back to a component during `OnTick`, that write is outside the
scheduler's awareness. The rule is: Actor `OnTick` may write to components that no ECS
system reads in the same frame after `OnTick`. If that rule is violated, the programmer
must restructure — either move the logic into a proper system, or ensure the conflicting
system runs in the next frame.

---

## 10. File Layout

```
ZEngine/
  ECS/
    WorldTick.h
    WorldTick.cpp        (RegisterSystem, OrderBefore, Commit, Tick, BuildEdges, TopologicalSort)
```

No new files beyond what `actor-ecs-architecture.md` already lists.

---

## 11. Deliverables Checklist

- [ ] `SystemDeps` struct in `WorldTick.h`
- [ ] `WorldTick::RegisterSystem(fn, deps)` — returns `SystemID`
- [ ] `WorldTick::OrderBefore(SystemID a, SystemID b)`
- [ ] `RegisterSystem` returns distinct IDs per system (monotonically increasing)
- [ ] `WorldTick::Commit()` — edge insertion, cycle detection, topological sort into waves
- [ ] `WorldTick::Tick()` — wave-by-wave dispatch via `ThreadPoolHelper`, barrier between waves
- [ ] Assert on conflict with no ordering edge (debug builds)
- [ ] Assert on cycle detected
- [ ] Assert on `Tick()` called before `Commit()`
- [ ] `tests/ECS/SchedulerTest.cpp`:
  - [ ] Two independent systems run in the same wave
  - [ ] Two conflicting systems with `OrderBefore` run in separate waves
  - [ ] Two conflicting systems with no `OrderBefore` assert in debug
  - [ ] Cycle detection asserts
  - [ ] Systems execute in correct order (write before read verified via component state)
