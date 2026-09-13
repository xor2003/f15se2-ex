"""Build static coastline GLBs from geographic land polygons for any campaign.

Requires Shapely 2.1 or newer. Corners are TL, TR, BL, BR longitude/latitude.
"""

import argparse
import base64
import json
from pathlib import Path
import struct

from shapely import constrained_delaunay_triangles
from shapely.geometry import box, shape
from shapely.ops import transform, unary_union
from f15assets.validation_model3d import glb_to_glmesh_bytes

# The legacy visibility test runs before the GLB is drawn. Class 7 covers
# a full 4096-unit terrain tile; class 0 would cull it as a point.
COAST_TILE_CULL_CLASS = 7


def project_to_world(corners):
    tl, tr, bl, br = corners
    east = (tr[0] - tl[0], tr[1] - tl[1])
    south = (bl[0] - tl[0], bl[1] - tl[1])
    bend = (br[0] - tr[0] - bl[0] + tl[0], br[1] - tr[1] - bl[1] + tl[1])

    def project(longitude, latitude, altitude=None):
        u = v = 0.5
        for _ in range(8):
            dx = tl[0] + east[0]*u + south[0]*v + bend[0]*u*v - longitude
            dy = tl[1] + east[1]*u + south[1]*v + bend[1]*u*v - latitude
            ax, ay = east[0] + bend[0]*v, east[1] + bend[1]*v
            bx, by = south[0] + bend[0]*u, south[1] + bend[1]*u
            determinant = ax*by - ay*bx
            if abs(determinant) < 1e-12:
                raise ValueError("Degenerate geographic corners")
            u -= (dx*by - dy*bx)/determinant
            v -= (ax*dy - ay*dx)/determinant
        # LOD4 spans four 4096-model-unit cells; game Y points northward.
        return u*16384, (1-v)*16384

    return project


