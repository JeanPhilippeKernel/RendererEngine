# ZEngine — Full Migration Plan

**Priority:** P0 — Master sequencing document; read before touching any code  
**Status:** Planning  
**Based on:** `actor-ecs-architecture.md`, `system-scheduler.md`, `animation-system.md`

---

## Guiding Principle

The Vulkan rendering pipeline, RenderGraph, materials, shaders, and device layer are
**live and must not regress**. Every phase leaves the engine in a buildable, runnable
state. Nothing is deleted until its replacement is proven to compile and the old code
is confirmed dead.

---

## Current State Inventory

| Area | File(s) | State | Fate |
|---|---|---|---|
| ECS core | — | Does not exist | Build from scratch (Phases 1–2) |
| Actor layer | — | Does not exist | Build from scratch (Phase 2) |
| `GraphicSceneEntity` | `Rendering/Entities/GraphicSceneEntity.h/.cpp` | Live, uses `entt` + `static weak_ptr<entt::registry>` | Replace with `Actor` (Phase 2), delete (Phase 5) |
| `GraphicScene` / `SceneRawData` / `SceneEntity` | `Rendering/Scenes/GraphicScene.h/.cpp` | Entirely inside `#if 0` | Delete `#if 0` block (Phase 5); keep `RenderScene` + `SceneData` structs which are live |
| `Rendering::Components::TransformComponent` | `Rendering/Components/TransformComponent.h` | Live — has methods + computed `Mat4f` | Keep during transition; replace call sites with `ECS::Components::TransformComponent` (Phase 3), delete (Phase 5) |
| `Rendering::Components::LightComponent` | `Rendering/Components/LightComponent.h` | Live — wraps `Ref<LightVNext>` | Migrate to plain-data `ECS::Components::LightComponent` (Phase 3) |
| `Rendering::Components::NameComponent` | `Rendering/Components/NameComponent.h` | Live — wraps `std::string` | Migrate to plain-data `ECS::Components::NameComponent` (Phase 3) |
| `Rendering::Components::UUIComponent` | `Rendering/Components/UUIComponent.h` | Live — generates UUID on construction | Migrate to plain-data `ECS::Components::UUIDComponent` (Phase 3) |
| `Rendering::Components::MaterialComponent` | `Rendering/Components/MaterialComponent.h` | Live — wraps `vector<Ref<ShaderMaterial>>` | Migrate to handle-based `ECS::Components::MaterialComponent` (Phase 3) |
| `Rendering::Components::GeometryComponent` | `Rendering/Components/GeometryComponent.h` | Dead/unused | Delete (Phase 5) |
| `Rendering::Components::ValidComponent` | `Rendering/Components/ValidComponent.h` | Dead/unused | Delete (Phase 5) |
| `Rendering::Components::CameraComponent` | `Rendering/Components/CameraComponent.h` | Dead/unused (only in `#if 0` or commented code) | Delete (Phase 5) |
| `GraphicScene3DSerializer` | `Serializers/GraphicScene3DSerializer.h/.cpp` | Live header; `.cpp` body mostly commented out — calls `GraphicSceneEntity::GetComponent` | Rewrite to use `Actor::GetComponent` (Phase 4) |
| `AssimpImporter` | `Importers/AssimpImporter.h/.cpp` | Live — mesh/material/texture working; animation is empty stubs | Add skeleton + clip extraction (Phase 4) |
| `AssetManager` | `Managers/AssetManager.h/.cpp` | Live | Add `AnimationManager*` integration (Phase 4) |
| `ThreadPoolHelper` | `Helpers/ThreadPool.h` | Live | Used as-is by `WorldTick` |
| `entt` | `__externals/entt` | Used only by `GraphicSceneEntity` + `GraphicScene.h` `#if 0` | Remove from CMakeLists after Phase 5 |
| Math — `Vec3<T> lerp` | `Core/Maths/MathUtils.h` | `lerp<T,T,F>` exists but is scalar-only — no `Vec3` overload | Add `Vec3<T> lerp` overload (Phase 0) |
| Math — `TRS()` | `Core/Maths/Matrix.h` | Does not exist | Add (Phase 0) |
| Matrix storage | `Core/Maths/Matrix.h` | Column-major: `m_data[col][row]` — access is `m(row,col)` | `TRS` must use `operator()` not `[]` indexing |

