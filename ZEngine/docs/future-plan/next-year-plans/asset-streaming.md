# ZEngine — Asset Streaming

**Priority:** Next-year plan — required for open-world games and large levels
**Status:** Design
**Depends on:** `vfs-ticket6-asset-registry.md`, `render-resource-manager.md`, `import-pipeline.md`

---

## 1. Problem

The v1 load model reads every asset referenced by the scene before the first frame renders. For a small level with 50 unique assets this is acceptable. For an open-world game with a 1km² map, 10,000 unique mesh and texture assets, and multiple LOD levels per asset, loading everything at startup is not feasible:

- VRAM budget: a typical mid-range GPU has 8GB. A 1km² scene with uncompressed textures and full-detail meshes at every point can consume 40GB of asset data. Compressed textures (see `texture-compression.md`) reduce this 4–6x but still exceed VRAM for large worlds.
- Load time: loading 10,000 assets synchronously at startup produces an unacceptable wait regardless of SSD speed.
- Memory fragmentation: loading and never evicting assets means memory consumption grows monotonically.

The solution is runtime asset streaming: load assets as the camera approaches them, evict assets when the camera moves far enough away, and maintain a real-time budget of active GPU memory.

---

## 2. Streaming Model

Each streamable asset defines a `StreamRadius` in world-space metres. The streaming system evaluates the camera-to-entity distance each frame:

- If `distance <= StreamRadius` and the asset is not loaded: schedule a load.
- If `distance > StreamRadius * UnloadFactor` and the asset is loaded and not recently visible: schedule an eviction.

`UnloadFactor` (default 1.5) creates hysteresis — the unload distance is 50% larger than the load distance. This prevents thrashing when the camera sits exactly at the load boundary.

The streaming model is intentionally simple. It does not account for camera velocity prediction or priority inheritance between assets that reference each other (e.g. a mesh that requires a texture). Those are v2 concerns. In v1, the import pipeline ensures that a mesh's dependent textures share its UUID tree and are loaded together as a single streaming unit.

---

## 3. StreamableComponent

`StreamableComponent` marks an entity as managed by the streaming system. Entities without this component are loaded at scene startup and never evicted (the v1 behavior, still valid for small scenes and essential assets like the player character).

```cpp
// ZEngine/Streaming/StreamableComponent.h

struct StreamableComponent {
    float    StreamRadius    = 100.f;   // metres; load when camera is within this distance
    float    UnloadFactor    = 1.5f;    // unload when camera exceeds StreamRadius * UnloadFactor
    float    LastVisibleTime = 0.f;     // scene time when entity was last rendered (set by GeometryPass)
    bool     IsLoaded        = false;   // true = GPU resources are resident
    bool     IsLoading       = false;   // true = load is in flight
    uint32_t StreamPriority  = 0;       // higher value = loads before lower-priority entities
    AssetUUID PrimaryAssetUUID = {};    // the asset to stream in (mesh, material, or prefab)
};
```

`LastVisibleTime` is written by the GeometryPass (the same pass that checks `CulledComponent` and `OccludedComponent`). An entity that has not been rendered for more than `EvictGracePeriodSeconds` (default 2.0) is eligible for eviction even if it is within `StreamRadius * UnloadFactor`. This covers the case where an entity is within range but always occluded.

`IsLoading = true` prevents duplicate load requests if `Tick` is called while a previous load is still in flight.

---

## 4. StreamingManager

`StreamingManager` is a scene-level service. It is the sole coordinator for streaming decisions. No other system initiates loads or evictions.

