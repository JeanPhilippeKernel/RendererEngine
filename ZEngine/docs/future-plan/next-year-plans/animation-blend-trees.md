# ZEngine — Animation Blend Trees and State Machines

**Priority:** Next-year plan
**Status:** Design — depends on a skeletal animation system that does not yet exist.
**Depends on:** animation asset/runtime and skinning contracts.

## Direction

Blend trees and state machines are authored pose-selection/blending layers above a
validated skeletal-animation base. The old node, manager, component, and evaluator
definitions were sketches; they do not establish a runtime layout, capacity, or public
API.

## Required design

- Define asset schema/versioning for states, parameters, transitions, blend spaces,
  masks, additive/reference poses, events, sync groups, and stable clip references.
- Decide evaluation clock, deterministic behavior, root-motion authority, transition
  interruption, update budget, pose-cache ownership, and error/overflow fallback.
- Make runtime state serializable only where it is product-authored. Pose buffers, GPU
  palettes, and transient evaluation handles remain derived state.
- Coordinate editor preview, authoring transactions, Play isolation, asset reload, and
  scene round-trip with the animation and scene-document contracts.

## Acceptance gates

- Test transition, blend-space, additive, interruption, event, and root-motion behavior
  against an explicit clock and reference poses.
- Validate malformed/obsolete graphs, missing clips, reload, entity deletion, and
  budget exhaustion without corrupting a render payload.
- Use visual and performance reference gates on supported hardware before exposing the
  feature as production-ready.