---

## Phase 0 — Math Prerequisites

**Goal:** Add the two math helpers that animation depends on. Zero risk — additive only.

### Files Modified

```
ZEngine/Core/Maths/MathUtils.h   — add Vec3<T> lerp overload
ZEngine/Core/Maths/Matrix.h      — add TRS() helper
```

### Tasks

- [ ] Add to `MathUtils.h`:

```cpp
template <typename T>
inline Vec3<T> lerp(const Vec3<T>& a, const Vec3<T>& b, T t) {
    return Vec3<T>(
        lerp(a.x, b.x, t),
        lerp(a.y, b.y, t),
        lerp(a.z, b.z, t));
}
```

- [ ] Add to `Matrix.h` (after `quaternionToMat4`):

```cpp
// Build a column-major Mat4f from position, rotation (quaternion), scale.
// Uses existing quaternionToMat4. Matrix storage is column-major: m_data[col][row].
inline Mat4f TRS(
    const Vec3f&                          position,
    const Core::Maths::Quaternion<float>& rotation,
    const Vec3f&                          scale)
{
    Mat4f m = quaternionToMat4(rotation);
    // Scale: multiply each rotation column by the corresponding scale component
    m(0,0) *= scale.x; m(1,0) *= scale.x; m(2,0) *= scale.x;
    m(0,1) *= scale.y; m(1,1) *= scale.y; m(2,1) *= scale.y;
    m(0,2) *= scale.z; m(1,2) *= scale.z; m(2,2) *= scale.z;
    // Translation: write into the 4th column
    m(0,3) = position.x;
    m(1,3) = position.y;
    m(2,3) = position.z;
    return m;
}
```

### Acceptance Criteria

Compiles cleanly. Existing math tests unaffected.

---

## Phase 1 — ECS Core

**Goal:** `ECS::Scene`, `EntityRegistry`, `ComponentStorage<T>`, `Query<Ts...>`,
`WorldTick` (with DAG scheduler) all compile and pass unit tests. Zero existing code
is touched.

### New Files

```
ZEngine/ZEngine/ECS/
  EntityID.h
  ComponentTypeID.h
  ArchetypeMask.h
  IComponentStorage.h
  ComponentStorage.h          (template, header-only)
  EntityRegistry.h
  EntityRegistry.cpp
  Scene.h
  Scene.cpp
  Query.h                     (template, header-only)
  WorldTick.h
  WorldTick.cpp

ZEngine/tests/ECS/
  ECSTest.cpp
  SchedulerTest.cpp
```

### Tasks

**`EntityID.h`**
- [x] `struct EntityID { uint32_t Index; uint32_t Generation; bool IsValid(); operator==; }`
- [x] `constexpr EntityID INVALID_ENTITY = {0, 0}`

**`ComponentTypeID.h`**
- [x] `using ComponentTypeID = uint32_t`
- [x] `NextTypeID()` — `static std::atomic<uint32_t>` counter, `fetch_add` relaxed
- [x] `ComponentTypeOf<T>()` — static local, calls `NextTypeID()` once per type

**`ArchetypeMask.h`**
- [x] `using ArchetypeMask = uint64_t`
- [x] `MaskBit(id)` — `ZENGINE_VALIDATE_ASSERT(id < 64, ...)` then `uint64_t(1) << id`
- [x] `MaskHas`, `MaskMatches`

**`IComponentStorage.h`**
- [x] `struct IComponentStorage { virtual void RemoveRaw(EntityID) = 0; }`

**`ComponentStorage<T>` (header-only)**
- [x] `m_dense`, `m_dense_ids`, `m_sparse` (UINT32_MAX = absent)
- [x] `Add` — grow sparse, assert no double-add, append to dense
- [x] `Remove` — swap-and-pop, update moved entity's sparse entry
- [x] `Get` — bounds check, UINT32_MAX check, **generation check** (`m_dense_ids[dense_idx] != id → nullptr`)
- [x] `Has` — same generation check
- [x] `ForEach` — iterates dense arrays in tandem

