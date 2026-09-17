# ZEngine — Geometry Residency and Streaming

**Status:** The current geometry pool, residency state, eviction, reload queue,
and compaction are implemented. This is not yet virtual geometry or
asynchronous asset-I/O streaming.

## Current implementation

`RenderResourceManager` owns one device-local global vertex buffer and one
device-local global index buffer. Their capacities are set by the generated
project `memory.geometry_streaming_mb` override when present, otherwise from
15% of the largest device-local heap; each axis is clamped to 128–512 MiB.
The two axes therefore consume an equal half of the configured total.

`GeometryPool` allocates one contiguous `GeometryRegion` per mesh: vertex
and index byte ranges are allocated/freed together. It first reuses sorted,
merged free-list ranges, then advances its append cursor. It tracks used bytes
and reports fragmentation; reset clears pool metadata but not Vulkan buffer
objects.

Each mesh handle has a `StreamingState`. A renderer must call
`IsMeshResident()` before it emits a draw. A non-resident mesh is skipped and
can be queued through `RequestMeshLoad()`; the render-thread SPSC queue is
drained during the next RRM `BeginFrame()` and appends CPU-resident imported
mesh data to the per-frame upload batch. Successful reload transitions directly
to `Resident`; it does not currently wait for a distinct asynchronous
readback/completion state.

When either pool axis exceeds 85% used, `GeometryStreamingManager` runs a
clock-hand pass. A resident mesh whose fresh `Referenced` bit is false may be
evicted unless it is pinned. The bit is cleared once per streaming tick, and
the renderer marks visible resident meshes later in the frame. Fragmentation
over 30% requests a compaction. RRM performs that compaction in `BeginFrame`
by reading the still CPU-resident mesh assets and reuploading resident entries
into a packed pool.

Sky and grid geometry is placed in separate builtin buffers. It is not affected
by geometry-pool reset, eviction, or compaction.

## Important limits

- The implementation is **mesh residency management**, not Nanite-style
  virtual geometry: there are no clusters, hierarchical culling, pages,
  feedback buffers, or partial mesh residency.
- The CPU mesh asset stays resident. Reload uses that copy; there is no
  background disk/network request, priority scheduler, or cancellation model.
- The reload SPSC queue has 256 entries. A full queue drops the request; callers
  must not treat a failed enqueue as eventual residency.
- Upload completion is governed by the existing batch/timeline mechanisms.
  Production tests must prove a mesh is never drawn before its uploaded region
  is available to the consuming submission.
- This capacity is independent of `MemoryBudgetConfig` CPU arena slots and of
  the environment-lighting 384 MiB persistent-resource gate.

## Production path to virtual geometry

1. First add telemetry and real-device tests for residency, reload, eviction,
   compaction, queue saturation, and reset under GPU validation.
2. Introduce an asset streaming service with cancellable, priority-aware I/O
   and explicit CPU decoded-data lifetime. Do not make the render thread wait
   on it.
3. Design stable page/cluster metadata, GPU residency tables, feedback,
   visibility hierarchy, and upload ownership. Scene serialization must refer
   to durable mesh assets, never transient geometry regions or buffer handles.
4. Gate page-table publication and eviction with GPU timeline proofs, immutable
   render snapshots, and scene epochs. Handle a scene unload, hot reload,
   resize, and device loss without a stale page becoming drawable.
5. Benchmark representative scenes before selecting a production budget policy;
   the current 15% heuristic is a startup default, not a hardware guarantee.
