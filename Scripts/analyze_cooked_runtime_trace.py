#!/usr/bin/env python3
"""Summarize standalone steady and load windows without claiming qualification."""
import argparse
import json
from pathlib import Path
import sys

from validate_scene_lifecycle import capture_records, distribution, read_stages

STAGES = (
    "SceneLoad.Main.FrameWorkMs",
    "SceneLoad.Main.FrameIntervalMs",
    "SceneLoad.Main.RuntimeUpdate",
    "SceneLoad.Main.PlatformEvents",
    "SceneLoad.Main.OverlayBuild",
    "SceneLoad.Main.SceneSynchronization",
    "SceneLoad.Main.PrepareScene",
    "SceneLoad.Main.PublishFrame",
    "SceneLoad.Main.RuntimeGuardedCommit",
    "SceneLoad.Render.FrameWorkMs",
    "SceneLoad.Render.FrameIntervalMs",
    "SceneLoad.Render.CookedUploadVisit",
    "SceneLoad.Render.EnvironmentPreparation",
    "SceneLoad.Render.QueueSubmit",
    "SceneLoad.Render.AcquireImage",
    "SceneLoad.Render.PresentWSI",
    "SceneLoad.Render.RecordScene",
)


def frame_scope_diagnostics(records, window, frame_stage, scopes_to_report):
    """Associate fully contained scopes with a measured frame on one thread."""
    frames = sorted((timestamp, value) for kind, name, timestamp, value in records
                    if kind == "value" and name == frame_stage and window[0] <= timestamp <= window[1])
    names = {name for name, _ in scopes_to_report}
    scopes = [(name, timestamp, value) for kind, name, timestamp, value in records
              if kind == "scope_ns" and name in names
              and window[0] <= timestamp <= window[1]]
    result = {"frames": len(frames), "partial_scopes": 0, "unassigned_scopes": []}
    for stage, label in scopes_to_report:
        per_frame = {}
        for name, end, duration in scopes:
            if name != stage:
                continue
            candidates = [index for index, (frame_end, work_ms) in enumerate(frames)
                          if frame_end - work_ms * 1_000_000 <= end - duration and end <= frame_end]
            if len(candidates) == 1:
                index = candidates[0]
                per_frame[index] = per_frame.get(index, 0) + duration / 1_000_000
            else:
                result["partial_scopes"] += 1
                result["unassigned_scopes"].append({"stage": name, "end_ns": end, "duration_ms": duration / 1_000_000})
        values = list(per_frame.values())
        result[label] = distribution(values) if values else None
        if values:
            worst = max(per_frame, key=per_frame.get)
            result[label]["worst_frame_end_ns"] = frames[worst][0]
    result["unassigned_scopes"].sort(key=lambda item: item["duration_ms"], reverse=True)
    result["unassigned_scopes"] = result["unassigned_scopes"][:20]
    return result


def main_frame_diagnostics(records, window):
    # RuntimeUpdate contains non-loading work. Commit is nested within it and
    # must be reported separately, rather than added to the update duration.
    return frame_scope_diagnostics(records, window, "SceneLoad.Main.FrameWorkMs", (
        ("SceneLoad.Main.RuntimeUpdate", "runtime_update_ms"),
        ("SceneLoad.Main.RuntimeGuardedCommit", "guarded_commit_ms")))


def render_frame_diagnostics(records, window):
    return frame_scope_diagnostics(records, window, "SceneLoad.Render.FrameWorkMs", (
        ("SceneLoad.Render.CookedUploadVisit", "cooked_upload_ms"),
        ("SceneLoad.Render.EnvironmentPreparation", "environment_preparation_ms")))


