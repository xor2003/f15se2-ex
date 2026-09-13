"""Install generated highland tiles in the standalone SVN campaign."""

import base64
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2] / "converted_assets_all" / "SVN" / "VN"
CORNERS = ((97.843, 15.059), (106.060, 23.514),
           (105.932, 7.412), (114.149, 16.180))


def coordinates(column, row):
    # Terrain coordinates run northward; WLD/map-image Y runs southward.
    u, v = (column + 0.5) / 16, 1.0 - (row + 0.5) / 16
    weights = ((1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v)
    return tuple(sum(weight * corner[axis] for weight, corner in zip(weights, CORNERS))
                 for axis in range(2))


def install():
    paths = [ROOT / f"VN.{extension}.json" for extension in ("3D3", "3DG", "3DT")]
    models, grid, tiles = [json.loads(path.read_text()) for path in paths]
    model_data = bytearray(base64.b64decode(models["model_data"]))
    # Distinct stream addresses let the runtime recover each GLB shape ID.
    for slot in range(80, 84):
        if models["shape_offsets"][slot] == 0:
            models["shape_offsets"][slot] = len(model_data)
            model_data.extend(bytes(5))
    # drawCockpit reads the ground palette index at byte 47 of this container.
    if len(model_data) < 48:
        model_data.extend(bytes(48 - len(model_data)))
    model_data[47] = 2
    models["model_data"] = base64.b64encode(model_data).decode("ascii")
    models["model_data_size"] = len(model_data)
    level = next(level for level in tiles["levels"] if level["level"] == 3)
    for variant in range(4):
        tile = next(tile for tile in level["objects"] if tile["tile_index"] == variant + 1)
        tile["objects"] = [{"x": 0, "y": 0, "z": 0, "shape_word": 80 + variant}]
    placed = 0
    grid["level3_grid"] = [0] * 256
    for row in range(16):
        for column in range(16):
            longitude, latitude = coordinates(column, row)
            # An inland prototype region; detailed geography is placed separately.
            if 102.5 <= longitude <= 105.5 and 16.0 <= latitude <= 21.0:
                grid["level3_grid"][row * 16 + column] = 1 + (column + row * 3) % 4
                placed += 1
    assert placed > 0
    for path, payload in zip(paths, (models, grid, tiles)):
        path.write_text(json.dumps(payload, indent=2) + "\n")
    manifest_path = ROOT.parent / "free-assets.json"
    manifest = json.loads(manifest_path.read_text())
    entries = {item["file"]: item for item in manifest["generated"]}
    for variant in range(4):
        model = ROOT / f"shape_{80 + variant:03d}_SVN_Terrain_{variant}.glb"
        relative = model.relative_to(ROOT.parent).as_posix()
        data = model.read_bytes()
        entries[relative] = {
            "file": relative, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "license": "CC0-1.0",
            "generator": "tools/f15assets/generate_svn_terrain.py",
        }
    attribution = ROOT.parent / "geography" / "osm-bases.json"
    if attribution.exists():
        data = attribution.read_bytes()
        entries["geography/osm-bases.json"] = {
            "file": "geography/osm-bases.json", "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "license": "ODbL-1.0",
            "generator": "tools/f15assets/place_svn_bases.py",
        }
    manifest["generated"] = list(entries.values())
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Installed four terrain models in {placed} inland cells")


if __name__ == "__main__":
    install()
