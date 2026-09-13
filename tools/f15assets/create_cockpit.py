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


def create_cockpit(image_path, output):
    """Embed the existing PNG and emit a self-contained glTF 2.0 binary."""
    png = image_path.read_bytes()
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Cockpit texture must be a PNG")
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

    def add_texture(filename):
        data = (image_path.parent / filename).read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"{filename} must be a PNG")
        image_index = len(document["images"])
        document["images"].append({"name": filename, "mimeType": "image/png", "bufferView": buffer_view(data)})
        texture_index = len(document["textures"])
        document["textures"].append({"source": image_index, "sampler": 0})
        return texture_index

    def rotate_y(point, degrees):
        angle = math.radians(degrees)
        x, y, z = point
        return (x * math.cos(angle) + z * math.sin(angle), y,
                -x * math.sin(angle) + z * math.cos(angle))

    def box(name, minimum, maximum, color, texture=None, uv_rect=(0, 0, 1, 1)):
        x0, y0, z0 = minimum
        x1, y1, z1 = maximum
        corners = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                   (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        vertices, texcoords = [], []
        u0, v0, u1, v1 = uv_rect
        for face in ((0, 1, 2, 3), (5, 4, 7, 6), (4, 0, 3, 7),
                     (1, 5, 6, 2), (3, 2, 6, 7), (4, 5, 1, 0)):
            quad(vertices, texcoords, [corners[i] for i in face],
                 [(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
        add_mesh(name, vertices, texcoords, solid=texture is None, color=color,
                 texture=texture if texture is not None else 0)

    # Side-panel relief uses only cabin pixels, never the painted sky/ground.
    # Rotate the geometry into aircraft space; the renderer rotates the camera.
    for name, filename, yaw in (("left", "256LEFT.png", 90),
                                ("right", "256RIGHT.png", -90)):
        texture = add_texture(filename)
        vertices, texcoords = [], []
        for top in range(96, 200, 4):
            for left in range(0, 320, 4):
                corners = [(left, top), (left + 4, top), (left + 4, top + 4), (left, top + 4)]
                def side_point(x, y):
                    # Upper vertical instruments, then an inward-sloping console.
                    depth = 1.0 if y <= 148 else 1.0 - 0.22 * (y - 148) / 52
                    return rotate_y(position(x, y, depth), yaw)
                quad(vertices, texcoords, [side_point(x, y) for x, y in corners],
                     [(x / 320, y / 200) for x, y in corners])
        add_mesh(name + "_console", vertices, texcoords, texture=texture)
        side = -1 if name == "left" else 1
        low_x, high_x = sorted((side * 0.72, side * 1.02))
        box(name + "_console_structure", (low_x, -0.68, -0.92),
            (high_x, -0.47, 0.92), [0.035, 0.045, 0.05, 1])

    rear_texture = add_texture("256REAR.png")
    # 256REAR depicts the pilot from outside. Use its metal and fabric areas for
    # actual cabin parts rather than putting a second pilot behind the camera.
    box("rear_bulkhead", (-1.02, -0.69, 1.00), (1.02, 0.16, 1.05), None,
        texture=rear_texture, uv_rect=(0.025, 0.51, 0.19, 0.66))
    box("floor", (-1.02, -0.74, -1.18), (1.02, -0.69, 1.05), [0.035, 0.04, 0.045, 1])
    box("seat_cushion", (-0.34, -0.58, -0.10), (0.34, -0.48, 0.53), None,
        texture=rear_texture, uv_rect=(0.10, 0.82, 0.23, 0.94))
    box("seat_back", (-0.34, -0.55, 0.53), (0.34, 0.10, 0.64), None,
        texture=rear_texture, uv_rect=(0.10, 0.82, 0.23, 0.94))
    box("headrest", (-0.21, 0.10, 0.55), (0.21, 0.36, 0.70), [0.055, 0.06, 0.065, 1])
    box("seat_left_rail", (-0.40, -0.69, 0.60), (-0.35, 0.35, 0.66), [0.20, 0.23, 0.25, 1])
    box("seat_right_rail", (0.35, -0.69, 0.60), (0.40, 0.35, 0.66), [0.20, 0.23, 0.25, 1])
    box("left_pedal", (-0.27, -0.65, -0.95), (-0.07, -0.60, -0.76), [0.14, 0.16, 0.17, 1])
    box("right_pedal", (0.07, -0.65, -0.95), (0.27, -0.60, -0.76), [0.14, 0.16, 0.17, 1])
    box("control_column", (-0.035, -0.69, -0.48), (0.035, -0.29, -0.42), [0.10, 0.12, 0.13, 1])
    box("control_grip", (-0.055, -0.31, -0.50), (0.055, -0.20, -0.40), [0.025, 0.03, 0.035, 1])

    def beam(name, start, end, radius=0.018):
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
        add_mesh(name, vertices, texcoords, solid=True, color=[0.18, 0.21, 0.23, 1])

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
    args = parser.parse_args()
    create_cockpit(args.image, args.output)
