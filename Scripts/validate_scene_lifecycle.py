#!/usr/bin/env python3
"""Validate SceneLifecycleProcess evidence; not a production acceptance certificate.

The executable checks outcomes, generations and per-slot ownership pins. This
independent reader checks completeness, finite timings and exported ledgers, and
summarizes optional bounded stage captures. Presentation, pixel comparisons,
matched steady-frame stalls and platform qualification need separate evidence.
"""

import argparse
import csv
from decimal import Decimal
import json
import math
from pathlib import Path


LEDGERS = (
    "cpu_cache_live_bytes", "cpu_cache_live_blocks", "gpu_allocation_bytes",
    "gpu_admitted_bytes", "gpu_allocation_count", "gpu_reserved_records",
    "native_reserved",
)
DRAINED = ("upload_live_bytes", "upload_live_blocks", "native_queued", "native_draining")
TIMINGS = ("request_to_settled_submission_ms", "max_host_update_ms", "request_to_first_submission_ms")
SCENARIOS = {
    0: "replacement",
    1: "cancel_before_dispatch",
    2: "superseding_replacement",
    3: "cancel_after_dispatch",
    4: "steady_rendering",
    5: "capacity_accepted",
    6: "capacity_refused",
}
COUNTERS = ("cycle", "mode", "submitted_generation", "submitted_frames") + LEDGERS + DRAINED
PEAKS = {
    "cpu_cache_peak_bytes": "cpu_cache_live_bytes",
    "upload_sum_pool_peak_bytes": "upload_live_bytes",
    "gpu_peak_allocation_bytes": "gpu_allocation_bytes",
    "gpu_peak_admitted_bytes": "gpu_admitted_bytes",
    "gpu_peak_allocation_count": "gpu_allocation_count",
}


def distribution(values):
    """Nearest-rank quantiles; maxima are never trimmed as outliers."""
    if not values or any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("missing, negative or non-finite timing samples")
    ordered = sorted(values)
    return {
        "count": len(ordered),
        **{name: ordered[math.ceil(len(ordered) * fraction) - 1]
           for name, fraction in (("p50_ms", .5), ("p95_ms", .95), ("p99_ms", .99))},
        "max_ms": ordered[-1],
    }


