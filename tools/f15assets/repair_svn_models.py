"""Rebuild SVN flight slots with explicit roles, axes and reference dimensions."""

import copy
import json
import math
import os
from pathlib import Path
import shutil
import struct
import tempfile

from fit_model_length import bounds, replace_json_chunk, refresh_records
from f15assets.validation_model3d import _read_glb_doc_and_bin, glb_to_glmesh_bytes


ROOT = Path(__file__).resolve().parents[2] / "converted_assets_all/SVN"
# GLB scene coordinates consumed by the renderer: X right, Y forward, Z up.
# These are bounding boxes in original model units, not mesh geometry.
REFERENCE_BOUNDS = {
    0: ((-16, -32, 0), (16, 36, 12)),
    1: ((-6, -66, -6), (6, 22, 6)),
    3: ((-48, -32, -32), (32, 32, 32)),
    5: ((-22, -32, 0), (22, 32, 0)),
    13: ((-6, -64, -6), (6, 18, 6)),
    14: ((-48, -48, -96), (48, 48, 36)),
    15: ((-4, -18, -4), (4, 18, 4)),
    17: ((-48, -24, -40), (64, 48, 48)),
    19: ((-6, -96, -6), (6, 32, 6)),
    21: ((-22, -32, 0), (22, 32, 0)),
}
FIGHTER_SLOTS = (2, 4, 8, 9, 10, 11, 12, 16, 18, 20, 22)


def fit_document(document, binary, reference, swap_forward_up=False):
    """Fit source length uniformly after converting its documented coordinate axes."""
    document = copy.deepcopy(document)
    scene = document["scenes"][document.get("scene", 0)]
    children = list(scene["nodes"])
    if swap_forward_up:
        # Source placeholders use +Z forward and +Y up. The reflection is
        # intentional; the GLMESH compiler reverses mirrored triangle winding.
        matrix = [1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1]
        scene["nodes"] = [len(document["nodes"])]
        document["nodes"].append({"name": "Y forward Z up", "matrix": matrix,
                                  "children": children})
    low, high = bounds(document, binary)
    reference_low, reference_high = reference
    length = high[1] - low[1]
    if length <= 0:
        raise ValueError("Aircraft has no longitudinal extent")
    scale = (reference_high[1] - reference_low[1]) / length
    translation = [(reference_low[a] + reference_high[a] - scale*(low[a]+high[a]))/2
                   for a in range(3)]
    children = list(scene["nodes"])
    scene["nodes"] = [len(document["nodes"])]
    document["nodes"].append({"name": "Original slot dimensions", "children": children,
                              "scale": [scale]*3, "translation": translation})
    return document


def procedural_glb(name, points, triangles, color):
    """Encode newly generated geometry directly in game-oriented GLB coordinates."""
    positions = b"".join(struct.pack("<3f", *point) for point in points)
    indices = b"".join(struct.pack("<I", index) for face in triangles for index in face)
    binary = positions + indices
    document = {
        "asset": {"version": "2.0", "generator": "f15assets repair_svn_models"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0},
                    "indices": 1, "material": 0, "mode": 4}]}],
        "materials": [{"name": name, "doubleSided": True,
                       "pbrMetallicRoughness": {"baseColorFactor": [*color, 1],
                                                "metallicFactor": 0, "roughnessFactor": 1}}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(positions)},
                        {"buffer": 0, "byteOffset": len(positions), "byteLength": len(indices)}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(points),
                       "type": "VEC3", "min": [min(p[a] for p in points) for a in range(3)],
                       "max": [max(p[a] for p in points) for a in range(3)]},
                      {"bufferView": 1, "componentType": 5125, "count": len(triangles)*3,
                       "type": "SCALAR"}],
        "extras": {"license": "CC0-1.0", "axes": "X right, Y forward, Z up"},
    }
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    chunks = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    chunks += struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<4sII", b"glTF", 2, 12+len(chunks)) + chunks


def projectile(slot):
    """Build an axial missile or bomb with fins inside the reference envelope."""
    low, high = REFERENCE_BOUNDS[slot]
    radius = high[0]
    length = high[1]-low[1]
    points, faces = [], []
    for y, ring_radius in ((low[1], radius*.35), (high[1]-length*.2, radius*.4), (high[1], 0)):
        points.extend((ring_radius*math.cos(i*math.tau/12), y,
                       ring_radius*math.sin(i*math.tau/12)) for i in range(12))
    for ring in range(2):
        for i in range(12):
            a, b, c, d = ring*12+i, ring*12+(i+1)%12, (ring+1)*12+i, (ring+1)*12+(i+1)%12
            faces.extend(((a, b, c), (b, d, c)))
    for i in range(4):
        angle = i*math.pi/2
        offset = len(points)
        points.extend(((0, low[1], 0), (radius*math.cos(angle), low[1]+length*.08,
                                      radius*math.sin(angle)), (0, low[1]+length*.28, 0)))
        faces.append((offset, offset+1, offset+2))
    return procedural_glb("Bomb" if slot == 15 else "Missile", points, faces, (.55, .58, .52))