**`EntityRegistry`**
- [x] `struct EntitySlot { uint32_t Generation; ArchetypeMask Mask; }`
- [x] `Create()` — pop free-list or append; increment generation, skip 0
- [x] `Destroy(id)` — assert alive, increment generation (skip 0), push to free-list
- [x] `IsAlive(id)` — index bounds + generation match
- [x] `SetMask`, `GetMask`, `ForEachAlive`

**`ECS::Scene`**
- [x] `CreateEntity`, `DestroyEntity` (calls `RemoveRaw` on all storages first)
- [x] `AddComponent<T>` — `GetOrCreateStorage`, `storage.Add`, update mask
- [x] `GetComponent<T>`, `RemoveComponent<T>`, `HasComponent<T>`
- [x] `GetMask(EntityID)`
- [x] `ForEach<Ts...>` — compute required mask, call `ForEachAlive`, skip non-matching
- [x] `m_storages` — `UnorderedHashMap<ComponentTypeID, std::unique_ptr<IComponentStorage>>`

**`Query<Ts...>` (header-only)**
- [x] Constructor pre-computes `ArchetypeMask`
- [x] `ForEach` delegates to `Scene::ForEach` with cached mask

**`WorldTick`**
- [x] `using SystemID = uint32_t`
- [x] `struct SystemDeps { ArchetypeMask ReadMask; ArchetypeMask WriteMask; }`
- [x] `RegisterSystem(SystemFn, SystemDeps)` — returns `SystemID` (index into `m_nodes`)
- [x] `OrderBefore(SystemID a, SystemID b)` — records edge a→b
- [x] `Commit()`:
  - Build adjacency list from `OrderBefore` calls
  - For every pair (A, B): check conflict rules; if conflict and no ordering edge → `ZENGINE_VALIDATE_ASSERT`
  - DFS cycle detection → `ZENGINE_VALIDATE_ASSERT` on back edge
  - Kahn's algorithm → `m_waves` (Array of Array of SystemID)
- [x] `Tick(scene, dt)`:
  - Assert `m_committed`
  - For each wave: submit all systems to `ThreadPoolHelper`, wait with `condition_variable` + atomic counter
  - Comment explaining why predicate-wait is race-free

**`ECSTest.cpp`** (8 tests)
- [x] `CreateEntity` returns valid ID
- [x] `DestroyEntity` makes ID invalid
- [x] `AddComponent` / `GetComponent` non-null
- [x] `RemoveComponent` → `GetComponent` null
- [x] `ForEach` only matches entities with all components
- [x] Generational handle rejected after destroy + recycle
- [x] `Query<A,B>` matches only entities with both
- [x] `DestroyEntity` cleans up all components

**`SchedulerTest.cpp`** (5 tests)
- [x] Two independent systems assigned to same wave
- [x] Conflicting systems with `OrderBefore` assigned to separate waves
- [x] Conflicting systems without `OrderBefore` asserts in debug
- [x] Cycle asserts
- [x] `Tick` before `Commit` asserts
- [x] `RegisterSystem` returns distinct IDs

### Acceptance Criteria

All ECS + scheduler tests pass under AddressSanitizer. Engine still builds and runs.
`entt` still linked — not touched.

---

## Phase 2 — ECS Components + Actor Layer

**Goal:** Plain-data `ECS::Components` namespace live. `Actor` base class and
`ActorManager` live. `GraphicSceneEntity` is not yet deleted — both exist simultaneously.

### New Files

```
ZEngine/ZEngine/ECS/
  Actor.h
  Actor.cpp
  ActorManager.h
  ActorManager.cpp

ZEngine/ZEngine/ECS/Components/
  TransformComponent.h    (plain data: Vec3f Position, Rotation, Scale)
  MeshComponent.h         (uint32_t MeshHandle)
  MaterialComponent.h     (uint32_t MaterialHandle)
  LightComponent.h        (plain data: type enum + color + intensity)
  CameraComponent.h       (plain data: fov, near, far)
  NameComponent.h         (Core::Containers::String Name)
  UUIDComponent.h         (uuids::uuid Identifier)
  RigidBodyComponent.h    (Vec3f Velocity, float Mass)

ZEngine/tests/ECS/
  ActorTest.cpp
```

### Modified Files