def read_samples(path, cycles, warmup, zero_retention=False, steady=False, capacity_draws=0):
    if cycles < 1 or warmup < 0 or (zero_retention and warmup < 8):
        raise ValueError("invalid cycle/warmup counts")
    if capacity_draws < 0 or sum((zero_retention, steady, bool(capacity_draws))) > 1:
        raise ValueError("invalid/mutually exclusive lifecycle modes")
    with Path(path).open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        required = set(COUNTERS + TIMINGS + ("phase",))
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError("incomplete lifecycle CSV header")
        if len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("duplicate lifecycle CSV columns")
        rows = list(reader)
    if len(rows) != cycles + warmup:
        raise ValueError(f"expected {cycles + warmup} complete cycles, found {len(rows)}")
    baseline = None
    previous_generation = 0
    previous_submissions = 0
    measured = []
    scenarios = {}
    windows = []
    peaks = {}
    for index, row in enumerate(rows):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f"malformed CSV row {index}")
        try:
            integers = {name: int(row[name]) for name in COUNTERS}
            timings = {name: float(row[name]) for name in TIMINGS}
        except (ValueError, TypeError) as error:
            raise ValueError(f"invalid numeric value at cycle {index}") from error
        if any(value < 0 for value in integers.values()) or any(not math.isfinite(value) or value < 0 for value in timings.values()):
            raise ValueError(f"negative or non-finite value at cycle {index}")
        phase = "warmup" if index < warmup else "measured"
        expected_mode = (6 if index % 4 == 1 else 5) if capacity_draws else 4 if steady and index else index % 4
        if integers["cycle"] != index or integers["mode"] != expected_mode or row["phase"] != phase:
            raise ValueError(f"missing/reordered cycle or invalid mode/phase at {index}")
        if integers["submitted_generation"] < max(1, previous_generation) or integers["submitted_frames"] < 8:
            raise ValueError(f"missing settled generation submissions at cycle {index}")
        if steady and index and (integers["submitted_generation"] != previous_generation or integers["submitted_frames"] < previous_submissions + 8):
            raise ValueError(f"steady window changed generation or replayed old submissions at cycle {index}")
        if capacity_draws:
            try:
                draws = int(row["accepted_draw_count"])
            except (KeyError, ValueError, TypeError) as error:
                raise ValueError("capacity evidence needs accepted draw counts") from error
            if draws != capacity_draws:
                raise ValueError(f"accepted draw capacity mismatch at cycle {index}")
            if index and integers["submitted_generation"] != previous_generation + (expected_mode != 6):
                raise ValueError(f"capacity refusal/commit changed generation incorrectly at cycle {index}")
        if timings["request_to_first_submission_ms"] > timings["request_to_settled_submission_ms"]:
            raise ValueError(f"first submission follows settlement at cycle {index}")
        previous_generation = integers["submitted_generation"]
        previous_submissions = integers["submitted_frames"]
        if set(PEAKS).intersection(row):
            try:
                current_peaks = {name: int(row[name]) for name in PEAKS}
            except (KeyError, ValueError, TypeError) as error:
                raise ValueError("incomplete/invalid high-water evidence") from error
            if any(value < integers[live] or value < peaks.get(name, 0)
                   for name, live in PEAKS.items() for value in (current_peaks[name],)):
                raise ValueError(f"high-water evidence below live or previous peak at cycle {index}")
            peaks = current_peaks
        if "window_started_ns" in row or "sample_timestamp_ns" in row:
            try:
                start, end = int(row["window_started_ns"]), int(row["sample_timestamp_ns"])
            except (ValueError, KeyError) as error:
                raise ValueError("incomplete/invalid sample window") from error
            if start < 0 or end <= start or (windows and start < windows[-1][1]):
                raise ValueError("overlapping or non-monotonic sample windows")
            windows.append((start, end))
        if index >= warmup:
            if zero_retention:
                ledger = tuple(integers[name] for name in LEDGERS)
                if any(integers[name] for name in DRAINED):
                    raise ValueError(f"undrained reference at cycle {index}")
                if baseline is None:
                    baseline = ledger
                if ledger != baseline:
                    raise ValueError(f"reference live ledger changed at cycle {index}")
            measured.append(timings)
            scenario = SCENARIOS[expected_mode]
            if zero_retention:
                # This sample includes a subsequent reference restoration, not
                # just the original operation's first-frame latency.
                scenario = "reference_restoration_after_" + scenario
            scenarios.setdefault(scenario, []).append(timings)
    return {
        "measured_cycles": cycles,
        "warmup_cycles": warmup,
        "reference_restoration": zero_retention,
        "steady_rendering": steady,
        "capacity_draws": capacity_draws or None,
        "observed_high_water": peaks or None,
        "measured_window_ns": [windows[warmup][0], windows[-1][1]] if windows else None,
        "timings": {name: distribution([row[name] for row in measured]) for name in TIMINGS},
        "timings_note": "Aggregate cycle timings include cancellations/refusals and steady windows; use scenario_timings for successful-load percentiles and sample counts.",
        "scenario_timings": {
            scenario: {
                "measured_cycles": len(values),
                "timings": {name: distribution([row[name] for row in values]) for name in TIMINGS},
            }
            for scenario, values in sorted(scenarios.items())
        },
        "reference_ledger": dict(zip(LEDGERS, baseline)) if baseline else None,
    }


