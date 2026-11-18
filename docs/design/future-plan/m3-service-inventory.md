# M3 simulation service inventory

This inventory records the current code boundaries for the first M3 slice. It
is not an approval to run Play or a claim that world-scoped service activation
is implemented. The approved ownership and transition requirements remain in
[Editor Play Mode](editor-play-mode.md) and issue [#874](https://github.com/JeanPhilippeKernel/RendererEngine/issues/874).

| Area | Current behavior | M3 work before admitting Play |
|---|---|---|
| Fixed-step loop | `Engine::AdvanceSimulation` checks the application policy and active scene binding, resets its accumulator when simulation is disabled, then runs systems, flushes commands, ticks actors and snapshots transforms in that order. | Bind only world B; define pause, exactly-one-step, generation and reentrancy behavior. A must never be ticked. |
| ECS systems | `WorldTick` registers function pointers and a dependency graph; a committed wave may run tasks on the thread pool and waits for them before returning. | Define world-owned registration, preparation and quiescence; ensure systems cannot reach world A through globals. |
| Deferred commands | `WorldCommands` owns its queued commands and flushes them into the supplied scene. Commands can contain callbacks. | Bind commands and callbacks to B's lifetime and activation, reject stale work and drain before teardown. |
| Actors | `ActorManager` is bound to a scene. `Create` makes an entity and invokes `OnCreate`; `Destroy` invokes `OnDestroy` and may destroy its entity. | A separately reviewed authored-actor adapter is required before restoring actor types or invoking lifecycle callbacks. No actor factory or callbacks during unpublished scene preparation. |
| Physics and audio | The scene can contain authored data, but M2's renderer-only loader does not establish world-scoped live physics or audio services. | Define separate service ownership, preparation, activation, pause/step and teardown contracts before admitting either as required gameplay. |
| Authoring session | `EditorSession` owns scene identity, dirty checkpoint, transactions and history. `BindScene` resets history. | Suspend and resume the same A without rebinding; coordinate editor input, scene/save transitions and external changes. |

The initial service policy is fail-closed: no gameplay, actor restoration, live
physics or audio capability is advertised by this slice. Pure ECS reconstruction
alone is not evidence that a Play session can run. A future admission path must
reject required unsupported services before publication and report a structured
outcome. Optional inactive data must be identified explicitly rather than silently
claimed as simulated.

The first implementation slice adds a session suspension boundary and a main-thread
transition ledger. The ledger records legal modes and request outcomes but performs
no world switch, service activation or simulation; its generation is distinct from
the scene-publication generation. The
owner cancels any active preview, drops queued editor requests, and keeps the
same authoring scene, epoch, dirty checkpoint and history. Resume checks the
stored binding and tracked structural/hierarchy revisions; a mismatch remains
suspended for recovery. These guards do not detect arbitrary direct writes to
component data by external code. M3 still needs world-scoped service isolation,
external-change policy and a coordinator before the boundary can safely be used
for Play.
