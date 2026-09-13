"""Generate route options from world slots and optional saved Overpass JSON."""

import argparse
import json
from pathlib import Path

from f15assets.osm_routes import generate_routes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("world", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--osm", type=Path, help="Saved Overpass response using out center")
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    response = json.loads(args.osm.read_text()) if args.osm else None
    routes = generate_routes(json.loads(args.config.read_text()),
                             json.loads(args.world.read_text()), response, args.count, args.seed)
    # Refuse to overwrite campaign inputs or an earlier selection accidentally.
    with args.output.open("x") as stream:
        stream.write(json.dumps(routes, indent=2) + "\n")
    print(f"Generated {len(routes['routes'])} of {routes['eligible_routes']} eligible routes")


if __name__ == "__main__":
    main()