```
ZEngine/ZEngine/Engine.h/.cpp   — add ECS::Scene + WorldTick + ActorManager to EngineContext
```

### Tasks

**`ECS::Components`** — all plain data, no virtual methods, no `Ref<>` inside

- [ ] `TransformComponent` — `Vec3f Position = {}; Vec3f Rotation = {}; Vec3f Scale = {1,1,1};`
- [ ] `MeshComponent` — `uint32_t MeshHandle = UINT32_MAX;`
- [ ] `MaterialComponent` — `uint32_t MaterialHandle = UINT32_MAX;`
- [ ] `LightComponent` — `enum class LightType`; `Vec3f Color`; `float Intensity`; `LightType Type`
- [ ] `CameraComponent` — `float Fov`; `float Near`; `float Far`; `bool Primary`
- [ ] `NameComponent` — `Core::Containers::String Name;`
- [ ] `UUIDComponent` — `uuids::uuid Identifier;` (UUID assigned at entity creation, not in constructor)
- [ ] `RigidBodyComponent` — `Vec3f Velocity; float Mass;`

**`Actor`**

- [x] `class Actor : public Helpers::RefCounted`
- [x] `static Ref<Actor> Create(Scene& scene)` — `CreateEntity`, store ID, call `OnCreate`
- [x] `static Ref<Actor> Wrap(Scene& scene, EntityID id)` — does NOT call `OnCreate`
- [x] `~Actor()` — call `OnDestroy`, then `m_scene->DestroyEntity(m_entity_id)` (guard against null scene)
- [x] `GetEntityID()`, `IsAlive()`
- [x] `AddComponent<T>`, `GetComponent<T>`, `HasComponent<T>`, `RemoveComponent<T>` — all delegate to `m_scene`
- [x] `virtual void OnCreate() {}`, `virtual void OnDestroy() {}`, `virtual void OnTick(float dt) {}`
- [x] `EntityID m_entity_id`; `Scene* m_scene` (non-owning)

**`ActorManager`**

- [x] Owns `Array<Ref<Actor>>` of live Actors
- [x] `Register(Ref<Actor>)`, `Unregister(EntityID)`
- [x] `Tick(float dt)` — call `OnTick` on each live Actor

**`Engine` integration**

- [x] Add `ECS::Scene Scene` to `EngineContext`
- [x] Add `ECS::WorldTick WorldTick` to `EngineContext`
- [x] Add `ECS::ActorManager ActorManager` to `EngineContext`
- [x] In `Engine::MainThreadRun`: call `WorldTick.Tick(Scene, dt)` then `ActorManager.Tick(dt)` each frame

**`ActorTest.cpp`**

- [ ] `Actor::Create` produces a valid `EntityID`
- [ ] `AddComponent` via Actor is visible to `scene.ForEach`
- [ ] `Actor::~Actor` destroys the entity — `scene.IsAlive(id)` returns false
- [ ] ECS system hits Actor entity in `ForEach<TransformComponent>`
- [ ] Two Actors wrapping the same `EntityID` asserts in debug

### Acceptance Criteria

Actor tests pass. `PlayerActor` subclass can be instantiated, have components added,
and be found by a `ForEach<TransformComponent>` query. Engine still builds and runs.
`GraphicSceneEntity` still exists — not touched.

---

## Phase 3 — Migrate Existing Components to ECS

**Goal:** All live `Rendering::Components` usages migrated to `ECS::Components`.
`GraphicSceneEntity::GetComponent<T>` call sites in `GraphicScene3DSerializer` migrated
to `Actor::GetComponent<T>`. Old component headers still exist but have no call sites.

### Modified Files

```
ZEngine/ZEngine/Serializers/GraphicScene3DSerializer.h/.cpp
ZEngine/ZEngine/Rendering/Scenes/GraphicScene.cpp           (includes only)
```

### Tasks

**Audit existing call sites first**

- [ ] `grep -rn "Rendering::Components::"` — full list of live usages
- [ ] `grep -rn "#include.*Rendering/Components"` — full list of includers

**`GraphicScene3DSerializer` migration**

The serializer's `SerializeSceneEntity(emitter, GraphicSceneEntity)` reads:
`UUIComponent`, `NameComponent`, `TransformComponent`, `MaterialComponent`,
`LightComponent`. The serialize/deserialize body is mostly commented out.

