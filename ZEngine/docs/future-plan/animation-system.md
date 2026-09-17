# ZEngine — Animation System

**Priority:** P2
**Status:** Design — no animation runtime, ECS animation components, or skinning pass
currently exists in the source tree.
**Depends on:** stable imported asset records, the ECS/Actor boundary, durable scene
serialization, and a defined skinned-mesh render contract.

## Purpose

Animation must let an imported skeleton and clips drive an entity's authored local
transform pose, produce a render-safe skinning payload, and survive save/load without
serializing transient CPU/GPU handles. It is not a replacement for the transform
hierarchy, editor history, or the asset import pipeline.

The previous document described unpublished headers, pool sizes, Assimp extraction,
and shader APIs as though they existed. Those sketches are retired; they were not an
implementation contract. In particular, the current importers do not establish a
shipping skeleton/clip asset format.

## Required design decisions

Before an implementation starts, decide and record:

- The imported asset schema: skeleton topology, inverse-bind data, clip channels,
  coordinate conversion, compression, versioning, and stable asset UUID references.
- The authored component schema: clip/state-machine reference, playback controls,
  parameters, and editor-preview state. Runtime pose allocations, render instance IDs,
  and GPU buffer handles are derived data and are never scene-file identity.
- The simulation boundary: sampling time, fixed-step policy, blending, event delivery,
  root motion ownership, and deterministic behavior required by replay/networking.
- The render boundary: vertex influence format, maximum supported influences/bones,
  palette upload lifetime, shader permutations, fallback behavior, and batching limits.
- The lifecycle policy for asset reload, entity destruction, scene reload, Play/Stop,
  unsupported source data, and exhaustion of any bounded runtime pool.

Use stable scene component/field keys and codecs from
[scene serialization](scene-serialization.md); do not use process-local component
type IDs, raw pointers, or in-memory handles in source files.

## Delivery order

1. Define and test the imported skeleton/clip asset contract and its validation limits.
2. Add a minimal single-clip skeletal path with explicit local-to-world evaluation.
3. Add immutable render payloads and a validated skinning draw path.
4. Add blending, events, root motion, preview, and asset hot-reload only after their
   ownership and failure semantics are specified.

## Acceptance gates

- A clip and skeleton round-trip through the asset pipeline with version/malformed-data
  coverage.
- A saved scene reopens on a fresh process with the same authored animation state;
  runtime and GPU handles are rebuilt.
- Pose evaluation, hierarchy propagation, blend boundaries, and root-motion policy have
  deterministic unit tests.
- Entity removal, scene replacement, Play/Stop, and asset reload cannot leave a render
  payload referring to retired data.
- Reference images and validation-layer runs cover skinned mesh rendering, palette
  limits, and the declared fallback.
