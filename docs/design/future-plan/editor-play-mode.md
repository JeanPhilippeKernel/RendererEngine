# ZEngine — Editor Play Mode

**Priority:** P1 — required for safe gameplay iteration
**Status:** Design, reconciled with persistent scenes and editor transactions
**Depends on:** scene-serialization.md, editor-undo-redo.md,
actor-ecs-architecture.md, game-runtime-boundary.md, rendering-flow.md
**Blocks:** trustworthy Play, Pause, Stop workflow

## 1. Goal

Play mode runs game simulation without changing the authored Edit document.
Stopping returns the editor to a valid reconstructed edit scene, including
hierarchy, render bindings, and editor-local selection cleanup.

The three modes are:

| Mode | Authored scene mutation | Simulation | Editor inspection/navigation |
|---|---|---|---|
| Edit | Allowed only through EditorSession transactions | Stopped | Allowed |
| Play | Disabled | Running | Read-only scene inspection; camera policy is explicit |
| Paused | Disabled | Frozen except an explicit single step | Read-only scene inspection; navigation allowed by input policy |

## 2. Ownership

EditorSession owns the current mode, SelectionModel, active transaction, history
boundary, and the result surfaced to UI. The Play coordinator owns the simulation
registration and source-scene snapshot/clone lifecycle. Neither is an ECS
component, and both run on the main thread.

Play snapshots are separate from undo/redo history. WorldCommands remains runtime
deferred mutation infrastructure and is not used to restore authored state.

## 3. Enter Play

Entering Play is a transaction with a failure-safe order:

~~~text
1. Reject if not in Edit mode.
2. Cancel an active editor interaction, restoring its before-state.
3. Capture or clone an immutable source scene snapshot.
4. If capture fails, report the error and remain in Edit with history intact.
5. Prepare the runtime/Play scene from that source snapshot.
6. Rebuild and register game systems, resume simulation services, and route input.
7. In a same-world restore design, invalidate undo history only now.
8. Publish the Play render snapshot and enter Play mode.
~~~

The documented current strategy restores a snapshot into the same editor world.
Because runtime EntityIDs and render bindings may change during restoration, old
commands cannot safely be replayed after this boundary. Clear history only after
the snapshot succeeds, not before. An isolated clone-of-edit-world strategy may
preserve Edit history later, but it needs an explicit identity-preserving design.

## 4. Pause and single step

Pause freezes simulation systems and relevant services such as physics/audio
according to their individual contracts. It does not re-enable authored mutation,
undo, redo, structural commands, or gizmo transactions.

Single step is legal only in Paused. It performs exactly the defined simulation
step then returns to Paused; it must not process an editor transaction midway
through the step. Editor UI may inspect read-only state and navigate only when
input routing says it may.

## 5. Stop and recovery

Stop is also all-or-nothing:

~~~text
1. Stop simulation and unregister runtime-only systems.
2. Restore or replace the Edit source from the immutable snapshot.
3. Resolve UUID hierarchy and asset references.
4. Rebuild world transforms, physics/editor proxies, and ECS-to-render bindings.
5. Increment scene-instance epoch; clear EntityID selection and pick caches.
6. Publish a fresh immutable Edit render snapshot.
7. Return to Edit only after all required rebuilds succeed.
~~~

If restore fails, enter an explicit recoverable error state. Do not expose a
partially restored scene as Edit mode. Offer a retry, reload from last saved file,
or a clearly marked recovery workflow. Play mutations never dirty authored source.

## 6. Input and UI behavior

Mode transitions cancel pointer capture and active UI editing before changing
input routing. Edit shortcuts are unavailable in Play/Paused. Text-entry focus
always wins over global shortcuts.

The toolbar and menu report:

- current mode;
- Play, Pause, Stop, and Step availability;
- snapshot/restore progress or failure;
- that undo/redo are intentionally unavailable outside Edit; and
- source dirty state independently from runtime simulation state.

## 7. Persistence and identity

The snapshot contains source-document data only: scene UUID, entity UUIDs,
serializable component values, UUID references, and scene settings such as the
grid. It excludes runtime EntityIDs, actor handles, GPU state, render IDs,
selection, transaction history, and interaction state.

Actor restoration follows the serializer's settled lifecycle. An actor whose
OnCreate reads component values must see deserialized components; ActorManager
does not become a parallel persistence system.

## 8. Required tests

- Enter Play with a clean and a dirty document; verify simulation changes do not
  alter source dirty state.
- Snapshot creation failure preserves the Edit scene, active history, and input mode.
- Active gizmo/inspector transaction is cancelled safely before Play.
- Pause and Step cannot author a scene or execute undo/redo.
- Stop rebuilds UUID references, hierarchy world transforms, renderer bindings,
  editor proxy data, selection caches, and output snapshots.
- Failed Stop recovery never exposes a partial Edit scene.
- Commands from before a same-world Play restoration cannot apply to reconstructed
  runtime EntityIDs.
- Repeated Play/Pause/Stop cycles remain leak-free and validation-layer clean.

## 9. Delivery checklist

- [ ] Main-thread Play coordinator and explicit state machine
- [ ] Immutable source snapshot/clone path using scene codecs
- [ ] Failure-safe Enter Play and Stop ordering
- [ ] Runtime system/service registration ownership
- [ ] Input, pointer-capture, and shortcut routing
- [ ] Rebuild/publish boundary for restored scenes
- [ ] Recovery UI and structured error reporting
- [ ] Automated lifecycle and long-session tests
