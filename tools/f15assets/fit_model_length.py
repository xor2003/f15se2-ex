"""Fit a GLB to a reference slot without changing its proportions or orientation."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile

from f15assets.gltf_scene import mesh_instances, transform_position
from f15assets.validation_model3d import (
    _gltf_accessor_values,
    _read_glb_doc_and_bin,
    glb_to_glmesh_bytes,
)


def bounds(document, binary):
    """Measure instantiated geometry, including node transforms."""
    points = []
    for mesh, matrix in mesh_instances(document):
        for primitive in mesh.get("primitives", []):
            accessor = primitive.get("attributes", {}).get("POSITION")
            if accessor is not None:
                points.extend(transform_position(matrix, point) for point in
                              _gltf_accessor_values(document, binary, accessor))
    if not points or not all(math.isfinite(value) for point in points for value in point):
        raise ValueError("Model must contain finite geometry")
    return ([min(point[axis] for point in points) for axis in range(3)],
            [max(point[axis] for point in points) for axis in range(3)])


def replace_json_chunk(source, document):
    """Preserve binary data and extension chunks while changing the scene graph."""
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    old_length, kind = struct.unpack_from("<II", source, 12)
    if source[:4] != b"glTF" or kind != 0x4E4F534A:
        raise ValueError("Expected GLB with a JSON first chunk")
    chunks = struct.pack("<II", len(encoded), kind) + encoded + source[20 + old_length:]
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def fit_model(model, reference, length_axis):
    """Apply uniform scale and reference-center alignment; rebuild the runtime cache."""
    document, binary = _read_glb_doc_and_bin(model)
    reference_document, reference_binary = _read_glb_doc_and_bin(reference)
    low, high = bounds(document, binary)
    reference_low, reference_high = bounds(reference_document, reference_binary)
    length = high[length_axis] - low[length_axis]
    reference_length = reference_high[length_axis] - reference_low[length_axis]
    if length <= 0 or reference_length <= 0:
        raise ValueError("Length axis must have nonzero extent")
    scale = reference_length / length
    translation = [(reference_low[axis] + reference_high[axis]) / 2 -
                   scale * (low[axis] + high[axis]) / 2 for axis in range(3)]
    if abs(scale - 1) < 1e-6 and max(map(abs, translation)) < 1e-5:
        return False
    if not document.get("scenes") or not document.get("nodes"):
        raise ValueError("Expected an explicit static scene")
    if document.get("animations") or document.get("skins"):
        raise ValueError("Only static models are supported")
    scene = document["scenes"][document.get("scene", 0)]
    root = {"name": "Reference slot fit", "children": list(scene.get("nodes", [])),
            "scale": [scale] * 3, "translation": translation}
    scene["nodes"] = [len(document["nodes"])]
    document["nodes"].append(root)
    payload = replace_json_chunk(model.read_bytes(), document)
    cache = model.parent / "cache" / (model.stem + ".glmesh")
    cache.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=model.parent) as temporary:
        staged_model = Path(temporary) / model.name
        staged_cache = Path(temporary) / cache.name
        staged_model.write_bytes(payload)
        staged_cache.write_bytes(glb_to_glmesh_bytes(staged_model))
        os.replace(staged_model, model)
        os.replace(staged_cache, cache)
    print(f"{model.name}: length {length:.3f} -> {reference_length:.3f}, scale {scale:.6f}")
    return True


def refresh_records(path, changed_files, root):
    """Refresh only existing inventory records for files changed by this operation."""
    document = json.loads(path.read_text())
    def visit(value):
        if isinstance(value, list):
            for child in value:
                visit(child)
        elif isinstance(value, dict):
            relative = value.get("file")
            if isinstance(relative, str) and relative in changed_files:
                data = (root / relative).read_bytes()
                value.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
            for child in value.values():
                if isinstance(child, (dict, list)):
                    visit(child)
    visit(document)
    path.write_text(json.dumps(document, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("reference_directory", type=Path)
    parser.add_argument("slots", nargs="+", type=int)
    parser.add_argument("--length-axis", choices=("x", "y", "z"), default="y",
                        help="Axis in the GLB scene, not Blender's coordinate system")
    args = parser.parse_args()
    changed = set()
    for slot in args.slots:
        matches = list((args.campaign / "15FLT").glob(f"shape_{slot:03d}_*.glb"))
        if len(matches) != 1:
            raise ValueError(f"Expected one replacement for slot {slot}, found {len(matches)}")
        model = matches[0]
        if fit_model(model, args.reference_directory / f"shape_{slot:03d}.glb",
                     "xyz".index(args.length_axis)):
            changed.add(model.relative_to(args.campaign).as_posix())
            changed.add((model.parent / "cache" / (model.stem + ".glmesh"))
                        .relative_to(args.campaign).as_posix())
    if changed:
        for name in ("free-assets.json", "inventory.json"):
            path = args.campaign / name
            if path.exists():
                refresh_records(path, changed, args.campaign)


if __name__ == "__main__":
    main()
