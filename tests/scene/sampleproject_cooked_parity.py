#!/usr/bin/env python3
"""Capture a copied SampleProject scene through source and cooked paths.

Requires a desktop Vulkan session and built SceneCookProcess and Obelisk. This
records a comparison, not a reviewed reference or a calibrated qualification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from compare_scene_capture import compare_region, parse_region, read_ppm, validate_capture_log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--cooker", required=True, type=Path)
    parser.add_argument("--obelisk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scene", help="select a scene already listed in the project, or an existing scene file by stem")
    parser.add_argument("--region", action="append", type=parse_region, default=[], help="compare an important object region: name:x,y,width,height")
    parser.add_argument("--minimum-assets", type=int, default=0, help="require at least this many assets in the cooked manifest")
    parser.add_argument("--expected-draws", type=int, default=0, help="require this many prepared and submitted draws in each capture")
    parser.add_argument("--stage-trace", action="store_true", help="write a bounded cooked SceneLoad trace in the output directory")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    project = args.project.resolve(strict=True)
    cooker = args.cooker.resolve(strict=True)
    obelisk = args.obelisk.resolve(strict=True)
    output = args.output.resolve(strict=True)
    if args.timeout <= 0 or not project.is_file() or not output.is_dir() or any(output.iterdir()):
        parser.error("--project must be a file; --output an existing empty directory; --timeout positive")
    if args.minimum_assets < 0 or args.expected_draws < 0 or len({name for name, _ in args.region}) != len(args.region):
        parser.error("asset and draw limits must be nonnegative and region names must be unique")
    if output == project.parent or project.parent in output.parents:
        parser.error("--output must be outside the project workspace")
    config = json.loads(project.read_text(encoding="utf-8"))
    defaults = [scene["name"] for scene in config["sceneList"] if scene.get("isDefault")]
    if len(defaults) != 1 or config.get("workingSpace") != ".":
        raise ValueError("project must use one default scene and a project-local workspace")
    # Do not let a copied workspace silently follow authoring files outside it.
    for directory, names, files in os.walk(project.parent, followlinks=False):
        for name in names + files:
            if (Path(directory) / name).is_symlink():
                raise ValueError("project copy contains a symlink")
    source_log, cooked_log = output / "source-log", output / "cooked-log"
    source_log.mkdir()
    cooked_log.mkdir()
    source_image, cooked_image = output / "source.ppm", output / "cooked.ppm"

    with tempfile.TemporaryDirectory(prefix="renderer-sample-parity-") as temporary:
        root = Path(temporary)
        source = root / "project"
        shutil.copytree(project.parent, source)
        copied_project = source / project.name
        if args.scene:
            if not args.scene or Path(args.scene).name != args.scene or not (source / "Scenes" / (args.scene + ".zescene")).is_file():
                raise ValueError("selected scene must be a scene file stem in the copied SampleProject Scenes directory")
            config["sceneList"] = [{"name": args.scene, "isDefault": True}]
            copied_project.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
            defaults = [args.scene]
        selected_config_digest = hashlib.sha256(copied_project.read_bytes()).hexdigest()
        workspace = root / "cook"
        workspace.mkdir()
        subprocess.run([str(cooker), "smoke-offline", str(copied_project), str(workspace), "805306368"], check=True, timeout=args.timeout)
        package = workspace / "output" / "desktop-vulkan-le64"
        manifest_bytes = (package / "manifest.json").read_bytes()
        manifest = json.loads(manifest_bytes)
        if len(manifest["assets"]) < args.minimum_assets:
            raise ValueError("cooked package contains fewer assets than required")
        selected = [scene for scene in manifest["scenes"] if Path(scene["source"]).name == defaults[0] + ".zescene"]
        if len(selected) != 1:
            raise ValueError("cooked manifest does not contain the default scene exactly once")
        subprocess.run([str(obelisk), "--launchEditor", "true", "--projectConfigFile", str(copied_project),
                        "--sourceCapture", str(source_image), "--cookedExpectedDraws", str(args.expected_draws), "--logDirectory", str(source_log)], check=True, timeout=args.timeout)
        validate_capture_log(source_log / "engine_dump.log")
        shutil.rmtree(source)
        cooked_command = [str(obelisk), "--launchEditor", "true", "--cookedPackage", str(package),
                          "--cookedScene", selected[0]["uuid"], "--cookedExitAfterFrame",
                          "--cookedCapture", str(cooked_image), "--cookedExpectedDraws", str(args.expected_draws), "--logDirectory", str(cooked_log)]
        if args.stage_trace:
            cooked_command.extend(["--cookedStageTrace", str(output / "cooked-stages.csv")])
        subprocess.run(cooked_command, check=True, timeout=args.timeout)
        validate_capture_log(cooked_log / "engine_dump.log")

    reference, candidate = read_ppm(source_image), read_ppm(cooked_image)
    width, height = reference[:2]
    comparison = compare_region(reference, candidate, (0, 0, width, height))
    regions = {name: compare_region(reference, candidate, rectangle) for name, rectangle in args.region}
    stages = None
    if args.stage_trace:
        from validate_scene_lifecycle import read_stages
        all_stages = read_stages(output / "cooked-stages.csv")
        required_stages = ("SceneLoad.Worker.CookedRequest", "SceneLoad.Render.CookedUploadVisit")
        if any(name not in all_stages for name in required_stages):
            raise ValueError("cooked stage trace is missing worker or render upload observations")
        stages = {name: result for name, result in all_stages.items()
                  if ".Cooked" in name or name == "SceneLoad.Render.EnvironmentPreparation"}
    report = {"scope": "SampleProject scene source-versus-cooked smoke; reference review required",
              "project_config_sha256": hashlib.sha256(project.read_bytes()).hexdigest(),
              "selected_config_sha256": selected_config_digest,
              "scene_name": defaults[0],
              "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
              "scene_uuid": selected[0]["uuid"], "cooked_assets": len(manifest["assets"]),
              "minimum_assets": args.minimum_assets, "expected_draws": args.expected_draws,
              "width": width, "height": height, "viewport": comparison, "regions": regions,
              "cooked_stages": stages}
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if comparison["passed"] and all(region["passed"] for region in regions.values()) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"SampleProject parity failed: {error}", file=sys.stderr)
        sys.exit(1)
