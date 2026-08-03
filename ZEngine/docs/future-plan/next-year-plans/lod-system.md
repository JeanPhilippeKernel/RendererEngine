# ZEngine — LOD System (Level of Detail)

**Priority:** Next-year plan — not required for first ship, required for performance at scale
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `render-graph-integration.md`, `culling-system.md`

---

## 1. Why LOD

A naive renderer draws every mesh at full resolution regardless of how far the object is from the camera. At distance, a 10,000-triangle tree occupies 4 pixels on screen and contributes no perceptible detail — yet it consumes vertex-processing budget identical to a foreground hero asset.

At 1000 trees the vertex count per frame becomes unacceptable. A 60 fps target at 1080p with a typical scene budget of ~2M triangles per frame cannot sustain dense geometry at every distance range. LOD (Level of Detail) solves this by substituting progressively simplified meshes as distance increases.

Concrete goals for the ZEngine LOD system:

- Reduce draw-call vertex count by 70–90% for objects beyond 50m.
- Add zero conditional branches in the main render submission loop (selection is pre-computed by a dedicated ECS system).
- Require no renderer changes for v1 — the GeometryPass consumes `ActiveLODComponent` the same way it would consume any mesh handle.
- Support up to four LOD levels per mesh asset.

---

## 2. LOD Component

`LODComponent` is a plain-data ECS component. It holds up to four `MeshHandle` values (one per level) and the world-space distance thresholds at which each level activates.

```cpp
// ZEngine/Rendering/LOD/LODComponent.h

static constexpr uint8_t k_MaxLODLevels = 4;

struct LODComponent {
    MeshHandle Levels[k_MaxLODLevels] = {};       // LOD0 = full detail, LOD3 = lowest
    float      Distances[k_MaxLODLevels] = {};    // world-space distance thresholds
    uint8_t    LevelCount = 1;  // minimum 1; 0 is invalid (entity would be invisible)
};
```

Field semantics:

- `Levels[0]` is always the highest-detail mesh (LOD0). It is identical to the mesh the entity would render without LOD.
- `Distances[i]` is the maximum world-space camera distance at which `Levels[i]` is used. When `distance > Distances[LevelCount - 1]`, the entity is marked fully culled (no draw submitted). This integrates with the culling system defined in `culling-system.md`.
- `LevelCount` must be at least 1. If it is 1 the entity renders at full detail regardless of distance (LOD system is effectively bypassed for that entity).

A separate `ActiveLODComponent` is written by the LOD system each frame and consumed by the renderer:

```cpp
// ZEngine/Rendering/LOD/LODComponent.h

struct ActiveLODComponent {
    uint8_t ActiveLevel = 0;        // index into LODComponent::Levels
    bool    BeyondAllLevels = false; // true = do not render this entity at all
};
```

`ActiveLODComponent` is never set by game code. Only `LODSystem` writes it.

---

## 3. LOD Selection System

`LODSystem` is a standard ECS system. It runs once per frame, iterates all entities that have both `TransformComponent` and `LODComponent`, and writes `ActiveLODComponent`.

```cpp
// ZEngine/Rendering/LOD/LODSystem.h

class LODSystem {
public:
    void Initialize(Scene& scene);
    void Tick(Scene& scene, float dt);
    SystemDeps GetDeps() const;
};
```

Tick logic (pseudocode reflecting actual implementation intent):

```
for each entity in scene with (TransformComponent, LODComponent):
    world_pos   = TransformComponent.WorldPosition
    dist        = length(world_pos - CameraState.WorldPosition)

    // LOD selection: Distances[i] is the maximum distance at which LOD i is shown.
    // Distances must be ascending: Distances[0] < Distances[1] < ... < Distances[LevelCount-1].
    // Start at lowest detail (LevelCount-1) and improve as entity gets closer.
    level = LevelCount - 1   // default: lowest detail (or culled if beyond last threshold)
    beyond = true
    for i in 0 .. LevelCount - 1:
        if dist <= Distances[i]:
            level = i        // entity is within this LOD's max distance
            beyond = false
            break
    // If dist > all Distances[], level = LevelCount-1 (lowest detail); beyond = true (may cull)

    write ActiveLODComponent { ActiveLevel = level, BeyondAllLevels = beyond }
```