def window_occlusion(records, window):
    """Return macOS window visibility transitions, including state at entry."""
    samples = sorted((timestamp, value) for kind, name, timestamp, value in records
                     if kind == "value" and name == "SceneLoad.Main.WindowOcclusionVisible" and timestamp <= window[1])
    if not samples:
        return None
    if any(value not in (0, 1, 2) for _, value in samples):
        raise ValueError("invalid window occlusion observation")
    preceding = [(timestamp, value) for timestamp, value in samples if timestamp <= window[0]]
    return {"visible_at_start": preceding[-1][1] if preceding else None,
            "changes": [{"timestamp_ns": timestamp, "visible": value} for timestamp, value in samples if window[0] < timestamp <= window[1]],
            "scope": "macOS window occlusion state (0 occluded, 1 visible, 2 unknown); visibility does not prove drawable availability or timing cause"}


def render_outliers(records, window, steady_p95_ms, allowance_ms=8.0):
    """Locate long intervals and overlapping render scopes without assigning cause."""
    threshold = steady_p95_ms + allowance_ms
    intervals = [(timestamp, value) for kind, name, timestamp, value in records
                 if kind == "value" and name == "SceneLoad.Render.FrameIntervalMs"
                 and window[0] <= timestamp <= window[1] and value > threshold]
    scopes = [(name, timestamp, duration) for kind, name, timestamp, duration in records
              if kind == "scope_ns" and name.startswith("SceneLoad.Render.")]
    platform_events = [(timestamp, duration) for kind, name, timestamp, duration in records
                       if kind == "scope_ns" and name == "SceneLoad.Main.PlatformEvents"]
    worker_values = [(name, timestamp, value) for kind, name, timestamp, value in records
                     if kind == "value" and name.startswith("SceneLoad.RecordingWorker.")]
    submit_values = [(name, timestamp, value) for kind, name, timestamp, value in records
                     if kind == "value" and name.startswith("SceneLoad.QueueSubmit.")]
    details = []
    for end, interval_ms in sorted(intervals, key=lambda entry: entry[1], reverse=True)[:20]:
        start = end - interval_ms * 1_000_000
        overlaps = [{"name": name, "end_ns": scope_end, "duration_ms": duration / 1_000_000}
                    for name, scope_end, duration in scopes
                    if scope_end >= start and scope_end - duration <= end]
        overlaps.sort(key=lambda scope: scope["duration_ms"], reverse=True)
        platform_overlaps = [{"end_ns": event_end, "duration_ms": duration / 1_000_000,
                              "overlap_ms": max(0, min(end, event_end) - max(start, event_end - duration)) / 1_000_000}
                             for event_end, duration in platform_events
                             if event_end >= start and event_end - duration <= end]
        platform_overlaps.sort(key=lambda event: event["overlap_ms"], reverse=True)
        worker_diagnostics = [{"name": name, "timestamp_ns": timestamp, "value_ms": value}
                              for name, timestamp, value in worker_values if start <= timestamp <= end]
        worker_diagnostics.sort(key=lambda sample: sample["value_ms"], reverse=True)
        submit_diagnostics = [{"name": name, "timestamp_ns": timestamp, "value_ms": value}
                              for name, timestamp, value in submit_values if start <= timestamp <= end]
        details.append({"interval_end_ns": end, "interval_ms": interval_ms, "longest_overlapping_scopes": overlaps[:5],
                        "overlapping_platform_events": platform_overlaps[:5], "recording_worker_samples": worker_diagnostics[:20],
                        "queue_submit_samples": submit_diagnostics, "window_occlusion": window_occlusion(records, (start, end))})
    return {"threshold_ms": threshold, "count": len(intervals), "largest_intervals": details,
            "note": "Overlap is diagnostic only; nested scopes are not additive and do not establish causation."}


