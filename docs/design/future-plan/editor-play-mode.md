# ZEngine — Editor Play Mode

**Priority:** P1 — required for safe gameplay iteration
**Status:** Design; isolated retained-authoring Play approved on 2026-11-07;
implementation and qualification remain outstanding
**Depends on:** scene-serialization.md, editor-undo-redo.md,
actor-ecs-architecture.md, game-runtime-boundary.md, rendering-flow.md
**Blocks:** trustworthy Play, Pause, Stop workflow

**Agreed strategy:** Retain the Edit world and prepare a separate Play world,
using the ownership/publication contracts in the
[runtime reconstruction proposal](scene-runtime-reconstruction.md). Refuse Enter
Play when the admitted memory budget cannot support the overlap. The former
same-world snapshot/restore proposal is superseded; it is not a fallback.

## 1. Goal

Play mode runs game simulation without changing the authored Edit world or its
document. Stopping reactivates the retained Edit world, rather than reconstructing
it from a snapshot. Runtime state is discarded, not implicitly copied back.

The three modes are:

| Mode | Authored scene mutation | Simulation | Editor inspection/navigation |
|---|---|---|---|
| Edit | Allowed only through EditorSession transactions | Stopped | Allowed |
| Play | Disabled | Running | Read-only scene inspection; camera policy is explicit |
| Paused | Disabled | Frozen except an explicit single step | Read-only scene inspection; navigation allowed by input policy |

The coordinator also has Starting, Stopping, and explicit transition-error states.
These are not additional authoring modes: editing remains suspended until a valid
Edit binding is restored. A second Play transition cannot race an unfinished one.

## 2. Ownership

EditorSession owns the authoring selection, transactions, history, dirty
checkpoint, and its binding to Edit world A. The main-thread Play coordinator
owns mode transitions, suspension/resumption of editor commands and input, and
the lifetime of Play world B. Mode/results are surfaced through editor UI; the
coordinator is not an ECS component.

World A remains allocated and unsimulated throughout Play. Its document, ECS,
render bindings, asset-version leases, and history remain owned. World B has its
own ECS, actor manager, deferred commands, simulation/service state, and mutable
resource instances. Read-only Play inspection uses a separate runtime context;
do not repoint authoring transactions or history to B.

The snapshot used to construct B is separate from undo/redo history. Reconstruct
using the shared document/candidate path, not a shallow ECS/pointer clone.
Immutable mesh/texture/shader payloads may be shared through versioned leases;
mutable material instances and simulation state may not be shared with A.

Admission counts retained A, candidate/active B, snapshot/scratch, resource
versions, uploads, pending retirement, and return-to-Edit preparation. Existing
editor A/B scene-load slots are not proof of sufficient capacity. Reserve the
required bounded capacity before activation; fail Enter Play without clearing
history or mutating A if it cannot fit. Never fall back to same-world execution.

## 3. Enter Play

Entering Play is a transaction with a failure-safe order:

~~~text
1. Require Edit with no conflicting scene/save/Play transition.
2. Cancel an active editor interaction, restoring its before-state.
3. Suspend authored mutation; retain A's history, dirty checkpoint and UI state.
4. Capture a bounded immutable source snapshot, including unsaved authored edits.
5. Admit storage/assets/services and prepare unpublished B asynchronously.
6. Resolve B's UUID references, rendering resources and required service state.
7. Prepare initial frame, input routing, and return-to-Edit/retirement capacity.
8. Revalidate A's instance/revision and all request/lifetime/cancellation guards.
9. Commit world/service/input bindings and Play mode; advance activation generation.
10. Publish the prepared runtime frame; simulation starts only after the guard exits.
~~~

Service activation is an already prepared no-fail switch, not a fallible startup
after publication. Unsupported required gameplay/physics/actor services reject
entry; a renderer-only candidate is not sufficient evidence of functional Play.
The editor may keep rendering A while preparation runs, but cannot mutate it.
Cancel or failure before commit discards B through normal retirement, resumes
Edit on A, and preserves history/dirty state except the cancelled transient edit.

Do not use the current clearing `EditorSession::BindScene` path to suspend A.
Provide explicit suspend/resume behavior with no authored-world replacement.
Queued editor transactions cannot replay later unless still valid and explicitly
resubmitted; do not silently flush them into B.

## 4. Pause and single step

Pause freezes simulation systems and relevant services such as physics/audio
according to their individual contracts. It does not re-enable authored mutation,
undo, redo, structural commands, or gizmo transactions.

Single step is legal only in Paused. It performs exactly the defined simulation
step on B, including the supported service step and B's deferred-command flush,
then returns to Paused. Paused wall time cannot accumulate catch-up simulation on
resume. It must not process an editor transaction midway through the step. Editor
UI may inspect read-only runtime state and navigate only when input routing says
it may. A is never a target of Tick, Step, or runtime WorldCommands.

## 5. Stop and recovery

Stop does not deserialize or replace A:

