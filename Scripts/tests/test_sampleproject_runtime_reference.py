from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from sampleproject_cooked_runtime import add_sampleproject_hierarchy, compare_reviewed_reference, count_dark_pixels, force_opaque_materials, parse_presence, remove_sampleproject_directional_light, review_result, validate_control_baseline, validation_layer_status


class ReviewedReferenceTest(unittest.TestCase):
    def test_control_baseline_requires_matching_device_build_and_pixels(self):
        import hashlib
        baseline = (1, 1, b"abc")
        binaries, shaders, device = {"runtime": "a"}, {"sha256": "b"}, {"gpu": "c"}
        evidence = {"binary_sha256": binaries, "shader_cache": shaders, "device": device,
                    "source_pixel_sha256": hashlib.sha256(baseline[2]).hexdigest()}
        validate_control_baseline(evidence, baseline, binaries, shaders, device)
        with self.assertRaisesRegex(ValueError, "identity"):
            validate_control_baseline(evidence, baseline, binaries, shaders, {"gpu": "different"})
        with self.assertRaisesRegex(ValueError, "pixels"):
            validate_control_baseline(evidence, (1, 1, b"def"), binaries, shaders, device)
        with self.assertRaisesRegex(ValueError, "identity"):
            validate_control_baseline({**evidence, "device": None}, baseline, binaries, shaders, None)

    def test_directional_light_control_requires_one_known_intensity(self):
        scene = (Path(__file__).parents[2] / "SampleProject/Scenes/level01.zescene").read_bytes()
        result = remove_sampleproject_directional_light(scene)
        self.assertIn(b'"intensity": 0\n', result)
        self.assertNotIn(b'"intensity": 3\n', result)
        with self.assertRaisesRegex(ValueError, "directional light"):
            remove_sampleproject_directional_light(result)

    def test_opaque_control_only_changes_blend_materials(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            blend = root / "blend.zematerial"
            opaque = root / "opaque.zematerial"
            blend.write_text('{"alpha_mode": "BLEND", "value": 1}')
            opaque.write_text('{"alpha_mode": "OPAQUE", "value": 2}')
            unchanged = opaque.read_bytes()
            changed = force_opaque_materials(root)
            self.assertEqual(len(changed), 1)
            self.assertEqual(json.loads(blend.read_text()), {"alpha_mode": "OPAQUE", "value": 1})
            self.assertEqual(opaque.read_bytes(), unchanged)
            with self.assertRaisesRegex(ValueError, "no blend materials"):
                force_opaque_materials(root)

    def test_hierarchy_fixture_moves_translation_and_rejects_reapplication(self):
        scene = (Path(__file__).parents[2] / "SampleProject/Scenes/level01.zescene").read_bytes()
        result = add_sampleproject_hierarchy(scene)
        self.assertEqual(result.count(b'"parent_uuid": "beb33379-561e-4413-af36-b7d9f5a6be26"'), 1)
        self.assertEqual(result.count(b'"position": [0.40000004, 1.2, 0]'), 1)
        with self.assertRaisesRegex(ValueError, "hierarchy fixture"):
            add_sampleproject_hierarchy(result)

    def test_named_region_detects_small_change_hidden_by_viewport_tolerance(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reference.ppm"
            pixels = bytes(20 * 20 * 3)
            path.write_bytes(b"P6\n20 20\n255\n" + pixels)
            changed = bytearray(pixels)
            changed[0] = 255
            result = compare_reviewed_reference(path, (20, 20, bytes(changed)), [("important", (0, 0, 1, 1))])
            self.assertTrue(result["viewport"]["passed"])
            self.assertFalse(result["regions"]["important"]["passed"])

    def test_rejects_reference_with_different_dimensions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reference.ppm"
            path.write_bytes(b"P6\n1 1\n255\n" + bytes(3))
            with self.assertRaisesRegex(ValueError, "dimensions differ"):
                compare_reviewed_reference(path, (2, 1, bytes(6)), [])
            result = review_result(path, (2, 1, bytes(6)), [], "review-1")
            self.assertIn("dimensions differ", result["error"])
            self.assertEqual(result["review_identifier"], "review-1")

    def test_presence_counts_only_dark_pixels_in_declared_region(self):
        capture = (2, 2, bytes((0, 0, 0, 99, 99, 99, 100, 0, 0, 255, 255, 255)))
        self.assertEqual(count_dark_pixels(capture, (0, 0, 2, 1)), 2)
        self.assertEqual(count_dark_pixels(capture, (0, 1, 2, 1)), 0)
        self.assertEqual(parse_presence("character:1000"), ("character", 1000))
        with self.assertRaises(ValueError):
            count_dark_pixels(capture, (1, 1, 2, 1))

    def test_validation_status_requires_one_explicit_record(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "engine.log"
            path.write_text("[VulkanValidation] khronos_enabled=1\n", encoding="utf-8")
            self.assertTrue(validation_layer_status(path))
            path.write_text("[VulkanValidation] khronos_enabled=0\n", encoding="utf-8")
            self.assertFalse(validation_layer_status(path))
            path.write_text("unrelated log\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing or repeated"):
                validation_layer_status(path)
            path.write_text("[VulkanValidation] khronos_enabled=1x\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "malformed"):
                validation_layer_status(path)
            path.write_text("[VulkanValidation] khronos_enabled=1\n[VulkanValidation] khronos_enabled=0\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing or repeated"):
                validation_layer_status(path)


if __name__ == "__main__":
    unittest.main()
