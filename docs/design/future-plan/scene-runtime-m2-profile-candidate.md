# M2 macOS Vulkan qualification profile candidate

Status: **draft; not frozen or qualified**. This records a bounded starting point
for the acceptance profile in `scene-runtime-reconstruction.md` sections 9.1–9.5.
The renderer remains RendererOnly. The evidence below is diagnostic and does not
establish the final limits or permit a platform qualification claim.

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
not be presented as measurements of that later binary. The [capture archive](scene-runtime-evidence/sampleproject-30-20261117.tar.gz)
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
[recorded observations](scene-runtime-evidence/lifecycle-cancellation-1100-20261117.json.gz)
do not contain the original runtime logs, which were deleted by the temporary
fixture cleanup.

A targeted 8,192-draw shared-mesh fixture subsequently completed four lifecycle
cycles with one cancellation at each boundary: 0.057 ms for CPU and 13.167 ms
for GPU. Source auditing and Vulkan validation were enabled. This is one sample
per boundary for a synthetic fixture, not a latency distribution or evidence for
maximum asset complexity. The [report](scene-runtime-evidence/capacity-cancellation-8192-20261117.json)
and [runtime logs](scene-runtime-evidence/capacity-cancellation-8192-20261117.tar.gz)
are preserved with this candidate.

The same cooker and runtime binaries accepted the synthetic 8,192-draw scene
and rejected 8,193 draws both at startup and during replacement. After the
replacement refusal, the original scene rendered and a retry succeeded. The
[refusal reports and logs](scene-runtime-evidence/draw-cap-refusal-20261117.tar.gz)
record source auditing and Vulkan validation. These fixtures share a triangle,
mesh and material; they do not establish limits for asset count, texture size,
material variety, or overall scene complexity. This is a draw-cap observation,
not yet a frozen multi-dimensional profile.

A further [diagnostic run](scene-runtime-evidence/material-variety-8192-20261117.tar.gz)
rendered 8,192 draws using 64 distinct cooked material objects and completed four
lifecycle cycles. Its CPU and GPU cancellation observations were 6.772 ms and
6.591 ms respectively. The materials share a texture and most properties, and
the run provides one cancellation sample per boundary. The original run verified
distinct object digests but did not retain its cooked manifest; the probe now
retains and validates the manifest for future captures. This evidence does not
establish a texture-complexity or maximum-asset-count limit.

A subsequent [texture-overlap probe](scene-runtime-evidence/texture-overlap-capacity-20261117.tar.gz)
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

A separate [refusal-and-retry probe](scene-runtime-evidence/texture-refusal-retry-20261117.tar.gz)
started with 48 distinct textures, attempted a 96-texture replacement, and hit
the same 125-slot limit. It observed the old scene render after the failure and
successfully retried the 48-texture scene, with source auditing and Vulkan
validation enabled. This establishes recoverability for that fixture, not
support for a 96-texture replacement or a universal texture count.
The [typed-outcome rerun](scene-runtime-evidence/texture-typed-refusal-20261117.tar.gz)
also checks that the retained failure reports the preparation stage and the
`Resources` error before acknowledgement and retry.

The two cooked scenes currently prepare separate texture handles even when their
asset UUIDs and object digests match. Scene retirement queues each owned handle
for release and seals it against further writes. Any future reuse must therefore
match both identity and digest, retain an explicit shared owner across active,
candidate and GPU consumers, and defer release until the final owner retires.
Reusing a handle while retaining the current per-scene release behavior would be
unsafe. Until such sharing exists, profile admission must account for both sets
of texture slots during replacement.

The [texture-lease regression evidence](scene-runtime-evidence/texture-lease-regression-20261117.tar.gz)
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

The earlier capture's per-domain GPU figures are not suitable for calibrating
texture and render-target limits: cooked 2D images were classified as render
targets because they had attachment capability. The renderer now explicitly
charges cooked textures and environment maps to the texture domain, and a
focused budget-refusal test and source-free smoke capture verified that path.
Recollect domain high-water evidence on the final build before freezing limits.
A [single corrected-domain capture](scene-runtime-evidence/sampleproject-corrected-domains-20261117.tar.gz)
on runtime SHA-256 `14af15626c422b8d299fc2483c7a650067fc65028d510565289f0122a6533b5e`
reported replacement peaks of 316,097,536 texture bytes and 131,975,552
render-target bytes. The domain peaks are independent maxima, not a simultaneous
sum, and exclude driver residency. Its matched trace found 73 replacement frame
intervals over the steady p95 plus 8 ms, including a roughly one-second queue
submission. This is a failing diagnostic observation, not a qualified result or
proof of why those intervals occurred. One capture does not replace the required
30-sample evidence on the final build.

