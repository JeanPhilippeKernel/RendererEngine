#!/usr/bin/env python3
"""Compare source and standalone cooked captures of SampleProject level01.

The copied scene receives a fixed authored camera; the project is never edited.
This is a render smoke test, not an approved visual reference or qualification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from compare_scene_capture import compare_region, parse_region, read_ppm, validate_capture_log_text
from cooked_capture_identity import device_identity, shader_identity
from cooked_scene_smoke import read_console_log, read_process_logs, require_clean_vulkan_console, require_zero_source_audit, sha256_file, validation_layer_status_text

CAMERA = "b1b1b1b1-7171-4141-8181-b1b1b1b1b1b1"
SCENE = "f6d4dbea-c21d-453d-ae91-79187dfa8b84"
ALTERNATE_SCENE = "f6d4dbea-c21d-453d-ae91-79187dfa8b85"
HIERARCHY_PARENT = "beb33379-561e-4413-af36-b7d9f5a6be26"
HIERARCHY_MESH = "e97f8f91-1fe7-415a-89ec-9e67e04ad960"
CAMERA_ENTITY = f'''  - "uuid": "{CAMERA}"
    "components":
      "CameraComponent":
        "schema_version": 1
        "fields":
          "fov_y": 60
          "near": 0.1
          "far": 1000
          "aspect_ratio": 1.7777778
          "is_main": true
      "NameComponent":
        "schema_version": 1
        "fields":
          "value": "Runtime qualification camera"
      "TransformComponent":
        "schema_version": 1
        "fields":
          "position": [0.5, 2, 6]
          "rotation": [0, 0, 0]
          "scale": [1, 1, 1]
'''


def add_sampleproject_hierarchy(scene):
    """Move the mesh translation to its existing parent in a copied level01."""
    parent_start = f'  - "uuid": "{HIERARCHY_PARENT}"\n'.encode()
    mesh_start = f'  - "uuid": "{HIERARCHY_MESH}"\n'.encode()
    if scene.count(parent_start) != 1 or scene.count(mesh_start) != 1 or b'"ParentComponent"' in scene:
        raise ValueError("SampleProject scene cannot safely receive the hierarchy fixture")
    parent_index, mesh_index = scene.index(parent_start), scene.index(mesh_start)
    if parent_index >= mesh_index:
        raise ValueError("unexpected SampleProject hierarchy fixture order")
    parent_end = scene.index(b'  - "uuid": ', parent_index + len(parent_start))
    parent = scene[parent_index:parent_end]
    mesh = scene[mesh_index:]
    origin = b'"position": [0, 0, 0]'
    translation = b'"position": [0.40000004, 1.2, 0]'
    components = b'    "components":\n'
    if parent.count(origin) != 1 or mesh.count(translation) != 1 or mesh.count(components) != 1:
        raise ValueError("unexpected SampleProject transform in hierarchy fixture")
    parent = parent.replace(origin, translation, 1)
    mesh = mesh.replace(translation, origin, 1).replace(components, components +
        (f'      "ParentComponent":\n        "schema_version": 1\n        "fields":\n          "parent_uuid": "{HIERARCHY_PARENT}"\n').encode(), 1)
    return scene[:parent_index] + parent + scene[parent_end:mesh_index] + mesh


def remove_sampleproject_directional_light(scene):
    """Disable the copied scene's authored directional light for a control."""
    light = b'      "LightComponent":\n'
    intensity = b'          "intensity": 3\n'
    if scene.count(light) != 1 or scene.count(intensity) != 1:
        raise ValueError("SampleProject directional light cannot be changed safely")
    return scene.replace(intensity, b'          "intensity": 0\n', 1)


