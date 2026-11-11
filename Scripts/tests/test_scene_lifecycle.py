import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("lifecycle", Path(__file__).parents[1] / "validate_scene_lifecycle.py")
lifecycle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lifecycle)


class LifecycleEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "samples.csv"
        self.rows = []
        for index in range(12):
            row = {name: 1 for name in lifecycle.COUNTERS}
            row.update({name: 0 for name in lifecycle.DRAINED})
            row.update({name: 3.0 for name in lifecycle.TIMINGS})
            row.update(cycle=index, mode=index % 4, phase="warmup" if index < 8 else "measured", submitted_generation=index + 1, submitted_frames=8)
            self.rows.append(row)

    def write(self):
        with self.path.open("w", newline="", encoding="utf-8") as output:
            writer = csv.DictWriter(output, fieldnames=self.rows[0].keys())
            writer.writeheader()
            writer.writerows(self.rows)

    def validate(self):
        self.write()
        return lifecycle.read_samples(self.path, 4, 8, True)

    def test_complete_reference_evidence(self):
        report = self.validate()
        self.assertEqual(report["measured_cycles"], 4)
        self.assertEqual(report["reference_ledger"]["cpu_cache_live_bytes"], 1)

    def test_successful_load_percentiles_do_not_include_cancelled_cycles_or_warmup(self):
        for row in self.rows[:8]:
            row.update({name: 999 for name in lifecycle.TIMINGS})
        for row in self.rows[8:]:
            duration = 10 if row["mode"] in (0, 2) else 0
            row.update({name: duration for name in lifecycle.TIMINGS})
        self.write()
        report = lifecycle.read_samples(self.path, 4, 8)
        self.assertEqual(report["timings"]["request_to_first_submission_ms"]["p50_ms"], 0)
        scenarios = report["scenario_timings"]
        self.assertEqual(set(scenarios), {"replacement", "superseding_replacement",
                                         "cancel_before_dispatch", "cancel_after_dispatch"})
        self.assertEqual(sum(group["measured_cycles"] for group in scenarios.values()), 4)
        for name in ("replacement", "superseding_replacement"):
            self.assertEqual(scenarios[name]["measured_cycles"], 1)
            timing = scenarios[name]["timings"]["request_to_first_submission_ms"]
            self.assertEqual(timing["count"], 1)
            self.assertEqual(timing["p50_ms"], 10)
            self.assertEqual(timing["max_ms"], 10)

    def test_reference_restoration_is_not_reported_as_replacement_latency(self):
        scenarios = self.validate()["scenario_timings"]
        self.assertTrue(all(name.startswith("reference_restoration_after_") for name in scenarios))
        self.assertNotIn("replacement", scenarios)

    def test_missing_or_duplicate_cycle_is_rejected(self):
        self.rows.pop()
        with self.assertRaisesRegex(ValueError, "complete cycles"):
            self.validate()
        self.rows.append(dict(self.rows[-1]))
        with self.assertRaisesRegex(ValueError, "reordered"):
            self.validate()

    def test_all_live_ledgers_are_checked(self):
        for name in lifecycle.LEDGERS:
            with self.subTest(name=name):
                self.rows[-1][name] = 2
                with self.assertRaisesRegex(ValueError, "ledger changed"):
                    self.validate()
                self.rows[-1][name] = 1

    def test_all_pending_queues_must_be_drained(self):
        for name in lifecycle.DRAINED:
            with self.subTest(name=name):
                self.rows[-1][name] = 1
                with self.assertRaisesRegex(ValueError, "undrained"):
                    self.validate()
                self.rows[-1][name] = 0

    def test_warmup_can_differ_but_is_not_a_measured_baseline(self):
        self.rows[0]["cpu_cache_live_bytes"] = 999
        self.assertEqual(self.validate()["reference_ledger"]["cpu_cache_live_bytes"], 1)

    def test_nonfinite_negative_and_missing_timings_are_rejected(self):
        for value in ("nan", "inf", "-inf", -1, ""):
            with self.subTest(value=value):
                self.rows[-1]["max_host_update_ms"] = value
                with self.assertRaises(ValueError):
                    self.validate()

    def test_generation_and_settlement_are_checked(self):
        self.rows[-1]["submitted_generation"] = 0
        with self.assertRaisesRegex(ValueError, "generation"):
            self.validate()
        self.rows[-1]["submitted_generation"] = 12
        self.rows[-1]["submitted_frames"] = 7
        with self.assertRaisesRegex(ValueError, "submissions"):
            self.validate()

    def test_first_frame_cannot_follow_settlement(self):
        self.rows[-1]["request_to_first_submission_ms"] = 4
        with self.assertRaisesRegex(ValueError, "follows settlement"):
            self.validate()

    def test_header_only_is_not_a_pass(self):
        self.write()
        header = self.path.read_text(encoding="utf-8").splitlines()[0]
        self.path.write_text(header + "\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            lifecycle.read_samples(self.path, 4, 8, True)

    def test_quantiles_keep_every_outlier(self):
        report = lifecycle.distribution([1] * 99 + [279])
        self.assertEqual(report["p99_ms"], 1)
        self.assertEqual(report["max_ms"], 279)
        for values in ([], [float("nan")], [-1]):
            with self.assertRaises(ValueError):
                lifecycle.distribution(values)

    def test_stage_units_and_outliers(self):
        self.path.write_text('kind,name,timestamp_ns,value\nscope_ns,"SceneLoad.Main.Process",1,2000000\nvalue,"SceneLoad.Render.FrameWorkMs",2,279\n', encoding="utf-8")
        report = lifecycle.read_stages(self.path)
        self.assertEqual(report["SceneLoad.Main.Process"]["max_ms"], 2)
        self.assertEqual(report["SceneLoad.Render.FrameWorkMs"]["max_ms"], 279)

    def test_steady_windows_have_an_explicit_mode_and_do_not_imply_requests(self):
        for index, row in enumerate(self.rows):
            row["mode"] = 4 if index else 0
            row["submitted_generation"] = 1
            row["submitted_frames"] = (index + 1) * 8
            row["window_started_ns"] = index * 10
            row["sample_timestamp_ns"] = index * 10 + 9
        self.write()
        report = lifecycle.read_samples(self.path, 4, 8, steady=True)
        self.assertTrue(report["steady_rendering"])
        self.assertEqual(set(report["scenario_timings"]), {"steady_rendering"})
        self.assertEqual(report["scenario_timings"]["steady_rendering"]["measured_cycles"], 4)
        self.assertEqual(report["measured_window_ns"], [80, 119])
        self.rows[-1]["window_started_ns"] = 0
        self.write()
        with self.assertRaisesRegex(ValueError, "non-monotonic"):
            lifecycle.read_samples(self.path, 4, 8, steady=True)

    def test_steady_cannot_replay_frames_or_admit_an_unrecorded_replacement(self):
        for index, row in enumerate(self.rows):
            row["mode"] = 4 if index else 0
            row["submitted_generation"] = 1
            row["submitted_frames"] = (index + 1) * 8
        self.rows[-1]["submitted_frames"] = self.rows[-2]["submitted_frames"]
        self.write()
        with self.assertRaisesRegex(ValueError, "replayed"):
            lifecycle.read_samples(self.path, 4, 8, steady=True)
        self.rows[-1]["submitted_frames"] = 96
        self.rows[-1]["submitted_generation"] = 2
        self.write()
        with self.assertRaisesRegex(ValueError, "changed generation"):
            lifecycle.read_samples(self.path, 4, 8, steady=True)

    def test_predeclared_window_retains_all_outliers_inside_it(self):
        self.path.write_text('kind,name,timestamp_ns,value\nvalue,"SceneLoad.Render.FrameWorkMs",1,400\nvalue,"SceneLoad.Render.FrameWorkMs",2,279\nvalue,"SceneLoad.Render.FrameWorkMs",3,500\n', encoding="utf-8")
        report = lifecycle.read_stages(self.path, [2, 2])
        self.assertEqual(report["SceneLoad.Render.FrameWorkMs"]["max_ms"], 279)
        with self.assertRaisesRegex(ValueError, "empty stage"):
            lifecycle.read_stages(self.path, [4, 5])

    def test_invalid_stage_data_cannot_pass(self):
        for row in ('value,"SceneLoad.Foo",1,2', 'scope_ns,"SceneLoad.Main.Process",1,nan', 'value,"SceneLoad.Render.FrameWorkMs",-1,2', 'scope_ns,"Other",1,2'):
            with self.subTest(row=row):
                self.path.write_text("kind,name,timestamp_ns,value\n" + row + "\n", encoding="utf-8")
                with self.assertRaises(ValueError):
                    lifecycle.read_stages(self.path)

    def test_memory_counters_keep_exact_byte_units_and_do_not_pollute_timings(self):
        self.path.write_text('kind,name,timestamp_ns,value\nvalue,"SceneLoad.Memory.DocumentPeakChargeBytes",1,134217729\n'
                             'scope_ns,"SceneLoad.Main.Process",2,1000000\n', encoding="utf-8")
        self.assertEqual(lifecycle.read_memory(self.path)["SceneLoad.Memory.DocumentPeakChargeBytes"]["maximum_bytes"], 134217729)
        self.assertEqual(set(lifecycle.read_stages(self.path)), {"SceneLoad.Main.Process"})
        self.assertEqual(lifecycle.read_memory(self.path, [2, 3]), {})
        for value in ("0.5", "9007199254740993", "nan", "-1"):
            self.path.write_text('kind,name,timestamp_ns,value\nvalue,"SceneLoad.Memory.DocumentPeakChargeBytes",1,' + value + '\n', encoding="utf-8")
            with self.subTest(value=value), self.assertRaises(ValueError):
                lifecycle.read_memory(self.path)

    def test_declared_memory_limits_require_complete_bounded_evidence(self):
        maximum = {"SceneArenaCapacityBytes": 200, "SceneArenaReservedBytes": 180,
                   "DocumentCapacityBytes": 128, "DocumentAllocatedChargeBytes": 60,
                   "DocumentWorkingChargeBytes": 10, "DocumentPeakChargeBytes": 100}
        memory = {"SceneLoad.Memory." + name: {"maximum_bytes": value} for name, value in maximum.items()}
        self.assertEqual(lifecycle.check_memory_limits(memory, 200, 128)["scene_storage_limit_bytes_per_slot"], 200)
        for name, value in (("SceneArenaReservedBytes", 201), ("SceneArenaReservedBytes", 127), ("DocumentPeakChargeBytes", 129),
                            ("DocumentAllocatedChargeBytes", 101), ("DocumentWorkingChargeBytes", 101)):
            key = "SceneLoad.Memory." + name
            original = memory[key]["maximum_bytes"]
            memory[key]["maximum_bytes"] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                lifecycle.check_memory_limits(memory, 200, 128)
            memory[key]["maximum_bytes"] = original
        with self.assertRaises(ValueError):
            lifecycle.check_memory_limits(memory, 200, 127)
        memory.pop("SceneLoad.Memory.DocumentPeakChargeBytes")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            lifecycle.check_memory_limits(memory, 200, 128)

    def test_capacity_acceptance_and_refusal_preserve_exact_submitted_draws(self):
        generation = 1
        for index, row in enumerate(self.rows):
            row["mode"] = 6 if index % 4 == 1 else 5
            generation += row["mode"] != 6
            row["submitted_generation"] = generation
            row["accepted_draw_count"] = 8192
        self.write()
        report = lifecycle.read_samples(self.path, 4, 8, capacity_draws=8192)
        self.assertEqual(report["capacity_draws"], 8192)
        self.assertEqual(report["scenario_timings"]["capacity_accepted"]["measured_cycles"], 3)
        self.assertEqual(report["scenario_timings"]["capacity_refused"]["measured_cycles"], 1)
        self.rows[-1]["accepted_draw_count"] = 8191
        self.write()
        with self.assertRaisesRegex(ValueError, "capacity mismatch"):
            lifecycle.read_samples(self.path, 4, 8, capacity_draws=8192)
        self.rows[-1]["accepted_draw_count"] = 8192
        self.rows[9]["submitted_generation"] += 1
        self.write()
        with self.assertRaisesRegex(ValueError, "generation incorrectly"):
            lifecycle.read_samples(self.path, 4, 8, capacity_draws=8192)

    def timing_evidence(self):
        self.path.write_text('kind,name,timestamp_ns,value\n'
                             'scope_ns,"SceneLoad.Main.Process",1,1000000\n'
                             'scope_ns,"SceneLoad.Main.Process",10,4000000\n'
                             'scope_ns,"SceneLoad.Main.GuardedCommit",11,2000000\n'
                             'scope_ns,"SceneLoad.Render.EnvironmentPreparation",12,8000000\n'
                             'scope_ns,"SceneLoad.Main.Process",20,1000000\n', encoding="utf-8")
        return {"steady_rendering": False, "reference_restoration": False,
                "measured_window_ns": [5, 15],
                "timings": {"request_to_first_submission_ms": {"max_ms": 5000}}}

    def test_timing_limits_accept_exact_bounds_and_reject_every_excess(self):
        report = self.timing_evidence()
        limits = {"load_ms": 5000, "main_loader_ms": 4, "commit_ms": 2, "render_preparation_ms": 8}
        self.assertEqual(lifecycle.check_timing_limits(report, self.path, limits), limits)
        for name, value in limits.items():
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "exceeds declared"):
                lifecycle.check_timing_limits(report, self.path, {name: value - .001})

    def test_timing_limits_cannot_hide_missing_partial_or_wrong_mode_evidence(self):
        report = self.timing_evidence()
        limits = {"main_loader_ms": 4}
        with self.assertRaisesRegex(ValueError, "stage capture"):
            lifecycle.check_timing_limits(report, None, limits)
        for window in ([0, 15], [5, 21]):
            report["measured_window_ns"] = window
            with self.assertRaisesRegex(ValueError, "complete measured window"):
                lifecycle.check_timing_limits(report, self.path, limits)
        report["measured_window_ns"] = [5, 15]
        for name in ("steady_rendering", "reference_restoration"):
            report[name] = True
            with self.assertRaisesRegex(ValueError, "replacement evidence"):
                lifecycle.check_timing_limits(report, self.path, limits)
            report[name] = False
        self.path.write_text('kind,name,timestamp_ns,value\n'
                             'scope_ns,"SceneLoad.Main.Process",1,1\n'
                             'scope_ns,"SceneLoad.Main.Process",10,1\n'
                             'scope_ns,"SceneLoad.Main.Process",20,1\n', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "missing timing evidence"):
            lifecycle.check_timing_limits(report, self.path, {"commit_ms": 2})
        for value in (0, -1, float("nan"), float("inf")):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "finite and positive"):
                lifecycle.check_timing_limits(report, self.path, {"load_ms": value})

    def test_high_water_is_complete_monotonic_and_not_below_live(self):
        for row in self.rows:
            row.update({name: 10 for name in lifecycle.PEAKS})
        self.assertEqual(self.validate()["observed_high_water"]["gpu_peak_admitted_bytes"], 10)
        for value in (9, 0, -1, "nan"):
            self.rows[-1]["gpu_peak_admitted_bytes"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.validate()
        del self.rows[-1]["gpu_peak_admitted_bytes"]
        with self.assertRaises(ValueError):
            self.validate()


if __name__ == "__main__":
    unittest.main()
