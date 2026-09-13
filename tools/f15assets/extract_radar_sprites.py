"""Extract non-aircraft radar icons from F15.png without resampling their pixels."""

import argparse
from pathlib import Path

from PIL import Image


def extract(source, destination):
    """Export atlas row 3, excluding the independently customized ownship icon."""
    paths = [destination / f"atlas_{column:02d}.png" for column in range(1, 16)]
    existing = [path for path in paths if path.exists()]
    if existing:
        raise FileExistsError(f"Refusing to overwrite customized sprite: {existing[0]}")
    with Image.open(source) as atlas:
        rgba = atlas.convert("RGBA")
        # Explicit alpha wins. Legacy indexed atlases key index zero; opaque RGB
        # atlases key black, not the top-left pixel (which can be white artwork).
        if "A" not in atlas.getbands() and "transparency" not in atlas.info:
            pixels = list(rgba.getdata())
            if atlas.mode == "P":
                transparent = [index == 0 for index in atlas.getdata()]
            else:
                transparent = [pixel[:3] == (0, 0, 0) for pixel in pixels]
            rgba.putdata([(*pixel[:3], 0 if keyed else 255)
                          for pixel, keyed in zip(pixels, transparent)])
        crops = []
        for column in range(1, 16):
            # blitGaugeSprite: x=column*8+1, y=row*8+31, width=height=7.
            x, y = column * 8 + 1, 55
            bounds = (round(x * atlas.width / 320), round(y * atlas.height / 200),
                      round((x + 7) * atlas.width / 320), round((y + 7) * atlas.height / 200))
            if bounds[2] <= bounds[0] or bounds[3] <= bounds[1]:
                raise ValueError("Atlas resolution is too small for radar sprites")
            crops.append(rgba.crop(bounds))
        destination.mkdir(parents=True, exist_ok=True)
        for path, sprite in zip(paths, crops):
            sprite.save(path)
            print(path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="F15.png atlas")
    parser.add_argument("destination", type=Path, help="Campaign flight/radar directory")
    args = parser.parse_args()
    extract(args.source, args.destination)