def analyze(evidence, trace, minimum_steady_frames):
    samples = evidence["samples"]
    if len(samples) != 2 or [sample["generation"] for sample in samples] != [2, 3]:
        raise ValueError("expected one initial load followed by one replacement")
    first, replacement = samples
    initial = (first["admission_ns"], first["first_submission_ns"])
    steady = (first["steady_start_ns"], first["steady_end_ns"])
    loading = (replacement["admission_ns"], replacement["first_submission_ns"])
    post = None
    if any(key in replacement for key in ("post_steady_start_ns", "post_steady_end_ns", "post_steady_submitted_frames")):
        if not all(key in replacement for key in ("post_steady_start_ns", "post_steady_end_ns", "post_steady_submitted_frames")):
            raise ValueError("partial post-replacement steady window")
        post = (replacement["post_steady_start_ns"], replacement["post_steady_end_ns"])
        if replacement["post_steady_submitted_frames"] < minimum_steady_frames or not (loading[1] <= post[0] < post[1]):
            raise ValueError("invalid post-replacement steady window")
    if first["steady_submitted_frames"] < minimum_steady_frames or not (0 < initial[0] < initial[1] <= steady[0] < steady[1] <= loading[0] < loading[1]):
        raise ValueError("incomplete, overlapping or unordered timing windows")
    records = list(capture_records(trace))
    timestamps = [timestamp for _, _, timestamp, _ in records]
    if not timestamps or min(timestamps) > steady[0] or max(timestamps) < (post[1] if post else loading[1]):
        raise ValueError("stage capture does not cover steady and replacement windows")
    windows = {}
    requested_windows = [("initial_load", initial), ("steady", steady), ("replacement", loading)]
    if post:
        requested_windows.append(("post_steady", post))
    for name, window in requested_windows:
        stages = read_stages(trace, window)
        required = {"SceneLoad.Main.FrameWorkMs", "SceneLoad.Main.FrameIntervalMs", "SceneLoad.Render.FrameIntervalMs"}
        if not required.issubset(stages):
            raise ValueError(f"missing frame observations in {name}")
        windows[name] = {"start_ns": window[0], "end_ns": window[1],
                         "first_record_ns": min(timestamp for timestamp in timestamps if window[0] <= timestamp <= window[1]),
                         "window_occlusion": window_occlusion(records, window),
                         "main_frame_diagnostics": main_frame_diagnostics(records, window),
                         "render_frame_diagnostics": render_frame_diagnostics(records, window),
                         "stages": {stage: stages[stage] for stage in STAGES if stage in stages}}
    if windows["steady"]["stages"]["SceneLoad.Render.FrameIntervalMs"]["count"] < minimum_steady_frames - 1:
        raise ValueError("steady window does not contain the required render intervals")
    if post and windows["post_steady"]["stages"]["SceneLoad.Render.FrameIntervalMs"]["count"] < minimum_steady_frames - 1:
        raise ValueError("post-replacement steady window does not contain the required render intervals")
    steady_p95 = windows["steady"]["stages"]["SceneLoad.Render.FrameIntervalMs"]["p95_ms"]
    for name, window in (("initial_load", initial), ("replacement", loading)):
        windows[name]["render_interval_outliers"] = render_outliers(records, window, steady_p95)
    return {"scope": "diagnostic comparison; not matched-frame attribution or qualification",
            "manifest_sha256": evidence["manifest_sha256"],
            "filesystem_cache_conditions": evidence["filesystem_cache_conditions"],
            "initial_trace_starts_after_admission_ns": max(0, min(timestamps) - initial[0]),
            "initial_admission_marker_observed": sum(1 for kind, name, timestamp, _ in records
                                                     if kind == "scope_ns" and name == "SceneLoad.Main.BeforeInitialAdmission" and timestamp <= initial[0]) == 1,
            "steady_submitted_frames": first["steady_submitted_frames"], "windows": windows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-steady-frames", type=int, default=120)
    args = parser.parse_args()
    if args.minimum_steady_frames < 2:
        parser.error("--minimum-steady-frames must be at least two")
    report = analyze(json.loads(args.evidence.read_text(encoding="utf-8")), args.trace, args.minimum_steady_frames)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(f"Wrote diagnostic timing report to {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Runtime trace analysis failed: {error}", file=sys.stderr)
        sys.exit(1)
