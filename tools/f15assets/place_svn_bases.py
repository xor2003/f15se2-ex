"""Place SVN land bases using a saved Overpass response."""

import json
import sys
from pathlib import Path
from f15assets.osm_routes import parse_locations, world_coordinates as project_location


ROOT = Path(__file__).resolve().parents[2] / "converted_assets_all" / "SVN"
BASES = (
    (27, "way", 217455233), (28, "relation", 20136775),
    (29, "way", 240659354), (30, "way", 585991360),
    (31, "way", 217466613), (32, "way", 217476265),
    (33, "way", 1081333880), (34, "way", 217534447),
)


def world_coordinates(longitude, latitude):
    return project_location(longitude, latitude, (
        (97.843, 15.059), (106.060, 23.514),
        (105.932, 7.412), (114.149, 16.180)))


def main(response_path):
    response = json.loads(Path(response_path).read_text())
    elements = parse_locations(response)
    world_path = ROOT / "SVN.WLD.json"
    world = json.loads(world_path.read_text())
    records = []
    for slot, kind, osm_id in BASES:
        element = elements[f"{kind}/{osm_id}"]
        longitude, latitude = element["lon"], element["lat"]
        x, y = world_coordinates(longitude, latitude)
        name = element["name"]
        obj = world["world_objects"][slot]
        obj.update(x_coord=x, y_coord=y, scenario_label=name)
        anchor = {"lon": longitude, "lat": latitude,
                  "source": f"https://www.openstreetmap.org/{kind}/{osm_id}"}
        obj["scenario_real_world_anchor"] = anchor
        for annotation in world["target_annotations"]:
            if annotation["object_index"] == slot:
                annotation.update(name=name, coordinates={"x_coord": x, "y_coord": y},
                                  real_world_anchor=anchor)
        records.append({"slot": slot, "name": name, **anchor, "x": x, "y": y})
    assert len(world["world_objects"]) == 37
    assert len(world["flight_units"]) == 11
    geography = ROOT / "geography"
    geography.mkdir(exist_ok=True)
    provenance = {"attribution": "OpenStreetMap contributors", "license": "ODbL-1.0",
                  "license_url": "https://www.openstreetmap.org/copyright",
                  "osm_timestamp": response.get("osm3s", {}).get("timestamp_osm_base"),
                  "notes": "Current geographic anchors for a fictional campaign.", "bases": records}
    (geography / "osm-bases.json").write_text(json.dumps(provenance, indent=2) + "\n")
    world_path.write_text(json.dumps(world, indent=2) + "\n")
    print("Placed 8 land bases; preserved 2 carrier bases, 37 world records and 11 flights")


if __name__ == "__main__":
    main(sys.argv[1])
