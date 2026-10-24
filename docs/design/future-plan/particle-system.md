# ZEngine — Particle / VFX System

**Priority:** P3
**Status:** Design — no particle runtime, emitter component, simulation pass, or VFX
asset format is implemented.
**Depends on:** material/shader assets, ECS authoring schema, RenderGraph, and a
defined geometry/texture residency policy.

## Scope

Particles are high-volume, short-lived visual simulation. They should be authored as
assets and entity-level emitter settings, simulated through a bounded runtime pool, and
published as immutable render data. They are not ordinary ECS entities per particle,
and a render pass must not read mutable editor/ECS structures.

The prior proposed component, pool, shader, sort algorithm, and scheduler APIs were
not implemented contracts. In particular, neither CPU nor GPU simulation has been
chosen, and no capacity or draw-data layout is guaranteed.

## Design gates

- Choose CPU, GPU, or hybrid simulation based on target platforms, determinism needs,
  profiling, readback requirements, and RenderGraph synchronization—not preference.
- Define asset/emitter schema, random-seed/replay policy, space, collision behavior,
  burst/rate semantics, LOD, bounds, and GPU resource lifetime.
- Specify transparent rendering policy. Sorting, weighted alternatives, soft particles,
  depth collision, and motion vectors need compatibility with the actual render path.
- Establish bounded capacities and explicit overflow/degradation behavior for emitters,
  particles, upload data, descriptors, and draw work.
- Make editor preview, pause/scrub, undoable parameter changes, Play isolation, and
  scene serialization use the authoring/history contracts rather than special cases.

## Acceptance gates

- Deterministic tests cover seed, emission, expiry, bounds, and the documented overflow
  policy.
- Scene reload, asset reload, emitter removal, and Play/Stop reclaim runtime state only
  after in-flight render work is safe.
- GPU validation and reference images cover depth, blending, LOD, camera cuts, resize,
  and the selected simulation/render synchronization path.
