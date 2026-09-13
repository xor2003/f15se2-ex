import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from place_svn_ground import compile_grid
from f15assets.terrain import build_3dg, parse_3dg, build_3dt, parse_3dt


class GroundGridCapacityTests(unittest.TestCase):
    def test_small_and_extended_grids_preserve_world_positions(self):
        for count in (4, 40):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "VN").mkdir()
                grid = {"format": "3DG", "signature": 0x3232,
                        "level4_top_grid": [0] * 16, "level3_grid": [0] * 256,
                        **{f"level{lod}_subgrid": [0] * 512 for lod in range(3)}}
                tiles = {"format": "3DT", "levels": [
                    {"level": lod, "objects": [
                        {"tile_index": index, "objects": []} for index in range(32)
                    ]} for lod in range(5)
                ]}
                objects = [{"objectIdx": 38, "x_coord": index * 128 + index,
                            "y_coord": 32767} for index in range(count)]
                grid_path = root / "VN/VN.3DG.json"
                tiles_path = root / "VN/VN.3DT.json"
                grid_path.write_text(json.dumps(grid))
                tiles_path.write_text(json.dumps(tiles))
                (root / "SVN.WLD.json").write_text(json.dumps({"world_objects": objects}))
                compile_grid(root)
                grid = parse_3dg(build_3dg(json.loads(grid_path.read_text())))
                tiles = parse_3dt(build_3dt(json.loads(tiles_path.read_text())))
                self.assertEqual(grid["signature"], 0x3232 if count == 4 else 0x3247)
                self.assertEqual(len(grid["level1_subgrid"]), 512 if count == 4 else 4096)
                for column, source in enumerate(objects):
                    parent = grid["level3_grid"][column // 16]
                    parent = grid["level2_subgrid"][parent * 16 + (column // 4) % 4]
                    tile_id = grid["level1_subgrid"][parent * 16 + column % 4]
                    placements = tiles["levels"][1]["objects"][tile_id]["objects"]
                    self.assertEqual(len(placements), 1)
                    placement = placements[0]
                    self.assertEqual(column * 4096 + 2048 + placement["x"], source["x_coord"] * 32)
                    self.assertEqual(2048 + placement["y"], 32)
                    self.assertEqual(placement["shape_word"], 38 | 128)


if __name__ == "__main__":
    unittest.main()
