# ZEngine — Native 3D Gizmo

**Priority:** P1 — required for production scene editing
**Status:** Completed — M0 through M2 native gizmo delivery merged in [#864](https://github.com/JeanPhilippeKernel/RendererEngine/pull/864)
**Depends on:** editor-entity-selection.md, editor-undo-redo.md,
scene-serialization.md, editor-grid.md, rendering-flow.md, render-graph-integration.md
**Blocks:** complete native viewport editing and retirement of inactive ImGuizmo integration
**Tracked by:** [#788](https://github.com/JeanPhilippeKernel/RendererEngine/issues/788).
That issue's actor-only wording is superseded by this document's all-ECS-target contract.

!!! success "Completion scope"
    M0 through M2 are complete: shared selection/history foundations, single- and multi-entity
    transforms, typed light/camera controls, arbitrary-axis Grid translation/rotation, the CPU
    handle path, immutable render-frame data, and the Gizmo Pass. Object-ID picking and
    depth-aware selection outlines remain separate work in `editor-entity-selection.md`.

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

### 1.1 Approved delivery milestones

The owner approved the following sequence on 2026-10-19. This replaces the implementation guide's
earlier actor-only scope and its deferral of multi-selection and shared undo support.

| Milestone | Required delivery | Guide packages |
|---|---|---|
| M0 — Shared foundations | `EditorSession` mutation/snapshot boundary, UUID/epoch target resolution, shared `SelectionModel`, bounded transaction history and real undo/redo | WP0, WP0a–WP0c |
| M1 — First usable gizmo | Single-entity translate/rotate/scale, pivot, snapping, toolbar, light/camera component gizmos; actor-backed and pure ECS targets share the same operation path | WP1–WP10 |
| M2 — Complete approved gizmo scope | Multi-selection with selected-root filtering and atomic transform batches; `SceneTarget::Grid` translate/rotate and shared scene-setting history | WP11–WP12 |

M0 must be in place before any production authored drag is enabled. M1 is an intermediate milestone,
not completion of this plan. A changed authored drag produces one undoable operation; cancellation,
no-op drags and editor-local pivot changes produce none. A gizmo-private undo stack is prohibited.

Viewport object picking, object outlines, Universal mode, surface snapping, collision/spline/brush
tools and new specialized editors remain separate deliveries. CPU handle hit testing is required;
GPU object picking is not a gizmo prerequisite. Selection must be available through the hierarchy or
another explicit editor action. Delivering the required shared foundation subset does not complete
the broader selection, serialization, render-snapshot or undo plans.

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
pivot                 active entity by default, or an editor-local custom point
snap settings         translate/rotate/scale increments and enable state
hovered handle        transient result of handle hit testing
active handle         fixed for an active interaction
pointer capture       ZUI viewport capture token
transaction token     EditorSession transaction identity
~~~

Operation, pivot, space, target roots, snap settings, camera inputs, and before
values are captured at pointer-down. They do not change under a drag.

Median/bounds-center pivot strategies remain extension points, not permission to invent new
selectors. Entity and Grid selection are mutually exclusive. Active-target identity is independent
of selected-root pruning, including when the active entity is a selected descendant.

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

Escape, focus/capture loss, target/hierarchy invalidation, selection replacement, scene load, or mode change
  -> restore before-state, cancel transaction, release capture
~~~

The system never emits one command per mouse move and never uses a time-window
heuristic to decide the bounds of a drag.

Validate the complete preview before applying any target. A rejected candidate leaves the last
valid preview intact, never a partially applied batch. Requested deletion, reparenting or scene
replacement cancels the active interaction before the external mutation; an unexpectedly missing
target is never resolved to a recycled entity or another scene. Restore still-valid original targets
without dereferencing invalid handles. Pivot-only dragging is editor-local and bypasses document
history while preserving capture and exact cancellation of the pivot state.

## 4. Transform rules

Entity TransformComponent values are local authored values. WorldTransform is
derived by HierarchySystem and is never written by gizmo code.

For multiple selected entities, first remove selected descendants of a selected
ancestor. Apply the requested world or local delta to the selected roots, then
derive each local matrix relative to its non-selected parent. Decompose according
to the engine's defined TRS policy; reject or explicitly warn on non-decomposable
shear rather than silently corrupting it.

Use the approved GZ rules and the guide's named thresholds: preserve negative scale signs, clamp
scale ratios to 0.001, draw scale cubes below an absolute component of 1e-4 at 40 % alpha without hit testing, and reject parent-local
TRS round-trip error above 1e-3, non-finite results or singular parent transforms. Single-entity
scale uses local axes regardless of the World/Local toggle. A custom-pivot position change still
requires parent-local conversion. Camera pointer and keyboard navigation are suppressed while
dragging; Q/W/E/R are mode keys only outside text editing and RMB fly navigation.

Mixed entity kinds with supported editable transforms are allowed. Capture one pivot/axis frame
from the active entity (or custom pivot) for a batch, and apply one shared interaction delta to all
selected roots, preserving spacing under snapping. Group local-axis scale uses the captured active
frame and must pass reconstruction for every root; it is not independent per-object screen motion.
Cache selected roots by selection/hierarchy revision using membership lookup and a hierarchy walk,
O(S + V) for S selected targets and V visited nodes, not pairwise ancestor scans.

Grid translation and rotation operate in its local U/V/N frame. Grid scale is
not offered. `editor-grid.md` owns the typed origin/orientation, codec and render-snapshot contract;
M2 must render the edited plane correctly, not merely move handles above a fixed XZ grid. This
required subset does not imply completion of every grid-rendering feature.

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
derive arbitrary world translation/rotation from screen-pixel deltas. The GZ5 rotate tangent fallback
and GZ10 uniform-scale screen mapping are explicit exceptions: use the camera/axis/pivot captured at
pointer-down and derive each preview from the immutable before-state.

## 6. Handle geometry and draw contract

The initial implementation uses fixed-capacity world-point lines, filled triangles and screen-aligned
sprites, expanded to pixel-width geometry by the shaders. It requires no static handle meshes.
Pipelines and per-frame-in-flight upload storage follow normal renderer/resource-manager lifetime.
The editor builds `GizmoFrameData` in `PrepareScene` with exactly the camera published in that
`RenderFrameState`; render code reads only this copied immutable input, never editor/ECS pointers.

The pass loads display-encoded `FrameColor` after tone mapping and the Grid Pass, with straight-alpha
blending and no depth read/test/write, including component gizmos. It declares exact graph versions;
registration order alone is not synchronization. Clip lines/triangles against the near plane before
emission. Colors, logical-pixel sizes, analytic AA/dashes and draw order follow the Figma guide.

Default-initialize control state, counts and handles, fully initialize emitted records, and copy only
used prefixes. No per-frame heap allocation or full-capacity clear/copy. On capacity pressure, omit
lower-priority decorations deterministically, not edited targets or visible interactive handles.

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

This section defines interoperability with the separately delivered object-picking plan. The approved
gizmo milestones use CPU projected-handle tests through ZUI's custom hit hook; neither a GPU handle-ID
pass nor asynchronous object readback is required for M0–M2. Do not treat object-picking completion as
a condition for gizmo completion or report object picking finished when the gizmo ships.

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
the axis/plane, angle, or scale ratio. One world unit is one metre; translation pills use raw values
with three decimals and no suffix, rotation uses ° and scale uses a dimensionless ratio. They are
discarded when the transaction ends. Their layout must remain inside the viewport and not
intercept pointer input unless deliberately interactive.

## 9. Frame placement

Main thread:

~~~text
collect ZUI intents/current input -> resolve selection (completed picks when available)
  -> update/cancel/commit gizmo transaction at exclusive editor mutation point
  -> hierarchy and render synchronization -> publish immutable render snapshot
~~~

Render thread:

~~~text
scene rendering -> post-tone-map editor overlays (outline, grid, gizmo)
  -> final ZUI composition -> present
~~~

The exact graph declares dependencies and may schedule compatible work; this
diagram does not authorize callers to assume a fixed raw Vulkan order.

Active interaction, release and cancellation processing run every main-thread update, even when no
ZUI overlay mailbox slot is free. The slot-gated UI build registers hit shapes/intents and draws
measurement snapshots; it cannot own authored writes. Route hover capture and active pointer/
keyboard capture before camera navigation. Validate cached hit shapes against scene, target,
viewport and camera revisions before allowing a new drag. The M0 mutation/snapshot boundary must
also make the existing borrowed `RenderScene` handoff safe; copied gizmo data alone is insufficient.

## 10. Required tests and visual gates

- Axis, plane, free, rotate, and scale math under perspective and orthographic cameras.
- Near-parallel rays, viewport exit, resize, high-DPI scaling, focus loss, and Escape.
- Single entities; selected parent/child; multiple roots; actor-backed and pure ECS targets.
- Parent-local reconstruction and no writes to WorldTransform.
- Grid U/V/N translation/rotation, snapping, and scale rejection.
- One operation per changed authored drag; none for no-op, cancellation or editor-local pivot edits;
  exact undo/redo and dirty/save-checkpoint behavior; bounded history/scratch memory.
- Atomic rejection of invalid multi-root previews; selected-root caching and fixed-buffer overflow.
- Release, focus/capture loss and cancellation work while UI overlay publishing is backpressured.
- Stale CPU hit shapes cannot start a drag. When object picking is integrated separately, delayed/
  stale readbacks cannot alter selection or a live drag.
- Validation-layer-clean draw path and reference images for handles, the approved no-depth-test
  visibility, color states, extreme zoom and all supported platforms. Depth-tested outlines remain
  a separate selection-plan feature, not a reason to depth-test the gizmo.

## 11. Delivery checklist

- [x] M0: shared session mutation/snapshot boundary, UUID/epoch resolver and SelectionModel
- [x] M0: shared bounded history, transaction lifecycle, save checkpoint and working undo/redo
- [x] M1: generic single-entity editing, including actor-backed and pure ECS targets
- [x] M1: transform, pivot, snap, toolbar and typed light/camera field interactions
- [x] M2: hierarchy multi-selection, selected-root cache and atomic TransformBatch operations
- [x] M2: SceneTarget::Grid adapter, U/V/N translate/rotate, scale rejection and scene-setting history
- [x] ZUI pointer capture and viewport input routing
- [x] Ray/constraint interaction library with unit tests
- [x] Fixed-capacity primitives, shaders/pipeline and immutable frame data with used-prefix copies
- [x] CPU handle tests with deterministic priority; object-picking integration kept separate
- [x] Measurement overlays and accessible visual states
- [x] Render-graph registration and validation-layer/reference-image tests
