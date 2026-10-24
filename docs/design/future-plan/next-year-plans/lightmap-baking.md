# ZEngine — Lightmap Baking

**Priority:** Next-year plan
**Status:** Design — no lightmap asset, UV1 contract, baker, or runtime lightmap path
is implemented.
**Depends on:** cooked scene/assets, stable static-geometry and light schemas, texture
format/cook support, and a render-material contract.

## Direction

Lightmaps are offline authored/cooked lighting data. They are neither a substitute for
the current dynamic sky/environment work nor a runtime render-graph feature that can be
added in isolation. A production design must define what is static, what changes at
runtime, how assets are invalidated, and how baked data is versioned and packaged.

The earlier xatlas/Embree choices, atlas size/format, components, shader paths, and
bake timings were proposals—not selected libraries, supported formats, or runtime APIs.

## Required decisions

- Static-light/static-geometry eligibility; direct, indirect, AO, emissive, and dynamic
  object interaction policy.
- UV/chart generation or validation, atlas/virtual-atlas strategy, padding/dilation,
  texel density, HDR encoding/compression, platform transcode, and quality tiers.
- Deterministic bake inputs: scene schema, source asset versions, settings, baker
  version, and cache key. A stale bake must be detectable and never silently trusted.
- Cook/package ownership, sandboxing/resource limits, cancellation/progress, failure
  diagnostics, and editor preview/rebake workflow.
- Material/shader integration, environment-lighting interaction, fallback behavior, and
  GPU residency budget.

## Acceptance gates

- A clean checkout cooks identical bake metadata for identical inputs and invalidates it
  when any declared input changes.
- Malformed geometry/UVs, over-budget output, missing dependencies, cancellation, and
  stale artifacts have explicit failures or fallbacks.
- Visual-reference and GPU-validation tests cover seams, chart borders, color space,
  exposure, dynamic-object interaction, and platform texture compatibility.
