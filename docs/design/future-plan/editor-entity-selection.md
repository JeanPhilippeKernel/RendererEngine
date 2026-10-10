# ZEngine — Editor Selection and Viewport Picking

**Priority:** P1 — required before inspector, hierarchy, and gizmo can be reliable
**Status:** Design, reconciled with the ECS, transaction, ZUI, and render paths
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

### 5.2 Object-ID path

Visible-object selection uses an editor-only object-ID rendering path. Reuse the
same GPU-driven indirect draw records as the scene whenever possible; do not
loop over every entity on the CPU. The ID target is a discrete integer format.

Pixel values are compact per-frame pick tokens, not EntityID indexes or UUID
bits. The main thread retains a PickFrameTable:

~~~text
pick frame token -> viewport generation, camera/projection revision,
                    scene-instance epoch, token-to-EntityID table
~~~

The graph stages the selected image pixel into a buffer and delivers it only
after the exact queue timeline has completed. The current graph readback API is
buffer-oriented, so image-to-buffer staging is required until a dedicated image
readback declaration exists.

When a result arrives, the main thread checks all of these before changing
selection:

1. the PickFrameTable still exists;
2. the scene instance, viewport generation, and camera/projection revision match;
3. the token is in range and resolves to a complete EntityID; and
4. Scene::IsAlive confirms that EntityID is still live.

Otherwise the result is discarded. The system does not stall the GPU waiting for
an answer and does not assume completion in the next frame.

### 5.3 Auxiliary raycasts

Camera-ray construction remains required for gizmo math, focus/frame selection,
and optional physics tools. Physics raycasting can be an auxiliary selection
mode, but a collider is not a prerequisite for selecting a visible mesh, light,
camera, empty, or other renderable/editor object.

## 6. Feedback and outline rendering

The outline/hover pass consumes immutable selected and hovered target data from
the render snapshot. It does not dereference mutable EditorSession or ECS scene
state on the render thread.

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
- Scene replacement, entity deletion, and Play Stop clear invalid selection
  caches and do not crash the outline or inspector.
- Render-thread outline data remains valid for the full mailbox-slot lifetime.

## 10. Delivery checklist

- [ ] EditorSession-owned SelectionModel and entity/scene target adapters
- [ ] Stable hierarchy rows and multi-selection input policy
- [ ] Immutable outline/hover snapshot data and render pass
- [ ] GPU object-ID path, pick token table, image staging, and timeline polling
- [ ] High-DPI viewport coordinate conversion and stale-result filtering
- [ ] Gizmo priority/capture integration
- [ ] Keyboard and context-menu routing
- [ ] Unit, integration, validation-layer, and reference-image tests
