"""Generate seamless terrain models for a campaign using Blender."""

import argparse
import json
import math
import sys
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[2] / "converted_assets_all" / "SVN" / "VN"
GRID_SIZE = 17
TILE_SIZE = 4096.0
TERRAIN_SLOTS = range(80, 84)
EXPOSED_ROCK_SLOPE_DEGREES = 40.0


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def material(name, color):
    result = bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1.0)
    result.use_backface_culling = False
    return result


def elevation(x, z, variant):
    edge = max(abs(x), abs(z)) / (TILE_SIZE * 0.5)
    border_fade = max(0.0, 1.0 - edge * edge) ** 2
    ridge = abs(math.sin((x + variant * 311.0) / 720.0))
    rolling = 0.5 + 0.5 * math.sin((x + z) / 860.0 + variant * 1.7)
    peak_x = (-720.0, 680.0, -180.0, 820.0)[variant]
    peak_z = (510.0, -620.0, -880.0, 760.0)[variant]
    distance = ((x - peak_x) ** 2 + (z - peak_z) ** 2) / (2.0 * 760.0 ** 2)
    peak = math.exp(-distance)
    height = (55.0 + 90.0 * rolling + 125.0 * ridge + 170.0 * peak) * border_fade
    return -height


def create_tile(slot, snow_line_meters=None, meters_per_model_unit=None,
                snow_transition_meters=200.0, output_directory=ROOT,
                name_prefix="SVN"):
    variant = slot - min(TERRAIN_SLOTS)
    vertices = []
    faces = []
    step = TILE_SIZE / (GRID_SIZE - 1)
    for row in range(GRID_SIZE):
        z = -TILE_SIZE * 0.5 + row * step
        for column in range(GRID_SIZE):
            x = -TILE_SIZE * 0.5 + column * step
            vertices.append((x, elevation(x, z, variant), z))
    for row in range(GRID_SIZE - 1):
        for column in range(GRID_SIZE - 1):
            a = row * GRID_SIZE + column
            b = a + 1
            c = a + GRID_SIZE
            d = c + 1
            faces.extend(((a, b, d), (a, d, c)))

    mesh = bpy.data.meshes.new(f"{name_prefix} terrain {variant}")
    mesh["f15_surface"] = "land"
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(material("Forest", (0.10, 0.22, 0.08)))
    mesh.materials.append(material("Forest variation", (0.13, 0.25, 0.10)))
    mesh.materials.append(material("Exposed rock", (0.25, 0.27, 0.24)))
    snow_steps = 8
    if snow_line_meters is not None:
        for step_index in range(1, snow_steps + 1):
            coverage = step_index / snow_steps
            forest = (0.10, 0.22, 0.08)
            snow = (0.82, 0.86, 0.88)
            color = tuple(low + (high - low) * coverage for low, high in zip(forest, snow))
            mesh.materials.append(material(f"Snow cover {step_index}", color))
    mesh.update()
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.collection.objects.link(obj)
    for polygon in mesh.polygons:
        # Y is vertical before export. Height alone does not imply bare rock.
        steep = abs(polygon.normal.y) < math.cos(math.radians(EXPOSED_ROCK_SLOPE_DEGREES))
        center = polygon.center
        vegetation = math.sin(center.x / 950.0 + variant) * math.cos(center.z / 1100.0)
        polygon.material_index = 2 if steep else int(vegetation > 0.2)
        if snow_line_meters is not None and not steep:
            altitude_meters = -center.y * meters_per_model_unit
            coverage = max(0.0, min(1.0,
                (altitude_meters - snow_line_meters) / snow_transition_meters))
            snow_step = int(coverage * snow_steps)
            if snow_step:
                polygon.material_index = 2 + snow_step
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    destination = output_directory / f"shape_{slot:03d}_{name_prefix}_Terrain_{variant}.glb"
    # Match the original converter: Blender negative Y becomes runtime positive Z.
    bpy.ops.export_scene.gltf(
        filepath=str(destination), export_format="GLB", use_selection=True, export_yup=True,
        export_extras=True
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, help="Campaign terrain-generation settings JSON")
    parser.add_argument("--root", type=Path, default=ROOT.parent,
                        help="Campaign asset directory")
    parser.add_argument("--container", default="VN", help="Terrain model container")
    parser.add_argument("--name-prefix", default="SVN", help="Generated model filename prefix")
    parser.add_argument("--snow-line-meters", type=float,
                        help="Altitude where snow begins; disabled when omitted")
    parser.add_argument("--meters-per-model-unit", type=float,
                        help="Include the terrain placement LOD scale")
    parser.add_argument("--snow-transition-meters", type=float, default=200.0)
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    preliminary, _ = parser.parse_known_args(arguments)
    if preliminary.config:
        settings = json.loads(preliminary.config.read_text())
        allowed = {"snow_line_meters", "meters_per_model_unit", "snow_transition_meters"}
        if not isinstance(settings, dict) or settings.keys() - allowed:
            parser.error("Terrain settings must contain only snow-line, scale and transition fields")
        for name, value in settings.items():
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                parser.error(f"{name} must be a number")
        parser.set_defaults(**settings)
    args = parser.parse_args(arguments)
    if args.snow_line_meters is not None:
        if not math.isfinite(args.snow_line_meters) or args.snow_line_meters < 0:
            parser.error("Snow line must be finite and nonnegative")
        if (args.meters_per_model_unit is None or
                not math.isfinite(args.meters_per_model_unit) or args.meters_per_model_unit <= 0):
            parser.error("Snow requires a finite, positive --meters-per-model-unit")
    if not math.isfinite(args.snow_transition_meters) or args.snow_transition_meters <= 0:
        parser.error("Snow transition must be finite and positive")
    output_directory = args.root / args.container
    output_directory.mkdir(parents=True, exist_ok=True)
    for terrain_slot in TERRAIN_SLOTS:
        clear_scene()
        create_tile(terrain_slot, args.snow_line_meters, args.meters_per_model_unit,
                    args.snow_transition_meters, output_directory, args.name_prefix)


if __name__ == "__main__":
    main()