- [ ] Replace `GraphicSceneEntity` parameter with `Actor&` or `EntityID` + `ECS::Scene&`
- [ ] Replace `entity.HasComponent<UUIComponent>()` → `scene.HasComponent<UUIDComponent>(id)`
- [ ] Replace `entity.GetComponent<NameComponent>()` → `scene.GetComponent<NameComponent>(id)`
- [ ] Replace `entity.GetComponent<TransformComponent>()` → `scene.GetComponent<ECS::Components::TransformComponent>(id)`
- [ ] Replace `entity.GetComponent<LightComponent>()` → `scene.GetComponent<ECS::Components::LightComponent>(id)`
- [ ] Replace `entity.GetComponent<MaterialComponent>()` → `scene.GetComponent<ECS::Components::MaterialComponent>(id)`
- [ ] Update `#include` list in serializer to remove `Rendering/Entities/GraphicSceneEntity.h`
      and `Rendering/Components/*.h`, add `ECS/Components/*.h`

**`GraphicScene.cpp` includes**

- [ ] Remove `#include <Rendering/Components/CameraComponent.h>`
- [ ] Remove `#include <Rendering/Components/LightComponent.h>`
- [ ] Remove `#include <Rendering/Components/UUIComponent.h>`
  (these includes exist in a file that is already mostly `#if 0` — confirm nothing live uses them)

### Acceptance Criteria

`grep -rn "Rendering::Components::"` returns zero live results (excluding the component
header files themselves and `#if 0` blocks). Serializer compiles against `ECS::Components`.

---

## Phase 4 — Animation System

**Goal:** Full animation stack live: `AnimationManager`, ECS components, two systems,
`AssimpImporter` extraction. Phase 0 must be complete before this phase starts.

### New Files

```
ZEngine/ZEngine/Animation/
  AnimationHandles.h
  SkeletonData.h
  AnimationClip.h
  AnimationManager.h
  AnimationManager.cpp
  AnimationSampleSystem.h
  AnimationSampleSystem.cpp
  SkinningUploadSystem.h
  SkinningUploadSystem.cpp

ZEngine/ZEngine/ECS/Components/
  SkeletonComponent.h      (includes AnimationHandles.h only)
  AnimatorComponent.h      (includes AnimationHandles.h only)
  SkinningComponent.h

ZEngine/tests/Animation/
  AnimationTest.cpp
```

### Modified Files

```
ZEngine/ZEngine/Importers/IAssetImporter.h    — add AnimationManager* to ImportConfiguration
ZEngine/ZEngine/Importers/AssimpImporter.h    — add ExtractSkeleton, ExtractAnimationClips
ZEngine/ZEngine/Importers/AssimpImporter.cpp  — implement both, wire into ImportAsync
ZEngine/ZEngine/Managers/AssetManager.h/.cpp  — add AnimationManager instance + initialization
```

### Tasks

- [ ] Phase 0 complete (`Vec3<T> lerp`, `TRS`)
- [ ] `AnimationHandles.h` — `SkeletonHandle`, `AnimationClipHandle`, `PoseHandle`, sentinels
- [ ] `SkeletonData` — `BoneCount`, `ParentIndices` (`int32_t`, -1 = root), `InverseBindMatrices`, `BoneNames`
- [ ] `AnimationClip` — `DurationSeconds`, `SampleRate = 30.f`, `BoneCount`, `Array<BoneChannel>`
- [ ] `BoneChannel` — `Array<Vec3f> PositionKeys`, `Array<Quaternion<float>> RotationKeys`, `Array<Vec3f> ScaleKeys`
- [ ] `AnimationManager` — skeleton pool, clip pool, pose buffer pool (`AllocatePose`, `GetPose`)
- [ ] `BoneTransform` struct in `AnimationManager.h` — `Vec3f Position/Scale`, `Quaternion<float> Rotation`
- [ ] `SkeletonComponent` — `Animation::SkeletonHandle Handle`
- [ ] `AnimatorComponent` — `AnimationClipHandle`, `PoseHandle`, `PlaybackTime`, `PlaybackRate`, `Loop`, `Playing`
- [ ] `SkinningComponent` — `GpuBufferHandle SkinDataBufferHandle`, `BoneMatrixBufferHandle`, `BoneCount`
- [ ] `AnimationSampleSystem(scene, dt, anim_mgr)`:
  - `ForEach<SkeletonComponent, AnimatorComponent>`
  - Advance time, wrap/clamp
  - `AllocatePose` on first use (when `PoseHandle == INVALID_POSE`)
  - Sample each bone: index + lerp/slerp using `Vec3<T> lerp` and `ZEngine::Core::Maths::slerp`
