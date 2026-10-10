# ZEngine — Physics System

**Priority:** P1 for collision-based games
**Status:** Foundation only — `RigidBodyComponent` is present as an authored
descriptor/reflection entry, but there is no `PhysicsWorld`, collider component,
physics backend, or simulation system in the source tree.
**Depends on:** ECS lifecycle, hierarchy/transform policy, fixed-step game loop, and
scene serialization.

## Current contract

`ECS::Components::RigidBodyComponent` currently contains `MotionKind`, `Mass`,
`Friction`, `Restitution`, and an internal `BodyID`. `BodyID` is read-only in
reflection and uses `UINT32_MAX` to mean inactive. It is a runtime binding, not
persisted object identity. No document may describe it as an implemented Jolt body.

The earlier document invented collider, character-controller, and `PhysicsWorld` APIs,
then treated Jolt as a chosen dependency. Those interfaces and that choice remain open.

## Required architecture

- Select a physics backend only after validating license, supported platforms, required
  determinism, debugger/tooling, allocator and job integration, and upgrade policy.
- Define a fixed simulation clock, maximum catch-up work, interpolation contract, and
  the owner of transform authority for static, kinematic, and dynamic bodies.
- Keep backend types and lifetimes inside the physics integration. ECS and serialized
  data use engine schemas/handles, never backend pointers or raw backend IDs.
- Stage ECS-to-physics mutations and physics-to-ECS results at documented frame
  boundaries. Concurrent direct writes to `TransformComponent::WorldTransform` are
  forbidden; world transforms remain hierarchy-derived.
- Design collider/cooked-shape assets, filtering, query APIs, contact events, rollback
  requirements, and failure behavior before exposing gameplay APIs.

## Delivery order

1. Decide backend and publish a minimal supported feature/capability matrix.
2. Add static/kinematic/dynamic body binding and a fixed-step boundary with tests.
3. Add primitive colliders, queries, and contact reporting with a stable event lifetime.
4. Add character control, mesh/convex cooking, joints, and any determinism/replication
   features only with their dedicated validation plans.

## Acceptance gates

- Scene load/save/Play/Stop/entity removal reconstructs or retires bodies without stale
  `BodyID` bindings.
- Parent transforms, interpolation, kinematic targets, and dynamic write-back follow a
  single documented authority model.
- Collision/filtering/query results, fixed-step catch-up, and contact lifetime have
  deterministic tests where the selected backend can support them.
- Backend errors, shape-cook failure, queue overflow, and shutdown are contained and
  observable.
