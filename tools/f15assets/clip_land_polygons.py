"""Clip a zipped land shapefile to a campaign region, retaining provenance."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile

import shapefile
from shapely.geometry import box, mapping, shape
from shapely.ops import unary_union


def clip_land(source, bounds):
    west, south, east, north = bounds
    if not (-180 <= west < east <= 180 and -90 <= south < north <= 90):
        raise ValueError("Bounds must be west south east north in geographic degrees")
    region = box(west, south, east, north)
    polygons = []
    with zipfile.ZipFile(source) as archive:
        components = {}
        for extension in ("shp", "shx", "dbf"):
            candidates = [name for name in archive.namelist() if name.lower().endswith("." + extension)]
            if len(candidates) != 1:
                raise ValueError(f"Expected one .{extension} component")
            components[extension] = io.BytesIO(archive.read(candidates[0]))
        reader = shapefile.Reader(**components)
        for record in reader.iterShapes():
            left, bottom, right, top = record.bbox
            if right < west or left > east or top < south or bottom > north:
                continue
            geometry = shape(record.__geo_interface__)
            if not geometry.is_valid:
                geometry = geometry.buffer(0)
            clipped = geometry.intersection(region)
            if not clipped.is_empty:
                polygons.append(clipped)
        reader.close()
    if not polygons:
        raise ValueError("No land intersects the requested region")
    return mapping(unary_union(polygons))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bounds", nargs=4, type=float, required=True,
                        metavar=("WEST", "SOUTH", "EAST", "NORTH"))
    parser.add_argument("--source-url", required=True)
    parser.add_argument("--license", required=True)
    args = parser.parse_args()
    geometry = clip_land(args.source, args.bounds)
    document = {
        "type": "FeatureCollection",
        "bbox": args.bounds,
        "features": [{"type": "Feature", "geometry": geometry, "properties": {
            "surface": "land", "source": args.source_url, "license": args.license,
            "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        }}],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, separators=(",", ":")) + "\n")
    print(f"Wrote clipped land polygons to {args.output}")


if __name__ == "__main__":
    main()