- [ ] `SkinningUploadSystem(scene, dt, anim_mgr, render_resource_mgr)`:
  - `ForEach<SkeletonComponent, AnimatorComponent, SkinningComponent>`
  - `thread_local` scratch buffers for global_pose and bone_matrices
  - Forward pass: `TRS` per bone, accumulate global pose (parent before child — guaranteed by `ExtractSkeleton`)
  - Final matrix: `global_pose[i] * InverseBindMatrix[i]`
  - `render_resource_mgr.UploadBuffer(BoneMatrixBufferHandle, data, size)`
- [ ] Register both systems with `WorldTick`, `OrderBefore(anim_sample_id, skinning_id)`
- [ ] `IAssetImporter.h` — add `Animation::AnimationManager* AnimationManager = nullptr` to `ImportConfiguration`
- [ ] `AssimpImporter::ExtractSkeleton`:
  - Collect bone name set from `aiMesh::mBones` across all meshes
  - Walk `aiScene::mRootNode` BFS; record nodes in bone name set in traversal order (parent before child)
  - Fill `ParentIndices` by looking up node's parent in bone list; -1 if parent is not a bone
  - Fill `InverseBindMatrices` from `aiBone::mOffsetMatrix` via `ConvertToMat4`; identity for bones absent from `mBones`
- [ ] `AssimpImporter::ExtractAnimationClips`:
  - For each `aiAnimation`: `DurationSeconds = mDuration / mTicksPerSecond`
  - Resample each channel to uniform 30fps: evaluate Assimp interpolation at `t = frame / 30.f`
  - Match channels to skeleton bone indices by name
- [ ] Wire into `AssimpImporter::ImportAsync` after existing mesh/material/texture extraction
- [ ] `AssetManager::Initialize` — create and store `AnimationManager` instance; pass pointer in `ImportConfiguration`
- [ ] `AnimationTest.cpp` — 6 tests (sample t=0, sample t=duration, loop wrap, system writes pose, upload called with correct bone count, INVALID_POSE early return)

### Acceptance Criteria

Animation tests pass under AddressSanitizer. A `.fbx` with skeletal animation produces
a valid `SkeletonData` + `AnimationClip` when imported. `AnimationSampleSystem` writes
a non-identity pose after one tick.

---

## Phase 5 — Dead Code Removal

**Goal:** Delete everything the new ECS + Actor layer replaces. Each step is a separate
commit. Engine builds and runs after every step.

### Step 5.1 — Delete obviously dead components

```
ZEngine/ZEngine/Rendering/Components/GeometryComponent.h    — dead, no call sites
ZEngine/ZEngine/Rendering/Components/ValidComponent.h       — dead, no call sites
ZEngine/ZEngine/Rendering/Components/CameraComponent.h      — dead (only in #if 0 / comments)
```

- [x] `grep -rn "GeometryComponent\|ValidComponent\|CameraComponent"` outside `#if 0` → must be zero
- [x] Delete the three files
- [x] Remove from CMakeLists if listed

### Step 5.2 — Delete old `Rendering::Components` headers (after Phase 3)

```
ZEngine/ZEngine/Rendering/Components/TransformComponent.h
ZEngine/ZEngine/Rendering/Components/LightComponent.h
ZEngine/ZEngine/Rendering/Components/NameComponent.h
ZEngine/ZEngine/Rendering/Components/UUIComponent.h
ZEngine/ZEngine/Rendering/Components/MaterialComponent.h
```

- [ ] `grep -rn "#include.*Rendering/Components"` → must be zero (excluding deleted files)
- [ ] `grep -rn "Rendering::Components::"` → must be zero
- [ ] Delete all five files

### Step 5.3 — Delete `GraphicSceneEntity`