```cpp
// ZEngine/Streaming/StreamingManager.h

class StreamingManager {
public:
    struct Config {
        float    MaxStreamRadius         = 500.f;   // metres; entities beyond this are never loaded
        uint64_t ActiveBudgetBytes       = 256ull * 1024 * 1024;  // 256MB GPU budget
        float    EvictGracePeriodSeconds = 2.f;
        uint32_t MaxLoadsPerFrame        = 4;       // throttle: max new loads kicked per frame
        uint32_t MaxEvictsPerFrame       = 2;
    };

    void Initialize(Config config, ImportCoordinator& importer,
                    RenderResourceManager& rrm);

    // Called once per frame by WorldTick, before ECS systems run
    void Tick(const Vec3f& camera_position, float scene_time, Scene& scene);

    // Budget query
    uint64_t ActiveBytesInFlight() const;
    uint64_t ActiveBytesResident() const;

private:
    void EnqueueLoad (EntityID entity, StreamableComponent& comp);
    void EnqueueEvict(EntityID entity, StreamableComponent& comp);
    void ProcessCompletedLoads(Scene& scene);

    ImportCoordinator*      m_Importer = nullptr;
    RenderResourceManager*  m_RRM      = nullptr;
    Config                  m_Config   = {};

    // Per-entity load tickets (in-flight requests to ImportCoordinator)
    UnorderedHashMap<EntityID, ImportTicket> m_PendingLoads;

    // Per-entity byte counts (for budget accounting)
    UnorderedHashMap<EntityID, uint64_t>     m_ResidentBytes;

    uint64_t m_TotalResidentBytes  = 0;
    uint64_t m_TotalInFlightBytes  = 0;
};
```

### Tick Algorithm

```
ProcessCompletedLoads(scene)   // check completed ImportTickets, set IsLoaded = true

load_candidates  = []
evict_candidates = []

for each entity with StreamableComponent in active cells (see section 5):
    dist = length(entity_world_pos - camera_position)

    if dist <= comp.StreamRadius and not comp.IsLoaded and not comp.IsLoading:
        load_candidates.push_back({ entity, priority = comp.StreamPriority,
                                    dist = dist })

    if dist > comp.StreamRadius * comp.UnloadFactor
       and comp.IsLoaded
       and (scene_time - comp.LastVisibleTime) > EvictGracePeriodSeconds:
        evict_candidates.push_back({ entity, dist = dist })

sort load_candidates by priority DESC, then dist ASC
sort evict_candidates by dist DESC  // evict farthest first

for each candidate in load_candidates (up to MaxLoadsPerFrame):
    if TotalInFlightBytes + estimated_size > ActiveBudgetBytes: break
    EnqueueLoad(entity, comp)

for each candidate in evict_candidates (up to MaxEvictsPerFrame):
    EnqueueEvict(entity, comp)
```

The budget check before each `EnqueueLoad` prevents overcommitting VRAM. `estimated_size` is read from the asset registry (`AssetRegistry::GetAssetSizeBytes(uuid)`), which is available without loading the asset.

### EnqueueLoad

```cpp
void StreamingManager::EnqueueLoad(EntityID entity, StreamableComponent& comp) {
    ImportRequest req;
    req.UUID     = comp.PrimaryAssetUUID;
    req.Priority = ImportPriority::High;
    ImportTicket ticket = m_Importer->Submit(req);
    m_PendingLoads.Insert(entity, ticket);
    comp.IsLoading = true;
    m_TotalInFlightBytes += m_Importer->GetEstimatedBytes(req.UUID);
}
```

### EnqueueEvict

```cpp
void StreamingManager::EnqueueEvict(EntityID entity, StreamableComponent& comp) {
    // Release GPU resources immediately (RRM reference-counts; other entities
    // sharing the same asset handle are unaffected)
    MeshHandle mesh = scene.GetComponent<RenderableComponent>(entity).MeshHandle;
    m_RRM->ReleaseMesh(mesh);
    // Similarly release texture handles referenced by the entity's material

    m_TotalResidentBytes -= m_ResidentBytes.Get(entity);
    m_ResidentBytes.Remove(entity);
    comp.IsLoaded = false;
}
```