Note: "Distances[] must be strictly ascending. LODSystem::Tick asserts this in debug builds."

`CameraState` is a per-scene singleton component registered by the camera system. `LODSystem` reads it as a dependency via `SystemDeps`. No global state, no singletons outside ECS.

SystemDeps mask:

```cpp
SystemDeps LODSystem::GetDeps() const {
    SystemDeps d;
    d.ReadComponents  = ComponentMask::TransformComponent
                      | ComponentMask::LODComponent
                      | ComponentMask::CameraState;
    d.WriteComponents = ComponentMask::ActiveLODComponent;
    return d;
}
```

This ensures the scheduler does not run `LODSystem` concurrently with anything that writes `TransformComponent` or `CameraState`, and does not run the GeometryPass (which reads `ActiveLODComponent`) until `LODSystem` finishes.

At the start of entity processing in `LODSystem::Tick`, validate LevelCount:

```cpp
ZENGINE_VALIDATE_ASSERT(lod.LevelCount >= 1 && lod.LevelCount <= k_MaxLODLevels,
    "LODComponent has invalid LevelCount %u (must be 1..%u)",
    lod.LevelCount, k_MaxLODLevels);
```

---

## 4. Screen-Space Size Metric

World-space distance is a coarse proxy. A large building at 200m and a pebble at 5m may occupy the same pixel area. For high-fidelity selection the projected screen-space size is preferred.

The metric used:

```
projected_size = mesh_radius / distance * focal_length
```

Where:

- `mesh_radius` is the bounding sphere radius of the mesh (stored in `BoundingSphereComponent`, see `culling-system.md`).
- `distance` is the camera-to-entity distance.
- `focal_length` is derived from the vertical field-of-view: `focal_length = 1.0f / tan(fov_y_radians * 0.5f)`.

LOD threshold comparison:

```
if projected_size > Thresholds[i]:
    use LOD level i
```

`Thresholds` are stored in `LODComponent::Distances` but interpreted as screen-space projected sizes when `LODComponent::UseScreenSize = true` (a one-byte flag). The offline cook pipeline fills these values during LOD generation.

For v1, plain world-space distance is acceptable. The screen-space metric is the v2 upgrade path; the data representation is identical, only the comparison changes.

---

## 5. LOD Generation (Offline Cook Pipeline)

LOD meshes are generated offline during the cook step, not at runtime. The tool used is **meshoptimizer** (MIT license, header-only C library, no dependencies).

Cook pipeline step:

1. Importer emits the original mesh as `LOD0`.
2. `LODGenerator` applies `meshopt_simplify` at target ratios:
   - LOD1: 50% of original triangle count
   - LOD2: 25%
   - LOD3: 12.5%
3. Each LOD mesh is assigned a stable UUID derived from `source_uuid + lod_level_index`.
4. All LOD meshes are stored as separate `MeshHandle` entries in `RenderResourceManager`.
5. The asset cook writes a `LODDescriptor` alongside the primary mesh asset. The importer reconstructs `LODComponent` field values from this descriptor at asset load time.

The LOD descriptor in the asset pack:

```cpp
struct LODDescriptor {
    uint32_t LevelCount;
    AssetUUID LevelUUIDs[k_MaxLODLevels];
    float     Distances[k_MaxLODLevels];   // world-space or screen-space depending on UseScreenSize
    bool      UseScreenSize;
};
```

meshoptimizer is not linked into the runtime. It is a cook-time dependency only. The runtime sees only the output `MeshHandle` values and distance thresholds.

---

## 6. LOD Transitions

### v1: Hard Switch

On the frame `LODSystem` sets a new `ActiveLevel`, the GeometryPass immediately uses the new mesh. There is no blending. This is acceptable for opaque geometry at distance and adds zero rendering cost.

