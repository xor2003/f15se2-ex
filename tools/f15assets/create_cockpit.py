"""Build a Blender-editable 3D dashboard with the game's live instrument bindings."""

import argparse
import json
import math
import struct
from pathlib import Path


# Logical, half-open rectangles. Names are runtime material bindings, not labels.
DISPLAYS = [
    ("display_map", (24, 112, 73, 57)),
    ("display_radar", (120, 104, 80, 72)),
    ("display_target", (232, 128, 73, 57)),
    ("indicator_R", (163, 191, 7, 7)),
    ("indicator_I", (180, 191, 7, 7)),
    ("indicator_B", (214, 191, 7, 7)),
    ("indicator_L", (197, 191, 7, 7)),
    ("weapons", (20, 188, 112, 12)),
    ("throttle", (2, 114, 15, 47)),
    ("fuel", (212, 128, 11, 50)),
]
VERTICAL_FOV = math.radians(60)
FOCAL_LENGTH = 100 / math.tan(VERTICAL_FOV / 2)


def position(x, y, depth):
    """Unproject a layout point: depth changes must not shift the default view."""
    return ((x - 160) * depth / FOCAL_LENGTH,
            (100 - y) * depth / FOCAL_LENGTH, -depth)


def panel_depth(x, y):
    """Tilt the dashboard and raise the monitor rims towards the pilot."""
    for _, (left, top, width, height) in DISPLAYS[:3]:
        if left - 2 <= x <= left + width + 2 and top - 2 <= y <= top + height + 2:
            return 0.96
    return 1.08 - 0.08 * (y - 96) / 104


class _TextureLoader:
    def __init__(self, data):
        self._data = data
    def read_bytes(self):
        return self._data


