# ZEngine — Hybrid Actor-ECS Architecture

**Priority:** P1  
**Status:** Core Implemented — ECS, Actor, WorldTick, WorldCommands all live; missing: MeshComponent, NameComponent, ECS↔RenderScene bridge, Outliner UI (tracked in issue #604)  
**Blocks:** `system-scheduler.md`, `animation-system.md`, `scene-serialization.md`

## Capacity constants

| Constant | Value | Rationale |
|---|---|---|
| `ActorManager::MAX_ACTORS` | 1024 | Tier 1 objects are few by design (players, cameras, lights, key NPCs, vehicles). Rarely exceeds a few hundred in practice. |
| `EntityRegistry::MAX_ENTITIES` | 65536 | Covers all EntityIDs — Tier 1 Actors + Tier 2 pure ECS entities combined. Sufficient for mid-scale games (action, RPG, ~32-player shooters). High-density objects (foliage, particles, building pieces) belong in dedicated systems, not the ECS. |

Memory cost at these limits (ECS sub-arena budget: 128 MB):
- `EntityRegistry` slots: 65536 × 12 bytes = ~750 KB
- `ActorManager` handle array: 1024 × sizeof(Actor) — Actor base is ~24 bytes = ~25 KB
- `ComponentStorage<T>` dense arrays dominate; with 8 component types at 30% occupancy: ~75 MB

---

## 1. Overview

ZEngine uses a two-tier object model:

| Tier | Name | Use case | Example |
|---|---|---|---|
| 1 | **Actor** | Few, complex, identity-bearing objects | Player, camera, directional light, key NPC |
| 2 | **ECS Entity** | Many, simple, data-only objects | Foliage, particles, projectiles, crowd agents |

Both tiers live in the same `ECS::Scene`. Both are backed by an `EntityID`. ECS systems query
both tiers uniformly — a `ForEach<TransformComponent, RigidBodyComponent>` hits Actors and
pure ECS entities alike with no special cases.

The programmer decides explicitly which tier an object belongs to. There is no automatic
promotion or demotion.

```
┌─────────────────────────────────────────────────────────────┐
│                        ECS::Scene                           │
│                                                             │
│  EntityRegistry   ComponentStorage<T>...   ArchetypeMasks   │
│                                                             │
│  Tier 1 entities ──────────────────────────────────────┐   │
│  (Actor-backed)    same slots, same dense arrays        │   │
│  Tier 2 entities ──────────────────────────────────────┘   │
│  (pure ECS)                                                 │
└──────────────────────────┬──────────────────────────────────┘
                           │ EntityID
              ┌────────────▼────────────┐
              │          Actor          │
              │  owns EntityID          │
              │  delegates component    │
              │  access to Scene        │
              │  has virtual OnTick()   │
              └─────────────────────────┘
```

---

## 2. Tier 2 — Pure ECS Entity

A Tier 2 entity is a bare `EntityID`. No heap object. No vtable. Just data in component
storage arrays.

```cpp
// Create a Tier 2 entity
EntityID grass = scene.CreateEntity();
scene.AddComponent<TransformComponent>(grass, {});
scene.AddComponent<MeshComponent>(grass, { mesh_handle });
```

Lifetime: `scene.DestroyEntity(id)` — removes the entity and all its components in one call.

---

## 3. Tier 1 — Actor

An Actor is a C++ object that:
- Owns exactly one `EntityID`
- Holds a non-owning reference to the `ECS::Scene` it lives in
- Exposes component access as thin wrappers over `Scene`
- Has virtual lifecycle hooks (`OnCreate`, `OnDestroy`, `OnTick`)
- Is accessed externally via `Helpers::Handle<Actor>` — a generational index into `ActorManager`'s arena-backed slot array

**`Ref<Actor>` (intrusive ref-counting) is NOT used.** Ref-counting is incompatible with the arena allocator model: the destructor would fire at an unpredictable time driven by the last pointer going out of scope, rather than at an explicit engine-controlled point. `Handle<Actor>` provides the same stale-handle safety via generation checks with zero atomic overhead and no heap allocation.

### 3.1 `Actor` base class

```cpp
// ZEngine/ECS/Actor.h
#pragma once
#include <ECS/Scene.h>
#include <Helpers/HandleManager.h>

namespace ZEngine::ECS {

    class Actor {
    public:
        [[nodiscard]] EntityID      GetEntityID() const { return m_entity_id; }
        [[nodiscard]] bool          IsAlive()     const;

        // Component access — delegates to m_scene
        template<typename T>
        void             AddComponent(T component);

        template<typename T>
        [[nodiscard]] T* GetComponent();

        template<typename T>
        [[nodiscard]] const T* GetComponent() const;

        template<typename T>
        [[nodiscard]] bool HasComponent() const;

        template<typename T>
        void             RemoveComponent();

        // Lifecycle — override in subclasses
        virtual void OnCreate()        {}
        virtual void OnDestroy()       {}
        virtual void OnTick(float dt)  {}

    protected:
        Actor() = default;
        virtual ~Actor() = default;

    private:
        friend class ActorManager;   // only ActorManager sets these fields

        EntityID  m_entity_id = INVALID_ENTITY;
        Scene*    m_scene     = nullptr;  // non-owning; set by ActorManager::Create
    };

}  // namespace ZEngine::ECS
```

Actors are allocated inside `ActorManager`'s `HandleManager<Actor>` slot array (arena-backed).
External code holds `Helpers::Handle<Actor>` — 16 bytes (`{Index, Generation}`), no vtable, no count.
Destruction is explicit: `ActorManager::Destroy(handle)` calls `OnDestroy()` then `scene.DestroyEntity(id)`.
No `Detach()` method needed — `ActorManager::Shutdown()` runs before `Scene::Shutdown()`, guaranteed by engine lifecycle order.

### 3.2 Subclassing

Gameplay objects subclass `Actor` and override the lifecycle hooks:

```cpp
class PlayerActor : public ZEngine::ECS::Actor {
public:
    void OnCreate() override {
        AddComponent<TransformComponent>({});
        AddComponent<RigidBodyComponent>({});
        AddComponent<CameraComponent>({});
    }

    void OnTick(float dt) override {
        auto* transform = GetComponent<TransformComponent>();
        // read input, move transform ...
    }
};

// Creation — returns a Handle<Actor>, not a pointer or Ref
Helpers::Handle<Actor> player = actor_manager.Create<PlayerActor>();

// Access
if (Actor* a = actor_manager.Access(player))
    a->OnTick(dt);

// Destruction — explicit, engine-controlled
actor_manager.Destroy(player);
// player handle is now stale; actor_manager.Access(player) returns nullptr
```

### 3.3 `ActorManager`

`ActorManager` owns all Actor objects in a `HandleManager<Actor>` backed by the ECS sub-arena.

```cpp
// ZEngine/ECS/ActorManager.h
class ActorManager {
public:
    static constexpr uint32_t MAX_ACTORS = 1024;

    void Initialize(Core::Memory::ArenaAllocator* arena, Scene& scene);

    // Allocates an Actor slot, creates an EntityID, calls OnCreate().
    // Returns a generational handle — 16 bytes, no heap, no ref count.
    template<typename T = Actor>
    Helpers::Handle<Actor> Create();

    // Calls OnDestroy(), destroys the EntityID, frees the slot.
    // Stale handles (already destroyed) are silently ignored.
    void Destroy(Helpers::Handle<Actor> handle);

    // Returns nullptr if handle is stale.
    Actor*       Access(Helpers::Handle<Actor> handle);
    const Actor* Access(Helpers::Handle<Actor> handle) const;

    bool IsLive(Helpers::Handle<Actor> handle) const;

    // Calls OnTick(dt) on all live Actors.
    void Tick(float dt);

    // Destroys all live Actors in reverse creation order, then shuts down the handle array.
    // Must be called before Scene::Shutdown().
    void Shutdown();

private:
    Helpers::HandleManager<Actor> m_handles;
    Scene*                        m_scene = nullptr;
};
```

### 3.3 Lifetime Rules

- `ActorManager::Create<T>()` allocates a slot in the arena, creates an `EntityID` in the
  scene, sets `Actor::m_scene` and `Actor::m_entity_id`, then calls `OnCreate()`.
  Returns a `Handle<Actor>` — the only way to reference an Actor externally.
- `ActorManager::Destroy(handle)` calls `OnDestroy()`, calls `m_scene->DestroyEntity(id)`,
  then frees the slot (increments generation). All existing handles to this Actor become stale.
- `ActorManager::Shutdown()` is called by the engine before `Scene::Shutdown()`, guaranteed
  by the engine lifecycle order defined in `engine-lifecycle.md`. No manual `Detach()` needed.
- Two Actors must never wrap the same `EntityID`. Asserted in debug builds in `Create()`.
- Actors are never heap-allocated individually. The arena owns the backing memory.

### 3.4 Component Access Implementation

All component methods on `Actor` delegate directly to `m_scene`. There is no intermediate
storage. The component data lives entirely in `ComponentStorage<T>` inside the Scene.

```cpp
template<typename T>
void Actor::AddComponent(T&& component) {
    ZENGINE_VALIDATE_ASSERT(IsAlive(), "Actor entity is not alive")
    m_scene->AddComponent<T>(m_entity_id, std::forward<T>(component));
}

template<typename T>
T* Actor::GetComponent() {
    ZENGINE_VALIDATE_ASSERT(IsAlive(), "Actor entity is not alive")
    return m_scene->GetComponent<T>(m_entity_id);
}
```

---

## 4. ECS Core

### Concepts: Archetype and Sparse-Set

Two ideas underpin the entire ECS implementation. Understanding them makes every design
decision in this section obvious.

**Archetype**

An archetype is the set of component types an entity has — its "shape". An entity with
`{TransformComponent, RigidBodyComponent}` has a different archetype from one with just
`{TransformComponent}`. We encode the archetype as a bitmask: one bit per component type.

```
ComponentTypeID:     0                   1                  2
                     TransformComponent  RigidBodyComponent  MeshComponent

Entity A  mask:      1                   1                  0   = 0b011
Entity B  mask:      1                   0                  1   = 0b101
Entity C  mask:      1                   1                  1   = 0b111
```

`ForEach<TransformComponent, RigidBodyComponent>` computes `required = 0b011` and
eliminates non-matching entities with a single bitwise AND — O(1) per entity. Without
this guard, `ForEach` would call `ComponentStorage::Get` on every entity for every
component type, degrading to O(entities × types). The mask check is not an optimization;
it is a correctness-preserving performance requirement.

**Sparse-Set**

A sparse-set gives O(1) lookup by `EntityID` while keeping component data in a dense,
packed array for cache-friendly iteration. Three arrays work together:

```
m_sparse    [entity Index → dense index, or INVALID if absent]
m_dense     [packed component data, no holes]
m_dense_ids [full EntityID at each dense slot — for generation check]
```

Example — 4 entities, entities 0 and 3 have `TransformComponent`:

```
m_sparse:    [0]   [INV] [INV] [1]     entity 0 → dense[0], entity 3 → dense[1]

m_dense:     [T0]  [T3]                packed, no gaps
m_dense_ids: [{0,1}] [{3,1}]           full EntityID stored for generation check
```

`Get(entity 3)`: `m_sparse[3] = 1` → check `m_dense_ids[1] == {3,1}` → return `&m_dense[1]`.

`Remove(entity 0)` uses swap-and-pop to keep the dense array packed:
swap `m_dense[0]` with the last element, update `m_sparse` for the moved entity, pop.
The dense array stays contiguous — `ForEach` iterates it with zero cache misses.

**How they work together**

```
ForEach<TransformComponent, RigidBodyComponent>(fn)
    |
    +-- EntityRegistry::ForEachAlive   iterate live entity slots
    |       for each entity:
    |           MaskMatches(mask, required)?  <- ARCHETYPE check, O(1)
    |               no  -> skip (one bitwise AND)
    |               yes -> call fn(id, transform, rigidbody)
    |
    +-- ComponentStorage::Get          <- SPARSE-SET lookup, O(1)
            m_sparse[id.Index] -> dense_idx
            generation check (m_dense_ids[dense_idx] == id?)
            return &m_dense[dense_idx]   direct pointer into packed array
```

Archetype filtering keeps the number of `Get` calls small.
Sparse-set makes each `Get` O(1) with cache-friendly data for the entities that do match.

---

### 4.1 `EntityID`

```cpp
// ZEngine/ECS/EntityID.h
struct EntityID {
    uint32_t Index      = 0;
    uint32_t Generation = 0;

    bool IsValid() const { return Generation != 0; }
    bool operator==(const EntityID&) const = default;
};

constexpr EntityID INVALID_ENTITY = {0, 0};
```

- `Index` is a stable slot index, reused across entity lifetimes.
- `Generation` starts at 1. 0 is the sentinel for `INVALID_ENTITY`.
- Stale handles (pointing to a recycled slot) are automatically rejected — the stored
  generation in the slot will have incremented.

### 4.2 `ComponentTypeID`

```cpp
// ZEngine/ECS/ComponentTypeID.h
using ComponentTypeID = uint32_t;

namespace detail {
    inline uint32_t NextTypeID() {
        static std::atomic<uint32_t> counter{0};
        return counter.fetch_add(1, std::memory_order_acq_rel);
    }
}

template<typename T>
ComponentTypeID ComponentTypeOf() {
    static uint32_t id = detail::NextTypeID();
    return id;
}
```

Counter is atomic — safe when types are registered from multiple threads during startup.
IDs are process-stable; not serializable across runs.

### 4.3 `ArchetypeMask`

```cpp
// ZEngine/ECS/ArchetypeMask.h
using ArchetypeMask = uint64_t;

inline ArchetypeMask MaskBit(ComponentTypeID id) {
    ZENGINE_VALIDATE_ASSERT(id != UINT32_MAX,
        "MaskBit: ComponentTypeID is UINT32_MAX — uninitialized or invalid type ID");
    ZENGINE_VALIDATE_ASSERT(id < 64,
        "MaskBit: component type ID %u exceeds ArchetypeMask v1 capacity (64)", id);
    return uint64_t(1) << id;
}

inline bool MaskHas(ArchetypeMask mask, ComponentTypeID id) {
    return (mask >> id) & 1;
}

inline bool MaskMatches(ArchetypeMask mask, ArchetypeMask required) {
    return (mask & required) == required;
}
```

v1 cap: 64 component types. v2 path: replace `uint64_t` with `std::bitset<128>`.

### 4.4 `ComponentStorage<T>`

Sparse-set. One per component type, owned by `ECS::Scene`.

```
m_dense      — packed array of T, contiguous, no holes
m_dense_ids  — EntityID at each dense index (stores index + generation)
m_sparse     — entity Index → dense index (UINT32_MAX = absent)
```

**Generation check on Get/Has/Remove** — `m_dense_ids` stores the full `EntityID` (not just
the index). Before returning a pointer, compare the stored `EntityID` against the requested
one. This prevents a stale handle from seeing a recycled slot's component:

```cpp
T* Get(EntityID id) {
    if (id.Index >= m_sparse.Size()) return nullptr;
    uint32_t dense_idx = m_sparse[id.Index];
    if (dense_idx == UINT32_MAX) return nullptr;
    if (m_dense_ids[dense_idx] != id) return nullptr;  // generation mismatch = stale
    return &m_dense[dense_idx];
}
```

`Has()` and `Remove()` apply the same generation check.

### 4.5 `EntityRegistry`

Generational slot allocator with free-list.

```cpp
static constexpr uint32_t MAX_ENTITIES = 65536;  // Tier 1 + Tier 2 combined
```

```
m_slots      — Array<EntitySlot{Generation, ArchetypeMask}>  (MAX_ENTITIES slots, arena-allocated)
m_free_list  — Array<uint32_t> of recyclable slot indices
m_alive_count
```

`Create()`: pop from free-list or append new slot. Increment generation, skip 0.  
`Destroy(id)`: assert alive, increment generation (skip 0), push index onto free-list.  
`IsAlive(id)`: `id.Generation != 0 && id.Index < m_slots.Size() && m_slots[id.Index].Generation == id.Generation`.

> **Note:** The `id.Generation != 0` guard ensures `INVALID_ENTITY {0,0}` is never considered alive, even if slot 0 exists with generation 0.

### 4.6 `ECS::Scene`

Top-level context. Owns the registry and all component storages.

```cpp
class Scene {
public:
    // Tier 2 entity lifetime
    EntityID CreateEntity();
    void     DestroyEntity(EntityID id);   // removes all components, frees slot
    bool     IsAlive(EntityID id) const;
    ArchetypeMask GetMask(EntityID id) const;

    // Component access
    template<typename T> void     AddComponent(EntityID id, T component);
    template<typename T> T*       GetComponent(EntityID id);
    template<typename T> const T* GetComponent(EntityID id) const;
    template<typename T> void     RemoveComponent(EntityID id);
    template<typename T> bool     HasComponent(EntityID id) const;

    // Returns the ArchetypeMask for a living entity (0 for dead/invalid).
    [[nodiscard]] ArchetypeMask GetMask(EntityID id) const;

    // Query — hits ALL living entities (both tiers).
    // Fn must be callable as: void(EntityID, Ts&...)
    // A stateless lambda or a free function matching this signature is required.
    // Passing a callable with the wrong signature produces a compile-time error.
    // Example:
    //   scene.ForEach<TransformComponent, RigidBodyComponent>(
    //       [](EntityID id, TransformComponent& t, RigidBodyComponent& rb) { ... });
    // Template avoids std::function heap allocation and virtual dispatch in hot path.
    template<typename... Ts, typename Fn>
    void ForEach(Fn&& fn);

private:
    EntityRegistry m_registry;
    Core::Containers::UnorderedHashMap<
        ComponentTypeID,
        std::unique_ptr<IComponentStorage>> m_storages;
};
```

`DestroyEntity` iterates all storages and calls `RemoveRaw(id)` on each before calling
`m_registry.Destroy(id)`. This ensures no orphaned component data.

### 4.7 `Query<Ts...>`

Reusable query object. Pre-computes `ArchetypeMask` once at construction; reuses it every
frame. Wraps `Scene::ForEach`.

```cpp
template<typename... Ts>
class Query {
public:
    explicit Query(Scene& scene) : m_scene(scene) {
        m_mask = (MaskBit(ComponentTypeOf<Ts>()) | ...);
    }
    // Template Fn — avoids std::function heap allocation and virtual dispatch.
    // Fn must be callable as void(EntityID, Ts&...).
    template<typename Fn>
    void ForEach(Fn&& fn) {
        m_scene.ForEach<Ts...>(std::forward<Fn>(fn));
    }
private:
    Scene&        m_scene;
    ArchetypeMask m_mask;
};
```

**Critical implementation note for `Scene::ForEach`**: the mask-and early-exit
**must** be the first check in the loop body, before any `ComponentStorage::Get` call:

```cpp
template<typename... Ts, typename Fn>
void Scene::ForEach(Fn&& fn) {
    const ArchetypeMask required = (MaskBit(ComponentTypeOf<Ts>()) | ...);
    m_registry.ForEachAlive([&](EntityID id) {
        // Skip entities that don't have all required components — O(1) bitmask test.
        // Without this guard, ForEach degrades to O(entities × component_types)
        // because ComponentStorage::Get must walk the sparse array for every entity.
        if (!MaskMatches(m_registry.GetMask(id), required)) return;
        fn(id, *GetComponent<Ts>(id)...);
    });
}
```

Omitting the `MaskMatches` guard is a correctness-preserving but catastrophic performance
bug on scenes with thousands of entities. The guard must not be "added as an optimization
later" — it belongs in the first implementation.

### 4.8 `WorldTick`

`WorldTick` is defined in full in `system-scheduler.md`. It owns the DAG, system
registration, conflict detection, and parallel wave dispatch. Do not implement
`WorldTick` from this document — use `system-scheduler.md` as the authoritative source.

---

## 5. Component Ownership Rules

| Rule | Rationale |
|---|---|
| Components are plain data structs — no virtual methods, no behavior | Keeps dense arrays cache-clean; behavior lives in systems or Actor subclasses |
| Every component header must include `static_assert(sizeof(T) <= 64, "Component exceeds cache line")` and `static_assert(alignof(T) <= 16, "Component misaligned")` | Prevents accidental padding and cache-inefficient layouts |
| Component types live in `ZEngine::ECS::Components` namespace | Separate from the old `ZEngine::Rendering::Components` types during transition |
| The existing `TransformComponent` in `Rendering/Components/` is **not** reused | It has methods and a computed `Mat4f` — not plain data. A new `ECS::Components::TransformComponent` with plain `Vec3f position/rotation/scale` replaces it in ECS context |
| Actor component access always goes through `Scene` | No component data lives on the Actor object itself |
| One component type per entity — no duplicate `AddComponent<T>` on the same entity | Asserted in `ComponentStorage::Add` |

---

## 6. Cross-Tier Interaction

Because Actor component data lives in the same `ComponentStorage<T>` arrays as Tier 2
entities, ECS systems require no special handling:

```cpp
// This hits both PlayerActor (Tier 1) and foliage entities (Tier 2)
scene.ForEach<TransformComponent, RigidBodyComponent>(
    [dt](EntityID id, TransformComponent& t, RigidBodyComponent& rb) {
        t.Position += rb.Velocity * dt;
    });
```

The Actor `OnTick` is called separately by the engine's Actor manager — it is the Actor's
private update slot for gameplay logic that does not belong in a shared system (input
reading, camera control, etc.). It runs after all ECS systems have ticked, so it sees
the updated component state.

```
Frame N:
  1. WorldTick::Tick(scene, dt)   — all ECS systems (hits Tier 1 + Tier 2)
  2. ActorManager::Tick(dt)       — calls OnTick on each live Actor
  3. Render
```

---

## 7. `WorldCommands` — Deferred Entity Mutations

ECS systems run in parallel across multiple threads during `WorldTick::Tick`. Calling
`scene.DestroyEntity()`, `scene.AddComponent()`, or `scene.RemoveComponent()` from within
a system is **not safe** — it mutates the entity registry and component storage arrays
while other workers may be iterating them.

The solution is a `WorldCommands` buffer: a per-frame accumulator of deferred mutations
that is applied atomically after all waves in `WorldTick::Tick` have completed.

### 7.1 `WorldCommands` declaration

```cpp
// ZEngine/ECS/WorldCommands.h
#pragma once
#include <ECS/EntityID.h>
#include <ECS/ComponentTypeID.h>
#include <Core/Containers/Array.h>
#include <functional>

namespace ZEngine::ECS {

    class Scene;

    class WorldCommands {
    public:
        // Queue entity creation. The new EntityID is not available until Flush().
        // Use a callback to receive it: SpawnEntity([](EntityID id){ ... })
        void SpawnEntity(std::function<void(EntityID)> on_spawned = {});

        // Queue entity destruction. Safe to call on an entity that is already
        // queued for destruction — duplicate destroys are silently ignored.
        void DestroyEntity(EntityID id);

        // Queue component add. T must be a registered component type.
        template<typename T>
        void AddComponent(EntityID id, T component);

        // Queue component removal.
        template<typename T>
        void RemoveComponent(EntityID id);

        // Apply all queued commands to the scene in submission order.
        // Called by the engine once per frame AFTER WorldTick::Tick completes.
        // Must not be called from within a system.
        void Flush(Scene& scene);

        // Discard all queued commands without applying them.
        // Used when transitioning scenes or resetting world state.
        void Clear();

        [[nodiscard]] bool IsEmpty() const;

    private:
        enum class CommandKind : uint8_t {
            SpawnEntity, DestroyEntity, AddComponent, RemoveComponent
        };
        struct Command {
            CommandKind Kind;
            EntityID    Target;           // INVALID_ENTITY for SpawnEntity
            ComponentTypeID TypeID;       // 0 for entity-only commands
            Core::Containers::Array<uint8_t> Data;  // serialized component bytes
            std::function<void(EntityID)>    OnSpawned;
        };
        Core::Containers::Array<Command> m_commands;
    };

} // namespace ZEngine::ECS
```

### 7.2 Usage pattern inside a system

```cpp
// Systems receive WorldCommands& as a third parameter
void SpawnProjectileSystem(Scene& scene, float dt, WorldCommands& commands) {
    scene.ForEach<WeaponComponent, TransformComponent>(
        [&](EntityID id, WeaponComponent& w, TransformComponent& t) {
            if (w.ShouldFire) {
                commands.SpawnEntity([pos = t.Position](EntityID proj_id) {
                    // This callback runs in Flush() on the main thread —
                    // safe to call scene methods here
                });
                w.ShouldFire = false;
            }
        });
}
```

### 7.3 Flush ordering in the main loop

```
WorldTick::Tick(scene, dt)    ← systems may enqueue into WorldCommands
world_commands.Flush(scene)   ← apply spawns/destroys/component mutations
actor_manager.Tick(dt)        ← Actors see the post-flush entity state
scene.SnapshotTransforms()    ← must be AFTER Tick and Flush
```

`Flush` processes commands in submission order. A `SpawnEntity` followed by
`AddComponent<T>` on that entity's ID (received via `on_spawned` callback) is valid.
A `DestroyEntity` followed by `AddComponent` on the same ID in the same flush is a
programmer error and asserts in debug.

### 7.4 What is NOT in `WorldCommands`

- Scene load/unload — handled by `engine-lifecycle.md`
- Actor creation — `Actor::Create(scene)` is safe to call from `Actor::OnTick` since
  that runs after `WorldTick::Tick` completes

---

## 8. Memory Layout

All ECS memory is carved from a single `ECSScene` sub-arena (128 MB, budgeted in
`MemoryBudgetConfig`). Nothing is heap-allocated individually.

```
MemoryManager::MainArena  (3 GB)
│
└── ECSScene sub-arena  (128 MB)
    │
    ├── EntityRegistry::m_slots          [65536 × 12 B = ~750 KB]
    │     EntitySlot { Generation(4), ArchetypeMask(8) }
    │     [0][1][2]...[65535]
    │      ↑   ↑
    │      │   └── Tier 2 pure ECS entity (dense data, no object)
    │      └────── Tier 1 Actor entity (EntityID stored in Actor object)
    │
    ├── EntityRegistry::m_free_list      [up to 65536 × 4 B = ~256 KB]
    │     recycled slot indices
    │
    ├── ActorManager::m_handles          [HandleManager<Actor>]
    │     Slot array: 1024 × sizeof(Actor) ≈ 1024 × 24 B = ~25 KB
    │     Generation array: 1024 × 8 B = ~8 KB
    │     Free-list next: 1024 × 4 B = ~4 KB
    │
    │     Slot 0: Actor { m_entity_id={0,1}, m_scene=* }  ← PlayerActor
    │     Slot 1: Actor { m_entity_id={1,1}, m_scene=* }  ← CameraActor
    │     Slot 2: Actor { m_entity_id={2,1}, m_scene=* }  ← DirectionalLight
    │     ...
    │     Slot 1023: (free)
    │
    ├── ComponentStorage<TransformComponent>
    │     m_dense     [packed array of TransformComponent, no holes]
    │     │  [T0][T1][T2][T3]...[Tn]
    │     m_dense_ids [EntityID at each dense index]
    │     │  [e0][e1][e2][e3]...[en]
    │     m_sparse    [entity Index → dense index, UINT32_MAX = absent]
    │        [0→0][1→1][2→2][3→UINT32_MAX][4→3]...
    │
    │     NOTE: Tier 1 Actor entities and Tier 2 pure entities occupy the
    │     same dense slots — no separation, uniform iteration.
    │
    ├── ComponentStorage<RigidBodyComponent>
    │     (same layout)
    │
    ├── ComponentStorage<MeshComponent>
    │     (same layout)
    │
    ├── ... (one storage per registered component type, up to 64 in v1)
    │
    ├── WorldCommands::m_commands        [per-frame deferred mutation buffer]
    │     cleared each frame after Flush()
    │
    └── Query scratch / system temporaries
```

### How a Handle<Actor> resolves to component data

```
Handle<Actor> player = { Index=0, Generation=1 }
    │
    ▼
ActorManager::m_handles[0]
    Actor { m_entity_id = {Index=0, Generation=1}, m_scene = &scene }
    │
    ▼
scene.GetComponent<TransformComponent>({Index=0, Generation=1})
    │
    ▼
ComponentStorage<TransformComponent>
    m_sparse[0] = 7            (dense index)
    m_dense_ids[7] == {0,1}    (generation check passes)
    return &m_dense[7]         (direct pointer into dense array)
```

### What lives where at runtime (example: 200 Actors + 5000 Tier 2 entities)

```
EntityRegistry::m_slots
  [0..199]   Tier 1 — Actor-backed entities
  [200..5199] Tier 2 — pure ECS entities
  [5200..65535] free

ActorManager::m_handles
  [0..199]   live Actor objects
  [200..1023] free

ComponentStorage<TransformComponent>::m_dense
  [0..5199]  all 5200 entities that have a TransformComponent (packed, no gaps)
  Tier 1 and Tier 2 are interleaved — ForEach iterates them uniformly
```

---

## 9. File Layout

```
ZEngine/
  ECS/
    EntityID.h
    ComponentTypeID.h
    ArchetypeMask.h
    ComponentStorage.h          (template, header-only)
    IComponentStorage.h
    EntityRegistry.h
    EntityRegistry.cpp
    Scene.h
    Scene.cpp
    Query.h                     (template, header-only)
    WorldTick.h
    WorldTick.cpp
    Actor.h
    Actor.cpp
    ActorManager.h
    ActorManager.cpp

  ECS/Components/
    TransformComponent.h        (plain data — Vec3f position/rotation/scale)
    MeshComponent.h
    RigidBodyComponent.h
    CameraComponent.h
    LightComponent.h

tests/
  ECS/
    ECSTest.cpp
    ActorTest.cpp
```

---

## 10. What Is Not in This Document

| Topic | Document |
|---|---|
| DAG-based parallel system scheduler | `system-scheduler.md` |
| Animation system (SkeletonComponent, AnimatorComponent, skinning) | `animation-system.md` |
| VFS stack | `vfs-design.md` + tickets 1–6 |
| Import pipeline, asset loading | `import-pipeline.md` |
| Scene serialization | `scene-serialization.md` |
| Fixed-timestep loop, `SnapshotTransforms`, interpolation alpha | `game-loop.md` |

> **Note for `Scene` implementors**: `game-loop.md` requires two methods on `ECS::Scene`
> that are not detailed in this document:
> - `Scene::SnapshotTransforms()` — copies each entity's `CurrentPosition` into
>   `PreviousPosition` at the end of each fixed step. Required for interpolation.
> - `Scene::FillRenderableTransforms(float alpha, Array<RenderableTransform>& out)` —
>   linearly interpolates between `PreviousPosition` and `CurrentPosition` using `alpha`
>   and fills the frame packet for the renderer.
>
> Both methods operate only on entities that have a `TransformComponent`. Add them to
> the `Scene` deliverable — do not wait for `game-loop.md` to be assigned.

---

## 11. Deliverables Checklist

- [x] `ZEngine/ECS/EntityID.h`
- [x] `ZEngine/ECS/ComponentTypeID.h` — atomic counter
- [x] `ZEngine/ECS/ArchetypeMask.h` — runtime bounds check in `MaskBit`
- [x] `ZEngine/ECS/ComponentStorage.h` — generation check in `Get`, `Has`, `Remove`
- [x] `ZEngine/ECS/IComponentStorage.h`
- [x] `ZEngine/ECS/EntityRegistry.h` + `.cpp` — `MAX_ENTITIES = 65536`; arena-allocated slot array
- [x] `ZEngine/ECS/Scene.h` + `.cpp` — includes `GetMask(EntityID)`, `SnapshotTransforms()`, `FillRenderableTransforms(alpha, out)`
- [x] `ZEngine/ECS/WorldCommands.h` + `.cpp` — deferred mutations, `Flush(Scene&)`, `Clear()`
- [x] `ZEngine/ECS/Query.h`
- [x] `ZEngine/ECS/WorldTick.h` + `.cpp` — DAG scheduler, wave dispatch, conflict detection
- [x] `ZEngine/ECS/Actor.h` + `.cpp` — no `Ref<Actor>`, no `RefCounted`; lifetime owned by `ActorManager`
- [x] `ZEngine/ECS/ActorManager.h` + `.cpp` — `HandleManager<Actor>` with `MAX_ACTORS = 1024`; `Create<T>()`, `Destroy(handle)`, `Access(handle)`, `Tick(dt)`, `Shutdown()`
- [x] `ZEngine/ECS/Components/TransformComponent.h` — plain data, separate from old type
- [x] `tests/ECS/ECSTest.cpp` — entity/component/query/generational handle tests
- [x] `tests/ECS/ActorTest.cpp` — covered in ECSTest.cpp — Actor create/destroy, component access via Actor, ECS system sees Actor entity
- [x] `tests/ECS/WorldCommandsTest.cpp` — covered in ECSTest.cpp — deferred spawn, deferred destroy, duplicate destroy guard, flush ordering
- [ ] `ZEngine/ECS/Components/MeshComponent.h` — `{ uuids::uuid MeshUUID; }` linking Actor to cooked mesh
- [ ] `ZEngine/ECS/Components/NameComponent.h` — `{ char Value[128]; }` display name for Outliner
- [ ] ECS → RenderScene bridge system — syncs `MeshComponent` add/remove to `RenderScene::MeshInstance` lifecycle; propagates `TransformComponent` changes to GPU buffer (tracked in issue #604)
- [ ] `Tetragrama/Components/HierarchyViewUIComponent` — rebuild around `ActorManager`; rows from `NameComponent`, selection via `ActorHandle` (tracked in issue #604)
