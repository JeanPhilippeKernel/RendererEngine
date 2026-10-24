# ZEngine — Editor Undo / Redo

**Priority:** P1 — required for reliable scene authoring
**Status:** Design, reconciled with the persistent-scene and gizmo contracts
**Depends on:** actor-ecs-architecture.md, component-reflection.md,
scene-serialization.md, editor-entity-selection.md, editor-play-mode.md
**Blocks:** production editor mutation, reliable inspector editing, native gizmo

## 1. Purpose and ownership

Undo/redo is an EditorSession service. It is the sole route for authored mutation
from the inspector, hierarchy, shortcuts, clipboard, gizmo, and scene-settings
panels. It operates only at the exclusive main-thread editor mutation point, not
inside a concurrently running ECS system.

WorldCommands remains the deferred structural-mutation mechanism used by runtime
systems. It is not a history log and must not be replayed for editor undo.

The service owns:

- the active interaction transaction;
- a bounded undo/redo history;
- the saved checkpoint and document dirty calculation; and
- user-facing operation names and invalidation messages.

Selection, hover, active gizmo handle, pointer capture, and panel layout are
editor-local state. They are not authored mutations and do not make a scene dirty.

## 2. Identity and target rules

EntityID is a generational runtime handle. It must not be the only identity held
by an operation, because delete/restore, scene reload, and Play restoration can
produce a new handle.

An entity operation records its authored UUID as the canonical target. A resolved
EntityID may be cached together with the current scene-instance epoch, but every
apply or revert validates that cache through Scene::IsAlive and re-resolves from
UUID when required. A command is never serialized to a scene file.

Scene-level settings use a tagged SceneTarget rather than pretending the setting
is an entity. SceneTarget::Grid is the first required setting target.

All targets are validated before an operation makes a mutation. If resolution,
schema validation, or an operation prerequisite fails, the operation reports one
actionable failure and leaves the scene unchanged.

## 3. Data-oriented operation interface

An operation is payload data and static function pointers. It requires no virtual
dispatch or std::function. New or changed engine-facing string APIs use cstring:
operation labels, UndoName, RedoName, and compound-operation labels do not expose
raw const char pointers.

The service exposes two application paths:

1. ExecuteAndPush validates, applies a new discrete operation exactly once, and
   appends it to history.
2. PushAlreadyApplied appends an already-previewed transaction without applying it
   a second time.

Undo validates and applies the complete before-state, moving the entry to redo.
Redo validates and applies the complete after-state, moving it back to undo. A
new committed operation clears the redo branch.

Each operation has an Apply, Revert, Validate, Destroy, and Name function. Apply
and Revert perform the matching derived-state rebuild and notify EditorSession to
publish a new immutable render snapshot. Validate prevents partial application.

## 4. Interaction transactions

An interaction lifecycle, not a time heuristic, defines one undo entry:

~~~text
activation
  -> BeginTransaction and capture before-state once
  -> apply live previews from that immutable base state
  -> deactivation or pointer release: commit one changed transaction
  -> Escape, lost capture, target loss, or mode switch: restore before-state and discard
~~~

The transaction remains active while ZUI pointer capture is held, even if the
cursor leaves the viewport or handle. Before Undo, Redo, scene load, selection
replacement, hot reload, or a mode transition, EditorSession cancels the active
transaction or explicitly completes it according to the initiating action.

Inspector controls follow the same lifecycle: capture the old value on field
activation, preview while edited, and commit at field deactivation. A text field
with keyboard focus suppresses global shortcuts that would alter its contents.

A completed transaction is already one operation. A 200 ms merge window is not
used for gizmo drags or slider scrubs. Optional merge support is reserved for
discrete repeat actions that declare a stable operation kind and exactly matching
target set.

## 5. Semantic operation payloads

Payloads use schema-aware codecs shared with scene serialization. Never snapshot
arbitrary component memory, field offsets, process-local component IDs, pointers,
GPU handles, render instance IDs, or raw ParentComponent runtime IDs.

