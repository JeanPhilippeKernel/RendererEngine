#!/usr/bin/env python3
"""Run the asset-bearing cooked scene in a fresh desktop Vulkan process.

Requires built SceneCookProcess and Obelisk binaries and a usable desktop GPU.
This is a startup/lifetime smoke test, not a pixel or performance qualification.
"""
import argparse
import hashlib
import json
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from validate_scene_lifecycle import distribution


SCENE = "10000000-0000-0000-0000-000000000001"
ALTERNATE_SCENE = "90000000-0000-0000-0000-000000000009"
MISSING_SCENE = "90000000-0000-0000-0000-000000000010"


def sha256_file(path):
    with pathlib.Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def read_process_logs(directory):
    """Read spdlog's rotated files oldest first and reject possible data loss."""
    directory = pathlib.Path(directory)
    rotated = {int(path.name.removeprefix("engine_dump.").removesuffix(".log")): path
               for path in directory.glob("engine_dump.*.log")
               if path.name.removeprefix("engine_dump.").removesuffix(".log").isdigit()}
    if any(index < 1 or index >= 5 for index in rotated):
        raise ValueError("process log reached its rotation limit; early evidence may be lost")
    if rotated and sorted(rotated) != list(range(1, max(rotated) + 1)):
        raise ValueError("process log has a missing rotated segment")
    paths = [rotated[index] for index in sorted(rotated, reverse=True)] + [directory / "engine_dump.log"]
    return "".join(path.read_text(encoding="utf-8") for path in paths)


def read_console_log(path):
    with pathlib.Path(path).open("rb") as source:
        data = source.read(16 * 1024 * 1024 + 1)
    if len(data) > 16 * 1024 * 1024:
        raise ValueError("process console output exceeds its evidence limit")
    return data.decode("utf-8", errors="replace")


def require_clean_vulkan_console(text):
    for line in text.splitlines():
        lowered = line.lower()
        if any(marker in lowered for marker in ("vuid-", "validation error", "vk_error_validation_failed", "vk_not_ready")):
            raise ValueError(f"Vulkan console validation failure: {line[:500]}")


def require_zero_source_audit(log):
    """Require one complete shutdown audit, with every counter exactly zero."""
    lines = [line for line in log.splitlines() if "[CookedSourceAudit]" in line]
    if len(lines) != 1:
        raise ValueError("missing or repeated cooked source-access audit")
    match = re.search(r"\[CookedSourceAudit\] vfs_denied=(\d+) registry_denied=(\d+) imports_denied=(\d+)(?:\s|$)", lines[0])
    if not match or tuple(map(int, match.groups())) != (0, 0, 0):
        raise ValueError("cooked source-access audit is malformed or reports denied lookups")


def validation_layer_status_text(log):
    lines = [line for line in log.splitlines() if "[VulkanValidation]" in line]
    if len(lines) != 1:
        raise ValueError("missing or repeated Vulkan validation-layer status")
    match = re.search(r"\[VulkanValidation\] khronos_enabled=([01])(?:\s|$)", lines[0])
    if not match:
        raise ValueError("malformed Vulkan validation-layer status")
    return match.group(1) == "1"


def validate_retry_publications(log):
    """The failure probe retries the original scene after preserving it."""
    publications = [(int(generation), scene) for generation, scene in re.findall(
        r"\[CookedRuntime\] Published cooked scene generation (\d+) scene=([0-9a-f-]+)", log)]
    if publications != [(2, SCENE), (3, SCENE)]:
        raise ValueError("failure retry published an unexpected scene or generation")


def read_cancellation_timings(log, cycles):
    matches = re.findall(r"\[CookedCancelTiming\] cancelled=(\d+) boundary=(cpu|gpu) request_ns=(\d+) cleanup_ns=(\d+) elapsed_ns=(\d+)", log)
    expected = (cycles + 1) // 2
    if len(matches) != expected:
        raise ValueError("missing or repeated cancellation timing observations")
    samples = []
    for index, (ordinal, boundary, requested, cleanup, elapsed) in enumerate(matches, 1):
        requested, cleanup, elapsed = map(int, (requested, cleanup, elapsed))
        if int(ordinal) != index or boundary != ("cpu" if index % 2 else "gpu") or requested <= 0 or cleanup < requested or elapsed != cleanup - requested:
            raise ValueError("invalid cancellation timing observation")
        samples.append({"cancelled": index, "boundary": boundary, "request_ns": requested, "cleanup_ns": cleanup, "elapsed_ns": elapsed})
    return samples


def validate_stress_sequence(log, cycles):
    """Check exact scene identities and post-cancellation generations."""
    publications = [(int(generation), scene) for generation, scene in re.findall(
        r"\[CookedRuntime\] Published cooked scene generation (\d+) scene=([0-9a-f-]+)", log)]
    committed = cycles // 2
    expected = [(generation, SCENE if generation % 2 == 0 else ALTERNATE_SCENE)
                for generation in range(2, committed + 3)]
    if publications != expected:
        raise ValueError("stress published an unexpected scene or generation")
    frames = [tuple(map(int, match)) for match in re.findall(
        r"\[CookedStress\] cancelled=(\d+) generation=(\d+) rendered_after_cleanup=1", log)]
    cancelled = (cycles + 1) // 2
    if frames != [(index, index + 1) for index in range(1, cancelled + 1)]:
        raise ValueError("stress cancellation did not retain the expected scene generation")


def require_stable_stress_window(log):
    """A resized render target invalidates a same-ledger comparison."""
    baseline = log.find("[CookedTiming] generation=2 ")
    if baseline < 0:
        raise ValueError("stress run has no initial timing baseline")
    if "Window size updated" in log[baseline:] or "Swapchain recreated:" in log[baseline:]:
        raise ValueError("stress viewport changed after the initial baseline; allocation comparisons require a fixed viewport")
    dimensions = re.findall(r"\[ZUI\] UIScale=[^ ]+ Screen=(\d+)x(\d+) Window=(\d+)x(\d+) Framebuffer=(\d+)x(\d+)", log[:baseline])
    if not dimensions:
        raise ValueError("stress run did not record initial window and framebuffer dimensions")
    screen_w, screen_h, window_w, window_h, fb_w, fb_h = map(int, dimensions[-1])
    if (window_w, window_h) != (1280, 720) or min(screen_w, screen_h, fb_w, fb_h) <= 0:
        raise ValueError("stress run did not use the configured 1280x720 window")
    return {"scene_viewport": [screen_w, screen_h], "window": [window_w, window_h], "framebuffer": [fb_w, fb_h]}


