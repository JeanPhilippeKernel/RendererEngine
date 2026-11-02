# Thumbnail Generation — Editor Asset Previews

**Priority:** P4
**Status:** Completed — core thumbnail generation (2026-11-02); implementation record for [PR #869](https://github.com/JeanPhilippeKernel/RendererEngine/pull/869), not a claim that the draft PR has merged
**Tracked by:** [#423](https://github.com/JeanPhilippeKernel/RendererEngine/issues/423)
**Depends on:** `render-resource-manager.md`, `import-pipeline.md`, the asset registry, the render graph, and ZUI bindless-image rendering
**Blocks:** Nothing critical; this is an editor experience feature.

## Completion scope and latest validation

The core milestone is complete: visible-only texture, mesh, and material previews; independent import-time loading without viewport placement; bounded scheduling and residency; validated persistent caching; material-alpha presentation; invalidation, cancellation, and timeline-safe shutdown. The checkerboard fills the entire rectangular preview well while the image remains square and centered; metadata below the preview stays solid.

The cache-cleanup/render-recording isolation, asynchronous folder listing with expected-linear VFS deduplication, and delete/reimport/viewport-drop lifecycle corrections are also implemented. These close the reproduced thumbnail-related stalls and lifecycle crash, not every possible renderer or driver stall.

- Local lifecycle-baseline full suites passed 1,734 engine tests in Release and 1,720 in Debug, with four existing engine skips in each configuration (and one existing disabled Release test). Native SampleProject acceptance covered delete/reimport/drop with an existing actor and import-time thumbnail regeneration before placement.
- After the checkerboard correction, all 1,260 editor tests pass in both Debug and Release. The layout regression verifies full-well backdrop bounds, square image dimensions, centering, and file orientation at 0.75x, 1x, and 1.5x scales. Native SampleProject material cards were visually checked in the updated Release editor.
- Windows, macOS, and Linux Debug build/test CI passed at `328ad003` in [run 41408750568](https://github.com/JeanPhilippeKernel/RendererEngine/actions/runs/41408750568). Those results precede the checkerboard-only correction; Release CI jobs were not selected. CI success is not a claim of native GPU visual coverage on all three platforms.
- Latest checkerboard evidence: `/private/tmp/checkerboard-preview-{debug,release}-editor.{log,json}` and `/private/tmp/rendererengine-card-checkerboard-assets-5408.png`. Lifecycle evidence: `/private/tmp/mesh-review.kOlfo0/`. These are local artifacts, not committed or durable CI baselines.

Completion applies to the scope above. Optional prefetch/priority scheduling and real driver device-loss acceptance remain explicit follow-ups below, not unchecked requirements for this milestone. Historical measurements retain their original dates, versions, and counts.

## Current-engine baseline

The content browser is `Tetragrama::Panels::ProjectViewPanel`. It is a virtualized ZUI panel, not an ImGui component. Visible registered textures, meshes, and materials use generated previews, with static asset-icon masks for loading, unsupported, and failed assets. ZUI renders a dynamic bindless texture through `ZUIImage(ctx, key, texture_index, ...)`; the index is the live `Rendering::Textures::TextureHandle::Index`.

`Rendering::RenderResourceManager` (RRM) owns texture allocation, upload, bindless registration, reload, and timeline-gated destruction. It does not expose the historical `ImageHandle`, `AllocateImage`, `ReleaseImage`, or ImGui texture APIs. A thumbnail must therefore be an RRM texture drawn by ZUI through its bindless index.

The asset registry provides UUID, type, path, import state, and `Meta.SourceHash` through a copied `AssetRecordSnapshot`. The source hash is the canonical input-version key for a persisted preview. A raw VFS entry with no registered asset stays a static icon; directories never create thumbnail requests.

The registry has a bounded callback fan-out alongside RRM's primary callbacks. `ThumbnailService` subscribes to stale and removed events; it never overwrites an RRM callback. Listener deduplication checks the whole table before reusing an empty slot. Observer delivery and removal are serialized, so removal waits for an in-flight callback before an owner can be destroyed; same-thread unsubscription is supported.

## Implemented behavior

The editor now has a bounded, visible-only asset-preview path:

- `ThumbnailService` retains at most 512 entries. When full it reclaims failed entries or the least-recently-used resident texture, without evicting a resident handle already acquired for the current UI frame. Pending/discarded entries retain their decode ownership until completion.
- Preview allocations preserve sixteen bindless slots for scene/graph work. A rejected GPU allocation also reclaims an offscreen resident before the CPU table fills, then defers submissions for four editor updates while RRM retires it. This matters on devices with fewer than 512 bindless slots (the tested Mac now has 125 global texture slots after reserving three sampled images for forward environment lighting). The 32 MiB figure is a maximum thumbnail pixel payload, not a promise of 512 simultaneous resident images on every GPU.
- At most four submission attempts are made per editor update, including rejected allocations. At most sixteen requests may be in flight, including discarded work. Deferred, unsubmitted entries are reclaimable when no longer visible. Diagnostics expose resident, queued, in-flight, failed, eviction, deferral, per-frame submission, and estimated resident-pixel-byte counters.
- The Project View resolves a locked registry snapshot by path only for visible file cards, and retains the authored static icon while a request is pending or unavailable. Failed source work releases its texture and retries after 60, 120, 240, then at most 480 editor updates; retries preserve their failure count.
- Only `Registered`/`Loaded` texture, mesh, and material snapshots are previewable. Stale/importing snapshots still contain an old source hash, so they cannot regenerate or reuse that old cache key. Completion rechecks registry identity, source hash, type, state, and material dependency fingerprint before publication.
- Fresh imports finalize embedded identity, content hash, and source-backed metadata on the import worker before main-thread registry publication. The deferred scanner therefore sees the same hash instead of marking a brand-new asset stale. Main-thread completion consumes copied metadata rather than hashing/reading the cooked model.
- Visible mesh/material cards independently request missing CPU bindings; viewport placement is not a prerequisite. Four fixed load records deduplicate UUID/source-hash requests and share the four-attempt/sixteen-in-flight admission budgets with preview work. Workers reuse checked `LoadMeshAsset` (including material dependencies) or standalone material ingestion; queue rejection retries later, never inline on the UI thread. Material loading precedes factor/dependency classification. Mesh geometry loads only after a disk-cache miss, so a warm mesh PNG does not load the model.
- Load records are independent of evictable thumbnail entries. Cancellation, invalidation, or navigation cannot reuse a record until its worker signals completion. Shutdown joins the worker pool before service deinitialization, which also clears queued records abandoned by pool shutdown; no per-request heap wrapper or worker-owned lease is needed.
- RRM decodes a raster source on its existing worker path, aspect-fits it into a transparent 128 x 128 canvas, and publishes it through the ordinary bindless upload path. Preview RGB remains display-encoded and is sampled through an RGBA8 UNORM texture to match ZUI's display-encoded compositing. The panel centers a square quad inside the wider card area and flips file-decoded rows back to source-file orientation.
- Decode observation is serialized because Project View builds on the main thread while the renderer can also observe tracked decode completion. Success is published only after upload submission, not when the worker merely queues pixels. ZUI declares its bindless reads to the graph so upload acquires do not depend on a scene/material pass being present.
- Registry callbacks enqueue into a fixed 64-event MPSC invalidation ring. Only the editor update mutates the table; overflow conservatively invalidates it. Repeated invalidations do not prematurely release discarded decodes or prevent replacement requests.
- RRM queues editor-owned releases, polls pending decodes and unconsumed producer upload timelines, and then schedules render-timeline retirement. Editor world shutdown explicitly unregisters and deinitializes the arena-owned service before registry/RRM teardown; it also hands off pending handles.
- Texture previews use a write-through PNG cache under `.zodiacengine/cache/thumbnails`, keyed by UUID, source hash, and schema revision 3. A validated metadata trailer is published atomically with the PNG. Lookup, validation, corruption fallback, source decoding, and cache writing happen on RRM workers; `Acquire` performs no filesystem lookup or removal. A failed cache write leaves the in-memory thumbnail usable. The project-root `.zodiacengine` subtree is excluded from scans and watcher-driven rescans, preventing generated PNGs from becoming imported assets.
- `AssetPreviewPass`, owned by `GraphicRenderer`, has sixteen fixed task records and prepares at most one mesh/material preview per render frame. It uses the existing graph's external 128px UNORM color target, fixed-size transient depth, global geometry buffers, bindless reads, and a separate sampled-layout publication consumer. The producer exports its exact written color version; the non-culled consumer reads that version. No private queue submission, device, or fence wait is used.
- Mesh previews use resident engine geometry (requesting reload for an existing evicted streaming handle), validated raw runtime bounds, radius-relative camera/clipping, and neutral shading. Material previews use a procedural UV sphere with fixed neutral key/fill lighting and environment revision 1. Persistent dependency hashes include authored/resolved texture UUIDs and source hashes; live fingerprints additionally include import states and GPU-handle generations. Missing/stale/unbound dependencies keep the fallback icon. Alpha mode, base factors, and cutoff also participate in both fingerprints.
- Explicit `OPAQUE` ignores base opacity for coverage, `MASK` uses a binary cutoff, and `BLEND` preserves fractional alpha. Explicit modes multiply base-color texture and factor; old cooked materials without a mode retain `LEGACY` texture-replacement/dither behavior. The original mode cannot reliably be recovered without reimport. Preview RGB is straight alpha, composited by ZUI over a checkerboard. A proven fully invisible surface gets a “Fully transparent” placeholder and a tooltip explaining base opacity, mode, cutoff, and Legacy reimport guidance, without consuming a GPU target, decode, graph job, or disk PNG. Textured coverage is otherwise unknown, except that a zero factor proves invisibility for explicit BLEND and positive-cutoff MASK. Opaque alpha zero and MASK cutoff zero remain visible. This state participates in invalidation and LRU reclamation.
- The scene shares the alpha helper with previews and depth/selection shaders. BLEND draws are excluded from opaque depth/G-buffer work and sorted far-to-near into a depth-tested, non-depth-writing HDR forward pass before tone mapping. Picking has an independent integer ID/depth pass, so the nearest nonzero-alpha surface remains selectable. This is conventional mesh/object sorting, not order-independent transparency; transparent atmospheric fog and authored double-sidedness remain limitations. Preview recording requires both real vertex and fragment shader modules; an incomplete pipeline fails instead of publishing/caching an empty image.
- Recorded/submitted previews retain their target until the exact submission timeline completes or recording is cancelled. UI cancellation never frees recording work early. Completed targets remain ordinary RRM textures and use the same LRU/deferred-release path as textures. File-decoded images flip V; graph-rendered images do not.
- The browser listing holds up to 4,096 entries independently of the 512-thumbnail LRU. Only visible rows plus one overscan row on each side build cards. Watcher debounce saturation coalesces into the existing overflow/full-rescan protocol, preventing startup/import bursts from exceeding its fixed hash-table load limit.

The sections below retain the broader design direction. Prefetch/priority controls and real driver device-loss acceptance remain outside this milestone; deliberately delayed producer timelines were subsequently tested. There is no eagerly loaded disk index: direct worker lookup validates each visible request. Detailed local acceptance notes are not part of this committed design record; their relevant results and limitations are summarized here.

### Fresh-import acceptance (2026-11-01)

An isolated copy of `SampleProject/project.json` used `DefaultScene` with one directional light and no meshes. A new `FreshImportFixed.glb` was imported through the ordinary Asset Importer, without scene placement. Its material folder was opened before the mesh folder: all 16 cards resolved to 11 rendered spheres and 5 authored fully-transparent placeholders. Opening Meshes then generated the new model's thumbnail. The viewport/outliner stayed unchanged throughout. Restart also restored the mesh thumbnails from disk; debugger inspection confirmed zero CPU meshes, zero material bindings, and no active asset-load records (`warm-runtime-verified.txt`).

Evidence: `/private/tmp/thumbnail-import-ready.PPJHxk/` (`fixed-materials-before-mesh.png`, `fixed-mesh-without-placement.png`, `warm-mesh-without-geometry.png`). Focused importer/thumbnail tests: 62 passed; complete editor suite: 1,241 passed. Engine baseline for this change: 1,702 passed, 4 existing hot-reload skips (1,706 total). Regression coverage includes no-placement loading, standalone materials, duplicate coalescing, bounded cancelled loads, rejected dispatch, source changes, joined shutdown, scanner hash consistency, and warm-cache publication without geometry ingestion.

### Allocation, efficiency, and readability review (2026-11-01)

The thumbnail service and import-to-registry handoff were reviewed together with their ingestion, worker-queue, and shutdown contracts:

- Unchanged cards reuse the first cache lookup rather than searching the 512-entry table three times. Dependency changes still invalidate outstanding work before replacement. The fixed-capacity linear lookup and bounded LRU scans remain intentionally simple; no heap-backed index or duplicate ownership map was added.
- Spent frame/retry budgets reject in O(1), before computing full-table statistics. The four-attempt and sixteen-in-flight limits are unchanged.
- Each of the four fixed load records lazily reserves and reuses its own 4 MiB material scratch arena. Decode strings remain transient; durable material data is cloned before reuse. Worker completion is a final release-store in a small dispatch wrapper, while guarded loading lives in a separate function. Joined deinitialization releases scratch, including cancelled/abandoned requests.
- Import completion no longer copies borrowed `AssetImporterOutput` strings or retains full sidecars. A compact 440-byte record stores type, path, UUID, and hash; the 6,760-byte sidecar/settings remain on disk. Records reserve once for the output count in the existing 8 MiB result slab and retain capacity across imports. Checked exhaustion reports an import failure, releases publication, and permits a later smaller import. Main-thread registration reconstructs only the metadata fields actually retained by the registry.

Repeatable Debug workloads process 131,072 acquisitions against a full synthetic pending table and 131,072 spent-budget attempts. The initial run took 1,285,938 us and 126,787 us respectively; optimized acquisitions took approximately 0.43 s and spent-budget checks approximately 0.3 ms. These isolate controller overhead, not real editor frame rate or cold decoding performance. Timings are recorded as test properties rather than hardware-dependent CI thresholds.

A temporary macOS allocator interposer covered malloc/calloc/realloc/aligned and zone allocation entry points and symbolized native stacks. Across ten scheduling, cancellation, rejection, scratch-reuse, warm-cache, and workload tests it intercepted 11,865 successful allocation calls overall, **zero beneath `Acquire` or `TakeSubmissionBudget`**, and 149 beneath the known-allocating importer-test positive control. Setup, thread creation, actual asset decoding, JSON parsing, durable asset data, and GPU/cache allocations are not claimed to be zero-allocation. The preview LRU controls preview textures, not AssetManager's separate CPU asset residency policy.

Verification: 67 focused tests and 1,246 complete editor tests passed. Added coverage checks compact record/storage reuse, borrowed-output independence, budget-failure recovery, and scratch reuse/release. Evidence is under `/private/tmp/thumbnail-review.SmvHiL/` (baseline/optimized XML properties, `allocation-probe-verified.log`, temporary probe source, and full editor results). A repeat native GUI capture after this refactor was unavailable because the desktop was locked; the preceding fresh-import visual acceptance remains recorded above. No shader or preview appearance changes were made by this review.

## Goal

Show useful previews for visible, registered texture, mesh, and material assets without stalling the editor, retaining stale GPU resources, or loading a project's complete preview cache at startup.

The implemented target is a 128 x 128 RGBA8 preview, aspect-fit inside the existing Project View thumbnail area, with a checkerboard drawn behind the image.

## Ownership and public boundary

The editor owns one `ThumbnailService` under the Tetragrama editor layer. It is a client of the registry, import pipeline, RRM, render graph, VFS, and Project View. It does not own Vulkan objects directly.

`ProjectViewPanel` only asks for a preview while it builds a visible card:

```cpp
const auto handle = thumbnails.Acquire(record);
if (handle.Valid())
    DrawThumbnailImage(ctx, key, static_cast<uint32_t>(handle.Index), scale,
                       thumbnails.IsFileDecoded(record));
// Otherwise retain the existing authored type icon or transparent placeholder.
```

`Acquire(record)` is idempotent. It returns a resident thumbnail immediately, schedules at most one request for a missing key within the budget, and otherwise returns an invalid handle so the panel retains its type icon. The panel owns neither callbacks nor a second per-UUID handle map. It resolves the result every frame, so a reused RRM texture slot cannot be drawn through a cached stale index. The priority-taking API sketches below describe a future extension, not the current signature.

```cpp
enum class ThumbnailPriority : uint8_t { Prefetch, Visible };

struct ThumbnailKey {
    uuids::uuid AssetUUID       = {};
    uint64_t    SourceHash      = 0;
    uint32_t    SchemaRevision = 0;
    uint8_t     AssetKind       = 0;
};

void Request(const Core::VFS::AssetRecordSnapshot& record, ThumbnailPriority priority);
Rendering::Textures::TextureHandle Acquire(const Core::VFS::AssetRecordSnapshot& record,
                                           ThumbnailPriority priority);
void OnAssetStale(const uuids::uuid& uuid);
void OnAssetRemoved(const uuids::uuid& uuid);
void BeginFrame(uint8_t frame_index);
void EndFrame();
```

`ThumbnailKey` equality includes UUID, source hash, schema revision, and asset kind. The schema revision changes when preview framing, shading, colour handling, or file encoding changes. Materials also require a dependency fingerprint before persistent previews are enabled: their own source hash is not sufficient when a referenced texture changes.

## State machine and bounded work

Each key has one entry in a bounded service table:

```
Missing
  -> DiskQueued | CpuQueued
DiskQueued -> UploadQueued | CpuQueued | Failed
CpuQueued  -> TextureDecode | RenderQueued | Failed
TextureDecode -> UploadQueued | Failed
RenderQueued  -> GpuSubmitted | Failed
GpuSubmitted  -> ReadbackReady | Failed
ReadbackReady -> UploadQueued | Failed
UploadQueued  -> Resident | Failed
Resident      -> Evicted | Invalidated
Evicted       -> DiskQueued | CpuQueued
Invalidated   -> Missing
```

Implemented requests are visible-only, deduplicated by key, and retryable on later frames when bounded queues are saturated. A future prefetch extension would select visible work first and promote existing prefetch requests rather than duplicate them; priority promotion is not part of the completed implementation.

Use context-plus-function-pointer jobs and preallocated request/result records, consistent with the engine thread pool. Do not use `std::function`, per-request heap allocation, `std::promise`, or a worker that waits for the render thread.

Workers perform bounded CPU work only: VFS reads, image decoding, resampling, PNG decoding, and disk encoding. They copy an `AssetRecordSnapshot` before starting and check the key again before publishing a result. CPU pixels come from a small leased-buffer pool; a lease remains alive until RRM has consumed the upload source data and its upload timeline proves reuse is safe.

## Texture previews

Texture previews require no render pass:

1. Read and decode the registered source or cooked texture on a worker.
2. Resample into an aspect-fit 128 x 128 RGBA8 buffer. Define colour conversion: colour textures become sRGB-compatible display data; data textures use their designated display transform.
3. Queue the buffer through RRM's normal asynchronous texture upload path.
4. Publish `Resident` only when the bindless texture is usable by the render graph and ZUI.

Follow RRM's input-lifetime and streaming-ticket contract. Do not invoke the synchronous font-atlas upload path from `ProjectViewPanel` and do not block on an upload fence.

## Mesh and material previews

Mesh and material previews use an editor preview path integrated with the render graph. They do not create a private Vulkan device, issue ad-hoc queue submissions, or call `WaitFence()` during an editor tick.

The reusable graph-backed offscreen-preview facility provides:

- a bounded ring of 128 x 128 color/depth targets and readback buffers;
- an explicit graph pass that renders one queued preview using current-frame command ownership and synchronization;
- timeline-tagged completion polling in later frames;
- a per-frame submission/readback budget, initially one preview of either class; and
- deferred destruction through the device/RRM timeline mechanism.

The preview camera frames a validated AABB or bounding sphere at fixed azimuth and elevation. It derives near and far planes from that bound, includes a fit margin, and falls back to a placeholder for empty, non-finite, or degenerate bounds. Materials use a fixed sphere with the same camera convention. The preview setup has a versioned neutral environment and directional key/fill lighting; its revision is part of the persistence key. Shadows, scene lights, and scene environment lighting are out of scope.

The completed preview remains the offscreen target's RRM texture and is drawn directly in ZUI. Asynchronous readback and disk persistence are also implemented; the live preview need not make a GPU-to-CPU-to-GPU round trip before becoming visually useful.

## Residency and eviction

The default target is 512 completed 128 x 128 RGBA8 textures: 32 MiB of pixel payload before image, descriptor, staging, and allocator overhead. It is a default, not a universal editor-memory guarantee. Expose resident count, GPU-byte estimate, queued work, dropped prefetch count, and last failure for diagnostics.

Entries record a frame-use stamp when a visible Project View card acquires them. Table/resource pressure reclaims an offscreen least-recently-used resident entry, delegating release to RRM/device timeline-gated destruction. The entry immediately loses its live `TextureHandle`; a later acquire resolves to a placeholder until disk or generation work completes.

Placeholder visuals use the existing authored asset icons or an explanatory fully-transparent state. They do not use fake texture handles and are not resident-preview entries in the LRU table.

Startup never decodes or uploads project-wide preview pixels and loads no disk index. Preview bytes are read on demand for visible assets, with bounded overscan.

## Persistent cache

Persistent previews belong in the writable project cache, `.zodiacengine/cache/thumbnails`, not beside source files or `.meta` sidecars. This matches the engine's PSO-cache policy. Sources may be read-only, outside the project, or shared by version control. The implemented texture cache layout is:

```
<project>/.zodiacengine/cache/thumbnails/<uuid>/<source-hash>-<schema>.png
```

The PNG is written to an exclusively created, UUID-suffixed temporary sibling, then renamed only after encoding, flushing, and closing succeed. A job never overwrites or removes another writer's temporary. Encoding traverses upload pixels with a negative row stride without modifying them or changing STB's global flip state. The UUID/source-hash/schema filename makes ordinary source changes a miss.

Schema 3 appends an 80-byte little-endian trailer to the PNG, avoiding torn PNG/sidecar pairs. It contains `ZTHUMB03`, asset UUID, source hash, dependency hash, schema/renderer revisions, width/height, color-space/encoder contracts, payload byte count, and an FNV-1a64 checksum covering both payload and metadata. The reader caps payloads at 2 MiB, validates the full identity/checksum, and checks exact RGBA dimensions before decoding. This checksum detects accidental corruption; it is not an authenticity mechanism. A missing/invalid cache falls back to source within the same worker request, then atomically replaces the cache if writable. Cache hits are not rewritten. No deletion or existence probe runs on the UI thread. Failed source work retains normal bounded backoff.

`ThumbnailCacheMaintenance` streams recognized UUID directories on an RRM worker, without a recursive traversal or accumulated disk index. It removes obsolete schema/renderer/source/resolved-dependency variants, orphan previews, known fully transparent previews, and recognized generated PNG sidecars. Unloaded materials prune old source hashes but preserve unresolved same-source dependency variants; transient imports preserve current-format keys. Future formats, symlinks (including generated ancestors), unknown names, nested directories, and active/recent writer files are preserved. Recognized crash-left temporary siblings require 24 hours of age. Empty UUID directories are removed nonrecursively. One copied-root RRM task uses non-inline worker submission, shares shutdown accounting with texture jobs, supports cancellation, and exposes maintenance counters. The service schedules it after startup scanning settles, periodically, and after coalesced invalidations; the resolver rechecks scanning before treating assets as orphans.

Watcher renames across the project-root `.zodiacengine` boundary become user-tree deletion or creation events; internal cache renames remain ignored. This prevents a source moved into the cache from retaining a registered user-asset identity.

Mesh/material persistence uses `<uuid>/<source-hash>-<dependency-hash>-<schema>-<renderer-revision>.png` (schema 3, renderer revision 3). The renderer revision invalidates previews produced by older alpha/shader behavior without changing source assets. Worker-only validated lookup precedes graph work; a miss/corruption transitions to a budgeted graph request without failure backoff. Warm hits use ordinary tracked uploads, never rerender/readback/rewrite, and retain top-down rows so their orientation matches cold targets.

Cold graph targets have transfer-source usage. The graph copies the exact produced color version into its timeline-gated readback ring and adds an explicitly ordered sampled-layout restoration after the copy. At most four RRM-owned 64 KiB tickets cover pending readbacks and worker encodes; tickets copy identity/path and never reference editor/task memory. Only successfully recorded/published previews accept writes. The render-thread callback copies pixels once, then non-inline worker submission performs positive-stride PNG encoding and atomic publication. Saturation or write failure leaves the resident preview usable. Cancellation, failed host invalidation, renderer disposal, and RRM shutdown release tickets; active encodes share RRM's shutdown worker counter. Optional preview decode/write jobs never execute inline on worker-queue saturation.

Cold material rendering waits nonblockingly for queued texture reloads/decodes, recorded-but-unsubmitted GPU uploads, and deferred/real bindless descriptor updates; bindless handles can be allocated before their pixels are ready, ordinary texture uploads are submitted after Present, and descriptor registration deliberately exposes a fallback for one graph frame. The graph then handles submitted upload producer timelines. This conservative guard avoids persisting old, uninitialized, or temporary fallback map data under a new dependency key. Warm cache uploads do not require a material render.

## Invalidation, cancellation, and shutdown

The asset-event dispatcher sends stale and removed notifications after the registry accepts the event. Stale removes the current key from residency and marks pending results obsolete. PNG source/dependency keying plus validated metadata prevent stale reuse; asynchronous maintenance removes obsolete generated variants. Never invalidate or free an RRM resource directly from an importer or watcher thread.

The implemented service retains discarded requests until terminal completion and identifies GPU targets by index plus generation. It rechecks the registry key and dependency fingerprint before publishing, preventing obsolete work from becoming resident after rapid reimport. Graph callbacks reference fixed renderer-owned task records, never editor memory. A future pooled/reusable completion transport should additionally carry explicit monotonically increasing request epochs.

Shutdown stops new requests, cancels CPU publication, retires graph previews according to timeline values, releases resident textures through RRM, then clears CPU leases. It must be safe during minimize and swapchain recreation; thumbnail work must not wait on a GPU fence to make frame or shutdown progress.

## Project View integration

`ProjectViewPanel` keeps ownership of listing, filtering, tile virtualization, selection, and static icons. It adds a lightweight registry resolution step for visible non-directory entries. Registered previewable assets call `Acquire`; everything else keeps the current type icon. Requests come only from tiles built by the virtualized grid and its bounded overscan rows; there is no separate prefetch queue yet.

The checkerboard fills the existing rectangular thumbnail region, independently of the square image centered over it without stretching. The backdrop is 110 x 79 logical pixels and the image is 79 x 79 at default scale; both scale with the tile. Proven invisible materials use an explanatory placeholder/tooltip. The card's type strip, name, selection, drag source, and context menu remain unchanged. Loading and failed states use static icons; failure diagnostics remain developer-facing.

## Completed delivery

- [x] Registry-event fan-out, bounded service records, authored placeholders, diagnostics, and Project View bindless-image integration.
- [x] Visible-only texture previews using worker decode, bounded resampling, asynchronous RRM uploads, and LRU eviction.
- [x] Validated worker-side cache lookup and atomic texture-preview persistence, without a startup disk index.
- [x] Graph-backed offscreen mesh previews with exact submission-timeline polling.
- [x] Material previews, dependency fingerprinting, explicit alpha handling, and rendered-preview persistence.

## Verification record

### Historical Release follow-up (2026-11-02)

Release/mixed-asset acceptance superseded the older statements that Release and deliberately delayed timelines had not been tested. That instrumented Release run passed 1,710 engine tests (four existing skips; one disabled) and all 1,246 editor tests. Native fresh-import and warm-cache checks passed without viewport placement. Six autonomous captures traversed all 2,048 mixed assets down and back, with zero dropped records and preview failures; moving browser BuildContent p95 was 0.400–0.443 ms. Real timeline-gated preview/readback and shutdown-order tests passed with synchronization validation; device-loss coverage was explicitly a simulated flag, not an actual driver failure.

That run retained 263–279 ms whole-frame outliers, sampled RSS/physical footprint and VMA accounting, and separate retained CPU mesh costs. It did not cover other native OS/GPU combinations or actual device-loss shutdown; those results alone were not a blanket production sign-off.

The subsequent whole-frame hitch investigation attributed reproduced long frames to periodic thumbnail-cache cleanup sharing worker-affine FIFO queues with render-graph recording. The isolation fix uses device-owned recording mailboxes, notified completion, and bounded non-inline background admission. Both full configurations passed, and 25,624 native frames contained no frames above 50 ms (33.530 ms maximum) while seven captured cleanup jobs still took 274–289 ms.

The smaller folder-opening outlier was subsequently corrected with asynchronous listing/preparation and expected-linear VFS path deduplication. Four verified openings of the 2,048-asset Mixed folder published prepared entries in 0.037–0.088 ms, with enclosing browser updates of 0.297–0.468 ms instead of 30.950–32.462 ms. Evidence: `/private/tmp/async-folder-listing.yTDxSp/`. Separate MoltenVK submission/drawable-acquisition outliers remain outside the thumbnail milestone; no universal frame-latency guarantee is claimed.

### Historical alpha evidence (2026-11-01, macOS Debug)

- The explicit-alpha build passed 1,689 engine tests (1,693 total, four pre-existing skips) and all 1,235 editor tests. All four Vulkan alpha pixel tests ran with validation enabled. Native fixtures verified OPAQUE/MASK/BLEND/LEGACY previews and selection in a scene with zero opaque draws. Original SampleProject cold/warm runs showed eleven previews plus five explanatory placeholders; all 26 warm-cache PNG hashes/sizes/mtimes and all 78 source/config/meta hashes were unchanged.

### Previous thumbnail milestones (before explicit-alpha acceptance)

The counts, cache versions, descriptor limits, and timings below describe their original runs, not new measurements of the current alpha build.

- `ZEngineTests`, `TetragramaTests`, and `Obelisk` build with the `Darwin_arm64_Debug` preset.
- All 1,235 editor tests pass, including thumbnail-service policy/integration tests and presentation regressions checking square, centered, correctly oriented image quads and transparent placeholders at 0.75x, 1x, and 1.5x tile scales. Integration covers upload-submission readiness, changed source hashes, real stale events, per-frame/in-flight budgets, 1,024 registered assets with no startup work, simulated 2,048-asset scrolling/reimport, pressure-driven eviction before the CPU table fills, rendered-cache miss/discard/warm-hit phases, full rendered-cache filenames, overlong-path fallback, and transparent-state invalidation without GPU/disk submission.
- The native engine suite passes 1,670 tests with four pre-existing RRM hot-reload skips (1,674 total), using the installed MoltenVK ICD outside the sandbox so Metal and FSEvents are available. Added checks cover full cache identity/checksum validation, oversized/truncated inputs, concurrent atomic writers, aspect fitting, callback/retirement ownership, graph color/depth/publication declarations, mesh framing, material dependency changes, shared-stage uniform reflection, reserved-slot allocation, and 2,048-event watcher saturation/recovery. Rendered persistence adds top-down RGBA round trips, stable/live dependency separation, resolved path-fallback UUIDs, bounded readback tickets, copied worker pixels, saturation without inline I/O, stopped-pool cleanup, sampled-layout restoration ordering, submitted-readback disposal, texture decode/reload/unsubmitted-upload/descriptor readiness, and dropping retired thumbnail descriptor writes rather than requeueing their obsolete generations indefinitely. Follow-up tests cover conservative transparency classification, failure-output reset, cleanup safety/key resolution/cancellation, copied-root worker cleanup and stopped-pool accounting, plus concurrent bounded timing capture and CSV export.
- Native visual tests use `SampleProject/project.json`, scene `GizmoActors`, and its 15 existing texture assets. Cold generation, warm-cache restart, small/default/large tile reflow, source orientation, display colors, and material fallback icons were inspected. The visual check exposed and fixed stretched quads, inverted file rows, and unintended sRGB-to-linear darkening. All 15 cache files retained their sizes and modification times on warm restart. A generated cache entry was backed up and deliberately corrupted; it automatically regenerated to the original SHA-256, and its displayed result matched the recovered PNG without a vertical flip (mean 8-bit RGB error about 2.5). Local screenshots are under `/private/tmp/thumbnail-evidence.XsPYOi/`, including `browser-before.png`, `browser-after.png`, `browser-small.png`, `browser-large.png`, and `sample-cache-recovered.png`.
- Minimize/restore was confirmed through native window state and engine logs. Restoration recreated the swapchain; the recovered thumbnail retained the same pixel-comparison result afterward (`sample-restored.png`), with no new validation errors. The 19 focused editor tests passed ten consecutive runs, and the 11 cache/listener/rename regressions passed twenty consecutive runs.
- Native graph previews were inspected for the existing character mesh and sixteen material cards, including intentionally fully cutout materials. Mesh framing and minimize/restore are captured in `mesh-final.png` and `mesh-restored-final.png`; material lighting is captured in `materials-final.png`. Native validation exposed and fixed an invalid graph export, double-counted shared-stage dynamic descriptors, and false missing-binding warnings.
- Schema 3 was separately validated in SampleProject: all fifteen cache sizes/mtimes remained unchanged on warm restart (`schema3-warm-final.png`). Only the checksum byte of a backed-up generated cache was corrupted while its PNG payload stayed valid; requesting it regenerated the exact original SHA-256 from source (`schema3-checksum-recovered.png`). The screen matched the recovered file without flipping, with mean 8-bit RGB error about 2.4. Source files were not modified.
- Rendered disk persistence was validated with SampleProject's existing mesh and sixteen materials: seventeen PNGs were generated, and all seventeen sizes/mtimes stayed unchanged on restart. Native inspection confirmed warm entries used decoded uploads with no graph fallback. Cold/warm screenshots are `persist-mesh-cold.png`, `persist-mesh-warm.png`, `persist-materials-cold.png`, and `persist-materials-warm.png` in the evidence directory. RGBA-composited pixel comparisons matched upright (mean 8-bit RGB error about 4.3–4.6).
- Backed-up generated mesh/material checksums were independently corrupted. Both regenerated to their exact original SHA-256 (`persist-mesh-recovered.png`, `persist-materials-recovered.png`), and material pixel comparison remained unchanged across minimize/restore and swapchain recreation (`persist-restored.png`). Source/config/script hashes remained unchanged.
- Live dependency reimport used an isolated SampleProject copy at `/private/tmp/thumbnail-dependency.xnHcfD/project.json`, never the user's source assets. Changing the existing albedo dependency created distinct material cache keys; a warm restart reused the changed key without rewriting it. Repeated asymmetric dependency edits exposed unsubmitted-upload and one-frame fallback-descriptor hazards, plus stale descriptor generations being requeued after thumbnail retirement. After fixing readiness and dropping obsolete generations, fresh blue renders were reproducible and the final live-regenerated green preview matched its independent cold reference byte-for-byte. The final green result has SHA-256 `e9c75e2f72e6ed50a85af1693927bf3a1047aa5b44f6282db785fd3d560c345d`; screen comparison was upright with mean RGB error about 5.5 (`persist-dependency-final-verified.png`). Earlier `persist-before-upload-submit-fix.png` and `persist-dependency-hot-green.png` are retained failure evidence, not accepted baselines.
- An isolated `/private/tmp/thumbnail-stress.SYfw25/project.json` contains 2,048 asymmetric landscape/portrait/square/alpha fixtures; none were added to SampleProject. Native startup counters were all zero. Continuous scroll reached assets 2,016–2,047 and caused over 1,500 evictions with zero source failures after pressure recovery. Live reimport of visible asset 2,028 changed its displayed quadrants. The run exposed watcher debounce saturation and the Mac's 127-slot bindless ceiling; both now have regressions. Headroom is reserved to avoid starving scene targets. See `stress-first-page.png`, `stress-last-page.png`, and `stress-reimport.png` in the evidence directory.
- The final reserved-slot build was rerun through asset 700 and resized while under pressure: 87 resident previews (5.44 MiB pixel payload), 111/127 total bindless slots used, zero failed previews, and 269 evictions at the sample point. Sixteen slots remained free; viewport picking/resize no longer reported unavailable scene targets. See `stress-headroom-final.png` and `stress-resized-headroom.png`. Unsubmitted offscreen entries can remain in the fixed table but do not hold CPU pixel buffers or GPU resources and are reclaimable.
- Follow-up native testing verified eleven material previews plus five explicit transparent placeholders, removed 50 regenerable obsolete cache files (831,697 bytes), and preserved all 27 current PNGs byte-for-byte with unchanged sizes/mtimes on restart. All 78 tracked source/config/meta hashes remained unchanged. Quantitative scrolling covered SampleProject and a two-phase traversal of all 2,048 stress fixtures, including a 149 ms initial folder-build cost, slot-pressure warming limits, and a 164 ms render-frame outlier. Device-loss shutdown and deliberately delayed producer timelines had not yet been tested in that run; later timeline and performance results are recorded above. These historical Debug measurements are not comprehensive GPU-memory or latency guarantees.

### Completed core acceptance

- [x] Key/dependency invalidation, deduplication, queue saturation, LRU ordering, placeholder fallback, stale-result/handle rejection, and cancellation regressions.
- [x] Cache source/schema/dependency identity, corruption/truncation/checksum rejection, atomic concurrent publication, and recoverable write failure coverage.
- [x] Registry fan-out and independent asset loading without viewport placement, including changed-source and joined-shutdown handling.
- [x] More than 512 registered assets with no eager startup preview work, bounded visible/overscan requests, and 2,048-asset scrolling/reimport coverage.
- [x] Graph target/publication declarations, exact delayed submission/readback timelines, cancellation/retirement, shutdown ordering, resize, minimize, and swapchain recreation. Device-loss suppression is tested only with a simulated flag.
- [x] Native texture, mesh, material, and transparency presentation checks, plus fallback regressions and full-well checkerboard/square-image layout coverage at all supported tile scales.
- [x] Bounded-residency/admission profiling, allocation probes of controller acquisition paths, render/background-worker isolation, and nonblocking folder-list publication. This is not a whole-pipeline zero-allocation or zero-stall claim.

### Deferred follow-ups, outside completion scope

- Optional prefetch/priority queues and promotion, including any additional request-epoch transport needed if completion records become reusable across outstanding jobs. The shipped visible-only service retains ownership until terminal completion.
- Actual driver/device-loss shutdown, broader native GPU/OS visual acceptance, and sanitizer coverage. Existing skipped hot-reload fixtures are not successful GPU-lifecycle coverage.
- Separate submission/presentation outlier investigation and, if needed, incremental cache maintenance. Thumbnail work isolation does not eliminate driver waits or other renderer stalls.
- Transparent atmospheric fog, authored double-sidedness, and order-independent transparency are renderer follow-ups, not completed thumbnail features.

## Non-goals

- Previews for directories, scripts, scenes, arbitrary unregistered files, or every asset during startup.
- Previews reflecting the active scene camera, lights, environment, selection, or viewport post-processing.
- A second Vulkan device, editor-local raw Vulkan ownership, synchronous GPU readback, or a separate ImGui path.
