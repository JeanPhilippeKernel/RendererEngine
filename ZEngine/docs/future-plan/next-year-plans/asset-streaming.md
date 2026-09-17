# ZEngine — Asset Streaming

**Priority:** Next-year plan
**Status:** Partial foundation only — geometry residency is implemented through
`GeometryStreamingManager`; no general scene/asset streaming system exists.
**Depends on:** stable asset UUIDs, import/cook artifacts, scene authoring, texture
residency, and unified GPU memory budgets.

## Current implementation

`Rendering::GeometryStreamingManager` is driven by
`RenderResourceManager::BeginFrame`. It queues mesh geometry loads on a bounded
256-entry SPSC queue, performs pressure-driven clock-sweep eviction above 85% pool use,
and requests compaction above 30% fragmentation. RenderScene marks referenced geometry
for the next sweep. It is a geometry-pool residency mechanism—not entity distance
streaming, a general asset manager, or a guarantee that textures/materials are resident.

The previous `StreamableComponent`, `StreamingManager`, import-ticket, distance,
and reference-count APIs were unimplemented designs and are not current engine APIs.

## Required architecture

- Define streamable authored units and stable asset dependencies. A mesh cannot become
  visible with an unresolved material, texture, shader variant, or required collision
  data without a documented fallback.
- Reconcile CPU memory, geometry pools, textures, descriptors, environment lighting,
  upload staging, and temporary compaction space under one measured budget policy.
- Define visibility, priority, prefetch, cancellation, eviction, degradation, and
  overload behavior. Screen relevance and explicit game intent must be evaluated before
  distance-only heuristics are adopted.
- Publish immutable residency decisions/render inputs. Loading and eviction cannot race
  in-flight command buffers or mutable scene data.
- Treat save/load, asset reload, scene transition, and Play/Stop as first-class
  residency lifecycle events.

## Delivery order

1. Complete measured geometry-pool behavior and its compaction/fallback tests.
2. Add dependency-aware texture/material residency and unified accounting.
3. Add authored spatial/semantic streaming units and scheduler policy.
4. Add predictive/background streaming only with telemetry that demonstrates a benefit.

## Acceptance gates

- Queue saturation, allocation failure, cancellation, and compaction preserve a valid
  placeholder or prior resource and report a bounded diagnostic.
- Telemetry accounts for resident, in-flight, staging, deferred-destruction, and
  compaction memory by resource class.
- Camera movement, scene replacement, and asset reload cannot render a retired handle;
  stress/reference runs demonstrate the declared degradation behavior.