def smoke(slot):
    """Make a low-poly smoke volume, not a radar attached to a particle."""
    low, high = REFERENCE_BOUNDS[slot]
    center = [(a+b)/2 for a, b in zip(low, high)]
    radii = [(b-a)/2 for a, b in zip(low, high)]
    points = []
    for latitude in range(9):
        angle = latitude*math.pi/8
        for longitude in range(16):
            azimuth = longitude*math.tau/16
            unit = (math.sin(angle)*math.cos(azimuth), math.sin(angle)*math.sin(azimuth), math.cos(angle))
            points.append(tuple(center[a]+radii[a]*unit[a] for a in range(3)))
    faces = []
    for row in range(8):
        for column in range(16):
            a, b = row*16+column, row*16+(column+1)%16
            faces.extend(((a, b, a+16), (b, b+16, a+16)))
    return procedural_glb("Smoke", points, faces, (.27, .27, .25))


def parachute():
    """Generate a canopy, suspension lines and a hanging payload in the slot bounds."""
    points = [(0, 0, 36)]
    points += [(48*math.cos(i*math.tau/16), 48*math.sin(i*math.tau/16), 6) for i in range(16)]
    faces = [(0, i+1, (i+1)%16+1) for i in range(16)]
    for i in range(0, 16, 2):
        end = len(points)
        points.extend(((-1, 0, -80), (1, 0, -80)))
        faces.append((i+1, end, end+1))
    end = len(points)
    points.extend(((-5, -3, -76), (5, -3, -76), (0, 4, -76), (0, 0, -96)))
    faces.extend(((end,end+1,end+2),(end,end+3,end+1),(end+1,end+3,end+2),(end+2,end+3,end)))
    return procedural_glb("Parachute", points, faces, (.65, .66, .48))


def shadow():
    """Generate an aircraft silhouette on the Z=0 plane."""
    outline = [(0,32), (3,6), (22,-8), (5,-13), (8,-30), (0,-26),
               (-8,-30), (-5,-13), (-22,-8), (-3,6)]
    points = [(0,0,0)] + [(x,y,0) for x,y in outline] + [(0,-32,0)]
    faces = [(0,i+1,(i+1)%len(outline)+1) for i in range(len(outline))]
    faces.append((5,11,7))
    return procedural_glb("Aircraft shadow", points, faces, (.035,.045,.035))


def main():
    models = ROOT / "15FLT"
    sources = models / "sources"
    upright = sources / "player_upright.glb"
    if not upright.exists():
        shutil.copyfile(next(models.glob("shape_006_*.glb")), upright)
    payloads = {}
    document, binary = _read_glb_doc_and_bin(upright)
    payloads[0] = replace_json_chunk(upright.read_bytes(), fit_document(document, binary, REFERENCE_BOUNDS[0]))
    reference_directory = ROOT.parent / "15FLT"
    for slot in FIGHTER_SLOTS:
        source = sources / f"aircraft_{slot:03d}.glb"
        document, binary = _read_glb_doc_and_bin(source)
        reference = bounds(*_read_glb_doc_and_bin(reference_directory / f"shape_{slot:03d}.glb"))
        payloads[slot] = replace_json_chunk(source.read_bytes(), fit_document(document, binary, reference, True))
    for slot in (1, 13, 15, 19):
        payloads[slot] = projectile(slot)
    for slot in (3, 17):
        payloads[slot] = smoke(slot)
    payloads[14] = parachute()
    payloads[5] = payloads[21] = shadow()
    destinations = {}
    for slot in payloads:
        matches = list(models.glob(f"shape_{slot:03d}_*.glb"))
        if len(matches) != 1:
            raise ValueError(f"Expected one file for slot {slot}: {matches}")
        destinations[slot] = matches[0]
    changed = set()
    # Build every cache before replacing any installed model.
    with tempfile.TemporaryDirectory(dir=models) as temporary:
        staged = Path(temporary)
        for slot, payload in payloads.items():
            model = staged / destinations[slot].name
            model.write_bytes(payload)
            model.with_suffix(".glmesh").write_bytes(glb_to_glmesh_bytes(model))
        for slot, destination in destinations.items():
            cache = models / "cache" / (destination.stem+".glmesh")
            os.replace(staged / destination.name, destination)
            os.replace(staged / cache.name, cache)
            changed.update((destination.relative_to(ROOT).as_posix(), cache.relative_to(ROOT).as_posix()))
            print(f"Rebuilt slot {slot:03d}: {destination.name}")
    for name in ("free-assets.json", "inventory.json"):
        refresh_records(ROOT/name, changed, ROOT)


if __name__ == "__main__":
    main()