def coastline_glb(land, water, center_x, center_y):
    document = {"asset": {"version": "2.0"}, "scene": 0,
                "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
                "meshes": [{"primitives": []}], "materials": [],
                "bufferViews": [], "accessors": []}
    binary = bytearray()
    for surface, geometry, color in (("land", land, [0.12, 0.18, 0.08, 1]),
                                     ("water", water, [0.025, 0.075, 0.11, 1])):
        vertices = []
        for triangle in constrained_delaunay_triangles(geometry).geoms:
            points = list(triangle.exterior.coords)[:3]
            # Positive-Z facing triangles, independent of polygon winding.
            a, b, c = points
            if (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0]) < 0:
                points.reverse()
            vertices.extend((x-center_x, y-center_y, 0.0) for x, y in points)
        if not vertices:
            continue
        material = len(document["materials"])
        document["materials"].append({"name": surface, "doubleSided": True,
            "extras": {"f15_surface": surface},
            "pbrMetallicRoughness": {"baseColorFactor": color, "metallicFactor": 0,
                                     "roughnessFactor": 1}})
        offset = len(binary)
        for vertex in vertices:
            binary.extend(struct.pack("<fff", *vertex))
        accessor = len(document["accessors"])
        document["bufferViews"].append({"buffer": 0, "byteOffset": offset,
                                       "byteLength": len(binary)-offset, "target": 34962})
        document["accessors"].append({"bufferView": accessor, "componentType": 5126,
            "count": len(vertices), "type": "VEC3",
            "min": [min(v[i] for v in vertices) for i in range(3)],
            "max": [max(v[i] for v in vertices) for i in range(3)]})
        document["meshes"][0]["primitives"].append({"attributes": {"POSITION": accessor},
                                                   "mode": 4, "material": material})
    document["buffers"] = [{"byteLength": len(binary)}]
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary += b"\0" * (-len(binary) % 4)
    return (struct.pack("<III", 0x46546c67, 2, 28+len(encoded)+len(binary)) +
            struct.pack("<II", len(encoded), 0x4e4f534a) + encoded +
            struct.pack("<II", len(binary), 0x004e4942) + binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("land", type=Path)
    parser.add_argument("container", type=Path, help="Base path, e.g. assets/VN/VN")
    parser.add_argument("--corners", type=float, nargs=8, required=True)
    parser.add_argument("--first-shape", type=int, required=True)
    parser.add_argument("--replace-generated", action="store_true",
                        help="Replace this generator's existing 16 coastline tiles")
    args = parser.parse_args()
    if not 0 <= args.first_shape <= 110:
        raise ValueError("Sixteen free shape slots below 126 are required; two slots are reserved")
    corners = list(zip(args.corners[::2], args.corners[1::2]))
    source = json.loads(args.land.read_text())
    land = unary_union([shape(feature["geometry"]) for feature in source["features"]])
    land = transform(project_to_world(corners), land).intersection(box(0,0,16384,16384))
    paths = [Path(str(args.container) + f".{suffix}.json") for suffix in ("3D3", "3DG", "3DT")]
    models, grid, tiles = [json.loads(path.read_text()) for path in paths]
    if any(grid["level4_top_grid"]) and not args.replace_generated:
        raise ValueError("LOD4 already contains objects; refusing to replace existing geography")
    offsets = models["shape_offsets"]
    offsets.extend([0] * max(0, args.first_shape + 16 - len(offsets)))
    if any(offsets[args.first_shape:args.first_shape+16]) and not args.replace_generated:
        raise ValueError("Requested coastline shape slots are occupied")
    streams = bytearray(base64.b64decode(models["model_data"]))
    level = next(level for level in tiles["levels"] if level["level"] == 4)
    coastline_tiles = []
    placeholder = bytes((COAST_TILE_CULL_CLASS, 0, 0, 0, 0))
    # Check every destination before writing any model. Replacement is limited
    # to the exact grid, streams and placements produced by this generator.
    for index in range(16):
        slot = args.first_shape + index
        tile = next(tile for tile in level["objects"] if tile["tile_index"] == index+1)
        if args.replace_generated:
            offset = offsets[slot]
            expected_objects = [{"x":0,"y":0,"z":0,"shape_word":slot}]
            matches_generated = (
                grid["level4_top_grid"][index] == index+1
                and tile["objects"] == expected_objects
                and models["shape_names"].get(str(slot)) == f"Coast {index}"
                and offset > 0
                and streams[offset:offset+len(placeholder)] == placeholder
            )
            if not matches_generated:
                raise ValueError(f"LOD4 tile {index+1} does not match generated coastline")
        elif tile["objects"]:
            raise ValueError(f"LOD4 tile {index+1} is occupied")
        coastline_tiles.append(tile)
    cache_dir = args.container.parent / "cache"
    cache_dir.mkdir(exist_ok=True)
    for row in range(4):
        for column in range(4):
            index = row*4 + column
            slot = args.first_shape + index
            region = box(column*4096,row*4096,(column+1)*4096,(row+1)*4096)
            ground = land.intersection(region)
            water = region.difference(land)
            model = args.container.parent / f"shape_{slot:03d}_Coast_{index:02d}.glb"
            model.write_bytes(coastline_glb(ground, water, (column+0.5)*4096, (row+0.5)*4096))
            (cache_dir / (model.stem + ".glmesh")).write_bytes(glb_to_glmesh_bytes(model))
            if not args.replace_generated:
                offsets[slot] = len(streams)
                streams.extend(placeholder)
            models["shape_names"][str(slot)] = f"Coast {index}"
            tile = coastline_tiles[index]
            tile["objects"] = [{"x":0,"y":0,"z":0,"shape_word":slot}]
            grid["level4_top_grid"][row*4+column] = index+1
    models["model_data"] = base64.b64encode(streams).decode()
    models["model_data_size"] = len(streams)
    for path, document in zip(paths, (models, grid, tiles)):
        path.write_text(json.dumps(document, indent=2) + "\n")
    print("Installed 16 coastline tiles; lower LOD terrain and objects preserved")


if __name__ == "__main__":
    main()