| Operation | Required behavior |
|---|---|
| Transform batch | Stores target UUIDs, before/after local TRS, pivot and space policy, and the selected-root set. World interaction is converted to every target's parent-local space. |
| Inspector field | Stores stable component and field schema keys and codec-produced before/after values. There is no fixed 64-byte field limit. |
| Create / duplicate | Stores a semantic entity or subtree snapshot. Duplication mints UUIDs and patches internal UUID references. |
| Delete | Stores the complete affected hierarchy/reference policy, semantic snapshot, UUIDs, and selection result. It explicitly chooses subtree deletion or child reparenting. |
| Add / remove component | Uses the registered component codec and validates required-component and side-effect rules. |
| Rename | Uses the NameComponent codec and one UTF-8 validation policy, rather than a fixed C buffer. |
| Reparent | Stores child, old parent, and new parent UUIDs; validates cycles; and includes local-transform conversion when preserving world transform. |
| Scene setting | Stores typed before/after data for a setting target such as SceneTarget::Grid. Grid translate/rotate are valid; grid scale is rejected. |

An actor is never the command target. Actor-backed entities and pure ECS entities
share the same operation path. Actor recreation follows the serializer's defined
post-deserialization lifecycle and does not introduce a second snapshot format.

## 6. Compound operations

A compound operation contains ordered semantic child operations. It validates all
children before applying any, applies forward, and reverts in reverse order.

There are two valid construction styles:

1. Build children without applying them, then ExecuteAndPush the compound.
2. Apply child previews while a transaction is active, finalize the compound, then
   PushAlreadyApplied it.

Mixing the two styles would apply children twice and is prohibited. Reparent with
preserve-world-transform, paste, duplicate subtree, and a multi-entity property
edit are compound operations.

## 7. Storage, capacity, and dirty state

History has separately configured entry and payload-byte budgets. Eviction releases
or compacts payload storage; a monotonic arena is acceptable only when resetting or
compacting it cannot invalidate retained entries. The UI may report a history
truncation, but it must remain safe and deterministic.

The saved checkpoint identifies the history state that matches the last successful
save. Dirty is true exactly when current document content differs from that
checkpoint. If bounded-history eviction removes the checkpoint, EditorSession
uses an authoritative source hash or conservatively remains dirty until the next
successful save.

Scene replacement, failed recovery, and schema-incompatible hot reload invalidate
history explicitly. The service clears entries and reports why; it never attempts
to apply stale payloads to a different scene instance.

## 8. Play-mode behavior

Undo and redo are unavailable in Play and Paused. Before entering Play,
EditorSession cancels an active transaction and the Play system must capture or
clone the Edit scene successfully. Only after success may a same-world
restore-based implementation clear history.

Stop restores source data through the serializer lifecycle, resolves UUID
references, rebuilds hierarchy and render bindings, clears runtime selection
caches, and publishes a fresh render snapshot. Simulation changes never dirty the
authored document.

An eventual isolated Play-world implementation may retain Edit-world history, but
that requires an explicit identity-preserving design and is out of scope here.

## 9. Keyboard and UI behavior

The Edit menu displays Undo followed by the current operation name and Redo
followed by the redo operation name. Commands are disabled when no applicable
entry exists, a transaction is unresolved, text input owns the shortcut, or the
editor is not in Edit mode.

Platform-native bindings are required:

- Command+Z / Control+Z: Undo
- Command+Shift+Z / Control+Shift+Z: Redo
- Control+Y: optional Redo where it is platform convention

The UI never assumes an operation succeeded only because a button was pressed; it
shows errors returned by the validation/apply path.

## 10. Required tests

- Apply, undo, redo, and redo-branch invalidation produce the exact semantic state.
- A completed gizmo drag or inspector scrub adds one entry; cancellation adds none.
- Undo to the saved checkpoint clears dirty; a new edit after undo invalidates redo.
- Entry and byte budgets evict safely without unbounded allocation.
- Transform batches handle selected parents/children and parent-local conversion.
- Create, duplicate, delete, and reparent preserve UUID/reference/hierarchy rules.
- Pure ECS entities work identically to actor-backed entities.
- Grid settings round-trip through one scene-setting transaction.
- Target loss, scene replacement, malformed payload, and schema incompatibility
  leave the scene unchanged and present a clear error.
- Play snapshot failure preserves Edit scene and history; Play/Paused cannot mutate
  authored content; Stop publishes restored render state.

## 11. Delivery checklist

- [ ] EditorSession mutation phase and transaction owner
- [ ] UUID target resolver and scene-setting target adapter
- [ ] Semantic component/entity/subtree snapshot codecs
- [ ] Bounded, reclaimable history with saved checkpoint
- [ ] Discrete and already-applied transaction paths
- [ ] Inspector transaction bridge and shortcut routing
- [ ] Lifecycle, transform-batch, hierarchy, and grid operations
- [ ] Play-mode invalidation and restored-scene rebuild
- [ ] Unit, integration, and long-session memory tests
