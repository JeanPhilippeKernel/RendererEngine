from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
from cooked_capacity_probe import ALTERNATE_MATERIAL, ALTERNATE_SCENE, MATERIAL, MAX_DRAWS, SCENE, append_instances, make_material_variants, make_texture_variants, summarize_initial_trace, validate_publications, validate_unique_material_manifest


class CookedCapacityFixtureTest(unittest.TestCase):
    def test_texture_variants_preserve_layout_and_have_distinct_pixels_and_ids(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pixels = bytearray(58)
            pixels[:2] = b"BM"
            pixels[10:14] = (54).to_bytes(4, "little")
            pixels[54:57] = b"\x0a\x14\x1e"
            (root / "pixel.bmp").write_bytes(pixels)
            (root / "pixel.bmp.meta").write_text(json.dumps({"uuid": "50000000-0000-0000-0000-000000000005"}))
            identities = make_texture_variants(root, 3)
            self.assertEqual(len({identity for identity, _ in identities}), 3)
            images = [(root / Path(path).name).read_bytes() for _, path in identities]
            self.assertEqual(len(set(images)), 3)
            self.assertTrue(all(image[:54] == images[0][:54] for image in images))

    def test_material_variants_have_distinct_identities_and_scene_references(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "material.zematerial").write_text(json.dumps({"uuid": MATERIAL, "name": "fixture"}))
            (root / "material.zematerial.meta").write_text(json.dumps({"uuid": MATERIAL, "importer": "MaterialImporter"}))
            identities = make_material_variants(root, 3)
            self.assertEqual(len(set(identities)), 3)
            self.assertEqual(json.loads((root / "capacity-material-00001.zematerial").read_text())["uuid"], identities[1])
            scene = root / "scene"
            scene.write_text('"entities":\n')
            append_instances(scene, 4, identities)
            self.assertIn(identities[1], scene.read_text())
            self.assertIn(identities[2], scene.read_text())

    def test_distinct_material_roots_and_objects_are_required(self):
        manifest = {"scenes": [{"uuid": SCENE, "roots": [{"kind": "material", "uuid": MATERIAL}]},
                               {"uuid": ALTERNATE_SCENE, "roots": [{"kind": "material", "uuid": ALTERNATE_MATERIAL}]}],
                    "assets": [{"kind": "material", "uuid": MATERIAL, "object": {"digest": "a"}},
                               {"kind": "material", "uuid": ALTERNATE_MATERIAL, "object": {"digest": "b"}}]}
        validate_unique_material_manifest(manifest)
        manifest["assets"][1]["object"]["digest"] = "a"
        with self.assertRaisesRegex(ValueError, "distinct cooked objects"):
            validate_unique_material_manifest(manifest)

    def test_trace_reports_partial_admission_coverage_and_rejects_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "stage.csv"
            trace.write_text("kind,name,timestamp_ns,value\nvalue,SceneLoad.Main.FrameWorkMs,110,0.00001\nscope_ns,SceneLoad.Main.RuntimeUpdate,109,3\nvalue,SceneLoad.Main.FrameWorkMs,210,0.00001\n", encoding="utf-8")
            sample = {"admission_ns": 100, "first_submission_ns": 200}
            report = summarize_initial_trace(trace, sample)
            self.assertEqual(report["starts_after_admission_ns"], 9)
            self.assertFalse(report["admission_marker_observed"])
            self.assertEqual(report["stages"]["SceneLoad.Main.FrameWorkMs"]["count"], 1)
            self.assertAlmostEqual(report["main_frame_diagnostics"]["runtime_update_ms"]["max_ms"], 0.000003)
            with self.assertRaisesRegex(ValueError, "does not cover"):
                summarize_initial_trace(trace, {"admission_ns": 100, "first_submission_ns": 220})
            with trace.open("a", encoding="utf-8") as output:
                output.write("scope_ns,SceneLoad.Main.BeforeInitialAdmission,99,1\n")
            self.assertTrue(summarize_initial_trace(trace, sample)["admission_marker_observed"])

    def test_refusal_rejects_partial_or_stale_publications_and_fallback(self):
        initial = f"[CookedRuntime] Published cooked scene generation 2 scene={SCENE}\n"
        retry = f"[CookedRuntime] Published cooked scene generation 3 scene={SCENE}\n"
        self.assertEqual(validate_publications(initial + retry, False, True), [2, 3])
        self.assertEqual(validate_publications(initial + retry.replace(SCENE, ALTERNATE_SCENE), False, False, True), [2, 3])
        with self.assertRaisesRegex(ValueError, "unexpected scene"):
            validate_publications(initial + retry, False, False, True)
        with self.assertRaisesRegex(ValueError, "unexpected scene"):
            validate_publications(initial + retry.replace(SCENE, "90000000-0000-0000-0000-000000000009"), False, True)
        with self.assertRaisesRegex(ValueError, "unexpected scene"):
            validate_publications(initial, True, False)
        with self.assertRaisesRegex(ValueError, "environment fallback"):
            validate_publications(initial + "[SkyEnvironment] Revision 4 is using the fallback\n" + retry, False, True)

    def test_appends_distinct_instances_without_changing_original_scene(self):
        with tempfile.TemporaryDirectory() as directory:
            scene = Path(directory) / "fixture.scene"
            original = b'"entities":\n  - "uuid": "original"\n'
            scene.write_bytes(original)
            append_instances(scene, 4)
            data = scene.read_bytes()
            self.assertTrue(data.startswith(original))
            identities = re.findall(rb'"uuid": "(d[0-9a-f-]+)"', data)
            self.assertEqual(len(identities), 3)
            self.assertEqual(len(set(identities)), 3)
            self.assertEqual(data.count(b'"MeshComponent"'), 3)

    def test_rejects_out_of_range_and_incomplete_scene_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            scene = Path(directory) / "fixture.scene"
            scene.write_bytes(b"incomplete")
            for count in (0, MAX_DRAWS + 2, 1):
                with self.assertRaises(ValueError):
                    append_instances(scene, count)
                self.assertEqual(scene.read_bytes(), b"incomplete")


if __name__ == "__main__":
    unittest.main()
