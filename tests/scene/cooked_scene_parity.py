#!/usr/bin/env python3
"""Compare source and cooked renders of one generated HDR/mesh fixture.

Requires a desktop Vulkan session and built SceneCookProcess and Obelisk.
The generated fixture is a semantic smoke test, not a reviewed golden or the
SampleProject qualification fixture. The output directory must be empty.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from compare_scene_capture import compare_region, read_ppm, validate_capture_log_text
from cooked_scene_smoke import read_process_logs, require_zero_source_audit
from sampleproject_cooked_runtime import run_capture, validation_layer_status_text


SCENE = "10000000-0000-0000-0000-000000000001"
ENVIRONMENT = "60000000-0000-0000-0000-000000000006"


def run(command, timeout):
    subprocess.run([str(arg) for arg in command], check=True, timeout=timeout)


def compare_transparency(opaque, blended):
    """Require a visible region between the opaque triangle and background."""
    width, height, opaque_pixels = opaque
    if blended[:2] != (width, height):
        raise ValueError("opaque and transparent reference dimensions differ")
    blend_pixels = blended[2]
    background = opaque_pixels[:3]
    intermediate = sum(all(opaque_pixels[index + channel] + 8 < blend_pixels[index + channel] < background[channel] - 8
                           for channel in range(3))
                       for index in range(0, len(opaque_pixels), 3))
    return {"intermediate_pixels": intermediate, "minimum_pixels": (width * height + 499) // 500,
            "passed": intermediate * 500 >= width * height}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cooker", required=True, type=Path)
    parser.add_argument("--obelisk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=120)
    variant = parser.add_mutually_exclusive_group()
    variant.add_argument("--blend", action="store_true", help="use the transparent material fixture")
    variant.add_argument("--hierarchy", action="store_true", help="place the mesh under a translated parent")
    parser.add_argument("--flat-reference", type=Path, help="compare the hierarchy capture with the equivalent flat source capture")
    parser.add_argument("--opaque-reference", type=Path, help="compare a blend capture with the matching opaque source capture")
    parser.add_argument("--require-validation", action="store_true", help="require Khronos validation for both captures")
    parser.add_argument("--standalone", action="store_true", help="use the authored camera and source-independent standalone runtime")
    args = parser.parse_args()
    cooker = args.cooker.resolve(strict=True)
    obelisk = args.obelisk.resolve(strict=True)
    output = args.output.resolve(strict=True)
    if args.timeout <= 0 or not output.is_dir() or any(output.iterdir()):
        parser.error("--output must be an existing empty directory and --timeout positive")
    if args.flat_reference and not args.hierarchy:
        parser.error("--flat-reference requires --hierarchy")
    if args.opaque_reference and not args.blend:
        parser.error("--opaque-reference requires --blend")
    source_log, cooked_log = output / "source-log", output / "cooked-log"
    source_log.mkdir()
    cooked_log.mkdir()
    source_image, cooked_image = output / "source.ppm", output / "cooked.ppm"

    with tempfile.TemporaryDirectory(prefix="renderer-parity-") as temporary:
        root = Path(temporary)
        baseline = root / "baseline"
        baseline.mkdir()
        mode = "write-blend" if args.blend else "write-hierarchy" if args.hierarchy else "write"
        if args.standalone:
            mode = "write-camera" if mode == "write" else mode + "-camera"
        run([cooker, mode, baseline], args.timeout)
        source = root / "source"
        source.mkdir()
        for part in ("assets", "_cache"):
            shutil.copytree(baseline / part, source / part)
        (source / "scenes").mkdir()
        shutil.copyfile(baseline / "scenes" / "scène_東京.scene", source / "scenes" / "fixture.zescene")
        # The cooker fixture's 2x1 HDR tests serialization but cannot be
        # imported by the editor. Use a valid constant 4x2 source and have the
        # real importer produce the exact artifact later consumed by both paths.
        (source / "assets" / "sky.hdr").write_bytes(
            b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n" + b"\x80\x80\x80\x81" * 8)
        config = {"projectName": "ParityFixture", "workingSpace": ".", "sceneDir": "scenes",
                  "assetDirs": {key: "assets" for key in ("textureDir", "soundDir", "meshDir", "materialDir", "spriteDir", "environmentMapDir")},
                  "sceneList": [{"name": "fixture", "isDefault": True}]}
        (source / "project.json").write_text(json.dumps(config), encoding="utf-8")
        source_view = ["--cookedRuntimeView", "--cookedFixedWindow"] if args.standalone else []
        run_capture([str(arg) for arg in [obelisk, "--launchEditor", "true", "--projectConfigFile", source / "project.json",
             "--sourceCapture", source_image, "--sourceImport", "/assets/sky.hdr",
             "--cookedExpectedDraws", "1", "--logDirectory", source_log, *source_view]], source_log, args.timeout)
        source_text = read_process_logs(source_log)
        validate_capture_log_text(source_text)
        source_validation = validation_layer_status_text(source_text)
        if args.require_validation and not source_validation:
            raise ValueError("source capture did not enable Khronos Vulkan validation")

        cooking = root / "cooking"
        cooking.mkdir()
        for part in ("assets", "_cache"):
            shutil.copytree(source / part, cooking / part)
        for part in ("scenes", "output", "scratch"):
            shutil.copytree(baseline / part, cooking / part)
        meta_path = cooking / "assets" / "sky.hdr.meta"
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        meta["artifact_path"] = f"envmaps/{ENVIRONMENT}.zenvmap"
        meta_path.write_text(json.dumps(meta), encoding="utf-8")
        run([cooker, "recook", cooking], args.timeout)
        package = root / "package"
        shutil.copytree(cooking / "output" / "desktop-vulkan-le64", package)
        # The consumer cannot reach the generated source tree or its registry.
        shutil.rmtree(source)
        shutil.rmtree(cooking)
        shutil.rmtree(baseline)
        launch = ["--cookedStandalone", "--cookedAuditSource", "--cookedFixedWindow"] if args.standalone else ["--launchEditor", "true"]
        run_capture([str(arg) for arg in [obelisk, *launch, "--cookedPackage", package,
             "--cookedScene", SCENE, "--cookedExitAfterFrame", "--cookedExpectedDraws", "1",
             "--cookedCapture", cooked_image, "--logDirectory", cooked_log]], cooked_log, args.timeout)
        cooked_text = read_process_logs(cooked_log)
        validate_capture_log_text(cooked_text)
        if args.standalone:
            require_zero_source_audit(cooked_text)
        cooked_validation = validation_layer_status_text(cooked_text)
        if args.require_validation and not cooked_validation:
            raise ValueError("cooked capture did not enable Khronos Vulkan validation")

    reference, candidate = read_ppm(source_image), read_ppm(cooked_image)
    width, height, pixels = reference
    background = pixels[:3]
    different = sum(max(abs(r - background[0]), abs(g - background[1]), abs(b - background[2])) > 4
                    for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3]))
    if different * 500 < width * height:
        raise RuntimeError("source capture is missing the fixture triangle")
    if args.blend:
        blended = sum(8 < r < background[0] - 8 and 8 < g < background[1] - 8 and 8 < b < background[2] - 8
                      for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3]))
        if blended * 500 < width * height:
            raise RuntimeError("transparent fixture has no visible intermediate-color triangle")
    comparison = compare_region(reference, candidate, (0, 0, width, height))
    report = {"scope": "generated source-versus-cooked smoke; reference review required", "material": "blend" if args.blend else "opaque", "hierarchy": args.hierarchy, "standalone": args.standalone,
              "width": width, "height": height,
              "source_validation_enabled": source_validation, "cooked_validation_enabled": cooked_validation,
              "viewport": comparison}
    if args.flat_reference:
        report["equivalent_flat"] = compare_region(read_ppm(args.flat_reference), reference, (0, 0, width, height))
    if args.opaque_reference:
        report["transparency"] = compare_transparency(read_ppm(args.opaque_reference), candidate)
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if comparison["passed"] and all(report[name]["passed"] for name in ("equivalent_flat", "transparency") if name in report) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Cooked scene parity failed: {error}", file=sys.stderr)
        sys.exit(1)
