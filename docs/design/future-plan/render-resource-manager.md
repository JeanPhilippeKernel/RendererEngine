# Render Resource Manager — GPU Lifetime, Uploads, and Reload

**Status:** Implemented core GPU-resource lifetime authority. The remaining items are production hardening, not the old proposed `ImageHandle` design.

## Current responsibility boundary

`Rendering::RenderResourceManager` (RRM) is allocated during `Engine::Initialize`, after the Vulkan device, asset registry, and renderer configuration are available. It subscribes to AssetRegistry ready/stale/removed callbacks and bridges CPU asset data to render-thread GPU lifetime operations. It is shut down before the device.

The asset/import side owns source files, imported CPU assets, and registry state. RRM owns global mesh buffers, generic GPU buffer slots, upload command and staging lifetimes, geometry residency, texture upload/reload coordination, and timeline-gated GPU frees. A scene or editor must not directly own Vulkan resources just because it refers to an asset.

## Live resource contracts

`BufferHandle` is a generational, eight-byte `RenderHandle<BufferTag>`; generation zero is invalid. It names either a mesh slot or (using the internal tag bit) a generic buffer slot. A stale handle fails the generation check. There is no RRM `ImageHandle`, `GPUImage`, `GetImage`, or RRM singleton API. Earlier examples of those types are retired.

Meshes reside in separate global device-local vertex and index buffers managed by `GeometryPool`. A mesh slot contains a variable-size `GeometryRegion` and is `Unloaded`, `Pending`, `Resident`, or `Evicting`. `RenderResourceManager::IsMeshResident` is the render-time gate; a non-resident mesh is skipped and the renderer may enqueue a reload. Builtin geometry is stored in independent pinned buffers, so reset, eviction, or compaction cannot overwrite sky/grid data.

Textures use `Textures::TextureHandle`, the handle understood by the bindless texture system. `IngestTexture` uploads first use and reconstructs an existing handle in place for reimport. `ScheduleTextureReload` is deduplicated by UUID. `ReleaseTexture` first patches materials to the invalid-map sentinel and then delegates GPU teardown through the device's timeline-gated path.

The current texture file APIs use `const char*` because those are their shipped declarations. New engine-owned string APIs should use `cstring` when available; this documentation does not claim a signature migration that code has not made.

## Per-frame ownership

`AppRenderPipeline::BeginFrame` calls RRM `BeginFrame(frame_index)`; it retires completed mesh-batch staging, drives geometry streaming/compaction, then flushes queued mesh uploads/swaps and texture reloads/releases. `EndFrame()` closes any accumulated mesh-upload batch before `SubmitAsyncUploads()` is called after presentation.

The texture path has per-frame timeline tickets. A consuming render graph must acquire a ticket only after the producer submission succeeds, then acknowledge the ticket after recording the acquisition. The resource manager can retire a texture upload command slot only after its timeline proves completion. Generic buffers and raw Vulkan resources scheduled through `EnqueueDeletion` use the device deferred-free queue, not an RRM-local frame-count ring.

RRM callbacks may originate from asset/import threads only through their protected pending queues. Render-resource lookup, frame flushes, residency changes, and graph ticket consumption are render-thread responsibilities.

## Geometry capacity and streaming

At startup, an explicit generated-project `memory.geometry_streaming_mb` value is split equally between vertex and index buffers and clamped to 128–512 MiB per axis. When absent or invalid, the same per-axis capacity is derived from 15% of the largest device-local heap, then clamped to that range. This means a no-override pool can occupy up to 1 GiB across the two buffers; it is separate from CPU arena profile values and from the 384 MiB environment-lighting budget.

`GeometryStreamingManager` uses a render-thread SPSC reload queue, variable regions with merged free lists, an 85% usage eviction threshold, and a 30% fragmentation compaction threshold. Compaction reuploads resident CPU mesh assets into a packed pool through the normal batch. It does not make I/O or CPU asset data streamable.

## Production completion criteria

1. Add real-device integration coverage for upload, stale-handle rejection, hot-reload replacement, release, streaming reload, eviction, compaction, resize cancellation, and device-lost shutdown.
2. Define an explicit immutable render snapshot boundary. The current `RenderScene` seqlock snapshot prevents torn instance reads but does not make all render inputs editor-safe immutable data.
3. Make queue saturation observable and recoverable. Today bounded pending queues can drop mesh swap/reload work; dropped work needs metrics and a deterministic retry policy.
4. Audit all timeline dependencies and ownership transitions under validation layers on every supported platform. No CPU frame-count assumption may substitute for a GPU-completion proof.
5. Turn geometry capacity, environment baking, textures, transient graph resources, and VMA heap pressure into one coherent *reporting* surface without falsely claiming a single hard budget where none exists.