~~~text
1. Enter Stopping; stop input/simulation and drain B's current simulation wave.
2. Quiesce B's services and reject new commands/callbacks targeting its generation.
3. Prepare a fresh frame from retained A and validate editor resume bindings.
4. Commit active bindings back to A and advance activation generation.
5. Clear runtime selection/picks and restore A's validated editor UI/camera state.
6. Resume authoring commands/history and return to Edit.
7. Retire B, its callbacks/services, and resource pins after CPU/GPU users drain.
~~~

Reserve normal return-binding and retirement capacity on Enter Play. No outgoing
teardown callback runs inside the commit or resolves B through A's active globals.
Do not wait for all GPU retirement to finish before showing A; retained frame and
resource leases protect in-flight B submissions.

If return preparation fails before commit, retain A untouched and B quiescent in
an explicit transition-error state; retry the return path without reloading the
last saved file over A's unsaved work. Do not claim Edit while its bindings are
incomplete. Device loss uses host recovery/shutdown policy, not a promise of
renderable rollback. Runtime mutations never dirty authored source.

## 6. Input and UI behavior

Mode transitions cancel pointer capture and active UI editing before changing
input routing. Edit shortcuts are unavailable while Starting/Play/Paused/Stopping.
Text-entry focus
always wins over global shortcuts.

Scene replacement, source save/reload, authoring asset edits, and automatic
application of reimport/file-watcher changes are deferred or rejected outside
Edit. Report external conflicts and revisit them after Stop; do not mutate retained
A or overwrite its pinned asset versions. Ordinary runtime file I/O is not rolled
back by Stop and must not be described as isolated merely because B is separate.

The toolbar and menu report:

- current mode;
- Play, Pause, Stop, and Step availability;
- preparation/return/retirement progress or failure;
- that undo/redo are intentionally unavailable outside Edit; and
- source dirty state independently from runtime simulation state.

## 7. Persistence and identity

The snapshot contains source-document data only: scene UUID, entity UUIDs,
serializable component values, UUID references, and scene settings such as the
grid. It excludes runtime EntityIDs, actor handles, GPU state, render IDs,
selection, transaction history, and interaction state. A and B may contain the
same entity UUIDs, but UUID resolution is scoped to their world instance; runtime
EntityIDs or actor handles never resolve across worlds.

World-instance identity stays stable while A is retained. Coordinator activation
generation changes on both Enter and Stop. A's authoring session epoch/history
binding remains valid because A was not replaced; asynchronous active-world work
also checks activation generation, so old picks/commands cannot apply after Stop.
Restore selection only after validating targets in A; discard runtime caches.
Actual authoring open/reload still replaces the instance and invalidates history
according to the ordinary editor lifecycle.

Globals and shared mutable assets are not isolated by a second ECS. Service
contexts must be world-scoped; mutation of shared authored asset payloads from B
is forbidden. Copy-on-write/per-world instances are required where runtime
mutation is supported. Apply-play-changes-to-source is out of scope. Same-process
Play does not provide crash isolation; a separate game process is a future option.

Loaded pure ECS remains pure ECS. Authored actor factories and live physics/audio
reconstruction require approved service contracts; `ActorManager::Create` is not
an existing restore API. Do not infer actor types or call side-effectful OnCreate
while preparing B. The runtime reconstruction milestones alone do not complete Play.

## 8. Required tests

- Enter Play with a clean and a dirty document; verify simulation changes do not
  alter source dirty state.
- Snapshot creation failure preserves the Edit scene, active history, and input mode.
- Budget/capability rejection and cancellation during preparation leave A intact;
  cancellation after commit never claims to reverse the published transition.
- Active gizmo/inspector transaction is cancelled safely before Play.
- Pause and Step cannot author a scene or execute undo/redo.
- Stop reuses the same A/document/ECS instance; source values and dirty checkpoint
  agree with the pre-Play state and undo/redo remains valid against A.
- Identical UUIDs/EntityID values in A and B cannot cause cross-world targeting;
  stale picks, queued commands and callbacks fail generation/lifetime guards.
- Failed return preparation never exposes a partially bound Edit scene or loses
  A's unsaved state; retry succeeds without reconstructing A from disk.
- Scene file/reimport changes during Play cannot mutate A's retained content;
  runtime mutable resource instances cannot write into shared authored assets.
- Resource overlap stays within declared budgets; delayed CPU/GPU readers cannot
  make Stop release B early, or allow a new Play preparation without capacity.
- Repeated Play/Pause/Stop cycles remain leak-free and validation-layer clean.

## 9. Delivery checklist

- [ ] Main-thread Play coordinator and explicit state machine
- [ ] Immutable source snapshot/clone path using scene codecs
- [ ] Retained authoring owner, command suspension/resumption, and history continuity
- [ ] Bounded dual-world admission and pinned immutable asset sharing
- [ ] Failure-safe Enter Play and Stop ordering
- [ ] Runtime system/service registration ownership
- [ ] Input, pointer-capture, and shortcut routing
- [ ] Guarded active-world switching and independent runtime retirement
- [ ] Recovery UI and structured error reporting
- [ ] Automated lifecycle and long-session tests
