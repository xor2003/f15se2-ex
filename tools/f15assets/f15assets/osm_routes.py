"""Import saved Overpass locations and generate four-point mission routes."""

import itertools
import math
import random


def parse_locations(response):
    """Read nodes and `out center` ways/relations without inventing missing centers."""
    locations = {}
    for element in response["elements"]:
        kind = element.get("type")
        if kind not in ("node", "way", "relation"):
            continue
        center = element if kind == "node" else element.get("center")
        if center is None:
            continue
        longitude, latitude = float(center["lon"]), float(center["lat"])
        if not (math.isfinite(longitude) and math.isfinite(latitude) and
                -180 <= longitude <= 180 and -90 <= latitude <= 90):
            raise ValueError("Invalid OSM coordinates")
        reference = f"{kind}/{int(element['id'])}"
        tags = element.get("tags", {})
        locations[reference] = {
            "lon": longitude, "lat": latitude,
            "name": tags.get("name:en", tags.get("name", reference)),
            "source": f"https://www.openstreetmap.org/{reference}",
        }
    return locations


def world_coordinates(longitude, latitude, corners, world_max=32767):
    """Invert a TL/TR/BL/BR bilinear map; reject singular or outside locations."""
    if len(corners) != 4 or any(len(corner) != 2 for corner in corners):
        raise ValueError("Expected four longitude/latitude corners: TL, TR, BL, BR")
    if not all(math.isfinite(v) for point in corners for v in point):
        raise ValueError("Map corners must be finite")
    if not math.isfinite(longitude) or not math.isfinite(latitude):
        raise ValueError("Location must be finite")
    origin, right, bottom, opposite = corners
    horizontal = [right[a] - origin[a] for a in range(2)]
    vertical = [bottom[a] - origin[a] for a in range(2)]
    cross = [opposite[a] - right[a] - bottom[a] + origin[a] for a in range(2)]
    u = v = 0.5
    for _ in range(20):
        error = [origin[a] + horizontal[a]*u + vertical[a]*v + cross[a]*u*v - target
                 for a, target in enumerate((longitude, latitude))]
        if max(map(abs, error)) < 1e-10:
            break
        du = [horizontal[a] + cross[a]*v for a in range(2)]
        dv = [vertical[a] + cross[a]*u for a in range(2)]
        determinant = du[0]*dv[1] - dv[0]*du[1]
        if abs(determinant) < 1e-12:
            raise ValueError("Singular map projection")
        u -= (error[0]*dv[1] - dv[0]*error[1]) / determinant
        v -= (du[0]*error[1] - error[0]*du[1]) / determinant
    else:
        raise ValueError("Map projection did not converge")
    if not (-1e-9 <= u <= 1+1e-9 and -1e-9 <= v <= 1+1e-9):
        raise ValueError(f"Location outside map: {longitude}, {latitude}")
    return round(min(1, max(0, u))*world_max), round(min(1, max(0, v))*world_max)


def resolve_point(specification, world, locations, corners):
    """Use a world slot or an explicitly chosen OSM reference, not inferred allegiance."""
    point = dict(specification)
    if "osm" in point:
        location = locations[point["osm"]]
        point.update(location)
        point["x_coord"], point["y_coord"] = world_coordinates(
            location["lon"], location["lat"], corners)
    elif "object_slot" in point:
        slot = point["object_slot"]
        if not isinstance(slot, int) or not 0 <= slot < len(world["world_objects"]):
            raise ValueError(f"Invalid world object slot: {slot}")
        obj = world["world_objects"][slot]
        point.update(x_coord=obj["x_coord"], y_coord=obj["y_coord"])
        point.setdefault("name", obj.get("scenario_label", f"Object {slot}"))
    else:
        raise ValueError("Each candidate needs an object_slot or osm reference")
    return point


def generate_routes(config, world, response=None, count=20, seed=1):
    """Sample unique start/target/target/end combinations with bounded memory."""
    if not 1 <= count <= 10000:
        raise ValueError("Route count must be between 1 and 10000")
    locations = parse_locations(response) if response is not None else {}
    pools = [[resolve_point(point, world, locations, config.get("corners", []))
              for point in config[role]] for role in ("starts", "targets", "ends")]
    if not pools[0] or len(pools[1]) < 2 or not pools[2]:
        raise ValueError("Need a start, two targets and an end")
    for pool in pools:
        identities = [(p.get("object_slot"), p.get("osm")) for p in pool]
        if len(set(identities)) != len(identities):
            raise ValueError("Duplicate candidate in route pool")
    max_leg = float(config.get("max_leg_world_units", 32767))
    if not math.isfinite(max_leg) or max_leg <= 0:
        raise ValueError("max_leg_world_units must be positive and finite")
    rng = random.Random(seed)
    selected = []
    eligible = 0
    for start, (first, second), end in itertools.product(
            pools[0], itertools.permutations(pools[1], 2), pools[2]):
        points = [start, first, second, end]
        coordinates = [(p["x_coord"], p["y_coord"]) for p in points]
        # Start and end may coincide for a round trip; targets must be distinct
        # from each other and from both bases.
        if (coordinates[1] == coordinates[2] or
                any(target in (coordinates[0], coordinates[3]) for target in coordinates[1:3])):
            continue
        lengths = [math.dist(a, b) for a, b in zip(coordinates, coordinates[1:])]
        if max(lengths) > max_leg:
            continue
        route = {"points": [dict(point, role=role) for point, role in zip(
            points, ("start", "primary", "secondary", "end"))],
            "length_world_units": round(sum(lengths), 2)}
        eligible += 1
        if len(selected) < count:
            selected.append(route)
        else:
            index = rng.randrange(eligible)
            if index < count:
                selected[index] = route
    if len(selected) < count:
        raise ValueError(f"Requested {count} routes; only {eligible} satisfy the constraints")
    for index, route in enumerate(selected, 1):
        route["id"] = f"route_{index:03d}"
    return {"format": "F15SE2_FOUR_POINT_ROUTES", "format_version": 1,
            "seed": seed, "eligible_routes": eligible, "routes": selected,
            "osm_attribution": "OpenStreetMap contributors",
            "osm_timestamp": (response or {}).get("osm3s", {}).get("timestamp_osm_base"),
            "notes": "Authoring options; OSM locations do not assign factions or move world objects."}
