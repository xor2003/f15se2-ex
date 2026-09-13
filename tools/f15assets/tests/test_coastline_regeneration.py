"""Coastline generation must preserve unrelated assets and support reruns."""

import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


GENERATOR = Path(__file__).resolve().parents[1] / "build_coast_tiles.py"


class CoastlineRegenerationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.container = self.root / "TEST"
        self.write_json("land.json", {
            "type": "FeatureCollection",
            "features": [{"type": "Feature", "properties": {}, "geometry": {
                "type": "Polygon",
                "coordinates": [[[0, 0], [0.5, 0], [0.5, 1], [0, 1], [0, 0]]],
            }}],
        })
        self.write_json("TEST.3D3.json", {
            "shape_offsets": [0] * 116,
            "shape_names": {},
            "model_data": base64.b64encode(b"\0").decode(),
            "model_data_size": 1,
        })
        self.write_json("TEST.3DG.json", {"level4_top_grid": [0] * 16})
        self.tiles = {"levels": [{"level": 4, "objects": [
            {"tile_index": index, "objects": []} for index in range(1, 17)
        ]}]}
        self.write_json("TEST.3DT.json", self.tiles)

    def write_json(self, name, document):
        (self.root / name).write_text(json.dumps(document))

    def generate(self, *options):
        return subprocess.run([
            sys.executable, str(GENERATOR), str(self.root / "land.json"),
            str(self.container), "--first-shape", "100", "--corners",
            "0", "1", "1", "1", "0", "0", "1", "0", *options,
        ], capture_output=True, text=True, timeout=30)

    def snapshot(self):
        return {str(path.relative_to(self.root)): path.read_bytes()
                for path in self.root.rglob("*") if path.is_file()}

    def test_regeneration_is_byte_identical(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        original = self.snapshot()
        self.assertEqual(len(list(self.root.glob("*.glb"))), 16)
        self.assertEqual(len(list((self.root / "cache").glob("*.glmesh"))), 16)
        result = self.generate("--replace-generated")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.snapshot(), original)

    def test_occupied_last_tile_leaves_every_file_unchanged(self):
        self.tiles["levels"][0]["objects"][-1]["objects"] = [
            {"x": 0, "y": 0, "z": 0, "shape_word": 21}
        ]
        self.write_json("TEST.3DT.json", self.tiles)
        original = self.snapshot()
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("LOD4 tile 16 is occupied", result.stderr)
        self.assertEqual(self.snapshot(), original)

    def test_replacement_rejects_modified_tile_without_writes(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / "TEST.3DT.json"
        tiles = json.loads(path.read_text())
        tiles["levels"][0]["objects"][-1]["objects"][0]["x"] = 1
        self.write_json(path.name, tiles)
        original = self.snapshot()
        result = self.generate("--replace-generated")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match generated coastline", result.stderr)
        self.assertEqual(self.snapshot(), original)


if __name__ == "__main__":
    unittest.main()
