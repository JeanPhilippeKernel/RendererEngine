# M2 macOS Vulkan qualification profile candidate

Status: **draft; not frozen or qualified**. This records a bounded starting point
for the acceptance profile in `scene-runtime-reconstruction.md` sections 9.1–9.5.
The renderer remains RendererOnly. The evidence below is diagnostic and does not
establish the final limits or permit a platform qualification claim.
The user moved strict performance and calibrated-resource qualification to
[follow-up #880](https://github.com/JeanPhilippeKernel/RendererEngine/issues/880)
on 2026-11-17; this does not change the limits or resolve the recorded outliers.

## Reference environment and fixture

The measured host was macOS 26.5.2 on an Apple M4 Pro with 48 GiB RAM, using
MoltenVK on the same GPU. The logical viewport was 1280 × 720 and the framebuffer
was 2560 × 1440. The operating-system filesystem cache was uncontrolled. The
fixture was a copy of SampleProject level01 with an added camera and a second
scene UUID sharing the same 32 cooked assets. The capture used validation and
source-access auditing. It is not a cold-disk measurement and does not prove a
near-limit workload.

The measurement identifies the source scene by SHA-256
`da6d6c6050200a0d31b17e05ef8b53950ef0ddc705ae4fe7f4e4dfacece9cd31`,
the cooked manifest by
`3d03885391f97d37151e890ee7de525251474e58420413c8cf22d0376afe9b22`,
the runtime executable by
`fc764de14e7e5d6bf8adc96f404942173a04fc82c547c4746d90fda6ec1b9076`,
and the 44-file shader cache by
`dfd8d94eb2a7e0afc9e34f4644052fb2d86de3e7396cb0ff5496d89717d21916`.
The executable predates the later queue-submit diagnostics; its measurements must
not be presented as measurements of that later binary. The [capture archive](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-30-20261117.tar.gz)
contains the report, per-process logs, individual observations and raw stage trace.
It does not include the executable or cooked package.

## Initial measurements and proposed limits

Thirty fresh processes and thirty same-process replacements reached an observed
render completion. The fresh p50/p95/maximum were 6.512/6.646/6.662 seconds;
replacement p50/p95/maximum were 7.071/7.428/7.464 seconds. A **12 second**
candidate limit for each gives at least 60% headroom over the observed maximum.
This is a proposal for the stated host and fixture, not a cold-cache or universal
load-time promise. First-use and later replacement results remain separate in the
full report.

In 275 CPU-boundary and 275 GPU-boundary cancellations of the synthetic stress
fixture, the maximum request-to-cleanup times were 9.799 ms and 28.498 ms. A
**50 ms** candidate cancellation limit gives at least 75% headroom over those
observations. That fixture is smaller than the required near-limit scene, so the
limit is not qualified until cancellation under the declared maximum workload is
measured. These cancellation results used a different executable from the
30-load capture and must be tied to the final build before acceptance. The
[recorded observations](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/lifecycle-cancellation-1100-20261117.json.gz)
match the original report, and the subsequently located [runtime logs](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/lifecycle-cancellation-1100-logs-20261117.tar.gz)
confirm the 1,100-cycle sequence, all 550 cancellation timings, Vulkan validation,
and zero source-access attempts. The runtime SHA-256 was
`d358e3fedeb1784c6bc700e21b9148dd91dc705580faf136c8c89db60ce9ed1e`;
this remains evidence for that older build rather than the current runtime.

The [current-runtime 1,100-cycle soak](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/lifecycle-final-1100-20261117.tar.gz)
completed 550 cancellations and 550 committed switches with Vulkan validation,
zero source-access attempts, and warmed reference allocation checks passing.
The runtime SHA-256 was
`5187b4d7bf09b3d6d1150856e523c14ef47c4b0dc4bdd159b70f08bc49ce023f`.
Across 275 cancellations at each boundary, CPU cleanup p95/maximum was
3.572/13.440 ms and GPU cleanup p95/maximum was 11.374/124.673 ms. The GPU
maximum exceeds the previously proposed 50 ms limit and must be investigated;
the limit has not been raised or qualified. This uses the small synthetic fixture,
not the declared maximum workload.
Profiling builds now log cancellation request, first render-owner observation,
and terminal mailbox timestamps. An [eight-cycle smoke](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/cancellation-mailbox-diagnostic-20261117.tar.gz)
verified the markers and source/validation checks; it did not reproduce the
124.673 ms outlier. The instrumentation changes the executable, so the preceding
1,100-cycle result remains evidence for its recorded binary, not this later one.
A [100-action diagnostic sample](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/cancellation-handoff-100-20261117.tar.gz)
on the instrumented runtime collected all 25 GPU handoffs with validation and
source auditing. GPU cancellation p95/maximum was 10.159/23.422 ms; the largest
request-to-render-observation interval was 23.259 ms and the largest observed-to-
mailbox-finished interval was 0.044 ms. These independent maxima do not establish
the cause of the earlier 124.673 ms outlier or qualify the proposed limit.
The render loop now pumps only cancelled cooked mailbox work after an invalid
frame acquisition, so minimized or unavailable surfaces need not prevent that
cleanup. The [focused Vulkan test and lifecycle smoke](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/cancellation-no-frame-20261117.tar.gz)
verify cancellation-only pumping does not start new uploads and can drain a
cancelled ticket. They do not reproduce the visible-window outlier or prove the
50 ms limit, and the change creates a later runtime binary.
The focused mailbox test additionally holds an accepted mesh upload behind an
unsignalled GPU timeline, verifies that no-frame cancellation remains pending,
then releases the timeline and drains the ticket without beginning another
frame. This verifies ownership across delayed completion, not a latency bound.
The render loop subsequently moved its cancellation-only mailbox visit ahead of
frame acquisition, without allowing more than one mailbox operation per frame.
An [eight-cycle smoke](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/cancellation-preacquire-20261117.tar.gz)
passed with validation and source auditing; its two GPU cancellation cleanup
observations were 6.698 and 9.990 ms. This does not establish a maximum or prove
that earlier presentation-related delays are eliminated. The runtime SHA-256
was `f7af4d1d0af102d3d2d2f95dfe3a84c578c431ed34e807662c8946bc7f1475e5`.
The [100-action follow-up](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/lifecycle/cancellation-preacquire-100-20261117.tar.gz)
observed 25 GPU cancellations with p95/maximum cleanup of 10.060/10.212 ms;
the maximum request-to-render-observation was 7.504 ms. Validation and source
auditing passed. This shorter sample is not proof of a 50 ms worst-case bound,
and it does not explain the earlier 124.673 ms observation.

A targeted 8,192-draw shared-mesh fixture subsequently completed four lifecycle
cycles with one cancellation at each boundary: 0.057 ms for CPU and 13.167 ms
for GPU. Source auditing and Vulkan validation were enabled. This is one sample
per boundary for a synthetic fixture, not a latency distribution or evidence for
maximum asset complexity. The [report](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/capacity-cancellation-8192-20261117.json)
and [runtime logs](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/capacity-cancellation-8192-20261117.tar.gz)
are preserved with this candidate.

The same cooker and runtime binaries accepted the synthetic 8,192-draw scene
and rejected 8,193 draws both at startup and during replacement. After the
replacement refusal, the original scene rendered and a retry succeeded. The
[refusal reports and logs](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/draw-cap-refusal-20261117.tar.gz)
record source auditing and Vulkan validation. These fixtures share a triangle,
mesh and material; they do not establish limits for asset count, texture size,
material variety, or overall scene complexity. This is a draw-cap observation,
not yet a frozen multi-dimensional profile.

A further [diagnostic run](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/material-variety-8192-20261117.tar.gz)
rendered 8,192 draws using 64 distinct cooked material objects and completed four
lifecycle cycles. Its CPU and GPU cancellation observations were 6.772 ms and
6.591 ms respectively. The materials share a texture and most properties, and
the run provides one cancellation sample per boundary. The original run verified
distinct object digests but did not retain its cooked manifest; the probe now
retains and validates the manifest for future captures. This evidence does not
establish a texture-complexity or maximum-asset-count limit.

A subsequent [texture-overlap probe](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/texture-overlap-capacity-20261117.tar.gz)
used 8,192 draws, 64 materials and 48 distinct one-pixel textures through four
lifecycle cycles, with auditing and validation enabled. On the same device a
64-texture replacement hit the runtime's 125 live texture slots while the prior
scene and engine resources were still resident. The active generation was
retained, but the stress process then reported failure; this is not a successful
retry test. The probe exposed a separate closure-lease exhaustion bug, now fixed
by releasing decoded texture leases after upload-owned staging accepts the data.
The slot count is shared with engine resources and concurrent candidates, so it
must not be interpreted as a per-scene texture allowance or a guarantee for
larger textures. The 48-texture result is one synthetic observation and does not
qualify the refused 64-texture overlap.

The [post-delegate-change rerun](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/capacity-metal-delegate-20261117.tar.gz)
retains the fixture manifest and logs for 8,192 draws, 64 materials and 48
one-pixel textures over four lifecycle cycles. It enabled Vulkan validation and
source auditing, retained the active rendered scene after both cancellations,
and observed CPU and GPU cancellation cleanup times of 0.049 ms and 32.102 ms.
The two replacement admission-to-submission observations were 13.042 and
12.988 seconds. These are single-run observations for this synthetic workload,
not the 30-sample SampleProject profile or a qualified maximum-workload limit.
The runtime SHA-256 was
`5187b4d7bf09b3d6d1150856e523c14ef47c4b0dc4bdd159b70f08bc49ce023f`.
A [later run after invalid-frame cancellation changes](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/capacity-current-20261117.tar.gz)
repeated the same 8,192-draw, 64-material, 48-texture fixture and four lifecycle
cycles with validation and source auditing enabled. It observed CPU/GPU cleanup
times of 0.052/13.095 ms and a tracked admitted-GPU peak of 1,532,985,216 bytes.
The retained fixture report records 8,193 entities including its camera; its
two-scene package manifest contains 114 assets. The entities share a synthetic
mesh, so these counts do not establish a general entity or asset limit.
These single observations do not qualify maximum workload latency or full driver
residency; the runtime SHA-256 was
`9b6f46f5aa80920aa0927ef3e79f2393e0725c82f6dd974a69073a82032c8291`.
On the earlier post-delegate runtime (`5187b4d7…`), an [8,193-draw replacement refusal](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/draw-refusal-metal-delegate-20261117.tar.gz)
preserved the active scene, rendered it after failure, and accepted a valid
retry. Vulkan validation and source auditing were enabled. This verifies the
draw limit's recovery path for the synthetic fixture, not every resource cap.
The [current-runtime rerun](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/draw-refusal-current-20261117.tar.gz)
also refused 8,193 draws, retained and rendered the active scene, and successfully
retried, with validation and source auditing enabled. The runtime SHA-256 was
`9b6f46f5aa80920aa0927ef3e79f2393e0725c82f6dd974a69073a82032c8291`.

An [editor-hosted package-failure smoke](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/failures/package-failures-current-20261117.txt)
on the same runtime rejected missing, corrupt, wrong-target and wrong-format
packages, as well as an incorrect expected draw count. The fixture's authoring
copy was removed before launch and Vulkan validation was required for the
successful initial run. This runner does not preserve per-process logs for these
failure cases or apply the standalone source-access audit; it supplements rather
than replaces the detailed failure and standalone evidence.
The [standalone failure run](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/failures/standalone-package-failures-20261117.tar.gz)
then exercised those cases on the same runtime with the authoring copy removed,
source auditing and Vulkan validation enabled. Each damaged package failed
before publication, shut down cleanly and reported zero denied source, registry
and import accesses. The archive retains the individual process logs; the opt-in
`CookedDesktop.StandalonePackageFailures` test repeats this coverage.

A separate [refusal-and-retry probe](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/texture-refusal-retry-20261117.tar.gz)
started with 48 distinct textures, attempted a 96-texture replacement, and hit
the same 125-slot limit. It observed the old scene render after the failure and
successfully retried the 48-texture scene, with source auditing and Vulkan
validation enabled. This establishes recoverability for that fixture, not
support for a 96-texture replacement or a universal texture count.
The [typed-outcome rerun](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/texture-typed-refusal-20261117.tar.gz)
also checks that the retained failure reports the preparation stage and the
`Resources` error before acknowledgement and retry.
The [post-delegate-change rerun](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/texture-refusal-metal-delegate-20261117.tar.gz)
repeated the 48-active/96-candidate texture overlap refusal and valid retry on
runtime SHA-256 `5187b4d7bf09b3d6d1150856e523c14ef47c4b0dc4bdd159b70f08bc49ce023f`,
with validation and source auditing enabled. This remains a recovery check,
not a qualified per-scene texture capacity.

The two cooked scenes currently prepare separate texture handles even when their
asset UUIDs and object digests match. Scene retirement queues each owned handle
for release and seals it against further writes. Any future reuse must therefore
match both identity and digest, retain an explicit shared owner across active,
candidate and GPU consumers, and defer release until the final owner retires.
Reusing a handle while retaining the current per-scene release behavior would be
unsafe. Until such sharing exists, profile admission must account for both sets
of texture slots during replacement.

The [texture-lease regression evidence](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/capacity/texture-lease-regression-20261117.tar.gz)
contains the original failed 64-texture startup log, the targeted upload-ownership
test, two Vulkan cancellation/refusal recovery tests, and focused Linux sanitizer
results. The original startup failure was generic and did not itself identify the
exhausted lease limit; the ownership test and subsequent successful initial
64-texture load support the fix. The Linux container compiled the affected files
from the current tree in an older cached source snapshot and had no GPU; it is not
a full current-worktree or rendered Linux qualification.

SampleProject replacement peaked at 173,265,956 bytes in the tracked CPU pool,
1,894,241,664 admitted GPU bytes, and 389 of 2,047 retirement entries. GPU domain
peaks are recorded individually in the report and need not have occurred at the
same time. GPU admitted bytes are an allocator ledger, not driver residency or
whole-process memory. The runtime's existing safety ceilings include a 1 GiB CPU
pool, a 256 MiB preparation-staging pool, a 16 MiB metadata pool, and a 4 GiB
total GPU admission limit; these are implementation ceilings, not calibrated
supported-scene-size claims.
The standalone runtime also configures `MaxAssets` to 8,192 and both
`MaxObjectBytes` and `MaxMeshWorkspaceBytes` to 512 MiB. These are separate
input/workspace ceilings, not additional guarantees that an 8,192-asset scene or
512 MiB object fits in the shared CPU pool. Their combined maximum-workload and
limit-plus-one behavior is not established by the synthetic texture fixture.

The earlier capture's per-domain GPU figures are not suitable for calibrating
texture and render-target limits: cooked 2D images were classified as render
targets because they had attachment capability. The renderer now explicitly
charges cooked textures and environment maps to the texture domain, and a
focused budget-refusal test and source-free smoke capture verified that path.
Recollect domain high-water evidence on the final build before freezing limits.
A [single corrected-domain capture](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-corrected-domains-20261117.tar.gz)
on runtime SHA-256 `14af15626c422b8d299fc2483c7a650067fc65028d510565289f0122a6533b5e`
reported replacement peaks of 316,097,536 texture bytes and 131,975,552
render-target bytes. The domain peaks are independent maxima, not a simultaneous
sum, and exclude driver residency. Its matched trace found 73 replacement frame
intervals over the steady p95 plus 8 ms, including a roughly one-second queue
submission. This is a failing diagnostic observation, not a qualified result or
proof of why those intervals occurred. One capture does not replace the required
30-sample evidence on the final build.

A [post-replacement steady trace](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-post-steady-20261117.tar.gz)
captured 120 submitted frames on each side of the replacement. Render-frame p95
was 25.451 ms before, 25.825 ms during and 25.628 ms after replacement. The
similar elevated steady intervals are evidence that a slow presentation state
can persist without a load in progress; they do not explain the separate
one-second initial submission wait or prove other captures' outliers external.
This is one diagnostic run and does not qualify the performance profile.

## Drawable-wait investigation

On the later diagnostic build, an initial-load interval of 1,008.491 ms contained
a 1,006.153 ms Vulkan queue submission, of which 1.202 ms was calling-thread CPU
time. The [trace report](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/submit-diagnostic-20261117.json)
records the full diagnostic. A [separate fresh-load thread sample](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/macos-drawable-stack-20261117.txt.gz)
showed the render thread inside MoltenVK's `MVKPresentableSwapchainImage` waiting
in `CAMetalLayer nextDrawable`. This stack is from another process, not a stack
captured during the exact traced interval; it does not by itself establish the
cause of that interval or exclude loader influence.

A second [trace](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/submit-async-probe-20261117.json.gz)
used `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` with the same executable, shader
cache and viewport. It did not show the one-second submission interval, but it
reported 90 replacement intervals over its matched steady threshold, primarily
overlapping image acquisition. This configuration is an experiment, not a
recommended fix or a qualifying comparison. Both traces are single captures;
presentation scheduling and window visibility were not controlled.

A later [window-visibility trace](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-window-visibility-20261117.tar.gz)
recorded the macOS window as fully occluded through its steady and replacement
windows. A separate [recovered trace](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-occlusion-transition-20261117.tar.gz)
showed all 60 replacement outliers after the window became occluded, with none
before. Its collector stopped after capture because it did not yet recognize the
visibility metric, so the recovered report lacks a verified manifest identity.
These observations identify a concrete
confounder, not proof that every stall is unrelated to loading. The timing tool
can now require visibility throughout all measured windows; a hidden or unknown
window is refused rather than treated as visible-window qualification. A valid
visible-window run remains necessary. Use `--trace-steady-frames 120
--require-visible-trace` with `tests/scene/sampleproject_cooked_timing.py` to check
the initial, pre-replacement, replacement and post-replacement windows. The
strict check requires macOS occlusion observations for all four trace windows
and for every timed load through observed render completion. It preserves the
diagnostic trace when trace-window visibility is not proven. Each test process
waits up to 15 seconds for its window to become visible before admitting the
scene; it does not force the window in front of other applications. Earlier
timing reports only checked the separate trace process and cannot establish
visibility for all of their individual timing samples.

In the visibility trace's initial load, the roughly 1,009 ms frame interval began
while the window was visible, and the window became fully occluded about 12 ms
into that interval. The long submission and visibility transition therefore
overlap closely. This supports window occlusion as a possible explanation; it
does not prove the cause or substitute for a visible-window measurement.

A later [visible-window trace](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-visible-user-20261117.tar.gz)
passed the visibility check in all four timing windows on runtime SHA-256
`b8e3af59b4a1e7059b24f1248314ea204686de0f8b340f7a2b37eb7d37f8d211`.
Replacement had no intervals over steady p95 plus 8 ms, while initial load had
two, at 41.817 ms and 16.758 ms against a 16.482 ms threshold. The larger interval
overlapped a 40.112 ms Vulkan queue submission. This rules out full-window
occlusion as an explanation for those two observations, but does not establish
their cause. The single trace is not the required 30-sample qualification.

The [post-delegate visible-window run](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-visible-final-20261117.tar.gz)
completed 30 fresh loads and 30 replacements with visibility verified during
each measured load. Fresh observed GPU-completion p50/p95/maximum were
5.443/5.468/5.472 seconds; replacement values were 5.362/5.413/5.422 seconds.
The filesystem cache was uncontrolled, and completion is a polling observation,
not presentation. In the separate visible trace, initial loading had one
33.394 ms render interval against a steady-p95-plus-8-ms threshold of 16.525 ms;
31.586 ms overlapped queue submission. Replacement had no such outliers in that
trace. The cause remains unproven, so this run does not satisfy the frame-stall
qualification or show that the delegate change fixed the issue.
Inspection of that outlier shows 0.497 ms of measured render-thread CPU time
inside the 31.585 ms queue-submit call; the remaining 31.088 ms is non-CPU time,
which can include blocking or descheduling. The immediately preceding recorded
cooked upload visit took 0.032 ms. These observations narrow the location of the
delay but do not identify its cause or prove whether loading contributed.
The original report marked two tiny environment scopes in each loading window
as unassigned because their complete frame ended just after the window. The
analyzer now includes complete boundary-crossing frames; re-analysis assigns all
four scopes, leaving the initial/replacement environment maxima unchanged at
3.438/0.213 ms. This corrects attribution, not the unexplained frame outlier or
the absence of a current-build qualification trace.

A temporary macOS experiment requested three swapchain images instead of two,
within surface limits. Its [visible trace](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/performance/sampleproject-triple-drawable-20261117.tar.gz)
confirmed three images and recorded initial/replacement interval maxima of
16.926/18.327 ms, below that run's steady-p95-plus-8-ms threshold of 19.269 ms.
Its steady p95 was 11.269 ms, versus 8.525 ms in the earlier two-image trace.
These are individual runs, not controlled distributions; they neither prove
improvement nor explain the original outlier. The experiment was not retained
as a renderer default, and does not qualify the frame-stall requirement.

After configuring the Metal layer's hosting view as its delegate (commit
`b6477ba0`), a [fixed-window SampleProject capture](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/visual/sampleproject-metal-delegate-20261117.tar.gz)
confirmed that the delegate was installed in both source and cooked runs. Vulkan
validation was enabled, the cooked source-access audit passed, and source and
cooked captures matched pixel for pixel. Their pixel hash also matches the
existing fixed-viewport reference, which the user subsequently approved. This
run did not measure the visible-window stalls or establish a performance
improvement.
After the invalid-frame cancellation change, a [new fixed-window capture](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/visual/sampleproject-approved-current-20261117.tar.gz)
matched the user-approved reference and the source render pixel for pixel, with
Vulkan validation and standalone source auditing enabled. Its runtime SHA-256 is
`9b6f46f5aa80920aa0927ef3e79f2393e0725c82f6dd974a69073a82032c8291`.
This confirms this fixture's reference match; it does not qualify the outstanding
performance and resource limits.
The [post-scheduling capture](https://github.com/JeanPhilippeKernel/RendererEngine/blob/0532bb35d45f2aae42fa2dbb3cd9e3d3711e1165/tests/scene/evidence/visual/sampleproject-approved-preacquire-20261117.tar.gz)
also matched both the source render and approved reference pixel for pixel,
with validation and source auditing enabled. Its runtime SHA-256 is
`f7af4d1d0af102d3d2d2f95dfe3a84c578c431ed34e807662c8946bc7f1475e5`.

## Work required before freezing

- Pin the final executable, cooker, shaders, driver and fixture manifests; preserve
  the raw evidence. Declare measured entity, draw and asset caps and verify each
  cap and cap-plus-one on the final build.
- Establish finite scene/document/scratch/cache, staging, GPU residency and
  queue limits including active/candidate overlap and pinned old versions. Record
  headroom and account for memory outside the allocator ledger.
- Measure load and cancellation latency at the declared workload limits. Verify
  4 ms aggregate loading work, 2 ms guarded commit, and the 8 ms matched-steady
  frame-stall requirement without discarding unexplained outliers.
- Resolve the initial-load Vulkan submission outlier. A separate diagnostic
  sample observed MoltenVK waiting in `CAMetalLayer nextDrawable`; that is not
  yet proof that every observed outlier is unrelated to loading. An asynchronous
  MoltenVK submission probe shifted waiting to image acquisition and produced
  replacement outliers, so it is not an accepted workaround.
- Complete semantic visual coverage against the approved fixed-viewport backend
  reference. Pass the applicable Linux, macOS and Windows CI gates; claim rendered
  qualification only for platforms with actual rendered evidence.