def capture_records(path, window=None):
    with Path(path).open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != ["kind", "name", "timestamp_ns", "value"]:
            raise ValueError("invalid stage CSV header")
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError("malformed stage CSV row")
            if row["kind"] not in ("scope_ns", "value") or not row["name"].startswith("SceneLoad."):
                raise ValueError("unexpected stage kind/name")
            try:
                timestamp = int(row["timestamp_ns"])
                value = float(row["value"])
            except (ValueError, TypeError) as error:
                raise ValueError("invalid stage numeric value") from error
            if timestamp < 0 or not math.isfinite(value) or value < 0:
                raise ValueError("negative or non-finite stage value")
            if window and not window[0] <= timestamp <= window[1]:
                continue
            if row["kind"] == "value":
                memory = row["name"].startswith("SceneLoad.Memory.") and row["name"].endswith("Bytes")
                if memory and (not value.is_integer() or value > 2 ** 53 or Decimal(row["value"]) != int(value)):
                    raise ValueError("inexact memory byte count")
                if row["name"].startswith("SceneLoad.Memory.") and not memory:
                    raise ValueError("unknown memory units")
                if not memory and not row["name"].endswith("Ms"):
                    raise ValueError("unknown value units")
            yield row["kind"], row["name"], timestamp, value


def read_stages(path, window=None, allow_empty=False):
    groups = {}
    for kind, name, _, value in capture_records(path, window):
        # Scope values use nanoseconds; the declared Frame*Ms plots use ms.
        if kind == "scope_ns":
            value /= 1_000_000
        elif name.startswith("SceneLoad.Memory."):
            continue
        groups.setdefault(name, []).append(value)
    if not groups and not allow_empty:
        raise ValueError("empty stage capture")
    return {name: distribution(values) for name, values in sorted(groups.items())}


def read_memory(path, window=None):
    groups = {}
    for kind, name, _, value in capture_records(path, window):
        if kind == "value" and name.startswith("SceneLoad.Memory.") and name.endswith("Bytes"):
            groups.setdefault(name, []).append(int(value))
    return {name: {"count": len(values), "maximum_bytes": max(values)}
            for name, values in sorted(groups.items())}


def check_memory_limits(memory, scene_bytes, document_bytes):
    if scene_bytes <= 0 or document_bytes <= 0 or document_bytes > scene_bytes:
        raise ValueError("invalid memory limits")
    try:
        maximum = {name: memory["SceneLoad.Memory." + name]["maximum_bytes"] for name in (
            "SceneArenaCapacityBytes", "SceneArenaReservedBytes", "DocumentCapacityBytes",
            "DocumentAllocatedChargeBytes", "DocumentWorkingChargeBytes", "DocumentPeakChargeBytes")}
    except KeyError as error:
        raise ValueError("incomplete scene/document memory evidence") from error
    if not (0 < maximum["SceneArenaReservedBytes"] <= maximum["SceneArenaCapacityBytes"] <= scene_bytes):
        raise ValueError("scene arena exceeds its declared limit")
    if not (0 < maximum["DocumentCapacityBytes"] <= document_bytes):
        raise ValueError("document capacity exceeds its declared limit")
    if maximum["DocumentCapacityBytes"] > maximum["SceneArenaReservedBytes"]:
        raise ValueError("nested document backing exceeds its scene reservation")
    if not (max(maximum["DocumentAllocatedChargeBytes"], maximum["DocumentWorkingChargeBytes"]) <=
            maximum["DocumentPeakChargeBytes"] <= maximum["DocumentCapacityBytes"]):
        raise ValueError("document charge exceeds its capacity or recorded peak")
    return {"scene_storage_limit_bytes_per_slot": scene_bytes,
            "document_working_limit_bytes_per_slot": document_bytes,
            "note": "document/editor reservations are nested in scene backing, not additional RAM; RSS is separate"}


