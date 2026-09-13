"""Edit the briefing body and arm together as a layered OpenRaster image."""

import argparse
import io
import math
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from PIL import Image

from sprite_atlas import sprite_regions


ARM_POSITIONS = ((62, 28), (62, 47), (61, 53), (62, 54),
                 (62, 55), (62, 56), (63, 57), (62, 56))


def png_bytes(image):
    """Encode an OpenRaster layer without temporary files."""
    stream = io.BytesIO()
    image.save(stream, format="PNG")
    return stream.getvalue()


def load_rgba(path):
    """Detach an RGBA image from its source file."""
    with Image.open(path) as image:
        return image.convert("RGBA")


def export(wall_path, arm_path, directory):
    """Export all poses on a common logical grid without downsampling art."""
    wall = load_rgba(wall_path)
    arms = load_rgba(arm_path)
    scale = math.ceil(max(wall.width / 320, wall.height / 200,
                          arms.width / 320, arms.height / 200))
    size = (320 * scale, 200 * scale)
    wall = wall.resize(size, Image.Resampling.NEAREST)
    arms = arms.resize(size, Image.Resampling.NEAREST)
    directory.mkdir(parents=True, exist_ok=False)
    wall.save(directory / "wall-base.png")
    arms.save(directory / "arms-base.png")
    for index, (name, rect) in enumerate(sprite_regions("ARMPIECE")[:8]):
        x, y, width, height = rect
        arm = arms.crop((x * scale, y * scale,
                         (x + width) * scale, (y + height) * scale))
        dest_x, dest_y = (value * scale for value in ARM_POSITIONS[index])
        preview = wall.copy()
        preview.alpha_composite(arm, (dest_x, dest_y))
        preview.save(directory / f"{name}-combined.png")
        root = ET.Element("image", w=str(size[0]), h=str(size[1]), name=name)
        stack = ET.SubElement(root, "stack")
        for layer_name, filename, lx, ly in (
                ("Arm", "data/arm.png", dest_x, dest_y),
                ("Body and room", "data/wall.png", 0, 0)):
            ET.SubElement(stack, "layer", name=layer_name, src=filename,
                          x=str(lx), y=str(ly), opacity="1.0",
                          visibility="visible", **{"composite-op": "svg:src-over"})
        with zipfile.ZipFile(directory / f"{name}.ora", "w") as archive:
            archive.writestr("mimetype", "image/openraster")
            archive.writestr("stack.xml", ET.tostring(root))
            archive.writestr("data/arm.png", png_bytes(arm))
            archive.writestr("data/wall.png", png_bytes(wall))
            archive.writestr("mergedimage.png", png_bytes(preview))
    print(f"Exported 8 layered body-and-arm poses to {directory}")


def rebuild(project, edited_path, pose, destination):
    """Split one edited pose into the shared wall and its original arm slot."""
    arms = load_rgba(project / "arms-base.png")
    scale = arms.width // 320
    x, y, width, height = sprite_regions("ARMPIECE")[pose][1]
    dest_x, dest_y = (value * scale for value in ARM_POSITIONS[pose])
    with zipfile.ZipFile(edited_path) as archive:
        root = ET.fromstring(archive.read("stack.xml"))
        if (int(root.attrib["w"]), int(root.attrib["h"])) != arms.size:
            raise ValueError("Keep the exported canvas dimensions")
        layers = list(root.iter("layer"))
        if len(layers) != 2 or {layer.get("name") for layer in layers} != {"Arm", "Body and room"}:
            raise ValueError("Keep exactly the Arm and Body and room layers")
        images = {}
        for layer in layers:
            if (layer.get("opacity", "1") not in ("1", "1.0") or
                    layer.get("visibility", "visible") != "visible" or
                    layer.get("composite-op", "svg:src-over") != "svg:src-over"):
                raise ValueError("Layers must be visible, fully opaque and normal blend")
            with Image.open(io.BytesIO(archive.read(layer.attrib["src"]))) as image:
                canvas = Image.new("RGBA", arms.size)
                canvas.paste(image.convert("RGBA"),
                             (int(layer.get("x", "0")), int(layer.get("y", "0"))))
            images[layer.attrib["name"]] = canvas
    arm = images["Arm"]
    bounds = arm.getbbox()
    slot = (dest_x, dest_y, dest_x + width * scale, dest_y + height * scale)
    if bounds and not (slot[0] <= bounds[0] and slot[1] <= bounds[1] and
                       bounds[2] <= slot[2] and bounds[3] <= slot[3]):
        raise ValueError("Arm artwork extends outside the game's sprite rectangle")
    # Paste, not composite: erasing a piece of the arm must erase atlas pixels.
    arms.paste(arm.crop(slot), (x * scale, y * scale))
    destination.mkdir(parents=True, exist_ok=False)
    arms.save(destination / "ARMPIECE.png")
    images["Body and room"].save(destination / "WALL.png")
    print(f"Rebuilt WALL.png and ARMPIECE.png in {destination}")


def main():
    """Dispatch export or rebuild without overwriting existing directories."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    unpack = commands.add_parser("export")
    unpack.add_argument("wall", type=Path)
    unpack.add_argument("arms", type=Path)
    unpack.add_argument("directory", type=Path)
    pack = commands.add_parser("rebuild")
    pack.add_argument("project", type=Path)
    pack.add_argument("edited", type=Path)
    pack.add_argument("destination", type=Path)
    pack.add_argument("--pose", type=int, choices=range(8), required=True)
    args = parser.parse_args()
    try:
        if args.command == "export":
            export(args.wall, args.arms, args.directory)
        else:
            rebuild(args.project, args.edited, args.pose, args.destination)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, ET.ParseError) as error:
        parser.exit(1, f"arm-composite: {error}\n")


if __name__ == "__main__":
    main()