def require_clean_stress_log(log):
    if re.search(r"\[(?:error|critical)\]", log, re.IGNORECASE):
        raise ValueError("stress run contains an engine error or critical log")
    require_no_environment_fallback(log)


def require_no_environment_fallback(log):
    if re.search(r"\[SkyEnvironment\].*using the fallback", log, re.IGNORECASE):
        raise ValueError("cooked run used an environment fallback")


def observed_timing_viewport(log):
    baseline = log.find("[CookedTiming] generation=2 ")
    if baseline < 0:
        return None
    records = re.findall(r"\[ZUI\] UIScale=[^ ]+ Screen=(\d+)x(\d+) Window=(\d+)x(\d+) Framebuffer=(\d+)x(\d+)", log[:baseline])
    if not records:
        return None
    screen_w, screen_h, window_w, window_h, fb_w, fb_h = map(int, records[-1])
    after = log[baseline:]
    return {"scene_viewport": [screen_w, screen_h], "window": [window_w, window_h], "framebuffer": [fb_w, fb_h],
            "resize_after_baseline": "Window size updated" in after or "Swapchain recreated:" in after}


def read_lifecycle_observations(log, generations, require_gpu_completion=False, require_cache=True, require_gpu_budget=False):
    pattern = (r"\[CookedLifecycle\] generation=(\d+) cpu_live=(\d+) cpu_peak=(\d+) staging_live=(\d+) staging_peak=(\d+) "
               r"metadata_live=(\d+) metadata_blocks=(\d+) gpu_live=(\d+) gpu_peak=(\d+) "
               r"retirement_queued=(\d+) retirement_draining=(\d+)")
    fields = ("generation", "cpu_live", "cpu_peak", "staging_live", "staging_peak", "metadata_live", "metadata_blocks",
              "gpu_live", "gpu_peak", "retirement_queued", "retirement_draining")
    samples = [dict(zip(fields, map(int, match))) for match in re.findall(pattern, log)]
    if [sample["generation"] for sample in samples] != generations:
        raise RuntimeError("missing cooked lifecycle memory observations")
    caches = [tuple(map(int, match)) for match in re.findall(
        r"\[CookedCache\] generation=(\d+) closures=(\d+) leases=(\d+) retiring=(\d+) asset_versions=(\d+)", log)]
    if (require_cache or caches) and [cache[0] for cache in caches] != generations:
        raise RuntimeError("missing cooked closure cache observations")
    for sample, (_, closures, leases, retiring, asset_versions) in zip(samples, caches):
        sample.update(cache_closures=closures, cache_leases=leases, cache_retiring=retiring, cache_asset_versions=asset_versions)
    gpu_budgets = [tuple(map(int, match)) for match in re.findall(
        r"\[CookedGpuBudget\] generation=(\d+) admitted_peak=(\d+) geometry_peak=(\d+) texture_peak=(\d+) target_peak=(\d+) uniform_peak=(\d+) staging_peak=(\d+) readback_peak=(\d+)", log)]
    if (require_gpu_budget or gpu_budgets) and [item[0] for item in gpu_budgets] != generations:
        raise RuntimeError("missing or reordered GPU budget observations")
    gpu_fields = ("gpu_admitted_peak", "gpu_geometry_peak", "gpu_texture_peak", "gpu_target_peak", "gpu_uniform_peak", "gpu_staging_peak", "gpu_readback_peak")
    for sample, record in zip(samples, gpu_budgets):
        values = record[1:]
        limits = (4 << 30, 4 << 30, 4 << 30, 4 << 30, 4 << 30, 512 << 20, 256 << 20)
        if any(value > limit for value, limit in zip(values, limits)) or any(value > values[0] for value in values[1:]):
            raise RuntimeError("GPU admitted peak exceeds configured bounds")
        sample.update(zip(gpu_fields, values))
    retirement_budgets = [tuple(map(int, match)) for match in re.findall(
        r"\[CookedRetirementBudget\] generation=(\d+) capacity=(\d+) high_water=(\d+)", log)]
    if (require_gpu_budget or retirement_budgets) and [item[0] for item in retirement_budgets] != generations:
        raise RuntimeError("missing or reordered retirement budget observations")
    for sample, (_, capacity, high_water) in zip(samples, retirement_budgets):
        if not capacity or high_water > capacity:
            raise RuntimeError("retirement high-water exceeds configured capacity")
        sample.update(retirement_capacity=capacity, retirement_high_water=high_water)
    timings = [tuple(map(int, match)) for match in re.findall(
        r"\[CookedTiming\] generation=(\d+) admission_ns=(\d+) first_submission_ns=(\d+) elapsed_ns=(\d+)", log)]
    if [timing[0] for timing in timings] != generations:
        raise RuntimeError("missing cooked admission-to-submission timing observations")
    for sample, (_, admission, submission, elapsed) in zip(samples, timings):
        if not admission or submission < admission or elapsed != submission - admission:
            raise RuntimeError("invalid cooked admission-to-submission timing")
        sample.update(admission_ns=admission, first_submission_ns=submission, admission_to_submission_ns=elapsed)
    completions = [tuple(map(int, match)) for match in re.findall(
        r"\[CookedGpuCompletion\] generation=(\d+) observed_ns=(\d+) timeline_value=(\d+)", log)]
    if require_gpu_completion and [value[0] for value in completions] != generations:
        raise RuntimeError("missing render-completion observations")
    if completions:
        if [value[0] for value in completions] != generations:
            raise RuntimeError("partial or reordered render-completion observations")
        for sample, (_, observed, timeline) in zip(samples, completions):
            if observed < sample["first_submission_ns"] or not timeline:
                raise RuntimeError("invalid render-completion observation")
            sample.update(render_completion_observed_ns=observed, render_timeline_value=timeline,
                          admission_to_completion_observed_ns=observed - sample["admission_ns"])
    if any(sample["cpu_live"] > 1024 * 1024 * 1024 or sample["cpu_peak"] > 1024 * 1024 * 1024 or sample["staging_live"] > 256 * 1024 * 1024 or sample["staging_peak"] > 256 * 1024 * 1024 or sample["metadata_live"] > 16 * 1024 * 1024 or sample["gpu_live"] > sample["gpu_peak"] for sample in samples):
        raise RuntimeError("cooked lifecycle memory observations exceed their configured bounds")
    return samples


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cooker", required=True, type=pathlib.Path)
    parser.add_argument("--obelisk", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--failure-cases", action="store_true", help="also reject missing, corrupt and incompatible package objects")
    parser.add_argument("--capture", type=pathlib.Path, help="write a scene viewport RGB8 PPM after rendering settles")
    parser.add_argument("--verify-capture", action="store_true", help="capture and validate a temporary standalone fixture image")
    parser.add_argument("--reloads", type=int, default=0, help="alternate two cooked scenes this many times")
    parser.add_argument("--fresh-processes", type=int, default=1, help="run this many fresh processes against one prepared package")
    parser.add_argument("--include-alternate", action="store_true", help="include a second cooked scene without requesting a replacement")
    parser.add_argument("--runtime-camera", help="use the fixture's authored camera: 'main' or an explicit UUID")
    parser.add_argument("--standalone", action="store_true", help="run the authored-camera fixture in the renderer-only host")
    parser.add_argument("--audit-source", action="store_true", help="deny and count source VFS, registry, and import lookups")
    parser.add_argument("--observe-gpu-completion", action="store_true", help="observe completion of the first accepted render submission")
    parser.add_argument("--require-validation", action="store_true", help="require the Khronos Vulkan validation layer to be enabled")
    parser.add_argument("--lifecycle-stress", action="store_true", help="alternate CPU and recorded-upload cancellations with committed scene switches")
    parser.add_argument("--fixed-window", action="store_true", help="use a non-resizable 1280x720 logical test window")
    parser.add_argument("--reject-runtime-camera", action="store_true", help="expect the selected runtime camera to be rejected")
    faults = parser.add_mutually_exclusive_group()
    faults.add_argument("--cancel-during-upload", action="store_true", help="cancel the first replacement after recording its mesh upload")
    faults.add_argument("--cancel-during-cpu", action="store_true", help="cancel the first replacement while the CPU worker owns its reconstruction")
    faults.add_argument("--fail-preflight", action="store_true", help="refuse publication after replacement resources are ready")
    faults.add_argument("--fail-staging", action="store_true", help="refuse the replacement's GPU staging allocation")
    faults.add_argument("--reject-submit", action="store_true", help="reject the replacement's recorded GPU batch before submission")
    faults.add_argument("--missing-replacement", action="store_true", help="request a scene absent from the cooked package after the first scene renders")
    faults.add_argument("--corrupt-replacement", action="store_true", help="corrupt only the replacement scene object before launch")
    faults.add_argument("--missing-replacement-material", action="store_true", help="remove a material required only by the replacement")
    faults.add_argument("--corrupt-replacement-material", action="store_true", help="corrupt a material required only by the replacement")
    faults.add_argument("--missing-replacement-camera", action="store_true", help="omit the authored camera from the replacement scene")
    faults.add_argument("--device-loss", action="store_true", help="inject device loss during a standalone replacement upload")
    faults.add_argument("--shutdown-cpu", action="store_true", help="close while a replacement worker is active")
    faults.add_argument("--shutdown-gpu", action="store_true", help="close with a recorded replacement upload")
    parser.add_argument("--retry-after-failure", action="store_true", help="retry the original scene after a replacement fault")
    parser.add_argument("--supersede-reload", action="store_true", help="supersede the first waiting replacement with a newer scene")
    parser.add_argument("--supersede-during-cpu", action="store_true", help="supersede a replacement while its CPU worker holds resources")
    parser.add_argument("--supersede-during-upload", action="store_true", help="supersede a replacement after recording its mesh upload")
    parser.add_argument("--evidence", type=pathlib.Path, help="write per-generation memory and submission timing observations as new JSON")
    parser.add_argument("--cache-conditions", default="uncontrolled; fresh process does not imply cold I/O", help="describe filesystem cache conditions; does not clear caches")
    parser.add_argument("--stage-trace", type=pathlib.Path, help="write a bounded SceneLoad stage CSV from the cooked process")
    parser.add_argument("--steady-frames", type=int, default=0, help="observe settled standalone frames before replacement")
    args = parser.parse_args()
    cooker, obelisk = args.cooker.resolve(strict=True), args.obelisk.resolve(strict=True)
    binary_hashes = {"cooker_sha256": sha256_file(cooker), "runtime_sha256": sha256_file(obelisk)}
    if args.timeout <= 0 or not 0 <= args.reloads <= (1100 if args.lifecycle_stress else 1000) or not 1 <= args.fresh_processes <= 1000:
        parser.error("--timeout must be positive; --reloads and --fresh-processes must be within their bounds")
    if not args.cache_conditions.strip():
        parser.error("--cache-conditions must describe the observed cache state")
    if args.lifecycle_stress and (not args.standalone or not args.reloads or args.runtime_camera is None or args.retry_after_failure or args.cancel_during_upload or args.cancel_during_cpu or args.fail_staging or args.fail_preflight or args.reject_submit or args.missing_replacement or args.corrupt_replacement or args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.device_loss or args.supersede_reload or args.supersede_during_cpu or args.supersede_during_upload or args.capture or args.steady_frames):
        parser.error("--lifecycle-stress requires standalone replacements without other probes")
    if args.audit_source and not args.standalone:
        parser.error("--audit-source requires --standalone")
    if args.fixed_window and not args.standalone:
        parser.error("--fixed-window requires --standalone")
    if args.observe_gpu_completion and (not args.standalone or args.device_loss or args.shutdown_cpu or args.shutdown_gpu):
        parser.error("--observe-gpu-completion requires standalone non-fatal execution")
    if args.include_alternate and (not args.standalone or args.runtime_camera is None or args.reloads or args.failure_cases):
        parser.error("--include-alternate requires a standalone authored-camera startup without replacements or package-failure tests")
    if (args.shutdown_cpu or args.shutdown_gpu) and (not args.standalone or args.reloads != 1 or args.retry_after_failure or args.lifecycle_stress or args.failure_cases or args.capture or args.steady_frames or args.supersede_reload or args.supersede_during_cpu or args.supersede_during_upload):
        parser.error("shutdown probes require one standalone replacement without other probes")
    if args.steady_frames < 0 or args.steady_frames > 600 or (args.steady_frames and (not args.standalone or args.fresh_processes != 1 or args.capture or args.verify_capture or args.failure_cases or args.retry_after_failure or args.supersede_reload or args.supersede_during_cpu or args.supersede_during_upload or args.reject_runtime_camera)):
        parser.error("--steady-frames requires a standalone run without capture or fault probes")
    if args.evidence is not None and args.evidence.exists():
        parser.error("--evidence must name a new file")
    evidence_log = args.evidence.with_suffix(args.evidence.suffix + ".engine.log") if args.evidence else None
    if evidence_log is not None and (evidence_log.exists() or (args.fresh_processes > 1 and any(evidence_log.parent.glob(evidence_log.name + ".process-*.log")))):
        parser.error("the evidence log path must not already exist")
    if args.stage_trace is not None and (args.stage_trace.exists() or args.failure_cases):
        parser.error("--stage-trace must name a new file and cannot be combined with --failure-cases")
    if (args.cancel_during_upload or args.cancel_during_cpu or args.fail_staging or args.fail_preflight or args.reject_submit or args.missing_replacement or args.corrupt_replacement or args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.device_loss) and (args.reloads != 1 or args.failure_cases or args.capture):
        parser.error("replacement fault probes require --reloads 1 and no capture or failure cases")
    if args.retry_after_failure and not (args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.missing_replacement or args.cancel_during_cpu or args.corrupt_replacement or args.cancel_during_upload or args.fail_staging or args.fail_preflight or args.reject_submit):
        parser.error("--retry-after-failure requires a replacement fault probe")
    if (args.corrupt_replacement_material or args.missing_replacement_material) and (not args.standalone or args.runtime_camera != "main" or not args.retry_after_failure):
        parser.error("replacement material faults require standalone main camera and retry")
    if args.missing_replacement_camera and (not args.standalone or args.runtime_camera != "main" or not args.retry_after_failure):
        parser.error("--missing-replacement-camera requires standalone main camera and retry")
    if args.fail_preflight and (not args.standalone or not args.retry_after_failure):
        parser.error("--fail-preflight requires standalone and retry")
    if args.reject_submit and not args.retry_after_failure:
        parser.error("--reject-submit requires --retry-after-failure")
    if args.device_loss and (not args.standalone or args.retry_after_failure or args.steady_frames or args.stage_trace):
        parser.error("--device-loss requires a standalone replacement without retry or timing probes")
    if args.fresh_processes > 1 and (args.reloads or args.failure_cases or args.capture or args.stage_trace or args.retry_after_failure):
        parser.error("multiple fresh processes cannot be combined with replacements, faults, capture, or stage tracing")
    if (args.supersede_reload or args.supersede_during_cpu or args.supersede_during_upload) and (args.reloads != 1 or sum((args.supersede_reload, args.supersede_during_cpu, args.supersede_during_upload)) > 1 or args.failure_cases or args.capture or args.cancel_during_upload or args.cancel_during_cpu or args.fail_staging or args.fail_preflight or args.reject_submit or args.missing_replacement or args.corrupt_replacement or args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.retry_after_failure):
        parser.error("supersession probes require one replacement and no other fault or capture")
    if args.runtime_camera is not None and (not args.runtime_camera or (args.reloads and not args.standalone) or args.failure_cases or (args.fresh_processes != 1 and not args.standalone)):
        parser.error("runtime-camera fixture requires one fresh process without replacements or failure cases")
    if args.reject_runtime_camera and (args.runtime_camera in (None, "main") or args.capture or args.stage_trace):
        parser.error("--reject-runtime-camera requires an explicit UUID without capture or stage tracing")
    if args.standalone and args.runtime_camera is None:
        parser.error("--standalone requires an authored camera")
    if args.verify_capture and (not args.standalone or args.capture or args.reloads or args.reject_runtime_camera or args.failure_cases):
        parser.error("--verify-capture requires one standalone authored-camera startup")
    if args.standalone and ((args.retry_after_failure and not (args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.missing_replacement or args.corrupt_replacement or args.cancel_during_upload or args.cancel_during_cpu or args.reject_submit or args.fail_staging or args.fail_preflight)) or ((args.missing_replacement or args.corrupt_replacement or args.cancel_during_upload or args.cancel_during_cpu or args.fail_staging) and not args.retry_after_failure)):
        parser.error("unsupported standalone fault probe")

    with tempfile.TemporaryDirectory(prefix="renderer-cooked-smoke-") as temporary:
        root = pathlib.Path(temporary)
        authoring = root / "authoring"
        authoring.mkdir()
        mode = "write-switch-unique-material" if (args.corrupt_replacement_material or args.missing_replacement_material) else "write-switch-no-camera" if args.missing_replacement_camera else "write-switch-camera" if args.runtime_camera is not None and (args.reloads or args.include_alternate) else "write-camera" if args.runtime_camera is not None else "write-switch" if args.reloads else "write"
        subprocess.run([str(cooker), mode, str(authoring)], check=True, timeout=args.timeout)
        if sha256_file(cooker) != binary_hashes["cooker_sha256"]:
            raise RuntimeError("cooker executable changed while creating the fixture")
        package = root / "package"
        shutil.copytree(authoring / "output" / "desktop-vulkan-le64", package)
        with (package / "manifest.json").open(encoding="utf-8") as source:
            manifest = json.load(source)
        manifest_sha256 = hashlib.sha256((package / "manifest.json").read_bytes()).hexdigest()
        if len(manifest.get("assets", [])) != (5 if (args.corrupt_replacement_material or args.missing_replacement_material) else 4):
            raise RuntimeError("fixture has the wrong number of cooked assets")
        if len(manifest.get("scenes", [])) != (2 if (args.reloads or args.include_alternate) else 1):
            raise RuntimeError("fixture has the wrong number of cooked scenes")
        if args.corrupt_replacement_material or args.missing_replacement_material:
            asset = next(asset for asset in manifest["assets"] if asset["uuid"] == "b0000000-0000-0000-0000-00000000000b")
            object_path = package / asset["object"]["path"]
            if args.missing_replacement_material:
                object_path.unlink()
            else:
                data = bytearray(object_path.read_bytes())
                data[-1] ^= 1
                object_path.write_bytes(data)
        if args.corrupt_replacement:
            scene = next(scene for scene in manifest["scenes"] if scene["uuid"] == ALTERNATE_SCENE)
            object_path = package / scene["object"]["path"]
            data = bytearray(object_path.read_bytes())
            data[-1] ^= 1
            object_path.write_bytes(data)
        shutil.rmtree(authoring)
        run_count = 0
        observations = []
        validation_enabled = False
        validation_states = []
        timing_viewports = []
        stress_window = None
        cancellation_timings = []

        def run(candidate: pathlib.Path, expected_draws: int = 1, capture=None) -> int:
            nonlocal run_count, validation_enabled, stress_window, cancellation_timings
            run_count += 1
            log_directory = root / f"log-{run_count}"
            log_directory.mkdir()
            command = [
                str(obelisk), *( ["--cookedStandalone"] if args.standalone else ["--launchEditor", "true"] ), "--cookedPackage", str(candidate),
                "--cookedScene", SCENE, "--cookedExitAfterFrame", "--cookedExpectedDraws", str(expected_draws),
                "--logDirectory", str(log_directory),
            ]
            if capture is not None:
                command.extend(["--cookedCapture", str(capture.resolve())])
            if args.runtime_camera is not None:
                if not args.standalone:
                    command.append("--cookedRuntimeView")
                if args.runtime_camera != "main":
                    command.extend(["--cookedCamera", args.runtime_camera])
            if args.audit_source:
                command.append("--cookedAuditSource")
            if args.observe_gpu_completion:
                command.append("--cookedObserveGpuCompletion")
            if args.lifecycle_stress:
                command.append("--cookedLifecycleStress")
            if args.fixed_window:
                command.append("--cookedFixedWindow")
            if args.reloads:
                command.extend(["--cookedReloads", str(args.reloads), "--cookedAlternateScene", MISSING_SCENE if args.missing_replacement else ALTERNATE_SCENE])
            if args.cancel_during_upload:
                command.append("--cookedCancelReload")
            if args.cancel_during_cpu:
                command.append("--cookedCancelCpu")
            if args.fail_preflight:
                command.append("--cookedFailPreflight")
            if args.fail_staging:
                command.append("--cookedFailReloadStaging")
            if args.reject_submit:
                command.append("--cookedRejectReloadSubmit")
            if args.device_loss:
                command.append("--cookedInjectDeviceLoss")
            if args.shutdown_cpu:
                command.append("--cookedShutdownCpu")
            if args.shutdown_gpu:
                command.append("--cookedShutdownGpu")
            if args.retry_after_failure:
                command.append("--cookedRetryFailure")
            if args.supersede_reload:
                command.append("--cookedSupersedeReload")
            if args.supersede_during_cpu:
                command.append("--cookedSupersedeCpu")
            if args.supersede_during_upload:
                command.append("--cookedSupersedeGpu")
            if args.stage_trace is not None:
                command.extend(["--cookedStageTrace", str(args.stage_trace.resolve())])
            if args.steady_frames:
                command.extend(["--cookedSteadyFrames", str(args.steady_frames)])
            stdout_path, stderr_path = log_directory / "stdout.log", log_directory / "stderr.log"
            try:
                with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                    code = subprocess.run(command, timeout=args.timeout, stdout=stdout, stderr=stderr).returncode
            finally:
                if evidence_log is not None and (log_directory / "engine_dump.log").exists():
                    # Save even when the process times out or later assertions fail.
                    destination = evidence_log.with_name(evidence_log.name + f".process-{run_count:04d}.log") if args.fresh_processes > 1 else evidence_log
                    destination.write_text(read_process_logs(log_directory), encoding="utf-8")
                    for stream in (stdout_path, stderr_path):
                        if stream.exists():
                            destination.with_name(destination.name + "." + stream.name).write_text(read_console_log(stream), encoding="utf-8")
            log = read_process_logs(log_directory)
            require_clean_vulkan_console(read_console_log(stdout_path))
            require_clean_vulkan_console(read_console_log(stderr_path))
            if sha256_file(obelisk) != binary_hashes["runtime_sha256"]:
                raise RuntimeError("runtime executable changed during the observation")
            validation_enabled = validation_layer_status_text(log)
            validation_states.append(validation_enabled)
            timing_viewports.append(observed_timing_viewport(log))
            if args.require_validation and not validation_enabled:
                raise RuntimeError("Khronos Vulkan validation layer was not enabled")
            if any(marker in log.lower() for marker in ("vuid-", "validation error", "vk_not_ready")):
                raise RuntimeError("Vulkan validation error during cooked lifecycle smoke")
            if args.standalone:
                if args.audit_source:
                    require_zero_source_audit(log)
                if args.reloads:
                    require_no_environment_fallback(log)
                if args.device_loss:
                    if code != 2 or "Injecting device loss after upload recording" not in log or "Device lost; entering fatal shutdown" not in log or "Published cooked scene generation 3" in log or "Active scene submitted after failure" in log or "Engine destroyed" not in log:
                        raise RuntimeError("standalone runtime did not enter fatal device-loss shutdown (exit {}):\n{}".format(code, "\n".join(log.splitlines()[-30:])))
                    return code
                if args.shutdown_cpu or args.shutdown_gpu:
                    boundary = "CPU worker pending" if args.shutdown_cpu else "GPU upload pending"
                    if code != 0 or f"Shutdown requested with {boundary}" not in log or "Shutdown cancelled and drained pending replacement" not in log or "Published cooked scene generation 3" in log or "Engine destroyed" not in log:
                        raise RuntimeError("standalone shutdown did not cancel and drain its replacement (exit {}):\n{}".format(code, "\n".join(log.splitlines()[-30:])))
                    return code
                if args.reject_runtime_camera:
                    if code != 2 or "requested runtime camera is missing, ambiguous, or has no transform" not in log or "Published cooked scene generation" in log or "Engine destroyed" not in log:
                        raise RuntimeError("standalone runtime did not reject the missing camera before publication (exit {}):\n{}".format(code, "\n".join(log.splitlines()[-30:])))
                    return 1
                generations = [int(value) for value in re.findall(r"\[CookedRuntime\] Published cooked scene generation (\d+)", log)]
                if args.supersede_reload and ("Superseded waiting request before worker admission" not in log or "Committed request outcome retained and acknowledged" not in log or f"Published cooked scene generation 3 scene={SCENE}" not in log):
                    raise RuntimeError("standalone runtime did not supersede the waiting request and publish the latest scene")
                if args.cancel_during_upload and ("Cancelled replacement after mesh upload recording" not in log or "Cancelled replacement drained; active scene generation 2 retained" not in log):
                    raise RuntimeError("standalone runtime did not cancel and drain the recorded upload")
                if args.cancel_during_cpu and ("Cancelled replacement while CPU worker owned its inputs" not in log or "Cancelled worker outcome acknowledged before cleanup" not in log or "Cancelled replacement drained; active scene generation 2 retained" not in log):
                    raise RuntimeError("standalone runtime did not cancel and drain the CPU worker")
                if args.supersede_during_cpu and ("Superseded CPU worker with cleanup pending" not in log or "Superseded worker outcome acknowledged before cleanup" not in log or "Superseded replacement drained; active scene retained" not in log):
                    raise RuntimeError("standalone runtime did not supersede and drain the CPU worker")
                if args.supersede_during_upload and ("Superseded recorded upload with cleanup pending" not in log or "Superseded replacement drained; active scene retained" not in log):
                    raise RuntimeError("standalone runtime did not supersede and drain the recorded upload")
                if args.reject_submit and log.count("[CookedUploadProbe] Rejected recorded cooked batch before queue submission") != 1:
                    raise RuntimeError("standalone runtime did not reject exactly one recorded upload batch")
                if args.fail_preflight and "[CookedRuntime] injected publication preflight refusal" not in log:
                    raise RuntimeError("standalone runtime did not refuse the ready candidate before publication")
                if args.fail_staging and "[CookedRuntime] Replacement staging allocation was refused" not in log:
                    raise RuntimeError("standalone replacement did not encounter a staging budget refusal")
                if args.corrupt_replacement and "[CookedRuntime] cooked reconstruction failed" not in log:
                    raise RuntimeError("standalone runtime did not reject the corrupted scene object during reconstruction")
                if (args.corrupt_replacement_material or args.missing_replacement_material) and "[CookedRuntime] Asset closure reconstruction failed" not in log:
                    raise RuntimeError("standalone runtime did not reject the invalid replacement material")
                if args.missing_replacement_camera and "[CookedRuntime] runtime camera is invalid" not in log:
                    raise RuntimeError("standalone runtime did not reject the replacement without a camera")
                if args.retry_after_failure and (((args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.missing_replacement or args.corrupt_replacement or args.reject_submit or args.fail_staging or args.fail_preflight) and "Replacement failed; active scene generation 2 retained" not in log) or "Active scene submitted after failure; retry admitted" not in log):
                    raise RuntimeError("standalone runtime did not render its outgoing scene after failure and admit retry")
                if args.retry_after_failure and (args.corrupt_replacement_material or args.missing_replacement_material or args.missing_replacement_camera or args.missing_replacement or args.corrupt_replacement or args.reject_submit or args.fail_staging or args.fail_preflight) and "Failed replacement outcome retained and acknowledged" not in log:
                    raise RuntimeError("standalone runtime did not retain and acknowledge the failed replacement outcome")
                if args.retry_after_failure:
                    validate_retry_publications(log)
                committed = args.reloads // 2 if args.lifecycle_stress else args.reloads
                if args.lifecycle_stress:
                    cancelled = (args.reloads + 1) // 2
                    cpu_cancelled = (args.reloads + 3) // 4
                    gpu_cancelled = (args.reloads + 1) // 4
                    validate_stress_sequence(log, args.reloads)
                    cancellation_timings = read_cancellation_timings(log, args.reloads)
                    stress_window = require_stable_stress_window(log)
                    require_clean_stress_log(log)
                    if (f"[CookedStress] admitted={args.reloads} cancelled={cancelled} committed={committed}" not in log
                            or len(re.findall(r"\[CookedStress\] cancelled=\d+ generation=\d+ rendered_after_cleanup=1", log)) != cancelled
                            or log.count("Cancelled worker outcome acknowledged before cleanup") != cpu_cancelled
                            or log.count("Cancelled upload outcome acknowledged before cleanup") != gpu_cancelled):
                        raise RuntimeError("lifecycle stress did not observe every terminal outcome and post-cancellation frame")
                if code != 0 or generations != list(range(2, committed + 3)) or "[CookedRuntime] Submitted 1 draws" not in log or "Engine destroyed" not in log:
                    raise RuntimeError("standalone cooked runtime did not publish and submit its fixture through normal shutdown (exit {}):\n{}".format(code, "\n".join(log.splitlines()[-30:])))
                observations[:] = read_lifecycle_observations(log, generations, args.observe_gpu_completion, require_gpu_budget=True)
                if args.lifecycle_stress:
                    if any(sample["cache_closures"] or sample["cache_leases"] or sample["cache_retiring"] or sample["cache_asset_versions"] for sample in observations):
                        raise RuntimeError("unleased cooked closures were not reclaimed after publication")
                    reference = [sample for sample in observations if sample["generation"] >= 4 and sample["generation"] % 2 == 0]
                    if len(reference) > 1:
                        fields = ("cpu_live", "staging_live", "metadata_live", "metadata_blocks", "gpu_live", "retirement_queued", "retirement_draining")
                        baseline = tuple(reference[0][field] for field in fields)
                        if baseline[-2:] != (0, 0) or any(tuple(sample[field] for field in fields) != baseline for sample in reference[1:]):
                            raise RuntimeError("tracked allocations did not return to the warmed reference scene baseline")
                if args.steady_frames:
                    windows = [tuple(map(int, match)) for match in re.findall(r"\[CookedSteady\] generation=(\d+) start_ns=(\d+) end_ns=(\d+) submitted_frames=(\d+)", log)]
                    if len(windows) != 1:
                        raise RuntimeError("missing or repeated standalone steady-rendering window")
                    generation, start, end, frames = windows[0]
                    if generation != generations[0] or frames < args.steady_frames or start < observations[0]["first_submission_ns"] or end <= start or (len(observations) > 1 and end > observations[1]["admission_ns"]):
                        raise RuntimeError("invalid standalone steady-rendering window")
                    observations[0].update(steady_start_ns=start, steady_end_ns=end, steady_submitted_frames=frames)
                if capture is not None:
                    from compare_scene_capture import read_ppm, validate_capture_log_text
                    validate_capture_log_text(log)
                    width, height, pixels = read_ppm(capture)
                    if width < 2 or height < 2 or min(pixels) == max(pixels):
                        raise RuntimeError("standalone scene capture has no visible variation")
                return code
            if code == 0:
                if args.retry_after_failure:
                    # This run deliberately logs the rejected request as an
                    # error. It is not suitable as pixel-capture evidence.
                    if "Engine destroyed" not in log:
                        raise RuntimeError("retry did not shut down normally")
                else:
                    from compare_scene_capture import validate_capture_log_text
                    validate_capture_log_text(log)
                generations = [int(value) for value in re.findall(r"Published cooked scene generation (\d+)", log)]
                if generations != list(range(2, args.reloads + 3)):
                    raise RuntimeError(f"unexpected cooked publication sequence: {generations}")
                observations[:] = read_lifecycle_observations(log, generations, require_cache=False)
            elif code == 1 and ("Outgoing scene submitted after cooked startup failure" not in log or "Engine destroyed" not in log):
                raise RuntimeError("failure did not preserve and render the outgoing scene through shutdown")
            if args.cancel_during_upload and "Cancelled cooked replacement after mesh upload recording" not in log:
                raise RuntimeError("replacement did not reach the recorded upload cancellation boundary")
            if args.cancel_during_cpu and "Cancelled cooked replacement during CPU preparation with worker cleanup pending" not in log:
                raise RuntimeError("replacement did not reach the CPU worker cancellation boundary")
            if args.fail_staging and "Cooked replacement staging allocation was refused" not in log:
                raise RuntimeError("replacement did not encounter a staging budget refusal")
            if args.reject_submit and log.count("[CookedUploadProbe] Rejected recorded cooked batch before queue submission") != 1:
                raise RuntimeError("replacement did not encounter exactly one recorded upload submission rejection")
            expected_publications = ["2", "3"] if args.retry_after_failure else ["2"]
            if args.missing_replacement and ("Cooked package capture failed" not in log or re.findall(r"Published cooked scene generation (\d+)", log) != expected_publications):
                raise RuntimeError("missing replacement did not fail package capture after the first cooked scene was published")
            if args.corrupt_replacement and ("Cooked scene reconstruction failed" not in log or re.findall(r"Published cooked scene generation (\d+)", log) != expected_publications):
                raise RuntimeError("corrupt replacement did not fail reconstruction after the first cooked scene was published")
            if args.retry_after_failure and ("Outgoing scene submitted after cooked startup failure" not in log or "Retrying cooked scene after preserved failure" not in log):
                raise RuntimeError("retry did not follow an observed preserved failure")
            if args.supersede_reload and "Superseded cooked waiting request with a newer scene" not in log:
                raise RuntimeError("cooked waiting request was not superseded")
            if args.supersede_during_cpu and "Superseded cooked CPU worker with cleanup pending" not in log:
                raise RuntimeError("cooked CPU worker was not superseded with cleanup pending")
            if args.supersede_during_upload and "Superseded cooked GPU upload with cleanup pending" not in log:
                raise RuntimeError("cooked GPU upload was not superseded with cleanup pending")
            if args.runtime_camera is not None and not args.reject_runtime_camera:
                positions = re.findall(r"\[CookedRuntimeCamera\] (prepared|capture)_position=([^\s]+) ([^\s]+) ([^\s]+)", log)
                observed = {kind: tuple(map(float, (x, y, z))) for kind, x, y, z in positions}
                if observed.get("prepared") != (1.0, 2.0, 8.0) or (capture is not None and observed.get("capture") != (1.0, 2.0, 8.0)):
                    raise RuntimeError("authored runtime camera was not used in the prepared and captured frames")
            if args.reject_runtime_camera and "requested runtime camera is missing, ambiguous, or has no transform" not in log:
                raise RuntimeError("missing runtime camera did not fail with the expected diagnostic")
            return code

        expected_status = 2 if args.device_loss else 1 if args.reject_runtime_camera or ((args.cancel_during_upload or args.cancel_during_cpu or args.fail_staging or args.missing_replacement or args.corrupt_replacement) and not args.retry_after_failure) else 0
        capture_path = root / "standalone-capture.ppm" if args.verify_capture else args.capture
        if run(package, capture=capture_path) != expected_status:
            raise RuntimeError("valid package did not submit its expected draw")
        if args.device_loss:
            print("Injected device loss after recording; runtime entered fatal shutdown without publishing the replacement.")
            return 0
        if args.shutdown_cpu or args.shutdown_gpu:
            print("Shutdown cancelled and drained the pending cooked replacement.")
            return 0
        if args.reject_runtime_camera:
            print("Rejected the missing runtime camera and preserved the outgoing scene.")
            return 0
        fresh_observations = [observations[0]] if args.fresh_processes > 1 else []
        for process in range(1, args.fresh_processes):
            if run(package) != 0 or len(observations) != 1:
                raise RuntimeError(f"fresh process {process + 1} failed")
            fresh_observations.append(observations[0])
            print(f"Completed fresh cooked process {process + 1}/{args.fresh_processes}.", flush=True)
        if args.retry_after_failure:
            if args.evidence is not None:
                with args.evidence.open("x", encoding="utf-8") as target:
                    json.dump({"scope": "cooked replacement failure and retry smoke", "outgoing_scene_submitted_after_failure": True,
                               "retry_published_and_submitted": True, "normal_shutdown": True, "samples": observations,
                               "khronos_validation_enabled": validation_enabled}, target, indent=2)
                    target.write("\n")
            print("Preserved the outgoing cooked scene after failure and successfully submitted a retry.")
            return 0
        if args.cancel_during_upload:
            if args.evidence is not None:
                with args.evidence.open("x", encoding="utf-8") as target:
                    json.dump({"scope": "recorded cooked mesh upload cancellation smoke", "cancelled_after_recording": True,
                               "outgoing_scene_submitted": True, "normal_shutdown": True, "validation_error_markers_seen": False, "khronos_validation_enabled": validation_enabled}, target, indent=2)
                    target.write("\n")
            print("Cancelled a recorded mesh upload; the outgoing scene rendered and shut down normally.")
            return 0
        if args.cancel_during_cpu:
            if args.evidence is not None:
                with args.evidence.open("x", encoding="utf-8") as target:
                    json.dump({"scope": "cooked CPU preparation cancellation smoke", "cancelled_with_worker_cleanup_pending": True,
                               "outgoing_scene_submitted": True, "normal_shutdown": True, "validation_error_markers_seen": False, "khronos_validation_enabled": validation_enabled}, target, indent=2)
                    target.write("\n")
            print("Cancelled a cooked CPU preparation; the outgoing scene rendered and shut down normally.")
            return 0
        if args.missing_replacement:
            if args.evidence is not None:
                with args.evidence.open("x", encoding="utf-8") as target:
                    json.dump({"scope": "missing cooked replacement scene smoke", "initial_cooked_scene_published": True,
                               "replacement_package_capture_failed": True, "outgoing_scene_submitted": True,
                               "normal_shutdown": True, "validation_error_markers_seen": False, "khronos_validation_enabled": validation_enabled}, target, indent=2)
                    target.write("\n")
            print("Rejected a missing cooked replacement; the outgoing cooked scene rendered and shut down normally.")
            return 0
        if args.corrupt_replacement:
            if args.evidence is not None:
                with args.evidence.open("x", encoding="utf-8") as target:
                    json.dump({"scope": "corrupt cooked replacement scene smoke", "initial_cooked_scene_published": True,
                               "replacement_reconstruction_failed": True, "outgoing_scene_submitted": True,
                               "normal_shutdown": True, "validation_error_markers_seen": False, "khronos_validation_enabled": validation_enabled}, target, indent=2)
                    target.write("\n")
            print("Rejected a corrupt cooked replacement; the outgoing cooked scene rendered and shut down normally.")
            return 0
        if args.fail_staging:
            if args.evidence is not None:
                with args.evidence.open("x", encoding="utf-8") as target:
                    json.dump({"scope": "cooked GPU staging allocation refusal smoke", "staging_refused": True,
                               "outgoing_scene_submitted": True, "normal_shutdown": True, "validation_error_markers_seen": False, "khronos_validation_enabled": validation_enabled}, target, indent=2)
                    target.write("\n")
            print("Refused cooked GPU staging; the outgoing scene rendered and shut down normally.")
            return 0
        if args.capture is not None:
            from compare_scene_capture import read_ppm
            width, height, pixels = read_ppm(args.capture)
            # This generated fixture has a red HDR cubemap and a dark triangle.
            # The assertions are semantic smoke checks, not visual qualification.
            red = dark = 0
            for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3]):
                red += r > 180 and r > 2 * g and r > 2 * b
                dark += r < 8 and g < 8 and b < 8
            if red * 2 < width * height or dark * 500 < width * height:
                raise RuntimeError("capture is missing the fixture's red HDR background or triangle")
            print(f"Captured {width}x{height} scene viewport to {args.capture}.")
        print("Cooked asset fixture submitted eight frames with one draw and shut down normally.")
        if args.reloads:
            print(f"Completed {args.reloads} lifecycle cycles." if args.lifecycle_stress else f"Completed {args.reloads} alternating cooked scene replacements.")
        if args.failure_cases:
            if run(package, expected_draws=2) != 2:
                raise RuntimeError("incorrect draw expectation was not rejected")
            print("Rejected incorrect expected draw count.")
            for case in ("missing", "corrupt", "target", "format"):
                damaged = root / case
                shutil.copytree(package, damaged)
                ledger = json.loads((damaged / "manifest.json").read_text(encoding="utf-8"))
                asset = damaged / ledger["assets"][0]["object"]["path"]
                if case == "missing":
                    asset.unlink()
                elif case == "corrupt":
                    data = bytearray(asset.read_bytes())
                    data[-1] ^= 1
                    asset.write_bytes(data)
                else:
                    if case == "target":
                        ledger["target"] = "unsupported-target"
                    else:
                        ledger["assets"][0]["format"] = "unsupported-format"
                    (damaged / "manifest.json").write_text(json.dumps(ledger), encoding="utf-8")
                if run(damaged) != 1:
                    raise RuntimeError(f"{case} package did not fail startup cleanly")
                print(f"Rejected {case} cooked package.")
        if args.evidence is not None:
            with args.evidence.open("x", encoding="utf-8") as target:
                json.dump({"scope": "cooked lifecycle smoke; not a calibrated qualification profile",
                           "reloads": args.reloads, "samples": observations,
                           "lifecycle_stress": args.lifecycle_stress,
                           "cancellation_timings": cancellation_timings,
                           "cancellation_latency_ms": {boundary: distribution([sample["elapsed_ns"] / 1_000_000 for sample in cancellation_timings if sample["boundary"] == boundary]) for boundary in ("cpu", "gpu") if any(sample["boundary"] == boundary for sample in cancellation_timings)},
                           "stress_window": stress_window,
                           "khronos_validation_enabled": validation_enabled,
                           "validation_states": validation_states,
                           "source_audit_enabled": args.audit_source,
                           "gpu_completion_observed": args.observe_gpu_completion,
                           "timing_viewports": timing_viewports,
                           "binary_sha256": binary_hashes,
                           "host_system": platform.platform(), "host_machine": platform.machine(),
                           "manifest_sha256": manifest_sha256,
                           "fresh_process_samples": fresh_observations,
                           "filesystem_cache_conditions": args.cache_conditions}, target, indent=2)
                target.write("\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Cooked scene smoke failed: {error}", file=sys.stderr)
        sys.exit(1)
