import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from analyze_cooked_timing_samples import analyze


def sample(generation, elapsed):
    return {"generation": generation, "admission_ns": 100, "first_submission_ns": 100 + elapsed,
            "admission_to_submission_ns": elapsed, "render_completion_observed_ns": 200 + elapsed,
            "render_timeline_value": generation, "admission_to_completion_observed_ns": elapsed + 100}


class CookedTimingSamplesTest(unittest.TestCase):
    def setUp(self):
        common = {"manifest_sha256": "a" * 64, "binary_sha256": {"runtime_sha256": "b" * 64, "cooker_sha256": "c" * 64},
                  "host_system": "fixture", "host_machine": "test", "filesystem_cache_conditions": "uncontrolled", "source_audit_enabled": True, "gpu_completion_observed": True}
        viewport = {"scene_viewport": [1280, 720], "window": [1280, 720], "framebuffer": [2560, 1440], "resize_after_baseline": False}
        self.fresh = {**common, "fresh_process_samples": [sample(2, 1000), sample(2, 2000)], "validation_states": [True, True], "timing_viewports": [viewport, viewport]}
        self.replacements = {**common, "samples": [sample(2, 1000), sample(3, 3000), sample(4, 4000)],
                             "reloads": 2, "validation_states": [True], "timing_viewports": [viewport]}

    def test_keeps_fresh_initial_and_replacement_samples_separate(self):
        result = analyze(self.fresh, self.replacements, 2)
        self.assertEqual(result["fresh_process_loads"]["count"], 2)
        self.assertEqual(result["same_process_initial_load"]["count"], 1)
        self.assertEqual(result["same_process_replacements"]["max_ms"], 0.004)
        self.assertEqual(result["same_process_first_replacement"]["max_ms"], 0.003)
        self.assertEqual(result["same_process_later_replacements"]["max_ms"], 0.004)

    def test_rejects_different_package_validation_or_invalid_timing(self):
        other = copy.deepcopy(self.replacements)
        other["manifest_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "manifest"):
            analyze(self.fresh, other, 2)
        other = copy.deepcopy(self.replacements)
        other["validation_states"] = [False]
        with self.assertRaisesRegex(ValueError, "validation"):
            analyze(self.fresh, other, 2)
        other = copy.deepcopy(self.replacements)
        other["samples"][1]["admission_to_submission_ns"] = 0
        with self.assertRaisesRegex(ValueError, "invalid admission"):
            analyze(self.fresh, other, 2)
        other = copy.deepcopy(self.replacements)
        other["timing_viewports"][0]["resize_after_baseline"] = True
        with self.assertRaisesRegex(ValueError, "viewport"):
            analyze(self.fresh, other, 2)

    def test_rejects_mismatched_or_partial_recorded_device_identity(self):
        self.fresh["shader_cache"] = {"sha256": "a" * 64, "files": 44}
        with self.assertRaisesRegex(ValueError, "shader_cache"):
            analyze(self.fresh, self.replacements, 2)
        self.replacements["shader_cache"] = {"sha256": "b" * 64, "files": 44}
        with self.assertRaisesRegex(ValueError, "shader_cache"):
            analyze(self.fresh, self.replacements, 2)
        self.replacements["shader_cache"] = copy.deepcopy(self.fresh["shader_cache"])
        self.assertEqual(analyze(self.fresh, self.replacements, 2)["shader_cache"], self.fresh["shader_cache"])
        self.fresh["shader_cache"]["files"] = 0
        self.replacements["shader_cache"]["files"] = 0
        with self.assertRaisesRegex(ValueError, "invalid shader_cache"):
            analyze(self.fresh, self.replacements, 2)


if __name__ == "__main__":
    unittest.main()
