import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from analyze_cooked_runtime_trace import analyze, render_frame_diagnostics, render_outliers, window_occlusion


class RuntimeTraceTest(unittest.TestCase):
    def test_occlusion_carries_prior_state_and_records_transitions(self):
        records = [("value", "SceneLoad.Main.WindowOcclusionVisible", 10, 1),
                   ("value", "SceneLoad.Main.WindowOcclusionVisible", 30, 0),
                   ("value", "SceneLoad.Main.WindowOcclusionVisible", 50, 1)]
        result = window_occlusion(records, (20, 40))
        self.assertEqual(result["visible_at_start"], 1)
        self.assertEqual(result["changes"], [{"timestamp_ns": 30, "visible": 0}])
        self.assertIsNone(window_occlusion(records, (0, 5)))
        with self.assertRaisesRegex(ValueError, "invalid window"):
            window_occlusion(records + [("value", "SceneLoad.Main.WindowOcclusionVisible", 35, 3)], (20, 40))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.trace = Path(self.temp.name) / "trace.csv"
        self.evidence = {"manifest_sha256": "fixture", "filesystem_cache_conditions": "uncontrolled",
                         "samples": [{"generation": 2, "admission_ns": 100, "first_submission_ns": 200,
                                      "steady_start_ns": 210, "steady_end_ns": 300, "steady_submitted_frames": 2},
                                     {"generation": 3, "admission_ns": 310, "first_submission_ns": 400}]}
        self.rows = []
        for timestamp in (110, 220, 280, 320, 390, 410):
            for name in ("SceneLoad.Main.FrameWorkMs", "SceneLoad.Main.FrameIntervalMs", "SceneLoad.Render.FrameIntervalMs"):
                self.rows.append({"kind": "value", "name": name, "timestamp_ns": timestamp, "value": 1})
        self.write()

    def write(self):
        with self.trace.open("w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(output, fieldnames=("kind", "name", "timestamp_ns", "value"))
            writer.writeheader()
            writer.writerows(self.rows)

    def test_reports_partial_initial_coverage(self):
        report = analyze(self.evidence, self.trace, 2)
        self.assertEqual(report["initial_trace_starts_after_admission_ns"], 10)
        self.assertFalse(report["initial_admission_marker_observed"])
        self.assertEqual(report["windows"]["steady"]["stages"]["SceneLoad.Render.FrameIntervalMs"]["count"], 2)

    def test_post_replacement_window_requires_complete_ordered_evidence(self):
        self.evidence["samples"][1].update(post_steady_start_ns=405, post_steady_end_ns=500, post_steady_submitted_frames=2)
        for timestamp in (450, 480, 510):
            for name in ("SceneLoad.Main.FrameWorkMs", "SceneLoad.Main.FrameIntervalMs", "SceneLoad.Render.FrameIntervalMs"):
                self.rows.append({"kind": "value", "name": name, "timestamp_ns": timestamp, "value": 1})
        self.write()
        self.assertIn("post_steady", analyze(self.evidence, self.trace, 2)["windows"])
        self.evidence["samples"][1]["post_steady_start_ns"] = 399
        with self.assertRaisesRegex(ValueError, "invalid post-replacement"):
            analyze(self.evidence, self.trace, 2)

    def test_detects_a_pre_admission_trace_marker(self):
        self.rows.append({"kind": "scope_ns", "name": "SceneLoad.Main.BeforeInitialAdmission", "timestamp_ns": 99, "value": 1})
        self.write()
        report = analyze(self.evidence, self.trace, 2)
        self.assertTrue(report["initial_admission_marker_observed"])
        self.assertEqual(report["initial_trace_starts_after_admission_ns"], 0)

    def test_reports_queue_submission_in_each_window(self):
        for timestamp, duration in ((150, 20), (250, 10), (350, 30)):
            self.rows.append({"kind": "scope_ns", "name": "SceneLoad.Render.QueueSubmit",
                              "timestamp_ns": timestamp, "value": duration})
        self.write()
        windows = analyze(self.evidence, self.trace, 2)["windows"]
        for name, duration in (("initial_load", 20), ("steady", 10), ("replacement", 30)):
            self.assertAlmostEqual(windows[name]["stages"]["SceneLoad.Render.QueueSubmit"]["max_ms"], duration / 1_000_000)

    def test_rejects_overlapping_windows_and_incomplete_trace(self):
        self.evidence["samples"][0]["steady_end_ns"] = 311
        with self.assertRaisesRegex(ValueError, "overlapping"):
            analyze(self.evidence, self.trace, 2)
        self.evidence["samples"][0]["steady_end_ns"] = 300
        self.rows = [row for row in self.rows if row["timestamp_ns"] < 400]
        self.write()
        with self.assertRaisesRegex(ValueError, "does not cover"):
            analyze(self.evidence, self.trace, 2)

    def test_reports_contained_scopes_and_rejects_partial_attribution(self):
        self.rows.extend([
            {"kind": "scope_ns", "name": "SceneLoad.Main.RuntimeUpdate", "timestamp_ns": 270, "value": 10},
            {"kind": "scope_ns", "name": "SceneLoad.Main.RuntimeUpdate", "timestamp_ns": 276, "value": 4},
            {"kind": "scope_ns", "name": "SceneLoad.Main.RuntimeGuardedCommit", "timestamp_ns": 268, "value": 3},
            {"kind": "scope_ns", "name": "SceneLoad.Main.RuntimeUpdate", "timestamp_ns": 290, "value": 5},
        ])
        # Synthetic frames must not overlap across the window boundaries.
        for row in self.rows:
            if row["name"] == "SceneLoad.Main.FrameWorkMs":
                row["value"] = 0.00005 if row["timestamp_ns"] in (220, 280) else 0.00001
        self.write()
        diagnostics = analyze(self.evidence, self.trace, 2)["windows"]["steady"]["main_frame_diagnostics"]
        self.assertEqual(diagnostics["runtime_update_ms"]["count"], 1)
        self.assertAlmostEqual(diagnostics["runtime_update_ms"]["max_ms"], 0.000014)
        self.assertEqual(diagnostics["runtime_update_ms"]["worst_frame_end_ns"], 280)
        self.assertEqual(diagnostics["guarded_commit_ms"]["count"], 1)
        self.assertEqual(diagnostics["partial_scopes"], 1)
        self.assertEqual(diagnostics["unassigned_scopes"][0]["end_ns"], 290)
        self.assertAlmostEqual(diagnostics["unassigned_scopes"][0]["duration_ms"], 0.000005)

    def test_render_repeated_visits_are_summed_per_frame(self):
        records = [("value", "SceneLoad.Render.FrameWorkMs", 100, 0.00005),
                   ("scope_ns", "SceneLoad.Render.EnvironmentPreparation", 80, 10),
                   ("scope_ns", "SceneLoad.Render.EnvironmentPreparation", 95, 5),
                   ("scope_ns", "SceneLoad.Render.CookedUploadVisit", 90, 3)]
        result = render_frame_diagnostics(records, (50, 100))
        self.assertAlmostEqual(result["environment_preparation_ms"]["max_ms"], 0.000015)
        self.assertEqual(result["environment_preparation_ms"]["count"], 1)
        crossing = render_frame_diagnostics(records, (50, 96))
        self.assertEqual(crossing["partial_scopes"], 0)
        self.assertAlmostEqual(crossing["environment_preparation_ms"]["max_ms"], 0.000015)
        self.assertAlmostEqual(result["cooked_upload_ms"]["max_ms"], 0.000003)

    def test_render_outliers_report_overlaps_without_claiming_causation(self):
        records = [("value", "SceneLoad.Render.FrameIntervalMs", 20_000_000, 12),
                   ("scope_ns", "SceneLoad.Render.EnvironmentPreparation", 19_000_000, 2_000_000),
                   ("scope_ns", "SceneLoad.Main.PlatformEvents", 18_000_000, 3_000_000),
                   ("value", "SceneLoad.RecordingWorker.DispatchDelayMs", 17_000_000, 6),
                   ("value", "SceneLoad.QueueSubmit.LongNonCpuMs", 19_500_000, 9),
                   ("value", "SceneLoad.Main.WindowOcclusionVisible", 7_000_000, 1),
                   ("value", "SceneLoad.Main.WindowOcclusionVisible", 16_000_000, 0),
                   ("scope_ns", "SceneLoad.Render.CookedUploadVisit", 30_000_000, 1_000_000)]
        report = render_outliers(records, (1, 25_000_000), steady_p95_ms=3)
        self.assertEqual(report["count"], 1)
        self.assertEqual(report["largest_intervals"][0]["longest_overlapping_scopes"][0]["name"], "SceneLoad.Render.EnvironmentPreparation")
        self.assertEqual(len(report["largest_intervals"][0]["longest_overlapping_scopes"]), 1)
        self.assertEqual(report["largest_intervals"][0]["overlapping_platform_events"][0]["overlap_ms"], 3)
        self.assertEqual(report["largest_intervals"][0]["recording_worker_samples"][0]["value_ms"], 6)
        self.assertEqual(report["largest_intervals"][0]["queue_submit_samples"][0]["value_ms"], 9)
        self.assertEqual(report["largest_intervals"][0]["window_occlusion"]["visible_at_start"], 1)
        self.assertEqual(report["largest_intervals"][0]["window_occlusion"]["changes"][0]["visible"], 0)


if __name__ == "__main__":
    unittest.main()