`RenderResourceManager` uses reference counting for GPU resources. Releasing a handle decrements the reference count. The GPU buffer is only freed when the count reaches zero. Two entities sharing the same mesh (e.g. instanced trees) are safe: the second release is a no-op on the GPU until both entities evict.

### ProcessCompletedLoads

```cpp
void StreamingManager::ProcessCompletedLoads(Scene& scene) {
    for each (entity, ticket) in m_PendingLoads:
        if m_Importer->IsComplete(ticket):
            ImportResult result = m_Importer->GetResult(ticket);
            // Bind the newly loaded GPU handles to the entity's components
            scene.GetComponent<RenderableComponent>(entity).MeshHandle = result.MeshHandle;
            // Apply texture handles, material references, etc.
            uint64_t bytes = result.ResidentBytes;
            m_ResidentBytes.Insert(entity, bytes);
            m_TotalResidentBytes  += bytes;
            m_TotalInFlightBytes  -= m_Importer->GetEstimatedBytes(result.UUID);
            scene.GetComponent<StreamableComponent>(entity).IsLoaded  = true;
            scene.GetComponent<StreamableComponent>(entity).IsLoading = false;
            m_PendingLoads.Remove(entity);
```

---

## 5. Streaming Cell Grid

Iterating every entity in the scene each frame to check streaming is O(total_entities). For a world with 50,000 entities this is too slow — most are far from the camera and irrelevant.

The streaming cell grid partitions the world into a uniform grid of cells with side length `CellSize` (default 100m). Each entity is registered in the cell corresponding to its world position. `StreamingManager::Tick` only evaluates entities in cells within `ceil(MaxStreamRadius / CellSize)` cells of the camera.

```cpp
// ZEngine/Streaming/StreamingGrid.h

struct CellCoord {
    int32_t X, Z;  // cell index (Y axis is not partitioned for streaming)
};

class StreamingGrid {
public:
    void Initialize(float cell_size);
    void RegisterEntity   (EntityID entity, const Vec3f& world_pos);
    void UnregisterEntity (EntityID entity);
    void UpdateEntity     (EntityID entity, const Vec3f& new_pos);

    // Returns all entities in cells within radius cells of center_cell
    void QueryRadius(CellCoord center, int32_t radius_cells,
                     Array<EntityID>& out_entities) const;

private:
    float                                  m_CellSize;
    UnorderedHashMap<uint64_t, Array<EntityID>> m_Cells;  // key: pack(X,Z) -> uint64
};
```

For a `MaxStreamRadius` of 500m and `CellSize` of 100m, the query radius is 5 cells. This covers at most `(2*5+1)^2 = 121` cells. Even at 50 entities per cell, that is 6,050 entities evaluated per frame — a manageable O(streaming_entities_in_range).

Entities are registered in the grid at scene load time and updated when `TransformComponent` changes (not every frame — static geometry is the common case).

---

## 6. LOD Integration

Streaming and LOD are orthogonal systems that cooperate. The streaming system controls whether GPU resources are resident; the LOD system controls which of the resident levels is rendered.

Cooperation protocol:

- LOD levels are separate streaming units. `LODDescriptor` (see `lod-system.md`) defines a `StreamRadius` per level: LOD3 (lowest detail) has the largest `StreamRadius`, LOD0 (full detail) has the smallest.
- The streaming system can have LOD3 loaded while LOD0 is still in flight.
- `ActiveLODComponent` is set to the highest available level — not the ideal level. The LOD system checks `IsLoaded` flags on each LOD level's `StreamableComponent` before selecting the active level.
- Upgrade path: when LOD0 finishes loading, `ProcessCompletedLoads` updates the mesh handle and the LOD system automatically switches to LOD0 on the next frame.

This means an entity is visible immediately at LOD3 quality and silently upgrades to higher quality as data arrives — no pop, no invisible frame.

---

## 7. Texture Mip Streaming

Texture mip streaming is a secondary priority, dependent on KTX2 support (see `texture-compression.md`). The design is noted here for completeness.

