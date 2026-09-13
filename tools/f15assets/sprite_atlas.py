"""Extract editable PNG sprites and rebuild their fixed-layout game atlas."""

import argparse
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw


LOGICAL_SIZE = (320, 200)


def sprite_regions(profile):
    """Return named logical rectangles from the game's sprite draw tables."""
    if profile == "ARMPIECE":
        # stdata.c: armSrcX/Y, armBlitW/H and medalSpriteX/Y/Width.
        arms = zip(
            (1, 83, 217, 164, 1, 1, 191, 266),
            (0, 0, 0, 46, 62, 124, 106, 83),
            (82, 93, 102, 102, 104, 92, 75, 53),
            (62, 46, 37, 45, 62, 75, 93, 116),
        )
        medals = zip((130, 113, 129, 112, 111, 161, 159),
                     (128, 128, 179, 179, 145, 145, 162),
                     (9, 11, 11, 13, 47, 11, 15))
        return [(f"arm_{i:02d}", tuple(rect)) for i, rect in enumerate(arms)] + [
            (f"medal_{i:02d}", (x, y, width, 16))
            for i, (x, y, width) in enumerate(medals)
        ]
    if profile == "DBICONS":
        # endata.c popupSpriteX/Y; endtypes.h POPUP_WIDTH/HEIGHT.
        return [(f"debrief_{i:02d}", ((i % 6) * 48, (i // 6) * 40, 48, 40))
                for i in range(18)]
    # egui.c/eghudr.c/egtarget.c: strips are kept whole, never auto-trimmed.
    regions = [("heading_tape", (0, 0, 141, 31)),
               ("gun_reticle_small", (147, 20, 13, 9)),
               ("gun_reticle", (130, 38, 25, 15)),
               ("seeker", (145, 4, 13, 11)),
               ("helmet", (209, 0, 111, 47)),
               ("rear_strip", (125, 54, 195, 2)),
               ("pause", (113, 55, 12, 7))]
    for row, kind in enumerate(("level", "below", "above", "symbol")):
        regions.extend((f"radar_{kind}_{column:02d}",
                        (column * 8 + 1, row * 8 + 31, 7, 7))
                       for column in range(16))
    regions.extend((f"explosion_{i:02d}", (i * 32, 63, 32, 32)) for i in range(8))
    regions.extend((f"marker_{i:02d}", (164 + i * 4, 0, 4, 4)) for i in range(8))
    regions.extend((f"ownship_{i:02d}", (164 + i * 4, 4, 4, 4)) for i in range(8))
    return regions


def pixel_box(rect, size):
    """Map shared logical boundaries identically, including noninteger scales."""
    x, y, width, height = rect
    return (x * size[0] // LOGICAL_SIZE[0], y * size[1] // LOGICAL_SIZE[1],
            (x + width) * size[0] // LOGICAL_SIZE[0],
            (y + height) * size[1] // LOGICAL_SIZE[1])


def extract(source, destination, profile, black_transparent=False):
    """Create an editing directory without replacing an existing project."""
    with Image.open(source) as image:
        atlas = image.convert("RGBA")
    if atlas.width < 320 or atlas.height < 200:
        raise ValueError("Atlas must be at least 320x200 pixels")
    if black_transparent:
        # Never infer transparency from the corner: F15 has white art there.
        atlas.putdata([(r, g, b, 0 if (r, g, b) == (0, 0, 0) else a)
                       for r, g, b, a in atlas.getdata()])
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(source, destination / "original.png")
    atlas.save(destination / "base.png")
    guide = atlas.copy()
    draw = ImageDraw.Draw(guide)
    sprites = []
    for name, rect in sprite_regions(profile):
        box = pixel_box(rect, atlas.size)
        filename = name + ".png"
        atlas.crop(box).save(destination / filename)
        sprites.append({"name": name, "file": filename,
                        "logical_xywh": list(rect), "pixel_box": list(box)})
        left, top, right, bottom = box
        draw.rectangle((left, top, right - 1, bottom - 1), outline="red")
    guide.save(destination / "layout-preview.png")
    manifest = {"version": 1, "profile": profile, "size": list(atlas.size),
                "logical_size": list(LOGICAL_SIZE),
                "black_transparent": black_transparent, "sprites": sprites}
    (destination / "atlas.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Extracted {len(sprites)} crops to {destination}")


def rebuild(project, output):
    """Apply edited pixels, rejecting inconsistent edits in overlapping crops."""
    manifest = json.loads((project / "atlas.json").read_text())
    if manifest["version"] != 1:
        raise ValueError("Unsupported atlas project version")
    with Image.open(project / "base.png") as image:
        atlas = image.convert("RGBA")
    if list(atlas.size) != manifest["size"]:
        raise ValueError("base.png dimensions differ from atlas.json")
    baseline = atlas.copy()
    changes = {}
    for sprite in manifest["sprites"]:
        filename = Path(sprite["file"])
        if filename.name != str(filename) or filename.suffix.lower() != ".png":
            raise ValueError("Sprite filenames must be local PNG filenames")
        box = tuple(sprite["pixel_box"])
        left, top, right, bottom = box
        if not (0 <= left < right <= atlas.width and 0 <= top < bottom <= atlas.height):
            raise ValueError(f"Invalid rectangle for {filename}")
        with Image.open(project / filename) as image:
            edited = image.convert("RGBA")
        original = baseline.crop(box)
        if edited.size != original.size:
            raise ValueError(f"{filename}: expected {original.size}, got {edited.size}; "
                             "keep the original canvas size, position and orientation")
        for index, (before, after) in enumerate(zip(original.getdata(), edited.getdata())):
            if before == after:
                continue
            point = (left + index % edited.width, top + index // edited.width)
            previous = changes.get(point)
            if previous is not None and previous[0] != after:
                raise ValueError(f"Conflicting edits at {point}: {previous[1]} and {filename}")
            changes[point] = (after, str(filename))
    # Replace pixels rather than alpha-compositing: transparent edits erase art.
    for point, (color, _) in changes.items():
        atlas.putpixel(point, color)
    if output.resolve().is_relative_to(project.resolve()):
        raise ValueError("Write the rebuilt atlas outside the editing directory")
    if output.exists():
        raise ValueError(f"Output already exists: {output}; choose a new filename")
    if not changes and not manifest["black_transparent"]:
        # Keep the palette, metadata and exact PNG bytes on an untouched round trip.
        shutil.copyfile(project / "original.png", output)
    else:
        atlas.save(output)
    print(f"Rebuilt {output}; {len(changes)} edited pixels")


def main():
    """Run the standalone sprite editing command."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    unpack = commands.add_parser("extract")
    unpack.add_argument("source", type=Path)
    unpack.add_argument("destination", type=Path)
    unpack.add_argument("--profile", choices=("F15", "ARMPIECE", "DBICONS"))
    unpack.add_argument("--black-transparent", action="store_true",
                        help="Make exact black transparent, including black inside sprites")
    pack = commands.add_parser("rebuild")
    pack.add_argument("project", type=Path)
    pack.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "extract":
            profile = args.profile or args.source.stem.upper()
            if profile not in ("F15", "ARMPIECE", "DBICONS"):
                parser.error("Use --profile F15, ARMPIECE or DBICONS")
            extract(args.source, args.destination, profile, args.black_transparent)
        else:
            rebuild(args.project, args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"sprite-atlas: {error}\n")


if __name__ == "__main__":
    main()
