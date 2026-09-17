# ZEngine — Native 3D Gizmo

**Priority:** P1 — required for production scene editing
**Status:** Design, reconciled with ZUI, global ECS selection, transactions, and the current render graph
**Depends on:** editor-entity-selection.md, editor-undo-redo.md,
scene-serialization.md, rendering-flow.md, render-graph-integration.md
**Blocks:** complete native viewport editing and retirement of inactive ImGuizmo integration
**Tracked by:** [#788](https://github.com/JeanPhilippeKernel/RendererEngine/issues/788).
That issue's actor-only wording is superseded by this document's all-ECS-target contract.

## 1. Goal

The native gizmo manipulates every transformable scene object, not only meshes or
actor-backed objects. It is an EditorSession interaction adapter with four
separate concerns:

1. target and operation state;
2. handle geometry and rendering;
3. robust world-space interaction math; and
4. ZUI input, pointer capture, and transaction integration.

It does not own scene persistence, selection storage, history, ECS world
transforms, or Vulkan resource lifetime.

## 2. Targets and state

The active target comes from SelectionModel:

- EntityTarget supports any ECS entity with a supported editable transform:
  mesh, light, camera, empty/group, and pure ECS entity.
- SceneTarget::Grid adapts SceneGridSettings and supports translate and rotate
  only.

Gizmo state is editor-local and non-serialized:

~~~text
operation             translate, rotate, scale
space                 world or local
pivot                 active object, median, bounds center, or defined custom point
snap settings         translate/rotate/scale increments and enable state
hovered handle        transient result of handle hit testing
active handle         fixed for an active interaction
pointer capture       ZUI viewport capture token
transaction token     EditorSession transaction identity
~~~

Operation, pivot, space, target roots, snap settings, camera inputs, and before
values are captured at pointer-down. They do not change under a drag.

## 3. Interaction lifecycle

~~~text
pointer-down on valid handle
  -> validate active targets
  -> derive selected transform roots
  -> capture pointer and BeginTransaction
  -> capture before local TRS or grid setting

pointer move while captured
  -> calculate delta from captured ray/plane/ring reference
  -> constrain and snap in the captured coordinate system
  -> derive preview from immutable before-state
  -> convert each entity result to parent-local TRS
  -> run hierarchy/render snapshot update

pointer-up
  -> commit one TransformBatch or SceneSetting operation if changed

Escape, focus/capture loss, target invalidation, scene load, or mode change
  -> restore before-state, cancel transaction, release capture
~~~

The system never emits one command per mouse move and never uses a time-window
heuristic to decide the bounds of a drag.

## 4. Transform rules

Entity TransformComponent values are local authored values. WorldTransform is
derived by HierarchySystem and is never written by gizmo code.

For multiple selected entities, first remove selected descendants of a selected
ancestor. Apply the requested world or local delta to the selected roots, then
derive each local matrix relative to its non-selected parent. Decompose according
to the engine's defined TRS policy; reject or explicitly warn on non-decomposable
shear rather than silently corrupting it.

The following policies are required before implementation:

- pivot behavior for translate, rotate, and scale;
- which parent/child and mixed-type selections are allowed;
- zero, near-zero, and negative scale behavior;
- local axes under a rotated/scaled parent;
- numerical epsilon and snapping policy; and
- camera movement/focus behavior while a handle owns pointer capture.

Grid translation and rotation operate in its local U/V/N frame. Grid scale is
not offered.

## 5. Ray-based manipulation math

The interaction layer uses unprojected cursor rays and captured reference
geometry:

| Operation | Reference geometry | Delta |
|---|---|---|
| Axis translate | Closest valid point between cursor ray and axis line | Scalar displacement along the axis |
| Plane translate | Ray/constraint-plane intersection | U/V displacement on the plane |
| Free translate | Camera-facing constraint plane | World-plane displacement |
| Axis rotate | Ray/rotation-plane intersection | Signed angle around the selected axis |
| Free rotate | Trackball or camera-facing ring with documented behavior | Stable signed rotation |
| Axis/uniform scale | Captured axis/radial distance with epsilon | Ratio relative to initial distance |

If a ray is parallel to a constraint, the system uses a deterministic fallback
plane chosen at drag start or leaves the previous preview intact. It does not
derive a world transform from a screen-pixel delta.

## 6. Handle geometry and draw contract

Static handle meshes and pipelines are owned by the renderer/resource manager
with the normal persistent-resource lifetime. Per-frame target, color, scale,
and camera data are drawn from the immutable render snapshot.

Handle size is camera-relative and clamped in screen-space to remain usable
across orthographic and perspective views. It supports axis arrows, plane
handles, rotate rings, scale handles, a center/free handle, hover/active
feedback, and accessibility-safe color differentiation.

Callback passes follow the current RenderGraph contract:

- Register declares this frame's resource reads/writes and may omit the pass.
- Prepare refreshes frame-local bindings after graph compilation.
- Execute records non-graph-managed work where necessary.
- RecordDraw records inside graph-managed dynamic rendering where supported.

The pass must not use retired Setup/Compile callbacks, create ad hoc per-frame
Vulkan attachments, or access mutable editor/ECS state from the render thread.

## 7. Picking

Object selection and handle selection have distinct namespaces. Object pixels use
per-frame compact tokens that map to complete EntityIDs in a retained
PickFrameTable. Handle hit tests return a compact handle identity tied to the
active operation and gizmo revision.

GPU readback is asynchronous and timeline-gated. It is not guaranteed to arrive
next frame. Until image readback is a native graph facility, copy the requested
pixel to a declared buffer readback. Discard a result if its scene epoch,
viewport generation, camera/projection revision, target revision, or timeline
completion is invalid.

Handle priority is deterministic: an active captured handle wins; otherwise a
hovered handle wins before an object pick. While captured, object picks cannot
replace the active selection.

## 8. ZUI bridge and overlays

ZUI supplies viewport bounds, physical/logical scale conversion, keyboard focus,
and explicit pointer capture. The bridge owns no transform logic; it only routes
events to the interaction layer and paints editor-local measurement overlays.

Measurements are rendered from a snapshot of the active transaction and include
the appropriate unit, axis/plane, angle, or scale ratio. They are discarded when
the transaction ends. Their layout must remain inside the viewport and not
intercept pointer input unless deliberately interactive.

## 9. Frame placement

Main thread:

~~~text
collect ZUI input -> resolve picking -> update/cancel/commit gizmo transaction
  -> hierarchy and render synchronization -> publish immutable render snapshot
~~~

Render thread:

~~~text
scene rendering -> post-tone-map editor overlays (outline, grid, gizmo)
  -> final ZUI composition -> present
~~~

The exact graph declares dependencies and may schedule compatible work; this
diagram does not authorize callers to assume a fixed raw Vulkan order.

## 10. Required tests and visual gates

- Axis, plane, free, rotate, and scale math under perspective and orthographic cameras.
- Near-parallel rays, viewport exit, resize, high-DPI scaling, focus loss, and Escape.
- Single entities; selected parent/child; multiple roots; actor-backed and pure ECS targets.
- Parent-local reconstruction and no writes to WorldTransform.
- Grid U/V/N translation/rotation, snapping, and scale rejection.
- One command for a completed drag; no command for a cancelled drag.
- Delayed/stale object or handle pick results cannot alter selection or a live drag.
- Validation-layer-clean draw path and reference images for handles, depth occlusion,
  color states, extreme zoom, and all supported platforms.

## 11. Delivery checklist

- [ ] Generic entity and SceneTarget::Grid adapters
- [ ] ZUI pointer capture and viewport input routing
- [ ] Transaction-lifetime bridge and transform-batch operation
- [ ] Ray/constraint interaction library with unit tests
- [ ] Static handle geometry, pipeline, and immutable render data
- [ ] Separate object/handle picking namespaces and staged readback
- [ ] Measurement overlays and accessible visual states
- [ ] Render-graph registration and validation-layer/reference-image tests
