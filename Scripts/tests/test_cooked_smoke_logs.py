from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from cooked_scene_smoke import ALTERNATE_SCENE, SCENE, observed_timing_viewport, read_cancellation_timings, read_lifecycle_observations, read_process_logs, require_clean_stress_log, require_clean_vulkan_console, require_stable_stress_window, require_zero_source_audit, sha256_file, validate_retry_publications, validate_stress_sequence


class CookedSmokeLogTest(unittest.TestCase):
    def test_gpu_budget_observations_require_order_and_respect_domain_limits(self):
        log = ("[CookedLifecycle] generation=2 cpu_live=1 cpu_peak=1 staging_live=1 staging_peak=1 "
               "metadata_live=1 metadata_blocks=1 gpu_live=1 gpu_peak=1 retirement_queued=0 retirement_draining=0\n"
               "[CookedCache] generation=2 closures=0 leases=0 retiring=0 asset_versions=0\n"
               "[CookedTiming] generation=2 admission_ns=10 first_submission_ns=20 elapsed_ns=10\n")
        with self.assertRaisesRegex(RuntimeError, "GPU budget"):
            read_lifecycle_observations(log, [2], require_gpu_budget=True)
        budget = "[CookedGpuBudget] generation=2 admitted_peak=100 geometry_peak=30 texture_peak=20 target_peak=10 uniform_peak=5 staging_peak=4 readback_peak=1\n"
        retirement = "[CookedRetirementBudget] generation=2 capacity=20 high_water=10\n"
        sample = read_lifecycle_observations(log + budget + retirement, [2], require_gpu_budget=True)[0]
        self.assertEqual(sample["gpu_admitted_peak"], 100)
        self.assertEqual(sample["gpu_geometry_peak"], 30)
        self.assertEqual(sample["retirement_high_water"], 10)
        with self.assertRaisesRegex(RuntimeError, "exceeds"):
            read_lifecycle_observations(log + budget.replace("staging_peak=4", "staging_peak=536870913") + retirement, [2], require_gpu_budget=True)
        with self.assertRaisesRegex(RuntimeError, "retirement high-water"):
            read_lifecycle_observations(log + budget + retirement.replace("high_water=10", "high_water=21"), [2], require_gpu_budget=True)

    def test_cancellation_timings_require_order_boundaries_and_exact_durations(self):
        log = ("[CookedCancelTiming] cancelled=1 boundary=cpu request_ns=10 cleanup_ns=20 elapsed_ns=10\n"
               "[CookedCancelTiming] cancelled=2 boundary=gpu request_ns=30 cleanup_ns=45 elapsed_ns=15\n")
        self.assertEqual([sample["elapsed_ns"] for sample in read_cancellation_timings(log, 4)], [10, 15])
        for bad in (log.replace("boundary=gpu", "boundary=cpu"), log.replace("elapsed_ns=15", "elapsed_ns=14"), log.splitlines()[0]):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                read_cancellation_timings(bad, 4)

    def test_source_audit_requires_one_complete_zero_record(self):
        clean = "[info] [CookedSourceAudit] vfs_denied=0 registry_denied=0 imports_denied=0\n"
        require_zero_source_audit(clean)
        for bad in ("", clean + clean, clean.replace("imports_denied=0", "imports_denied=01"),
                    clean.replace("vfs_denied=0", "vfs_denied=2"), clean.rstrip() + "oops\n"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                require_zero_source_audit(bad)

    def test_failure_retry_requires_the_original_scene_identity(self):
        log = (f"[CookedRuntime] Published cooked scene generation 2 scene={SCENE}\n"
               f"[CookedRuntime] Published cooked scene generation 3 scene={SCENE}\n")
        validate_retry_publications(log)
        with self.assertRaisesRegex(ValueError, "unexpected scene or generation"):
            validate_retry_publications(log.replace(f"generation 3 scene={SCENE}", f"generation 3 scene={ALTERNATE_SCENE}"))
        with self.assertRaisesRegex(ValueError, "unexpected scene or generation"):
            validate_retry_publications(log + f"[CookedRuntime] Published cooked scene generation 3 scene={SCENE}\n")

    def test_executable_hash_covers_file_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "binary"
            path.write_bytes(b"first")
            original = sha256_file(path)
            path.write_bytes(b"second")
            self.assertNotEqual(original, sha256_file(path))

    def test_stress_rejects_stale_scene_and_wrong_cancel_generation(self):
        log = (f"[CookedRuntime] Published cooked scene generation 2 scene={SCENE}\n"
               "[CookedStress] cancelled=1 generation=2 rendered_after_cleanup=1\n"
               f"[CookedRuntime] Published cooked scene generation 3 scene={ALTERNATE_SCENE}\n"
               "[CookedStress] cancelled=2 generation=3 rendered_after_cleanup=1\n"
               f"[CookedRuntime] Published cooked scene generation 4 scene={SCENE}\n")
        validate_stress_sequence(log, 4)
        with self.assertRaisesRegex(ValueError, "unexpected scene"):
            validate_stress_sequence(log.replace(f"generation 3 scene={ALTERNATE_SCENE}", f"generation 3 scene={SCENE}"), 4)
        with self.assertRaisesRegex(ValueError, "expected scene generation"):
            validate_stress_sequence(log.replace("cancelled=2 generation=3", "cancelled=2 generation=2"), 4)

    def test_stress_rejects_resize_after_baseline_but_not_startup(self):
        header = "[ZUI] UIScale=2.00 Screen=1280x720 Window=1280x720 Framebuffer=2560x1440\n"
        dimensions = require_stable_stress_window("Window size updated\n" + header + "[CookedTiming] generation=2 admission_ns=1\n")
        self.assertEqual(dimensions["framebuffer"], [2560, 1440])
        with self.assertRaisesRegex(ValueError, "viewport changed"):
            require_stable_stress_window(header + "[CookedTiming] generation=2 admission_ns=1\nSwapchain recreated: 10x10\n")
        self.assertTrue(observed_timing_viewport(header + "[CookedTiming] generation=2 admission_ns=1\nWindow size updated\n")["resize_after_baseline"])
        self.assertIsNone(observed_timing_viewport("[CookedTiming] generation=2 admission_ns=1\n"))

    def test_stress_rejects_engine_errors_and_environment_fallback(self):
        require_clean_stress_log("[info] normal operation\n")
        with self.assertRaisesRegex(ValueError, "engine error"):
            require_clean_stress_log("[ERROR] resource failed\n")
        with self.assertRaisesRegex(ValueError, "environment fallback"):
            require_clean_stress_log("[warning] [SkyEnvironment] revision is using the fallback\n")

    def test_console_validation_errors_cannot_be_hidden_by_clean_engine_log(self):
        require_clean_vulkan_console("normal process output\n")
        with self.assertRaisesRegex(ValueError, "Vulkan console validation failure"):
            require_clean_vulkan_console("Vulkan validation error: missing driver library\n")
        with self.assertRaisesRegex(ValueError, "Vulkan console validation failure"):
            require_clean_vulkan_console("VUID-vkQueueSubmit-invalid\n")

    def test_gpu_completion_requires_matching_generation_and_timeline(self):
        log = ("[CookedLifecycle] generation=2 cpu_live=1 cpu_peak=1 staging_live=1 staging_peak=1 "
               "metadata_live=1 metadata_blocks=1 gpu_live=1 gpu_peak=1 retirement_queued=0 retirement_draining=0\n"
               "[CookedCache] generation=2 closures=0 leases=0 retiring=0 asset_versions=0\n"
               "[CookedTiming] generation=2 admission_ns=10 first_submission_ns=20 elapsed_ns=10\n")
        with self.assertRaisesRegex(RuntimeError, "missing render-completion"):
            read_lifecycle_observations(log, [2], True)
        valid = log + "[CookedGpuCompletion] generation=2 observed_ns=25 timeline_value=4\n"
        self.assertEqual(read_lifecycle_observations(valid, [2], True)[0]["admission_to_completion_observed_ns"], 15)
        with self.assertRaisesRegex(RuntimeError, "invalid render-completion"):
            read_lifecycle_observations(valid.replace("timeline_value=4", "timeline_value=0"), [2], True)
        with self.assertRaisesRegex(RuntimeError, "missing render-completion"):
            read_lifecycle_observations(valid.replace("generation=2 observed_ns", "generation=3 observed_ns"), [2], True)

    def test_editor_lifecycle_does_not_claim_standalone_cache_observations(self):
        log = ("[CookedLifecycle] generation=2 cpu_live=1 cpu_peak=1 staging_live=1 staging_peak=1 "
               "metadata_live=1 metadata_blocks=1 gpu_live=1 gpu_peak=1 retirement_queued=0 retirement_draining=0\n"
               "[CookedTiming] generation=2 admission_ns=10 first_submission_ns=20 elapsed_ns=10\n")
        with self.assertRaisesRegex(RuntimeError, "cache observations"):
            read_lifecycle_observations(log, [2])
        result = read_lifecycle_observations(log, [2], require_cache=False)
        self.assertNotIn("cache_closures", result[0])

    def test_reassembles_rotated_segments_in_chronological_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, content in (("engine_dump.2.log", "old\n"), ("engine_dump.1.log", "middle\n"), ("engine_dump.log", "new\n")):
                (root / name).write_text(content, encoding="utf-8")
            self.assertEqual(read_process_logs(root), "old\nmiddle\nnew\n")

    def test_rejects_missing_or_possibly_overwritten_segments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "engine_dump.log").write_text("current", encoding="utf-8")
            (root / "engine_dump.2.log").write_text("old", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing rotated segment"):
                read_process_logs(root)
            (root / "engine_dump.5.log").write_text("older", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "rotation limit"):
                read_process_logs(root)


if __name__ == "__main__":
    unittest.main()