Vulkan supports sparse images (`VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT`) and `VK_EXT_image_compression`, which enable partial mip residency. The streaming design for textures mirrors the mesh design:

- At first stream-in: upload only the lowest 4 mip levels (coarse resolution). This fits in a small fraction of the full texture size.
- As the camera approaches within a closer `DetailStreamRadius`: upload the next 4 mip levels.
- On budget pressure: evict the highest mip levels first (they are the largest and least needed at distance).

The mip streaming system uses `VkSparseImageMemoryBind` to bind and unbind mip pages. Each mip level is treated as an independent streaming unit with its own residency state.

The KTX2 container stores all mip levels in a single file. The `VFSContext` supports partial reads (byte-range reads), so individual mip levels can be uploaded without loading the entire KTX2 file.

Full implementation is deferred until `texture-compression.md` is complete and KTX2 runtime loading is in place.

---

## 8. Scene Graph Integration

Scene serialization is aware of streaming. When a scene is serialized, entities with `StreamableComponent` write their streaming metadata:

```
entity:
  StreamableComponent:
    StreamRadius: 200.0
    UnloadFactor: 1.5
    StreamPriority: 10
    PrimaryAssetUUID: "a3f2..."
```

At scene load time (not asset load time):

1. All entities are created with components, including `StreamableComponent`.
2. `RenderableComponent` is created with a null `MeshHandle` (no GPU resource yet).
3. `StreamingManager` registers each streamable entity in the `StreamingGrid`.
4. On the first `StreamingManager::Tick`, entities within `StreamRadius` of the initial camera position are enqueued for load.
5. The game can optionally block the first frame until a minimum set of assets is loaded (a "blocking preload" list specified in the scene header, used for assets the player can see immediately on spawn).

This ensures scene load time is bounded by the blocking preload set, not the total asset count.

---

## 9. File Layout

```
ZEngine/Streaming/
    StreamableComponent.h           -- StreamableComponent struct
    StreamingManager.h              -- StreamingManager class declaration
    StreamingManager.cpp            -- Tick, EnqueueLoad, EnqueueEvict, ProcessCompletedLoads
    StreamingGrid.h                 -- CellCoord, StreamingGrid class
    StreamingGrid.cpp               -- cell registration, radius query
```

---

## 10. Deliverables Checklist

- [ ] `StreamableComponent` defined and registered in ECS
- [ ] `StreamingGrid` implemented with `RegisterEntity`, `UnregisterEntity`, `QueryRadius`
- [ ] `StreamingManager::Initialize` wires up `ImportCoordinator` and `RenderResourceManager`
- [ ] `StreamingManager::Tick` implemented: load/evict candidate collection, priority sort, budget check, throttle limits
- [ ] `EnqueueLoad` submits `ImportRequest` and records in-flight ticket
- [ ] `ProcessCompletedLoads` binds loaded GPU handles to entity components
- [ ] `EnqueueEvict` releases GPU handles via `RenderResourceManager` reference counting
- [ ] LOD streaming cooperation: `StreamableComponent` per LOD level, upgrade on load completion
- [ ] Scene serialization reads/writes `StreamableComponent` fields including `PrimaryAssetUUID`
- [ ] Blocking preload list mechanism at scene load time
- [ ] Budget accounting: `ActiveBytesInFlight`, `ActiveBytesResident` queries exposed
- [ ] Unit test: entity at distance 50m with `StreamRadius = 100m` is enqueued for load
- [ ] Unit test: entity at distance 200m with `StreamRadius = 100m` and `UnloadFactor = 1.5` is enqueued for eviction
- [ ] Unit test: budget exceeded, new loads are blocked
- [ ] Integration test: open-world scene with 5000 entities, camera moves 500m, assert VRAM does not exceed budget
- [ ] Texture mip streaming design documented (deferred to post-KTX2 implementation)
