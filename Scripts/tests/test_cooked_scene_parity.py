import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from cooked_scene_parity import compare_transparency


class CookedSceneParityTest(unittest.TestCase):
    def test_transparency_requires_intermediate_pixel_over_opaque_triangle(self):
        opaque = (2, 1, bytes([200, 200, 200, 0, 0, 0]))
        blended = (2, 1, bytes([200, 200, 200, 100, 100, 100]))
        self.assertTrue(compare_transparency(opaque, blended)["passed"])
        self.assertFalse(compare_transparency(opaque, opaque)["passed"])
        self.assertFalse(compare_transparency(opaque, (2, 1, bytes([200] * 6)))["passed"])
        with self.assertRaisesRegex(ValueError, "dimensions"):
            compare_transparency(opaque, (1, 1, bytes([100] * 3)))


if __name__ == "__main__":
    unittest.main()
