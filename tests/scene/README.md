# Scene runtime tools

This directory contains the scene-runtime smoke, parity, capacity and timing
tools, plus the validators shared by their reports. Paths in the CLI examples
below are relative to the repository root.

- `cooked_scene_smoke.py` exercises standalone and editor-hosted cooked scenes,
  replacement, cancellation and injected failures.
- `cooked_scene_parity.py` and `sampleproject_cooked_parity.py` compare source
  and cooked scene behavior. `sampleproject_cooked_runtime.py` captures and
  compares the rendered SampleProject scene.
- `cooked_capacity_probe.py` creates synthetic capacity fixtures.
  `sampleproject_cooked_timing.py` collects timing observations.
- `analyze_cooked_runtime_trace.py` and `analyze_cooked_timing_samples.py`
  analyze those observations. `compare_scene_capture.py`,
  `validate_scene_lifecycle.py`, `verify_scene_sanitizer_results.py` and
  `cooked_capture_identity.py` provide shared validation and provenance checks.

Run a tool with `python3 tests/scene/<tool>.py --help`. Run the Python checks
with `python3 -m unittest discover -s tests/scene/unit -v`.

Raw M2 observations and their limitations are indexed in
[`scene-runtime-evidence`](evidence/README.md).
