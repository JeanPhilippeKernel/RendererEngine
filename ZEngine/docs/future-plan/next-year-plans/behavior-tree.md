# ZEngine — Behavior Trees

**Priority:** Next-year plan
**Status:** Design — no behavior-tree runtime, asset schema, ECS component, or visual
authoring tool is implemented.
**Depends on:** game-logic extension boundary, animation/physics contracts where used,
and durable scene/asset serialization.

## Direction

Behavior trees are one possible AI authoring model. The old node layout, blackboard,
callback table, scheduler registrations, Lua comparison, and performance figures were
proposals—not existing engine APIs or measurements.

## Required design

- Define whether trees are authored assets, code-defined graphs, or both; establish
  stable schema/version/migration and reference rules.
- Specify node semantics, evaluation budget, deterministic/random behavior, tick rate,
  cancellation/abort rules, debug tracing, and failure handling.
- Keep per-agent mutable state separate from shared definition data and give both clear
  scene/asset/reload lifetime rules.
- Expose gameplay/physics/animation interaction through stable interfaces, not direct
  assumptions about unimplemented subsystem types.
- Define a blackboard type system and bounded memory policy before publishing a
  designer-facing format.

## Acceptance gates

- Deterministic tests cover composites, decorators, aborts, long-running actions,
  invalid graphs, budget exhaustion, and scene/asset reload.
- Visual debugging and profiling demonstrate the documented update cost and lifecycle
  behavior for the target game workload.