def check_timing_limits(report, stage_path, limits):
    """Enforce declared maxima; never infer an attributable frame-stall pass.

    A truncated two-minute trace must not qualify a longer run. Preparation is
    conservatively bounded in full, not subtracted from an unrelated steady max.
    """
    limits = {name: value for name, value in limits.items() if value is not None}
    if any(not math.isfinite(value) or value <= 0 for value in limits.values()):
        raise ValueError("timing limits must be finite and positive")
    if not limits:
        return {}
    if report["steady_rendering"] or report["reference_restoration"]:
        raise ValueError("load timing limits require replacement evidence, not steady/reference restoration")
    window = report.get("measured_window_ns")
    if not stage_path or not window:
        raise ValueError("timing limits require stage capture and measured windows")
    first, last = None, None
    for _, _, timestamp, _ in capture_records(stage_path):
        first = timestamp if first is None else min(first, timestamp)
        last = timestamp if last is None else max(last, timestamp)
    if first is None or first > window[0] or last < window[1]:
        raise ValueError("stage capture does not cover the complete measured window")
    stages = read_stages(stage_path, window)
    sources = {
        "load_ms": report["timings"]["request_to_first_submission_ms"],
        "main_loader_ms": stages.get("SceneLoad.Main.Process"),
        "commit_ms": stages.get("SceneLoad.Main.GuardedCommit"),
        "render_preparation_ms": stages.get("SceneLoad.Render.EnvironmentPreparation"),
    }
    for name, limit in limits.items():
        source = sources.get(name)
        if not source:
            raise ValueError(f"missing timing evidence for {name}")
        if source["max_ms"] > limit:
            raise ValueError(f"{name} maximum {source['max_ms']} ms exceeds declared {limit} ms")
    return limits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--cycles", type=int, required=True)
    parser.add_argument("--warmup", type=int, required=True)
    parser.add_argument("--zero-retention", action="store_true")
    parser.add_argument("--steady", action="store_true")
    parser.add_argument("--capacity-draws", type=int, default=0,
                        help="Require exact accepted draw counts and alternating over-cap refusal (modes 5/6)")
    parser.add_argument("--stages", type=Path)
    parser.add_argument("--scene-storage-limit-bytes", type=int)
    parser.add_argument("--document-limit-bytes", type=int)
    parser.add_argument("--load-limit-ms", type=float)
    parser.add_argument("--main-loader-limit-ms", type=float)
    parser.add_argument("--commit-limit-ms", type=float)
    parser.add_argument("--render-preparation-limit-ms", type=float,
                        help="Conservative whole-stage maximum; not matched-frame attribution")
    arguments = parser.parse_args()
    try:
        if arguments.zero_retention and arguments.steady:
            raise ValueError("steady and zero-retention are mutually exclusive")
        report = read_samples(arguments.samples, arguments.cycles, arguments.warmup, arguments.zero_retention, arguments.steady, arguments.capacity_draws)
        if arguments.stages:
            report["stage_capture"] = read_stages(arguments.stages)
            report["memory_high_water"] = read_memory(arguments.stages)
            if report["measured_window_ns"]:
                report["measured_stage_capture"] = read_stages(arguments.stages, report["measured_window_ns"], allow_empty=True)
                report["stage_capture_note"] = "bounded capture; an empty measured capture means no overlap, not a timing pass"
        if arguments.scene_storage_limit_bytes is not None or arguments.document_limit_bytes is not None:
            if not arguments.stages or arguments.scene_storage_limit_bytes is None or arguments.document_limit_bytes is None:
                raise ValueError("memory limits require stage capture and both declared bounds")
            report["memory_limits_checked"] = check_memory_limits(report["memory_high_water"], arguments.scene_storage_limit_bytes, arguments.document_limit_bytes)
        report["timing_limits_checked"] = check_timing_limits(report, arguments.stages, {
            "load_ms": arguments.load_limit_ms, "main_loader_ms": arguments.main_loader_limit_ms,
            "commit_ms": arguments.commit_limit_ms, "render_preparation_ms": arguments.render_preparation_limit_ms})
        report["qualification"] = "lifecycle evidence only; not full production acceptance"
        print(json.dumps(report, indent=2, allow_nan=False))
    except (ValueError, OSError) as error:
        parser.exit(1, f"invalid scene lifecycle evidence: {error}\n")


if __name__ == "__main__":
    main()
