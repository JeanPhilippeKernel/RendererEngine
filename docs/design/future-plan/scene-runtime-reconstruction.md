# Scene Runtime Reconstruction — Review Proposal

**Status:** Approved implementation contract; isolated retained-authoring Play approved on
2026-11-07; milestone 1 implementation is present; acceptance qualification is
in progress, not yet a production-readiness claim.
**Tracks:** [#829](https://github.com/JeanPhilippeKernel/RendererEngine/issues/829)
and the remaining runtime/Play coverage in
[#719](https://github.com/JeanPhilippeKernel/RendererEngine/issues/719).
**Depends on:** [scene serialization](scene-serialization.md),
[cooker contracts](scene-cooker-contracts.md), and the existing render handoff.

## 1. Recommendation

Extend the existing transactional reconstruction path into an engine-owned scene
lifetime, then add source-independent cooked asset loading. Do not build another
scene parser or a parallel ECS reconstruction implementation. Deliver Play as a
separate milestone after runtime publication works.

The cooker is already implemented. It produces validated artifacts; it does not
yet provide the complete load, asset residency, publication, and simulation
lifecycle needed to run a shipped scene.

This proposal is an implementation contract to review, not a production-readiness
claim. The acceptance gates below must pass before the corresponding milestone
is considered complete.

## 2. Existing foundation and missing work

| Area | Reuse | Delivery boundary |
|---|---|---|
| Documents and schemas | `SceneDocument`, source migrations, strict `BinarySceneCodec` | No new serialization format |
| ECS reconstruction | `PopulateCandidate`, UUID mapping, reference resolution, hierarchy validation | M1: engine-owned lifetime around this existing path |
| Editor loading | `EditorSceneCandidate`, asynchronous scene-open guards | M1: shared ownership/preparation with editor policy retained in Tetragrama |
| Publication | Main-thread editor handoff and epoch checks | M1: common guarded commit and explicit retirement |
| Assets | Cook manifest/object validation and existing renderer upload facilities | M2: cooked mesh, material, texture, and environment bindings without source registry lookup |
| Rendering | Immutable `RenderFrameState` handoff | M1: pin exact asset versions until all relevant submissions retire |
| Gameplay | Existing simulation loop and actor manager | M3: explicit service activation and authored actor reconstruction |
| Play | Design document; simulation disabled in editor | M3: separate authoring/runtime ownership and mode transitions |

Current editor loading already builds an unpublished candidate and preserves the
active scene on preparation failure. It also creates a bound, empty actor manager;
it does not fabricate actors for loaded pure-ECS entities. These behaviors must
survive the refactor.

## 3. End-to-end flow

```text
AUTHORING / BUILD TIME                      RUNTIME

Scene YAML + imported assets               Immutable package root
             |                                     |
             v                                     v
     Existing scene cooker                 Capture manifest snapshot
             |                             + schema/format lifetimes
             v                                     |
    Scene objects + asset objects                  v
    + published package manifest           Validate target and limits
             |                                     |
             +---------- deployment -------------->|
                                                   v
                                      Read and verify object bytes
                                                   |
                                                   v
                                      Strict binary scene decode
                                                   |
                                                   v
                                      Populate unpublished ECS
                                      + resolve UUID references
                                                   |
                                                   v
                                      Resolve cooked asset closure
                                      + prepare render bindings
                                                   |
                                                   v
                                      Complete required GPU uploads
                                                   |
                                                   v
                                      Main-thread commit preflight
                                                   |
                                                   v
                                      Publish new scene generation
                                                   |
                                                   v
                                      Retire old scene/resources
```

Any failure before commit discards the candidate and keeps the active scene.
There is no runtime migration, recook, importer, or source-directory fallback.
Source loading joins the same preparation path after its existing parse/migration
stage; it does not pass through cooked decoding.

## 4. Ownership and code placement

Proposed responsibilities, not final API names:

```text
Application / editor adapter (retains authoring owner during Play)
  | requests, dirty consent, UI policy
  v
Engine scene coordinator
  +-- active scene owner -----------------------------+
  +-- one preparing load + one waiting request        |
  |     +-- captured request/generation               |
  |     +-- package snapshot + schema/format leases   |
  |     +-- bounded scratch + candidate scene owner   |
  +-- bounded retirement queue                        |
                                                      v
Scene owner                              Immutable frame snapshots
  +-- scene-storage lease                + asset/resource pins
  +-- owned SceneDocument                           |
  +-- ECS Scene                                    v
  +-- bound ActorManager                   Renderer / GPU submission
  +-- scene-local derived bindings                  |
  +-- asset version leases                          v
  +-- staged simulation registrations       Completion releases pins
```

Shared scene ownership and preparation belong in `ZEngine/ZEngine/ECS`, beside
`SceneDocument`. Cooked package asset lookup belongs in the engine asset layer,
not Tetragrama. Manifest parsing and object verification should reuse shared
primitives from `ZEngine/ZEngine/Cook`; runtime must not instantiate a cooker.
Editor source paths, dirty prompts, selection, history, and legacy editor-only
fixups remain in Tetragrama. Application startup adapts the existing
`GameApplication` seam; a new game DLL/plugin ABI is not required.

The scene owner is move-only. Internal pointers and handles are borrowed only
within its lifetime; asynchronous work holds explicit operation/resource leases,
not raw pointers to an editor or scene that can disappear. Keep the document
alive because reconstructed values, including scene names, may borrow its storage.
Allocation routes outlive their documents. Manifest storage and borrowed asset
format descriptors likewise require retained allocator/registry lifetimes.

Shutdown order is explicit: stop/cancel work, drain callbacks and frame users,
deactivate scene services, destroy actors while their ECS still exists, destroy
scene/document/bindings, then release backing storage. Assets still in GPU use
remain pinned independently until their completion fences permit release.

## 5. Transaction and publication contract

```text
Requested -> Waiting -> Preparing -> WaitingForGPU -> Ready -> Commit -> Active
                |          |              |            |                 |
                +----------+--------------+------------+                 v
                                          |                            Retiring
                                          v                               |
                               Failed / Cancelled / Superseded             v
                                   active unchanged                     Released
```

### 5.1. Requests, cancellation, and completion

Request admission reserves a bounded result record and captures the coordinator
lifetime, request sequence, expected active generation, and input snapshot.
Invalid arguments, shutdown, or exhausted request capacity reject admission
synchronously without cancelling an already admitted load.

There is one preparing operation and at most one waiting request containing no
candidate allocation. A newer admitted request supersedes both the older waiting
request and any not-yet-committed preparation. Preparation of the latest request
starts only when storage, worker, upload, and retirement capacity permit it.
Superseded work cannot publish, even if its I/O or upload completes successfully.

Each admitted request records exactly one terminal outcome:

| Outcome | Meaning |
|---|---|
| Committed | The guarded publication completed; includes the new generation. |
| Cancelled | Explicit cancellation or coordinator shutdown won before commit. |
| Superseded | A newer admitted request replaced it before commit. |
| Failed | Preparation/preflight failed; includes a stable category and stage. |

Admission, cancellation, supersession, and commit are serialized by the main-thread
coordinator. The first terminal transition wins. Cancellation is accepted up to
entry into the guarded commit; afterward it reports TooLate and never claims
rollback. Terminal publication denial may precede resource reclamation: cancelled
uploads remain charged and pinned until completion. Expose cleanup separately.

Results remain in the request's bounded record until acknowledged/released.
Completion notifications run on the main thread after leaving the commit guard;
they cannot reenter that commit. Expired UI listeners suppress notification, not
the terminal result. Result/notification capacity is reserved at admission; queue
saturation cannot lose a completion. Shutdown seals admission, cancels pending
work, records terminal results, and drains engine-owned cleanup before services
disappear. No caller-owned raw pointer survives into an asynchronous callback.

### 5.2. Thread and allocation ownership

| Work | Exclusive owner / execution rule |
|---|---|
| Request state, storage leases, active bindings, consent | Main-thread coordinator |
| Object I/O, verification, source parse/migration, binary decode | Load worker; immutable input and operation-local storage |
| Unpublished ECS population and hierarchy resolution | One load worker; exclusively transferred candidate storage |
| Editor-only candidate policy and session preparation | Main mutation thread after worker ownership returns |
| Asset cache index/version publication | Asset service under its own synchronization; no decode or upload wait while locked |
| Resource creation, upload submission, descriptors, pipeline readiness | Existing renderer resource owner/queue, not an arbitrary load worker |
| CPU scene/actor teardown | Main mutation thread after readers drain |
| GPU destruction/reuse | Renderer retirement owner after required completion signals |

Initialize/freeze schema registrations and built-in component type IDs before
workers start. Worker codecs must be explicitly admitted as pure candidate-local
operations: immutable callback context, no active-world access, service
registration, engine-global allocation/reset, or renderer calls. Built-ins require
an audit and concurrency tests before being admitted. Source migration callbacks
retain their existing pure-tree contract. Unapproved custom callbacks fail the
load with UnsupportedCapability; do not silently execute them on a worker or
fall back to a large synchronous main-thread load. Unknown source data still
follows the existing read-only inspection policy.

Arena/slab ownership transfers are explicit and exclusive. A worker cannot
allocate from engine-global/editor A/B bookkeeping or rewind a shared arena.
Reserve its storage on the main thread, then transfer only its candidate-local
allocator view. Route or explicitly charge helper scratch, including the current
UUID resolver scratch; do not hide OS allocations outside the load budget.
The actor manager stays empty during worker construction. Queue ownership transfer
provides the happens-before boundary before the main thread reads the candidate.

### 5.3. Guarded publication

Each request captures the target coordinator's lifetime token, expected active
generation, request sequence, and package/schema identities. Cancellation,
shutdown, replacement requests, registry changes, or an intervening scene switch
invalidate its right to publish. Check both asynchronous completions and the
final commit. World-instance identity is stable while an owner is retained;
coordinator activation generation changes on every active-world switch. Retain
existing editor epoch checks for the authoring session's instance binding, and
also validate activation generation for delayed active-world work. Replacing an
authoring world invalidates its epoch/history; suspending it for Play does not
pretend it was replaced. These tokens have distinct, explicit scopes rather than
competing authority over the same lifetime.
Editor discard consent is also bound to the document and external revisions;
edits made while loading invalidate old consent even if the scene generation did
not change. Dirty consent waits are part of preflight, never part of commit.

Before commit, on the main mutation thread:

1. Validate request identity, cancellation, owner lifetime, and expected generation.
2. Obtain any required editor consent and finish/cancel active edit interaction.
3. Confirm required assets, first render snapshot, and service preparation succeeded.
4. Reserve all binding, notification, and retirement capacity needed by the switch.
5. Establish that the remaining commit operations cannot return recoverable failure.

The implementation must account for these existing bindings explicitly:

| Binding / side effect | Prepare and preflight | Guarded commit / afterward |
|---|---|---|
| `EngineContext::Scene`, `ActorManager` | Build complete candidate owners | Switch borrowed views to the new owner |
| `WorldCommands`, `WorldTick`, simulation clock | Drain an outgoing simulation wave; prepare scene-local command/tick state | Switch to new state, reset accumulator; never replay old commands into it |
| `GameApplication::CurrentScene`, camera, initial frame | Prepare compatible renderer data and camera payload | Switch one generation-coherent binding set |
| `EditorSession`, settings, selection/history/picks | Validate consent/revisions, cancel preview, reserve reset/bind or suspend/resume storage | Allocation-free replacement reset or Play suspend/resume; invalidate old runtime targets |
| Outgoing ownership and callbacks | Reserve retirement or authoring-retention ownership and stop new outgoing work | Move old owner to retirement, or retain A on Enter Play; teardown occurs outside commit |
| Completion/UI/recent-file persistence | Reserve result and notification records | Deliver afterward; persistence failures do not reverse success |

Commit runs between simulation/frame-preparation phases, with no concurrent
reader of mutable application/engine bindings. The order is: recheck all guards,
enter the non-reentrant mutation guard, move owners and switch all prepared views,
advance generation, publish the prepared initial frame/result, leave the guard.
No observer sees a mixed binding set. Existing submitted immutable old frames
may finish; newly produced frames must use the committed generation.

No I/O, allocation, wait, renderer submission, arbitrary callback, `OnDestroy`, or
fallible attach is allowed inside this sequence. Refactor existing editor bind
and attach into fallible preparation plus a no-fail application step; an assertion
after outgoing teardown is not proof of transactional safety. Outgoing actors
are torn down later with their own scene context, not through the newly active
global bindings. Legacy callbacks relying on outgoing globals require an explicit
adapter before the switch path supports them. Violating a no-fail invariant is
an engine defect, not a recoverable load result.

Scene-local systems must be prepared without side effects on the active scene.
Their activation must have a no-fail switch contract; systems that cannot meet
that contract need a separately reviewed recovery design before inclusion.
Old simulation work and queued commands must never target the new world by
accident. Generation-tag delayed picks, loads, and scene-directed work. Milestones
1 and 2 do not activate newly reconstructed gameplay systems.

Retirement waits for any remaining CPU readers. Render snapshots remain immutable
copies, not borrowed mutable ECS pointers. GPU resources are released only after
their last referencing submission completes. Cancellation after upload submission
uses this same retirement path; it must not free in-flight resources immediately.

### 5.4. Failure domains and diagnostics

Ordinary I/O, corruption, unsupported capability/version, budget, allocation, or
upload-preparation errors are precommit load failures. Preserve the old active
owner, bindings, generation, and authored state, apart from explicitly cancelled
transient edit previews. With no active scene, remain in a host-owned empty/error
state; the runtime smoke host reports the result and exits unsuccessfully, while
an interactive host can offer retry or another package.

Device loss, process-wide allocator failure outside the reserved load path, and
broken engine invariants are not ordinary load failures. They enter the host's
fatal/device-recovery path; do not promise that the old scene remains renderable
or wait forever on completion fences that can no longer signal. Device recovery
itself is outside this proposal. Cleanup follows the device owner's loss/shutdown
rules, never an invented successful GPU completion.

Diagnostics contain request ID, stage, stable category, retryability, scene UUID,
expected/observed generation, and applicable typed asset identity/object digest
and underlying I/O/GPU error. Categories distinguish InvalidInput, I/O,
CorruptObject, UnsupportedVersion, UnsupportedCapability, BudgetExceeded,
AllocationFailed, UploadFailed, and DeviceLost. Reuse existing codec diagnostics
as bounded nested detail, not as a replacement for operation outcome. Preserve
the terminal category and dropped-message count when detail storage fills.

No automatic retry loop: Busy admission can be retried after capacity changes;
I/O retry is an explicit new request; corrupt/unsupported content requires a new
package or runtime. Cancellation is checked between bounded work units. Slow
I/O/upload operations remain charged until they return or device shutdown
resolves them; timeout must never authorize premature memory reuse. Stage age,
progress, cancellation, and cleanup state remain observable to the host.

## 6. Cooked package and asset readiness

### 6.1. Package compatibility and snapshot coherence

Capture one immutable manifest/package snapshot for a load. Validate target,
versions, scene selection, dependency closure, sizes, and every consumed object's
integrity before decoding it. A manifest `Rebuild` result means an unsupported or
invalid runtime package, not permission to cook. Structural manifest validation
alone does not verify object bytes.

Decode exactly the bytes that were verified: retain a verified private buffer or
a file/root lease whose immutability is guaranteed. Reopening a mutable path after
verification is insufficient. SHA-256 detects corruption; package authenticity
and signing are separate distribution concerns. Package paths are validated
root-relative VFS paths; reject traversal, invalid lengths/offsets, conflicting
identities, and unbounded counts before allocating or opening object paths.
The deployed object store must retain objects needed by pinned package snapshots.
If that retention cannot be guaranteed, verified private copies are required;
a root handle alone does not make mutable files immutable. Package replacement
affects future requests, never retargets an admitted request midway through load.

The initial supported target is the current `DesktopVulkanLE64` contract, with
strict current manifest/binary/component/asset format versions and renderer/device
capabilities. Startup freezes a runtime capability table; preflight compares the
selected scene and complete asset closure against it. Decoder availability alone
does not imply supported runtime behavior. No new manifest version or inferred
historical compatibility is introduced by this proposal.

### 6.2. Explicit runtime capability policy

| Data / feature | Milestones 1 and 2 policy |
|---|---|
| UUID, name, transform, parent hierarchy | Reconstruct and validate; never serialize runtime handles |
| Mesh, material, texture and environment references | Resolve complete typed closure and renderer bindings |
| Lights, camera, grid/sky scene settings | Reconstruct supported rendering values; editor-only display stays host policy |
| Rigid-body values | Preserve as authored data in an explicitly renderer-only host; no physics activation claim |
| Actor scripts, physics/audio behavior or custom runtime services | Reject a requested execution requirement without an approved adapter |
| Unknown/newer cooked schema or unsupported asset format | Reject, even if source inspection would be read-only |
| Unknown/newer source data | Retain the existing read-only editor inspection/save rejection policy |
| Custom known schemas | Require an explicit capability entry and audited reconstruction callbacks; default reject |

Load intent is explicit: RendererOnly for milestone 2, Gameplay only after its
required services are supported. A renderer-only load reports inactive simulation
data in diagnostics; it must not be presented as successful gameplay launch.
There is no generic permission to ignore an unknown component or asset. Existing
editor source behavior is preserved by the source adapter, not by weakening the
strict cooked runtime policy.

### 6.3. First-frame readiness

Resolve assets by typed manifest identity and content/version, not UUID alone.
Prepare all required mesh, material, texture, environment, and scene-light
references, including transitive dependencies and draw/instance bounds. Missing
required bindings fail the load; silent mesh-only success or arbitrary truncation
is not an acceptable first milestone. Optional resources require explicit schema
defaults rather than an implicit missing-asset policy.

The first runtime milestone uses a strict **first-frame-ready** policy: required
CPU decode, GPU upload completion, descriptors, and render bindings are ready
before publication. CPU cache presence alone is not readiness. Deferred streaming
and renderer-approved fallback resources are later policies, not hidden behavior
in this gate. Preparation may use shared caches but must pin immutable versions;
the old scene can continue using an older version of the same asset UUID.

The readiness set also includes geometry/index bindings, material variants and
blend/depth state, sampled mip residency required by the initial rendering
policy, bindless/descriptors, environment-derived lighting resources when enabled,
and the shader/pipeline variants used by the initial frame. Shaders are supplied
by a compatible versioned host resource set, not fetched from authoring sources.
Pipeline creation may occur from those shipped shaders, but cannot be postponed
to an unbounded first draw on the main/render critical path. No debug/magenta
fallback may stand in for a required package object. Schema-defined defaults for
absent optional texture maps or disabled environment lighting remain legal.

For source-free rendering, choose the camera by an explicit launch camera UUID,
or otherwise require exactly one `CameraComponent::IsMain` camera. Validate its
transform, projection parameters, and viewport-derived aspect. Missing/ambiguous
cameras fail with a diagnostic; do not choose entity iteration order. The editor
adapter retains its existing editor camera policy instead of imposing this rule
on editor source open.

GPU readiness requires the renderer's completion/visibility and queue-ownership
rules, not merely a submitted upload. The coordinator polls asynchronous readiness
while the outgoing scene keeps updating/rendering; no global device-idle wait or
main-thread fence wait is introduced for loading. Resource work on a render-critical
thread must obey the acceptance profile's slice budget. Prepare the immutable
initial snapshot and reserve its handoff capacity before the guarded switch.

Source-free launch needs a startup route that mounts the cooked package without
the current authoring project scan/import workflow. Rendering a cooked scene in
the existing application host is sufficient; shipping installers and a new host
target are not prerequisites.

## 7. Memory and backpressure

Use bounded scene-storage leases and bounded asynchronous/upload queues. The
existing editor A/B arenas demonstrate replacement ownership, but do not prove
that editor + Play + pending + retiring scenes fit in two slots.

```text
Peak load memory = active scene + candidate scene + retained document/manifest
                 + decode scratch + CPU asset versions + upload staging
                 + bounded retiring scenes/resources + normal engine overhead
```

These are disjoint accounting terms: scene terms cover ECS/local binding storage;
documents and asset allocations are counted in their separate terms, not again
inside the scene-owner aggregate. Report reserved scene slots separately from
live bytes within them.

Keep CPU RAM and GPU memory budgets separate. Shared assets count once per live
version; overlapping versions and pending uploads still count. No automatic
transfer of the cooker's 768 MiB workspace limit into a runtime scene budget.
Select defaults from measured scene footprints and declared target limits during
implementation; this proposal does not invent a RAM guarantee.

Allow one preparing replacement plus the single bounded waiting request described
in section 5.1. A newer request cannot reclaim older storage until workers/uploads
release it. Pause preparation or reject admission when storage or retirement
capacity is exhausted; never allocate an unbounded extra arena. Limits cover
document/ECS growth, asset residency, upload staging, and queued retirement, with
structured budget diagnostics. Shutdown drains these owners before engine arenas
or schema/format registries are destroyed.

### 7.1. Cache reclamation and observable accounting

Cache keys include typed identity, object content/format version, and any target
or decode/device variant affecting the payload. Coalesce concurrent requests for
the same immutable version. A requesting operation owns a lease; cancelling one
request does not cancel shared work still needed by another owner.

Track separate CPU, GPU, and staging ledgers. Each allocation is charged once
before allocation/submission, including cache metadata, scratch, alignment and
allocator overhead within the tracked pool. Cache hits transfer/share leases;
they do not double-charge a shared payload. Old and new content versions are
distinct charges. Retirement transfers accounting ownership, not an early credit.
Release credit only when the allocator/resource owner can actually reuse storage.
Do not equate requested payload bytes, reserved arena bytes, committed pages,
process RSS, and GPU allocations; report those measurements separately.

GPU backing allocation admission uses a single fixed 16,384-record ledger for
buffers and images, including the staging ring and one-shot staging. Each native
object first reserves bookkeeping, then charges its queried Vulkan memory
requirements before VMA allocates backing memory. Pending reservations count
against both aggregate and domain ceilings; native allocation/binding/view failure
returns the reservation. An alias retains the same backing record rather than
charging its memory twice. Retiring objects keep their charge until the final
native object releases the allocation, including when the original object retires
before an alias. Limit changes cannot undercut already admitted work.

The initial safety ceilings are 4 GiB aggregate, at most 512 MiB in HostStaging
and 256 MiB in HostReadback; other domains remain bounded by the aggregate ceiling.
They are configurable admission ceilings, not a qualified residency profile or a
supported scene-size promise. Synchronized current/high-water admission and live
allocation counters are separate from sampled VMA block bytes and driver heap
telemetry. These limits bound backing allocations, not driver-internal metadata
or VMA's retained empty blocks; those remain part of profile calibration and
reserved-memory qualification.

Native disposal capacity is reserved before creating buffers/images and aliases,
shader modules, cached pipelines/layouts/samplers/render passes, descriptor pools,
cached image views, framebuffers, query pools, fences and semaphores. The existing
2,048-slot retirement ring retains its sentinel, admitting at most 2,047 total
active, queued and draining disposal records; it is not an extra unbounded overflow
queue. A move-only unpublished lease returns its credit on creation failure.
Published native handles or typed buffer/image records retain that credit until
actual destruction. Native buffers and their aliases each reserve one disposal
record while sharing one backing-memory charge. Descriptor sets owned by a pool
and a default image view owned by an image retire as part of their owner's record.

Multiple producers serialize admission/enqueue, and one serialized consumer owns
native destruction without holding the producer mutex across buffer/image frees.
Draining records cannot be reused until native destruction finishes. A guarded
immediate-destruction path returns unpublished/cache-idle native credits without
queuing them. The render timeline's next value is atomic for cross-owner retirement
reads. External raw-handle producers must use the admitted creation path; legacy
unreserved entries can only claim currently free capacity when enqueued and do not
have the guaranteed disposal credit of an admitted object. Native reservation,
queue/drain occupancy and high-water values are queryable independently of GPU
backing bytes.

CPU TLSF pools expose synchronized backing/control bytes and exact current live
block bytes/counts, including block overhead. Their high-water counters observe
operation boundaries, not the temporary old/new overlap inside a realloc; the
fixed backing capacity remains its hard upper bound. Upload telemetry aggregates
the decode, synchronous-scratch and task pools without allocating, and reports
active decode leases and pending worker tasks. Cross-pool sampling is not an
atomic process snapshot, and a sum of pool peaks is not a simultaneous RSS peak.

Source mesh, material and texture payloads share the 256 MiB asset-cache slab.
Retained strings occupy individually reclaimable immutable payload blocks, not
the process arena. Descriptor growth is checked before cache publication; fixed
UUID indexes refuse excess entries instead of rehashing on a worker. Registry
indexes reserve the full 8,192-record profile, including the case where every
record has the same asset type, before concurrent registration begins.

Source material reads snapshot at most 1 MiB into caller-owned bounded scratch
and use the allocation-free material schema reader. An explicit source-compatible
policy preserves missing legacy fields and ignores bounded, fully validated
unknown metadata; the cooker's strict closed schema remains the default. Both
policies reject unsafe texture paths, invalid identities, duplicate known fields,
non-finite material values and excessive nesting/string sizes. Failed decoding
preserves output and rewinds its scratch allocation. A required texture refusal
cannot publish a partially resolved material or silently substitute a fallback.
Cache publication uses the captured registry generation/record and announces
readiness only after complete CPU descriptors are committed. Independently
completed dependencies may remain in the bounded shared cache after cancellation.

Source instances retain their mesh/material/texture CPU closure through an
`AssetCacheLease`. Its fixed slot-indexed metadata is allocated inside admitted
scene storage, before temporary decode checkpoints. Repeated references deduplicate
without growth or quadratic searches. Dynamic editor placement owns a separate
scene-local lease. Immutable payload references protect detached old versions;
stable logical UUID counters also protect current versions and dependencies added
by reimport. Preview tasks use a small operation pin until immutable GPU draw
metadata and staging own the needed bytes. Pipeline/preview owners retire before
the cache slab shuts down. Cancellation releases source pins only after worker
completion, never while a worker may still use them.

CPU cache maintenance is explicit, serialized with decoding, and try-locks cache
access. It does not run as an ordinary per-frame scan. Pressure recovery evicts
eligible entries in least-recently-used order with bounded, preallocated sorting
workspace, then retries staging outside the cache lock without admitting a second
decode workspace. Zero-retention mode and explicit maintenance can reclaim all
eligible CPU payloads. Mesh/hierarchy UUID slot identities remain stable for reload;
evicted borrowed arrays/string views are cleared before their blocks are returned.
Bounded scalar material/GPU descriptors and texture handle mappings remain owned
by their existing tables and GPU retirement policies, not by reclaimed CPU source
strings. Material source rehydration reuses its existing slot. GPU compaction
preflights all resident CPU sources under the cache guard and defers without
resetting geometry if an idle GPU version has outlived its CPU cache entry.

CPU texture releases are deduplicated and have capacity for the entire admitted
UUID index. GPU fire-and-forget releases transfer to the fixed per-texture
retirement bitmap when the smaller pending queue is saturated. Neither path can
lose disposal ownership because its bounded queue fills.

An asset version is evictable only after all scene/request/frame pins, in-flight
upload users, and GPU submissions release it. Evict unpinned completed entries
least-recently-used under budget pressure; a zero-reference cache hit may be
retained only within its configured cache cap. Never clear all caches or patch
live UUID bindings to recover space. If eligible eviction cannot satisfy the
reservation, wait under bounded request capacity or fail with BudgetExceeded.

Expose stage timings, request outcomes/queue occupancy, active/pending/retiring
scene counts, per-pool current/high-water/reserved bytes, cache hits/misses and
evictable/pinned bytes, staging backlog, outstanding submission age, and cleanup
counts. Attribute each reservation to a request, scene, cache, or renderer owner.
The zero-cache test mode explicitly evicts eligible entries so leak assertions
can distinguish warm-cache retention from unreclaimed ownership.

## 8. Actors, physics, and Play boundaries

Loaded pure-ECS scenes remain pure ECS. `ActorManager::Create` currently creates
another entity and immediately calls `OnCreate`; it is not an authored actor restore
API. Supporting authored actor types requires a separate stable type/factory
contract, binding to existing loaded entities, post-deserialization callbacks,
and side-effect-free preparation. Do not infer actor types from ECS composition.

Similarly, restoring rigid-body component values is not proof that live physics
bodies, audio emitters, or gameplay services have been reconstructed. Milestones
1 and 2 prove scene/render reconstruction only. Unsupported required simulation
features must be rejected explicitly, not advertised as working gameplay.

**Approved decision (2026-11-07):** Retain the authoring world and construct an
isolated runtime world. The [Play contract](editor-play-mode.md) now uses that
strategy; the older same-world snapshot/restore approach is superseded, not a
fallback when memory is tight.

```text
Edit owner A -- capture immutable source --> prepare Play owner B
     |                                           |
     | retained, not simulated                   v
     |                                  commit runtime B; Play/Pause
     |                                           |
     +<-------- Stop: rebind retained A -----------+
                  retire runtime B
```

Retaining A avoids rebuilding authored data on Stop and must preserve valid Edit
history through dedicated suspend/resume behavior. Existing
`EditorSession::BindScene` clears history and cannot simply be reused unchanged.
Runtime selection/picks use instance and activation-generation guards; authored
history stays bound to A while authoring commands are suspended. Enter Play uses
the current in-memory source, including unsaved edits. Stop reactivates A with a
fresh frame, invalidates runtime inspection/picks, and retires B without restoring
A from disk or copying runtime changes back. If the extra world cannot fit, fail
Enter Play while keeping Edit intact. Budget active/candidate/retiring resources
and return-to-Edit preparation, not only two ECS arenas.

Share immutable asset versions through leases, not mutable material instances or
simulation state. World-scoped services must not mutate A through engine globals.
This is same-process world isolation, not crash isolation or rollback of runtime
file I/O. Shipping runtime retains no editor authoring world. The Play contract
defines command suspension, history, input, Stop, and admission requirements;
their implementation/service adapters and qualification remain milestone 3 work.

## 9. Delivery and acceptance gates

### 9.1. Supported qualification scope

Qualification covers single-active-scene replacement on the existing desktop
Vulkan target, not additive scenes, world streaming, device recovery, or every
GPU that can launch the engine. Record OS, CPU, physical RAM, GPU/driver, renderer
backend, build configuration/commit, host shader identity, viewport, and budgets
for each qualified profile. The local reference profile starts with the user's
current macOS machine and SampleProject; Windows local execution remains the
user's responsibility. Linux/Windows/macOS build and non-GPU lifecycle checks run
in CI. A platform needs actual rendered evidence, from automation or a recorded
manual run, before claiming rendered qualification on that platform.

Source fixture and cooked package identities are pinned. Include a small semantic
fixture, SampleProject visual fixtures, a generated near-limit fixture, and
limit-plus-one inputs. Derive admitted entity/draw/asset counts from declared
profile caps no greater than the existing engine/renderer limits; do not present
theoretical `MAX_ENTITIES` as a proven supported scene size. Fixture manifests
record exact counts and expected capabilities. This is test evidence, not a new
runtime package format.

### 9.2. Performance, memory, and visual acceptance profile

The following are proposed requirements, not reported measurements:

- Loading-related main-thread work is sliced to at most 4 ms per frame in
  aggregate; guarded commit is at most 2 ms within that allowance. File/decode,
  hierarchy population, GPU waits, and pipeline compilation do not move into the
  commit to satisfy a nominal slice count. Render-critical resource preparation
  is measured too; moving a stall from main to render thread is not success.
- For the declared reference fixture/device, no loader-attributable frame stall
  exceeds 8 ms over matched steady rendering. Report p50/p95/p99 and maximum frame
  time as well as per-stage spans. Investigate outliers; do not discard them
  without recorded external causes. These targets do not promise a universal
  frame rate on arbitrary hardware or scenes.
- Before final qualification, freeze finite CPU scene/document/scratch/cache,
  GPU residency, staging, queue/retirement, cancellation-latency, and load-time
  limits in the profile. Account for active/candidate overlap and all pinned old
  versions. Derive defaults from initial measurements with declared headroom;
  exceeding limits fails acceptance, not an invitation to silently raise them.
- Measure 30 fresh-process loads and 30 same-process replacements, reporting
  first-use and warm states separately. Record filesystem cache conditions; a
  fresh process does not prove cold disk I/O. End-to-end timing runs from admitted
  request to first rendered frame of its generation, not just CPU decode. Report
  dirty-consent wait separately. Unsupported or uncalibrated profiles cannot pass
  production acceptance; calibration is a bounded deliverable, not a fabricated
  RAM/throughput promise in this document.
- Visual checks use fixed viewport, camera, exposure, seed, timestep, and a defined
  temporal-history settling sequence. Compare source versus cooked output on the
  same device/backend and against reviewed backend-specific references. Initially
  allow at most 0.5% of pixels with any channel differing by more than 4/255 in a
  declared RGBA8 capture space, excluding only predeclared UI masks. Validate the
  tolerance against repeat-capture noise before freezing it; widening it requires
  review. Missing objects, incorrect material/blend modes, hierarchy/camera errors,
  non-finite output, or lost environment lighting fail semantic assertions even
  when they occupy fewer pixels than the tolerance.

Timing/visual thresholds can be revised during proposal review or documented
calibration, never retroactively weakened to pass an unexplained regression.
No build results or benchmark history accumulate in this contract; retain the
profile and evidence as versioned test/PR artifacts.

### 9.3. Acceptance matrix

This matrix spans the delivery milestones. M1 qualifies source-backed replacement,
publication, failure, bounds and retirement. Cooked reconstruction, source-access
denial and source-versus-cooked visual equivalence are M2 gates; gameplay service
activation and Play transitions are M3 gates. They do not expand M1 into a second
cooked loader or gameplay implementation. M1 still requires rendered source
fixtures, reference reclamation and its applicable timing/visual checks.

| Guarantee | Required exercise | Pass condition |
|---|---|---|
| Reconstruction | Fresh source/cooked process; UUIDs, components, hierarchy, camera and settings | Semantic values agree; runtime handles are rebuilt, not compared as persisted identity |
| Complete rendering | SampleProject opaque/transparent materials, textures, lights/environment | All required bindings ready; semantic checks and frozen visual profile pass |
| Source independence | Deny source/registry/import paths; monitor VFS access | A cooked scene renders with zero source/import lookups |
| Recoverable failure | Inject failure at each reservation/read/decode/bind/upload/preflight boundary | No partial publication; old bindings/generation/authored state preserved and next valid request succeeds |
| Exactly-once outcome | Deterministic barriers around Ready/commit; cancellation, newer request, shutdown | Exactly one terminal result; no stale publish; TooLate after commit |
| Editor consent | Edit/change source after consent while load is pending | Old consent cannot discard the new revision; existing source inspection/save rules remain intact |
| Thread/reader safety | Overlapping candidate build, old-world simulation, frame reads, and owner destruction | No mutable cross-owner access or callbacks into dead owners; race/sanitizer tests pass where supported |
| Retirement | Hold old CPU frame/readers and delay upload/render completion | No premature reuse; resources reclaim after their last owner/completion releases |
| Bounds/backpressure | Exact-cap and cap-plus-one scenes; saturated worker/result/upload/retirement queues | Within-cap input succeeds; excess fails/queues as specified without unbounded allocation or lost completion |
| Integrity/capability | Malformed/truncated objects, path escape, changed file, versions/targets, known-but-unsupported services | Stable diagnostic and no source fallback; decode uses verified bytes |
| Lifecycle stress | 100 warm-up then 1,000 alternating load/cancel/switch cycles, including shutdown races | Zero stale publications, lost outcomes, use-after-free or renderer validation errors |
| Reclamation | Repeat with eligible cache retention disabled; restore the same reference scene and drain uploads/readers | Tracked live allocations/pins return to the warmed reference baseline; reserved pools remain within caps |
| Performance | Timed runs in the frozen profile; trace both CPU critical paths and resource readiness | Per-stage, frame-stall, total-load and high-water limits pass; all outliers accounted for |
| Device loss | Inject device loss during preparation/retirement | Host enters fatal/device-loss policy; no false preserved-renderability or successful-fence claim |

Fault injection uses deterministic barriers and bounded test timeouts, not sleeps
as proof of a race. Exercise before/after commit and before/after upload submission.
Use supported sanitizer configurations for CPU memory/undefined behavior and
concurrency checks, plus Vulkan validation for GPU lifetime/synchronization.
If a tool is unavailable on one platform, record its alternative coverage and
limitation; a passing unit build is not a substitute for all lifecycle evidence.
After warm-up, allocator-owned live bytes must return exactly to the same reference
scene's baseline in the zero-cache test. RSS/driver residency is reported for
trends but not incorrectly required to shrink byte-for-byte like an explicit
allocation ledger.

### 9.4. Milestone 1 — Engine-owned scene lifetime and publication

The source-backed implementation uses `SceneStorageLease` and `SceneInstance` owning
bounded storage, the retained document, ECS, actors, and scene-local command/tick
state. The editor candidate delegates to that engine owner. The exclusive loader
worker prepares candidate hierarchy matrices and the editor instance adapter;
its mesh resolver returns checked values under the cache ingest lock, never
borrowed mutable cache pointers. No active world, actor callback, session or GPU
service is mutated by this candidate-local work. Editor replacement
uses a generation-guarded, prepared binding switch; history destruction and actor
callbacks run afterward under a sealed, synchronous legacy-retirement adapter.
The adapter temporarily exposes the outgoing engine/render views to legacy
callbacks without reactivating its generation or admitting scene/session work.
Submitted rendering continues through immutable snapshots.

When a render pipeline is attached, editor replacement now synchronizes the
candidate hierarchy, instance identities/transforms and lights, captures the
existing editor camera/view policy without outgoing picks or gizmos, and reserves
a complete immutable authoring-frame mailbox slot before entering the publication
guard. The guarded switch publishes that already captured slot with the new scene
generation; no scene copy, allocation or fallible mailbox claim occurs at commit.
A move-only producer-owned reservation releases unpublished capacity on rollback
and cannot be reclaimed by normal frame publication or consumer coalescing. Missing
camera or snapshot capacity preserves the outgoing scene. Headless hosts without
a render pipeline perform CPU publication only. This reservation establishes CPU
handoff; the preparation gates below establish GPU readiness separately. Later
same-generation frames may supersede the initial frame
under the mailbox's normal latest-state policy.

The editor resource request also copies a camera-independent initial-frame input
into its bounded request record: at most 8,192 instance transforms and opaque
entity identities, plus lights and grid values. Instance mesh identities must be
in the admitted sorted mesh set; non-finite transforms and out-of-range light
counts are refused before render preparation. The synchronous API validates the
complete input before claiming a record. The editor uses deferred input: admission
captures only bounded metadata, then each producer update validates and copies
at most 64 instances into the reserved record. No source span is retained between
updates. Partial input cannot queue render work, acquire a scene lease or expose a
prepared frame. Invalid batches fail the candidate, and cancellation releases even
a partially filled record through the normal bounded cleanup path. The unpublished editor adapter uses
its existing entity membership for linear-time transform capture, not repeated
linear instance-ID searches. No ECS or producer-array pointer crosses this handoff.

After exact geometry/material retention, the same shared 16-operation pump expands
one submesh or instance boundary per operation. It prepares geometry offsets and
ranges, reserved material-table indices, transform indices, world bounds and the
opaque/transparent classification. Actual retained submesh counts, not an earlier
mutable-cache estimate, must fit the 8,192-draw limit. Invalid/empty draw ranges,
non-finite derived bounds and overflow fail before readiness. Sphere bounds use
a conservative stretch bound under nested rotation/nonuniform-scale shear, with
outward-rounded radius and double intermediates rather than max column length.
Partial expansion
is never exposed as a prepared draw snapshot, and cancellation uses the existing
bounded resource cleanup. The two request records own fixed transform, identity
and draw arrays; they are not unaccounted per-load heap allocations.

The first rendered resource frame consumes that exact copied draw/light/grid
snapshot and transfers its geometry/material dependencies to accepted-submission
retirement. Reimporting the same UUID or coalescing authoring frames cannot silently
change this first draw layout. Acknowledging the first recorded frame ends use of
the initial draw snapshot; subsequent authoring frames resume the ordinary latest
state. Camera frustum extraction, transparent ordering and pick-request association
remain render-owner presentation work.

On GPU hosts, the candidate also allocates immutable transform/light/material/draw
storage and finishes its mapped host writes before becoming resource-ready.
Allocation and write refusal fail the unpublished candidate, without staging or
a GPU wait.
Allocation, batches of at most 64 transforms, and the light upload share the
existing preparation operation budget. Material uploads cover at most 16 entries
per operation and draw uploads at most 64. The material table preserves the
reserved shader indices, initializes holes to safe defaults, and never copies
unretained entries from another scene or preview. Asset previews use their own
material uniforms. Pick tokens are fixed from opaque entity identities and draw
indices, so picking/selection do not require rewriting immutable draw data.
Empty scenes receive initialized storage, an identity transform and their
authored light array. CPU-only hosts do not claim this GPU-buffer proof. The first
rendered frame imports these exact buffers and does not upload the immutable
tables again; normal authoring frames resume per-frame buffers after the first
recorded acknowledgement. Recording holds and every accepted consumer
timeline protect the candidate allocations independently of mailbox readers;
even an empty scene cannot bypass these gates. Device loss is not completion.
The candidate additionally allocates three independent culling input/output
pairs. Preparation initializes the inputs in opaque/transparent partitions from
the retained draw layout, in batches of at most 64 source draws; transparent
camera ordering and frustum planes remain presentation work. Camera-dependent
input updates use only the acquired idle frame slot. Compute fully writes the
device-local indirect output before graphics reads it; no host upload of that
output is needed. Graph imports and all indirect-draw callbacks select the same
exact output buffer. Cancellation/rollback frees at most one allocation per
remaining cleanup operation; joined-reader idle shutdown can drain all storage.
The two candidate records bound the number and size of these allocations, but
aggregate residency admission and elapsed-time calibration remain required.
The render owner prepares isolated descriptor banks for frustum culling, depth,
G-buffer and object-ID passes before admitting an initial GPU frame. Each pump
populates at most one buffered-frame bank using the retained initial storage,
global geometry buffers and explicit reflected binding recipes. Preparation does
not change the acquired frame or the outgoing pass's descriptor sets. Unknown
required bindings and failed writes refuse readiness; previous-frame writes do
not establish a new proof. Shader generation leases cover preparation through
activation. Adoption requires matching device/shader generation and complete
proofs for every buffered frame, transfers the bank without allocation or waits,
and defers the replaced pool. Ordinary graph preparation refreshes presentation
inputs before recording; candidate replay pointers are not copied into an active
pass. Cancellation and joined-reader shutdown drain unadopted banks.
The shared texture-set ABI bakes its device-owned wrap/clamp samplers into the
reserved layout as immutable samplers. Pass preparation validates the requested
canonical handle instead of updating a shared sampler descriptor, including when
other buffered frames still reference that set. A different sampler is refused;
pass-local sampler bindings remain independently writable under their owner.
The same isolated-bank pump also prepares forward-transparent storage, camera,
texture-array and sampler bindings, plus its exact retained diffuse irradiance,
specular environment and BRDF images. Environment backgrounds retain the prepared
radiance source, and sky-sphere/gizmo passes prepare their camera bindings. The
renderer supplies a borrowed, typed environment input; successful image writes
clone its leases into each buffered-frame bank. Missing required inputs refuse
the candidate without resolving outgoing handles or substituting fallback images.
Adoption transfers those image leases, not just descriptor handles; they survive
release of the preparation owner and follow accepted-submission retirement.
Lighting, sky-view LUT, aerial-perspective, sky-composite, tone-mapping and grid
banks use explicit pass-scoped image and sampler recipes. Lighting retains the
initial light buffer, four exact G-buffer/depth images and three IBL versions.
Atmosphere compute banks retain their static LUT sources and exact candidate
storage destinations; compositing retains all five graph inputs plus static
transmittance. Tone mapping selects ordinary or composited HDR from the captured
initial camera's atmosphere-view classification, including the below-ground and
invalid-view fallback. Grid retains the candidate depth image. Unknown names,
wrong descriptor kinds, missing pins and incomplete banks refuse readiness rather
than resolving outgoing resources. Grid-label banks retain the exact atlas
version and candidate depth image when the captured initial layout contains a
label draw; empty layouts omit the bank rather than requiring a UI font for
headless/runtime scenes. Main-thread font capture copies only U/V metrics, logical
viewport height and atlas identity into the initial view. Initial recording uses
that same capture rather than a newer mailbox font. Selection-outline banks bind
the candidate object-ID image, nearest-clamp sampler and candidate-owned
per-frame token-state buffers. Each buffer contains one entry per admitted draw
plus reserved token zero, initialized in bounded batches before readiness.
Initial recording imports those exact buffers and writes current selection
feedback only into its frame slot; it does not use the outgoing renderer's token
storage. They follow the same reader and accepted-submission retirement as the
other initial buffers. Camera-dependent presentation is prepared separately:
for a captured initial view, candidate culling inputs are partitioned
into opaque and transparent ranges before publication. Transparent indices use
the shared far-to-near ordering and original draw-index tie-break, prepared with
bounded heap-building and extraction steps in fixed candidate storage. Initial
recording uses those exact inputs and counts rather than sorting or uploading
them again. Uncaptured views retain ordinary camera-dependent recording and do
not claim prepared presentation. Camera constants and other required presentation
inputs are covered by the captured-view camera preparation below; end-to-end
readiness remains subject to the acceptance matrix. Asset-version pins deliberately reject
transient alias images. Explicit graph-image pins instead retain an exact image
object and its shared allocator allocation; backing and alias objects each retain
that allocation until their last image object retires. This preserves storage,
not pixel contents: graph barriers must still order writes and alias reuse.
Normal declared sampled and storage graph bindings, including attachment-kind
resources, carry these pins through descriptor/frame retention. Sampled and
storage writes share exact-image lease validation. Storage-image arrays retain
each element independently, preflight all requested views and lease capacity
before replacing descriptors, and preserve old bank leases on refusal. Replay
restores each element's exact lease; repeated raw callbacks preserve an existing
matching lease instead of dropping ownership. Prepared storage banks transfer
those leases on adoption and acquire accepted-frame holds only when verified as
recording inputs. These mechanisms preserve objects/allocations, not contents;
write/read visibility and alias ordering still belong to graph synchronization.
Descriptor replay distinguishes an interface that has previously passed complete
verification from partial startup writes. A resize while the first asynchronous
pipeline is pending may bind graph resources before callback uniforms/samplers;
those unverified replay records are discarded, not mistaken for a shader reload.
Normal preparation must still prove every required binding before recording.
Starting a new frame does not erase prior interface verification or permit an
incompatible shader reload to bypass replay validation.
Storage-image leases alone do not establish complete candidate graph readiness;
attachment preparation below covers framebuffers/views, and the camera and
backend preparation establish their separate required proofs.
Core depth, G-buffer, object-ID, HDR, final-color, atmosphere-view LUT/volume and
composited HDR images now have a shared
typed schema used by ordinary graph registration and candidate preparation.
Preparation allocates and retains at most one image per descriptor-preparation
pump before starting buffered-frame banks. Incoming images do not replace
outgoing pool handles or descriptors. Activation transfers exact prepared images
into pre-reserved, reusable pool metadata and queues old framebuffers/images for
retirement without allocating replacement images or waiting for device idle.
An incoming retained viewport with a matching ready plan bypasses the outgoing
graph resize path. Cancellation/refusal releases unadopted images; adopted images
remain pool-owned after preparation leases end. Atmosphere-view storage retains
its fixed 192-by-108 LUT and 32-cubed volumes independently of viewport extent;
composited HDR retains the captured viewport extent. Ordinary pass declarations
and candidate allocation use the same format, shape and usage recipes.
Required graphics profiles also declare ordered, typed attachment targets rather
than guessing image identity from formats or pass names. The transparent profile
alone redirects its HDR attachment to composited HDR when the retained initial
view requires it. Before descriptor-bank preparation, a bounded attachment pump
validates exact candidate default views and extents and prepares at most one
legacy framebuffer per operation. Dynamic rendering validates and retains the
same attachment-view metadata without creating a framebuffer. Readiness requires
this stage as well as targets and descriptor banks. Activation swaps a reusable
spare framebuffer into the graph; cancellation retires unpublished framebuffers
before releasing image-view pins. First compilation must consume those exact
views/extents and, for legacy rendering, the same prepared framebuffer. It refuses
a mismatch rather than allocating a fallback after publication. Compatibility
between the prepared and live render-pass handles is checked against this
engine's canonical single-subpass construction, including ordered formats and
dependencies; it does not assume that formats alone establish arbitrary Vulkan
render-pass compatibility. Load/store and layout differences do not prevent reuse.
Attachment recipes also specify load operations and depth-read versus output
roles. Pipeline preparation creates the exact load/store render-pass description;
candidate graphics backends borrow that graph-owned attachment instead of
creating another native render pass during first compilation. A bounded backend
pump initializes private graphics/compute objects, reserves their binding and
attachment-list storage, clears stale replay records before spare reuse, and
prepares their pipeline versions. It never replaces or rewrites the outgoing
backend while the candidate is unpublished. Activation swaps the prepared backend
into each required persistent slot, retaining the outgoing object as a reusable
spare. Repeated activation is idempotent. First compilation refuses missing
required backends and mismatched attachment operations rather than creating a
fallback. Backend disposal releases its pipeline and owned attachment, not
borrowed render-target handles; graph shutdown drains active and spare backends
before their graph-owned prepared attachments. Compute-only preparation follows
the same ownership transfer without requiring an image plan. Backend and
prepared-descriptor object reservations, binding-name tables, target lists and
pipeline-wrapper storage use checked arena admission. Private graphics backends
clone the admitted scalar/vertex-input description with checked array copies;
graphics and compute backends use the exact admitted shader rather than rerunning
callbacks or shader lookup. Partial objects are destroyed before their exclusive
arena transaction rewinds, and only complete metadata is retained for reuse.
Descriptor replay stores at most 64 records and 4,096 aggregate name bytes inline
per pass. Updating an existing record consumes no further name storage; an
exceeded record/name limit invalidates preparation instead of silently losing
replay coverage or asserting on an arena string allocation. Spare reset reuses
that storage. These are finite metadata bounds, not measured timing guarantees.
Graphics pipeline state assembly uses fixed stack storage for the existing PSO
limits of 16 vertex bindings, 32 vertex attributes and eight color blend states;
it does not allocate or rewind shared render scratch. Excess counts fail
preparation before cache/native requests. Graphics and compute pipeline layout
admission validates bounded set-layout/push-constant recipes and returns native
or cache-capacity refusal instead of asserting. A native failure removes its
unpublished layout-cache reservation so a valid retry cannot hit a null cached
handle; existing layouts and outgoing pipeline pins are unchanged. Device loss
enters the terminal device policy. Compatibility-render-pass extraction and
cache/native creation are also checked: indexed recipes must be contiguous,
formats and reference layouts must match their color/depth roles, and the bound
is eight colors plus one depth/stencil attachment. Native creation checks device
color-count and format capabilities; unsupported formats and native refusal
release their unpublished cache reservations for retry without modifying active
handles. Hits reuse the retained object without native creation or repeated
format queries. Empty compatibility subpasses remain legal. Warmup records use
an explicit format version and reject older binary layouts after key-shape
changes rather than reinterpreting their bytes. Shader initialization and
descriptor-layout admission use the checked paths described next; these local
transactions do not qualify every legacy shader/cache caller.
Descriptor-layout cache admission has a checked entry point with bounded recipe
validation and fixed stack metadata. Its exclusive render-owner transaction
reserves immutable-sampler list identities and native-layout cache entries;
failure removes only reservations first created by that request. Older layouts
and their immutable metadata remain intact. Native refusal and cache saturation
retain their exact status, unsupported binding features fail before native
creation, and a device-support query gates new native layouts. Success commits
both reservations together; a cache hit does not run native creation again.
Shader factory admission uses checked unpublished initialization. Each shader
slot retains one reusable 5 MiB child arena, including a private 1 MiB binary-read
scratch child; failed retries do not carve another child or rewind device-owned
storage. Shader name/stage paths are retained inline, not allocated from shared
device scratch. Exact bounded SPIR-V reads preserve output and scratch cursors
on failure. Shared stage reflection checks entry-point identity, descriptor
set/count/type/stage compatibility and push-constant ranges; it does not silently
truncate descriptor arrays or omit unsupported descriptor classes. Reflection
metadata, descriptor layouts, pool creation and set allocation use checked
admission. Unpublished native modules/pools are released directly on refusal,
without charging the accepted-work retirement queue; published resources retain
their existing timeline/async ownership. The factory removes failed handles and
does not publish their cache entries or mutate the caller's recipe. Device loss
enters terminal device policy. Legacy direct initialization/reload wrappers still
assert on failure; this is not a failure-preserving hot-reload contract. Samplers
used by scene preparation remain borrowed from device-startup state, not
allocated anew for each candidate. These finite local bounds do not establish
aggregate shader/compiler memory or end-to-end first-frame timing qualification.
Graphics-description callbacks expose checked admission with output/cursor
preservation on refusal. Unqualified callbacks fail closed; all built-in graphics
passes implement the contract. Vertex-input recipes use checked reservations;
description and pipeline-wrapper metadata are admitted together before shader
lookup. That exclusive recipe checkpoint never spans shader/VFS work that can
retain independent allocations. A failed first shader lookup retains its bounded
pipeline recipe and later requests retry the lookup instead of permanently
reusing a wrapper with a missing shader. Outgoing backends remain unchanged.
Candidate framebuffer admission reserves wrapper storage before native creation.
Metadata/native refusal preserves output and the recipe cursor, including an
existing empty spare wrapper; a live wrapper is never overwritten. Valid retries
reuse the spare. Device loss prevents further native admission. These local
transactions do not establish aggregate retirement limits or timing acceptance.
Native render-pass preparation has a checked path shared with the legacy
asserting constructor. It validates contiguous recipe indices, enum ranges,
resolved formats, one depth/stencil role and canonical dependency endpoints.
Vulkan descriptions use fixed stack storage for at most eight colors plus one
depth/stencil attachment and 32 dependencies, not scratch-arena growth. Dynamic
rendering validates the same recipe without creating a legacy object. Native
errors retain their exact status and device loss enters the terminal device
policy. Candidate attachment-table/object admission is checked and rolls back
partial arena reservations; a native refusal retains its bounded recipe for
retry without constructing a pipeline or modifying an outgoing backend.
Exact subresource image-view lookup uses checked admission. It validates the
image range and supported view type, reserves cache metadata before native
creation, and retains at most 64 additional views per image within the shared
image-view slab. Default views do not consume this additional-view allowance.
Metadata/native refusal leaves existing handles usable; native failures retain
their exact status and device loss enters the terminal device policy. Cache hits
need no allocation, including when the per-image allowance is full. The cache is
render-owner-only, not a concurrent mutation API.
Image/framebuffer/backend adoption is one part of the first-frame gate, not a
substitute for captured presentation inputs, exact resource retention and producer
completion. Full failure/lifetime/budget qualification remains an acceptance task.
GPU-backed editor requests capture an immutable initial camera, viewport extent,
resize sequence and grid visibility at resource admission. A zero drawable extent
defers admission. The render owner resolves this view before resize and recording
until the first prepared frame records, without modifying the retained latest
mailbox view; a coalesced newer resize remains available on the following frame.
Headless requests do not establish viewport readiness. This captured identity is
an input for candidate preparation; readiness additionally requires the camera,
descriptor, attachment and backend proofs described in this section.
Captured views also prepare an immutable, candidate-owned camera uniform buffer
before readiness. Its view-projection inverse uses checked inversion; singular
or non-finite camera data refuses publication. Allocation and mapped writes are
separate bounded preparation steps. Camera storage follows the initial bundle's
reader and accepted-submission retirement, including cancelled preparation.
Prepared UBCamera banks bind that buffer at dynamic offset zero. A scoped camera
source preserves the same binding when first-recording callbacks refresh their
descriptors; other uniforms, including asset previews, still use their ordinary
heaps. Replay retains the explicit buffer identity. Initial recording neither
inverts the captured camera nor pushes it into the shared frame upload heap;
ordinary subsequent frames return to their per-frame camera heap. Captured-view
descriptor admission rejects a missing camera buffer instead of silently using
the outgoing heap. These guarantees do not establish aggregate memory/timing
qualification or qualify unrelated legacy allocation callers.
Viewport resize prepares new root/alias image versions before switching the
pool, keeping old pinned views valid rather than reconstructing them in place.
Handle-capacity refusal rolls back unpublished replacements and preserves the
previous graph extent. Both legacy and dynamic-rendering attachment lists refresh
their exact handles after rebinding. Root replacement and candidate sky images use
recoverable texture creation: invalid extents/capabilities, full handle tables and
image-allocation metadata/VMA refusal unwind unpublished handles and allocations.
Device loss retains its fatal identity rather than becoming ordinary capacity
pressure. Default image views are part of this allocation transaction; additional
subresource views use the checked view-cache admission described above. Resize
retains its device-idle slow path; this is not asynchronous resize or complete
aggregate GPU-memory admission.
Resize refusal does not acknowledge the mailbox resize sequence or record a scene
into mismatched old targets. Resource/presentation servicing continues, and the
same retained request can retry after pressure is relieved.
The prepared-bank handoff alone does not complete the strict readiness gate.

Mesh-release requests from scene removal now mark bounded UUID-map registrations
under the map lock; only the render thread drains them and mutates slots/pool
regions. Requests coalesce in place without a separate allocation/overflow path,
and generation checks protect reused slots. This removes producer-side geometry
mutation.

The renderer also exposes move-only, render-thread-owned geometry pins. Each pin
retains immutable region offsets, counts and a monotonic content-version identity
independent of the mutable public mesh handle. Hot reload can publish a new mesh
version while an older pinned region remains allocated. Release/reset detach
pinned regions until their last pin retires; eviction skips pinned current
versions. In-place compaction conservatively defers while any version is pinned.
A fixed table admits at most 4,096 distinct pinned versions, including detached
content; additional references to one admitted current version share its record.
Saturation refuses new pins without evicting admitted versions. Retained geometry
stays charged to the existing geometry pool, with bounded free-list metadata sized
for both live slots and detached pinned versions. Pins must retire before the
resource manager shuts down; explicit caller-owned pins must be released by their
owners.

Scene and preview draw preparation automatically retain one renderer-owned pin
per geometry version, shared across instances and frames. Accepted render-graph
queue submissions and the final frame submission extend its timeline gates. A
version retires only after every accepted gate completes and no recording frame
still references it. Aborting presentation or recording does not discard gates
from already accepted submissions. Nonblocking polling also runs on skipped
frames, and timeline owners must unregister before destroying their semaphores.
Shutdown drains renderer-owned pins after the existing device-idle wait.

Each uploaded geometry version also owns an immutable CPU draw-metadata block:
submesh ranges/material UUIDs and local bounds are copied under the same ingest
lease as the uploaded vertices/indices. Scene and preview draws use that retained
layout instead of resolving the latest mesh payload by UUID. Replacement, release,
reset and compaction reclaim metadata only when its owning geometry version can
retire; shutdown releases it before the asset cache. Blocks use fallible allocation
from the existing 256 MiB asset-cache container slab, with no new arena reservation
or heap fallback. Invalid ranges or slab exhaustion refuse the upload rather than
publish a geometry/layout mismatch. This retains no vertex/index CPU copy and no
material contents: outside the retained bundle described below, captured material
UUIDs still resolve the material cache during draw admission.

The resource manager now exposes explicit render-thread texture-version and
material-version leases. A texture lease retains its generational bindless handle,
image identity and dimensions/format; repeated readers share one fixed record per
texture slot (at most 8,192 slots, with out-of-range admission refused). Destruction
seals further admission for that generation and waits for the final lease before
entering the existing deferred-disposal path. Stale destruction cannot mark a
different retained generation. A fixed retirement bitmap keeps final-pin releases
lossless when the producer release queue is full; pumps admit at most 16 such
retirements. Aliased views/aliasable backing are refused because a view lease alone
cannot retain or freeze their allocation owner. Material leases copy shader values and own up to
five texture leases in caller-owned fixed storage; absent explicitly indexed maps
fail with rollback, while sentinel optional maps remain absent. UUID lookup copies
under the ingest lock, so no mutable cache pointer escapes.

Explicit callers must retain leases until all submitted consumers retire. Initial
texture work/descriptor publication conservatively blocks first admission. A
previously admitted image can be retained again during unrelated work, even after
its final earlier lease retired; managed image writes invalidate that marker before
mutation, and a changed handle generation cannot reuse it. Acquiring a lease does
not prove completed uploads or consumer visibility/queue ownership. Managed
upload, reconstruction and image-write paths refuse mutation of retained versions.
Asset UUID reloads now prepare copy-on-write images in 16 fixed replacement records,
leaving the published handle and existing leases untouched during decoding/upload.
Publication requires an accepted producer ticket, observed timeline completion and
drained decode/upload/descriptor work; it switches the UUID mapping, matching loaded
texture descriptor and all five authored material-map references under the cache
lock. The streaming ticket stays available for the graph's consumer acquire; this
cache switch is not itself proof of shader visibility or queue ownership. Reload
signals supersede older unpublished candidates, and pending deletion or a changed
expected handle rejects publication. Failed/cancelled candidates retain their
worker/producer dependencies before retirement; upload queue rejection/cancellation
reports failure rather than leaving the decode observer pending indefinitely.
Release-queue saturation retains the replacement record for retry instead of losing
its retirement target. Old images enter normal deferred disposal only after their
explicit leases retire; global fallback/other UUID-shared handles are not retired by
one UUID's replacement. Swapchain cancellation discards/retries unpublished reloads;
shutdown drains replacement records after the existing idle wait. Storage and GPU
allocations remain charged to the existing fixed tables, decode slabs and texture
pool; this is not aggregate memory admission. The readiness guard is conservative
and global, so unrelated continuous texture work can delay publication. Initial
texture ingestion and explicit low-level unpinned image updates keep their existing
paths.

Scene draw preparation and material previews now automatically retain material
values and their texture leases. A fixed 4,096-entry table deduplicates exact shader
fields (without reading structure padding) through bounded hash buckets; repeated
instances/frames share one admitted version. The draw material ID, blend routing
and per-frame GPU material upload use the same immutable copy, not a later upload
of the mutable asset cache. Uploads batch contiguous admitted entries in fixed
16-material scratch storage. Changed values/maps admit a different entry, including
copy-on-write texture publication while older submitted draws remain alive. Existing
retained versions can be reused during unrelated uploads without freezing a new
image. Saturation or an unretainable explicit map refuses that draw; the legacy
missing-material fallback is an explicit opt-in, not substitution for a known
material whose lease failed. Required scene admission uses the stricter policy
described below; ordinary editor drawing preserves its compatibility behavior.

Geometry and material/texture versions share resource submission gates for every
accepted graph queue and the final frame. Cancelled recording releases only
unsubmitted work; aborted presentation preserves gates from accepted graph work.
Skipped-frame polling and semaphore-owner unregister checks cover both resource
types, and renderer-owned leases drain after the existing shutdown idle wait.
This establishes draw lifetime as one layer of the first-frame publication gate.
The resource-preparation service acquires material/texture leases as well, and
editor source-open publication now requires that result and the environment
preparation/activation handoff described below, including its pipeline gate.
Candidate descriptor banks establish the required binding proofs separately.
Consumer visibility/ownership remains the graph's submission-time acquire/barrier
responsibility, not a claim made by a CPU lease. Aggregate profile qualification
remains required.

The resource manager also provides a resource-preparation service with two
fixed request slots, each admitting at most 4,096 sorted/deduplicated
mesh UUIDs. Inputs are copied into resource-manager-owned storage; no worker or
render consumer borrows the requesting scene. Requests carry the load identity,
expected generation and a non-reused preparation serial. The render owner shares
a 16-operation budget per pump across acquisition and cleanup, alternating which
request runs first. Polling also runs on skipped acquisitions without a GPU wait.
`GeometryOnly` rejects missing/nonresident geometry. `MeshMaterials` restores
missing render registrations from explicitly loaded CPU-cache slots and requests
residency for evicted meshes. CPU slot zero is valid; scanner placeholders do not
qualify. Missing CPU bindings, registration/geometry capacity exhaustion or failed
reloads fail explicitly, without truncating or substituting required meshes.

Residency requests coalesce by render-slot generation, so shared meshes and repeated
instances occupy one queue entry. The fixed queue admits at most 255 distinct
reloads; saturation leaves the candidate preparing and retries later. Each drain
visits at most 16 entries, including stale ones. A replacement generation cannot
be mutated by an earlier queued request, and a newly reloaded version receives
one eviction grace cycle so the preparation pump can pin it. Failed attempts
terminate waiting candidates; a fresh request can retry. Cancelling a candidate
does not borrow its storage into queued cache work or cancel another scene's
shared residency request. This bounds operation counts, not upload bytes or
elapsed time; byte admission and timing qualification remain required.

Preparation retains each acquired geometry version and conservatively gates it on
every upload batch recorded before acquisition, including a still-open batch.
`Resident` alone is insufficient: geometry-ready requires both accepted submission
and observed upload-timeline completion. A rejected submission/device loss cannot
become readiness through a later timeline signal. Release/cancellation requests
do not free pins or request storage until their accepted upload gates retire;
shutdown seals admission and drains these service-owned pins after the existing
idle wait without taking ownership of unrelated explicit pins.

`PrepareSceneResources` explicitly distinguishes `GeometryOnly` from
`MeshMaterials` content. The latter walks the pinned geometry's immutable submesh
metadata, retaining at most 4,096 distinct material UUIDs per request in fixed
storage with a half-full open-addressed index. Shared materials retain one copied
shader value set and up to five texture-version leases. Each submesh visit
(including duplicates), mesh boundary, geometry acquisition or material release
consumes the shared operation budget. The added material/index storage is about
2.3 MiB across both request slots, with no new arena or heap fallback. Missing
metadata/materials, unretainable explicit texture maps or capacity exhaustion
fail without exposing a partial bundle or substituting fallback resources. This
service prepares loaded CPU dependencies; it does not decode absent assets. Pending
texture decode/descriptor work defers acquisition rather than being reported as a
missing dependency. Accepted texture producer tickets are queried nonblockingly
before retention completes; the graph still owns their later visibility/ownership
acquire. Direct batch copies conservatively extend the geometry upload gate.

Mesh/material scene preparation uses `RequiredSceneBindings`, distinct from the
ordinary editor's `ExistingBindings` retention policy. It validates all five
authored texture-map identities and GPU indices under the cache lock. Only a map
with no authored UUID/path and no GPU binding is an absent optional map. A lost
reference, path without identity, untyped GPU binding, missing typed texture,
debug fallback, mismatched index/generation or failed texture decode refuses
preparation. Successful retention checks the actual held image generation against
the copied identity; partial retention rolls back. Structured material errors are
published with the failed preparation result. Missing materials and invalid or
failed required bindings fail even during unrelated texture work; only temporary
retention unavailability can defer behind pending uploads/descriptors. This does
not establish sampled-mip completeness, environment requiredness or pipeline and
consumer visibility readiness.

Initial material texture ingestion now requests decode observation as well as
copy-on-write replacements. Observation is indexed by the bounded 8,192 texture
slots and checked against each handle generation; CPU slot zero and texture
generation zero remain valid. Worker failure and accepted-upload success update
the fixed state directly under a short lock, replacing the completion ring and
its 64-entry observation table. Completion cannot be lost to ring saturation,
stale generations cannot alter a replacement, and success cannot erase a reported
failure. This reuses the former ring's storage scale without a new arena. Success
still means an accepted upload, not observed GPU completion or consumer acquire;
the preparation service must continue to establish those separate gates.

`ResourcesRetained` means that leases, observed producer completion, and geometry
upload gates are established, not that the complete descriptors/pipelines and
consumer visibility/ownership are ready for shader use.

Streaming upload tickets retain the producer's old/new release-layout pair.
Across queue families, the graph's acquire repeats that pair and the producer
family; within one family it preserves the already transitioned final layout.
The first consumer supplies its actual stage/access mask, and a ticket is
acknowledged only after the acquire-bearing submission is accepted. This
upload route transitions and releases every mip/layer, matching the graph's
whole-image acquire; copying base pixels does not make generated mip contents
ready before the environment-bake gate. The protocol still requires
separate-family device qualification; producer timeline
completion alone does not establish consumer ownership.

A separate fixed producer-completion record per admitted texture slot retains
the exact generation and accepted timeline/value after acquire acknowledgement.
Preparation clears that gate only after observing completion; submitting a
consumer that waits on the upload cannot prematurely mark its inputs ready.
Records do not grow with requests or frames and are cleared before producer
semaphore destruction at idle/device-loss shutdown.
Texture retirement uses the same completion proof even when its acquire ticket
has already been acknowledged. Removing that ticket cannot admit destruction
while the upload is still in flight; errors retain the gate until successful
observation or the joined idle/device-loss shutdown path.
Direct device destruction also retains accepted producers without requiring a
remaining scene/material pin. Pending producer releases enter the existing
lossless retirement bitmap, seal new pin/write admission, and reach native
disposal only through the render-owner completion gate. The version mutex
protects completion records shared with other release owners; Vulkan polling
occurs outside that mutex and cannot clear a replaced producer's proof.
For live handles within the admitted texture-slot range, native disposal-queue
refusal restores the retirement bitmap entry instead of losing destruction.
Repeated saturated pumps retain sealed admission and retry without allocating
overflow storage; successful queue admission consumes that retry intent.
Recorded texture jobs reserve their command slot and independent staging buffer
until submission or explicit rejection. Cancellation, queue-capacity refusal and
rejected submission reclaim the exact timeline/value slot immediately rather
than waiting for a signal the rejected job cannot produce. Both completion and
rejection share the same command/staging reset path. Rejection invalidates the
unsubmitted host layout, and admitted upload slots always use generation-scoped
failure observation, even when the file caller did not request decode tracking.
A failed generation cannot acquire a scene lease merely because its pixels were
decoded or its command recording completed.
Before recording, each asynchronous upload claims the same bounded texture-slot
lifetime record under the version mutex. Pending submission blocks new writes,
duplicate uploads and scene pins, and retains the native image if another owner
requests destruction. Submission transfers the hold to its accepted timeline;
rejection releases it only after command/staging reset and failure publication,
preserving any earlier accepted producer proof. Invalid input and unavailable
command capacity claim no hold. Async uploads beyond the admitted lifetime-slot
range are refused rather than bypassing retention. The private device copy path
is accessible only to the resource manager after this admission; ordinary callers
continue through the public version-write gate. This covers recording/queued
lifetime, not aggregate upload admission or separate-family device qualification.
The per-generation record also retains an unconsumed-acquire flag and a
conservative graphics-upload policy. Initial uploads of fresh images may use
transfer; once graphics has acquired/used an image, reuploads remain on graphics
instead of silently omitting an ownership handback. Rejecting a recording may
invalidate its host layout but cannot reset that policy. Graphics overwrites
order against prior queue reads/writes through an all-commands memory barrier.
Ordinary writes and reconstruction observe the accepted producer's completion
outside the version mutex and recheck the exact admission state under it. They
also refuse an outstanding acquire even after producer completion. Acquisition
acknowledgement and producer completion remain independent proofs; neither alone
authorizes a rewrite. The conservative write-availability query may report busy
until a writer/preparation pump observes completion. This does not add a general
bidirectional transfer protocol for arbitrary compute-owned images.
Pending decode dispatch and worker entry/completion inspect synchronized release
identities, not native texture tables. An admitted release also seals new writes
under the upload-admission mutex, closing the race after a deferral's preliminary
cancellation check. Cancellation need not interrupt the codec;
a worker already decoding may finish, but its result cannot revive a released
generation. The render-owner deferral pump distinguishes temporary pin/command
pressure from terminal stale handles, destruction, malformed sizes and failed
decode/descriptor observation. Terminal records release pixels and their decode
slab lease instead of entering an endless retry. Explicit deferral discard also
publishes terminal failure. A shared cleanup path consumes ownership once, and
late failure publication cannot modify a reused generation's decode state.
Frame cancellation and timeline reset preserve already accepted, unconsumed
texture tickets. Their producer semaphores remain owned until shutdown; observing
completion or waiting idle cannot replace the graph's required consumer acquire.

Queue selection honors the callback's requested role even after a native backend
exists. A compute pipeline does not override a graphics-queue scheduling policy:
environment baking remains on the graphics/upload-consumer/publication timeline,
while callbacks requesting asynchronous compute retain that role on capable
devices. Callback-free backends still infer their role from pipeline kind, and
occlusion-query writers remain graphics-only.

The sky lifetime model now supports explicit candidate publication. An accepted
candidate leaves the outgoing presentation config, celestial-light snapshot and
frame-selected environment unchanged. A completed GPU bake remains owned in a
bounded prepared snapshot until exact-revision activation, cancellation,
supersession or shutdown. Prepared and outgoing snapshots share the existing
environment memory budget; outgoing frame pins and timeline retirement remain
in force after activation. Explicit completion cannot bypass submitted GPU stage
completion or substitute unbaked texture generations, lighting metadata or failed
required inputs. SkySphere may prepare the engine-defined fallback without a
bake, but it is not selected until activation.

Editor candidates now resolve hierarchy and celestial-light inputs from their
own world before copying an environment request into the bounded resource
preparation record. Geometry/material retention alone no longer admits that
request to publication or the frame mailbox: its environment must also be ready.
The render owner holds a request lease through asynchronous source/GPU work and
keeps the prepared snapshot until the matching scene-generation frame activates
it. Outgoing frames cannot supersede the candidate's bake. A retained mailbox
frame also prevents a later candidate from stealing its prepared environment;
cancellation without a frame drains submitted stages before releasing request
storage. The recorded-frame acknowledgement requires successful activation.
The first selected frame uses the exact prepared input even when a newer editor
edit coalesces the initial mailbox; subsequent frames can apply that edit.

Atmosphere source radiance, diffuse irradiance and specular prefiltering record
one cubemap face per GPU batch, without reducing resolution, mip coverage or
sample counts. A matching recorded revision/stage/face and successful submission
are required before its completion timeline can advance the face cursor. All six
faces must complete before advancing the stage; partially written images retain
their contents and storage layout between batches. Only the final face transitions
the completed image to sampled layout. Each completed batch is a cancellation
boundary, and no partial environment may satisfy publication readiness.

Renderer-owned monotonic revisions distinguish scene generations from their
scene-local sky revisions, so a new scene's lower authoring revision is not
mistaken for stale input. HDRI source identity and native artifact path are copied
under the ingest lock; artifact reads and upload scheduling happen outside it.
Required environment failure preserves the outgoing CPU world and reports a
specific preparation error. This connects the lifetime model to editor source
publication. The pipeline gate below and the candidate descriptor preparation
above extend that handoff. Consumer visibility remains the graph's responsibility;
aggregate profile qualification remains separate.

The shared graphics/compute pipeline builders now expose callback-free asynchronous
preparation using the same canonical descriptions as synchronous baking. Cache
results distinguish pending compilation, retryable bounded-capacity backpressure,
unavailable worker service, ready variants and terminal compilation failure. Failed
entries remain observable until normal invalidation/age eviction instead of being
silently rescheduled by polling. A ready pipeline is pinned before replacing a
pipeline object's existing borrow; pending or compile failure preserves that
borrow. Descriptor replay failure cannot report readiness. Immutable cache jobs
do not retain a callback into a disposed pipeline object. This is a preparation
primitive. The scene gate below uses it for the host-declared variant set;
descriptor/version leases and accepted-submission retention provide the separate
consumer ownership proofs described in this section.

The graph now accepts explicit host scene-pipeline profiles, including variants
whose callbacks are inactive in the outgoing frame. Profiles declare usage and
the exact ordered attachment formats, including depth. A single render-owner
preparation transaction admits at most 32 declared variants and polls a caller-
bounded number per pump. It reuses one persistent recipe per declared pass,
deduplicates compilation through the existing PSO cache, treats capacity pressure
as retryable and compilation failure as terminal, and holds shader-generation
leases until explicitly ended. Ending or cancelling releases those generation
leases without dropping the bounded cached PSO borrows or leaving callbacks into
the transaction. Profiled graphics passes reject attachment-format/order drift
before pipeline baking and recording. The default renderer declares its core,
sky and environment-bake profiles. An operation-count slice is not proof of the
elapsed-time acceptance budget.

Candidate environment requests now start that pipeline transaction before bake
scheduling. Selection includes core renderer/editor profiles, the candidate's sky
mode and bake kernels, and view-local atmosphere profiles only when the host
supports them. Both exact pipeline preparation and the exact environment snapshot
must be ready before the resource request admits publication/mailbox retention.
Missing pipeline service, rejected admission or terminal preparation failure
reports `RequiredPipeline` and preserves the outgoing scene. Cancellation releases
the transaction after environment GPU work has drained. Activation keeps shader
generation leases until a successful first-frame recording; rejected/minimized
frames cannot release them, advance the next candidate, or replace the prepared
sky with a coalesced authoring edit. Profiled graph passes use asynchronous cache
lookup instead of synchronous first-use compilation, including hot-reload misses;
pending/backpressured compilation rejects recording without a false frame
acknowledgement. Descriptor contents, accepted-submission retention and graph
consumer visibility are separate proof layers; pipeline readiness alone is not
the complete readiness gate or a production-acceptance claim.

An asynchronous pipeline preparation now leases the shader generation, including
its reflection and descriptor-layout metadata. Normal hot-reload requests coalesce
while any preparation owns that generation; releasing the last lease only makes
the reload eligible for the next render-owner safe point, never an inline reload
from a destructor. Pending preparation therefore cannot silently adopt a newer
host shader generation. Disposal releases the lease while immutable cache jobs
retain their existing module-drain ownership. Shader generation exhaustion refuses
reload instead of wrapping to an older identity. This transaction pins a host
generation; descriptor contents are retained by the separate candidate/pass banks.

Bindless texture publication now processes at most 64 queued real updates per
render-owner pump, using fixed temporary storage for image infos and descriptor
writes. Deferred fallback/real pairs wait behind the existing real-write backlog
and are likewise sliced to 64 requests. Both queues reject retired handle
generations before writing; a deferred fallback cannot target a reused slot.
Remaining queue entries continue to block conservative initial texture admission.
Deferred admission uses a multiple-producer queue because texture decode workers
can finish concurrently. Capacity rejection is explicit and makes a tracked load
fail; the already-owned upload still drains, and its success cannot erase that
failure or falsely establish descriptor readiness.

The shared bindless targets are now registered during device initialization and
sealed before scene admission, rather than discovered by the first draw callback.
Unknown late targets are rejected. A fixed per-texture publication ledger records
the exact live texture/image generation, view, declared layout and monotonically
versioned contents after writes to every shared buffered target. Queue emptiness
alone is not a proof. Preparation queues a missing exact publication through the
bounded flush and remains pending; invalid targets/identities, device loss or
exhausted queue/version capacity fail without producing a publication token.
Idempotent real writes preserve existing tokens. Deferred fallback writes cannot
overwrite a texture while an immutable texture lease retains it.

`MeshMaterials` candidates require these tokens after their texture producer gate
and before reserving a frame-material entry. Failure reports `RequiredDescriptor`
without replacing the outgoing world. Tokens accompany the exact image leases
through material moves, frame clones and existing accepted-submission retirement.
Render-owner handoff refuses stale tokens without modifying output snapshots.
This proves authored-material sampled-image descriptor contents in the fixed
shared ABI. Pass-local buffers/images and sampler contents use the candidate-bank
proofs above. Required sampled mips depend on producer/environment completion;
consumer visibility/queue ownership depends on graph acquires and barriers.
Descriptor-update legality on every supported device capability profile also
remains part of Vulkan/platform qualification.

Graph compilation now rejects unusable pipelines and missing required descriptor
bindings for both graphics and compute passes instead of ignoring verification.
A pass starts a fresh, bounded descriptor-write proof after pipeline resolution
and before automatic resource synchronization, callback preparation and declared
resource binding. Successful writes must cover the active buffered frame and
shader generation; names retained from an earlier frame are not sufficient.
Non-bindless arrays require every reflected element (up to 64), and the sealed
shared sampled array is an explicit separate case. Capacity/type/count refusal
or any failed binding attempt rejects that preparation, even if the callback
later writes the same binding successfully. Descriptor replay restores a shader
interface but cannot stand in for this fresh-frame proof.
A rejected graph returns no recorded output, does not acknowledge the candidate's
first frame, and does not replace the viewport output or enqueue sky-submission
callbacks. The application retains its open primary buffer to finish the acquired
image's presentation bridge. This closes a false-success path; the separate
candidate-bank pump establishes prepublication preparation. Vulkan-backed passes own bounded
descriptor banks independently of the shader's legacy sets. Each bank allocates
only shader-local sets, with at most eight set positions, three buffered frames
and 64 distinct local bindings/64 elements per local binding. Reserved bindless
sets remain device-owned and shared only when the shader uses their actual
layout; an empty shader-layout gap at a reserved index remains local. Graph
shutdown explicitly releases bank storage even for arena-owned backends whose
destructors do not run. Set writes and command recording resolve
the same pass-owned bank; two passes using one shader cannot overwrite each
other's local bindings. A generation change invalidates old lookups, and a
replacement pool is published only after every allocation succeeds. Replaced
and disposed pools follow the device's existing timeline-deferred free path;
unexposed failed drafts can be destroyed immediately. Bank storage alone does not
pin its images, buffers or shader generation, and frame rewrites still require
the host's buffered-frame fence.

Sampled-image bindings can now explicitly clone an exact texture-version lease
into each buffered frame's pass-owned bank. Repeating that same binding through
an ordinary callback preserves its lease; changing the image replaces the lease
only after a valid descriptor write. Shader replay uses the retained version,
not a new UUID-cache acquisition. Successful binding verification transfers a
separate hold into render-owner frame retirement. A fixed, direct-indexed table
admits at most 8,192 exact versions, with a sparse active-slot list for submission
and collection. Repeated uses share one entry, pending recordings prevent early
collection, and every accepted queue/final-frame gate must complete before the
entry releases. Discarding a descriptor bank cannot discard that submission hold.
Extending a prior exact lease does not reopen admission after destruction or
shutdown has sealed new acquisitions.

Before reporting environment readiness, the renderer transactionally retains
the prepared candidate's source radiance, available static atmosphere images,
and selected diffuse/specular/BRDF lighting images in six fixed slots. Missing,
failed, alias-backed or otherwise unsupported inputs fail the candidate's required
binding preparation; temporary upload/admission gates defer readiness without
publishing partial retention. The outgoing scene remains selected. Activation
selects these exact sources for callback and declared sampled-image bindings;
each successful write clones its lease into the pass bank. Callback access to the
source span is scoped, and no pass retains that borrowed span. Cancellation clears
the sources, while failed/minimized first frames keep them through retry. A
successful first recording releases the preparation holds, independently of the
pass-bank and accepted-submission holds.
Shutdown explicitly releases renderer preparation leases and pass-bank storage
after render readers/recorders have joined and before resource-manager teardown.
Both persistent and frame-local backend lists are drained idempotently. Pool
destruction remains timeline-deferred, and accepted-submission image holds remain
in the resource manager until its idle/terminal-device drain; releasing the CPU
binding owner is not permission to reclaim an in-flight image.

This integrates retained sampled environment inputs with real scene activation.
Candidate-owned initial GPU buffers, all required profiled pass banks, storage
images and attachment/backends are prepared as described above. Fresh-write
verification is still required when recording. Producer/environment completion
and graph consumer acquires establish different proofs; no descriptor write alone
establishes them. Their full platform/lifetime qualification remains an acceptance
gate rather than an inference from successful preparation.

Read-only render-owner lookups return the retained geometry and material values;
consumers must not resolve newer UUID-cache values in their place. Count/error
results are published at terminal preparation transitions, not read from mutable
render progress. Cancellation releases material and geometry pins in bounded
slices after the geometry producer gate, and shutdown drains them after idle.
Callers must retain the request through all consumers or independently retain
the exact resource versions until their accepted submission gates retire.

The render owner can now transfer prepared geometry/material dependencies into
the existing automatic frame-retirement tables. Geometry transfer retains the
same version record even after its public mesh slot has been replaced or reset;
material transfer clones the held image leases and uses the copied shader values,
not a new UUID/handle lookup. A pending texture deletion still blocks new asset
admission but cannot revoke a live owner's handoff to its submitted consumer.
No new allocation or upload is performed. Duplicate draws share the same frame
entries. After successful transfer the preparation request may be released;
accepted graph/final-frame gates and pending recording now independently retain
those consumers. Transfer refusal preserves output snapshots, and partially
cloned map references roll back. Shutdown sealing refuses new frame transfers.
Material preparation also reserves entries in the existing fixed frame material
table without marking them as recording draws. Outgoing accepted GPU work and
candidate reservations share its 4,096-entry capacity. Exhaustion fails preparation
before CPU publication; frame polling cannot reclaim a reserved entry. Cleanup
drops reservations in the same bounded slice as material leases, retaining any
entries still protected by accepted submissions or recording. This reserves the
mesh/material handoff, not aggregate memory or the complete binding-readiness set.

Mesh/material retention is one part of the complete dependency handoff. The
editor now retains its CPU candidate across resource preparation and requires
`ResourcesRetained` before source-open publication when a real renderer/resource
manager is attached. Failure or supersession preserves the outgoing world and
releases the unpublished candidate without waiting for GPU completion. Resource
storage/pins retire asynchronously in the renderer. Headless hosts remain CPU-only.

The reserved initial authoring frame carries the resource identity and activation
generation. Each mailbox slot acquires a move-only bundle-reader lease before
claiming capacity; rollback, overwrite, coalescing, consumer release and joined-
reader shutdown release it. Owner cancellation seals new mailbox acquisition but
cannot invalidate an already-held consumer. Following same-generation editor
frames carry the identity too, so replacing the initial mailbox frame cannot lose
the dependency handoff. Draw preparation uses the retained geometry/materials,
and the render owner acknowledges when the complete captured scene was recorded.
The main owner then releases the preparation request and resumes ordinary cache
resolution for authoring/reload. Already-published mailbox frames and accepted GPU
work retain their own dependencies. New assets added after activation use normal
streaming; they do not replace the initial retained mesh versions.

A returned geometry snapshot must remain covered by its retained request, mailbox
reader lease or transferred consumer pin. It
retains matching CPU draw metadata; material/texture retention requires the
`MeshMaterials` mode, including its shared sampled-image publication proofs.
Pipeline generation retention belongs to the environment transaction described
above. Neither mode alone establishes pass-local descriptors or consumer
visibility/queue ownership. Candidate banks, prepared initial storage/attachments,
pipeline/environment preparation and graph acquires complete those separate
layers; the authoring-frame reservation alone does not provide them. Legacy draws skip on
pin refusal; required dependencies must instead be admitted before publication.
Long-lived pins can defer compaction and create capacity pressure; admission must
handle refusal, not overwrite pinned offsets.

`EngineContext::SceneLoads` now owns an allocation-free request ledger with eight
reserved result records, lifetime/sequence identities, generation-checked commit,
one preparing operation, and at most one latest waiting request. Rejected
admission preserves earlier work. Cancellation/supersession record one terminal
outcome independently of worker cleanup; acknowledgement cannot release a live
payload. Editor source open uses this ledger, preserves the consent/owner checks,
and retries unavailable worker capacity without inline I/O. Callers may retain a
pollable result explicitly; legacy UI requests are acknowledged by the editor
after cleanup. Shutdown seals admission before joins and requires cleanup to drain
before engine services disappear.

Source-open admission reserves an available A/B storage lease before worker I/O.
Occupied slots wait without starting a load; missing or undersized configuration
fails without touching the source. Saturated worker queues release the lease and
retry without constructing ECS arrays on the UI thread. Cancelled or superseded
workers retain their lease until completion or shutdown join.

The worker initializes and populates an unpublished `SceneInstance` under exclusive
ownership. Source reads reserve one reusable TLSF document slab inside the admitted
scene slot; input bytes, libyaml allocations, migration workspaces, retained decoded
blocks and principal fixed codec scratch share its aggregate cap. Refusal does not
fall back to heap/OS allocation on this route. Temporary blocks are freed for reuse;
the retained document is destroyed before its route and scene lease. Internally
routed documents cannot escape through synchronous adapter rollback. Mesh-reference
scratch borrows the remaining arena with scoped rewind; entity-resolution scratch
is charged to the scene arena and retained until reset.
Only the ready owner crosses back to the main thread for editor/render binding and
guarded publication. No actors are created and no lifecycle callbacks run during
worker reconstruction. Cancellation during population denies publication but does
not interrupt individual codec calls; cleanup waits for population to return.

Worker dispatch requires explicit `PureCandidateLocal` codec opt-in and frozen
schema/migration registries. This deliberately rejects even unused custom schemas
without opt-in; arbitrary plugin callbacks are not automatically worker-qualified.
Built-in codecs opt in, and their component type IDs are initialized before
dispatch. Shared arena page-commit accounting is synchronized between exclusively
owned sibling arenas; concurrent mutation of one arena remains unsupported.

Full allocator/codec sanitizer and platform qualification, calibrated aggregate
limits and the remaining acceptance gates below are still outstanding. The
ledger bounds request/result bookkeeping, not all existing asset preparation
memory. Editor source open snapshots `SceneDocumentWorkingBytes`; its default
aggregate cap is the existing 128 MiB decoded-document limit, carved within each
200 MiB scene slot rather than added to it. Admission requires room for this cap
and minimum ECS storage before dispatch. This is an aggregate working limit, not
a promise that a full 128 MiB decoded tree plus maximum source/parser overlap will
fit. Small profiles may configure a lower cap explicitly. Per-owner capacity,
conservative live allocation charges, fixed working charges and high-water usage
are queryable after worker completion. These counters are not RSS or exact allocator
payload sizes. Built-in callback coverage does not bound arbitrary plugin stacks
or callback-owned allocations. CPU asset-cache ingestion, upload decode scratch
and native retirement capacity use the bounded policies in section 7.1, including
source ownership, eligible CPU cache eviction and zero-retention maintenance.
Rendered mixed-lifecycle reclamation qualification remains an acceptance gate;
CPU payload reclamation alone is not proof of complete GPU/native retirement.
GPU buffer/image/staging backing admission follows section 7.1; its safety ceilings
and the default CPU split remain unqualified against the final performance and
near-limit profile.
CPU retirement is currently synchronous after commit, not a general deferred-reader
or GPU-resource retirement queue.

- Extract shared ownership/preparation from the existing editor candidate without
  duplicating document population or losing source compatibility behavior.
- Define guarded commit, cancellation, shutdown, backpressure, and retirement.
- Migrate editor open/reload through that path with unchanged dirty/epoch policy.
- Pass existing candidate, scene I/O, and render handoff regressions, plus stale
  completion, owner destruction, allocation failure, and publication fault tests.
- Prove the binding sequence and callback/thread audit in section 5 before
  replacing the existing editor handoff; do not claim readiness from a refactor
  that only preserves the previous assertions.

#### Reproducible M1 validation entry points

`ZEngineTests` covers scene storage/coordinator ownership, admission failures,
immutable resource versions, completion gates, cancellation, descriptor readiness,
and exact-cap/plus-one draw preparation. `TetragramaTests` covers editor consent,
binding/callback ownership, stale completions, publication and shutdown. These
remain headless where their fixture permits; GPU fixtures require their declared
Vulkan setup.

Hierarchy synchronization caches local TRS, parent revisions and derived matrices
inside the scene's admitted hierarchy workspace. Static frames reuse derivation;
legacy raw TRS/parent writes and derived-matrix repair remain supported. No dirty
flag supplied by an editor writer is required for correctness. ECS-to-render
identity joins use bounded stack storage and sorted lookup, preserving sparse,
reordered and stale-generation handling without quadratic per-entity scans.

The explicit `SceneLifecycleProcess` target runs the real editor against a
caller-owned project, without saving scenes. It alternates successful loads,
immediate cancellation, supersession, and post-dispatch cancellation, then closes
with another request dispatched. It checks terminal outcomes, cleanup and
publication generations. Settlement requires at least eight accepted graphics
submissions of the active generation, not eight main-thread updates. Submission
observability is coherent and read-only across threads; an aborted acquire,
rejected graph or unsuccessful graphics submission cannot advance it. Accepted
submission is not proof of display presentation or GPU completion.

Window focus gates new file-input edges, not an admitted load's lifetime. Loading,
resource-readiness polling, publication and cancellation cleanup continue while
unfocused. Existing dirty-revision consent remains mandatory; focus loss never
grants discard approval. Headless regressions cover unfocused admission,
publication, prepared-resource completion and worker-safe cancellation cleanup.

Example after building the target and configuring the normal engine-assets/Vulkan
environment:

```sh
SceneLifecycleProcess project.json /Scenes/GizmoActors.zescene /Scenes/level01.zescene evidence samples.csv 1000 100 zero-retention
python3 Scripts/validate_scene_lifecycle.py evidence/samples.csv --cycles 1000 --warmup 100 --zero-retention --stages evidence/samples-stages.csv
```

Without `zero-retention` the runner is a mixed-lifecycle soak. With that option it
also restores scene A after every operation, disables eligible CPU cache retention,
waits for decode/task/upload and native retirement queues to drain, and requires
exact warmed reference CPU/GPU live ledgers, occupied scene slots and per-asset
ownership pins. Reference-restoration timings do not describe the preceding mixed
operation. Baseline mismatches time out as failures rather than silently becoming
a new baseline. Reserved pools are distinct from these live ledgers.

The mutually exclusive `steady` mode loads scene A once, then samples windows
spanning eight newly accepted submissions without replacement requests. It marks
those windows as mode 4; their durations are sampling intervals, not load latency.
Use `--steady` in the validator for that mode. Run a matched scene/viewport/build
control alongside a short replacement profile, with the same declared warm-up.
Absolute sample-window timestamps partition stage evidence without deleting
outliers inside a measured window.

The validator's `scenario_timings` separates ordinary and superseding replacements,
cancellation before/after dispatch, steady windows, and accepted/refused capacity
requests. Use each scenario's measured count and timing distribution for load
qualification; the aggregate `timings` includes non-load cycles and is not a
successful-load percentile. Zero-retention groups explicitly describe reference
restoration after each operation, not that operation's first-frame latency.

The explicit `SceneLifetimeFixtureProcess` target generates a new temporary
project through the production serializers and checked mesh reader. It refuses
an existing destination and creates small startup, exactly 8,192-instance and
8,193-instance scenes sharing one triangle/material. Use the runner's `capacity`
mode with the exact-cap and over-cap scenes and the validator's
`--capacity-draws 8192`. Exact-cap settlement requires that draw count in accepted
graphics submissions. Every fourth operation attempts the over-cap scene and must
preserve the active generation and its complete draws. This fixture isolates
capacity/admission behavior, not complex-material or gameplay throughput.
The sample CSV includes CPU-cache, upload-pool and GPU allocation high-water
counters; upload-pool peaks are a conservative sum of individual pool peaks, not
a simultaneous whole-process peak or an RSS measurement.

The bounded stage capture also records `SceneLoad.Memory.*Bytes` counters from
the exclusive reconstruction owner, including before mesh scratch rewind and
before owner destruction. Document peak charge is maintained by its allocation
route across parsing/codec working charges; per-stage arena reservation samples
include the nested document/editor reservations. The capture preserves exact
integer byte counts through 2^53; the per-frame UI plot remains approximate.
The validator keeps these counters separate from millisecond distributions.
Use `--scene-storage-limit-bytes` and `--document-limit-bytes` with the profile's
predeclared per-slot limits to require complete bounded memory evidence. The two
fixed slots bound simultaneous scene overlap; do not sum document capacities on
top of their owning scene slots. OS RSS/footprint peaks remain separate trend
measurements, not exact live-allocation ledgers or a promise of immediate return
to baseline after GPU/driver retirement.

Non-Tracy profiling captures the first two minutes of `SceneLoad.*` stages into a
bounded CSV for short timing profiles. Reclamation-only `zero-retention` runs
capture a 30-second diagnostic prefix: their every-cycle ledgers/pins, not that
partial trace, qualify reclamation. Export occurs only after render/worker owners
join. Overflow still fails the run; the runner reports record/drop/export counts
and retains the bounded prefix for diagnosis. The trace includes unclamped,
unsmoothed main/render frame intervals and frame work, excluding the main-loop
frame-rate cap from work. Pipeline, target, attachment, backend and descriptor
preparation have separate spans inside the environment preparation scope, so a
small mesh/material slice cannot hide expensive native preparation elsewhere.
Sky request, bake admission, atmosphere/source/lighting allocations, completion
polling and snapshot retirement also have separate spans; environment preparation
is not synonymous with pipeline creation.
Frame spans also separate scene recording, graph setup, recording-worker waits,
acquire/fence waits, native queue submission, presentation and retirement.
Recording-worker task spans and creation-to-entry dispatch delays distinguish
task work from dispatch/wake-up latency. These wall-clock observations are not
direct OS scheduler/preemption measurements or proof of a loader-caused delay.
Tasks lasting at least 1 ms additionally report wall time, calling-thread CPU
time, and their non-CPU difference where a fine-grained thread CPU clock is
available. CPU reads are nested inside the wall interval. An unavailable or
inconsistent clock is reported explicitly, never treated as zero CPU work.
Non-CPU time includes blocking, descheduling and clock-read overhead; it does not identify their
cause. Coarse scheduler-accounting ticks and process-wide CPU time are not
substituted on unsupported platforms. These are profiling-only observations,
not changes to worker scheduling or permission to discard historical outliers.
Completed GPU pass sums/maxima are
exported only from available timestamp-query results: their capture timestamp is
the later CPU poll, not the original submitted frame. Sums may include overlapping
queues and are not GPU frame time or display presentation latency. Do not align
them to a same-timestamp CPU frame or subtract unrelated maxima to claim causal
loader attribution.
Persistent PSO warmup recording is deduplicated by exact live cache entry
(including shader modules, generation and variant), not just shader name. A ready
pipeline re-poll does not reopen/hash its shader files. New generations/variants
still record independently, and replacing the recipe collection invalidates those
recording marks. Failed hashing does not mark an entry as recorded.
These are nested spans, not additive timing charges. Overflow fails evidence export. The validator rejects
missing/reordered cycles, malformed/non-finite measurements, undrained references
and live-ledger drift; its own negative tests are registered with CTest when Python
is available. Report all samples and maxima; a distribution summary is not an
automatic timing pass.

For a prospectively declared replacement timing profile, pass `--load-limit-ms`,
`--main-loader-limit-ms`, `--commit-limit-ms` and
`--render-preparation-limit-ms` to enforce maxima rather than just summarize them.
The complete measured window must be covered by the bounded stage capture;
partial traces and steady/reference-restoration modes cannot qualify replacement
timing. The preparation limit conservatively bounds the whole environment stage.
It is not proof of matched steady-frame attribution, and an unrelated steady
maximum must never be subtracted to excuse a preparation outlier.

A long soak can finish warm-up after its bounded stage capture ends. Its measured
stage distribution is then empty, explicitly not a timing pass. Use a short,
separately declared timing profile rather than treating startup/warm-up-only stage
data as measured-phase evidence.

These entry points do not replace the frozen platform/fixture manifest, matched
steady-frame attribution, high-water qualification or reviewed pixel references
in sections 9.1–9.3. OS window maximization/DPI, camera/exposure/history settling
and visual tolerances must be fixed or recorded by that profile; the smoke runner
alone does not claim deterministic pixel qualification. Unsupported sanitizers
must retain their recorded limitation and alternative coverage. Windows execution
remains user-owned.

`Scripts/compare_scene_capture.py` compares bounded P6 RGB8 captures produced by
the Vulkan screenshot layer with `VK_SCREENSHOT_FORMAT=UNORM`. Opaque presentation
alpha is implicitly 255. Each explicitly named viewport/object rectangle uses
the fixed 4/255 channel and 0.5% pixel thresholds; neither automatic registration,
rescaling nor automatic masks can hide a changed object. Declare important object
regions as well as the viewport so a small missing object cannot pass solely on
the whole-frame percentage. Compare repeated captures before reviewing a reference
as a backend-specific golden; matching fresh captures alone is not visual approval.
The comparator's malformed-input, threshold-boundary and small-object-loss tests
are also registered with CTest when Python is available.

Supply each completed capture process's engine log with `--validation-log`; errors
or incomplete shutdown invalidate pixel evidence even if the rasters match. A
capture backend must not reuse a command buffer recorded for one-time submission.
If the installed screenshot layer does so, use one post-settlement capture per
fresh process or a separately qualified capture backend. Multi-capture equality
from that failing layer can be stale data, not proof of deterministic rendering.

### 9.5. Milestone 2 — Cooked scene to a rendered first frame

- Add retained package lookup, verified decoding, versioned asset leases, and
  complete required render-resource preparation.
- Launch in a fresh process with authoring sources/import registry unavailable;
  render meshes with materials/textures and the scene environment.
- Exercise missing/corrupt/unsupported objects, target mismatch, exhausted budgets,
  failed GPU uploads, cancellation during uploads, and repeated scene switches.
- Keep the old scene usable after every precommit failure; demonstrate bounded
  CPU/GPU retention and no renderer validation errors after retirement.
- Use `SampleProject/project.json` for editor regression/visual coverage and a
  cooked copy for the source-free rendered test. Check transparent materials,
  lighting/environment, hierarchy transforms, and replacement against references.
- Freeze the qualification profile and pass the applicable acceptance matrix;
  report RendererOnly scope rather than implying supported gameplay services.

### 9.6. Milestone 3 — Play and simulation integration

- Implement the approved isolated retained-authoring ownership in the Play contract.
- Define supported gameplay/physics activation contracts before claiming gameplay
  reconstruction; keep undefined authored actor restore out of scope.
- Implement Enter/Stop/Pause/Step, authoring-history policy, input routing, and
  failures under the approved ownership and memory model.
- Complete the remaining runtime/Play matrix in #719. Milestones 1 or 2 alone do
  not close that issue.

Tests run on supported CI platforms; local Windows validation is left to the
user as requested. Cross-platform CI remains a merge gate. Record results in
test/PR evidence, not as accumulating build reports in this contract.

## 10. Review status and approved Play decision

The proposal now specifies operation outcomes/cancellation, publication binding
order, thread/allocator ownership, runtime capabilities, complete first-frame
readiness, failure domains, cache reclamation, and acceptance evidence. These are
proposed implementation requirements; closing the design gaps does not mean
the implementation or its qualification has already passed.

The existing recommendation remains engine-owned transactional lifetime first,
strict first-frame-ready cooked rendering second, and gameplay/Play later.
Authored actor restore, full physics/audio reconstruction, shipping packaging,
plugin ABI, additive loading/streaming, hot reload, signing, and device recovery
remain excluded unless separately approved. No new manifest format is required.

The user approved isolated retained-authoring Play on 2026-11-07: keep Edit world
A, construct runtime world B, preserve valid authoring history, and refuse entry
if the required overlap cannot fit. The Play document is reconciled with that
choice. There is no remaining isolation-versus-restoration decision; implementation
still follows the staged milestones and the ongoing proposal review.

Hardware-specific profile calibration remains an implementation/qualification
deliverable, not an open policy choice. Missing calibrated limits or platform
evidence blocks acceptance of that profile. Approval of this documentation does
not grant permission to silently relax thresholds or expand scope.
