# ZEngine — Hybrid Actor-ECS Architecture

**Priority:** P1 — Implement first; all other systems depend on this  
**Status:** Design  
**Blocks:** `system-scheduler.md`, `animation-system.md`, `scene-serialization.md`

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
- Is reference-counted via `Ref<Actor>` (intrusive ptr, consistent with ZEngine convention)

### 3.1 `Actor` base class

```cpp
// ZEngine/ECS/Actor.h
#pragma once
#include <ECS/Scene.h>
#include <Helpers/IntrusivePtr.h>

namespace ZEngine::ECS {

    class Actor : public Helpers::RefCounted {
    public:
        // Creates a new entity in `scene`, stores its ID, calls OnCreate.
        static Ref<Actor> Create(Scene& scene);

        // Wraps an existing entity. Does NOT call OnCreate.
        // Use when deserializing or promoting a Tier 2 entity.
        static Ref<Actor> Wrap(Scene& scene, EntityID id);

        virtual ~Actor();

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

        // Call before Scene shutdown if Actor may outlive its Scene.
        // Nulls m_scene so the destructor skips DestroyEntity.
        void Detach();

    protected:
        Actor() = default;

    private:
        EntityID  m_entity_id = INVALID_ENTITY;
        Scene*    m_scene     = nullptr;  // non-owning. Scene must outlive Actor.
                                          // Call Detach() before Scene shutdown if order cannot be guaranteed.
                                          // Destructor guards: if (!m_scene) skips DestroyEntity.
    };

}  // namespace ZEngine::ECS
```

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
```

### 3.3 Lifetime Rules

- `Actor::Create(scene)` allocates the Actor object, creates an `EntityID` in the scene,
  stores it, then calls `OnCreate()`.
- `Actor` destructor implementation:
  ```cpp
  ~Actor() {
      if (m_scene && IsAlive()) {  // IsAlive checks generation — safe in release builds
          OnDestroy();
          m_scene->DestroyEntity(m_entity_id);
      }
  }
  ```
  > **Note:** The `IsAlive()` guard prevents double-destroy if two Actors accidentally wrap the same EntityID. This fires in both debug and release builds, not just debug.
- If the Scene is destroyed before the Actor, the Actor's destructor must not call
  `DestroyEntity`. Use a weak reference or an explicit `Detach()` call before Scene
  shutdown. The engine startup/shutdown order must ensure Scene outlives all Actors.
- Two Actors must never wrap the same `EntityID`. This is asserted in debug builds.

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

```
m_slots      — Array<EntitySlot{Generation, ArchetypeMask}>
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

## 8. File Layout

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

## 9. What Is Not in This Document

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

## 10. Deliverables Checklist

- [ ] `ZEngine/ECS/EntityID.h`
- [ ] `ZEngine/ECS/ComponentTypeID.h` — atomic counter
- [ ] `ZEngine/ECS/ArchetypeMask.h` — runtime bounds check in `MaskBit`
- [ ] `ZEngine/ECS/ComponentStorage.h` — generation check in `Get`, `Has`, `Remove`
- [ ] `ZEngine/ECS/IComponentStorage.h`
- [ ] `ZEngine/ECS/EntityRegistry.h` + `.cpp`
- [ ] `ZEngine/ECS/Scene.h` + `.cpp` — includes `GetMask(EntityID)`, `SnapshotTransforms()`, `FillRenderableTransforms(alpha, out)`
- [ ] `ZEngine/ECS/WorldCommands.h` + `.cpp` — deferred mutations, `Flush(Scene&)`, `Clear()`
- [ ] `ZEngine/ECS/Query.h`
- [ ] `ZEngine/ECS/WorldTick.h` + `.cpp`
- [ ] `ZEngine/ECS/Actor.h` + `.cpp`
- [ ] `ZEngine/ECS/ActorManager.h` + `.cpp`
- [ ] `ZEngine/ECS/Components/TransformComponent.h` — plain data, separate from old type
- [ ] `tests/ECS/ECSTest.cpp` — entity/component/query/generational handle tests
- [ ] `tests/ECS/ActorTest.cpp` — Actor create/destroy, component access via Actor, ECS system sees Actor entity
- [ ] `tests/ECS/WorldCommandsTest.cpp` — deferred spawn, deferred destroy, duplicate destroy guard, flush ordering
