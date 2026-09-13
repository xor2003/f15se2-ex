"""Compile SVN ground-object positions into the original hierarchical tile grid."""

import argparse
import base64
import json
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2] / "converted_assets_all" / "SVN"
TILE_TYPES = 256
LEGACY_TILE_TYPES = 32
CHILDREN = 16
WORLD_MAP_SIZE = 32768
FINE_UNITS_PER_MAP_UNIT = 32
TILE_FINE_SIZE = 4096


def object_key(objects):
    return tuple((obj["x"], obj["y"], obj["z"], obj["shape_word"]) for obj in objects)


def compile_grid(root=ROOT, container="VN", world_name="SVN.WLD.json"):
    grid_path = root / container / f"{container}.3DG.json"
    tiles_path = root / container / f"{container}.3DT.json"
    grid = json.loads(grid_path.read_text())
    tiles = json.loads(tiles_path.read_text())
    world = json.loads((root / world_name).read_text())
    mission_regions = bytearray(base64.b64decode(world["terrain_grid"]))
    model_categories = base64.b64decode(world["mission_object_type_table"])
    placement_path = root / container / "placement.json"
    model_lods = (json.loads(placement_path.read_text())["model_lods"]
                  if placement_path.exists() else {})
    levels = {level["level"]: level for level in tiles["levels"]}
    old_terrain = {tile["tile_index"]: tile["objects"]
                   for tile in levels[3]["objects"]}
    objects_by_level = {1: defaultdict(list), 2: defaultdict(list)}
    for obj in world["world_objects"]:
        if obj["objectIdx"] == 0:
            continue
        model = obj["objectIdx"] & 127
        if model_categories[model]:
            # The original mission search samples only regions with low bits zero.
            region = (obj["y_coord"] // 2048) * 16 + obj["x_coord"] // 2048
            mission_regions[region] &= ~3
        lod = model_lods.get(str(model), 1)
        if lod not in objects_by_level:
            raise ValueError(f"Model {model}: ground placement LOD must be 1 or 2")
        # Each higher LOD uses four times larger world units.
        fine_units_per_model_unit = 4 ** (lod - 1)
        x = obj["x_coord"] * FINE_UNITS_PER_MAP_UNIT
        y = ((WORLD_MAP_SIZE - obj["y_coord"]) % WORLD_MAP_SIZE) * FINE_UNITS_PER_MAP_UNIT
        x //= fine_units_per_model_unit
        y //= fine_units_per_model_unit
        cell = (x // TILE_FINE_SIZE, y // TILE_FINE_SIZE)
        objects_by_level[lod][cell].append({
            "x": x % TILE_FINE_SIZE - TILE_FINE_SIZE // 2,
            "y": y % TILE_FINE_SIZE - TILE_FINE_SIZE // 2,
            "z": 0, "shape_word": model | 128,
        })

    def intern(table, key):
        if key not in table:
            if len(table) >= TILE_TYPES:
                raise ValueError(f"Ground placement exceeds {TILE_TYPES} tile patterns at one LOD")
            table[key] = len(table)
        return table[key]

    empty_children = (0,) * CHILDREN
    patterns = {1: {((), empty_children): 0},
                2: {((), empty_children): 0}, 3: {((), empty_children): 0}}
    cells = {}
    for cell, objects in objects_by_level[1].items():
        cells[cell] = intern(patterns[1], (object_key(objects), empty_children))
    for level in (2, 3):
        parents = defaultdict(lambda: [0] * CHILDREN)
        for (column, row), tile_id in cells.items():
            parents[column // 4, row // 4][column % 4 + (row % 4) * 4] = tile_id
        if level == 2:
            for cell in objects_by_level[2]:
                parents[cell]
        if level == 3:
            for row in range(16):
                for column in range(16):
                    parents[column, row]
        cells = {}
        for (column, row), children in parents.items():
            terrain = (old_terrain[grid["level3_grid"][row * 16 + column]] if level == 3
                       else objects_by_level[2].get((column, row), []))
            cells[column, row] = intern(patterns[level], (object_key(terrain), tuple(children)))

    grid["level3_grid"] = [cells[column, row] for row in range(16) for column in range(16)]
    storage_patterns = (LEGACY_TILE_TYPES if max(map(len, patterns.values())) <= LEGACY_TILE_TYPES
                        else TILE_TYPES)
    grid["signature"] = 0x3232 if storage_patterns == LEGACY_TILE_TYPES else 0x3247
    grid["level0_subgrid"] = [0] * (storage_patterns * CHILDREN)
    for level in (1, 2, 3):
        definitions = [{"tile_index": index, "objects": []} for index in range(storage_patterns)]
        child_grid = [0] * (storage_patterns * CHILDREN)
        for (objects, children), index in patterns[level].items():
            definitions[index]["objects"] = [dict(zip(("x", "y", "z", "shape_word"), obj))
                                               for obj in objects]
            child_grid[index * CHILDREN:(index + 1) * CHILDREN] = children
        levels[level]["objects"] = definitions
        if level > 1:
            grid[f"level{level - 1}_subgrid"] = child_grid
    tiles["tile_counts"] = [len(level["objects"]) for level in tiles["levels"]]
    grid_path.write_text(json.dumps(grid, indent=2) + "\n")
    tiles_path.write_text(json.dumps(tiles, indent=2) + "\n")
    world["terrain_grid"] = base64.b64encode(mission_regions).decode("ascii")
    (root / world_name).write_text(json.dumps(world, indent=2) + "\n")
    print("Placed", sum(len(objects) for cells in objects_by_level.values()
                         for objects in cells.values()), "ground objects;",
          "tile patterns:", {level: len(table) for level, table in patterns.items()})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--container", default="VN")
    parser.add_argument("--world", default="SVN.WLD.json")
    args = parser.parse_args()
    compile_grid(args.root, args.container, args.world)
