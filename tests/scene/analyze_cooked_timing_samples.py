#!/usr/bin/env python3
"""Compare bounded fresh/replacement timing observations, not qualification."""
import argparse
import json
from pathlib import Path
import re
import sys

from validate_scene_lifecycle import distribution


def analyze(fresh, replacements, count=30):
    if count < 1:
        raise ValueError("sample count must be positive")
    first = fresh["fresh_process_samples"]
    same = replacements["samples"]
    if len(first) != count or len(same) != count + 1 or replacements["reloads"] != count:
        raise ValueError("fresh or replacement sample count is incomplete")
    if [item["generation"] for item in first] != [2] * count or [item["generation"] for item in same] != list(range(2, count + 3)):
        raise ValueError("unexpected scene generations")
    if fresh["manifest_sha256"] != replacements["manifest_sha256"] or not re.fullmatch(r"[0-9a-f]{64}", fresh["manifest_sha256"]):
        raise ValueError("timing runs do not use the same pinned cooked manifest")
    if fresh["binary_sha256"] != replacements["binary_sha256"] or any(not re.fullmatch(r"[0-9a-f]{64}", fresh["binary_sha256"].get(key, "")) for key in ("runtime_sha256", "cooker_sha256")):
        raise ValueError("timing runs do not use the same pinned binaries")
    if fresh["host_system"] != replacements["host_system"] or fresh["host_machine"] != replacements["host_machine"]:
        raise ValueError("timing runs do not identify the same host")
    for key in ("hardware", "device", "shader_cache"):
        if (key in fresh or key in replacements) and (key not in fresh or key not in replacements or fresh[key] != replacements[key]):
            raise ValueError(f"timing runs do not identify the same {key}")
    shaders = fresh.get("shader_cache")
    if shaders is not None and (not isinstance(shaders, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(shaders.get("sha256", ""))) or not isinstance(shaders.get("files"), int) or isinstance(shaders.get("files"), bool) or shaders["files"] < 1):
        raise ValueError("invalid shader_cache identity")
    hardware = fresh.get("hardware")
    if hardware is not None and (not isinstance(hardware, dict) or not isinstance(hardware.get("cpu"), str) or not hardware["cpu"].strip() or not isinstance(hardware.get("physical_memory_bytes"), int) or isinstance(hardware.get("physical_memory_bytes"), bool) or hardware["physical_memory_bytes"] <= 0):
        raise ValueError("invalid hardware identity")
    device = fresh.get("device")
    if device is not None and (not isinstance(device, dict) or not isinstance(device.get("gpu"), str) or not device["gpu"].strip() or not isinstance(device.get("driver_path"), str) or not device["driver_path"].strip() or not re.fullmatch(r"[0-9a-f]{64}", str(device.get("driver_sha256", "")))):
        raise ValueError("invalid device identity")
    if fresh["validation_states"] != [True] * count or replacements["validation_states"] != [True]:
        raise ValueError("validation was not confirmed for every process")
    if fresh["source_audit_enabled"] is not True or replacements["source_audit_enabled"] is not True:
        raise ValueError("source-independence audit was not enabled for both runs")
    if fresh["gpu_completion_observed"] is not True or replacements["gpu_completion_observed"] is not True:
        raise ValueError("GPU completion observation was not enabled for both runs")
    viewports = fresh["timing_viewports"] + replacements["timing_viewports"]
    if len(viewports) != count + 1 or any(not isinstance(view, dict) or view.get("resize_after_baseline") is not False for view in viewports):
        raise ValueError("viewport evidence is missing or includes a resize")
    for view in viewports:
        if any(not isinstance(view.get(key), list) or len(view[key]) != 2 or any(not isinstance(value, int) or isinstance(value, bool) or value <= 0 for value in view[key]) for key in ("scene_viewport", "window", "framebuffer")):
            raise ValueError("invalid viewport dimensions")
    if any(view != viewports[0] for view in viewports[1:]):
        raise ValueError("timing runs do not use matching viewport dimensions")
    if any(not isinstance(item["filesystem_cache_conditions"], str) or not item["filesystem_cache_conditions"].strip() for item in (fresh, replacements)):
        raise ValueError("filesystem cache conditions are missing")

    def milliseconds(items):
        result = []
        for item in items:
            admission, submitted, elapsed = (item[key] for key in ("admission_ns", "first_submission_ns", "admission_to_submission_ns"))
            if any(isinstance(value, bool) or not isinstance(value, int) for value in (admission, submitted, elapsed)) or admission <= 0 or submitted <= admission or elapsed != submitted - admission:
                raise ValueError("invalid admission-to-submission sample")
            result.append(elapsed / 1_000_000)
        return result

    def completion_milliseconds(items):
        result = []
        for item in items:
            observed, timeline, elapsed = (item[key] for key in ("render_completion_observed_ns", "render_timeline_value", "admission_to_completion_observed_ns"))
            if any(isinstance(value, bool) or not isinstance(value, int) for value in (observed, timeline, elapsed)) or timeline <= 0 or observed < item["first_submission_ns"] or elapsed != observed - item["admission_ns"]:
                raise ValueError("invalid observed render completion")
            result.append(elapsed / 1_000_000)
        return result

    return {"scope": "diagnostic accepted-submission and observed GPU-completion timing; not presentation or qualification",
            "manifest_sha256": fresh["manifest_sha256"], "binary_sha256": fresh["binary_sha256"],
            "host_system": fresh["host_system"], "host_machine": fresh["host_machine"],
            **{key: fresh[key] for key in ("hardware", "device", "shader_cache") if key in fresh},
            "viewport": viewports[0],
            "cache_conditions": {"fresh": fresh["filesystem_cache_conditions"], "replacement": replacements["filesystem_cache_conditions"]},
            "fresh_process_loads": distribution(milliseconds(first)),
            "same_process_initial_load": distribution(milliseconds(same[:1])),
            "same_process_replacements": distribution(milliseconds(same[1:])),
            "same_process_first_replacement": distribution(milliseconds(same[1:2])),
            "same_process_later_replacements": distribution(milliseconds(same[2:])) if count > 1 else None,
            "fresh_process_completion_observed": distribution(completion_milliseconds(first)),
            "same_process_initial_completion_observed": distribution(completion_milliseconds(same[:1])),
            "same_process_replacements_completion_observed": distribution(completion_milliseconds(same[1:])),
            "same_process_first_replacement_completion_observed": distribution(completion_milliseconds(same[1:2])),
            "same_process_later_replacements_completion_observed": distribution(completion_milliseconds(same[2:])) if count > 1 else None,
            "note": "Fresh process does not imply cold disk I/O. GPU completion is an upper-bound polling observation, not presentation."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fresh", required=True, type=Path)
    parser.add_argument("--replacements", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--count", type=int, default=30)
    args = parser.parse_args()
    report = analyze(json.loads(args.fresh.read_text()), json.loads(args.replacements.read_text()), args.count)
    with args.output.open("x", encoding="utf-8") as target:
        json.dump(report, target, indent=2)
        target.write("\n")
    print(f"Wrote timing diagnostics to {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Cooked timing analysis failed: {error}", file=sys.stderr)
        sys.exit(1)
