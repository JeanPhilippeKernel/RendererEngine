# ZEngine — Mesh LOD System

**Priority:** Next-year plan
**Status:** Design — no mesh-LOD asset schema, selection system, or cross-fade path is
implemented.
**Depends on:** imported/cooked mesh metadata, geometry streaming, material variants,
culling, and GPU memory budgets.

## Scope

Mesh LOD is a resource-selection policy: choose an available representation for a
visible object based on an explicit error metric, then make sure that representation is
resident and renderable. It is distinct from the grid pass's local line-density
parameter and from generic distance culling.

The previous component, thresholds, import APIs, and renderer hooks were sketches. They
must not be treated as current source contracts.

## Required design

- Define source/cooked representation: authored LODs, generated LODs, meshlets, bounds,
  error metrics, material compatibility, stable asset identity, and versioning.
- Define selection metric (screen-space error or equivalent), hysteresis, per-view
  behavior, transition/cross-fade policy, camera cuts, occlusion interaction, and
  fallback when a preferred representation is not resident.
- Couple the selection with geometry and texture streaming so a high-detail request
  cannot violate resource budgets or cause visible use-after-free.
- Specify authoring/inspector validation, deterministic cook output, telemetry, and
  interaction with shadows, picking, collision, and baked lighting.

## Acceptance gates

- Selection is stable around thresholds and correct across viewport resize, FOV changes,
  camera cuts, multiple views, and missing representations.
- Cook/load validates bounds and representation compatibility; scene/asset reload
  reconstructs residency safely.
- Reference images and profiling demonstrate no popping beyond the declared transition
  policy and remain within measured frame/memory budgets.
