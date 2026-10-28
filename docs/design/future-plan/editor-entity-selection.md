# ZEngine — Editor Selection and Viewport Picking

**Priority:** P1 — required before inspector, hierarchy, and gizmo can be reliable
**Status:** M1/M2 delivered — selection, hierarchy integration, gizmo
priority, viewport cursor routing, asynchronous object-ID picking, and
depth-visible selected/hovered feedback are live. M3 editor ergonomics and
reference-image automation remain separate follow-up work.
**Depends on:** actor-ecs-architecture.md, scene-serialization.md,
editor-undo-redo.md, rendering-flow.md, render-graph-integration.md
**Blocks:** generic scene editing and native gizmo interaction
**Tracked by:** [#298](https://github.com/JeanPhilippeKernel/RendererEngine/issues/298) and
[#672](https://github.com/JeanPhilippeKernel/RendererEngine/issues/672). #672 predates the
current ECS/ZUI design; its actor-handle and ImGuizmo details are not this contract.

## 1. Goal

Selection is editor-side state that identifies authored scene targets for the
hierarchy, inspector, outlines, gizmo, shortcuts, and undoable mutations. It
must work for actor-backed Tier 1 entities and pure ECS Tier 2 entities, and it
must survive delayed GPU picking without selecting a recycled entity.

It is not an ECS component, not game-visible state, and not serialized with a
scene.

## 2. Selection model

EditorSession owns one main-thread SelectionModel:

~~~text
Ordered entity targets       UUID canonical; EntityID is validated cache
Active entity                determines pivot/inspector focus
Hovered target               transient viewport feedback
Optional scene target        SceneTarget::Grid is the first case
Selection revision           increments whenever target membership changes
~~~

An EntityTarget contains an authored UUID and may cache an EntityID only while
the current scene-instance epoch matches. Every consumer validates target
liveness before use. ActorHandle is not a global selection identity: an actor
may provide a façade for an entity, but pure ECS entities have no actor.

Multi-selection is part of the baseline design:

- plain click replaces selection;
- modifier click toggles membership;
- range selection is a hierarchy behavior and preserves document order;
- hierarchy and viewport use the same model;
- a parent and any selected descendant reduce to selected roots for transform
  operations, preventing double transformation; and
- grid selection is mutually exclusive with entity transform selection unless a
  deliberate future mixed-target operation defines semantics.

Selection changes do not make the scene dirty and do not belong in undo history.
Commands that change scene structure may record a pre/post selection outcome for
good editor behavior, but that outcome is applied only after the document
mutation succeeds.

## 3. Main-thread ownership and phase

Selection updates, ZUI callbacks, transactions, and scene mutation occur on the
main thread. EditorSession provides an exclusive mutation phase outside WorldTick
waves. It performs this order:

~~~text
input and ZUI event collection
  -> resolve completed pick readbacks
  -> update selection / active interaction
  -> preview or commit editor transaction
  -> hierarchy and transform propagation
  -> ECS-to-render snapshot creation
  -> build ZUI payload and publish mailbox slot
~~~

WorldCommands remains for deferred mutations issued by runtime systems. A
hierarchy Delete or Duplicate initiated by the editor is an EditorSession
transaction, not a WorldCommands request.

## 4. Hierarchy selection

The hierarchy renders stable tree rows from the ECS hierarchy and UUID identity.
It must not require an ActorManager lookup. A row may display actor-specific
information when one exists, but selection remains the entity target.

Rows report click, modifier, range, hover, context-menu, keyboard focus, and
drag/drop reparent intent through SelectionModel. Structural actions create
semantic editor operations:

- Delete follows the explicit subtree or child-reparent policy.
- Duplicate snapshots the selected subtree, mints UUIDs, and patches internal
  UUID references.
- Rename validates the NameComponent string through its codec.
- Reparent validates cycles and owns any preserve-world-transform conversion.

The selection model is refreshed after a scene load, Play Stop, entity deletion,
or a target invalidation. It never retains a stale EntityID after a scene epoch
change.

## 5. Viewport picking

### 5.1 Input eligibility

The viewport accepts selection input only when it is visible, enabled, and owns
the relevant ZUI focus/capture state. A completed gizmo or marquee interaction
consumes its pointer sequence; object selection does not run underneath it.
Keyboard shortcuts are suppressed when a text widget owns keyboard input.

Cursor coordinates are transformed from window logical coordinates to the
rendered viewport's physical extent using the current framebuffer scale and
viewport placement. A click outside the image, letterboxed region, or stale
viewport generation is rejected.

For a pointer position `p`, logical image origin `o`, logical image size `s`,
and physical target extent `e`, the request pixel is:

~~~text
pixel = floor((p - o) * e / s)
~~~

The conversion uses the same top-left image-coordinate convention as the
object-ID attachment, then rejects `pixel` unless it is in `[0, e)`. It samples
the image bounds, not panel bounds or an assumed window scale, so it remains
correct with high-DPI scaling, render-scale changes, and letterboxing.

### 5.2 Precision cursor and pointer feedback

The editable viewport uses a crosshair while the pointer is over its rendered
image and no higher-priority interaction supplies a cursor. This gives scene
selection and placement a stable, precise visual target without changing the
cursor over viewport chrome, surrounding panels, text fields, or menus.

Cursor choice is owned by the interaction that owns the pointer. The UI input
layer resolves requests once per frame; tools request a semantic cursor and do
not call platform cursor APIs directly. The winning request is deterministic:

| Priority | Condition | Cursor policy |
|---|---|---|
| 1 | OS modal state or captured native UI control | The operating system or control owns the cursor. |
| 2 | Active gizmo capture | The active operation requests its cursor for the whole capture. |
| 3 | Hovered gizmo handle | The handle requests its operation cursor before a viewport request. |
| 4 | Active marquee, camera navigation, or viewport tool | The tool requests its cursor for the interaction. |
| 5 | Editable rendered image under the pointer | Crosshair. |
| 6 | Any other surface | The surface's normal cursor. |

The first implementation uses only cursor shapes the platform layer can supply
consistently. It maps semantic requests as follows:

| Interaction | Semantic cursor | Fallback |
|---|---|---|
| Idle scene selection or placement | Crosshair | Crosshair |
| Marquee selection | Crosshair | Crosshair |
| Translate/scale gizmo | Resize or move | Crosshair while preserving capture ownership |
| Rotate gizmo | Rotate | Crosshair while preserving capture ownership |
| Camera look/navigation with relative pointer input | Hidden and pointer-locked | Default arrow if pointer lock is unavailable |
| Pan/orbit without relative input | Grab while idle, grabbing while active | Default arrow |

Exact art for operation cursors is not part of selection state. The platform
layer may initially use the fallback cursor until themed cursor assets are
available, but it must preserve the precedence and restore behavior above.

Cursor state follows pointer-capture lifetime, not merely hover lifetime:

- a gizmo or navigation capture keeps its cursor when the pointer leaves the
  image, until it releases or cancels;
- a disabled, read-only, obscured, or unfocused viewport makes no viewport
  cursor request;
- pointer leave, release, cancellation, focus loss, viewport destruction, and
  viewport-generation change clear the viewport request; and
- ending relative-pointer navigation restores the physical pointer according to
  the platform input layer, then lets the surface under it choose the cursor.

The crosshair and operation cursors are presentation state only. They do not
affect pick coordinates, selection, transactions, camera state, or render
snapshots. A cursor request must never cause a synthetic pointer event or a
selection action.

### 5.3 Object-ID path

Visible-object selection uses an editor-only object-ID rendering path. Reuse the
same GPU-driven indirect draw records as the scene whenever possible; do not
loop over every entity on the CPU. The ID target is a discrete integer format.

Pixel values are compact per-frame pick tokens, not EntityID indexes or UUID
bits. The main thread retains a PickFrameTable:

~~~text
pick frame token -> viewport/render-target generation, camera/projection
                    revision, scene-instance epoch, token-to-EntityID table
~~~

The graph stages the selected image pixel into a mapped readback allocation and
delivers it only after the exact queue timeline has completed. `DeclareImageReadback`
declares the exact source image version, subresource, region, and output byte
count; it records one image-to-buffer copy through the graph's transfer-read
state and the existing timeline-gated ring. A single `R32_UINT` pick therefore
stages four bytes, never a full target image.

The object-ID attachment is an editor-only, single-sample `R32_UINT` target. It
is cleared to zero, has blending disabled, and is written by the same visible
opaque and masked geometry that contributes to the scene depth buffer. Its depth
test is the scene depth test, so the resulting token identifies the front-most
selectable surface at the requested pixel. Token zero means background or a
non-selectable draw. When MSAA is enabled, the picking path uses a distinct
single-sample target with the same depth decision; integer tokens must never be
filtered or resolved by averaging.

The renderer assigns one token to each submitted draw record for the lifetime of
a PickFrameTable and maps that token to the record's complete EntityID. Multiple
submeshes may therefore use distinct tokens while resolving to the same selected
target, and a culled entity has no token. The table is bounded by the submitted
draw-record capacity and is released only after its readback has completed or
been safely abandoned.

Initial scope is opaque and alpha-masked scene geometry plus editor proxies for
non-mesh targets. Transparent material picking needs an explicit policy before
it joins the path: use its depth-writing representation, a dedicated
depth-sorted token pass, or declare it non-selectable. It must never silently
select an object behind a transparent surface because that happened to be the
last ID written.

The input request captures its selection modifier and physical pixel at pointer
release. New input never rewrites a submitted request. Focus loss, viewport
destruction, and viewport replacement cancel the request; a late callback may
finish its fixed storage lifetime but cannot change selection. The initial
implementation holds one fixed PickFrameTable and ignores later clicks until its
timeline-gated result arrives; a future multi-request ring must use distinct
immutable callback contexts and preserve the same stale-result rules. This keeps
memory bounded and selection non-blocking.

When a result arrives, the main thread checks all of these before changing
selection:

1. the PickFrameTable still exists;
2. the scene instance, viewport/render-target generation, and camera/projection
   revision match;
3. the token is in range and resolves to a complete EntityID; and
4. Scene::IsAlive confirms that EntityID is still live.

Otherwise the result is discarded. The system does not stall the GPU waiting for
an answer and does not assume completion in the next frame.

### 5.4 Auxiliary raycasts

Camera-ray construction remains required for gizmo math, focus/frame selection,
and optional physics tools. Physics raycasting can be an auxiliary selection
mode, but a collider is not a prerequisite for selecting a visible mesh, light,
camera, empty, or other renderable/editor object.

## 6. Feedback and outline rendering

The outline/hover pass consumes immutable selected and hovered target data from
the render snapshot. It does not dereference mutable EditorSession or ECS scene
state on the render thread.

The first outline implementation reuses the visible `R32_UINT` object-ID image
already produced by the G-buffer. Alongside the draw records, the main thread
publishes a fixed token-state table for the same mailbox slot:

~~~text
token state: 0 = none, 1 = hovered, 2 = selected, 3 = selected and hovered
~~~

The full-screen pass fetches the centre and neighbouring integer tokens with
`texelFetch`, resolves each through that immutable table, and emits colour only
where the centre and a connected neighbour differ in visible state. Selection
wins over hover for the edge's colour. The initial pass uses the four cardinal
neighbours and a hard one-physical-pixel edge. That keeps the first release
predictable at every render scale. A soft outer halo or diagonal-neighbour
coverage is a visual-polish follow-up and must be added with reference-image
coverage rather than changing line width implicitly.

This avoids a second mesh pass, per-selected-object command lists, and an
additional full-resolution selection-mask attachment. It also prevents internal
submesh seams: tokens that resolve to the same state produce no edge. The
object-ID image already contains only the depth-visible fragment, so the outline
is naturally occlusion-correct. X-ray selection remains an explicit later mode
with its own depth policy; it must not be created by sampling a hidden ID.

The token-state table is bounded by `SceneData::MAX_DRAW_COMMANDS`, zero-filled
when the mailbox slot is prepared, and retained by that slot until rendering
finishes. The render pass cannot consult live selection state or cache token
tables across frames: indirect-draw token assignment is frame-local. The first
implementation uses one zero-initialized `uint32_t` per draw token (states
`0..3`); it may pack that storage after profiling proves it worthwhile. It adds
no additional render target. The existing object-ID image remains a four-byte
integer attachment; its later allocation policy may make it transient on frames
with neither an object pick nor feedback, but that optimization must retain a
pipeline-compatible no-ID path and is not a reason to introduce a second mask.

Hover arrives through a rate-limited, cancellation-aware object-ID request that
uses the same coordinate conversion and stale-result checks as click selection.
It never changes selection membership, the active target, or opens a
transaction. While a gizmo handle, marquee, navigation capture, popup, or text
control owns the pointer, hover is cleared and no hover readback is queued. The
initial click readback and hover readback use separate immutable request
contexts, so a late hover result cannot resolve a click table.

Outline eligibility is an adapter decision:

- renderable entities use their resolved draw records;
- non-mesh targets such as lights/cameras use a dedicated icon/bounds overlay;
- pure ECS entities may opt into an editor proxy; and
- SceneTarget::Grid uses its explicit origin/axis marker, never the infinite plane.

Selected and hovered feedback use separate color/style policy and must respect
scene depth. A selected object hidden behind another object is not outlined
through the occluder unless an explicit x-ray editor mode is active.

The pass follows the current render-graph callback lifecycle. Its resources and
attachment compatibility are graph declared; it must not create ad hoc
per-frame Vulkan targets or use retired Setup/Compile callback examples.

### 6.1 Initial delivery sequence

The initial work is delivered in three independently reviewable milestones. A
later milestone cannot weaken the stale-result, capture, or mailbox-lifetime
requirements established by an earlier one.

| Milestone | Scope | Acceptance criteria |
|---|---|---|
| M1 — precise click selection | Crosshair ownership, physical-pixel conversion, `R32_UINT` object IDs, a single immutable click table, and timeline-gated readback. | A click on front-most opaque or masked geometry selects exactly the live target; a background click clears it; stale, resized, replaced, or recycled results are discarded; gizmo capture prevents selection. |
| M2 — persistent feedback | Immutable selected snapshot, one-pixel depth-correct selected outline, and cancellation-aware hover readback with a separate immutable table. | The selected object has no internal submesh seams; an occluded object is not outlined; moving the pointer updates hover without changing selection; hover clears while another interaction owns the pointer; delayed hover can never resolve a click request. |
| M3 — editor ergonomics | Operation cursors, marquee, hierarchy range behavior, shortcuts, context menus, and reference-image coverage. | Cursor precedence restores across all capture and cancellation paths; keyboard and context commands obey text and modal ownership; visual captures cover selected, hovered, and occluded mesh and proxy targets. |

M1 and M2 are the first implementation target. M3 remains deliberately
separate: themed cursor art and alternate outline styles are usability polish,
not a reason to compromise M1/M2 correctness.

## 7. Interaction with gizmo and transactions

The gizmo queries the active selection target after handle-picking has priority.
On pointer-down over a valid handle it asks EditorSession to begin a transaction
and captures the pointer. It owns the sequence until release or cancellation.

During a drag:

- hover object picks cannot replace selection;
- each preview starts from the transaction's captured base transforms, avoiding
  cumulative numeric drift;
- derived WorldTransform is never written; and
- an invalidated target cancels and restores the transaction.

On release, the gizmo commits one transform-batch or scene-setting operation if
the semantic state changed. Escape, lost focus, capture loss, and mode transition
restore the before-state and add no history entry.

## 8. Keyboard behavior

| Binding | Edit-mode behavior |
|---|---|
| Escape | Cancel active transaction; otherwise clear selection. |
| Delete / Backspace | Create a semantic delete transaction after confirmation policy. |
| Duplicate binding | Create one duplicate-subtree transaction. |
| F | Frame the camera on the selected target bounds or proxy. |
| T / R / S | Choose transform gizmo operation when the viewport owns shortcuts. |
| Local/world binding | Change gizmo space; it is editor-local state. |
| Undo / Redo | Delegate to EditorSession when no text field or interaction owns the shortcut. |

Platform shortcut conventions are defined by the UI input layer, not assumed from
an ImGui integration.

## 9. Required tests

- Hierarchy and viewport selection produce identical ordered entity targets.
- A mesh, light, camera, empty/group, actor-backed entity, and pure ECS entity
  can all be selected.
- Modifier/range selection and selected-root filtering have deterministic order.
- High-DPI scaling, letterboxing, viewport resize, and camera revision produce
  correct or safely rejected pick coordinates.
- Completed, delayed, stale, recycled, and out-of-range pick tokens behave safely.
- Object picks never depend on physics collider presence.
- Gizmo pointer capture prevents accidental selection replacement.
- Cursor precedence is deterministic for gizmo hover/capture, marquee, camera
  navigation, and the editable image; it restores correctly after leave,
  release, cancellation, focus loss, and viewport replacement.
- Relative-pointer navigation hides/locks and restores the cursor without
  emitting a synthetic click or selection.
- Scene replacement, entity deletion, and Play Stop clear invalid selection
  caches and do not crash the outline or inspector.
- Render-thread outline data remains valid for the full mailbox-slot lifetime.
- Outline edges are stable for selected submeshes of one entity, occlusion
  boundaries, high-DPI scaling, render-target resize, and selection changes
  while a frame is retained.

## 10. Delivery checklist

- [x] EditorSession-owned SelectionModel and entity/scene target adapters
- [x] Stable hierarchy rows and multi-selection input policy
- [x] Immutable outline/hover snapshot data and render pass
- [x] GPU object-ID path, pick token table, image staging, and timeline polling
- [x] High-DPI viewport coordinate conversion and stale-result filtering
- [x] Viewport precision crosshair, cursor ownership, and platform fallbacks
- [ ] Operation-specific gizmo and navigation cursor artwork
- [x] Gizmo priority/capture integration
- [ ] Keyboard and context-menu routing
- [x] Unit and integration coverage for input eligibility, coordinate mapping,
  callback ordering, malformed results, and stale click rejection
- [ ] Validation-layer and reference-image coverage, including outline/hover
  rendering
