# Draw Submission Ordering

**Status:** Material-level sort utilities and GPU frustum culling are
implemented; the live scene indirect path does not yet use material sorting or
support a production transparent submission path.
**Tracked by:** [#663](https://github.com/JeanPhilippeKernel/RendererEngine/issues/663) for
compacted indirect-count submission. Its claim that the current renderer is CPU-cull-only is
stale: the shipped `FrustumCullingPass` already performs GPU frustum rejection.

## Current draw path

`AppRenderPipeline::RenderScene` snapshots `RenderScene::Instances`, builds
one `SubMeshAllocation` and one `FrustumCullingInput` per submesh, and
writes per-frame transform, draw-data, and culling-input buffers. Candidate
order is the snapshot/submesh order. It is not the old unordered-map path
described by prior versions of this document.

`FrustumCullingPass` copies each indirect candidate to the per-frame
`CulledIndirectBuffer` and changes `instanceCount` for candidates outside
the view. Both `DepthPrePass` and `GbufferPass` issue the full candidate
range from that buffer. The fixed capacity is
`SceneData::MAX_DRAW_COMMANDS == 8192`; exceeding it validates rather than
silently truncating the frame.

`Materials::DrawSorter` exists as a separate CPU utility. Given
`MaterialDrawItem` input, it produces:

- opaque items stably ordered by PSO-key hash, material index, front-to-back
  view depth, then submission order;
- transparent items stably ordered strictly back-to-front view depth, then
  submission order.

It is not currently invoked by the live `AppRenderPipeline` indirect
submission path. Its `PSOKeyHash` is a sort key, not a cache identity:
`PSOCache` must still compare full canonical keys.

## Required production integration

1. Define a render-item representation that carries exact material/pipeline
   variant, pass classification, depth, mesh region, transform, and stable
   submission identity. It must be part of the immutable render snapshot, not
   derived by reading mutable editor/ECS state on the render thread.
2. Split opaque, alpha-tested, conventional alpha-blended, and order-independent
   transparency explicitly. Do not pass a single mixed indirect list to depth
   and G-buffer passes and claim transparent compositing is correct.
3. Use the current render-graph callback contract to declare the buffers and
   per-pass ranges. Each draw data index and indirect command must remain
   paired after culling, batching, sorting, and multi-frame buffering.
4. Choose the batching model only after profiling: CPU sorted direct/indirect
   ranges, GPU sort, indirect-count compaction, and bindless material data
   have different synchronization and validation costs.
5. Test stable ordering, depth edge cases (including NaN/reversed depth),
   material/PSO changes, frustum-culling compaction, transparent compositing,
   and frame-to-frame resource lifetime under validation layers.
