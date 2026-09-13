"""Generate CC0 ground landmarks for a campaign using Blender."""

import argparse
import base64
import hashlib
import json
import sys
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[2] / "converted_assets_all" / "SVN"
MODELS = ROOT / "VN"
CONCRETE = (0.24, 0.25, 0.23)
METAL = (0.15, 0.19, 0.16)
ROOF = (0.27, 0.17, 0.11)
MARKING = (0.75, 0.72, 0.58)
# Visibility classes from the same reference models used by fit_ground_footprint.
REFERENCE_CULL_CLASSES = {21: 5, 38: 2, 90: 5, 93: 4}


def box(name, center, size, color, surface=None):
    """Dimensions are model units; negative Y is above the ground."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*color, 1)
    obj.data.materials.append(material)
    if surface is not None:
        obj.data["f15_surface"] = surface


def runway_marking(name, center, width, length):
    """Flat paint; center height includes a small offset to avoid z-fighting."""
    x, y, z = center
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([
        (x - width / 2, y, z - length / 2),
        (x + width / 2, y, z - length / 2),
        (x + width / 2, y, z + length / 2),
        (x - width / 2, y, z + length / 2),
    ], [], [(0, 1, 2, 3)])
    paint = bpy.data.materials.new(name)
    paint.diffuse_color = (0.9, 0.9, 0.9, 1.0)
    mesh.materials.append(paint)
    bpy.context.collection.objects.link(bpy.data.objects.new(name, mesh))


def airbase():
    box("Runway", (0, -1, 0), (48, 2, 640), CONCRETE, surface="runway")
    box("Apron", (75, -1, 0), (100, 2, 260), CONCRETE)
    for position in range(-280, 281, 40):
        runway_marking("Centerline", (0, -2.01, position), 2, 18)
    for position in (-90, 0, 90):
        box("Hangar", (90, -14, position), (48, 28, 54), METAL)
        box("Hangar door", (65.8, -11, position), (0.4, 20, 42), ROOF)
    box("Tower", (45, -25, 190), (12, 50, 12), CONCRETE)
    box("Tower cabin", (45, -49, 190), (22, 10, 22), METAL)


def radar():
    box("Radar pad", (0, -1, 0), (100, 2, 100), CONCRETE)
    box("Equipment shelter", (25, -7, 20), (28, 14, 20), METAL)
    # The 100-unit pad is fitted to 64 units; 125 units becomes the original 80-unit height.
    box("Mast", (-15, -56, 0), (4, 112, 4), METAL)
    box("Antenna", (-15, -112.5, 0), (38, 25, 3), METAL)
    for position in (-40, 40):
        box("Launcher", (position, -5, -30), (16, 10, 24), METAL)
        box("Missile rail", (position, -12, -30), (3, 4, 30), MARKING)


def depot():
    box("Depot yard", (0, -1, 0), (160, 2, 150), CONCRETE)
    for position in (-48, 0, 48):
        box("Warehouse", (position, -12, -30), (36, 24, 70), ROOF)
        box("Warehouse roof", (position, -25, -30), (40, 2, 74), METAL)
        box("Stores", (position, -5, 40), (26, 10, 20), METAL)


def carrier():
    # LOD 2 multiplies coordinates by four: deck height 32 matches the
    # game's 128-foot carrier surface. The runway passes through world origin.
    box("Hull", (4, -14, 136), (112, 28, 720), METAL)
    box("Flight deck", (4, -30, 136), (136, 4, 752), CONCRETE)
    box("Island", (56, -50, 240), (24, 36, 100), METAL)
    box("Bridge", (56, -72, 240), (32, 8, 60), CONCRETE)
    box("Mast", (56, -86, 240), (3, 20, 3), METAL)
    for position in range(-220, 493, 40):
        box("Deck centerline", (0, -32.1, position), (2, 0.2, 18), MARKING)


def fit_ground_footprint(slot):
    """Preserve proportions and align with the original X/Z footprint."""
    if slot == 90:
        # Carrier geometry is authored against the deck/spawn contract above.
        return
    # Bounds measured from the original VN models, in game model units.
    footprints = {
        21: (-256, 256, -512, 512),
        38: (-32, 32, -32, 32),
        90: (-64, 72, -240, 512),
        93: (-128, 256, -256, 256),
    }
    objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    vertices = [obj.matrix_world @ vertex.co
                for obj in objects for vertex in obj.data.vertices]
    minimum = [min(vertex[axis] for vertex in vertices) for axis in range(3)]
    maximum = [max(vertex[axis] for vertex in vertices) for axis in range(3)]
    left, right, back, front = footprints[slot]
    scale = min((right - left) / (maximum[0] - minimum[0]),
                (front - back) / (maximum[2] - minimum[2]))
    center_x = (minimum[0] + maximum[0]) / 2
    center_z = (minimum[2] + maximum[2]) / 2
    # Align the usable runway, not an asymmetric collection of hangars.
    runway_vertices = [obj.matrix_world @ vertex.co
                       for obj in objects if obj.data.get("f15_surface") == "runway"
                       for vertex in obj.data.vertices]
    if runway_vertices:
        center_x = (min(v.x for v in runway_vertices) + max(v.x for v in runway_vertices)) / 2
        center_z = (min(v.z for v in runway_vertices) + max(v.z for v in runway_vertices)) / 2
        # Keep every part inside the reference footprint after changing the anchor.
        for extent, available in (
            (center_x - minimum[0], (right - left) / 2),
            (maximum[0] - center_x, (right - left) / 2),
            (center_z - minimum[2], (front - back) / 2),
            (maximum[2] - center_z, (front - back) / 2),
        ):
            if extent > 0:
                scale = min(scale, available / extent)
    for obj in objects:
        inverse = obj.matrix_world.inverted()
        for vertex in obj.data.vertices:
            position = obj.matrix_world @ vertex.co
            position.x = (position.x - center_x) * scale + (left + right) / 2
            position.y *= scale
            position.z = (position.z - center_z) * scale + (back + front) / 2
            vertex.co = inverse @ position


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="Campaign asset directory")
    parser.add_argument("--container", default="VN", help="Terrain model container")
    parser.add_argument("--name-prefix", default="SVN", help="Generated model filename prefix")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = parser.parse_args(arguments)
    models = args.root / args.container
    container_path = models / f"{args.container}.3D3.json"
    container = json.loads(container_path.read_text())
    model_data = bytearray(base64.b64decode(container["model_data"]))
    manifest_path = args.root / "free-assets.json"
    manifest = json.loads(manifest_path.read_text())
    entries = {item["file"]: item for item in manifest["generated"]}
    for slot, name, generate in (
        (21, "Airbase", airbase), (38, "SAM_Radar", radar),
        (90, "Carrier", carrier), (93, "Supply_Dump", depot),
    ):
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)
        generate()
        fit_ground_footprint(slot)
        destination = models / f"shape_{slot:03d}_{args.name_prefix}_{name}.glb"
        bpy.ops.export_scene.gltf(filepath=str(destination), export_format="GLB",
                                  export_yup=True, export_extras=True)
        # Unique stream addresses preserve shape identification in terrain submits.
        if container["shape_offsets"][slot] == 0:
            container["shape_offsets"][slot] = len(model_data)
            model_data.extend(bytes(5))
        model_data[container["shape_offsets"][slot]] = REFERENCE_CULL_CLASSES[slot]
        container["shape_names"][str(slot)] = name.replace("_", " ")
        relative = destination.relative_to(args.root).as_posix()
        data = destination.read_bytes()
        entries[relative] = {
            "file": relative, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "license": "CC0-1.0",
            "generator": "tools/f15assets/generate_svn_ground.py",
        }
    container["model_data"] = base64.b64encode(model_data).decode("ascii")
    container["model_data_size"] = len(model_data)
    container_path.write_text(json.dumps(container, indent=2) + "\n")
    manifest["generated"] = list(entries.values())
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
