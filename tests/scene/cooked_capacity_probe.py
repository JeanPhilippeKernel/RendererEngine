#!/usr/bin/env python3
"""Generate a many-instance cooked fixture and run it in standalone Vulkan.

This is capacity evidence for a synthetic shared triangle/material, not a
qualification profile for arbitrary complex scenes. The source fixture is
removed before launch. A nonzero exit reports failure without claiming that
the requested capacity is supported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

from analyze_cooked_runtime_trace import main_frame_diagnostics, render_outliers
from cooked_scene_smoke import (observed_timing_viewport, read_cancellation_timings, read_console_log,
                                read_lifecycle_observations, read_process_logs, require_clean_stress_log,
                                require_clean_vulkan_console, require_stable_stress_window, require_zero_source_audit,
                                sha256_file, validate_stress_sequence, validation_layer_status_text)
from validate_scene_lifecycle import capture_records, read_stages


SCENE = "10000000-0000-0000-0000-000000000001"
ALTERNATE_SCENE = "90000000-0000-0000-0000-000000000009"
MATERIAL = "40000000-0000-0000-0000-000000000004"
ALTERNATE_MATERIAL = "b0000000-0000-0000-0000-00000000000b"
MAX_DRAWS = 8192


def validate_unique_material_manifest(manifest):
    scenes = {scene["uuid"]: scene for scene in manifest["scenes"]}
    assets = {asset["uuid"]: asset for asset in manifest["assets"] if asset["kind"] == "material"}
    if SCENE not in scenes or ALTERNATE_SCENE not in scenes or MATERIAL not in assets or ALTERNATE_MATERIAL not in assets:
        raise ValueError("distinct-material package is missing expected scenes or materials")
    roots = lambda scene: {item["uuid"] for item in scene["roots"] if item["kind"] == "material"}
    if MATERIAL not in roots(scenes[SCENE]) or ALTERNATE_MATERIAL not in roots(scenes[ALTERNATE_SCENE]):
        raise ValueError("distinct-material package has unexpected scene roots")
    if assets[MATERIAL]["object"]["digest"] == assets[ALTERNATE_MATERIAL]["object"]["digest"]:
        raise ValueError("distinct materials do not have distinct cooked objects")


def summarize_initial_trace(trace, sample):
    records = list(capture_records(trace))
    timestamps = [timestamp for _, _, timestamp, _ in records]
    start, end = sample["admission_ns"], sample["first_submission_ns"]
    if not timestamps or start >= end or max(timestamps) < end:
        raise ValueError("stage capture does not cover the first submission")
    markers = [timestamp for kind, name, timestamp, _ in records
               if kind == "scope_ns" and name == "SceneLoad.Main.BeforeInitialAdmission" and timestamp <= start]
    return {"scope": "diagnostic only; partial initial coverage cannot prove a frame budget",
            "starts_after_admission_ns": max(0, min(timestamps) - start),
            "admission_marker_observed": len(markers) == 1,
            "main_frame_diagnostics": main_frame_diagnostics(records, (start, end)),
            "stages": read_stages(trace, (start, end))}


def validate_publications(log, expect_refusal, replacement_refusal, steady_replacement=False):
    publications = [(int(generation), scene) for generation, scene in re.findall(
        r"\[CookedRuntime\] Published cooked scene generation (\d+) scene=([0-9a-f-]+)", log)]
    expected = ([] if expect_refusal else [(2, SCENE), (3, SCENE)] if replacement_refusal
                else [(2, SCENE), (3, ALTERNATE_SCENE)] if steady_replacement else [(2, SCENE)])
    if publications != expected:
        raise ValueError("capacity run published an unexpected scene or generation")
    if replacement_refusal and re.search(r"\[SkyEnvironment\].*using the fallback", log):
        raise ValueError("capacity failure caused an environment fallback")
    return [generation for generation, _ in publications]


def make_texture_variants(directory, count):
    """Create distinct one-pixel textures using the fixture's BMP layout."""
    if count < 1:
        raise ValueError("texture count must be positive")
    source = (directory / "pixel.bmp").read_bytes()
    metadata = json.loads((directory / "pixel.bmp.meta").read_text())
    base = "50000000-0000-0000-0000-000000000005"
    if len(source) != 58 or source[:2] != b"BM" or int.from_bytes(source[10:14], "little") != 54 or metadata.get("uuid") != base:
        raise ValueError("unexpected capacity texture fixture")
    identities = [(base, "assets/pixel.bmp")]
    for index in range(1, count):
        identity = f"c0000000-0000-4000-8000-{index:012x}"
        name = f"capacity-texture-{index:05d}.bmp"
        pixels = bytearray(source)
        pixels[54:57] = index.to_bytes(3, "little")
        (directory / name).write_bytes(pixels)
        (directory / (name + ".meta")).write_text(json.dumps(dict(metadata, uuid=identity), sort_keys=True) + "\n")
        identities.append((identity, "assets/" + name))
    return identities


