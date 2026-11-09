import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("capture", Path(__file__).parents[1] / "compare_scene_capture.py")
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class SceneCaptureTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.path = Path(directory.name) / "frame.ppm"

    def test_raster_whitespace_bytes_are_not_stripped(self):
        pixels = b"\n\r\t # "
        self.path.write_bytes(b"P6\n# header comment\n2 1\n255\n" + pixels)
        self.assertEqual(capture.read_ppm(self.path), (2, 1, pixels))

    def test_crlf_separator(self):
        self.path.write_bytes(b"P6\r\n1 1\r\n255\r\n\n\r\t")
        self.assertEqual(capture.read_ppm(self.path), (1, 1, b"\n\r\t"))

    def test_malformed_or_wrong_space_capture_is_rejected(self):
        for data in (b"", b"P3\n1 1\n255\n123", b"P6\n0 1\n255\n", b"P6\n1 1\n65535\n123456", b"P6\n1 1\n255\n12", b"P6\n1 1\n255\n1234", b"P6\n# missing newline"):
            with self.subTest(data=data):
                self.path.write_bytes(data)
                with self.assertRaises(ValueError):
                    capture.read_ppm(self.path)

    def test_exact_contract_thresholds(self):
        reference = (200, 1, bytes(600))
        pixels = bytearray(600)
        pixels[0] = 4
        result = capture.compare_region(reference, (200, 1, pixels), (0, 0, 200, 1))
        self.assertTrue(result["passed"])
        self.assertEqual(result["changed_pixels"], 0)
        pixels[0] = 5
        self.assertTrue(capture.compare_region(reference, (200, 1, pixels), (0, 0, 200, 1))["passed"])
        pixels[3] = 5
        self.assertFalse(capture.compare_region(reference, (200, 1, pixels), (0, 0, 200, 1))["passed"])

    def test_every_color_channel_can_fail(self):
        for channel in range(3):
            pixels = bytearray(3)
            pixels[channel] = 255
            with self.subTest(channel=channel):
                self.assertFalse(capture.compare_region((1, 1, bytes(3)), (1, 1, pixels), (0, 0, 1, 1))["passed"])

    def test_object_region_detects_loss_below_whole_frame_tolerance(self):
        reference = (1000, 1, bytes(3000))
        pixels = bytearray(3000)
        pixels[300:306] = b"\xff" * 6
        candidate = (1000, 1, pixels)
        self.assertTrue(capture.compare_region(reference, candidate, (0, 0, 1000, 1))["passed"])
        self.assertFalse(capture.compare_region(reference, candidate, (100, 0, 2, 1))["passed"])

    def test_regions_are_explicit_and_do_not_rescale(self):
        image = (2, 2, bytes(12))
        for rectangle in ((-1, 0, 1, 1), (0, 0, 0, 1), (0, 0, 3, 2), (1, 1, 2, 2)):
            with self.subTest(rectangle=rectangle), self.assertRaises(ValueError):
                capture.compare_region(image, image, rectangle)
        with self.assertRaises(ValueError):
            capture.compare_region(image, (1, 4, bytes(12)), (0, 0, 1, 1))

    def test_capture_log_rejects_validation_failures_even_with_matching_pixels(self):
        for marker in ("[error]", "VUID-vkQueueSubmit", "VK_NOT_READY", "Validation Error"):
            with self.subTest(marker=marker):
                self.path.write_text(marker + "\nEngine destroyed\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "validation errors"):
                    capture.validate_capture_log(self.path)

    def test_capture_log_requires_complete_nonempty_clean_shutdown(self):
        for text in ("", "Engine initialized\n"):
            self.path.write_text(text, encoding="utf-8")
            with self.assertRaises(ValueError):
                capture.validate_capture_log(self.path)
        self.path.write_text("[info] Engine initialized\n[info] Engine destroyed\n", encoding="utf-8")
        capture.validate_capture_log(self.path)


if __name__ == "__main__":
    unittest.main()