Artifacts: a visible pop when the mesh changes level. The pop is less noticeable at distance and is an accepted v1 trade-off.

### v2: Screen-Space Dithering Blend (Noted, Not Scheduled)

For v2, a cross-fade between two LOD levels can be implemented in the material shader using a dithering pattern:

- Both LOD levels are submitted in the same frame during the transition window.
- The outgoing mesh clips via a checkerboard dither pattern, the incoming mesh clips via its inverse.
- Together they produce a visually continuous transition.
- `ActiveLODComponent` would gain a `TransitionAlpha` float and a `TransitionTargetLevel` field.
- The GeometryPass would detect active transitions and submit two draw calls instead of one.

This is deferred to v2. No code is written for it now; the component layout is designed to accommodate it without breaking v1.

---

## 7. Integration with GeometryPass

The GeometryPass iterates visible entities and selects vertex/index buffer ranges for each draw. With LOD, this selection is trivial because `ActiveLODComponent` directly holds the mesh handle index.

GeometryPass inner loop change:

```cpp
// Before LOD (v1 baseline):
MeshHandle mesh = renderableComp.MeshHandle;

// After LOD:
MeshHandle mesh;
if (scene.HasComponent<ActiveLODComponent>(entity)) {
    const ActiveLODComponent& lod = scene.GetComponent<ActiveLODComponent>(entity);
    if (lod.BeyondAllLevels) continue; // skip draw
    const LODComponent& lodComp = scene.GetComponent<LODComponent>(entity);
    mesh = lodComp.Levels[lod.ActiveLevel];
} else {
    mesh = renderableComp.MeshHandle;
}
```

No branching overhead in the common path for non-LOD entities. For LOD entities the branch is predictable (almost always false for `BeyondAllLevels` for visible entities after frustum culling).

`BeyondAllLevels = true` acts as a secondary cull signal. The culling system (see `culling-system.md`) sets `CulledComponent` for out-of-frustum entities; LOD's `BeyondAllLevels` handles the case where the entity is within the frustum but beyond the render distance threshold.

---

## 8. File Layout

```
ZEngine/Rendering/LOD/
    LODComponent.h          -- LODComponent, ActiveLODComponent structs
    LODSystem.h             -- LODSystem class declaration
    LODSystem.cpp           -- Tick implementation, screen-space metric
    LODDescriptor.h         -- LODDescriptor struct used by cook + importer

ZEngine/Tools/Cook/
    LODGenerator.h          -- meshoptimizer wrapper for LOD simplification
    LODGenerator.cpp        -- simplification ratios, UUID derivation, serialization

ZEngine/Importers/
    MeshImporter.cpp        -- extended to read LODDescriptor and populate LODComponent
```

meshoptimizer source lives under `ZEngine/ThirdParty/meshoptimizer/` (cook-only, not compiled into the engine runtime).

---

## 9. Deliverables Checklist

- [ ] `LODComponent` and `ActiveLODComponent` structs defined and registered in ECS
- [ ] `LODSystem::Tick` implemented with world-space distance selection
- [ ] `CameraState` singleton component available to `LODSystem` via `SystemDeps`
- [ ] `LODGenerator` integrated into the cook pipeline using meshoptimizer
- [ ] Cook pipeline emits `LODDescriptor` alongside primary mesh asset
- [ ] `MeshImporter` reconstructs `LODComponent` from `LODDescriptor` at load time
- [ ] `GeometryPass` reads `ActiveLODComponent` and selects correct `MeshHandle`
- [ ] `BeyondAllLevels` flag skips draw submission (cooperates with culling system)
- [ ] Screen-space metric implemented behind `UseScreenSize` flag (v2)
- [ ] Dithering blend transition design documented (v2, not yet scheduled)
- [ ] Unit tests: distance threshold selection, boundary conditions, `LevelCount = 1` bypass
- [ ] Integration test: 1000-entity scene, assert draw call count drops by at least 70% at 200m camera distance