def make_material_variants(directory, count, textures=None):
    """Create distinct material objects using the requested texture versions."""
    if count < 1:
        raise ValueError("material count must be positive")
    source = json.loads((directory / "material.zematerial").read_text())
    meta = json.loads((directory / "material.zematerial.meta").read_text())
    if source.get("uuid") != MATERIAL or meta.get("uuid") != MATERIAL:
        raise ValueError("unexpected capacity fixture material identity")
    identities = [MATERIAL]
    for index in range(1, count):
        identity = f"a0000000-0000-4000-8000-{index:012x}"
        material = dict(source, uuid=identity, name=f"Capacity material {index}")
        if textures:
            texture_uuid, texture_path = textures[index % len(textures)]
            material["textures"] = dict(source["textures"], albedo={"uuid": texture_uuid, "path": texture_path})
        metadata = dict(meta, uuid=identity)
        path = directory / f"capacity-material-{index:05d}.zematerial"
        path.write_text(json.dumps(material, sort_keys=True) + "\n")
        path.with_suffix(path.suffix + ".meta").write_text(json.dumps(metadata, sort_keys=True) + "\n")
        identities.append(identity)
    return identities


def append_instances(scene, draws, materials=None):
    """The base fixture already contains one mesh and one camera."""
    if not 1 <= draws <= MAX_DRAWS + 1:
        raise ValueError("draw count must be between 1 and the limit plus one")
    original = scene.read_bytes()
    if not original.endswith(b"\n"):
        raise ValueError("fixture scene is not newline terminated")
    materials = materials or [MATERIAL]
    if materials[0] != MATERIAL or len(materials) > draws or len(set(materials)) != len(materials):
        raise ValueError("invalid fixture material identities")
    with scene.open("ab") as target:
        for index in range(1, draws):
            target.write(f'''  - "uuid": "d0000000-0000-4000-8000-{index:012x}"
    "components":
      "TransformComponent":
        "schema_version": 1
        "fields":
          "position": [0, 0, 0]
          "rotation": [0, 0, 0]
          "scale": [1, 1, 1]
      "MeshComponent":
        "schema_version": 1
        "fields":
          "mesh_uuid": "30000000-0000-0000-0000-000000000003"
      "MaterialComponent":
        "schema_version": 1
        "fields":
          "material_uuid": "{materials[index % len(materials)]}"
'''.encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cooker", required=True, type=Path)
    parser.add_argument("--obelisk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--draws", type=int, default=MAX_DRAWS)
    parser.add_argument("--materials", type=int, default=1, help="number of distinct material versions sharing the fixture texture")
    parser.add_argument("--textures", type=int, default=1, help="number of distinct one-pixel textures used by the materials")
    parser.add_argument("--expect-refusal", action="store_true", help="expect the limit-plus-one fixture to fail before publication")
    parser.add_argument("--replacement-refusal", action="store_true", help="reject a limit-plus-one replacement and retry the active scene")
    parser.add_argument("--texture-overlap-refusal", action="store_true", help="reject a larger texture replacement and retry the smaller active scene")
    parser.add_argument("--active-materials", type=int, default=0, help="material count in the initial scene for texture-overlap refusal")
    parser.add_argument("--stage-trace", action="store_true", help="capture diagnostic load stages at a fixed logical window")
    parser.add_argument("--steady-frames", type=int, default=0, help="record settled standalone frames after initial load (requires --stage-trace)")
    parser.add_argument("--steady-replacement", action="store_true", help="replace the settled scene with an equivalent capacity scene")
    parser.add_argument("--unique-replacement-material", action="store_true", help="use a distinct material version in the replacement scene")
    parser.add_argument("--lifecycle-cycles", type=int, default=0, help="alternate cancellations and replacements of the capacity scene")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    cooker, obelisk = args.cooker.resolve(strict=True), args.obelisk.resolve(strict=True)
    binary_hashes = {"cooker_sha256": sha256_file(cooker), "runtime_sha256": sha256_file(obelisk)}
    if (args.timeout <= 0 or not 1 <= args.draws <= MAX_DRAWS + 1 or not 1 <= args.materials <= args.draws or not 1 <= args.textures <= args.materials or args.expect_refusal and args.replacement_refusal
            or (args.draws == MAX_DRAWS + 1) != (args.expect_refusal or args.replacement_refusal)
            or not 0 <= args.steady_frames <= 600
            or (args.steady_frames and (not args.stage_trace or args.expect_refusal or args.replacement_refusal))
            or (args.steady_replacement and not args.steady_frames)
            or (args.unique_replacement_material and not args.steady_replacement)
            or (args.materials > 1 and (args.unique_replacement_material or args.replacement_refusal))
            or (args.texture_overlap_refusal and (args.expect_refusal or args.replacement_refusal or args.steady_replacement or args.lifecycle_cycles
                                                   or not 1 <= args.active_materials < args.textures or args.textures != args.materials))
            or (args.active_materials and not args.texture_overlap_refusal)
            or args.lifecycle_cycles < 0
            or (args.lifecycle_cycles and (args.lifecycle_cycles < 4 or args.lifecycle_cycles % 4
                                           or args.expect_refusal or args.replacement_refusal or args.stage_trace))):
        parser.error("invalid capacity probe options; lifecycle cycles require a multiple of four and cannot be combined with refusal or stage-trace modes")
    output = args.output.resolve(strict=True)
    if not output.is_dir() or any(output.iterdir()):
        parser.error("output must be an existing empty directory")
    with tempfile.TemporaryDirectory(prefix="renderer-cooked-capacity-") as temporary:
        root = Path(temporary)
        authoring = root / "authoring"
        authoring.mkdir()
        fixture_mode = "write-switch-unique-material" if args.unique_replacement_material else "write-camera"
        subprocess.run([str(cooker), fixture_mode, str(authoring)], check=True, timeout=args.timeout)
        scene = authoring / "scenes" / "scène_東京.scene"
        fixture = authoring / "scenes" / "fixture.zescene"
        shutil.copyfile(scene, fixture)
        capacity_scene = fixture
        if args.replacement_refusal:
            capacity_scene = authoring / "scenes" / "alternate.zescene"
            original = fixture.read_bytes()
            if original.count(SCENE.encode()) != 1:
                raise ValueError("fixture does not contain a unique scene UUID")
            capacity_scene.write_bytes(original.replace(SCENE.encode(), ALTERNATE_SCENE.encode(), 1))
        textures = make_texture_variants(authoring / "assets", args.textures)
        materials = make_material_variants(authoring / "assets", args.materials, textures)
        append_instances(capacity_scene, args.draws, materials[:args.active_materials] if args.texture_overlap_refusal else materials)
        if args.texture_overlap_refusal:
            original = (authoring / "scenes" / "scène_東京.scene").read_bytes()
            if original.count(SCENE.encode()) != 1:
                raise ValueError("fixture does not contain a unique scene UUID")
            alternate = authoring / "scenes" / "alternate.zescene"
            alternate.write_bytes(original.replace(SCENE.encode(), ALTERNATE_SCENE.encode(), 1))
            append_instances(alternate, args.draws, materials)
        if args.steady_replacement or args.lifecycle_cycles:
            original = fixture.read_bytes()
            if original.count(SCENE.encode()) != 1:
                raise ValueError("fixture does not contain a unique scene UUID")
            alternate = original.replace(SCENE.encode(), ALTERNATE_SCENE.encode(), 1)
            if args.unique_replacement_material:
                if alternate.count(MATERIAL.encode()) != args.draws:
                    raise ValueError("fixture does not contain one material reference per draw")
                alternate = alternate.replace(MATERIAL.encode(), ALTERNATE_MATERIAL.encode())
            (authoring / "scenes" / "alternate.zescene").write_bytes(alternate)
        source_digest = hashlib.sha256(capacity_scene.read_bytes()).hexdigest()
        alternate_digest = hashlib.sha256((authoring / "scenes" / "alternate.zescene").read_bytes()).hexdigest() if args.texture_overlap_refusal else None
        # The regular fixture's recook mode deliberately caps decoded scene
        # memory at 128 KiB. Use the offline project cook with a larger bounded
        # working budget for capacity input, just as the real project path does.
        config = {"projectName": "CookedCapacityFixture", "workingSpace": ".", "sceneDir": "scenes",
                  "assetDirs": {key: "assets" for key in ("textureDir", "soundDir", "meshDir", "materialDir", "spriteDir", "environmentMapDir")},
                  "sceneList": [{"name": "fixture", "isDefault": True}]}
        if args.replacement_refusal or args.texture_overlap_refusal or args.steady_replacement or args.lifecycle_cycles:
            config["sceneList"].append({"name": "alternate", "isDefault": False})
        (authoring / "project.json").write_text(json.dumps(config), encoding="utf-8")
        cooked = root / "cooked"
        cooked.mkdir()
        subprocess.run([str(cooker), "smoke-offline", str(authoring / "project.json"), str(cooked), "805306368"], check=True, timeout=args.timeout)
        package = root / "package"
        shutil.copytree(cooked / "output" / "desktop-vulkan-le64", package)
        manifest = (package / "manifest.json").read_bytes()
        manifest_data = json.loads(manifest)
        material_assets = {item["uuid"]: item for item in manifest_data["assets"] if item["kind"] == "material"}
        if not set(materials).issubset(material_assets) or len({material_assets[identity]["object"]["digest"] for identity in materials}) != len(materials):
            raise ValueError("capacity package lacks distinct material versions")
        texture_assets = {item["uuid"]: item for item in manifest_data["assets"] if item["kind"] == "texture"}
        texture_ids = [identity for identity, _ in textures]
        if not set(texture_ids).issubset(texture_assets) or len({texture_assets[identity]["object"]["digest"] for identity in texture_ids}) != len(texture_ids):
            raise ValueError("capacity package lacks distinct texture versions")
        expected_scene = ALTERNATE_SCENE if args.replacement_refusal or args.texture_overlap_refusal else SCENE
        roots = {item["uuid"] for scene in manifest_data["scenes"] if scene["uuid"] == expected_scene
                 for item in scene["roots"] if item["kind"] == "material"}
        if not set(materials).issubset(roots):
            raise ValueError("capacity scene does not reference every requested material")
        if args.texture_overlap_refusal:
            active_roots = {item["uuid"] for scene in manifest_data["scenes"] if scene["uuid"] == SCENE
                            for item in scene["roots"] if item["kind"] == "material"}
            if active_roots != set(materials[:args.active_materials]):
                raise ValueError("active scene material set differs from the refusal fixture")
        if args.unique_replacement_material:
            validate_unique_material_manifest(manifest_data)
        (output / "manifest.json").write_bytes(manifest)
        shutil.rmtree(authoring)
        shutil.rmtree(cooked)
        logs = output / "runtime-log"
        logs.mkdir()
        command = [str(obelisk), "--cookedStandalone", "--cookedPackage", str(package), "--cookedScene", SCENE,
                   "--cookedExitAfterFrame", "--cookedExpectedDraws", str(1 if args.replacement_refusal else args.draws), "--cookedAuditSource", "--logDirectory", str(logs)]
        if args.stage_trace:
            command.extend(["--cookedFixedWindow", "--cookedStageTrace", str(output / "stage.csv")])
        if args.steady_frames:
            command.extend(["--cookedSteadyFrames", str(args.steady_frames)])
        if args.replacement_refusal or args.texture_overlap_refusal:
            command.extend(["--cookedReloads", "1", "--cookedAlternateScene", ALTERNATE_SCENE, "--cookedRetryFailure"])
        elif args.steady_replacement:
            command.extend(["--cookedReloads", "1", "--cookedAlternateScene", ALTERNATE_SCENE])
        elif args.lifecycle_cycles:
            command.extend(["--cookedReloads", str(args.lifecycle_cycles), "--cookedAlternateScene", ALTERNATE_SCENE,
                            "--cookedLifecycleStress", "--cookedFixedWindow"])
        stdout_path, stderr_path = logs / "stdout.log", logs / "stderr.log"
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            completed = subprocess.run(command, timeout=args.timeout, stdout=stdout, stderr=stderr)
        log = read_process_logs(logs)
        require_clean_vulkan_console(read_console_log(stdout_path))
        require_clean_vulkan_console(read_console_log(stderr_path))
        if sha256_file(obelisk) != binary_hashes["runtime_sha256"]:
            raise RuntimeError("runtime executable changed during the capacity observation")
        valid_refusal = (args.expect_refusal and completed.returncode == 2
                         and "complete scene exceeds the instance budget" in log
                         and "Published cooked scene generation" not in log)
        valid_render = (not args.expect_refusal and completed.returncode == 0
                        and not args.replacement_refusal and not args.texture_overlap_refusal and f"[CookedRuntime] Submitted {args.draws} draws" in log)
        valid_replacement = (args.replacement_refusal and completed.returncode == 0
                             and "complete scene exceeds the instance budget" in log
                             and "Failed replacement outcome retained and acknowledged" in log
                             and "Replacement failed; active scene generation 2 retained" in log
                             and "Active scene submitted after failure; retry admitted" in log
                             and f"Published cooked scene generation 3 scene={SCENE}" in log
                             and "[CookedRuntime] Submitted 1 draws" in log)
        valid_texture_refusal = (args.texture_overlap_refusal and completed.returncode == 0
                                 and "texture allocation status=-10" in log
                                 and "Failed replacement outcome retained and acknowledged" in log
                                 and "Replacement failed; active scene generation 2 retained" in log
                                 and "Active scene submitted after failure; retry admitted" in log
                                 and f"Published cooked scene generation 3 scene={SCENE}" in log
                                 and f"[CookedRuntime] Submitted {args.draws} draws" in log)
        require_zero_source_audit(log)
        if not (valid_refusal or valid_render or valid_replacement or valid_texture_refusal) or not validation_layer_status_text(log) or "Engine destroyed" not in log or any(marker in log.lower() for marker in ("vuid-", "validation error", "vk_not_ready")):
            raise RuntimeError(f"capacity fixture did not render and shut down cleanly (exit {completed.returncode}); inspect {logs}")
        cancellation_timings = []
        if args.lifecycle_cycles:
            validate_stress_sequence(log, args.lifecycle_cycles)
            cancellation_timings = read_cancellation_timings(log, args.lifecycle_cycles)
            require_stable_stress_window(log)
            require_clean_stress_log(log)
            cancelled = args.lifecycle_cycles // 2
            if (f"[CookedStress] admitted={args.lifecycle_cycles} cancelled={cancelled} committed={cancelled}" not in log
                    or log.count("Cancelled worker outcome acknowledged before cleanup") != cancelled // 2
                    or log.count("Cancelled upload outcome acknowledged before cleanup") != cancelled // 2):
                raise ValueError("capacity lifecycle run did not observe every cancellation outcome")
            generations = list(range(2, cancelled + 3))
        else:
            generations = validate_publications(log, args.expect_refusal, args.replacement_refusal or args.texture_overlap_refusal, args.steady_replacement)
        samples = read_lifecycle_observations(log, generations, require_gpu_budget=True) if generations else []
        if args.lifecycle_cycles and any(sample[field] for sample in samples
                                         for field in ("cache_closures", "cache_leases", "cache_retiring", "cache_asset_versions")):
            raise ValueError("capacity lifecycle run retained an unleased cooked closure")
        report = {"scope": "synthetic shared-mesh cooked capacity observation; not a calibrated profile",
                  "draws": args.draws, "entities_including_camera": args.draws + 1, "distinct_materials": args.materials, "distinct_textures": args.textures,
                  "limit_plus_one_refused": args.expect_refusal or args.replacement_refusal,
                  "active_scene_retained_and_retried": args.replacement_refusal or args.texture_overlap_refusal,
                  "texture_overlap_refused_and_retried": args.texture_overlap_refusal,
                  "active_materials": args.active_materials if args.texture_overlap_refusal else args.materials,
                  "unique_replacement_material": args.unique_replacement_material,
                  "scene_sha256": source_digest, "manifest_sha256": hashlib.sha256(manifest).hexdigest(),
                  "alternate_scene_sha256": alternate_digest,
                  "binary_sha256": binary_hashes, "host_system": platform.platform(), "host_machine": platform.machine(),
                  "filesystem_cache_conditions": "uncontrolled", "samples": samples,
                  "lifecycle_cycles": args.lifecycle_cycles, "cancellation_timings": cancellation_timings,
                  "validation_enabled": True, "source_audit_zero": True}
        if args.stage_trace and samples:
            report["initial_load_trace"] = summarize_initial_trace(output / "stage.csv", samples[0])
        if args.steady_frames:
            windows = [tuple(map(int, row)) for row in re.findall(r"\[CookedSteady\] generation=(\d+) start_ns=(\d+) end_ns=(\d+) submitted_frames=(\d+)", log)]
            viewport = observed_timing_viewport(log)
            if len(windows) != 1 or not viewport or viewport["resize_after_baseline"]:
                raise ValueError("missing steady window or viewport changed during observation")
            generation, start, end, frames = windows[0]
            if generation != generations[0] or frames < args.steady_frames or start < samples[0]["first_submission_ns"] or end <= start:
                raise ValueError("invalid steady-rendering window")
            trace = output / "stage.csv"
            steady = read_stages(trace, (start, end))
            intervals = steady.get("SceneLoad.Render.FrameIntervalMs")
            if not intervals or intervals["count"] < args.steady_frames - 1:
                raise ValueError("steady trace lacks the required render intervals")
            trace_records = list(capture_records(trace))
            report["steady_render"] = {"start_ns": start, "end_ns": end, "submitted_frames": frames,
                                       "viewport": viewport, "intervals": intervals,
                                       "steady_interval_outliers": render_outliers(trace_records, (start, end), intervals["p95_ms"]),
                                       "initial_interval_outliers": render_outliers(trace_records,
                                           (samples[0]["admission_ns"], samples[0]["first_submission_ns"]), intervals["p95_ms"]),
                                       "scope": "diagnostic comparison; initial bootstrap and steady scene differ, so not matched-frame qualification"}
            if args.steady_replacement:
                if len(samples) != 2 or end > samples[1]["admission_ns"]:
                    raise ValueError("replacement overlaps the steady-rendering window")
                replacement = (samples[1]["admission_ns"], samples[1]["first_submission_ns"])
                report["steady_render"]["replacement_interval_outliers"] = render_outliers(
                    trace_records, replacement, intervals["p95_ms"])
                report["steady_render"]["replacement_main_frame_diagnostics"] = main_frame_diagnostics(trace_records, replacement)
                report["steady_render"]["replacement_stages"] = read_stages(trace, replacement)
        (output / "capacity.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Cooked capacity probe failed: {error}", file=sys.stderr)
        sys.exit(1)
