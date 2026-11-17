# ZEngine — Profiling and Instrumentation

**Status:** Extensive proposal for review; not implemented or a production-readiness
claim. Revised 2026-11-10 against the M1 source tree.
**Scope:** in-editor Performance tooling, CPU/GPU instrumentation and correlation,
memory attribution, optional Tracy integration, bounded capture/export and qualification.
**Related:** [PR #872](https://github.com/JeanPhilippeKernel/RendererEngine/pull/872),
[atomic-layout task #875](https://github.com/JeanPhilippeKernel/RendererEngine/issues/875),
[M2 #873](https://github.com/JeanPhilippeKernel/RendererEngine/issues/873) and
[M3 #874](https://github.com/JeanPhilippeKernel/RendererEngine/issues/874).

All new defaults, APIs and stages below are proposals. Existing facilities are
distinguished from planned behavior. Benchmark/build history belongs in implementation
PR artifacts, not in this contract.

## 1. Recommendation and outcomes

Deliver an integrated **Performance panel** for everyday diagnosis, backed by shared
engine instrumentation, and **Tracy** for deep trace analysis. Keep the existing Memory
Profiler as a compatible view of shared memory telemetry. Do not build a second full
Tracy implementation inside the editor or require a Tracy connection to inspect frame
times and engine budgets.

The intended workflow is:

1. Notice a frame spike, resource growth or budget pressure inside the editor.
2. Select the affected frame/request and inspect CPU owners, GPU passes and memory.
3. Capture a bounded interval with explicit completeness and instrumentation mode.
4. Use Tracy or external hardware tools for deeper investigation when needed.
5. Compare against a matched baseline and attach reproducible evidence to a PR.

Observed duration is not causal attribution. A long present call, idle GPU interval,
cache-miss count or increasing RSS does not by itself identify a responsible subsystem.

| Consumer | Purpose | Does not replace |
|---|---|---|
| In-editor Performance panel | Live summaries, bounded frame inspection and capture controls | Arbitrarily deep trace navigation or hardware analysis |
| Memory Profiler panel | Owner/pool/resource budgets, counts and trends | General process heap profiler or physical-residency authority |
| Optional runtime overlay | Minimal development-host frame/memory indicators | Editor tooling or shipping telemetry policy |
| Tracy desktop application | Detailed thread zones, task flows, supported GPU/allocation analysis | Engine-authoritative budget and lease accounting |
| Headless capture/test helpers | Repeatable machine-readable measurements | Interactive UI or hardware profiler |
| Platform/vendor tools | Sampling, scheduler, coherency/cache, shader counters and display investigation | Portable engine instrumentation |

Initial non-goals: remote shipping-game control, player analytics, automatic
false-sharing diagnosis, shader instruction profiling, whole-system tracing, universal
heap interception, crash-safe trace recovery, and exact physical CPU/GPU residency.
Each needs a separately reviewed capability rather than an implied checkbox.

## 2. Verified starting point and gaps

| Facility | Present in source | Gap to this proposal |
|---|---|---|
| `Profiling.h` | Tracy, lightweight fallback or no-op macro paths | Current mutually exclusive routing cannot supply both local detail and Tracy |
| CPU fallback | `ScopeGuard`, counters, 4,096-entry sample/value arrays | No normal-loop frame advance/presentation or thread/parent identity in old samples |
| Capture | 262,144 records, prefix/timed capture, CSV and drop count | Start/stop/export require quiescent writers; not live editor capture controls |
| Thread CPU clock | Validity-aware thread clock and wall/non-CPU comparison | Not universal platform support or scheduling attribution |
| Tracy | OFF by default, pinned client v0.13.1, on-demand configuration | Incomplete GPU/frame/allocation integration and session workflow |
| Memory profiler | Named arenas, current/sample peaks and warnings; main-thread update | Not whole-process live payload or allocation-origin tracking |
| Allocation hooks | Macros exist | Fallback `RecordAlloc`/`RecordFree` are empty; allocator coverage incomplete |
| CPU resource ledgers | TLSF live/high-water, cache/upload ownership statistics | Not a unified attributed investigation view |
| GPU timing | RenderGraph query pools and completed `RGPassTiming` records | No integrated timing UI or complete common-clock submission/presentation trace |
| GPU memory | VMA/driver budgets, allocation ledger, environment/transient statistics | Distinct domains still need provenance and detailed ownership views |
| Editor UI | `MemoryProfilerPanel` | No full Performance panel or capture/comparison workflow |

Currently `ZENGINE_PROFILING` is supplied for Debug and RelWithDebInfo, not ordinary
Release. `ZENGINE_TRACY` defaults OFF and its engine compile definition is private;
audit propagation to editor/helpers before claiming consistent instrumentation.
Section 11 proposes build policy; those are not existing additional options.

Two correctness prerequisites cannot be solved by merely wiring old APIs:

- Resetting a shared writer index and copying a CPU sample array can race workers
  still writing reserved entries. Atomic reservation is not completed publication.
  Replace that transport before enabling normal-loop snapshots; do not expose live
  producer arrays to ZUI.
- Names are currently borrowed pointers. A formatted stack name, such as an arena
  plot label, cannot remain borrowed until later CSV export. Register stable names
  or copy bounded dynamic data into owned metadata.

Legacy CSV remains a separate format. Do not reinterpret old records as containing
thread identity, calibrated queues or actual display timestamps never captured.

## 3. Architecture and ownership

```text
 Main / render / worker threads       Renderer / allocator owners
   zones, tasks and counters           GPU/query and memory records
                 |                                 |
                 +------ stable instrument IDs ----+
                                   |
                       Engine Profiling service
                   +---------------+----------------+
                   |                                |
          bounded local transport            optional Tracy adapter
          per-thread publication             supported client APIs
                   |                                |
             one collector                    Tracy client/server
            /      |       \                        |
 immutable UI   bounded    test                detailed analysis
  snapshots     archive   summaries
      |            |
 Performance /     +--> async export --> versioned artifact
 Memory / overlay
```

Shared instrumentation has distinct sinks; this does not require translation into
Tracy's private wire protocol. Tracy GPU integration may need its own bounded query
contexts under the renderer ownership rules in section 7.

### 3.1. Placement

- `ZEngine/ZEngine/Profiling`: configuration, stable metadata/IDs, CPU transport,
  snapshots, capture state, export schema and Tracy adapters. Core facilities must
  not depend on Tetragrama, ZUI, an active scene or a GPU.
- Engine/application/render pipeline and worker scheduler: phase/frame/task markers
  and lifetime hooks; not ad-hoc profiler file output.
- Renderer/RenderGraph: GPU capabilities, queries, completion-gated results and
  resource snapshots under renderer-owned lifetime.
- Memory/asset/upload owners: authoritative counters plus optional detail hooks;
  profiler failures never change allocation or retirement semantics.
- `Tetragrama/Panels`: Performance presentation and existing Memory panel, issuing
  bounded service commands instead of accessing raw buffers.
- `Scripts` and tests: artifact validation/comparison and process helpers, without
  machine-specific probes as implicit dependencies.

API names beyond the existing tree are implementation choices. Use one engine
service lifetime, not one global singleton per UI or backend.

### 3.2. Thread and storage contract

| Operation | Owner / rule |
|---|---|
| Configuration/thread/name registration | Bootstrap/control owner; bounded registration before hot use |
| CPU emission | Registered current thread, sole writer of its ring |
| Drain/history/capture mutation | One collector, off main/render critical paths |
| GPU queries/results | Renderer owner, genuine completion ownership |
| Allocator statistics | Existing synchronization; no added global heap lock |
| UI commands | Main-thread bounded command channel, acknowledged results |
| UI drawing | Immutable retained snapshot, never mutable producer storage |
| File/compression/export work | Bounded export worker, not the producer/collector |

Preallocate per-thread rings. Release-publish fully initialized records and acquire-
consume published records only. Never overwrite unread slots. Thread exit seals a
watermark and transfers storage ownership; registration generations prevent OS thread-ID
reuse from aliasing old lanes. Excess/unregistered threads continue engine work while
their detail is explicitly unavailable/dropped.

Prefer completed CPU spans containing start/duration and parent/span IDs, with a bounded
per-thread scope stack, rather than unmatched transport begin/end pairs. Scope/nesting
loss invalidates derived exclusive time. A thread-local recursion guard excludes
profiler internals; profiler memory is separately owned and charged.

Publish snapshots through a bounded ownership mailbox: Writing → Ready → Reading → Free.
A slow panel pins its slot; the collector skips/coalesces publication instead of
blocking or overwriting it. No C++ data race is permitted behind a seqlock-like retry.
Closing/reopening/detaching panels releases readers safely.

## 4. Data, identity and measurement semantics

### 4.1. Record envelope

Use a versioned bounded binary schema with tagged payload and availability/quality
flags. Initial capacity arithmetic assumes 64-byte transport records; larger logical
events use bounded linked records counted against the same budget. Final packing is
reviewed before implementation, not inferred from the list below.

Session metadata includes build/commit/instrumentation version, OS/backend/device/driver,
clock units/domains, mode/caps/categories, profiler overhead configuration, and applicable
fixture/shader identities. Site/category/thread IDs refer to session-owned dictionaries.
Names are bounded and immutable; dynamic values are owned bounded copies, not borrowed
`cstring` pointers. Dictionary exhaustion records unknown-name/loss status.

Events carry applicable identities, not every identity on every record:

- session/configuration epoch and thread registration generation;
- monotonic CPU timestamp, category/site, span/parent or async-operation ID;
- main iteration, render frame, immutable snapshot sequence and scene generation;
- queue/device epoch, accepted submission ID and completion association;
- allocation/resource ID plus reuse generation, request ID or typed asset version.

Raw pointers/process-local ECS IDs are not durable exported identity; asset UUID alone
does not identify a version. Use explicit links, not nearest-timestamp matching.

### 4.2. Timing definitions

| Quantity | Meaning / limitation |
|---|---|
| CPU wall zone | Includes blocking and descheduling inside the instrumented interval |
| Thread CPU time | Calling-thread execution accounting, only with a valid supported clock |
| Wall minus CPU | Non-CPU interval including waits, descheduling and clock overhead; not causal proof |
| Main work / interval | Work phase versus elapsed spacing of main iterations |
| Render work / interval | Recording/submission work versus spacing of render iterations |
| Task timing | Enqueue → start, start → finish and dependency waits are distinct |
| GPU pass | Timestamp-bracketed interval on a valid queue; can include stalls/dependencies |
| Submission latency | Accepted CPU submit through observed GPU completion; not display time |
| Presentation | Acquire wait, present request and actual display feedback are separate observations |

Main/render rates can differ. Define `MainFrame` and `RenderFrame` plus snapshot links,
not a shared mutable frame counter reset by competing threads. Preserve cross-frame
span boundaries; any view clipping is labelled. Represent skipped/minimized frames,
resize/device epochs, failed submissions and absent presentation explicitly.

Use `duration_cast<nanoseconds>` on a validated monotonic clock; do not assume native
`steady_clock::duration::count()` is nanoseconds. Record units/frequency/domain. Invalid
time is N/A, not zero. Bytes/counts stay integers through storage/export, with floating
conversion only where needed for drawing.

Inclusive/exclusive aggregation requires complete nesting on one thread. Do not subtract
async children or sum parallel workers as elapsed frame time. Percentiles name their
population, interval, mode, losses and warm-up policy. Incomplete data is flagged,
not silently removed to improve a result.

## 5. CPU instrumentation

Required initial coverage:

- Main/render boundaries and input, application/ECS update, command flush, hierarchy/
  scene preparation, immutable handoff, recording and submission phases.
- Worker enqueue/start/finish with stable async flow IDs and distinct wait/useful-work
  spans. Ordinary nested scopes cannot migrate across threads.
- Asset I/O/decode/import/upload preparation, pipelines/resource admission, retaining
  compatibility with existing `SceneLoad.*`, thumbnail and content-browser sites.
- Explicit waits/lock sites only on shortlisted hotspots, not hooks on every mutex.
- Optional long-task thread CPU samples with unavailable status; process-wide/coarse
  CPU accounting must not masquerade as a precise thread clock.

Keep macro compatibility where practical. Register stable sites once and fan out to
enabled sinks without per-scope formatting/allocation. Distinguish same-named functions
using site/module identity. Dynamic labels require owned metadata rather than reuse of
literal-only APIs.

The local UI supports bounded thread lanes, nesting and hot-region aggregates. Arbitrary
long-session search, symbolized sampled stacks and OS context switches remain Tracy/
platform-tool depth. Sampling/call stacks/lock traces are opt-in separately; surface
symbol/privilege/support requirements and overhead. A checkbox cannot claim an unbuilt
or unqualified platform capability.

## 6. In-editor Performance and Memory experience

### 6.1. Proposed layout

```text
 Performance [Live/Pause view] [Arm/Record/Stop] [Export] [Tracy status]
 Mode: Summary   Capture: Idle   GPU: delayed 3 frames   Loss: 0
 ------------------------------------------------------------------
 CPU work/interval | GPU queue spans | Present feedback | Memory
 ----------------- bounded recent-frame graphs --------------------
 Selected: Render 1824 / Main 1841 / Snapshot 1900 / Scene generation 7
 [Overview] [CPU] [GPU] [Memory] [Capture health]
 ------------------------------------------------------------------
 CPU: thread lanes/hot regions   GPU: queue/pass rows/dependencies
 Memory: owners/resources       Health: caps, loss, unavailable data
```

This wireframe defines relationships, not final styling. Reuse ZUI docking/theme/input
and accessibility conventions; support keyboard focus, high DPI and narrow widths.
Units/scope/status labels are mandatory, and color is not the only status indicator.

### 6.2. Behavior

- Overview separates main/render work and intervals, supported GPU summaries and
  distinct CPU/GPU budgets. It does not present a misleading universal bottleneck badge.
- Select a spike to inspect correlated records with GPU age/pending state. Pause freezes
  the **view**, not gameplay/capture. A pinned view cannot block producers; expired
  detail is reported rather than retained indefinitely.
- CPU shows lanes/nesting, inclusive/exclusive values and task links, marking loss or
  incomplete hierarchy. Wall waits are never labelled CPU execution.
- GPU shows pass names, resolved queues, completion age and submission links. Pass sums
  are not GPU frame time; uncalibrated clock lanes remain distinct.
- Memory shares providers with the existing panel, with owner/resource drill-down;
  the UI never resamples mutable allocator/renderer internals unsafely.
- Health always exposes mode/categories, coverage start, age, query/ring/dictionary
  capacity, losses and export failures. Unsupported data is N/A.
- Commands are acknowledged nonblocking operations. Disabled controls show why.
  Closing a panel does not implicitly terminate an explicitly armed/recording session.
- Initial summary history: 512 frames with virtualized bounded detail. This is not a
  promise of complete events for all 512 frames; show actual retained ranges/evictions.

### 6.3. Runtime and headless hosts

A minimal opt-in runtime overlay has low-rate summaries/health, no editor dependency
and no input interception without host consent. Headless hosts use the same service
through tested CLI/control seams. Introduce exact CLI syntax with implementation;
do not advertise fictional existing flags. No shipping network service is implied.

## 7. GPU profiling and Tracy GPU integration

### 7.1. Renderer safety

Preserve RenderGraph's valid-bit/period checks, finite per-frame query capacity and
host-query-reset policy until a separately validated replacement exists. Check each
resolved queue family; graphics timestamps do not imply compute/transfer support.

Associate timing sets with original frame/context, queue and device epoch. Read after
existing real completion ownership permits, or with nonblocking availability checks.
No added `WAIT_BIT`, fence wait, queue-idle or device-idle to make a chart current.
Delayed/failed/missing data is explicit, never invented zero-duration work.

Skipped/aborted passes and failed submissions must not appear completed. Pool/context
retirement follows genuine last-use completion through resize, cancellation, shutdown
and device loss. Query overflow omits instrumented passes with a count; it must not
affect rendering correctness or create an unbounded query pool.

### 7.2. Correlation and clocks

Portable baseline: per-pass durations with logical CPU submission/task links.
Cross-queue subtraction or CPU/GPU offset estimation is prohibited without a validated
common/calibrated domain. Where `VK_EXT_calibrated_timestamps` or a qualified equivalent
exists, retain clock pairs, domains, age and maximum deviation; refresh at bounded
cadence and invalidate discontinuities/device changes. Display alignment uncertainty.
Without support, separate lanes with logical links are a valid degraded mode.

Present request, acquire wait, accepted submit and actual display time require separate
capabilities. Without display feedback do not claim input-to-photon/display latency.
MoltenVK/native diagnostics require backend-specific evidence; temporary M1 Metal probes
are not permanent portable engine infrastructure.

### 7.3. Tracy adapter

Audit the pinned client's documented Vulkan contexts/zones/collection APIs. Do not
inject stored `RGPassTiming` into an invented Tracy wire format. Define ownership for
each actual queue and primary/secondary worker-recording arrangement.

Prefer shared engine timing policy/sites. If supported APIs cannot share query lifetimes,
permit separate local and Tracy query pools only in explicit Detailed+Tracy mode;
charge both capacities/overhead and show dual instrumentation. Summary must not silently
double queries. Claim GPU integration only after multi-queue, delayed collection,
secondary recording, resize and context-destruction tests pass.

GPU intervals include scheduling/dependencies, not isolated shader throughput. Use
vendor/native tools for shader/hardware counters and label perturbation. Validation
layers prove neither timing accuracy nor absence of GPU bottlenecks.

## 8. Memory accounting and attribution

### 8.1. Distinct quantities

| Domain | Expose | Never claim |
|---|---|---|
| Named arenas | Reserved/known committed bytes, offset and sampled high-water | Offset equals live payload or RSS |
| Slabs/pools | Backing/control, live charged bytes/blocks, operation high-water, available requested bytes | Backing and suballocation usage are independent physical RAM |
| Assets/scenes | Version pins, active/candidate/retiring ownership, cache retention | UUID alone identifies one immutable allocation/version |
| Upload/readback | Pool/staging, live leases/bytes, completion retention | Submitted means reusable |
| VMA | Allocation bytes/count versus blocks | Allocations and blocks are additive usage |
| Driver heaps | Usage/budget with driver/fallback provenance and age | Exact process-exclusive VRAM/residency |
| RenderGraph | Virtual resources, physical backing and aliasing | Virtual and physical are additive |
| OS process | RSS/working set and supported footprint/commit, with definitions | Full owner ledger or exact logical reclamation |

Unified memory can appear in both CPU/GPU views. Do not publish a combined RAM+VRAM
total as physical consumption. Unknown commitment/residency is unknown, not zero.
Frame-sampled arena peaks may miss within-frame maxima; distinguish them from
per-operation high-water ledgers.

### 8.2. Attribution levels

1. **Summary:** authoritative owner/pool/domain counts, budgets and high-water; no
   allocation stack or full resource scan every frame.
2. **Tracked detail:** bounded lifetime records, allocation ID/reuse generation,
   requested/charged bytes, owner/category and logical backing relationship, with
   resource names/asset versions when available.
3. **Stack detail:** optional filtered/sampled CPU allocation stacks, bounded symbols
   and explicit coverage/overhead.

Hook reviewed engine-controlled OS backing, slab/pool and resource boundaries. Failed
realloc preserves its old live record; pointer reuse receives a new generation.
Cross-thread frees preserve identity/causal ordering. Arena reset retires logical
allocations by owner/reset epoch, not fabricated native frees. Relate backing and
suballocation records to prevent double counting.

Do not put arena backing plus every bump allocation into one unqualified Tracy heap.
Use supported named pools/arenas or labelled logical events, with safe reset/reconnect
semantics. Exclude profiler recursion. Third-party/driver/library routes not instrumented
remain explicitly outside coverage.

Late attach cannot recover unrecorded allocation origins. Where possible capture an
owner-coherent baseline labelled unknown origin. Ring/map loss marks detail incomplete
and disables exact leak/lifetime conclusions while authoritative summaries continue.
Rebaseline only at documented coherent boundaries; never invent a missing free or claim
all allocations have stacks. Bound cross-thread correlation/maps as well as event rings.

Comparisons select equal reference-world/drain states and distinguish active/candidate/
retiring/cache overlap. Use exact ledgers before RSS trends: retained caches are not
automatically leaks, and an unexplained live pin cannot be dismissed as driver caching.

## 9. Capture, history and export

### 9.1. State machine

```text
 Idle --> Armed (rolling pre-trigger) --> Recording --> Stopping --> Draining
   |                                  ^                              |
   +---------- manual start ----------+                              v
                                                        Ready --> Exporting
                                                          ^           |
                                                          +-----------+
```

Admission reserves archive/metadata/command/result capacity. Reject Busy/BudgetExceeded
without disturbing another capture. One local detailed session is active; summaries
continue. Manual, timed and hitch-triggered modes share the lifecycle. Initial trigger:
declared main/render interval threshold, bounded pre/post windows and cooldown. It detects
a spike, not its cause. Category/trigger selection is recorded before arming.

Capture/configuration epochs identify records. Stop establishes a cutoff; producer
watermarks and collector-owned chunks seal a fully published prefix. Export never
reads producer arrays. Idle/exited/long-running threads have acknowledgement/finite
timeout rules; do not wait indefinitely for another event. Scopes crossing start/stop
are partial/outside, not silently complete. Control epochs cannot reuse storage until
prior registered producers/consumers no longer reference it.

GPU results can arrive after CPU stop. Draining retains admitted query/context ownership
until results or a finite declared deadline marks them missing. A capture timeout does
not authorize early resource reuse: retain a bounded renderer retirement owner when
needed and reject new capture admission if it would overlap exhausted slots.

Manual archive exhaustion stops detail with truncation status; no blocking, silent
prefix overwrite or capacity growth. Armed mode intentionally evicts oldest pre-trigger
history with counts/range metadata. Proposed pre-trigger cap: 8 MiB within the same
32 MiB archive, leaving at least 24 MiB for post-trigger records. Retained duration
depends on event rate; no guaranteed seconds from a byte cap.

Export failure is separate from capture failure. Ready data survives disk-full,
permissions/cancellation so another destination can be retried. Explicit discard/new
session releases it; capture admission failure preserves previous data. Crash-safe
recovery is outside scope, not justification for synchronous hot-path writes.

### 9.2. Artifact and comparison

Version a local bundle containing manifest, bounded chunks, name/clock dictionaries,
capabilities, losses/coverage and summaries. Specify endian/field sizes/count limits,
checksums and supported versions. Compression is bounded export-worker work; offer
uncompressed export. Tracy files remain Tracy-managed, associated through session/build/
mode metadata where supported, not falsely advertised as the same binary schema.

Provide CSV summaries/legacy automation plus a tested bounded interchange export for
supported CPU spans/flows. Choose/validate its schema before claiming viewer support.
Uncalibrated GPU domains must not become misleading common-clock trace timestamps.

Imported files are untrusted: reject invalid bounds/versions/checksums, traversal and
decompression bombs before unsafe allocation. No executed commands, arbitrary URL fetches
or automatic symbol downloads. Export uses a permitted user-selected path and temporary/
atomic publication where supported; partial output cannot masquerade as valid capture.

Compare matched workload/options, mode/categories, hardware/driver, viewport, worker
counts, scene/shader identities and cache/warm-up state, or display mismatch warnings.
Retain raw samples/outliers and state populations/exclusions/confidence. Mixed sampling,
loss or truncation cannot produce an apparently exact regression conclusion.

## 10. Bounds, perturbation and failure behavior

### 10.1. Proposed development profile

The following are **initial review caps**, not measurements or engine RAM guarantees.
Calibrate once against finite workloads before freezing qualification. Thereafter,
exceedance fails the profile rather than silently raising limits. New caps do not alter
M1's historical capture capacity or its acceptance evidence.

| Dedicated local CPU storage | Proposed ceiling |
|---|---:|
| Rings: 32 threads × 4,096 records × 64 bytes | 8 MiB |
| UI snapshots, 512-frame summaries, bounded detail cache | 8 MiB |
| One archive including pre-trigger history | 32 MiB |
| Dictionaries, allocation-ID maps, thread/scope metadata | 4 MiB |
| Export/compression/import scratch | 8 MiB |
| Collector/control/alignment reserve | 4 MiB |
| **Total dedicated local CPU ceiling** | **64 MiB** |

Validate actual whole layouts/allocator overhead against those terms. A 64-byte record
does not require extra per-record padding. Optional buffers are admitted at safe control
boundaries; Summary must not permanently commit the Detailed ceiling. No hidden large
worker stacks or borrowed/rewound scene arenas.

Sites/names, thread slots, scope depth, allocation maps, snapshots, exports and GPU pairs
have finite subcaps. Stage P0 publishes concrete entry counts/byte arithmetic before
implementation. More than 32 registered instrumented lanes is partial coverage or another
explicit profile, never a reason to stop engine workers.

Initial metadata subcaps proposed for P0's layout audit: 4,096 sites/names, 128 categories,
32 registered thread lanes, scope depth 128 per lane, and 16,384 detailed live-allocation
identities. Charge bounded name text, map load-factor/slots, scope stacks and bookkeeping
within the same 4 MiB metadata ceiling. Unsupported cardinality marks detail incomplete;
this is not a 16,384-allocation limit on the engine. Permit one pending export and three
UI snapshot slots; include their actual bytes in the table rather than allocating a
second uncharged archive for each viewer. A new capture cannot replace a Ready/Exporting
archive without explicit discard or completed export/release.

Summary starts with a separate proposed 16 MiB local ceiling, using only the summary/
metadata/control terms above and admitting detail rings/archive/export scratch only
when requested. Record core main/render phase totals once per phase/frame; do not
route every fine-grained zone into a supposedly lightweight Summary mode. Proposed
presentation/sampling cadence: charts and UI snapshots 10 Hz, CPU owner statistics 2 Hz,
driver budget/RSS samples 1 Hz, unless an explicit measured profile requests more.
Frame summaries retain original per-frame observations independently of UI cadence.
Sample each owner through its synchronization/owner-thread path; lowering cadence is
not permission to let a background sampler race allocator or renderer state.

The 64 MiB ceiling is a bounded design starting point for one short detailed capture,
not a claim every developer needs that RAM continuously. A 32 MiB archive holds at most
524,288 fixed 64-byte records before linked metadata overhead; event rate determines
its duration. Admission must report the predicted tradeoff, not promise a fixed-length
capture independent of workload. Smaller/larger profiles require explicit reviewed caps.

GPU pools are separate: record actual frame-context count, per-queue timed pairs, Tracy
contexts and driver-visible byte estimates when available. Preserve the existing finite
`TimestampPassCapacity` initially. GPU queries and Tracy client/server/network buffers
are measured separately; do not silently include them in the 64 MiB local CPU claim.

### 10.2. Proposed perturbation gates

Use optimized builds with matched options/categories/workload, repeated paired batches,
uncertainty, background-load records and no-profiler controls. Freeze event rates before
comparison; do not report clock-scale deltas below noise as precise wins.

| Mode | Initial review target on the declared profile |
|---|---|
| Compiled out | No profiling calls/events, client dependency or profiler-owned runtime storage; verify binary/link artifacts |
| Summary, hidden panel | ≤1% median main/render work increase and ≤0.2 ms extra p99 frame interval |
| Summary, visible panel | ≤0.5 ms p95 panel CPU build; bounded rows/history; graphics cost reported separately |
| Detailed local, no stacks | ≤5% median critical-path work increase and ≤1 ms extra p99 frame interval |
| Tracy / stack / dual-query detail | Independently measured profiles; no inherited Summary overhead claim |

Relative and absolute limits both apply where listed. These are proposed targets, not
results. An explicitly selected more intrusive session is labelled perturbing and
cannot uncritically certify uninstrumented performance. Existing scene 4/2/8 ms gates
are independent; profiler overhead does not authorize increasing engine limits.

Local emission performs no file/network I/O, string formatting, allocation, symbolization
or blocking global lock. Measure Tracy's documented behavior separately, not as assumed
allocation-free. Budget collector/export scheduling without taking unbounded existing
resource-worker capacity. When behind, drop detail/defer export instead of stalling owners.

Expose loss per ring/unregistered thread/scope depth/name table/allocation map/archive/
GPU pool. Coalesced UI snapshots report age/skips rather than pretending events were
lost. Missing metadata invalidates dependent calculations. No interpolated frees,
completion or display events. Profiler OOM disables optional diagnostics safely, without
changing valid engine allocation outcomes or resource budgets.

## 11. Builds, Tracy lifecycle, privacy and shutdown

### 11.1. Proposed mode policy

| Build | Local facilities | Tracy | Default |
|---|---|---|---|
| Debug developer | Summary/detail available | Optional | Label Debug and validation-layer timing overhead |
| RelWithDebInfo / optimized developer | Available, useful symbols | Optional | Preferred detailed profiling configuration |
| Ordinary Release | Explicit instrumentation profile | Explicit opt-in | Do not assume macros are enabled |
| Shipping | Separately approved diagnostic build only | Not linked/listening by default | No capture/network endpoint without product/security review |

Reconcile with existing `ZENGINE_PROFILING`/`ZENGINE_TRACY`, not automatic enablement in
every build. Propagate configuration consistently to libraries, editor, inline frontend
and helpers; avoid unrelated public ABI changes. Test OFF/local/Tracy combinations and
reject unsupported configurations during configure rather than surprising linkage.

Tracy is an optional sink, not the panel's master switch. Pin client/server compatibility.
Document actual discovery/listen address/ports under explicit developer configuration;
do not promise loopback-only without verifying client options. Disconnect/reconnect cannot
cause invalid lifetimes/unbounded retention or recover unknown allocation origins.

Summary/frame metadata remains available with Tracy. Do not duplicate every CPU event
unless Detailed+Tracy is selected; deep Tracy may disable redundant local zones while
the panel states its reduced coverage. Use documented task/lock APIs; a normal nested
zone cannot begin/end on different threads.

### 11.2. Privacy and distribution

Default local exports use module/relative sites and opaque IDs, not raw pointers,
absolute usernames/paths, file contents, secrets or arbitrary user asset text. Opt-in
stacks/symbols and Tracy may reveal more: warn on export and document each sink's actual
redaction capability. The engine cannot claim to sanitize a Tracy file it does not own.

No automatic uploads. Users select destination/retention/deletion; exported files persist
until deleted while in-memory history is bounded. CI artifacts need declared retention
and sanitized fixtures. Imports/symbols follow untrusted-input rules. Document client
licensing/redistribution separately from distribution of the external Tracy application.

### 11.3. Shutdown/failure isolation

Seal capture commands; stop producers/categories via epochs; stop/join workers; drain
published CPU/admitted GPU work only as real device shutdown permits; finalize loss;
cancel/join exporters with finite handling; release readers/contexts before dictionaries/
storage. Late allocator callbacks must not access dead profiler state. Never join the
collector while holding a lock it needs or wait indefinitely for a lost device.

Device loss invalidates the GPU epoch. A full disk cannot block rendering; a dead Tracy
connection cannot crash the local panel. Recursion, registration races, partial shutdown
and profiler errors need tests; measurement failure is not successful engine execution.

## 12. Validation and production acceptance

Qualification records OS/CPU/GPU/driver, memory, backend, compiler/configuration/symbols,
validation-layer state, mode/caps, fixture/shader IDs and exact commands. Normal CI covers
Linux/Windows/macOS builds and deterministic non-GPU tests. Native rendered/timestamp/
Tracy evidence is required for each claimed platform capability.

| Axis | Required cases | Gate |
|---|---|---|
| Transport | Concurrent/wrapping/full rings, slow drain, exit/ID reuse, names/depth | No races/unpublished reads/overwrite; bounded storage and correct loss |
| Frames/clocks | Cross-frame spans, unequal loop rates, invalid clock, long runs | Correct identity/units, explicit N/A; no fabricated aggregation |
| UI | Slow/pinned/closed/reopened views, high DPI/narrow/minimized | Immutable bounded reads, no producer block; visible age |
| Tracy | Local/Tracy/combined, disconnect/reconnect, task flow | Correct configured coverage and context lifetime |
| GPU | Unsupported features/calibration, wrap, delays, failed submit, resize/loss | No extra waits or early reuse; origin IDs and honest availability |
| Memory | Realloc failures, cross-thread free/reuse, arena reset, aliasing, late attach | Correct totals or incomplete status; no double counts |
| Capture | Arm/stop/trigger races, idle thread, full archive, delayed GPU, disk-full | One outcome, bounded drain, sealed valid prefix and retryable export |
| Import | Corrupt sizes/IDs/checksums/version/traversal/expansion | Bounded rejection before unsafe allocation/access |
| Bounds | Exact-cap and cap-plus-one for every pool/queue | No hidden growth or stalled engine work |
| Perturbation | Matched optimized OFF/Summary/Detailed/Tracy | Frozen memory/overhead gates with uncertainty/outliers |
| Lifecycle | Repeated capture/import/export, scene switches, shutdown | Drained baseline restored; no UAF/leak/new GPU validation errors |

Use deterministic barriers/fault injection, not sleeps as race proof. Run supported
sanitizers and GPU validation, recording unavailable tools and alternatives honestly.
TSan does not detect false sharing; GPU validation is not performance qualification.

Initial performance suite: steady rendering, replacement, worker-asset pressure and
telemetry-heavy fixtures, including pinned `SampleProject/project.json` and a near-capacity
fixture. Use at least 10 interleaved paired steady batches and 30 first-use/replacement
samples where relevant; predeclare duration/warm-up/uncertainty. These are experimental
minima, not assurance every effect is resolvable. Expand for variance under a recorded
analysis plan, not repeated runs until a desired outcome appears.

Lifecycle qualification: 100 warm-up + 1,000 bounded capture/stop/export-or-cancel cycles,
restoring the same reference scene and draining owners. With retention disabled, profiler
live ledgers return to warmed baseline; reserved pools stay within caps. RSS/driver usage
are trends. Existing visuals remain semantically unchanged; UI exclusions require
predeclared masks in fixed-camera comparisons.

Acceptance requires useful non-debugger local controls, honest provenance/degraded modes,
safe optional Tracy, tested portability, bounded storage and perturbation evidence.
APIs or an attractive panel alone are not production acceptance.

## 13. Delivery plan and exit gates

These **P0–P5 profiling stages** are unrelated to scene M1/M2/M3 or GitHub priority
labels. Separate PRs/branches must not silently expand #872 or make all profiler work
a prerequisite for cooked rendering/Play.

| Stage | Deliverable | Exit gate |
|---|---|---|
| P0 — Foundations | Record/ID/clock schema, concrete cap arithmetic, build propagation, safe transport/name ownership | Concurrency/overflow/shutdown tests; legacy capture strategy; baseline profile frozen |
| P1 — Local Performance | Main/render markers, flows, immutable snapshots, bounded CPU/summary UI and shared memory | Useful without Tracy; identity/age/loss visible; Summary/panel gates pass |
| P2 — GPU | Per-pass views, frame/queue epochs, capability status, links, optional calibration | No added waits; unsupported/resize/loss tests and native timing evidence |
| P3 — Memory detail | Attributed owner/resource records, bounded maps, optional stacks, reset/realloc | No double counts/false leaks; coverage/loss/overhead tests |
| P4 — Deep Tracy | Frontend fan-out, task/frame/lock hooks, Vulkan contexts and selected named pools | Connect/disconnect and CPU/GPU lifetime qualification; local summaries retained |
| P5 — Capture product | Live triggers/control, bounded export/import, comparisons, headless/overlay controls | Full failure/security/perturbation/lifecycle evidence and platform handoff |

Earlier stages require safe capture/state primitives in P0; P5 is product completion,
not a late fix for unsafe exporter/producer access. P3/P4 can proceed after their
foundation/GPU seams stabilize; expose partial capability until their gates pass.
Prefer a small complete first panel over an unbounded custom trace viewer.

Estimate P0 after the transport/Tracy audit and later PRs from accepted scope/target
profiles. No completion date or assumed access to three platforms is invented here.

## 14. Review decisions and deferred boundaries

Recommended defaults, not previously approved user decisions:

1. Keep an integrated Performance panel plus optional Tracy, sharing instrumentation
   and immutable telemetry rather than replacing either workflow.
2. Deliver bounded summaries/CPU inspection first, trustworthy GPU correlation and
   memory detail next; leave arbitrary-depth analysis to Tracy/platform tools.
3. Review the 64 MiB local Detailed ceiling and section 10 perturbation targets as
   calibration starting points; Summary stays smaller and shipping instrumentation OFF.
4. Accept explicit uncalibrated-GPU/presentation-unavailable degraded modes; never
   weaken semantics to draw apparently aligned lanes.
5. Default to sanitized local artifacts, without player analytics or remote control;
   richer paths/stacks/network diagnostics require informed opt-in.

Bring measured budget/platform/privilege tradeoffs and any shipping/network expansion
back for review. General heap interception, full driver residency, automatic scheduling/
false-sharing causality, a separate profiler process and fleet analytics remain future
proposals, not implicit completion requirements.

## 15. References

- [Memory budget reference](../memory-budget.md) and [future memory policy](memory-budget.md).
- [RenderGraph GPU timestamp design](../completed/render-graph-redesign.md) and
  [renderer resource management](render-resource-manager.md).
- [Runtime reconstruction acceptance](scene-runtime-reconstruction.md), §§9.1–9.3;
  [Play ownership](editor-play-mode.md).
- [UI system / earlier DebugOverlay sketch](../completed/ui-system.md): this proposal
  supersedes its minimal profiler roadmap, not the implementation history.
- Source: `Profiling/{Profiling,ProfilerBuffer,MemoryProfiler,ThreadCpuTime}`, `RenderGraph`,
  `GraphicRenderer`, `MemoryProfilerPanel`, `TLSFSlab`, `GpuAllocator`, `RenderResourceManager`
  and `Tetragrama/tests/SceneLifecycleProcess`.
- [Tracy upstream](https://github.com/wolfpld/tracy); use documentation matching the pinned
  client instead of assumptions from another version.
