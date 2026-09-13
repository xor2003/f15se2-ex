"""Export complete-person briefing poses from a masked body and existing arms."""

import argparse
import math
from pathlib import Path

from PIL import Image, ImageChops

from arm_composite import ARM_POSITIONS, load_rgba
from sprite_atlas import sprite_regions


def export_people(wall_path, arms_path, mask_path, room_path, destination):
    """Separate the body with an explicit mask, then combine each complete pose."""
    wall = load_rgba(wall_path)
    arms = load_rgba(arms_path)
    room = load_rgba(room_path)
    with Image.open(mask_path) as image:
        mask = image.convert("L")
    if mask.size != wall.size:
        raise ValueError("Body mask must have the same dimensions as WALL.png")
    if mask.getextrema() == (0, 0):
        raise ValueError("Body mask is empty: paint the body white, background black")
    if room.getchannel("A").getextrema()[0] != 255:
        raise ValueError("The room-only background must be opaque")

    # A manual mask is necessary: black clothing is not black background, and
    # pixels hidden behind the original body cannot be recovered from WALL.png.
    wall.putalpha(ImageChops.multiply(wall.getchannel("A"), mask))
    scale = math.ceil(max(image.width / 320 for image in (wall, arms, room)))
    scale = max(scale, math.ceil(max(image.height / 200 for image in (wall, arms, room))))
    size = (320 * scale, 200 * scale)
    body = wall.resize(size, Image.Resampling.NEAREST)
    arms = arms.resize(size, Image.Resampling.NEAREST)
    room = room.resize(size, Image.Resampling.NEAREST)
    poses = []
    # Menu animation positions 0..6 use legacy atlas slots 1..7, not slots 0..6.
    for slot, (_, rect) in enumerate(sprite_regions("ARMPIECE")[1:8], start=1):
        x, y, width, height = rect
        arm = arms.crop((x * scale, y * scale,
                         (x + width) * scale, (y + height) * scale))
        if arm.getchannel("A").getextrema()[0] == 255:
            raise ValueError(f"Arm slot {slot} has no transparency; prepare its alpha first")
        pose = body.copy()
        dest_x, dest_y = ARM_POSITIONS[slot]
        pose.alpha_composite(arm, (dest_x * scale, dest_y * scale))
        poses.append(pose)

    destination.mkdir(parents=True, exist_ok=False)
    room.save(destination / "room.png")
    for frame, pose in enumerate(poses):
        pose.save(destination / f"{frame}.png")
    print(f"Exported room and 7 complete-person poses to {destination}")


def main():
    """Create a runtime-ready directory without modifying source assets."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wall", type=Path)
    parser.add_argument("arms", type=Path)
    parser.add_argument("body_mask", type=Path)
    parser.add_argument("room", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        export_people(args.wall, args.arms, args.body_mask, args.room, args.destination)
    except (OSError, ValueError) as error:
        parser.exit(1, f"briefing-person: {error}\n")


if __name__ == "__main__":
    main()