def create_cockpit(image_path, output, console_texture=None):
    """Embed the existing PNG and emit a self-contained glTF 2.0 binary."""
    png = image_path.read_bytes()
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Cockpit texture must be a PNG")

    base_dir = image_path.parent
    left_found = base_dir / "256LEFT.png"
    right_found = base_dir / "256RIGHT.png"

    if console_texture is None:
        if left_found.exists():
            console_texture = _TextureLoader(left_found.read_bytes())
        elif right_found.exists():
            console_texture = _TextureLoader(right_found.read_bytes())
    binary = bytearray()
    document = {
        "asset": {"version": "2.0", "generator": "f15assets/create_cockpit.py"},
        "extensionsUsed": ["KHR_materials_unlit"],
        "scene": 0, "scenes": [{"name": "F-15 cockpit", "nodes": []}],
        "nodes": [], "meshes": [], "materials": [], "bufferViews": [], "accessors": [],
        "buffers": [{"byteLength": 0}],
        "cameras": [{"name": "pilot", "type": "perspective", "perspective": {
            "yfov": VERTICAL_FOV, "aspectRatio": 1.6, "znear": 0.05, "zfar": 100}}],
    }

    def buffer_view(data):
        while len(binary) % 4:
            binary.append(0)
        index = len(document["bufferViews"])
        document["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
        binary.extend(data)
        return index

    def accessor(values, dimensions):
        flat = [component for vector in values for component in vector]
        view = buffer_view(struct.pack("<" + "f" * len(flat), *flat))
        index = len(document["accessors"])
        document["accessors"].append({
            "bufferView": view, "componentType": 5126, "count": len(values),
            "type": f"VEC{dimensions}",
            "min": [min(v[axis] for v in values) for axis in range(dimensions)],
            "max": [max(v[axis] for v in values) for axis in range(dimensions)],
        })
        return index

    image_view = buffer_view(png)
    document["images"] = [{"name": "cockpit artwork", "mimeType": "image/png", "bufferView": image_view}]
    document["samplers"] = [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}]
    document["textures"] = [{"source": 0, "sampler": 0}]

    def add_mesh(name, vertices, texcoords, live_rect=None, solid=False, color=None, texture=0):
        material = {"name": name, "doubleSided": True,
                    "extensions": {"KHR_materials_unlit": {}},
                    "pbrMetallicRoughness": {"metallicFactor": 0, "roughnessFactor": 1}}
        pbr = material["pbrMetallicRoughness"]
        if solid:
            pbr["baseColorFactor"] = color or [0.08, 0.09, 0.10, 1]
        elif live_rect:
            # A dark preview in Blender; the game supplies the live image.
            pbr["baseColorFactor"] = [0.01, 0.015, 0.02, 1]
        else:
            pbr["baseColorTexture"] = {"index": texture}
            material.update(alphaMode="MASK", alphaCutoff=0.5)
        material_index = len(document["materials"])
        document["materials"].append(material)
        mesh_index = len(document["meshes"])
        document["meshes"].append({"name": name, "primitives": [{
            "attributes": {"POSITION": accessor(vertices, 3), "TEXCOORD_0": accessor(texcoords, 2)},
            "mode": 4, "material": material_index}]})
        node_index = len(document["nodes"])
        document["nodes"].append({"name": name, "mesh": mesh_index})
        document["scenes"][0]["nodes"].append(node_index)

    def quad(vertices, texcoords, corners, uv_corners):
        # Layout Y points down; this winding faces the pilot along +Z.
        for index in (0, 2, 1, 0, 3, 2):
            vertices.append(corners[index])
            texcoords.append(uv_corners[index])

    # The painted screen cutouts may extend beyond the live displays. An opaque
    # structural panel behind them prevents those slivers from revealing terrain.
    vertices, texcoords = [], []
    quad(vertices, texcoords,
         [position(x, y, 1.20) for x, y in ((0, 96), (320, 96), (320, 200), (0, 200))],
         [(0, 0), (1, 0), (1, 1), (0, 1)])
    add_mesh("dashboard_backing", vertices, texcoords, solid=True, color=[0.006, 0.008, 0.01, 1])

    xs = set(range(0, 321, 4))
    ys = set(range(96, 201, 4))
    for _, (x, y, width, height) in DISPLAYS:
        xs.update((x, x + width))
        ys.update((y, y + height))
    for _, (x, y, width, height) in DISPLAYS[:3]:
        xs.update((x - 2, x + width + 2))
        ys.update((y - 2, y + height + 2))
    xs, ys = sorted(xs), sorted(ys)
    vertices, texcoords = [], []
    for top, bottom in zip(ys, ys[1:]):
        for left, right in zip(xs, xs[1:]):
            cx, cy = (left + right) / 2, (top + bottom) / 2
            if any(x <= cx < x + w and y <= cy < y + h for _, (x, y, w, h) in DISPLAYS):
                continue
            corners = [(left, top), (right, top), (right, bottom), (left, bottom)]
            quad(vertices, texcoords,
                 [position(x, y, panel_depth(x, y)) for x, y in corners],
                 [(x / 320, y / 200) for x, y in corners])
    add_mesh("dashboard", vertices, texcoords)

    for name, rect in DISPLAYS:
        x, y, width, height = rect
        corners = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
        depth = 1.04 if name.startswith("display_") else panel_depth(x, y)
        vertices, texcoords = [], []
        quad(vertices, texcoords, [position(px, py, depth) for px, py in corners],
             [(0, 0), (1, 0), (1, 1), (0, 1)])
        add_mesh(name, vertices, texcoords, live_rect=rect)
        if name.startswith("display_"):
            vertices, texcoords = [], []
            for start, end in zip(corners, corners[1:] + corners[:1]):
                quad(vertices, texcoords,
                     [position(*start, 0.96), position(*end, 0.96),
                      position(*end, depth), position(*start, depth)],
                     [(0, 0), (1, 0), (1, 1), (0, 1)])
            add_mesh(name + "_recess", vertices, texcoords, solid=True)

            # Each rim is a separate raised ring with sloping inner/outer faces,
            # not a painted outline. Inner edges use the exact display rectangle.
            rings = [
                [(x - 3, y - 3), (x + width + 3, y - 3),
                 (x + width + 3, y + height + 3), (x - 3, y + height + 3)],
                [(x - 1, y - 1), (x + width + 1, y - 1),
                 (x + width + 1, y + height + 1), (x - 1, y + height + 1)],
                corners,
            ]
            depths = (0.95, 0.92, 1.04)
            for band, shade in ((0, [0.30, 0.34, 0.37, 1]),
                                (1, [0.12, 0.15, 0.17, 1])):
                vertices, texcoords = [], []
                for side in range(4):
                    following = (side + 1) % 4
                    quad(vertices, texcoords,
                         [position(*rings[band][side], depths[band]),
                          position(*rings[band][following], depths[band]),
                          position(*rings[band + 1][following], depths[band + 1]),
                          position(*rings[band + 1][side], depths[band + 1])],
                         [(0, 0), (1, 0), (1, 1), (0, 1)])
                add_mesh(name + ("_outer_bevel" if band == 0 else "_inner_bevel"),
                         vertices, texcoords, solid=True, color=shade)

    # Physical caps for the five existing map buttons. Their original texture is
    # retained on the front; side faces supply thickness when edited/viewed in 3D.
    for button, center_x in enumerate((36, 48, 60, 72, 84)):
        corners = [(center_x - 2, 172), (center_x + 2, 172),
                   (center_x + 2, 177), (center_x - 2, 177)]
        vertices, texcoords = [], []
        quad(vertices, texcoords, [position(x, y, 0.93) for x, y in corners],
             [(x / 320, y / 200) for x, y in corners])
        add_mesh(f"map_button_{button + 1}_cap", vertices, texcoords)
        vertices, texcoords = [], []
        for start, end in zip(corners, corners[1:] + corners[:1]):
            quad(vertices, texcoords,
                 [position(*start, 0.93), position(*end, 0.93),
                  position(*end, 1.02), position(*start, 1.02)],
                 [(0, 0), (1, 0), (1, 1), (0, 1)])
        add_mesh(f"map_button_{button + 1}_sides", vertices, texcoords, solid=True)

    def box(name, minimum, maximum, color, transform=None):
        x0, y0, z0 = minimum
        x1, y1, z1 = maximum
        corners = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                   (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        if transform:
            corners = [transform(point) for point in corners]
        vertices, texcoords = [], []
        for face in ((0, 1, 2, 3), (5, 4, 7, 6), (4, 0, 3, 7),
                     (1, 5, 6, 2), (3, 2, 6, 7), (4, 5, 1, 0)):
            quad(vertices, texcoords, [corners[i] for i in face],
                 [(0, 1), (1, 1), (1, 0), (0, 0)])
        add_mesh(name, vertices, texcoords, solid=True, color=color)

    def beam(name, start, end, radius=0.018, color=None, capped=False):
        direction = [end[i] - start[i] for i in range(3)]
        length = math.sqrt(sum(v * v for v in direction))
        direction = [v / length for v in direction]
        helper = (0, 1, 0) if abs(direction[1]) < 0.9 else (1, 0, 0)
        u = (direction[1] * helper[2] - direction[2] * helper[1],
             direction[2] * helper[0] - direction[0] * helper[2],
             direction[0] * helper[1] - direction[1] * helper[0])
        norm = math.sqrt(sum(v * v for v in u))
        u = [v / norm for v in u]
        v = (direction[1] * u[2] - direction[2] * u[1],
             direction[2] * u[0] - direction[0] * u[2],
             direction[0] * u[1] - direction[1] * u[0])
        rings = []
        for center in (start, end):
            rings.append([tuple(center[axis] + radius * (u[axis] * math.cos(i * math.tau / 8) +
                          v[axis] * math.sin(i * math.tau / 8)) for axis in range(3)) for i in range(8)])
        vertices, texcoords = [], []
        for i in range(8):
            j = (i + 1) % 8
            quad(vertices, texcoords, [rings[0][i], rings[0][j], rings[1][j], rings[1][i]],
                 [(0, 0), (1, 0), (1, 1), (0, 1)])
        if capped:
            for ring, center in zip(rings, (start, end)):
                for i in range(8):
                    vertices.extend((center, ring[i], ring[(i + 1) % 8]))
                    texcoords.extend(((0.5, 0.5), (0, 0), (1, 0)))
        add_mesh(name, vertices, texcoords, solid=True, color=color or [0.18, 0.21, 0.23, 1])

    metal = [0.18, 0.21, 0.23, 1]
    shell = [0.075, 0.09, 0.10, 1]
    panel = [0.12, 0.14, 0.15, 1]
    rubber = [0.025, 0.03, 0.035, 1]
    olive = [0.14, 0.16, 0.085, 1]
    webbing = [0.29, 0.30, 0.20, 1]
    silver = [0.48, 0.51, 0.50, 1]
    ivory = [0.68, 0.70, 0.62, 1]

    panel_texture = None
    if console_texture is not None:
        data = console_texture.read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError("Console texture must be a PNG")
        image_index = len(document["images"])
        document["images"].append({"name": "console panels", "mimeType": "image/png",
                                   "bufferView": buffer_view(data)})
        panel_texture = len(document["textures"])
        document["textures"].append({"source": image_index, "sampler": 0})

    def console_point(point):
        x, y, z = point
        return (x, y + 0.50 * (abs(x) - 0.64), z)

    for name, side in (("left", -1), ("right", 1)):
        low_x, high_x = sorted((side * 0.64, side * 1.02))
        box(name + "_console_structure", (low_x, -0.69, -0.92),
            (high_x, -0.43, 0.92), shell)
        box(name + "_console", (low_x, -0.45, -0.92),
            (high_x, -0.43, 0.92), metal, transform=console_point)
        wall_x0, wall_x1 = sorted((side * 0.99, side * 1.04))
        box(name + "_sidewall", (wall_x0, -0.69, -0.92), (wall_x1, 0.02, 1.02), shell)
        for module, z in enumerate((-0.70, 0.14, 0.59)):
            x0, x1 = sorted((side * 0.68, side * 0.97))
            prefix = f"{name}_panel_{module}"
            box(prefix, (x0, -0.43, z - 0.17), (x1, -0.414, z + 0.17),
                panel, transform=console_point)
            if panel_texture is not None:
                tile = module + (0 if side < 0 else 3)
                u0, u1 = (tile % 3) / 3, (tile % 3 + 1) / 3
                v0, v1 = (tile // 3) / 2, (tile // 3 + 1) / 2
                corners = [(side * 0.68, -0.413, z - 0.17),
                           (side * 0.97, -0.413, z - 0.17),
                           (side * 0.97, -0.413, z + 0.17),
                           (side * 0.68, -0.413, z + 0.17)]
                vertices, texcoords = [], []
                quad(vertices, texcoords, [console_point(p) for p in corners],
                     [(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
                add_mesh(prefix + "_texture", vertices, texcoords, texture=panel_texture)
                continue
            for row in range(2):
                for column in range(2):
                    x = side * (0.74 + column * 0.15)
                    cz = z - 0.08 + row * 0.15
                    control = f"{prefix}_switch_{row}_{column}"
                    box(control + "_base", (x - 0.022, -0.414, cz - 0.022),
                        (x + 0.022, -0.405, cz + 0.022), rubber, transform=console_point)
                    beam(control, console_point((x, -0.405, cz)),
                         console_point((x, -0.358, cz + (0.016 if row else -0.016))),
                         0.008, silver, capped=True)
                    box(control + "_mark", (x - 0.018, -0.413, cz + 0.033),
                        (x + 0.018, -0.410, cz + 0.039), ivory, transform=console_point)
            for x in (x0 + 0.015, x1 - 0.015):
                beam(f"{prefix}_fastener_{x:.3f}", console_point((x, -0.414, z - 0.15)),
                     console_point((x, -0.408, z - 0.15)), 0.007, silver, capped=True)
        for index, x in enumerate((side * 0.75, side * 0.90)):
            beam(f"{name}_rotary_knob_{index}", console_point((x, -0.43, -0.04)),
                 console_point((x, -0.39, -0.04)), 0.030, rubber, capped=True)
            box(f"{name}_knob_pointer_{index}", (x - 0.004, -0.389, -0.063),
                (x + 0.004, -0.385, -0.044), ivory, transform=console_point)

    box("throttle_quadrant", (-0.95, -0.37, -0.43), (-0.67, -0.28, -0.12), rubber)
    for index, x in enumerate((-0.86, -0.75)):
        box(f"throttle_slot_{index}", (x - 0.016, -0.279, -0.41),
            (x + 0.016, -0.274, -0.14), metal)
        beam(f"throttle_lever_{index}", (x, -0.28, -0.26), (x, -0.15, -0.32),
             0.014, silver, capped=True)
        box(f"throttle_handle_{index}", (x - 0.043, -0.17, -0.36),
            (x + 0.043, -0.12, -0.28), rubber)
        box(f"throttle_handle_mark_{index}", (x - 0.03, -0.119, -0.325),
            (x + 0.03, -0.116, -0.315), ivory)

    box("rear_bulkhead", (-1.02, -0.69, 1.00), (1.02, 0.16, 1.05), shell)
    box("floor", (-1.02, -0.74, -1.18), (1.02, -0.69, 1.05), rubber)
    for side in (-1, 1):
        x = side * 0.73
        box(f"rear_access_panel_{side}", (x - 0.18, -0.36, 0.978),
            (x + 0.18, 0.05, 0.997), panel)
        for rib in range(3):
            y = -0.27 + rib * 0.10
            box(f"rear_panel_rib_{side}_{rib}", (x - 0.14, y, 0.964),
                (x + 0.14, y + 0.018, 0.98), metal)

    def seat_point(point):
        x, y, z = point
        return (x, y, z + 0.16 * (y + 0.55))

    box("seat_pan", (-0.38, -0.62, -0.14), (0.38, -0.57, 0.60), metal)
    box("seat_cushion", (-0.33, -0.57, -0.10), (0.33, -0.48, 0.50), olive)
    box("seat_shell", (-0.38, -0.58, 0.57), (0.38, 0.15, 0.64), metal, transform=seat_point)
    box("seat_back", (-0.33, -0.50, 0.49), (0.33, 0.12, 0.57), olive, transform=seat_point)
    box("headrest_shell", (-0.24, 0.12, 0.53), (0.24, 0.39, 0.69), metal, transform=seat_point)
    box("headrest", (-0.21, 0.15, 0.48), (0.21, 0.36, 0.54), rubber, transform=seat_point)
    for name, side in (("left", -1), ("right", 1)):
        x = side * 0.375
        box(f"seat_{name}_rail", (x - 0.025, -0.69, 0.72),
            (x + 0.025, 0.39, 0.77), metal)
        box(f"seat_{name}_bolster", (x - 0.04, -0.56, -0.08),
            (x + 0.04, -0.41, 0.52), shell)
        x = side * 0.16
        box(f"harness_{name}_shoulder", (x - 0.034, -0.43, 0.477),
            (x + 0.034, 0.14, 0.49), webbing, transform=seat_point)
        box(f"harness_{name}_adjuster", (x - 0.045, -0.12, 0.464),
            (x + 0.045, -0.065, 0.478), silver, transform=seat_point)
        x0, x1 = sorted((side * 0.035, side * 0.33))
        box(f"harness_{name}_lap", (x0, -0.479, 0.11), (x1, -0.465, 0.19), webbing)
    box("harness_buckle", (-0.046, -0.465, 0.10), (0.046, -0.44, 0.20), silver)
    box("harness_release", (-0.025, -0.439, 0.125), (0.025, -0.432, 0.175), rubber)
    for index in range(3):
        z = 0.24 + index * 0.075
        box(f"cushion_seam_{index}", (-0.28, -0.48, z), (0.28, -0.477, z + 0.006), shell)
    for name, x in (("left", -0.17), ("right", 0.17)):
        box(name + "_pedal", (x - 0.10, -0.65, -0.95), (x + 0.10, -0.60, -0.76), metal)
        for index in range(3):
            z = -0.92 + index * 0.05
            box(f"{name}_pedal_tread_{index}", (x - 0.085, -0.60, z),
                (x + 0.085, -0.594, z + 0.012), rubber)
    box("control_boot", (-0.09, -0.69, -0.54), (0.09, -0.60, -0.36), rubber)
    beam("control_column", (0, -0.62, -0.45), (0, -0.48, -0.44), 0.028, metal, capped=True)
    box("control_grip", (-0.045, -0.51, -0.49), (0.045, -0.40, -0.40), rubber)
    box("control_thumb_hat", (-0.026, -0.40, -0.46), (0.002, -0.385, -0.43), metal)

    for frame_name, z in (("front", -0.72), ("rear", 0.82)):
        arch = [(0.96 * math.cos(i * math.pi / 12),
                 0.04 + 0.72 * math.sin(i * math.pi / 12), z) for i in range(13)]
        for i, (start, end) in enumerate(zip(arch, arch[1:])):
            beam(f"canopy_{frame_name}_{i:02d}", start, end)
    beam("canopy_left_sill", (-0.96, 0.04, -0.92), (-0.96, 0.04, 1.02), 0.025)
    beam("canopy_right_sill", (0.96, 0.04, -0.92), (0.96, 0.04, 1.02), 0.025)
    beam("canopy_roof", (0, 0.76, -0.72), (0, 0.76, 0.82), 0.014)

    document["scenes"][0]["nodes"].append(len(document["nodes"]))
    document["nodes"].append({"name": "pilot", "camera": 0})
    document["buffers"][0]["byteLength"] = len(binary)
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as stream:
        stream.write(struct.pack("<4sII", b"glTF", 2, 28 + len(encoded) + len(binary)))
        stream.write(struct.pack("<I4s", len(encoded), b"JSON"))
        stream.write(encoded)
        stream.write(struct.pack("<I4s", len(binary), b"BIN\0"))
        stream.write(binary)
    print(f"Created {output}: {len(document['meshes'])} meshes, embedded cockpit PNG")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--console-texture", type=Path)
    args = parser.parse_args()
    create_cockpit(args.image, args.output, args.console_texture)