`GraphicSceneEntity` is used only by `GraphicScene3DSerializer` (already migrated in Phase 3).

- [x] `grep -rn "GraphicSceneEntity"` outside `#if 0` → must be zero after Phase 3
- [x] Delete `Rendering/Entities/GraphicSceneEntity.h`
- [x] Delete `Rendering/Entities/GraphicSceneEntity.cpp`

### Step 5.4 — Delete `#if 0` block in `GraphicScene.h/.cpp`

The `#if 0` block contains: `SceneNodeHierarchy`, `DrawData`, `SceneRawData`, `SceneEntity`,
`GetEntityRegistry()`, old `GraphicScene` class. These are entirely dead.

- [ ] Delete the entire `#if 0 ... #endif` block from `GraphicScene.h`
- [ ] Delete the corresponding `#if 0 ... #endif` block from `GraphicScene.cpp`
- [ ] Verify `RenderScene` and `SceneData` structs (which ARE live) are untouched

### Step 5.5 — Remove `entt` dependency

- [ ] `grep -rn "entt" ZEngine/ZEngine` (excluding `__externals`) → must be zero after steps 5.3–5.4
- [ ] Remove `entt` from `ZEngine/CMakeLists.txt` target_link_libraries
- [ ] Remove `entt` from include paths if listed separately

### Step 5.6 — Remove `GraphicScene.cpp` entt includes

- [ ] `GraphicScene.cpp` includes `CameraComponent.h`, `LightComponent.h`, `UUIComponent.h` — confirm these are only used inside the now-deleted `#if 0` block, then remove the includes

### Acceptance Criteria

`grep -rn "entt" ZEngine/ZEngine` returns zero results.
`grep -rn "Rendering::Components::"` returns zero results.
`grep -rn "GraphicSceneEntity"` returns zero results.
Full build succeeds. All tests pass.

---

## Phase 6 — VFS Stack (Parallel Track)

**Goal:** VFS Ticket 1 live — `VFSPath`, `IVFSFile`, `IVFSBackend`, `IVFSContext`,
`VFSDiskContext`. Unblocks the import pipeline and full `AssimpImporter` end-to-end wiring.

This phase runs in parallel with Phases 1–5. It does not block any of them, but it is
a prerequisite for the import pipeline, shader asset pipeline, and scene serialization.

See `vfs-design.md` and `vfs-ticket2` through `vfs-ticket6` for the full spec.

**VFS Ticket 1 new files:**
```
ZEngine/ZEngine/Core/VFS/
  VFSError.h
  VFSPath.h
  VFSPath.cpp
  IVFSFile.h
  IVFSBackend.h
  IVFSContext.h
  VFSDiskContext.cpp

ZEngine/tests/
  test_vfspath.cpp
```

---

## Build Order

```
Phase 0   Math prerequisites (Vec3 lerp, TRS)                     ← start here
   │
   ├── Phase 6  VFS Ticket 1 (parallel)
   │
Phase 1   ECS core (Scene, Registry, ComponentStorage, WorldTick + scheduler)
   │
Phase 2   ECS Components + Actor layer
   │
Phase 3   Migrate existing Rendering::Components → ECS::Components
   │
Phase 4   Animation system + AssimpImporter extraction
   │
Phase 5   Dead code removal (step by step)
              5.1  Dead component headers
              5.2  Old Rendering::Components headers
              5.3  GraphicSceneEntity
              5.4  #if 0 block in GraphicScene
              5.5  entt removed from CMakeLists
              5.6  Dangling includes cleaned
```

---

## What Is Not in This Plan

| Topic | Reason |
|---|---|
| Import pipeline end-to-end | Depends on VFS Phase 6 + `render-resource-manager.md` |
| GPU skinning shader | Requires `SkinningComponent` buffers (Phase 4) + shader authoring |
| Scene serialization | Depends on `scene-serialization.md` + stable ECS entity IDs |
| `std::vector` / `std::string` → custom containers (~40 files) | Separate cleanup pass, not blocking |
| Render resource manager | Separate doc (`render-resource-manager.md`) |
| VFS Tickets 2–6 (mount table, scanner, file watcher, .meta, asset registry) | Sequenced after VFS Ticket 1 |