def force_opaque_materials(directory):
    """Change copied authored blend materials to opaque for a negative control."""
    changed = []
    for path in sorted(directory.rglob("*.zematerial")):
        original = path.read_bytes()
        material = json.loads(original)
        if material.get("alpha_mode") == "BLEND":
            material["alpha_mode"] = "OPAQUE"
            modified = (json.dumps(material, indent=4) + "\n").encode()
            path.write_bytes(modified)
            changed.append({"path": path.relative_to(directory).as_posix(),
                            "source_sha256": hashlib.sha256(original).hexdigest(),
                            "modified_sha256": hashlib.sha256(modified).hexdigest()})
    if not changed:
        raise ValueError("SampleProject fixture has no blend materials for the negative control")
    return changed


def compare_reviewed_reference(path, candidate, regions):
    reference = read_ppm(path)
    width, height = candidate[:2]
    return {"reference_pixel_sha256": hashlib.sha256(reference[2]).hexdigest(),
            "viewport": compare_region(reference, candidate, (0, 0, width, height)),
            "regions": {name: compare_region(reference, candidate, rect) for name, rect in regions}}


def review_result(path, candidate, regions, review_identifier):
    try:
        result = compare_reviewed_reference(path, candidate, regions)
    except ValueError as error:
        # Keep the source/cooked comparison available when reference geometry
        # or format does not match the requested profile.
        result = {"error": str(error)}
    result["review_identifier"] = review_identifier
    return result


def count_dark_pixels(capture, rect, threshold=100):
    width, height, pixels = capture
    x, y, extent_x, extent_y = rect
    if x < 0 or y < 0 or extent_x < 1 or extent_y < 1 or x + extent_x > width or y + extent_y > height:
        raise ValueError("presence region is empty or outside the capture")
    return sum(max(pixels[offset:offset + 3]) < threshold
               for row in range(y, y + extent_y)
               for offset in range((row * width + x) * 3, (row * width + x + extent_x) * 3, 3))


def parse_presence(text):
    try:
        name, minimum = text.split(":", 1)
        count = int(minimum)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected region-name:minimum-dark-pixels") from error
    if not name or count < 1:
        raise argparse.ArgumentTypeError("expected a region name and positive minimum pixel count")
    return name, count


def validate_control_baseline(evidence, baseline, binary_hashes, shaders, device):
    """Require a negative-control baseline to come from the same capture setup."""
    if device is None or evidence.get("device") != device or evidence.get("binary_sha256") != binary_hashes or evidence.get("shader_cache") != shaders:
        raise ValueError("control baseline has different or incomplete build/device identity")
    if evidence.get("source_pixel_sha256") != hashlib.sha256(baseline[2]).hexdigest():
        raise ValueError("control baseline pixels do not match their evidence")


def validation_layer_status(path):
    return validation_layer_status_text(Path(path).read_text(encoding="utf-8"))


