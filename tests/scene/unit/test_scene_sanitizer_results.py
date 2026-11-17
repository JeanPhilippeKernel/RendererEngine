from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).parents[1]))
from verify_scene_sanitizer_results import REQUIRED, verify


class SceneSanitizerResultsTest(unittest.TestCase):
    def test_requires_every_suite_and_rejects_skipped_cases(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.xml"
            root = ET.Element("testsuites")
            for name in sorted(REQUIRED):
                suite = ET.SubElement(root, "testsuite", name=name, tests="1", skipped="0", failures="0", errors="0", disabled="0")
                ET.SubElement(suite, "testcase", name="case", status="run")
            ET.ElementTree(root).write(path)
            self.assertEqual(verify(path), len(REQUIRED))
            root[0][0].set("status", "notrun")
            ET.ElementTree(root).write(path)
            with self.assertRaisesRegex(ValueError, "did not run"):
                verify(path)
            root.remove(root[-1])
            ET.ElementTree(root).write(path)
            with self.assertRaisesRegex(ValueError, "missing"):
                verify(path)


if __name__ == "__main__":
    unittest.main()
