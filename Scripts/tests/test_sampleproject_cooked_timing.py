import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from sampleproject_cooked_timing import ALTERNATE, CAMERA, SCENE, device_identity, memory_summary, require_visible_trace, scene_pair, shader_identity


class SampleProjectTimingTest(unittest.TestCase):
    def test_visible_trace_rejects_occluded_or_unknown_windows(self):
        visible = {"window_occlusion": {"visible_at_start": 1, "changes": []}}
        windows = {name: visible for name in ("initial_load", "steady", "replacement", "post_steady")}
        require_visible_trace({"windows": windows})
        with self.assertRaisesRegex(ValueError, "replacement"):
            require_visible_trace({"windows": {**windows, "replacement": {"window_occlusion": {"visible_at_start": 1, "changes": [{"visible": 0}]}}}})
        with self.assertRaisesRegex(ValueError, "steady"):
            require_visible_trace({"windows": {**windows, "steady": {"window_occlusion": None}}})
        with self.assertRaisesRegex(ValueError, "all four"):
            require_visible_trace({"windows": {}})

    def test_memory_summary_preserves_peak_and_sampled_scopes(self):
        sample = {"cpu_peak": 10, "staging_peak": 20, "gpu_peak": 30, "metadata_live": 4,
                  "metadata_blocks": 1, "retirement_queued": 2, "retirement_draining": 0}
        result = memory_summary([sample, {**sample, "cpu_peak": 15}])
        self.assertEqual(result["cpu_pool_peak_bytes"], 15)
        self.assertEqual(result["retirement_max_queued_at_observation"], 2)
        with self.assertRaisesRegex(ValueError, "requires"):
            memory_summary([])
        with self.assertRaisesRegex(ValueError, "invalid"):
            memory_summary([{**sample, "gpu_peak": -1}])

    def test_device_identity_requires_unique_device_and_driver(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            driver = Path(directory) / "driver.bin"
            driver.write_bytes(b"driver")
            log = f'Using "GPU" with driver: "{driver}"\nCreated VkDevice to run on GPU GPU with the following 2 extensions\n'
            self.assertEqual(device_identity(log)["gpu"], "GPU")
            self.assertIsNone(device_identity(log + log))
            self.assertIsNone(device_identity("no device information"))

    def test_memory_summary_requires_complete_budget_observations(self):
        sample = {"cpu_peak": 10, "staging_peak": 20, "gpu_peak": 30, "metadata_live": 4,
                  "metadata_blocks": 1, "retirement_queued": 2, "retirement_draining": 0,
                  "gpu_admitted_peak": 100, "gpu_geometry_peak": 20, "gpu_texture_peak": 30,
                  "gpu_target_peak": 10, "gpu_uniform_peak": 5, "gpu_staging_peak": 15,
                  "gpu_readback_peak": 0, "retirement_high_water": 4, "retirement_capacity": 16}
        result = memory_summary([sample, {**sample, "gpu_admitted_peak": 120, "retirement_high_water": 7}])
        self.assertEqual(result["gpu_admitted_peak_bytes"], 120)
        self.assertEqual(result["gpu_texture_peak_bytes"], 30)
        self.assertEqual(result["retirement_high_water"], 7)
        self.assertEqual(result["retirement_capacity"], 16)
        with self.assertRaisesRegex(ValueError, "partial GPU"):
            memory_summary([sample, {k: v for k, v in sample.items() if k != "gpu_admitted_peak"}])
        with self.assertRaisesRegex(ValueError, "capacity changed"):
            memory_summary([sample, {**sample, "retirement_capacity": 32}])

    def test_shader_identity_tracks_names_and_contents(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "missing"):
                shader_identity(root)
            path = root / "fixture.spv"
            path.write_bytes(b"first")
            original = shader_identity(root)
            path.write_bytes(b"second")
            changed = shader_identity(root)
            self.assertNotEqual(changed, original)
            path.rename(root / "renamed.spv")
            self.assertNotEqual(shader_identity(root), changed)

    def test_pair_preserves_source_and_adds_unique_scene_identity_and_camera(self):
        original = ('"scene":\n  "uuid": "' + SCENE + '"\n').encode()
        initial, alternate = scene_pair(original)
        self.assertTrue(initial.startswith(original))
        self.assertIn(CAMERA.encode(), initial)
        self.assertIn(ALTERNATE.encode(), alternate)
        self.assertNotIn(SCENE.encode(), alternate)
        self.assertEqual(initial.count(CAMERA.encode()), 1)
        with self.assertRaises(ValueError):
            scene_pair(initial)
        with self.assertRaises(ValueError):
            scene_pair(original + original)


if __name__ == "__main__":
    unittest.main()
