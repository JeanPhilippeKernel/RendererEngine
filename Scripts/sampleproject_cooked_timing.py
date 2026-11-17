#!/usr/bin/env python3
"""Measure copied SampleProject loads and replacements; not qualification.

The fixture adds one camera and duplicates level01 with a distinct scene UUID.
The two scenes share assets. The authoring copy is removed before runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

from analyze_cooked_timing_samples import analyze
from analyze_cooked_runtime_trace import analyze as analyze_trace
from cooked_capture_identity import device_identity, shader_identity
from cooked_scene_smoke import (observed_timing_viewport, read_lifecycle_observations, read_process_logs,
                                require_clean_stress_log, require_zero_source_audit, sha256_file,
                                validation_layer_status_text)
from sampleproject_cooked_runtime import CAMERA, CAMERA_ENTITY, run_capture


SCENE = "f6d4dbea-c21d-453d-ae91-79187dfa8b84"
ALTERNATE = "f6d4dbea-c21d-453d-ae91-79187dfa8b85"


def host_hardware():
    if platform.system() != "Darwin":
        return None
    try:
        cpu = subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True, timeout=5).strip()
        memory = int(subprocess.check_output(["sysctl", "-n", "hw.memsize"], text=True, timeout=5).strip())
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    return {"cpu": cpu, "physical_memory_bytes": memory} if cpu and memory > 0 else None


def memory_summary(samples):
    if not samples:
        raise ValueError("memory summary requires lifecycle observations")
    fields = {"cpu_pool_peak_bytes": "cpu_peak", "preparation_staging_peak_bytes": "staging_peak",
              "tracked_gpu_allocation_peak_bytes": "gpu_peak", "metadata_max_live_bytes": "metadata_live",
              "metadata_max_live_blocks": "metadata_blocks", "retirement_max_queued_at_observation": "retirement_queued",
              "retirement_max_draining_at_observation": "retirement_draining"}
    if any("gpu_admitted_peak" in sample for sample in samples) and not all("gpu_admitted_peak" in sample for sample in samples):
        raise ValueError("partial GPU budget observations")
    if all("gpu_admitted_peak" in sample for sample in samples):
        fields.update({"gpu_admitted_peak_bytes": "gpu_admitted_peak", "gpu_geometry_peak_bytes": "gpu_geometry_peak",
                       "gpu_texture_peak_bytes": "gpu_texture_peak", "gpu_target_peak_bytes": "gpu_target_peak",
                       "gpu_uniform_peak_bytes": "gpu_uniform_peak", "gpu_staging_peak_bytes": "gpu_staging_peak",
                       "gpu_readback_peak_bytes": "gpu_readback_peak"})
    if any("retirement_high_water" in sample for sample in samples) and not all("retirement_high_water" in sample for sample in samples):
        raise ValueError("partial retirement budget observations")
    if all("retirement_high_water" in sample for sample in samples):
        fields["retirement_high_water"] = "retirement_high_water"
    result = {}
    for label, key in fields.items():
        values = [sample[key] for sample in samples]
        if any(isinstance(value, bool) or not isinstance(value, int) or value < 0 for value in values):
            raise ValueError("invalid lifecycle memory observation")
        result[label] = max(values)
    if "retirement_high_water" in fields:
        capacities = {sample["retirement_capacity"] for sample in samples}
        if len(capacities) != 1 or next(iter(capacities)) <= 0:
            raise ValueError("retirement capacity changed across observations")
        result["retirement_capacity"] = next(iter(capacities))
    result["scope"] = "tracked allocator peaks and sampled metadata/retirement counts; excludes driver residency"
    return result


def require_visible_trace(report):
    windows = report.get("windows", {})
    required = {"initial_load", "steady", "replacement", "post_steady"}
    if set(windows) != required:
        raise ValueError("visible trace requires all four timing windows")
    for name, window in windows.items():
        state = window.get("window_occlusion")
        if not state or state["visible_at_start"] != 1 or any(change["visible"] != 1 for change in state["changes"]):
            raise ValueError(f"window visibility is not proven throughout {name}; inspect the trace")


def scene_pair(original):
    marker = ('"uuid": "' + SCENE + '"').encode()
    if original.count(marker) != 1 or CAMERA.encode() in original or not original.endswith(b"\n"):
        raise ValueError("SampleProject scene cannot safely receive the fixture camera and alternate UUID")
    initial = original + CAMERA_ENTITY.encode()
    alternate = initial.replace(marker, ('"uuid": "' + ALTERNATE + '"').encode(), 1)
    return initial, alternate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--cooker", type=Path, required=True)
    parser.add_argument("--obelisk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--expected-draws", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--trace-steady-frames", type=int, default=0, help="also capture steady frames before and after one replacement")
    parser.add_argument("--require-visible-trace", action="store_true", help="reject a trace without proven window visibility throughout its timing windows")
    args = parser.parse_args()
    project, cooker, obelisk, output = (path.resolve(strict=True) for path in (args.project, args.cooker, args.obelisk, args.output))
    if not project.is_file() or not output.is_dir() or any(output.iterdir()) or not 1 <= args.count <= 100 or args.expected_draws < 1 or args.timeout <= 0 or (args.trace_steady_frames and not 2 <= args.trace_steady_frames <= 600) or (args.require_visible_trace and not args.trace_steady_frames):
        parser.error("provide a project, empty output directory and positive bounded limits")
    if output == project.parent or project.parent in output.parents:
        parser.error("output must be outside the project workspace")
    for directory, names, files in os.walk(project.parent, followlinks=False):
        if any((Path(directory) / name).is_symlink() for name in names + files):
            raise ValueError("project copy contains a symlink")
    original = (project.parent / "Scenes" / "level01.zescene").read_bytes()
    initial, alternate = scene_pair(original)
    hashes = {"cooker_sha256": sha256_file(cooker), "runtime_sha256": sha256_file(obelisk)}
    asset_root = Path(os.environ.get("ZENGINE_ASSET_ROOT") or obelisk.parent / "ZodiacEngine").resolve(strict=True)
    shader_cache = asset_root / "Shaders" / "Cache"
    shaders = shader_identity(shader_cache)
    hardware = host_hardware()
    with tempfile.TemporaryDirectory(prefix="renderer-sample-timing-") as temporary:
        root = Path(temporary)
        workspace = root / "project"
        shutil.copytree(project.parent, workspace)
        config_path = workspace / project.name
        config = json.loads(config_path.read_text(encoding="utf-8"))
        if config.get("workingSpace") != ".":
            raise ValueError("project must use a project-local workspace")
        config["sceneList"] = [{"name": "level01", "isDefault": True}, {"name": "level01_timing_alternate", "isDefault": False}]
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        (workspace / "Scenes" / "level01.zescene").write_bytes(initial)
        (workspace / "Scenes" / "level01_timing_alternate.zescene").write_bytes(alternate)
        cook = root / "cook"
        cook.mkdir()
        subprocess.run([str(cooker), "smoke-offline", str(config_path), str(cook), "805306368"], check=True, timeout=args.timeout)
        package = cook / "output" / "desktop-vulkan-le64"
        manifest_bytes = (package / "manifest.json").read_bytes()
        manifest = json.loads(manifest_bytes)
        if {item["uuid"] for item in manifest["scenes"]} != {SCENE, ALTERNATE} or len(manifest["assets"]) < 32:
            raise ValueError("cooked package lacks the expected scenes or assets")
        shutil.rmtree(workspace)
        common = {"manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(), "binary_sha256": hashes,
                  "host_system": platform.platform(), "host_machine": platform.machine(),
                  "shader_cache": shaders, "hardware": hardware,
                  "filesystem_cache_conditions": "uncontrolled operating-system cache; no cold-disk claim",
                  "source_audit_enabled": True, "gpu_completion_observed": True}

        devices = []

        def capture(name, reloads, steady_frames=0, trace=None):
            if shader_identity(shader_cache) != shaders:
                raise ValueError("runtime shader cache changed before a timing process")
            logs = output / name
            logs.mkdir()
            command = [str(obelisk), "--cookedStandalone", "--cookedPackage", str(package), "--cookedScene", SCENE,
                       "--cookedCamera", CAMERA, "--cookedExitAfterFrame", "--cookedExpectedDraws", str(args.expected_draws),
                       "--cookedAuditSource", "--cookedObserveGpuCompletion", "--cookedFixedWindow", "--logDirectory", str(logs)]
            if reloads:
                command.extend(["--cookedReloads", str(reloads), "--cookedAlternateScene", ALTERNATE])
            if steady_frames:
                command.extend(["--cookedSteadyFrames", str(steady_frames), "--cookedPostSteadyFrames", str(steady_frames), "--cookedStageTrace", str(trace)])
                if args.require_visible_trace:
                    command.append("--cookedWaitVisible")
            run_capture(command, logs, args.timeout)
            if shader_identity(shader_cache) != shaders:
                raise ValueError("runtime shader cache changed during a timing process")
            log = read_process_logs(logs)
            devices.append(device_identity(log))
            require_zero_source_audit(log)
            require_clean_stress_log(log)
            if not validation_layer_status_text(log) or "Engine destroyed" not in log:
                raise ValueError("runtime validation or clean shutdown was not confirmed")
            expected = [(generation, SCENE if generation % 2 == 0 else ALTERNATE) for generation in range(2, reloads + 3)]
            observed = [(int(generation), scene) for generation, scene in re.findall(r"\[CookedRuntime\] Published cooked scene generation (\d+) scene=([0-9a-f-]+)", log)]
            if observed != expected or f"[CookedRuntime] Submitted {args.expected_draws} draws" not in log:
                raise ValueError("runtime did not publish and render the expected SampleProject scenes")
            if sha256_file(obelisk) != hashes["runtime_sha256"]:
                raise ValueError("runtime binary changed during timing collection")
            samples = read_lifecycle_observations(log, [generation for generation, _ in expected], True, require_gpu_budget=True)
            if steady_frames:
                windows = [tuple(map(int, row)) for row in re.findall(r"\[CookedSteady\] generation=(\d+) start_ns=(\d+) end_ns=(\d+) submitted_frames=(\d+)", log)]
                if len(windows) != 1:
                    raise ValueError("missing or repeated steady-rendering window")
                generation, start, end, frames = windows[0]
                if generation != 2 or frames < steady_frames or start < samples[0]["first_submission_ns"] or end <= start or end > samples[1]["admission_ns"]:
                    raise ValueError("invalid steady-rendering window")
                samples[0].update(steady_start_ns=start, steady_end_ns=end, steady_submitted_frames=frames)
                post = [tuple(map(int, row)) for row in re.findall(r"\[CookedPostSteady\] generation=(\d+) start_ns=(\d+) end_ns=(\d+) submitted_frames=(\d+)", log)]
                if len(post) != 1:
                    raise ValueError("missing or repeated post-replacement steady window")
                generation, start, end, frames = post[0]
                if generation != 3 or frames < steady_frames or start < samples[1]["first_submission_ns"] or end <= start:
                    raise ValueError("invalid post-replacement steady window")
                samples[1].update(post_steady_start_ns=start, post_steady_end_ns=end, post_steady_submitted_frames=frames)
            return samples, observed_timing_viewport(log)

        fresh_samples, viewports = [], []
        for index in range(args.count):
            samples, viewport = capture(f"fresh-{index + 1:02d}", 0)
            fresh_samples.extend(samples)
            viewports.append(viewport)
        samples, viewport = capture("replacements", args.count)
        if sha256_file(cooker) != hashes["cooker_sha256"]:
            raise ValueError("cooker binary changed during timing collection")
        fresh = {**common, "fresh_process_samples": fresh_samples, "validation_states": [True] * args.count, "timing_viewports": viewports}
        replacements = {**common, "reloads": args.count, "samples": samples, "validation_states": [True], "timing_viewports": [viewport]}
        fresh["device"] = devices[0]
        replacements["device"] = devices[0]
        report = analyze(fresh, replacements, args.count)
        trace_report = None
        if args.trace_steady_frames:
            trace_path = output / "stage.csv"
            trace_samples, trace_viewport = capture("trace", 1, args.trace_steady_frames, trace_path)
            if trace_viewport != viewport or trace_viewport is None or trace_viewport["resize_after_baseline"]:
                raise ValueError("trace viewport differs from timing samples")
            trace_report = analyze_trace({**common, "samples": trace_samples}, trace_path, args.trace_steady_frames)
        if shader_identity(shader_cache) != shaders:
            raise ValueError("runtime shader cache changed during timing collection")
        if any(device != devices[0] for device in devices[1:]):
            raise ValueError("GPU or driver identity changed during timing collection")
        if trace_report is not None:
            trace_report.update(binary_sha256=hashes, shader_cache=shaders, hardware=hardware, device=devices[0], viewport=viewport)
            (output / "trace.json").write_text(json.dumps(trace_report, indent=2) + "\n", encoding="utf-8")
            if args.require_visible_trace:
                require_visible_trace(trace_report)
        report.update(fixture="SampleProject level01 copy with added camera and same-assets alternate scene",
                      shader_cache=shaders,
                      hardware=hardware, device=devices[0],
                      memory={"fresh_processes": memory_summary(fresh_samples),
                              "same_process_replacements": memory_summary(samples)},
                      original_scene_sha256=hashlib.sha256(original).hexdigest(),
                      augmented_scene_sha256=hashlib.sha256(initial).hexdigest(),
                      alternate_scene_sha256=hashlib.sha256(alternate).hexdigest(), cooked_assets=len(manifest["assets"]))
        (output / "fresh.json").write_text(json.dumps(fresh, indent=2) + "\n", encoding="utf-8")
        (output / "replacements.json").write_text(json.dumps(replacements, indent=2) + "\n", encoding="utf-8")
        (output / "timing.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"SampleProject timing failed: {error}", file=sys.stderr)
        sys.exit(1)