def run_capture(command, log_directory, timeout):
    stdout_path, stderr_path = log_directory / "stdout.log", log_directory / "stderr.log"
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        subprocess.run(command, check=True, timeout=timeout, stdout=stdout, stderr=stderr)
    require_clean_vulkan_console(read_console_log(stdout_path))
    require_clean_vulkan_console(read_console_log(stderr_path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--cooker", type=Path, required=True)
    parser.add_argument("--obelisk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-assets", type=int, default=32)
    parser.add_argument("--expected-draws", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--region", action="append", type=parse_region, default=[], help="compare a named object region: name:x,y,width,height")
    parser.add_argument("--presence", action="append", type=parse_presence, default=[], help="require dark pixels in a named region: region-name:minimum-count")
    parser.add_argument("--reviewed-reference", type=Path, help="backend-specific reviewed RGB8 reference capture")
    parser.add_argument("--reference-review", help="record the reference review identifier")
    parser.add_argument("--require-validation", action="store_true", help="require Khronos validation in both capture processes")
    parser.add_argument("--fixed-window", action="store_true", help="capture both processes at a fixed 1280x720 logical window")
    parser.add_argument("--hierarchy", action="store_true", help="parent the SampleProject mesh while preserving its world transform in the copied scene")
    parser.add_argument("--replacement", action="store_true", help="replace the flat scene with the hierarchy variant before capture")
    parser.add_argument("--force-opaque", action="store_true", help="change copied blend materials to opaque for a visual negative control")
    parser.add_argument("--no-directional-light", action="store_true", help="disable the copied scene's directional light for a visual negative control")
    parser.add_argument("--different-from", type=Path, help="require the source and cooked captures to differ from this reference")
    parser.add_argument("--minimum-different-pixels", type=int, default=0, help="minimum pixels differing by the comparison channel threshold")
    parser.add_argument("--baseline-evidence", type=Path, help="verify the negative-control reference's binary, shader, device and pixel identities")
    args = parser.parse_args()
    project, cooker, obelisk, output = (p.resolve(strict=True) for p in (args.project, args.cooker, args.obelisk, args.output))
    if not project.is_file() or not output.is_dir() or any(output.iterdir()) or args.timeout <= 0 or args.minimum_assets < 1 or args.expected_draws < 1:
        parser.error("provide a project file, an empty output directory, and positive limits")
    if args.replacement and not args.hierarchy:
        parser.error("--replacement requires --hierarchy")
    if args.minimum_different_pixels < 0 or bool(args.different_from) != (args.minimum_different_pixels > 0):
        parser.error("--different-from and a positive --minimum-different-pixels must be provided together")
    if args.baseline_evidence and not args.different_from:
        parser.error("--baseline-evidence requires --different-from")
    if output == project.parent or project.parent in output.parents:
        parser.error("output must be outside the project workspace")
    if bool(args.reviewed_reference) != bool(args.reference_review):
        parser.error("--reviewed-reference and --reference-review must be provided together")
    if len({name for name, _ in args.region}) != len(args.region):
        parser.error("region names must be unique")
    if len({name for name, _ in args.presence}) != len(args.presence) or not {name for name, _ in args.presence}.issubset({name for name, _ in args.region}):
        parser.error("presence checks must reference unique declared regions")
    binary_hashes = {"cooker_sha256": sha256_file(cooker), "runtime_sha256": sha256_file(obelisk)}
    asset_root = Path(os.environ.get("ZENGINE_ASSET_ROOT") or obelisk.parent / "ZodiacEngine").resolve(strict=True)
    shader_cache = asset_root / "Shaders" / "Cache"
    shaders = shader_identity(shader_cache)
    for directory, names, files in os.walk(project.parent, followlinks=False):
        if any((Path(directory) / name).is_symlink() for name in names + files):
            raise ValueError("project copy contains a symlink")
    source_scene = project.parent / "Scenes" / "level01.zescene"
    original = source_scene.read_bytes()
    if CAMERA.encode() in original or not original.endswith(b"\n"):
        raise ValueError("scene cannot safely receive the fixture camera")
    runtime_log = output / "runtime-log"
    runtime_log.mkdir()
    source_log = output / "source-log"
    source_log.mkdir()
    capture = output / "runtime.ppm"
    source_capture = output / "source.ppm"
    with tempfile.TemporaryDirectory(prefix="renderer-sample-runtime-") as temporary:
        root = Path(temporary)
        workspace = root / "project"
        shutil.copytree(project.parent, workspace)
        copied_project = workspace / project.name
        config = json.loads(copied_project.read_text(encoding="utf-8"))
        if config.get("workingSpace") != ".":
            raise ValueError("project must use a project-local workspace")
        modified_materials = force_opaque_materials(workspace / "Assets" / "Materials") if args.force_opaque else []
        config["sceneList"] = [{"name": "level01", "isDefault": True}]
        if args.replacement:
            config["sceneList"].append({"name": "level01_hierarchy", "isDefault": False})
        copied_project.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        variant = remove_sampleproject_directional_light(original) if args.no_directional_light else original
        augmented = (add_sampleproject_hierarchy(variant) if args.hierarchy else variant) + CAMERA_ENTITY.encode()
        scene_path = workspace / "Scenes" / "level01.zescene"
        if args.replacement:
            marker = ('"uuid": "' + SCENE + '"').encode()
            if original.count(marker) != 1 or ALTERNATE_SCENE.encode() in original:
                raise ValueError("SampleProject scene cannot safely receive the alternate identity")
            alternate = augmented.replace(marker, ('"uuid": "' + ALTERNATE_SCENE + '"').encode(), 1)
            scene_path.write_bytes(variant + CAMERA_ENTITY.encode())
            (workspace / "Scenes" / "level01_hierarchy.zescene").write_bytes(alternate)
        else:
            scene_path.write_bytes(augmented)
        cook = root / "cook"
        cook.mkdir()
        subprocess.run([str(cooker), "smoke-offline", str(copied_project), str(cook), "805306368"], check=True, timeout=args.timeout)
        package = cook / "output" / "desktop-vulkan-le64"
        manifest_bytes = (package / "manifest.json").read_bytes()
        manifest = json.loads(manifest_bytes)
        selected = [item for item in manifest["scenes"] if Path(item["source"]).name == "level01.zescene"]
        if len(selected) != 1 or len(manifest["assets"]) < args.minimum_assets:
            raise ValueError("cooked manifest does not contain the required scene and assets")
        if args.replacement:
            if {item["uuid"] for item in manifest["scenes"]} != {SCENE, ALTERNATE_SCENE}:
                raise ValueError("cooked manifest does not contain both replacement scenes")
            scene_path.write_bytes(augmented)
        fixed_window = ["--cookedFixedWindow"] if args.fixed_window else []
        run_capture([str(obelisk), "--launchEditor", "true", "--projectConfigFile", str(copied_project), "--sourceCapture", str(source_capture), "--cookedRuntimeView", "--cookedCamera", CAMERA, "--cookedExpectedDraws", str(args.expected_draws), "--logDirectory", str(source_log), *fixed_window], source_log, args.timeout)
        source_text = read_process_logs(source_log)
        validate_capture_log_text(source_text)
        source_validation = validation_layer_status_text(source_text)
        if args.require_validation and not source_validation:
            raise ValueError("source capture did not enable Khronos Vulkan validation")
        shutil.rmtree(workspace)
        replacement = ["--cookedReloads", "1", "--cookedAlternateScene", ALTERNATE_SCENE] if args.replacement else []
        run_capture([str(obelisk), "--cookedStandalone", "--cookedPackage", str(package), "--cookedScene", selected[0]["uuid"], "--cookedCamera", CAMERA, "--cookedExitAfterFrame", "--cookedCapture", str(capture), "--cookedExpectedDraws", str(args.expected_draws), "--cookedAuditSource", "--logDirectory", str(runtime_log), *fixed_window, *replacement], runtime_log, args.timeout)
        runtime_text = read_process_logs(runtime_log)
        if args.replacement:
            published = re.findall(r"\[CookedRuntime\] Published cooked scene generation (\d+) scene=([0-9a-f-]+)", runtime_text)
            if published != [("2", SCENE), ("3", ALTERNATE_SCENE)]:
                raise ValueError("replacement capture did not publish the expected scenes")
        validate_capture_log_text(runtime_text)
        runtime_validation = validation_layer_status_text(runtime_text)
        if args.require_validation and not runtime_validation:
            raise ValueError("runtime capture did not enable Khronos Vulkan validation")
        require_zero_source_audit(runtime_text)
        source_device, runtime_device = device_identity(source_text), device_identity(runtime_text)
        if source_device != runtime_device:
            raise ValueError("source and cooked captures used different or incompletely identified devices")
        if sha256_file(cooker) != binary_hashes["cooker_sha256"] or sha256_file(obelisk) != binary_hashes["runtime_sha256"] or shader_identity(shader_cache) != shaders:
            raise ValueError("binary or shader cache changed during capture")
    width, height, pixels = read_ppm(capture)
    if min(pixels) == max(pixels):
        raise ValueError("runtime scene capture has no visible variation")
    reference = read_ppm(source_capture)
    candidate = (width, height, pixels)
    viewport = compare_region(reference, candidate, (0, 0, width, height))
    regions = {name: compare_region(reference, candidate, rect) for name, rect in args.region}
    rectangles = dict(args.region)
    presence = {}
    for name, minimum in args.presence:
        source_count = count_dark_pixels(reference, rectangles[name])
        runtime_count = count_dark_pixels(candidate, rectangles[name])
        presence[name] = {"threshold_rgb_exclusive": 100, "minimum_pixels": minimum,
                          "source_pixels": source_count, "runtime_pixels": runtime_count,
                          "passed": source_count >= minimum and runtime_count >= minimum}
    report = {"scope": "source-versus-standalone cooked SampleProject smoke; added camera; visual review required",
              "original_scene_sha256": hashlib.sha256(original).hexdigest(),
              "binary_sha256": binary_hashes, "shader_cache": shaders, "device": runtime_device,
              "augmented_scene_sha256": hashlib.sha256(augmented).hexdigest(),
              "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
              "scene_uuid": selected[0]["uuid"], "camera_uuid": CAMERA,
              "captured_scene_uuid": ALTERNATE_SCENE if args.replacement else selected[0]["uuid"],
              "alternate_scene_sha256": hashlib.sha256(alternate).hexdigest() if args.replacement else None,
              "camera_position": [0.5, 2, 6], "cooked_assets": len(manifest["assets"]),
              "source_audit": "zero denied instrumented engine-VFS source reads, registry lookups, and import enqueues from service initialization through joined-worker shutdown",
              "expected_draws": args.expected_draws, "width": width, "height": height,
              "source_pixel_sha256": hashlib.sha256(reference[2]).hexdigest(),
              "runtime_pixel_sha256": hashlib.sha256(pixels).hexdigest(),
              "fixed_window": args.fixed_window,
              "hierarchy_fixture": args.hierarchy,
              "replacement_fixture": args.replacement,
              "opaque_negative_control": modified_materials,
              "directional_light_disabled": args.no_directional_light,
              "source_validation_enabled": source_validation, "runtime_validation_enabled": runtime_validation,
              "viewport": viewport, "regions": regions, "presence": presence}
    if args.different_from:
        baseline = read_ppm(args.different_from)
        if args.baseline_evidence:
            validate_control_baseline(json.loads(args.baseline_evidence.read_text(encoding="utf-8")), baseline, binary_hashes, shaders, runtime_device)
        source_difference = compare_region(baseline, reference, (0, 0, width, height))
        runtime_difference = compare_region(baseline, candidate, (0, 0, width, height))
        for difference in (source_difference, runtime_difference):
            difference["within_similarity_tolerance"] = difference.pop("passed")
        report["negative_control"] = {"baseline_pixel_sha256": hashlib.sha256(baseline[2]).hexdigest(),
                                      "baseline_identity_verified": bool(args.baseline_evidence),
                                      "minimum_different_pixels": args.minimum_different_pixels,
                                      "source": source_difference, "runtime": runtime_difference,
                                      "passed": min(source_difference["changed_pixels"], runtime_difference["changed_pixels"]) >= args.minimum_different_pixels}
    if args.reviewed_reference:
        report["reviewed_reference"] = review_result(args.reviewed_reference, candidate, args.region, args.reference_review)
    (output / "runtime-smoke.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    reviewed = report.get("reviewed_reference")
    reviewed_passed = not reviewed or ("error" not in reviewed and reviewed["viewport"]["passed"] and all(item["passed"] for item in reviewed["regions"].values()))
    if not viewport["passed"] or not all(item["passed"] for item in regions.values()) or not all(item["passed"] for item in presence.values()) or not reviewed_passed or not report.get("negative_control", {"passed": True})["passed"]:
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"SampleProject runtime smoke failed: {error}", file=sys.stderr)
        sys.exit(1)
