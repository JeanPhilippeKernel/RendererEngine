# ZEngine — Virtual Geometry Streaming

**Status:** Design
**Closes:** Issue #622
**Depends on:** Global geometry buffer (PR #611, shipped), async upload queue (PR #766, shipped),
`bindless-descriptor-architecture.md` (design decision on pool model)
**Blocks:** Large-scene support, LOD system (Sprints 15–16)

---

## 1. Problem

The current global VB/IB reserves **1 GB of VRAM at startup** (512 MB vertex + 512 MB index,
`GeometryBytes` in `MemoryManager.h`), regardless of how much scene content is loaded. The
buffers are append-only: `ReleaseMeshGeometry` frees the slot and UUID map entry but leaves
the bytes permanently orphaned. Hot-reload swap makes this worse — each re-ingest appends new
data at the current watermark and orphans the old region. The only reclaim mechanism is
`ResetGeometryBuffers`, a full bulk reset that also corrupts the skybox and grid (see §3).

For projects whose total geometry fits in VRAM this is fine. For large scenes it is not, and
the static 1 GB reservation is wasted on machines with 4–8 GB VRAM budgets.

---

## 2. Design principles

- **Variable-size regions, not fixed pages.** One mesh asset = one contiguous VB region + one
  contiguous IB region. This matches how `AppendMeshData` already works and how the draw
  path consumes offsets (`VertexSB[VertexOffset + gl_VertexIndex]`). Forcing fixed-size pages
  would require splitting meshes across page boundaries, changing the draw path, and adding
  mapping overhead — complexity with no benefit for this engine's mesh size distribution.
- **The render thread never waits on streaming I/O.** A mesh whose region is not yet resident
  is simply skipped in `RenderScene` (no draw command emitted). No stall, no proxy.
- **Async upload through the existing batch timeline.** Streaming loads go through
  `m_batch_timeline` / `m_async_uploads` (the same path used for normal mesh uploads),
  not through a new fence-blocking submission.
- **Compaction instead of in-place reuse.** Fragmentation is resolved by a safe compaction
  pass triggered when free space falls below a threshold. Compaction re-uploads all resident
  meshes from offset 0, then re-registers builtins. This is simpler than a first-fit
  allocator over variable-size holes.
- **Builtins are pinned on a separate allocation.** Skybox and grid geometry live in a
  dedicated small VkBuffer that is never touched by the streaming system, eliminating the
  existing `ResetGeometryBuffers` corruption bug.

---

## 3. Existing bug — builtin geometry and ResetGeometryBuffers

`RegisterBuiltinGeometry` appends skybox/grid data to the main global VB/IB at the current
cursor and stores the resulting element offsets in the pass structs (`m_vtx_offset`,
`m_idx_offset`). `ResetGeometryBuffersInternal` zeroes the cursors but does not zero the
VkBuffer contents and does not call `RegisterBuiltinGeometry` again. The pass structs keep
their stale offsets, which point at GPU bytes that will be silently overwritten by the next
mesh upload. The passes appear to work until that overwrite occurs.

**Fix (prerequisite to streaming):** Give builtins their own separate VkBuffer allocation,
independent of the main streaming pool. Skybox and grid register into this buffer once at
`Setup` time; it is never touched by `ResetGeometryBuffers` or the streaming eviction path.

---

## 4. New types

### 4.1 GeometryRegion — a contiguous slot in the pool

```cpp
/// @brief A contiguous byte range allocated from the streaming geometry pool.
/// @details Stores both vertex and index regions together since they are always
///          allocated and freed as a pair for a single mesh asset.
struct GeometryRegion
{
    VkDeviceSize VtxByteOffset = 0; ///< Byte offset of this mesh's vertex data in the pool VB.
    VkDeviceSize IdxByteOffset = 0; ///< Byte offset of this mesh's index data in the pool IB.
    VkDeviceSize VtxByteSize   = 0; ///< Byte size of the vertex region.
    VkDeviceSize IdxByteSize   = 0; ///< Byte size of the index region.
};
```

Replaces the cursor pair as the unit of allocation. Stored alongside the existing `MeshSlot`
element-count offsets (which remain the draw-path interface — nothing in draw code changes).

### 4.2 StreamingState — per mesh slot

```cpp
/// @brief Lifecycle state of a mesh slot in the streaming geometry pool.
enum class StreamingState : uint8_t
{
    Unloaded = 0, ///< No region allocated; mesh has never been loaded or was evicted.
    Pending,      ///< Region allocated, GPU upload enqueued but not yet signalled.
    Resident,     ///< GPU-resident and safe to emit draw commands for.
    Evicting,     ///< Marked for eviction; a replacement upload may be in flight.
};
```

Added to `MeshSlot`. `RenderScene` checks `Resident` before emitting a draw command.

### 4.3 GeometryPool — the streaming allocator

Owns the global VB/IB and the free-region list. Replaces the raw cursor pair in
`RenderResourceManager`.

```cpp
/// @brief Variable-size region allocator over the global streaming vertex and index buffers.
/// @details One region per mesh asset; allocated on stream-in, freed on eviction.
///          Adjacent free regions are merged on free to limit fragmentation.
///          When the append cursor is exhausted and no free region is large enough,
///          Allocate returns false — the caller should trigger a compaction pass.
struct GeometryPool
{
    Core::Memory::BufferView VertexBuffer = {}; ///< Device-local VkBuffer for vertex data.
    Core::Memory::BufferView IndexBuffer  = {}; ///< Device-local VkBuffer for index data.

    VkDeviceSize VtxCapacity = 0; ///< Total vertex buffer capacity in bytes.
    VkDeviceSize IdxCapacity = 0; ///< Total index buffer capacity in bytes.
    VkDeviceSize VtxUsed     = 0; ///< Bytes currently held by live regions (excluding holes).
    VkDeviceSize IdxUsed     = 0;

    /// @brief Sorted free lists — one per axis.  Adjacent entries are merged on Free().
    Core::Containers::Array<GeometryRegion> FreeVtxRegions = {};
    Core::Containers::Array<GeometryRegion> FreeIdxRegions = {};

    VkDeviceSize VtxCursor = 0; ///< Append cursor; advanced only when the free list has no fit.
    VkDeviceSize IdxCursor = 0;

    /// @brief Allocate a region for a mesh of the given byte sizes.
    /// @param vtx_bytes Byte size of the vertex data to reserve.
    /// @param idx_bytes Byte size of the index data to reserve.
    /// @param out       Filled with the allocated region offsets and sizes on success.
    /// @return true on success; false if the pool is full (caller should compact).
    bool         Allocate(VkDeviceSize vtx_bytes, VkDeviceSize idx_bytes, GeometryRegion& out);

    /// @brief Return a region to the free list.
    /// @details Adjacent free regions at the same offset are merged to limit fragmentation.
    /// @param region The region previously returned by Allocate.
    void         Free(const GeometryRegion& region);

    /// @brief Ratio of dead bytes (holes) to total capacity.
    /// @details Compaction is triggered when this exceeds kCompactionThreshold (default: 0.30).
    /// @return Value in [0, 1]; 0 = fully packed, 1 = entirely holes.
    float        FragmentationRatio() const;
};
```

### 4.4 GeometryStreamingManager — the background worker

```cpp
/// @brief Per-mesh load request submitted to the streaming manager.
struct StreamRequest
{
    uuids::uuid           UUID     = {};     ///< Asset UUID, used for dedup and UUID map update.
    Managers::AssetHandle Asset    = 0;      ///< Asset handle to read geometry data from.
    BufferHandle          Handle   = {};     ///< Pre-allocated mesh slot — manager fills its region.
    uint8_t               Priority = 0;      ///< 0 = high (currently visible), 1 = prefetch.
};

/// @brief Drives streaming geometry loads and evictions on behalf of RenderResourceManager.
/// @details Tick() is called once per frame from RenderResourceManager::BeginFrame.
///          Load requests are enqueued from any thread via RequestLoad; eviction and
///          compaction are render-thread-only operations driven by Tick().
class GeometryStreamingManager
{
public:
    /// @brief Bind the manager to a device and RRM. Must be called before Tick().
    void Initialize(VulkanDevice* device, RenderResourceManager* rrm);
    void Deinitialize();

    /// @brief Per-frame driver — called from RenderResourceManager::BeginFrame.
    /// @details Drains completed async uploads (Pending → Resident), checks fragmentation,
    ///          and runs the eviction sweep if the pool is under pressure.
    /// @param frame_index Current swapchain frame index.
    void Tick(uint32_t frame_index);

    /// @brief Enqueue a mesh load request. Thread-safe.
    /// @param req Describes the mesh to load and its priority.
    void RequestLoad(const StreamRequest& req);

    /// @brief Mark a mesh slot for eviction. Render-thread only.
    /// @param handle Handle of the resident mesh to evict.
    void RequestEvict(BufferHandle handle);
};
```

`Tick` is called from `RenderResourceManager::BeginFrame`. It:
1. Drains completed async uploads, transitions `Pending → Resident`.
2. Checks fragmentation ratio; if above threshold, schedules a compaction.
3. Evicts the lowest-priority resident mesh if the pool is full and a new load is waiting.

Background uploads are submitted via the existing `m_async_uploads` queue; the batch
timeline (`m_batch_timeline`) signals completion exactly as it does for normal mesh uploads.

---

## 5. Eviction policy — clock-hand

Each `MeshSlot` carries a `Referenced` bit, set by `RenderScene` whenever the mesh emits a
draw command (i.e., it is visible and resident this frame).

The streaming manager's eviction sweep walks `m_mesh_slots` in order (clock hand). For each
`Resident` slot:
- If `Referenced == true`: clear the bit, advance.
- If `Referenced == false`: this slot is a candidate. Evict if the pool needs space.

This gives recently-drawn meshes one full cycle of grace before eviction. No sorted structure,
no timestamps — O(N) sweep over the slot array, called only when eviction is needed.

Slots with `Pinned == true` (builtins, or any slot explicitly pinned by the caller) are
skipped by the sweep unconditionally.

---

## 6. Compaction

Triggered when `GeometryPool::FragmentationRatio()` exceeds `kCompactionThreshold` (default:
0.30 — 30% of capacity is holes). The compaction pass:

1. Snapshot all `Resident` mesh slots and their `GeometryRegion` byte ranges.
2. Reset pool cursors to 0, clear the free lists.
3. Re-upload all resident meshes in slot order, assigning new regions from offset 0.
4. Update each `MeshSlot`'s `VtxOffset`/`IdxOffset` atomically before the next frame's
   draw-command assembly.
5. Builtin geometry is unaffected — it lives in a separate pinned buffer.

Compaction runs inside `RenderResourceManager::BeginFrame` and completes before `RenderScene`
assembles draw commands. The re-upload batch is submitted via `m_batch_timeline` as usual;
`BeginFrame` waits on that signal before returning.

For scenes where mesh count is large and eviction is frequent, compaction should be rare
(fragmentation stays low because evicted regions are freed and reused). If it fires
frequently, the pool budget should be increased — raise `geometry_streaming_mb` in
`project.json` or let auto-detection pick a larger value on higher-VRAM hardware (see §7).

---

## 7. Pool budget and configuration

### 7.1 Auto-detection

The streaming pool size is never prompted from the user during project creation. At startup,
before `VulkanDevice::Initialize`, the engine queries `VkPhysicalDeviceMemoryProperties` for
the total device-local heap size and derives the geometry streaming budget as **15% of
device-local VRAM**, clamped to [128 MB, 512 MB]:

```cpp
/// @brief Derive a geometry streaming pool size from the physical device's VRAM.
/// @param device_local_bytes Total device-local heap capacity in bytes.
/// @return Budget in bytes, clamped to [128 MB, 512 MB].
static VkDeviceSize DeriveGeometryBudget(VkDeviceSize device_local_bytes)
{
    VkDeviceSize budget = device_local_bytes * 15 / 100;
    budget              = std::max(budget, 128ULL << 20);
    budget              = std::min(budget, 512ULL << 20);
    return budget;
}
```

This produces sensible defaults without user input: a 4 GB GPU gets ~600 MB clamped to
512 MB; a 2 GB GPU gets ~300 MB; a 1 GB GPU gets ~150 MB.

### 7.2 Optional project.json override

If the project file contains a `"memory"` section, the explicit value overrides the
auto-detected budget. The field is optional — omitting it silently falls back to
auto-detection. Users only set it when targeting a known VRAM tier (e.g. a console port or a
low-end PC SKU) or when the watermark warning fires repeatedly.

```json
{
    "memory": {
        "geometry_streaming_mb": 256
    }
}
```

### 7.3 Two-pass project file read

`project.json` is currently parsed in `EditorConfiguration::ReadConfig` (Tetragrama), which
runs after `VulkanDevice::Initialize`. The `"memory"` section must be read **before** the
device initializes because `GpuAllocator::Initialize` commits pool sizes at that point.

The fix is a lightweight pre-parse in `EntryPoint.cpp` — just the `"memory"` section, using
the same `--projectConfigFile` argument already present — before `MemoryBudgetConfig` is
constructed. The full `EditorConfiguration::ReadConfig` still runs later for everything else.

```cpp
/// @brief Read the geometry streaming budget override from a project config file.
/// @details Returns 0 if the file is absent, unparseable, or has no memory section —
///          the caller falls back to auto-detection in that case.
/// @param config_file Absolute path to project.json.
/// @return Override budget in bytes, or 0 for auto-detect.
static VkDeviceSize ReadGeometryBudgetOverride(const std::string& config_file)
{
    if (config_file.empty())
        return 0;
    std::ifstream f(config_file);
    if (!f.is_open())
        return 0;
    auto json = nlohmann::json::parse(f, nullptr, false);
    if (json.is_discarded() || !json.contains("memory"))
        return 0;
    const auto& mem = json["memory"];
    if (!mem.contains("geometry_streaming_mb"))
        return 0;
    return static_cast<VkDeviceSize>(mem["geometry_streaming_mb"].get<uint32_t>()) << 20;
}
```

### 7.4 Compile-time constants

```cpp
/// @brief Builtin geometry buffer size — skybox + grid + headroom. Never auto-derived.
constexpr uint64_t BuiltinGeometryBytes = 1ULL << 20;
```

`MAX_BUFFERS` (4096) and `MAX_UUID_MAP` (4096) are slot counts, not byte budgets, and remain
unchanged.

---

## 8. Changes to existing code

### RenderResourceManager

| Change | Reason |
|---|---|
| Replace `m_vtx_cursor`/`m_idx_cursor` with `GeometryPool m_pool` | Pool owns allocation |
| Add `BufferView m_builtin_vtx_buf`, `m_builtin_idx_buf` | Pinned builtin geometry |
| Add `StreamingState`, `Referenced`, `Pinned` to `MeshSlot` | Draw guard + eviction |
| `ResetGeometryBuffers` resets pool only, never touches builtin buffers | Bug fix |
| `RegisterBuiltinGeometry` writes to builtin buffers | Bug fix |
| `AppendMeshData` calls `m_pool.Allocate()` instead of advancing cursors | Pool allocation |
| `ReleaseMeshGeometry` calls `m_pool.Free(region)` | Byte reclaim |
| `BeginFrame` calls `m_streaming_mgr.Tick(frame_index)` | Streaming driver |

### AppRenderPipeline::RenderScene

```cpp
auto handle = rrm->FindMeshBuffer(inst.MeshUUID);
if (!handle.IsValid())
    continue;

/// @note Mesh may be allocated but not yet GPU-resident (upload in flight).
///       Skip draw command emission rather than stalling the render thread.
if (!rrm->IsMeshResident(handle))
    continue;

rrm->MarkMeshReferenced(handle);   // set clock-hand Referenced bit for this frame
```

### RendererPasses (SkyboxPass, GridPass)

`RegisterBuiltinGeometry` writes into the pinned builtin buffer after this change. The draw
path for these passes already uses the conventional `BindVertexBuffer`/`BindIndexBuffer` +
`DrawIndexed` path (not the storage-buffer path), so they bind the builtin buffer's
`VkBuffer` handle directly. No shader changes required.

### GpuAllocator / MemoryManager

Replace `GeometryBytes = 512 MB` with two separate VMA pool allocations:
`GeometryStreamingBytes` (256 MB, two blocks) for the streaming pool and
`BuiltinGeometryBytes` (1 MB, one block) for the builtin buffer.

---

## 9. What does NOT change

- **Draw-path shader code**: `VertexSB[VertexOffset + gl_VertexIndex]` is unchanged. The
  streaming system updates `MeshSlot.VtxOffset`/`IdxOffset` when a region is assigned; the
  shader reads them as before.
- **`SubMeshAllocation` / `VkDrawIndirectCommand` assembly**: unchanged — still element-count
  offsets, still indirect.
- **Texture streaming**: handled separately by the LRU eviction path described in
  `bindless-descriptor-architecture.md` §7. Geometry and texture streaming are independent.
- **`AsyncUploadQueue` / `m_batch_timeline`**: reused as-is for streaming uploads.

---

## 10. Deliverables checklist

| Item | Status |
|---|---|
| Fix `RegisterBuiltinGeometry` — separate pinned builtin buffer | Not started |
| `GeometryPool` struct — `Allocate`/`Free`/`FragmentationRatio` | Not started |
| Replace cursors with `GeometryPool` in RRM | Not started |
| `StreamingState` + `Referenced` + `Pinned` in `MeshSlot` | Not started |
| `ReleaseMeshGeometry` calls `m_pool.Free` | Not started |
| `GeometryStreamingManager` skeleton + `Tick` | Not started |
| Clock-hand eviction sweep | Not started |
| Compaction pass | Not started |
| `RenderScene` resident guard + `MarkMeshReferenced` | Not started |
| `MemoryBudgetConfig` new fields, reduce default 512 MB → 128 MB per buffer | Not started |
| Unit tests: alloc/free/fragmentation, eviction order, compaction correctness | Not started |

---

## 11. Implementation order

1. **Builtin buffer bug fix** — isolated, testable, no streaming machinery needed.
2. **`GeometryPool`** — pure data structure, no Vulkan. Unit-testable standalone.
3. **Wire `GeometryPool` into RRM** — replace cursors, update `ReleaseMeshGeometry`.
4. **`StreamingState` + draw-path guard** — once pool is live, non-resident meshes must be
   skippable before the eviction sweep is wired in.
5. **Eviction sweep** — clock-hand, driven by `Tick`.
6. **Compaction** — last, only needed once fragmentation is observed in practice.