A [post-replacement steady trace](scene-runtime-evidence/sampleproject-post-steady-20261117.tar.gz)
captured 120 submitted frames on each side of the replacement. Render-frame p95
was 25.451 ms before, 25.825 ms during and 25.628 ms after replacement. The
similar elevated steady intervals are evidence that a slow presentation state
can persist without a load in progress; they do not explain the separate
one-second initial submission wait or prove other captures' outliers external.
This is one diagnostic run and does not qualify the performance profile.

## Drawable-wait investigation

On the later diagnostic build, an initial-load interval of 1,008.491 ms contained
a 1,006.153 ms Vulkan queue submission, of which 1.202 ms was calling-thread CPU
time. The [trace report](scene-runtime-evidence/submit-diagnostic-20261117.json)
records the full diagnostic. A [separate fresh-load thread sample](scene-runtime-evidence/macos-drawable-stack-20261117.txt.gz)
showed the render thread inside MoltenVK's `MVKPresentableSwapchainImage` waiting
in `CAMetalLayer nextDrawable`. This stack is from another process, not a stack
captured during the exact traced interval; it does not by itself establish the
cause of that interval or exclude loader influence.

A second [trace](scene-runtime-evidence/submit-async-probe-20261117.json.gz)
used `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0` with the same executable, shader
cache and viewport. It did not show the one-second submission interval, but it
reported 90 replacement intervals over its matched steady threshold, primarily
overlapping image acquisition. This configuration is an experiment, not a
recommended fix or a qualifying comparison. Both traces are single captures;
presentation scheduling and window visibility were not controlled.

A later [window-visibility trace](scene-runtime-evidence/sampleproject-window-visibility-20261117.tar.gz)
recorded the macOS window as fully occluded through its steady and replacement
windows. A separate [recovered trace](scene-runtime-evidence/sampleproject-occlusion-transition-20261117.tar.gz)
showed all 60 replacement outliers after the window became occluded, with none
before. Its collector stopped after capture because it did not yet recognize the
visibility metric, so the recovered report lacks a verified manifest identity.
These observations identify a concrete
confounder, not proof that every stall is unrelated to loading. The timing tool
can now require visibility throughout all measured windows; a hidden or unknown
window is refused rather than treated as visible-window qualification. A valid
visible-window run remains necessary. Use `--trace-steady-frames 120
--require-visible-trace` with `Scripts/sampleproject_cooked_timing.py` to check
the initial, pre-replacement, replacement and post-replacement windows. The
strict check requires macOS occlusion observations for all four windows and
preserves the diagnostic trace when visibility is not proven. For the traced
process it waits up to 15 seconds for the test window to become visible before
admitting the scene; it does not force the window in front of other applications.

In the visibility trace's initial load, the roughly 1,009 ms frame interval began
while the window was visible, and the window became fully occluded about 12 ms
into that interval. The long submission and visibility transition therefore
overlap closely. This supports window occlusion as a possible explanation; it
does not prove the cause or substitute for a visible-window measurement.

A later [visible-window trace](scene-runtime-evidence/sampleproject-visible-user-20261117.tar.gz)
passed the visibility check in all four timing windows on runtime SHA-256
`b8e3af59b4a1e7059b24f1248314ea204686de0f8b340f7a2b37eb7d37f8d211`.
Replacement had no intervals over steady p95 plus 8 ms, while initial load had
two, at 41.817 ms and 16.758 ms against a 16.482 ms threshold. The larger interval
overlapped a 40.112 ms Vulkan queue submission. This rules out full-window
occlusion as an explanation for those two observations, but does not establish
their cause. The single trace is not the required 30-sample qualification.

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
- Review the fixed-viewport backend reference and complete semantic visual
  coverage. Pass the applicable Linux, macOS and Windows CI gates; claim rendered
  qualification only for platforms with actual rendered evidence.
