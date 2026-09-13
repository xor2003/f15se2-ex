#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import re
import shlex
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    from tools.f15assets.f15assets import (
        decode_pic_asset,
        decode_title640_pic_asset,
        parse_3d3,
        export_3d3_shape_gltfs,
        export_3d3_to_gltf,
        export_3d3_to_glb,
        export_3d3_gltf_to_glb,
        parse_3dg,
        parse_3dt,
        parse_wld,
        build_3d3,
        build_3dg,
        build_3dt,
        build_wld,
        export_fonts,
        export_sounds,
    )
    from tools.f15assets.f15assets.pic import known_runtime_pic_palette, to_png_data
    from tools.f15assets.f15assets.sounds import DEFAULT_SAMPLE_RATE
    from tools.f15assets.f15assets.io import from_base64, to_base64
    from tools.f15assets.f15assets.validation_image import validate_pic_png_replacement, validate_png_replacement_loadability
    from tools.f15assets.f15assets.validation_sound import validate_sound_replacements
    from tools.f15assets.f15assets.validation_font import validate_font_replacements
    from tools.f15assets.f15assets.validation_structured import validate_structured_json_replacements
    from tools.f15assets.f15assets.validation_model3d import (
        glb_to_glmesh_bytes,
        validate_3d3_glb_replacements,
    )
except ModuleNotFoundError:
    from f15assets import (
        decode_pic_asset,
        decode_title640_pic_asset,
        parse_3d3,
        export_3d3_shape_gltfs,
        export_3d3_to_gltf,
        export_3d3_to_glb,
        export_3d3_gltf_to_glb,
        parse_3dg,
        parse_3dt,
        parse_wld,
        build_3d3,
        build_3dg,
        build_3dt,
        build_wld,
        export_fonts,
        export_sounds,
    )
    from f15assets.pic import known_runtime_pic_palette, to_png_data
    from f15assets.sounds import DEFAULT_SAMPLE_RATE
    from f15assets.io import from_base64, to_base64
    from f15assets.validation_image import validate_pic_png_replacement, validate_png_replacement_loadability
    from f15assets.validation_sound import validate_sound_replacements
    from f15assets.validation_font import validate_font_replacements
    from f15assets.validation_structured import validate_structured_json_replacements
    from f15assets.validation_model3d import (
        glb_to_glmesh_bytes,
        validate_3d3_glb_replacements,
    )

ASSET_EXT_FORMATS = {
    ".pic": "PIC",
    ".spr": "PIC",
    ".3d3": "3D3",
    ".3dt": "3DT",
    ".3dg": "3DG",
    ".wld": "WLD",
}

THEATER_WLD_STEM_ALIASES = {
    "LB": "LIBYA",
    "PG": "GULF",
}

F117_PIC_PALETTE_ALIASES = {
    "256LEFT": "FLIGHT.PAL",
    "256PIT": "FLIGHT.PAL",
    "256REAR": "FLIGHT.PAL",
    "256RIGHT": "FLIGHT.PAL",
    "CLIMBIN": "ADV.PAL",
}

# Some tests and external scripts imported this helper from cli.py before the
# per-format validator split. Keep the alias while new code uses the public name.
_glb_to_glmesh_bytes = glb_to_glmesh_bytes


def _iter_asset_paths(root: Path, recursive: bool):
    root = root
    if not root.exists():
        return []

    if recursive:
        return sorted(
            path
            for path in root.rglob("*")
            if path.is_file() and _detect_format(path)
        )

    return sorted(
        path for path in root.iterdir() if path.is_file() and _detect_format(path)
    )


def _read_binary(path: Path) -> bytes:
    with path.open("rb") as f:
        return f.read()


def _write_json(path: Path, payload: Dict[str, Any], pretty: bool = True, *, media_sidecar: bool = True) -> None:
    # Default converter sidecars are media-first: for 3D3, GLB files are the
    # editable source and JSON is minimized. Full bridge dumps deliberately opt
    # out so runtime can rebuild the legacy byte stream without original .3D3.
    writable_payload = _sidecar_payload_for_write(payload) if media_sidecar else payload
    ordered_payload = _human_ordered_payload(writable_payload)
    with path.open("w", encoding="utf-8") as f:
        if pretty:
            json.dump(ordered_payload, f, indent=2)
            f.write("\n")
        else:
            json.dump(ordered_payload, f)


def _sidecar_payload_for_write(payload: Dict[str, Any]) -> Dict[str, Any]:
    if payload.get("format") == "PIC":
        out = {
            key: payload[key]
            for key in (
                "format",
                "version",
                "source_name",
                "decoded_width",
                "decoded_height",
                "max_lzw_width",
                "bitstream_mode",
                "palette_profile",
                "original_length",
                "decoded_length",
                "stored_height",
                "layout",
                "notes",
            )
            if key in payload
        }
        out["image"] = {
            "preferred": "png",
            "authoritative": True,
            "notes": "Pixel data, dimensions, and palette from the matching indexed PNG override JSON metadata.",
        }
        return out

    if payload.get("format") != "3D3":
        return payload

    # GLB is the preferred model replacement/editing artifact. Keep the JSON
    # sidecar as a small index/manifest instead of duplicating model bytecode.
    out = {
        key: payload[key]
        for key in (
            "format",
            "version",
            "signature",
            "shape_offsets",
            "model_data_size",
            "shape_names",
        )
        if key in payload
    }
    shared_pool = payload.get("shared_vertex_pool")
    if isinstance(shared_pool, dict):
        out["shared_vertex_pool"] = {
            "index_count": len(shared_pool.get("x_indices", [])),
            "x_value_count": len(shared_pool.get("x_values", [])),
            "y_value_count": len(shared_pool.get("y_values", [])),
            "z_value_count": len(shared_pool.get("z_values", [])),
        }
    else:
        out["shared_vertex_pool"] = None
    out["geometry"] = {
        "preferred": "glb",
        "authoritative": True,
        "combined": True,
        "per_shape_files": True,
        "notes": "3D geometry and GLB extras override JSON metadata; JSON is only an index.",
    }
    return out


_LARGE_JSON_KEYS = {
    "compressed_payload_base64",
    "model_data",
    "name_table",
    "palette_dac6_base64",
    "palette_rgb8_base64",
    "pixels_base64",
    "raw_bytes_base64",
    "shape_target_category_table",
    "terrain_grid",
    "trailing_bytes",
    "unknown_bytes_base64",
    "kill_tally_or_unit_flags",
    "mission_object_type_table",
}

_JSON_KEY_ORDER = {
    "WLD": [
        "format",
        "version",
        "terrain_target_ids",
        "read_item_size",
        "ground_unit_count",
        "world_object_count",
        "world_objects",
        "flight_unit_count",
        "flight_units",
        "name_strings",
    ],
    "3D3": [
        "format",
        "version",
        "signature",
        "shape_offsets",
        "shape_names",
        "model_data_size",
        "shared_vertex_pool",
    ],
}

_RECORD_KEY_ORDER = [
    "name",
    "label",
    "unitRef",
    "unitType",
    "objectIdx",
    "x_coord",
    "y_coord",
    "z_coord",
    "startX",
    "startY",
    "startZ",
    "waypointX",
    "waypointY",
    "waypointZ",
    "targetFlags",
    "occupantType",
    "patrolCount",
    "waypointIdx",
    "flags",
    "weaponType",
    "terrainColor",
    "damage",
]


def _is_large_json_key(key: object) -> bool:
    text = str(key)
    return text in _LARGE_JSON_KEYS or text.endswith("_base64")


def _ordered_human_mapping(payload: Dict[str, Any], preferred: list[str] | None = None) -> Dict[str, Any]:
    preferred = preferred or []
    out: Dict[str, Any] = {}
    seen = set()

    for key in preferred:
        if key in payload:
            out[key] = _human_ordered_payload(payload[key])
            seen.add(key)

    normal_keys = [
        key for key in payload.keys()
        if key not in seen and not _is_large_json_key(key)
    ]
    large_keys = [
        key for key in payload.keys()
        if key not in seen and _is_large_json_key(key)
    ]
    for key in normal_keys + large_keys:
        out[key] = _human_ordered_payload(payload[key])
    return out


def _human_ordered_payload(value: Any) -> Any:
    if isinstance(value, list):
        return [_human_ordered_payload(item) for item in value]
    if not isinstance(value, dict):
        return value

    fmt = value.get("format")
    if isinstance(fmt, str) and fmt in _JSON_KEY_ORDER:
        return _ordered_human_mapping(value, _JSON_KEY_ORDER[fmt])

    return _ordered_human_mapping(value, _RECORD_KEY_ORDER)


def _write_gltf(path: Path, payload: Dict[str, Any], pretty: bool = True) -> None:
    with path.open("w", encoding="utf-8") as f:
        if pretty:
            json.dump(payload, f, indent=2, sort_keys=True)
            f.write("\n")
        else:
            json.dump(payload, f)


def _write_gltf_binary(path: Path, payload: Dict[str, Any]) -> None:
    glb_data = export_3d3_to_glb(payload)
    path.write_bytes(glb_data)


def _write_gltf_binary_doc(path: Path, gltf: Dict[str, Any]) -> None:
    glb_data = export_3d3_gltf_to_glb(gltf)
    path.write_bytes(glb_data)


def _remove_if_exists(path: Path) -> None:
    if path.exists():
        try:
            path.unlink()
        except OSError:
            pass


def _detect_format(path: Path) -> str:
    return ASSET_EXT_FORMATS.get(path.suffix.lower(), "")


def _safe_output_stem(value: object) -> str:
    text = str(value or "").strip()
    safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in text)
    safe = "_".join(part for part in safe.split("_") if part)
    return safe[:96] or "model"


def _set_name_string(payload: Dict[str, Any], index: int, value: str) -> None:
    names = payload.setdefault("name_strings", [])
    if not isinstance(names, list):
        names = []
        payload["name_strings"] = names
    while len(names) <= index:
        names.append("")
    names[index] = value


def _sync_wld_name_table(payload: Dict[str, Any]) -> None:
    names = payload.get("name_strings", [])
    if not isinstance(names, list):
        return
    encoded = b"".join(str(name).encode("ascii", errors="replace") + b"\x00" for name in names)
    payload["name_table"] = to_base64(encoded[:750].ljust(750, b"\x00"))


def _object_name(payload: Dict[str, Any], obj: Dict[str, Any]) -> str:
    names = payload.get("name_strings", [])
    try:
        name_index = int(obj.get("objectIdx", 0)) & 0x7F
    except (TypeError, ValueError):
        name_index = 0
    if isinstance(names, list) and 0 <= name_index < len(names):
        return str(names[name_index] or "")
    return ""


def _annotate_campaign_targets(payload: Dict[str, Any]) -> None:
    mission_table = from_base64(payload.get("mission_object_type_table", ""))
    objectives_by_id = {
        str(item.get("id", "")): item
        for item in ((payload.get("mission_plan") or {}).get("objectives") or [])
        if isinstance(item, dict)
    }
    target_annotations = []
    for idx, obj in enumerate(payload.get("world_objects", [])):
        if not isinstance(obj, dict):
            continue
        try:
            object_idx = int(obj.get("objectIdx", 0)) & 0x7F
        except (TypeError, ValueError):
            object_idx = 0
        mission_class = mission_table[object_idx] if object_idx < len(mission_table) else 0
        role = "reference"
        if mission_class:
            role = "mission_target"
        if idx == 0:
            role = "soviet_start_area"
        elif idx in {1, 2, 3} and mission_class:
            role = "primary_us_target"
        objective = objectives_by_id.get(str(obj.get("scenario_objective_id", "")), {})
        route_waypoint = _campaign_route_waypoint_for_objective(
            payload, str(obj.get("scenario_objective_id", "") or objective.get("id", ""))
        )
        target_annotations.append(
            {
                "object_index": idx,
                "name": obj.get("scenario_label") or _object_name(payload, obj),
                "legacy_name": obj.get("legacy_label") or _object_name(payload, obj),
                "role": role,
                "scenario_objective_id": obj.get("scenario_objective_id", ""),
                "scenario_objective": obj.get("scenario_objective", ""),
                "scenario_priority": obj.get("scenario_priority", objective.get("priority", "")),
                "scenario_faction": obj.get("scenario_faction", ""),
                "scenario_domain": obj.get("scenario_domain", ""),
                "scenario_action": obj.get("scenario_action", ""),
                "scenario_target_type": obj.get("scenario_target_type", objective.get("target_type", "")),
                "scenario_threat_level": obj.get("scenario_threat_level", objective.get("threat_level", "")),
                "scenario_desired_effect": obj.get("scenario_desired_effect", objective.get("desired_effect", "")),
                "suggested_loadout": objective.get("suggested_loadout", ""),
                "success_criteria": objective.get("success_criteria", []),
                "radio_cue_ids": objective.get("radio_cue_ids", []),
                "mission_object_type": mission_class,
                "coordinates": {
                    "x_coord": obj.get("x_coord", 0),
                    "y_coord": obj.get("y_coord", 0),
                },
                "real_world_anchor": (
                    {
                        "label": route_waypoint.get("label", ""),
                        "lon": route_waypoint.get("lon"),
                        "lat": route_waypoint.get("lat"),
                        "doctrine": route_waypoint.get("doctrine", ""),
                    }
                    if route_waypoint
                    else None
                ),
                "notes": (
                    "Edit role/name/coordinates in WLD JSON or the map editor. "
                    "mission_object_type comes from the preserved mission table."
                ),
            }
        )
    payload["target_annotations"] = target_annotations


_SVN_WORLD_MAX = 32768
_SVN_OSM_TILE_ZOOM = 6
_SVN_OSM_TILE_SIZE = 256
_SVN_VN_GEOREF = {
    # Same affine transform as tools/f15assets/map_editor.html. It maps Web
    # Mercator tile pixels at zoom 6 into legacy VN WLD coordinates.
    "xx": 41.1144272,
    "xy": -42.6172157,
    "x0": -200320.7,
    "yx": 47.2476764,
    "yy": 43.2960353,
    "y0": -922104.446,
}
_SVN_ROUTE_ANCHORS = {
    "start_soviet_hq": {
        "waypoint_id": "wp_hq",
        "kind": "friendly_base",
        "label": "Gia Lam / Hanoi forward control",
        "lon": 105.886,
        "lat": 21.040,
        "doctrine": "friendly staging and recovery point",
    },
    "suppress_hanoi_radar": {
        "waypoint_id": "wp_hanoi_radar",
        "kind": "sead_target",
        "label": "Hanoi SAM radar belt",
        "lon": 105.854,
        "lat": 21.075,
        "doctrine": "open inland radar corridor",
    },
    "interdict_haiphong_logistics": {
        "waypoint_id": "wp_haiphong",
        "kind": "logistics_target",
        "label": "Haiphong port logistics",
        "lon": 106.682,
        "lat": 20.866,
        "doctrine": "cut port/depot/bridge supply flow",
    },
    "strike_carrier_group": {
        "waypoint_id": "wp_carrier",
        "kind": "naval_strike_target",
        "label": "Gulf of Tonkin carrier box",
        "lon": 107.650,
        "lat": 19.950,
        "doctrine": "disable carrier flight operations",
    },
    "sweep_tonkin_patrol": {
        "waypoint_id": "wp_tonkin_cap",
        "kind": "fighter_sweep_area",
        "label": "Gulf of Tonkin CAP lane",
        "lon": 107.250,
        "lat": 20.420,
        "doctrine": "pull USA fighters off the carrier strike lane",
    },
    "strike_da_nang_airbase": {
        "waypoint_id": "wp_danang",
        "kind": "airbase_strike_target",
        "label": "Da Nang airbase",
        "lon": 108.199,
        "lat": 16.043,
        "doctrine": "crater runway and burn fuel/parked-aircraft capacity",
    },
}


def _svn_lon_to_tile_pixel_x(lon: float) -> float:
    return ((lon + 180.0) / 360.0) * (2 ** _SVN_OSM_TILE_ZOOM) * _SVN_OSM_TILE_SIZE


def _svn_lat_to_tile_pixel_y(lat: float) -> float:
    # Web Mercator tile-space conversion kept local so campaign generation does
    # not depend on browser/editor code.
    rad = math.radians(lat)
    return (
        1.0 - math.log(math.tan(rad) + 1.0 / math.cos(rad)) / math.pi
    ) / 2.0 * (2 ** _SVN_OSM_TILE_ZOOM) * _SVN_OSM_TILE_SIZE


def _svn_lonlat_to_world(lon: float, lat: float) -> Tuple[int, int]:
    px = _svn_lon_to_tile_pixel_x(lon)
    py = _svn_lat_to_tile_pixel_y(lat)
    t = _SVN_VN_GEOREF
    wx = t["xx"] * px + t["xy"] * py + t["x0"]
    wy = t["yx"] * px + t["yy"] * py + t["y0"]
    return (
        max(0, min(_SVN_WORLD_MAX, round(wx))),
        max(0, min(_SVN_WORLD_MAX, round(wy))),
    )


def _campaign_route_waypoint_for_objective(payload: Dict[str, Any], objective_id: str) -> Optional[Dict[str, Any]]:
    if not objective_id:
        return None
    plan = payload.get("campaign_route_plan")
    if not isinstance(plan, dict):
        return None
    waypoints = plan.get("waypoints")
    if not isinstance(waypoints, list):
        return None
    for waypoint in waypoints:
        if isinstance(waypoint, dict) and waypoint.get("objective_id") == objective_id:
            return waypoint
    return None


def _apply_soviet_vietnam_route_plan(out: Dict[str, Any], objectives: List[Dict[str, Any]]) -> None:
    # This is authoring metadata for the map editor and future mission tooling.
    # The legacy runtime still consumes world_objects first, so each waypoint is
    # synchronized into its starter object slot below.
    anchors: Dict[str, Dict[str, Any]] = {}
    for objective_id, raw in _SVN_ROUTE_ANCHORS.items():
        x_coord, y_coord = _svn_lonlat_to_world(float(raw["lon"]), float(raw["lat"]))
        anchor = dict(raw)
        anchor["x_coord"] = x_coord
        anchor["y_coord"] = y_coord
        anchor["coordinate_source"] = "author_approximate_osm_anchor"
        anchors[objective_id] = anchor

    objectives_by_id = {str(item.get("id", "")): item for item in objectives if isinstance(item, dict)}
    world_objects = out.get("world_objects", [])
    if isinstance(world_objects, list):
        for objective_id, anchor in anchors.items():
            objective = objectives_by_id.get(objective_id)
            if not objective:
                continue
            try:
                slot = int(objective.get("object_slot", -1))
            except (TypeError, ValueError):
                slot = -1
            if 0 <= slot < len(world_objects) and isinstance(world_objects[slot], dict):
                obj = world_objects[slot]
                obj["x_coord"] = anchor["x_coord"]
                obj["y_coord"] = anchor["y_coord"]
                obj["scenario_real_world_anchor"] = {
                    "label": anchor["label"],
                    "lon": anchor["lon"],
                    "lat": anchor["lat"],
                    "coordinate_source": anchor["coordinate_source"],
                    "doctrine": anchor["doctrine"],
                }
                obj["editor_notes"] = (
                    f"Campaign starter target anchored near {anchor['label']}; "
                    "move in map editor if gameplay balance needs a different location."
                )
                objective["real_world_anchor"] = {
                    "label": anchor["label"],
                    "lon": anchor["lon"],
                    "lat": anchor["lat"],
                    "x_coord": anchor["x_coord"],
                    "y_coord": anchor["y_coord"],
                    "doctrine": anchor["doctrine"],
                }

    def waypoint(objective_id: str) -> Dict[str, Any]:
        anchor = anchors[objective_id]
        return {
            "id": anchor["waypoint_id"],
            "objective_id": objective_id,
            "kind": anchor["kind"],
            "label": anchor["label"],
            "lon": anchor["lon"],
            "lat": anchor["lat"],
            "x_coord": anchor["x_coord"],
            "y_coord": anchor["y_coord"],
            "doctrine": anchor["doctrine"],
            "coordinate_source": anchor["coordinate_source"],
        }

    out["campaign_route_plan"] = {
        "format": "F15SE2_CAMPAIGN_ROUTE_PLAN",
        "format_version": 1,
        "coordinate_system": "legacy_wld_x_coord_y_coord with approximate lon/lat anchors",
        "georef": {
            "theater": "VN",
            "source": "tools/f15assets/map_editor.html OSM_GEOREFS.VN",
            "world_max": _SVN_WORLD_MAX,
            "osm_tile_zoom": _SVN_OSM_TILE_ZOOM,
            "affine": dict(_SVN_VN_GEOREF),
        },
        "waypoints": [
            waypoint("start_soviet_hq"),
            waypoint("suppress_hanoi_radar"),
            waypoint("interdict_haiphong_logistics"),
            waypoint("sweep_tonkin_patrol"),
            waypoint("strike_carrier_group"),
            waypoint("strike_da_nang_airbase"),
        ],
        "sortie_routes": [
            {
                "sortie_id": "opening_sead",
                "route": ["wp_hq", "wp_hanoi_radar", "wp_hq"],
                "mission_logic": "clear radar corridor before deeper strike sorties",
            },
            {
                "sortie_id": "carrier_strike",
                "route": ["wp_hq", "wp_tonkin_cap", "wp_carrier", "wp_hq"],
                "mission_logic": "sweep CAP lane, strike carrier box, return north",
            },
            {
                "sortie_id": "airbase_and_logistics",
                "route": ["wp_hq", "wp_haiphong", "wp_danang", "wp_hq"],
                "mission_logic": "interdict logistics, then attack southern airbase if fuel/weapons permit",
            },
        ],
        "notes": [
            "Route plan is editable metadata for tools and future mission logic; legacy runtime still consumes world_objects first.",
            "Coordinates are approximate OSM anchors transformed into the VN WLD map by the map editor georeference.",
        ],
    }
    out.setdefault("scenario_design", {})["route_authoring_notes"] = (
        "campaign_route_plan ties sorties to approximate Vietnam real-world anchors "
        "transformed into WLD coordinates; edit waypoints and object x_coord/y_coord "
        "together when redesigning missions."
    )


def _apply_soviet_vietnam_campaign(payload: Dict[str, Any]) -> Dict[str, Any]:
    if payload.get("format") != "WLD":
        raise ValueError("custom campaign template must be a WLD JSON payload")

    out = json.loads(json.dumps(payload))
    out["campaign"] = {
        "id": "SVN",
        "title": "Li Si Cin: Soviet Vietnam Campaign",
        "schema": "F15SE2_MODERN_CAMPAIGN_WLD",
        "schema_version": 1,
        "theater": "Vietnam",
        "side": "Soviet / Warsaw Pact expeditionary group",
        "opponent": "United States and allied air/naval forces",
        "protagonist": {
            "name": "Lisicin",
            "callsign": "Li Si Cin",
            "background": (
                "An F-15 plane was stolen by Soviet spy pilot Lisicin. "
                "Vietnamese allies call him Li Si Cin while he flies captured "
                "and Soviet aircraft against USA targets."
            ),
        },
        "format_version": 1,
        "authoring_notes": [
            "This is a modern-format WLD JSON scaffold derived from the selected template.",
            "It is intended for custom scenario editing, not byte-identical original proof.",
            "Keep WLD/3DT/3DG JSON loadable; replace aircraft visuals with selected 15FLT/shape_###*.glb files.",
        ],
    }
    out["scenario_design"] = {
        "premise": (
            "Soviet spy pilot Lisicin has stolen an F-15 plane and now operates "
            "in Vietnam as Li Si Cin, supporting North Vietnam against USA "
            "carrier, airbase, radar, and logistics targets."
        ),
        "recommended_player_role": "Lisicin / Li Si Cin, Soviet spy pilot",
        "mission_target_ideas": [
            "USA carrier group and escort ships in the Gulf of Tonkin",
            "Da Nang / coastal airbase strike",
            "SAM and radar defense suppression around Hanoi and Haiphong",
            "Interdiction of USA logistics depots and bridges",
            "Fighter sweep against USA patrol aircraft",
        ],
        "aircraft_visual_swaps": [
            {
                "container": "15FLT",
                "files": "shape_###*.glb",
                "notes": (
                    "Replace selected aircraft model slots with custom MiG/Sukhoi/Tupole GLBs. "
                    "The slot number controls runtime lookup; behavior still comes from game aircraft tables."
                ),
            }
        ],
    }
    mission_objectives = [
        {
            "id": "start_soviet_hq",
            "name": "Soviet forward HQ",
            "object_slot": 0,
            "role": "start_area",
            "priority": "friendly",
            "faction": "soviet_vietnamese",
            "domain": "base",
            "action": "defend",
            "target_type": "friendly_command_post",
            "threat_level": "friendly",
            "desired_effect": "preserve",
            "success_criteria": ["HQ remains available as sortie staging point"],
            "suggested_loadout": "none",
            "radio_cue_ids": ["voice_cue_000_sample0"],
            "intent": "Player staging area and campaign anchor for Lisicin / Li Si Cin.",
        },
        {
            "id": "strike_carrier_group",
            "name": "USA carrier group",
            "object_slot": 6,
            "role": "primary_target",
            "priority": "primary",
            "faction": "usa",
            "domain": "naval",
            "action": "strike",
            "target_type": "carrier_task_force",
            "threat_level": "very_high",
            "target_systems": ["carrier deck", "escort radar", "point-defense escorts"],
            "desired_effect": "disable carrier flight operations",
            "success_criteria": [
                "primary ship or carrier-equivalent object destroyed",
                "at least one escort/radar object suppressed if present",
            ],
            "suggested_loadout": "anti-ship / heavy strike ordnance with fighter cover",
            "radio_cue_ids": ["voice_cue_002_sample2_variant0"],
            "intent": "Main naval strike target in the Gulf of Tonkin.",
        },
        {
            "id": "strike_da_nang_airbase",
            "name": "Da Nang airbase",
            "object_slot": 7,
            "role": "primary_target",
            "priority": "primary",
            "faction": "usa",
            "domain": "airbase",
            "action": "strike",
            "target_type": "runway_and_fuel_site",
            "threat_level": "high",
            "target_systems": ["runway", "fuel storage", "parked aircraft", "tower/radar"],
            "desired_effect": "reduce USA sortie tempo from the coastal airbase",
            "success_criteria": [
                "runway or airbase object destroyed",
                "secondary fuel/logistics object destroyed if placed",
            ],
            "suggested_loadout": "runway cratering bombs plus self-defense missiles",
            "radio_cue_ids": ["voice_cue_003_sample2_variant1"],
            "intent": "Airbase strike target for runway, fuel, and aircraft concentration.",
        },
        {
            "id": "suppress_hanoi_radar",
            "name": "Hanoi radar net",
            "object_slot": 3,
            "role": "primary_target",
            "priority": "primary",
            "faction": "usa_allied",
            "domain": "air_defense",
            "action": "suppress",
            "target_type": "sam_radar_network",
            "threat_level": "high",
            "target_systems": ["search radar", "SAM launcher", "AAA site"],
            "desired_effect": "open a low-risk corridor toward Hanoi and Haiphong",
            "success_criteria": [
                "radar object destroyed or moved out of active route",
                "SAM/AAA object destroyed if paired with the radar",
            ],
            "suggested_loadout": "anti-radiation / precision strike weapons",
            "radio_cue_ids": ["voice_cue_001_sample4", "voice_cue_004_sample2_variant2"],
            "intent": "SAM/radar suppression target protecting North Vietnam.",
        },
        {
            "id": "interdict_haiphong_logistics",
            "name": "Haiphong logistics",
            "object_slot": 4,
            "role": "secondary_target",
            "priority": "secondary",
            "faction": "usa_allied",
            "domain": "logistics",
            "action": "interdict",
            "target_type": "port_depot_bridge_cluster",
            "threat_level": "medium",
            "target_systems": ["depot", "bridge", "port crane", "convoy staging point"],
            "desired_effect": "slow USA resupply and coastal reinforcement",
            "success_criteria": [
                "depot/bridge-equivalent object destroyed",
                "escape route remains open after attack",
            ],
            "suggested_loadout": "medium bombs or rockets with fuel margin for egress",
            "radio_cue_ids": ["voice_cue_004_sample2_variant2"],
            "intent": "Depot, bridge, port, or logistics interdiction target.",
        },
        {
            "id": "sweep_tonkin_patrol",
            "name": "Tonkin patrol",
            "object_slot": 5,
            "role": "air_patrol",
            "priority": "secondary",
            "faction": "usa",
            "domain": "air",
            "action": "sweep",
            "target_type": "fighter_patrol_area",
            "threat_level": "medium_high",
            "target_systems": ["CAP patrol", "AWACS/radar cue", "fighter ingress lane"],
            "desired_effect": "draw USA fighters away from the carrier strike lane",
            "success_criteria": [
                "enemy patrol aircraft destroyed or displaced",
                "carrier strike route remains clear",
            ],
            "suggested_loadout": "air-to-air missiles and external fuel",
            "radio_cue_ids": ["voice_cue_002_sample2_variant0"],
            "intent": "USA patrol aircraft / CAP encounter area.",
        },
    ]
    mission_target_sets = [
        {
            "id": "target_set_hanoi_corridor",
            "sortie_id": "opening_sead",
            "title": "Hanoi radar corridor package",
            "objective_ids": ["suppress_hanoi_radar"],
            "package_role": "SEAD / ingress corridor opening",
            "attack_order": ["search_radar", "sam_launcher", "aaa_screen"],
            "target_elements": [
                {
                    "element_id": "search_radar",
                    "objective_id": "suppress_hanoi_radar",
                    "role": "primary",
                    "recommended_weapon": "anti-radiation missile or precision bomb",
                    "effect": "blind the radar belt",
                    "placement_hint": "Place on or near the Hanoi radar waypoint; this is the runtime primary target slot.",
                    "defense_behavior": "Should cue SAM/AAA objects and be visible early enough for a SEAD pass.",
                },
                {
                    "element_id": "sam_launcher",
                    "objective_id": "suppress_hanoi_radar",
                    "role": "secondary",
                    "recommended_weapon": "bombs/rockets",
                    "effect": "reduce missile threat during egress",
                    "placement_hint": "Place 1-3 nearby launcher objects offset from the radar so one bomb run cannot erase the whole site.",
                    "defense_behavior": "Threat should punish high-altitude direct ingress but leave low-altitude masking viable.",
                },
                {
                    "element_id": "aaa_screen",
                    "objective_id": "suppress_hanoi_radar",
                    "role": "optional",
                    "recommended_weapon": "cannon/rockets",
                    "effect": "make repeated passes survivable",
                    "placement_hint": "Place around the radar perimeter or along the egress lane.",
                    "defense_behavior": "Short-range pressure only; avoid making the first sortie impossible.",
                },
            ],
            "victory_logic": {"required_primary_destroyed": 1, "required_secondary_destroyed": 0, "friendly_survival_required": ["start_soviet_hq"]},
            "editor_notes": "Place paired radar/SAM/AAA objects around Hanoi; keep one clear low-altitude return lane to HQ.",
        },
        {
            "id": "target_set_tonkin_carrier",
            "sortie_id": "carrier_strike",
            "title": "Gulf of Tonkin carrier strike package",
            "objective_ids": ["sweep_tonkin_patrol", "strike_carrier_group"],
            "package_role": "fighter sweep followed by anti-ship strike",
            "attack_order": ["cap_patrol", "escort_radar", "carrier_deck"],
            "target_elements": [
                {
                    "element_id": "cap_patrol",
                    "objective_id": "sweep_tonkin_patrol",
                    "role": "screen",
                    "recommended_weapon": "air-to-air missiles",
                    "effect": "open the approach lane",
                    "placement_hint": "Place ahead of the carrier box, not directly on top of it, so sweep-first and bypass tactics differ.",
                    "defense_behavior": "Intercept player before anti-ship release; disengage pressure after carrier strike lane is opened.",
                },
                {
                    "element_id": "escort_radar",
                    "objective_id": "strike_carrier_group",
                    "role": "secondary",
                    "recommended_weapon": "anti-radiation or precision strike",
                    "effect": "reduce fleet warning time",
                    "placement_hint": "Place close to the carrier group as an escort/radar picket object.",
                    "defense_behavior": "Supports point defense and early warning; not required if using a simple one-object carrier target.",
                },
                {
                    "element_id": "carrier_deck",
                    "objective_id": "strike_carrier_group",
                    "role": "primary",
                    "recommended_weapon": "heavy bombs / anti-ship loadout",
                    "effect": "disable carrier flight operations",
                    "placement_hint": "Place at the Gulf of Tonkin carrier waypoint; this is the runtime primary target slot.",
                    "defense_behavior": "High-value target with escort coverage; should not require destroying every ship to complete the sortie.",
                },
            ],
            "victory_logic": {"required_primary_destroyed": 1, "required_secondary_destroyed": 1, "bonus_objective_ids": ["sweep_tonkin_patrol"]},
            "editor_notes": "Keep carrier and CAP separated enough that the player must choose sweep-first or high-risk direct strike.",
        },
        {
            "id": "target_set_southern_air_bridge",
            "sortie_id": "airbase_and_logistics",
            "title": "Da Nang and Haiphong air-bridge package",
            "objective_ids": ["interdict_haiphong_logistics", "strike_da_nang_airbase"],
            "package_role": "logistics interdiction plus airbase strike",
            "attack_order": ["haiphong_depot", "da_nang_runway", "fuel_storage"],
            "target_elements": [
                {
                    "element_id": "haiphong_depot",
                    "objective_id": "interdict_haiphong_logistics",
                    "role": "secondary",
                    "recommended_weapon": "medium bombs",
                    "effect": "slow coastal resupply",
                    "placement_hint": "Place on the Haiphong logistics waypoint as a depot, bridge, port, or convoy-staging object.",
                    "defense_behavior": "Medium threat; should be optional if fuel or damage state makes Da Nang the safer priority.",
                },
                {
                    "element_id": "da_nang_runway",
                    "objective_id": "strike_da_nang_airbase",
                    "role": "primary",
                    "recommended_weapon": "runway cratering bombs",
                    "effect": "stop USA sorties from Da Nang",
                    "placement_hint": "Place at the Da Nang waypoint; this is the runtime primary target slot.",
                    "defense_behavior": "Defended by local AAA/SAM and possible fighters, but reachable without carrier-level escort density.",
                },
                {
                    "element_id": "fuel_storage",
                    "objective_id": "strike_da_nang_airbase",
                    "role": "secondary",
                    "recommended_weapon": "bombs/rockets",
                    "effect": "increase recovery time after runway repair",
                    "placement_hint": "Place near but not overlapping the runway target so the player must choose a second pass.",
                    "defense_behavior": "Soft secondary target; useful for score/campaign effect rather than hard mission completion.",
                },
            ],
            "victory_logic": {"required_primary_destroyed": 1, "required_secondary_destroyed": 1, "egress_waypoint": "wp_hq"},
            "editor_notes": "This route is long; keep threats sparse or provide a custom aircraft/fuel balance in future runtime logic.",
        },
    ]

    out["mission_plan"] = {
        "format_version": 1,
        "notes": [
            "Scenario-authoring fields are ignored by legacy WLD rebuild.",
            "Map/editor tooling should treat object_slot as the starter object to move, copy, or replace.",
            "mission_target_sets groups objectives into richer packages for editors/future runtime mission logic.",
            "Original mission generation still uses preserved WLD tables until deeper campaign runtime support is added.",
        ],
        "mission_target_sets": mission_target_sets,
        "sortie_sequence": [
            {
                "id": "opening_sead",
                "phase": 1,
                "title": "Open the radar corridor",
                "player_aircraft": "captured F-15 / Soviet-marked cover identity",
                "primary_objective_id": "suppress_hanoi_radar",
                "secondary_objective_id": "",
                "objective_ids": ["suppress_hanoi_radar"],
                "primary_objective_ids": ["suppress_hanoi_radar"],
                "secondary_objective_ids": [],
                "radio_cue_ids": ["voice_cue_000_sample0", "voice_cue_001_sample4"],
                "briefing": "Destroy or suppress the Hanoi radar net so Lisicin can move deeper into the theater.",
            },
            {
                "id": "carrier_strike",
                "phase": 2,
                "title": "Strike the carrier group",
                "player_aircraft": "captured F-15 with Soviet/Vietnamese ground control",
                "primary_objective_id": "strike_carrier_group",
                "secondary_objective_id": "sweep_tonkin_patrol",
                "objective_ids": ["strike_carrier_group", "sweep_tonkin_patrol"],
                "primary_objective_ids": ["strike_carrier_group"],
                "secondary_objective_ids": ["sweep_tonkin_patrol"],
                "radio_cue_ids": ["voice_cue_002_sample2_variant0"],
                "briefing": "Attack the USA carrier group while fighter patrols are displaced over the Gulf of Tonkin.",
            },
            {
                "id": "airbase_and_logistics",
                "phase": 3,
                "title": "Cut the air bridge",
                "player_aircraft": "captured F-15 or custom Soviet strike aircraft GLB slot",
                "primary_objective_id": "strike_da_nang_airbase",
                "secondary_objective_id": "interdict_haiphong_logistics",
                "objective_ids": ["strike_da_nang_airbase", "interdict_haiphong_logistics"],
                "primary_objective_ids": ["strike_da_nang_airbase"],
                "secondary_objective_ids": ["interdict_haiphong_logistics"],
                "radio_cue_ids": ["voice_cue_003_sample2_variant1", "voice_cue_004_sample2_variant2"],
                "briefing": "Hit Da Nang and Haiphong support targets to reduce USA sortie tempo.",
            },
        ],
        "objectives": mission_objectives,
    }

    _set_name_string(out, 0, "SOVIET HQ")
    _set_name_string(out, 6, "USA CARRIER GROUP")
    _set_name_string(out, 7, "DA NANG AIRBASE")
    _set_name_string(out, 3, "HANOI RADAR NET")
    _set_name_string(out, 4, "HAIPHONG LOGISTICS")
    _set_name_string(out, 5, "TONKIN PATROL")
    _sync_wld_name_table(out)

    world_objects = out.get("world_objects", [])
    if isinstance(world_objects, list):
        max_object_slot = max(int(item.get("object_slot", 0)) for item in mission_objectives if isinstance(item, dict))
        while len(world_objects) <= max_object_slot:
            slot = len(world_objects)
            # Minimal WLD records keep generated campaigns buildable even when
            # the source template is a tiny test/draft theater. Real gameplay
            # coordinates and scenario labels are assigned below and by the route
            # anchor sync; runtime behavior still falls back to legacy tables.
            world_objects.append(
                {
                    "unitRef": 0,
                    "x_coord": 0,
                    "y_coord": 0,
                    "unitType": 0,
                    "targetFlags": 0,
                    "occupantType": 0,
                    "patrolCount": 0,
                    "objectIdx": slot,
                }
            )
        out["read_item_size"] = max(int(out.get("read_item_size") or 0), len(world_objects))
        # This is the first base index, not the number of serialized records.
        # Expanding it to read_item_size makes mission generation skip every base.
        out["world_object_count"] = int(out.get("world_object_count", len(world_objects)))
        objectives_by_slot = {int(item["object_slot"]): item for item in mission_objectives}
        scenario_roles = {
            "start_soviet_hq": "soviet_start_area",
            "strike_carrier_group": "primary_us_naval_target",
            "strike_da_nang_airbase": "primary_us_airbase_target",
            "suppress_hanoi_radar": "primary_radar_target",
            "interdict_haiphong_logistics": "secondary_logistics_target",
            "sweep_tonkin_patrol": "air_patrol_area",
        }
        for idx, obj in enumerate(world_objects):
            if not isinstance(obj, dict):
                continue
            objective = objectives_by_slot.get(idx, {})
            if not objective:
                continue
            # Preserve the original WLD object index/name relationship for the
            # binary rebuild, but give editors a scenario-specific label. Several
            # legacy objects intentionally reuse the same objectIdx/name string.
            obj["legacy_label"] = _object_name(out, obj)
            obj["scenario_label"] = str(objective.get("name") or _object_name(out, obj))
            obj["scenario_role"] = scenario_roles.get(str(objective.get("id") or ""), "campaign_objective")
            obj["scenario_objective_id"] = objective.get("id", "")
            obj["scenario_objective"] = objective.get("intent", "")
            obj["scenario_priority"] = objective.get("priority", "")
            obj["scenario_faction"] = objective.get("faction", "")
            obj["scenario_domain"] = objective.get("domain", "")
            obj["scenario_action"] = objective.get("action", "")
            obj["scenario_target_type"] = objective.get("target_type", "")
            obj["scenario_threat_level"] = objective.get("threat_level", "")
            obj["scenario_desired_effect"] = objective.get("desired_effect", "")
            obj["editor_notes"] = "Scenario scaffold field; ignored by binary WLD rebuild."

    _apply_soviet_vietnam_route_plan(out, mission_objectives)
    _annotate_campaign_targets(out)
    return out


def _related_world_stem_for_theater_asset(source_path: Path) -> str:
    stem = source_path.stem
    upper_stem = stem.upper()
    wld_stem = THEATER_WLD_STEM_ALIASES.get(upper_stem, upper_stem)
    if (source_path.with_name(f"{wld_stem}.WLD")).exists() or (
        source_path.with_name(f"{wld_stem}.wld")
    ).exists():
        return wld_stem
    return stem


def _asset_output_dir(src: Path, relative: Path, input_root: Path, output_root: Path, fmt: str) -> Path:
    parent = output_root / relative.parent
    if fmt == "WLD":
        return parent / src.stem
    if fmt in {"3D3", "3DT", "3DG"}:
        return parent / _related_world_stem_for_theater_asset(src)
    return output_root / relative.with_suffix("")


def _write_3d3_shape_models(
    output_dir: Path,
    payload: Dict[str, Any],
    model_format: str,
    pretty: bool,
    write_cache: bool = False,
) -> None:
    if model_format == "none":
        return
    output_dir.mkdir(parents=True, exist_ok=True)
    for stale_glmesh in output_dir.glob("shape_*.glmesh"):
        _remove_if_exists(stale_glmesh)
    cache_dir = output_dir / "cache"
    if model_format == "glb" and cache_dir.exists():
        for stale_glmesh in cache_dir.glob("shape_*.glmesh"):
            _remove_if_exists(stale_glmesh)
    for shape_index, shape_name, shape_gltf in export_3d3_shape_gltfs(payload):
        label = _safe_output_stem(shape_name)
        base_shape = f"shape_{shape_index:03d}"
        base = base_shape if label == base_shape else f"{base_shape}_{label}"
        if model_format == "glb":
            glb_path = output_dir / f"{base}.glb"
            _write_gltf_binary_doc(glb_path, shape_gltf)
            if write_cache:
                cache_dir.mkdir(parents=True, exist_ok=True)
                (cache_dir / f"{base}.glmesh").write_bytes(glb_to_glmesh_bytes(glb_path))
        elif model_format == "gltf":
            _write_gltf(output_dir / f"{base}.gltf", shape_gltf, pretty=pretty)


def _log_3d3_gltf_diagnostics(gltf: Dict[str, Any], source_path: Path | None) -> None:
    label = str(source_path) if source_path is not None else "3D3"
    skipped = gltf.get("extras", {}).get("skipped_shapes", [])
    if isinstance(skipped, list):
        for item in skipped:
            if not isinstance(item, dict):
                continue
            meta = item.get("shape_payload")
            if not isinstance(meta, dict):
                meta = {}
            reason = (
                meta.get("render_error")
                or meta.get("edge_decode_error")
                or "no_renderable_primitives"
            )
            print(
                "warning: "
                f"{label}: skipped shape {item.get('shape_index')} "
                f"{item.get('shape_name')} "
                f"offset {item.get('shape_offset')}..{item.get('shape_end')} "
                f"render_mode={item.get('render_mode')} "
                f"reason={reason} "
                f"metadata={json.dumps(meta, sort_keys=True)}",
                file=sys.stderr,
            )

    raw_color_usage = gltf.get("extras", {}).get("raw_color_usage", {})
    point_counts = {}
    if isinstance(raw_color_usage, dict) and isinstance(raw_color_usage.get("points"), dict):
        point_counts = raw_color_usage["points"]
    if point_counts:
        print(
            "info: "
            f"{label}: exported degenerate source lines as POINTS "
            f"colors={json.dumps(point_counts, sort_keys=True)}",
            file=sys.stderr,
        )


def _dac6_to_rgb8(data: bytes) -> bytes:
    return bytes(((value & 0x3F) << 2) | ((value & 0x3F) >> 4) for value in data[:768])


def _find_external_palette(source_path: Path) -> tuple[Path, bytes, str] | None:
    same_stem = source_path.with_suffix(".PAL")
    if same_stem.exists() and same_stem.stat().st_size >= 768:
        return same_stem, same_stem.read_bytes()[:768], "same_stem_pal"

    lower_stem = source_path.with_suffix(".pal")
    if lower_stem.exists() and lower_stem.stat().st_size >= 768:
        return lower_stem, lower_stem.read_bytes()[:768], "same_stem_pal"

    alias_name = F117_PIC_PALETTE_ALIASES.get(source_path.stem.upper())
    if alias_name:
        alias_path = source_path.with_name(alias_name)
        if alias_path.exists() and alias_path.stat().st_size >= 768:
            return alias_path, alias_path.read_bytes()[:768], "f117_known_pic_palette_alias"
        alias_path = source_path.with_name(alias_name.lower())
        if alias_path.exists() and alias_path.stat().st_size >= 768:
            return alias_path, alias_path.read_bytes()[:768], "f117_known_pic_palette_alias"

    palette_table = source_path.with_name("PALETTES.PAL")
    if palette_table.exists() and palette_table.stat().st_size >= 768:
        return palette_table, palette_table.read_bytes()[:768], "f117_palettes_pal_chunk0"

    palette_table = source_path.with_name("palettes.pal")
    if palette_table.exists() and palette_table.stat().st_size >= 768:
        return palette_table, palette_table.read_bytes()[:768], "f117_palettes_pal_chunk0"

    return None


def _attach_external_palette(payload: Dict[str, Any], source_path: Path | None) -> None:
    if source_path is None:
        return
    profile = payload.get("palette_profile")
    if isinstance(profile, dict) and profile.get("status") == "embedded":
        return

    index_bit_depth = 8
    if isinstance(profile, dict):
        try:
            index_bit_depth = int(profile.get("index_bit_depth", 8))
        except (TypeError, ValueError):
            index_bit_depth = 8
    known_palette = known_runtime_pic_palette(source_path.name, index_bit_depth)
    if known_palette is not None:
        payload["palette_dac6_base64"] = to_base64(bytes(known_palette["palette_dac6"]))
        payload["palette_rgb8_base64"] = to_base64(bytes(known_palette["palette_rgb8"]))
        payload["palette_profile"] = known_palette["profile"]
        return

    found = _find_external_palette(source_path)
    if found is None:
        return

    palette_path, palette_dac6, source = found
    payload["palette_dac6_base64"] = to_base64(palette_dac6)
    payload["palette_rgb8_base64"] = to_base64(_dac6_to_rgb8(palette_dac6))
    payload["palette_profile"] = {
        "source": source,
        # Store the palette filename, not the user's absolute install path, so
        # converted image sidecars remain shareable across machines.
        "source_file": palette_path.name,
        "status": "external",
        "index_mode": "indexed",
        "index_bit_depth": payload.get("palette_profile", {}).get("index_bit_depth", 8)
        if isinstance(payload.get("palette_profile"), dict)
        else 8,
        "color_mode": "palette_index",
        "palette_format": "vga_dac_6bit_rgb_triples",
        "notes": "External 768-byte VGA DAC palette applied during PNG export.",
    }


def _decode_asset(
    data: bytes,
    fmt: str,
    args: argparse.Namespace,
    source_path: Path | None = None,
) -> Dict[str, Any]:
    if args.png and fmt != "PIC":
        raise ValueError("--png is only supported for PIC/SPR inputs")

    if args.gltf and fmt != "3D3":
        raise ValueError("--gltf is only supported for 3D3 inputs")

    if fmt == "PIC":
        is_title640 = source_path is not None and source_path.name.upper() == "TITLE640.PIC"
        if is_title640:
            payload = decode_title640_pic_asset(
                data, source_name=source_path.name if source_path is not None else None
            )
        else:
            payload = decode_pic_asset(
                data, source_name=source_path.name if source_path is not None else None
            )
        _attach_external_palette(payload, source_path)
        if args.png:
            pixels = from_base64(payload["pixels_base64"])
            index_bit_depth = 8
            try:
                index_bit_depth = int(payload.get("palette_profile", {}).get("index_bit_depth", 8))
            except (TypeError, ValueError):
                index_bit_depth = 8
            palette = None
            palette_rgb8 = payload.get("palette_rgb8_base64")
            if isinstance(palette_rgb8, str):
                palette = list(from_base64(palette_rgb8))
            image = to_png_data(
                pixels,
                width=int(payload.get("decoded_width", 320)),
                height=int(payload.get("decoded_height", 200)),
                index_bit_depth=index_bit_depth,
                palette=palette,
            )
            image.save(args.png)
        return payload

    if fmt == "3D3":
        payload = parse_3d3(data)
        if source_path is not None:
            payload["shape_names"] = _load_shape_names_for_3d3(source_path)
        if getattr(args, "gltf", None):
            gltf_path = Path(args.gltf)
            if gltf_path.suffix.lower() not in {".gltf", ".glb"}:
                raise ValueError("--gltf expects a .gltf or .glb output path")
            gltf = export_3d3_to_gltf(payload)
            _log_3d3_gltf_diagnostics(gltf, source_path)
            if gltf_path.suffix.lower() == ".glb":
                _write_gltf_binary_doc(gltf_path, gltf)
            else:
                _write_gltf(gltf_path, gltf, pretty=args.pretty)
        return payload

    if fmt == "3DT":
        return parse_3dt(data)

    if fmt == "3DG":
        return parse_3dg(data)

    if fmt == "WLD":
        return parse_wld(data)

    raise ValueError(f"unsupported format: {fmt}")


def _load_shape_names_for_3d3(source_path: Path) -> Dict[str, str]:
    stem = source_path.stem.upper()
    wld_stem = THEATER_WLD_STEM_ALIASES.get(stem, stem)
    wld_path = source_path.with_name(f"{wld_stem}.WLD")
    if not wld_path.exists():
        wld_path = source_path.with_name(f"{wld_stem}.wld")
    if not wld_path.exists():
        return {}

    try:
        wld = parse_wld(_read_binary(wld_path))
    except Exception:
        return {}

    names = wld.get("name_strings", [])
    if not isinstance(names, list):
        return {}

    shape_names: Dict[str, str] = {}
    for obj in wld.get("world_objects", []):
        try:
            object_name_index = int(obj.get("objectIdx", 0)) & 0x7F
        except (AttributeError, TypeError, ValueError):
            continue
        if object_name_index >= len(names):
            continue
        name = str(names[object_name_index] or "").strip()
        if name:
            shape_names.setdefault(str(object_name_index), name)
    return shape_names


def _decode_args(png: str | None = None, gltf: str | None = None, pretty: bool = False) -> argparse.Namespace:
    return argparse.Namespace(png=png, gltf=gltf, pretty=pretty)


def _minimize_3d3_json(payload: Dict[str, Any]) -> Dict[str, Any]:
    shared_pool = payload.get("shared_vertex_pool")
    shared_pool_summary = None
    if isinstance(shared_pool, dict):
        shared_pool_summary = {
            "index_count": len(shared_pool.get("x_indices", [])),
            "x_value_count": len(shared_pool.get("x_values", [])),
            "y_value_count": len(shared_pool.get("y_values", [])),
            "z_value_count": len(shared_pool.get("z_values", [])),
        }
    minimized = {
        "format": payload.get("format", "3D3"),
        "version": payload.get("version", 1),
        "signature": payload.get("signature"),
        "shape_offsets": payload.get("shape_offsets", []),
        "shape_names": payload.get("shape_names", {}),
        "model_data_size": payload.get("model_data_size"),
        "shared_vertex_pool_summary": shared_pool_summary,
        "trailing_byte_count": len(from_base64(payload.get("trailing_bytes", "")))
        if isinstance(payload.get("trailing_bytes"), str)
        else 0,
        "notes": (
            "Minimized 3D3 index. GLB files are authoritative for geometry; "
            "use decode or convert-tree --include-3d3-model-data for a full "
            "reverse-engineering dump with model_data."
        ),
    }
    return minimized


def cmd_decode(args: argparse.Namespace) -> int:
    fmt = args.format.upper() if args.format else _detect_format(Path(args.input))
    if not fmt:
        raise ValueError("cannot detect format from extension; pass --format")

    data = _read_binary(Path(args.input))
    payload = _decode_asset(data, fmt, args, source_path=Path(args.input))
    _write_json(Path(args.json), payload, pretty=args.pretty, media_sidecar=False)
    return 0


def cmd_convert_tree(args: argparse.Namespace) -> int:
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        if not getattr(args, "loadability_only", False):
            raise ValueError(f"input must be an existing directory: {args.input}")
        if getattr(args, "require_all", False):
            raise ValueError("--require-all needs an existing original input directory; omit it for source-free --loadability-only checks")
        print(
            f"warning: original input directory is missing: {args.input}; running source-free loadability checks only",
            file=sys.stderr,
        )

    output_root.mkdir(parents=True, exist_ok=True)

    sources = _iter_asset_paths(input_root, recursive=args.recursive)
    if not sources:
        raise ValueError("no supported asset files found")

    failed = 0
    for src in sources:
        fmt = _detect_format(src)
        if not fmt:
            continue

        relative = src.relative_to(input_root)
        dst_base = output_root / relative.with_suffix("")
        output_base = dst_base
        if fmt in {"3D3", "WLD", "3DT", "3DG"}:
            # Keep complex/gameplay-bearing formats isolated so their sidecars,
            # combined model, and per-shape model files are easy to browse and edit.
            output_base = _asset_output_dir(src, relative, input_root, output_root, fmt) / src.name

        output_base.parent.mkdir(parents=True, exist_ok=True)

        if fmt in {"3D3", "WLD", "3DT", "3DG"}:
            json_path = output_base.with_name(output_base.name + ".json")
        else:
            json_path = output_base.with_suffix(".json")
        json_path.parent.mkdir(parents=True, exist_ok=True)
        png_path = None
        if fmt == "PIC" and not args.no_png:
            png_path = str(output_base.with_suffix(".png"))

        gltf_path = None
        if fmt == "3D3":
            if args.models == "gltf":
                gltf_path = str(output_base.with_name(output_base.name + ".gltf"))
            elif args.models == "glb":
                gltf_path = str(output_base.with_name(output_base.name + ".glb"))

        decode_args = _decode_args(
            png=png_path,
            gltf=gltf_path,
            pretty=args.pretty,
        )

        try:
            payload = _decode_asset(_read_binary(src), fmt, decode_args, source_path=src)
        except Exception as exc:
            failed += 1
            if not args.continue_on_error:
                raise
            print(f"error: {exc} ({src})", file=sys.stderr)
            continue

        write_json_sidecar = fmt != "PIC" or png_path is None or getattr(args, "include_image_json", False)
        if write_json_sidecar:
            json_payload = payload
            if fmt == "3D3" and not getattr(args, "include_3d3_model_data", False):
                json_payload = _minimize_3d3_json(payload)
            _write_json(
                json_path,
                json_payload,
                pretty=args.pretty,
                media_sidecar=not (fmt == "3D3" and getattr(args, "include_3d3_model_data", False)),
            )
        else:
            _remove_if_exists(json_path)
        _remove_if_exists(json_path.with_suffix(".yaml"))

        if fmt == "3D3":
            _write_3d3_shape_models(
                output_base.parent,
                payload,
                args.models,
                args.pretty,
                write_cache=getattr(args, "include_glmesh_cache", False),
            )
            legacy_exts = [".json", ".yaml", ".gltf", ".glb", ".glmesh"]
            for legacy_ext in legacy_exts:
                _remove_if_exists(dst_base.with_suffix(legacy_ext))
        elif fmt in {"WLD", "3DT", "3DG"}:
            for legacy_ext in [".json", ".yaml"]:
                _remove_if_exists(dst_base.with_suffix(legacy_ext))

    return 0 if failed == 0 else 1


def cmd_export_fonts(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root)
    output_root = Path(args.output)
    if not (repo_root / "src" / "fontdata.h").exists():
        raise ValueError(f"repo root does not contain src/fontdata.h: {repo_root}")
    index = export_fonts(
        repo_root,
        output_root,
        write_bdf=not args.no_bdf,
        include_metadata=getattr(args, "include_metadata", False),
    )
    index_path = output_root / "fonts.json"
    if getattr(args, "include_metadata", False):
        _write_json(index_path, index, pretty=args.pretty)
    else:
        _remove_if_exists(index_path)
    return 0


def cmd_export_sounds(args: argparse.Namespace) -> int:
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        raise ValueError(f"input must be an existing directory: {args.input}")
    index = export_sounds(
        input_root,
        output_root,
        sample_rate=args.sample_rate,
        include_raw_blob=getattr(args, "include_raw_blob", False),
        include_metadata=getattr(args, "include_metadata", False),
    )
    if getattr(args, "include_metadata", False):
        _write_json(output_root / "sounds.json", index, pretty=args.pretty)
    return 0


def cmd_build_binary(args: argparse.Namespace) -> int:
    json_path = Path(args.json)
    payload = json.loads(json_path.read_text(encoding="utf-8"))
    fmt = args.format.upper() if args.format else str(payload.get("format", "")).upper()
    builders = {
        "3D3": build_3d3,
        "WLD": build_wld,
        "3DT": build_3dt,
        "3DG": build_3dg,
    }
    if fmt not in builders:
        raise ValueError("build-binary currently supports 3D3, WLD, 3DT, and 3DG JSON")
    sys.stdout.buffer.write(builders[fmt](payload))
    return 0


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def _write_rgba_png(path: Path, width: int, height: int, pixel_func) -> None:
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            row.extend(pixel_func(x, y, width, height))
        rows.append(bytes(row))
    data = b"".join(rows)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + _png_chunk(b"IDAT", zlib.compress(data, 9))
        + _png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def _read_png_metadata(path: Path) -> Dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    offset = 8
    saw_plte = False
    width = height = bit_depth = color_type = None
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + length
        if payload_end + 4 > len(data):
            raise ValueError("truncated PNG chunk")
        if kind == b"IHDR":
            if length != 13:
                raise ValueError("invalid IHDR length")
            width, height, bit_depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", data[payload_start:payload_end])
        elif kind == b"PLTE":
            saw_plte = True
        elif kind == b"IEND":
            break
        offset = payload_end + 4
    if width is None:
        raise ValueError("missing IHDR")
    return {
        "width": width,
        "height": height,
        "bit_depth": bit_depth,
        "color_type": color_type,
        "has_palette": saw_plte,
    }


def _png_color_model_from_metadata(meta: Dict[str, Any]) -> str:
    bit_depth = int(meta.get("bit_depth") or 0)
    color_type = int(meta.get("color_type") or -1)
    if bit_depth == 8 and color_type == 6:
        return "RGBA8888"
    if bit_depth == 8 and color_type == 2:
        return "RGB888"
    if color_type == 3 and meta.get("has_palette"):
        return "indexed_with_embedded_palette"
    return f"png_bit_depth_{bit_depth}_color_type_{color_type}"


def _read_wav_metadata(path: Path) -> Dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    offset = 12
    fmt: Dict[str, int] = {}
    data_bytes = 0
    while offset + 8 <= len(data):
        kind = data[offset:offset + 4]
        length = struct.unpack("<I", data[offset + 4:offset + 8])[0]
        payload_start = offset + 8
        payload_end = payload_start + length
        if payload_end > len(data):
            raise ValueError("truncated WAV chunk")
        if kind == b"fmt ":
            if length < 16:
                raise ValueError("invalid fmt chunk")
            audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample = struct.unpack(
                "<HHIIHH", data[payload_start:payload_start + 16]
            )
            fmt = {
                "audio_format": audio_format,
                "channels": channels,
                "sample_rate_hz": sample_rate,
                "byte_rate": byte_rate,
                "block_align": block_align,
                "bits_per_sample": bits_per_sample,
            }
        elif kind == b"data":
            data_bytes += length
        offset = payload_end + (length & 1)
    if not fmt:
        raise ValueError("missing fmt chunk")
    if data_bytes <= 0:
        raise ValueError("missing audio data")
    bytes_per_sample_frame = max(1, int(fmt["channels"]) * max(1, int(fmt["bits_per_sample"]) // 8))
    frame_count = data_bytes // bytes_per_sample_frame
    duration = frame_count / float(fmt["sample_rate_hz"]) if int(fmt["sample_rate_hz"]) > 0 else 0.0
    return {
        **fmt,
        "data_bytes": data_bytes,
        "frame_count": frame_count,
        "duration_seconds": duration,
    }


def _font_container_kind(path: Path) -> str:
    data = path.read_bytes()
    if len(data) >= 4 and data[:4] in (b"\x00\x01\x00\x00", b"true"):
        return "truetype"
    if len(data) >= 4 and data[:4] == b"OTTO":
        return "opentype_cff"
    if len(data) >= 4 and data[:4] == b"ttcf":
        return "truetype_collection"
    if data.startswith(b"STARTFONT"):
        return "bdf"
    if len(data) >= 8 and data[:8] == b"\x89PNG\r\n\x1a\n":
        return "png"
    raise ValueError("unsupported font container")


def _validate_font_container_for_extension(path: Path) -> Optional[str]:
    kind = _font_container_kind(path)
    suffix = path.suffix.lower()
    if suffix == ".ttf" and kind in {"truetype", "opentype_cff", "truetype_collection"}:
        return None
    if suffix == ".otf" and kind in {"opentype_cff", "truetype"}:
        return None
    if suffix == ".bdf" and kind == "bdf":
        return None
    if suffix == ".png" and kind == "png":
        return None
    return f"extension {suffix or '<none>'} does not match font container {kind}"


def _read_glb_metadata(path: Path) -> Dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("GLB too short")
    magic, version, total_length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError("bad GLB magic")
    if version != 2:
        raise ValueError(f"unsupported GLB version {version}")
    if total_length != len(data):
        raise ValueError(f"GLB length header mismatch: header={total_length}, actual={len(data)}")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("first GLB chunk is not JSON")
    json_end = 20 + json_length
    if json_end > len(data):
        raise ValueError("truncated GLB JSON chunk")
    doc = json.loads(data[20:json_end].decode("utf-8"))
    mesh_count = len(doc.get("meshes") or []) if isinstance(doc.get("meshes"), list) else 0
    material_count = len(doc.get("materials") or []) if isinstance(doc.get("materials"), list) else 0
    return {
        "version": version,
        "byte_length": len(data),
        "mesh_count": mesh_count,
        "material_count": material_count,
    }


def _validate_asound_music_payload(payload: Dict[str, Any], json_path: Path) -> list[str]:
    errors: list[str] = []
    voices = payload.get("voices")
    if payload.get("format") != "F15_ASOUND_MUSIC":
        errors.append(f"{json_path}: format must be F15_ASOUND_MUSIC")
    if int(payload.get("version") or 0) != 1:
        errors.append(f"{json_path}: version must be 1")
    if int(payload.get("tick_hz") or 0) <= 0:
        errors.append(f"{json_path}: tick_hz must be positive")
    if not isinstance(voices, list) or len(voices) != 12:
        errors.append(f"{json_path}: expected 12 intro/release voices")
        return errors

    by_symbol = {voice.get("source_symbol"): voice for voice in voices if isinstance(voice, dict)}
    for phase in ("intro", "release"):
        for voice_id in range(6):
            symbol = f"asound_{phase}_voice{voice_id}"
            voice = by_symbol.get(symbol)
            if not isinstance(voice, dict):
                errors.append(f"{json_path}: missing voice {symbol}")
                continue
            if voice.get("phase") != phase:
                errors.append(f"{json_path}: {symbol} phase must be {phase}")
            if int(voice.get("voice") if voice.get("voice") is not None else -1) != voice_id:
                errors.append(f"{json_path}: {symbol} voice index must be {voice_id}")
            stream = voice.get("stream_bytes")
            if not isinstance(stream, list) or not stream:
                errors.append(f"{json_path}: {symbol} stream_bytes must be a non-empty list")
                continue
            for pos, value in enumerate(stream):
                try:
                    byte = int(value)
                except (TypeError, ValueError):
                    errors.append(f"{json_path}: {symbol} stream byte {pos} is not numeric")
                    break
                if byte < 0 or byte > 255:
                    errors.append(f"{json_path}: {symbol} stream byte {pos} out of byte range: {byte}")
                    break
            if len(stream) < 2 or int(stream[-2]) != 0 or int(stream[-1]) != 0:
                errors.append(f"{json_path}: {symbol} stream should end with ASOUND zero-note terminator")
    return errors


def _mix(a: int, b: int, t: float) -> int:
    return max(0, min(255, int(a + (b - a) * t)))


def _star_mask(x: int, y: int, cx: float, cy: float, r: float) -> bool:
    # Five-point star approximation: alternating angular radius, good enough for
    # generated starter art without depending on Pillow or vector renderers.
    import math
    dx = x - cx
    dy = y - cy
    dist = math.hypot(dx, dy)
    if dist > r:
        return False
    angle = math.atan2(dy, dx) + math.pi / 2
    wave = (math.cos(angle * 5) + 1) / 2
    edge = r * (0.45 + 0.55 * wave)
    return dist <= edge


def _line_dist(x: int, y: int, x1: float, y1: float, x2: float, y2: float) -> float:
    # Distance from pixel center to a finite line segment. Used by generated art
    # for route lines, aircraft silhouettes, and radar sweeps without depending
    # on Pillow/vector renderers.
    import math
    vx = x2 - x1
    vy = y2 - y1
    wx = x - x1
    wy = y - y1
    denom = vx * vx + vy * vy
    if denom <= 0:
        return math.hypot(x - x1, y - y1)
    t = max(0.0, min(1.0, (wx * vx + wy * vy) / denom))
    px = x1 + t * vx
    py = y1 + t * vy
    return math.hypot(x - px, y - py)


def _write_soviet_vietnam_starter_art(out_dir: Path) -> list[dict[str, str]]:
    title640 = out_dir / "TITLE640.png"
    title = out_dir / "TITLE.png"
    desk = out_dir / "DESK.png"
    wall = out_dir / "WALL.png"
    hiscore = out_dir / "HISCORE.png"
    armpiece = out_dir / "ARMPIECE.png"
    theater_map = out_dir / "VN.png"
    arm_dir = out_dir / "start" / "menu" / "arm"
    title640_size = (3840, 2100)
    full_page_size = (2560, 1600)
    panel_size = (1920, 1200)
    map_size = (1792, 1344)
    full_page_target = (320, 200)
    title640_target = (640, 350)
    map_target = (224, 168)

    def art_entry(
        file_name: str,
        replaces: str,
        kind: str,
        source_size: tuple[int, int],
        color_model: str,
        target_size: tuple[int, int],
        runtime_path: str,
    ) -> dict[str, Any]:
        """Describe high-resolution art as source pixels fitted into legacy space.

        Custom PNGs are the editable source of truth, but the game/UI placement
        remains in the original logical coordinate system. Keeping the target
        rectangle in metadata lets editors and launchers preview replacements
        without assuming that source pixels are game-space pixels.
        """
        return {
            "file": file_name,
            "replaces": replaces,
            "kind": kind,
            "width": source_size[0],
            "height": source_size[1],
            "target_width": target_size[0],
            "target_height": target_size[1],
            "scaling": "fit_original_game_rectangle",
            "color_model": color_model,
            "runtime_path": runtime_path,
            "replacement_contract": _modern_replacement_contract(
                runtime_path,
                "PNG",
                replaces,
            ),
            "legacy_limits_removed": {
                "source_resolution": True,
                "source_color_count": color_model in {"RGB888", "RGBA8888"},
                "legacy_palette_required": False,
                "source_pixels_are_game_pixels": False,
            },
            "license_status": "generated_placeholder_needs_replacement_or_review",
        }

    try:
        from PIL import Image, ImageDraw, ImageFilter, ImageFont
    except Exception:
        Image = None
        ImageDraw = None
        ImageFilter = None
        ImageFont = None

    if Image is not None and ImageDraw is not None:
        def raster_paint(image: Any, seed: int, preserve_alpha: bool = False) -> Any:
            """Add deterministic bitmap-paint texture to generated placeholder art.

            The SVN campaign art should be editable raster PNG source material,
            not clean vector-style mockups. This keeps generation reproducible
            while adding grain, soft color washes, and canvas-like fibers.
            """
            import random

            rng = random.Random(seed)
            base = image.convert("RGBA")
            w, h = base.size
            alpha = base.getchannel("A") if preserve_alpha else None
            blur_size = max(1, w // 480)
            wash = Image.new("RGBA", (max(1, w // 12), max(1, h // 12)), (0, 0, 0, 0))
            wash_draw = ImageDraw.Draw(wash, "RGBA")
            for _ in range(max(24, (w * h) // 42000)):
                x = rng.randrange(wash.size[0])
                y = rng.randrange(wash.size[1])
                rx = rng.randrange(2, max(3, wash.size[0] // 9))
                ry = rng.randrange(2, max(3, wash.size[1] // 9))
                wash_draw.ellipse(
                    (x - rx, y - ry, x + rx, y + ry),
                    fill=(
                        rng.randrange(20, 235),
                        rng.randrange(20, 235),
                        rng.randrange(20, 235),
                        rng.randrange(12, 34),
                    ),
                )
            wash = wash.resize((w, h), resample=Image.Resampling.BICUBIC).filter(ImageFilter.GaussianBlur(blur_size))
            base = Image.alpha_composite(base, wash)
            grain = Image.effect_noise((w, h), 22).convert("L")
            grain_rgba = Image.merge("RGBA", (grain, grain, grain, Image.new("L", (w, h), 255)))
            base = Image.blend(base, grain_rgba, 0.055)
            fiber = Image.new("RGBA", (max(1, w // 4), max(1, h // 4)), (0, 0, 0, 0))
            fiber_draw = ImageDraw.Draw(fiber, "RGBA")
            for y in range(0, fiber.size[1], 3):
                shade = rng.randrange(180, 240)
                fiber_draw.line([(0, y), (fiber.size[0], y)], fill=(shade, shade, shade, 10), width=1)
            fiber = fiber.resize((w, h), resample=Image.Resampling.BILINEAR)
            base = Image.alpha_composite(base, fiber)
            if alpha is not None:
                base.putalpha(alpha)
            return base

        def save_rgb(path: Path, image: Any) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            raster_paint(image, sum(path.name.encode("utf-8"))).convert("RGB").save(path)

        def save_rgba(path: Path, image: Any) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            raster_paint(image, sum(str(path).encode("utf-8")), preserve_alpha=True).save(path)

        def gradient(size: tuple[int, int], top: tuple[int, int, int], bottom: tuple[int, int, int]) -> Any:
            w, h = size
            strip = Image.new("RGB", (1, h))
            pixels = strip.load()
            for y in range(h):
                t = y / max(1, h - 1)
                pixels[0, y] = tuple(_mix(top[i], bottom[i], t) for i in range(3))
            return strip.resize((w, h))

        def load_font(size: int, bold: bool = False) -> Any:
            # Use widely installed DejaVu fonts when available. They include
            # Cyrillic glyphs, so the generated campaign art can show the
            # Russian Li Si Cin fiction hook without requiring bundled fonts.
            if ImageFont is None:
                return None
            candidates = [
                "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            ]
            for candidate in candidates:
                try:
                    return ImageFont.truetype(candidate, size=size)
                except Exception:
                    continue
            try:
                return ImageFont.load_default()
            except Exception:
                return None

        def text_shadow(
            draw: Any,
            xy: tuple[float, float],
            text: str,
            font: Any,
            fill: tuple[int, int, int, int],
            shadow: tuple[int, int, int, int] = (0, 0, 0, 190),
            offset: int = 4,
        ) -> None:
            # Pillow text is only decorative here; runtime menu text is still
            # drawn by the game. Shadow keeps generated art legible after the
            # high-resolution PNG is scaled into the original screen rectangle.
            if font is None:
                return
            draw.text((xy[0] + offset, xy[1] + offset), text, font=font, fill=shadow)
            draw.text(xy, text, font=font, fill=fill)

        def draw_star(draw: Any, cx: float, cy: float, radius: float, fill: tuple[int, int, int, int]) -> None:
            import math
            pts = []
            for i in range(10):
                a = -math.pi / 2 + i * math.pi / 5
                r = radius if i % 2 == 0 else radius * 0.42
                pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
            draw.polygon(pts, fill=fill)

        def draw_campaign_title(path: Path, size: tuple[int, int]) -> None:
            w, h = size
            im = gradient(size, (9, 25, 39), (72, 66, 42)).convert("RGBA")
            d = ImageDraw.Draw(im, "RGBA")
            # Backdrop: red dawn, Vietnam coast/river, radar rings, and the
            # stolen F-15 strike track. This is deliberately bold so it remains
            # readable after runtime scales it into legacy screen rectangles.
            d.ellipse((w * 0.58, -h * 0.08, w * 0.86, h * 0.31), fill=(225, 82, 36, 150))
            d.polygon([(0, h), (0, h * 0.78), (w * 0.18, h * 0.74), (w * 0.44, h * 0.80), (w, h * 0.70), (w, h)], fill=(43, 56, 36, 255))
            d.line([(w * 0.45, 0), (w * 0.40, h * 0.25), (w * 0.46, h * 0.48), (w * 0.37, h * 0.78), (w * 0.42, h)], fill=(192, 116, 61, 210), width=max(4, int(h * 0.008)))
            d.line([(w * 0.60, 0), (w * 0.62, h * 0.20), (w * 0.56, h * 0.42), (w * 0.64, h * 0.70), (w * 0.60, h)], fill=(35, 142, 178, 230), width=max(12, int(h * 0.030)))
            radar = (w * 0.80, h * 0.34)
            for r in (0.10, 0.20, 0.30):
                rr = h * r
                d.ellipse((radar[0] - rr, radar[1] - rr, radar[0] + rr, radar[1] + rr), outline=(126, 214, 178, 115), width=max(2, int(h * 0.0025)))
            d.line([radar, (w * 0.58, h * 0.18)], fill=(126, 214, 178, 170), width=max(2, int(h * 0.004)))
            draw_star(d, w * 0.18, h * 0.24, h * 0.145, (218, 36, 32, 235))
            d.line([(w * 0.20, h * 0.70), (w * 0.42, h * 0.60), (w * 0.63, h * 0.48), (w * 0.81, h * 0.32)], fill=(238, 212, 92, 190), width=max(3, int(h * 0.004)))
            # Stolen F-15 silhouette, afterburner, MiG cover, carrier target,
            # and flak bursts.
            d.line([(w * 0.34, h * 0.58), (w * 0.69, h * 0.50)], fill=(230, 226, 204, 245), width=max(6, int(h * 0.010)))
            d.line([(w * 0.47, h * 0.55), (w * 0.55, h * 0.42)], fill=(230, 226, 204, 235), width=max(6, int(h * 0.012)))
            d.line([(w * 0.48, h * 0.56), (w * 0.58, h * 0.66)], fill=(230, 226, 204, 235), width=max(6, int(h * 0.012)))
            d.line([(w * 0.37, h * 0.57), (w * 0.32, h * 0.49)], fill=(230, 226, 204, 230), width=max(5, int(h * 0.010)))
            d.line([(w * 0.37, h * 0.57), (w * 0.34, h * 0.65)], fill=(230, 226, 204, 230), width=max(5, int(h * 0.010)))
            d.line([(w * 0.30, h * 0.57), (w * 0.20, h * 0.60)], fill=(255, 92, 28, 220), width=max(6, int(h * 0.012)))
            d.line([(w * 0.58, h * 0.36), (w * 0.82, h * 0.31)], fill=(186, 214, 162, 225), width=max(4, int(h * 0.007)))
            d.line([(w * 0.67, h * 0.34), (w * 0.74, h * 0.25)], fill=(186, 214, 162, 220), width=max(4, int(h * 0.007)))
            d.line([(w * 0.68, h * 0.35), (w * 0.77, h * 0.43)], fill=(186, 214, 162, 220), width=max(4, int(h * 0.007)))
            d.polygon([(w * 0.66, h * 0.77), (w * 0.89, h * 0.74), (w * 0.83, h * 0.80), (w * 0.67, h * 0.81)], fill=(68, 76, 72, 230))
            for cx, cy in ((0.61, 0.43), (0.64, 0.47), (0.73, 0.39)):
                rr = max(12, int(h * 0.018))
                d.ellipse((w * cx - rr, h * cy - rr, w * cx + rr, h * cy + rr), outline=(255, 205, 84, 230), width=max(2, int(h * 0.003)))
            headline = load_font(max(24, int(h * 0.072)), bold=True)
            subhead = load_font(max(14, int(h * 0.026)), bold=True)
            small = load_font(max(12, int(h * 0.019)), bold=False)
            text_shadow(d, (w * 0.070, h * 0.070), "ОПЕРАЦИЯ ЛИ СИ ЦИН", headline, (246, 226, 156, 245), offset=max(3, int(h * 0.004)))
            text_shadow(d, (w * 0.075, h * 0.160), "STOLEN F-15 / VIETNAM FRONT", subhead, (177, 219, 176, 235), offset=max(2, int(h * 0.003)))
            text_shadow(d, (w * 0.075, h * 0.205), "custom modern asset campaign", small, (211, 198, 143, 220), offset=max(2, int(h * 0.002)))
            text_shadow(d, (w * 0.655, h * 0.675), "YANKEE CARRIER", small, (238, 211, 108, 230), offset=max(2, int(h * 0.002)))
            save_rgb(path, im)

        def draw_ops_board(path: Path, size: tuple[int, int], kind: str) -> None:
            w, h = size
            colors = {
                "desk": ((15, 43, 37), (34, 68, 51)),
                "wall": ((28, 32, 24), (44, 54, 36)),
                "hiscore": ((24, 36, 32), (58, 66, 48)),
                "armpiece": ((31, 42, 33), (81, 84, 58)),
            }
            im = gradient(size, *colors[kind]).convert("RGBA")
            d = ImageDraw.Draw(im, "RGBA")
            if kind in {"desk", "wall"}:
                margin = w * (0.08 if kind == "desk" else 0.12)
                board = (margin, h * 0.12, w - margin, h * 0.82)
                d.rectangle(board, fill=(38, 64, 46, 230), outline=(126, 118, 75, 180), width=max(3, int(h * 0.004)))
                step = max(32, int(w / 24))
                x = int(board[0])
                while x < board[2]:
                    d.line([(x, board[1]), (x, board[3])], fill=(64, 92, 66, 100), width=2)
                    x += step
                y = int(board[1])
                while y < board[3]:
                    d.line([(board[0], y), (board[2], y)], fill=(64, 92, 66, 100), width=2)
                    y += step
                d.line([(w * 0.24, h * 0.66), (w * 0.45, h * 0.52), (w * 0.69, h * 0.40)], fill=(218, 58, 45, 220), width=max(4, int(h * 0.006)))
                d.line([(w * 0.31, h * 0.18), (w * 0.25, h * 0.50), (w * 0.33, h * 0.78)], fill=(192, 118, 64, 210), width=max(5, int(h * 0.008)))
                board_font = load_font(max(14, int(h * 0.032)), bold=True)
                small_font = load_font(max(10, int(h * 0.020)), bold=False)
                text_shadow(d, (w * 0.145, h * 0.145), "SVN STRIKE BOARD", board_font, (224, 210, 146, 230), offset=max(2, int(h * 0.002)))
                text_shadow(d, (w * 0.145, h * 0.200), "HANOI / TONKIN / DANANG", small_font, (165, 213, 164, 220), offset=max(2, int(h * 0.002)))
                if kind == "wall":
                    d.ellipse((w * 0.74, h * 0.02, w * 0.90, h * 0.18), fill=(224, 50, 35, 145))
            elif kind == "hiscore":
                d.rounded_rectangle((w * 0.10, h * 0.16, w * 0.90, h * 0.88), radius=int(h * 0.035), fill=(63, 72, 52, 230), outline=(193, 154, 71, 180), width=max(4, int(h * 0.006)))
                draw_star(d, w * 0.20, h * 0.30, h * 0.16, (202, 34, 31, 230))
                d.ellipse((w * 0.64, h * 0.16, w * 0.88, h * 0.44), outline=(218, 178, 79, 230), width=max(5, int(h * 0.007)))
                text_shadow(d, (w * 0.290, h * 0.250), "HEROES OF THE FRONT", load_font(max(16, int(h * 0.040)), True), (238, 222, 154, 235), offset=max(2, int(h * 0.003)))
                text_shadow(d, (w * 0.290, h * 0.330), "Li Si Cin squadron records", load_font(max(12, int(h * 0.026)), False), (172, 216, 168, 220), offset=max(2, int(h * 0.002)))
            else:
                d.polygon([(0, h), (w * 0.42, h), (w * 0.38, h * 0.20), (0, h * 0.18)], fill=(57, 76, 53, 230))
                d.polygon([(w * 0.34, h * 0.14), (w * 0.91, h * 0.14), (w * 0.86, h * 0.84), (w * 0.38, h * 0.84)], fill=(189, 178, 137, 245))
                for y in range(int(h * 0.22), int(h * 0.78), max(20, int(h * 0.055))):
                    d.line([(w * 0.41, y), (w * 0.83, y)], fill=(116, 137, 106, 150), width=max(2, int(h * 0.003)))
                d.ellipse((w * 0.67, h * 0.19, w * 0.85, h * 0.37), outline=(178, 34, 30, 210), width=max(4, int(h * 0.006)))
                text_shadow(d, (w * 0.430, h * 0.190), "ОСОБОЕ ЗАДАНИЕ", load_font(max(14, int(h * 0.038)), True), (82, 67, 42, 235), shadow=(245, 234, 190, 155), offset=max(2, int(h * 0.002)))
                text_shadow(d, (w * 0.430, h * 0.270), "pilot: Lisicin / Li Si Cin", load_font(max(10, int(h * 0.024)), False), (83, 95, 69, 230), shadow=(245, 234, 190, 130), offset=max(2, int(h * 0.002)))
            save_rgb(path, im)

        def draw_theater_map(path: Path) -> None:
            w, h = map_size
            im = Image.new("RGBA", map_size, (41, 91, 118, 255))
            d = ImageDraw.Draw(im, "RGBA")
            d.polygon([(0, 0), (w * 0.55, 0), (w * 0.50, h * 0.18), (w * 0.58, h * 0.35), (w * 0.49, h * 0.58), (w * 0.56, h), (0, h)], fill=(70, 118, 67, 255))
            d.polygon([(0, 0), (w * 0.34, 0), (w * 0.28, h * 0.42), (w * 0.38, h), (0, h)], fill=(130, 92, 54, 235))
            d.line([(w * 0.48, 0), (w * 0.52, h * 0.24), (w * 0.46, h * 0.50), (w * 0.60, h)], fill=(62, 145, 160, 230), width=max(8, int(w * 0.010)))
            step_x = max(64, w // 14)
            step_y = max(64, h // 12)
            for x in range(0, w, step_x):
                d.line([(x, 0), (x, h)], fill=(28, 42, 34, 75), width=2)
            for y in range(0, h, step_y):
                d.line([(0, y), (w, y)], fill=(28, 42, 34, 75), width=2)
            d.line([(w * 0.45, h * 0.18), (w * 0.56, h * 0.28), (w * 0.65, h * 0.48), (w * 0.50, h * 0.76)], fill=(226, 206, 88, 220), width=max(4, int(w * 0.004)))
            for px, py in ((0.50, 0.16), (0.63, 0.26), (0.64, 0.45), (0.48, 0.76)):
                rr = max(12, int(w * 0.018))
                d.ellipse((w * px - rr, h * py - rr, w * px + rr, h * py + rr), outline=(190, 36, 32, 230), width=max(3, int(w * 0.003)))
            map_font = load_font(max(12, int(h * 0.035)), bold=True)
            label_font = load_font(max(10, int(h * 0.024)), bold=False)
            text_shadow(d, (w * 0.055, h * 0.055), "SVN TARGET MAP", map_font, (238, 226, 157, 235), offset=max(2, int(h * 0.002)))
            text_shadow(d, (w * 0.510, h * 0.120), "HANOI", label_font, (28, 24, 18, 220), shadow=(236, 220, 147, 170), offset=max(1, int(h * 0.0015)))
            text_shadow(d, (w * 0.650, h * 0.255), "TONKIN", label_font, (28, 24, 18, 220), shadow=(236, 220, 147, 170), offset=max(1, int(h * 0.0015)))
            text_shadow(d, (w * 0.490, h * 0.775), "DANANG", label_font, (28, 24, 18, 220), shadow=(236, 220, 147, 170), offset=max(1, int(h * 0.0015)))
            save_rgba(path, im)

        def draw_arm_cel(path: Path, frame: int) -> None:
            w, h = full_page_size
            shoulder = (w * 0.18, h * 0.78)
            targets = [
                (w * 0.66, h * 0.24),
                (w * 0.68, h * 0.34),
                (w * 0.70, h * 0.45),
                (w * 0.67, h * 0.56),
                (w * 0.64, h * 0.67),
                (w * 0.48, h * 0.80),
                (w * 0.34, h * 0.86),
            ]
            wrist = targets[max(0, min(frame, len(targets) - 1))]
            im = Image.new("RGBA", full_page_size, (0, 0, 0, 0))
            d = ImageDraw.Draw(im, "RGBA")
            d.line([shoulder, wrist], fill=(55, 74, 51, 230), width=max(1, int(h * 0.060)))
            cuff_r = h * 0.040
            d.ellipse((wrist[0] - cuff_r, wrist[1] - cuff_r, wrist[0] + cuff_r, wrist[1] + cuff_r), outline=(172, 176, 132, 235), width=max(1, int(h * 0.018)))
            d.line([wrist, (wrist[0] + w * 0.085, wrist[1] - h * 0.030)], fill=(218, 188, 102, 235), width=max(1, int(h * 0.020)))
            save_rgba(path, im)

        draw_campaign_title(title640, title640_size)
        draw_campaign_title(title, full_page_size)
        draw_ops_board(desk, panel_size, "desk")
        draw_ops_board(wall, full_page_size, "wall")
        draw_ops_board(hiscore, panel_size, "hiscore")
        draw_ops_board(armpiece, panel_size, "armpiece")
        draw_theater_map(theater_map)
        arm_dir.mkdir(parents=True, exist_ok=True)
        arm_entries = []
        for frame in range(7):
            arm_path = arm_dir / f"{frame}.png"
            draw_arm_cel(arm_path, frame)
            arm_entries.append(
                art_entry(
                    str(arm_path.relative_to(out_dir)),
                    f"start/menu/arm/{frame}.png",
                    "high_resolution_truecolor_briefing_arm_cel_png",
                    full_page_size,
                    "RGBA8888",
                    full_page_target,
                    "high_resolution_truecolor_overlay",
                )
            )
        return [
            art_entry(title640.name, "TITLE640.PIC", "high_resolution_truecolor_png", title640_size, "RGB888", title640_target, "high_resolution_truecolor_title"),
            art_entry(title.name, "TITLE.PIC", "high_resolution_truecolor_png", full_page_size, "RGB888", full_page_target, "truecolor_page_backdrop"),
            art_entry(desk.name, "DESK.PIC", "high_resolution_truecolor_png", panel_size, "RGB888", full_page_target, "truecolor_page_backdrop"),
            art_entry(wall.name, "WALL.PIC", "high_resolution_truecolor_png", full_page_size, "RGB888", full_page_target, "truecolor_page_backdrop"),
            art_entry(hiscore.name, "HISCORE.PIC", "high_resolution_truecolor_png", panel_size, "RGB888", full_page_target, "truecolor_page_backdrop"),
            art_entry(armpiece.name, "ARMPIECE.PIC", "high_resolution_truecolor_png", panel_size, "RGB888", full_page_target, "truecolor_page_backdrop"),
            art_entry(theater_map.name, "VN.SPR", "high_resolution_truecolor_theater_map_png", map_size, "RGBA8888", map_target, "high_resolution_truecolor_map"),
        ] + arm_entries

    def title_px(x: int, y: int, w: int, h: int) -> bytes:
        import math
        t = y / max(1, h - 1)
        sun = math.hypot(x - w * 0.72, y - h * 0.23)
        river = abs(x - (w * 0.60 + math.sin(y * 0.016) * w * 0.085)) < w * 0.030
        coast = abs(x - (w * 0.44 + math.sin(y * 0.011) * w * 0.055)) < w * 0.010
        radar_center = (w * 0.80, h * 0.34)
        radar = abs(math.hypot(x - radar_center[0], y - radar_center[1]) % (h * 0.105)) < 2.2
        sweep_angle = math.atan2(y - radar_center[1], x - radar_center[0])
        sweep = abs(math.sin(sweep_angle - 0.65)) < 0.012 and math.hypot(x - radar_center[0], y - radar_center[1]) < h * 0.34
        star = _star_mask(x, y, w * 0.18, h * 0.24, h * 0.145)
        # Stolen F-15 / opposing MiG silhouettes: simple high-contrast aircraft
        # strokes keep the art readable after downscaling into the game viewport.
        f15_body = _line_dist(x, y, w * 0.34, h * 0.58, w * 0.69, h * 0.50) < h * 0.010
        f15_wing = _line_dist(x, y, w * 0.47, h * 0.55, w * 0.55, h * 0.42) < h * 0.011 or _line_dist(x, y, w * 0.48, h * 0.56, w * 0.58, h * 0.66) < h * 0.011
        f15_tail = _line_dist(x, y, w * 0.37, h * 0.57, w * 0.32, h * 0.49) < h * 0.010 or _line_dist(x, y, w * 0.38, h * 0.57, w * 0.34, h * 0.65) < h * 0.010
        f15_afterburner = _line_dist(x, y, w * 0.30, h * 0.57, w * 0.20, h * 0.60) < h * 0.012
        mig_body = _line_dist(x, y, w * 0.58, h * 0.36, w * 0.82, h * 0.31) < h * 0.007
        mig_wing = _line_dist(x, y, w * 0.67, h * 0.34, w * 0.74, h * 0.25) < h * 0.007 or _line_dist(x, y, w * 0.68, h * 0.35, w * 0.77, h * 0.43) < h * 0.007
        route = _line_dist(x, y, w * 0.20, h * 0.70, w * 0.81, h * 0.32) < 2.6 and ((x // 24) % 2 == 0)
        carrier = w * 0.66 < x < w * 0.88 and abs((y - h * 0.76) - (x - w * 0.66) * -0.055) < h * 0.018
        flak = any(abs(math.hypot(x - cx, y - cy) - h * 0.016) < 2.0 for cx, cy in ((w * 0.61, h * 0.43), (w * 0.64, h * 0.47), (w * 0.73, h * 0.39)))
        mountains = y > h * (0.72 + 0.045 * math.sin(x * 0.018) + 0.030 * math.sin(x * 0.043))
        r = _mix(10, 74, t)
        g = _mix(26, 72, t)
        b = _mix(39, 46, t)
        if sun < h * 0.18:
            glow = 1.0 - sun / (h * 0.18)
            r = _mix(r, 232, glow * 0.75)
            g = _mix(g, 86, glow * 0.45)
            b = _mix(b, 34, glow * 0.25)
        if mountains:
            r, g, b = 46, 56, 36
        if river:
            r, g, b = 34, 138, 174
        if coast:
            r, g, b = 182, 111, 61
        if radar or sweep:
            r, g, b = 128, 208, 174
        if route:
            r, g, b = 235, 204, 88
        if star:
            r, g, b = 218, 36, 32
        if f15_body or f15_wing or f15_tail:
            r, g, b = 230, 226, 204
        if f15_afterburner:
            r, g, b = 244, 88, 36
        if mig_body or mig_wing:
            r, g, b = 186, 214, 162
        if carrier:
            r, g, b = 68, 76, 72
        if flak:
            r, g, b = 242, 182, 76
        vignette = 1.0 - min(0.55, math.hypot((x / w) - 0.5, (y / h) - 0.5))
        return bytes([int(r * vignette), int(g * vignette), int(b * vignette), 255])

    def desk_px(x: int, y: int, w: int, h: int) -> bytes:
        import math
        grid = (x % 80 < 2) or (y % 80 < 2)
        scan = (y % 9) == 0
        coast = abs((x / w) - (0.35 + math.sin(y * 0.014) * 0.08)) < 0.018
        target = abs(math.hypot(x - w * 0.68, y - h * 0.42) - 70) < 3
        r, g, b = 17, 47, 38
        if grid:
            r, g, b = 42, 82, 62
        if coast:
            r, g, b = 188, 114, 62
        if target:
            r, g, b = 221, 64, 52
        if scan:
            r = min(255, r + 10)
            g = min(255, g + 18)
            b = min(255, b + 10)
        return bytes([r, g, b, 255])

    def wall_px(x: int, y: int, w: int, h: int) -> bytes:
        import math
        # Mission room background: map board, red operations light, and subtle
        # paper-grid geometry so the legacy menu text still reads over it.
        board = w * 0.12 < x < w * 0.88 and h * 0.12 < y < h * 0.78
        grid = board and ((x % 72 < 2) or (y % 72 < 2))
        coast = board and abs((x / w) - (0.30 + math.sin(y * 0.018) * 0.10)) < 0.014
        route = board and abs((y - h * 0.58) - math.sin(x * 0.015) * h * 0.08) < 4
        lamp = math.hypot(x - w * 0.82, y - h * 0.12) < h * 0.10
        r, g, b = 30, 34, 25
        if board:
            r, g, b = 37, 62, 45
        if grid:
            r, g, b = 56, 86, 62
        if coast:
            r, g, b = 188, 127, 67
        if route:
            r, g, b = 215, 55, 45
        if lamp:
            glow = 1.0 - min(1.0, math.hypot(x - w * 0.82, y - h * 0.12) / (h * 0.10))
            r = _mix(r, 225, glow)
            g = _mix(g, 42, glow)
            b = _mix(b, 34, glow)
        return bytes([r, g, b, 255])

    def hiscore_px(x: int, y: int, w: int, h: int) -> bytes:
        import math
        stripe = ((x + y) // 48) % 2 == 0
        star = _star_mask(x, y, w * 0.20, h * 0.30, h * 0.16)
        medal = abs(math.hypot(x - w * 0.76, y - h * 0.30) - h * 0.12) < 6
        panel = w * 0.10 < x < w * 0.90 and h * 0.16 < y < h * 0.88
        r, g, b = (26, 38, 34) if stripe else (34, 48, 40)
        if panel:
            r, g, b = _mix(r, 61, 0.7), _mix(g, 72, 0.7), _mix(b, 53, 0.7)
        if star:
            r, g, b = 202, 34, 31
        if medal:
            r, g, b = 218, 178, 79
        return bytes([r, g, b, 255])

    def armpiece_px(x: int, y: int, w: int, h: int) -> bytes:
        import math
        sleeve = x < w * 0.42 and y > h * 0.18
        paper = w * 0.34 < x < w * 0.91 and h * 0.14 < y < h * 0.84
        line = paper and (y % 34 < 2)
        stamp = paper and abs(math.hypot(x - w * 0.76, y - h * 0.28) - h * 0.09) < 5
        r, g, b = 31, 42, 33
        if sleeve:
            r, g, b = 57, 76, 53
        if paper:
            r, g, b = 189, 178, 137
        if line:
            r, g, b = 116, 137, 106
        if stamp:
            r, g, b = 178, 34, 30
        return bytes([r, g, b, 255])

    def arm_cel_px(frame: int):
        def px(x: int, y: int, w: int, h: int) -> bytes:
            import math
            # Full-frame transparent pointer-arm cel. The runtime scales it with
            # the briefing wall height, so each cel must keep the wall's aspect.
            shoulder = (w * 0.18, h * 0.78)
            targets = [
                (w * 0.66, h * 0.24),
                (w * 0.68, h * 0.34),
                (w * 0.70, h * 0.45),
                (w * 0.67, h * 0.56),
                (w * 0.64, h * 0.67),
                (w * 0.48, h * 0.80),
                (w * 0.34, h * 0.86),
            ]
            wrist = targets[max(0, min(frame, len(targets) - 1))]
            sleeve = _line_dist(x, y, shoulder[0], shoulder[1], wrist[0], wrist[1]) < h * 0.030
            cuff = abs(math.hypot(x - wrist[0], y - wrist[1]) - h * 0.030) < h * 0.010
            pointer = _line_dist(x, y, wrist[0], wrist[1], wrist[0] + w * 0.085, wrist[1] - h * 0.030) < h * 0.010
            if pointer:
                return bytes([218, 188, 102, 235])
            if cuff:
                return bytes([172, 176, 132, 235])
            if sleeve:
                stripe = 0.72 + 0.18 * math.sin((x + y) * 0.035)
                return bytes([int(62 * stripe), int(84 * stripe), int(58 * stripe), 230])
            return bytes([0, 0, 0, 0])

        return px

    def map_px(x: int, y: int, w: int, h: int) -> bytes:
        import math
        nx = x / max(1, w - 1)
        ny = y / max(1, h - 1)
        coast = 0.54 + math.sin(ny * 10.5) * 0.045 + math.sin(ny * 31.0) * 0.015
        river = abs(nx - (0.48 + math.sin(ny * 18.0) * 0.040)) < 0.010
        delta = ny > 0.68 and abs(nx - (0.60 + math.sin(ny * 26.0) * 0.050)) < 0.045
        grid = (x % (w // 14) < 2) or (y % (h // 12) < 2)
        route = abs((ny - 0.18) - (nx - 0.55) * 0.95) < 0.010 and 0.40 < nx < 0.76
        target_ring = any(
            abs(math.hypot(nx - px, ny - py) - 0.030) < 0.004
            for px, py in ((0.50, 0.16), (0.63, 0.26), (0.64, 0.45), (0.48, 0.76))
        )
        land = nx < coast
        highlands = land and nx < 0.38 and (math.sin(nx * 38.0 + ny * 17.0) + math.sin(nx * 17.0)) > 0.45
        if land:
            r, g, b = (68, 116, 66)
        else:
            r, g, b = (39, 88, 112)
        if highlands:
            r, g, b = (129, 91, 53)
        if river or delta:
            r, g, b = (60, 142, 156)
        if grid:
            r, g, b = _mix(r, 28, 0.20), _mix(g, 42, 0.20), _mix(b, 34, 0.20)
        if route:
            r, g, b = (226, 206, 88)
        if target_ring:
            r, g, b = (190, 36, 32)
        return bytes([r, g, b, 255])

    def write_arm_cel_png(path: Path, frame: int) -> None:
        """Write transparent briefing arm cels as vector strokes when Pillow exists.

        The old pixel callback rendered seven full 2560x1600 cels by evaluating
        geometry for every pixel. That made full custom campaign regeneration
        slow enough to interrupt. The cels are simple strokes, so use Pillow's
        deterministic rasterizer and keep the callback fallback for minimal
        Python environments.
        """
        try:
            from PIL import Image, ImageDraw
        except Exception:
            _write_rgba_png(path, *full_page_size, arm_cel_px(frame))
            return

        w, h = full_page_size
        shoulder = (w * 0.18, h * 0.78)
        targets = [
            (w * 0.66, h * 0.24),
            (w * 0.68, h * 0.34),
            (w * 0.70, h * 0.45),
            (w * 0.67, h * 0.56),
            (w * 0.64, h * 0.67),
            (w * 0.48, h * 0.80),
            (w * 0.34, h * 0.86),
        ]
        wrist = targets[max(0, min(frame, len(targets) - 1))]
        image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image, "RGBA")
        sleeve_width = max(1, int(h * 0.060))
        pointer_width = max(1, int(h * 0.020))
        cuff_r = h * 0.040
        # Draw sleeve/cuff/pointer in back-to-front order to match the earlier
        # pixel generator's intended visual layering.
        draw.line([shoulder, wrist], fill=(55, 74, 51, 230), width=sleeve_width)
        draw.ellipse(
            (wrist[0] - cuff_r, wrist[1] - cuff_r, wrist[0] + cuff_r, wrist[1] + cuff_r),
            outline=(172, 176, 132, 235),
            width=max(1, int(h * 0.018)),
        )
        draw.line(
            [wrist, (wrist[0] + w * 0.085, wrist[1] - h * 0.030)],
            fill=(218, 188, 102, 235),
            width=pointer_width,
        )
        path.parent.mkdir(parents=True, exist_ok=True)
        image.save(path)

    _write_rgba_png(title640, *title640_size, title_px)
    _write_rgba_png(title, *full_page_size, title_px)
    _write_rgba_png(desk, *panel_size, desk_px)
    _write_rgba_png(wall, *full_page_size, wall_px)
    _write_rgba_png(hiscore, *panel_size, hiscore_px)
    _write_rgba_png(armpiece, *panel_size, armpiece_px)
    _write_rgba_png(theater_map, *map_size, map_px)
    arm_dir.mkdir(parents=True, exist_ok=True)
    arm_entries = []
    for frame in range(7):
        arm_path = arm_dir / f"{frame}.png"
        write_arm_cel_png(arm_path, frame)
        arm_entries.append(
            art_entry(
                str(arm_path.relative_to(out_dir)),
                f"start/menu/arm/{frame}.png",
                "high_resolution_truecolor_briefing_arm_cel_png",
                full_page_size,
                "RGBA8888",
                full_page_target,
                "high_resolution_truecolor_overlay",
            )
        )
    return [
        art_entry(title640.name, "TITLE640.PIC", "high_resolution_truecolor_png", title640_size, "RGBA8888", title640_target, "high_resolution_truecolor_title"),
        art_entry(title.name, "TITLE.PIC", "high_resolution_truecolor_png", full_page_size, "RGBA8888", full_page_target, "truecolor_page_backdrop"),
        art_entry(desk.name, "DESK.PIC", "high_resolution_truecolor_png", panel_size, "RGBA8888", full_page_target, "truecolor_page_backdrop"),
        art_entry(wall.name, "WALL.PIC", "high_resolution_truecolor_png", full_page_size, "RGBA8888", full_page_target, "truecolor_page_backdrop"),
        art_entry(hiscore.name, "HISCORE.PIC", "high_resolution_truecolor_png", panel_size, "RGBA8888", full_page_target, "truecolor_page_backdrop"),
        art_entry(armpiece.name, "ARMPIECE.PIC", "high_resolution_truecolor_png", panel_size, "RGBA8888", full_page_target, "truecolor_page_backdrop"),
        art_entry(theater_map.name, "VN.SPR", "high_resolution_truecolor_theater_map_png", map_size, "RGBA8888", map_target, "high_resolution_truecolor_map"),
    ] + arm_entries


def _write_pcm8_wav(path: Path, samples: bytes, sample_rate: int = DEFAULT_SAMPLE_RATE) -> None:
    data_size = len(samples)
    header = (
        b"RIFF"
        + struct.pack("<I", 36 + data_size)
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate, sample_rate, 1, 8)
        + b"data"
        + struct.pack("<I", data_size)
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + samples)


def _rewrite_tts_wav_as_radio_pcm8(path: Path, cue_index: int, sample_rate: int = DEFAULT_SAMPLE_RATE) -> bool:
    """Convert a generated TTS WAV into the game's simple radio-cue format.

    External TTS tools usually emit 16-bit/22 kHz+ speech. The legacy digitized
    driver uses unsigned 8-bit PCM near 7850 Hz, so starter campaign cues are
    normalized to that format and lightly dirtied with deterministic static.
    User-supplied or fake test outputs that are not readable WAVs are left as-is.
    """
    import wave

    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            width = wav.getsampwidth()
            source_rate = wav.getframerate()
            frames = wav.readframes(wav.getnframes())
    except Exception:
        return False
    if channels <= 0 or source_rate <= 0 or width not in {1, 2, 3, 4}:
        return False

    source_count = len(frames) // max(1, channels * width)
    if source_count <= 0:
        return False

    samples: list[float] = []
    for frame_index in range(source_count):
        mixed = 0.0
        frame_offset = frame_index * channels * width
        for channel in range(channels):
            offset = frame_offset + channel * width
            raw = frames[offset : offset + width]
            if width == 1:
                value = (raw[0] - 128) / 128.0
            else:
                signed = int.from_bytes(raw, "little", signed=True)
                max_abs = float(1 << (width * 8 - 1))
                value = max(-1.0, min(1.0, signed / max_abs))
            mixed += value
        samples.append(mixed / channels)

    target_count = max(1, int(len(samples) * sample_rate / source_rate))
    processed = bytearray()
    previous = 0.0
    for out_index in range(target_count):
        src_pos = out_index * source_rate / sample_rate
        left = min(len(samples) - 1, int(src_pos))
        right = min(len(samples) - 1, left + 1)
        frac = src_pos - left
        sample = samples[left] * (1.0 - frac) + samples[right] * frac
        # Remove some low-frequency TTS boom, compress hard like a radio, and
        # add deterministic low-level static so repeated builds are identical.
        high_passed = sample - previous * 0.965
        previous = sample
        driven = math.tanh(high_passed * 2.8)
        static = ((((out_index * 1103515245 + cue_index * 69069) >> 17) & 15) - 7) / 160.0
        gated_static = static * (0.55 + 0.45 * (out_index % 97 < 7))
        value = max(-1.0, min(1.0, driven * 0.86 + gated_static))
        processed.append(max(0, min(255, 128 + int(value * 102))))
    _write_pcm8_wav(path, bytes(processed), sample_rate)
    return True


def _modern_replacement_contract(runtime_path: str, source_format: str, fallback: str) -> dict[str, Any]:
    """Common manifest block for editable modern replacement sources.

    The custom pack is meant to be edited directly in modern formats. This block
    tells tools which runtime path consumes a file and what legacy asset remains
    as fallback if the modern replacement is absent or rejected.
    """
    return {
        "source_of_truth": True,
        "runtime_path": runtime_path,
        "source_format": source_format,
        "fallback": fallback,
    }


SOVIET_VIETNAM_RADIO_CUE_SPECS = [
    (
        "voice_cue_000_sample0",
        "Ли Си Цин, это Ромб. Взлёт разрешён. Держи малую высоту.",
        "Li Si Cin, this is Romb. Takeoff cleared. Keep low altitude.",
    ),
    (
        "voice_cue_001_sample4",
        "Группа прикрытия видит цель. Американский радар впереди.",
        "Cover group has the target. American radar ahead.",
    ),
    (
        "voice_cue_002_sample2_variant0",
        "Главная цель отмечена. Удар по авианосной группе. Уход на север.",
        "Primary target marked. Strike the carrier group. Exit north.",
    ),
    (
        "voice_cue_003_sample2_variant1",
        "Дананг активен. Полоса и топливо. Маневрируй немедленно.",
        "Da Nang is active. Runway and fuel. Maneuver immediately.",
    ),
    (
        "voice_cue_004_sample2_variant2",
        "Возвращайся на базу. Топливо и боекомплект на исходе.",
        "Return to base. Fuel and ammunition are nearly gone.",
    ),
]


def _radio_cue_samples(index: int, duration_ms: int = 900) -> bytes:
    import math
    count = max(1, DEFAULT_SAMPLE_RATE * duration_ms // 1000)
    out = bytearray()
    base = 360 + index * 90
    for i in range(count):
        t = i / DEFAULT_SAMPLE_RATE
        tone = math.sin(2 * math.pi * base * t) * 24
        chirp = math.sin(2 * math.pi * (base * 1.7 + 120 * t) * t) * 14
        gate = 1.0 if (i // 180 + index) % 3 else 0.35
        # Deterministic pseudo-static, centered around unsigned 8-bit silence.
        noise = (((i * 1103515245 + index * 12345) >> 16) & 31) - 15
        sample = 128 + int((tone + chirp) * gate) + noise
        out.append(max(0, min(255, sample)))
    return bytes(out)


def _default_radio_tts_command() -> str:
    espeak_ng = shutil.which("espeak-ng")
    if espeak_ng:
        # Store a shell-like command template because user-supplied TTS hooks use the
        # same format. The installer quotes substituted values before shlex.split().
        return f"{espeak_ng} -v ru -s 145 -a 135 -w {{output}} {{text}}"
    espeak = shutil.which("espeak")
    if espeak:
        return f"{espeak} -v ru -s 145 -a 135 -w {{output}} {{text}}"
    return ""


def _write_soviet_vietnam_radio_cues(out_dir: Path) -> list[dict[str, str]]:
    written = []
    for idx, (cue_id, ru_text, en_text) in enumerate(SOVIET_VIETNAM_RADIO_CUE_SPECS):
        path = out_dir / "sounds" / f"{cue_id}.wav"
        _write_pcm8_wav(path, _radio_cue_samples(idx))
        script_path = out_dir / "sounds" / f"{cue_id}.txt"
        script_path.write_text(f"{ru_text}\n{en_text}\n", encoding="utf-8")
        written.append(
            {
                "file": f"sounds/{cue_id}.wav",
                "script": f"sounds/{cue_id}.txt",
                "replaces": cue_id,
                "kind": "generated_radio_style_pcm8",
                "license_status": "generated_placeholder_needs_replacement_or_review",
                "replacement_contract": _modern_replacement_contract(
                    "campaign_local_wav_radio_cue",
                    "WAV PCM",
                    "legacy digitized cue blob",
                ),
                "text_ru": ru_text,
                "text_en": en_text,
            }
        )
    return written


def _svn_music_stream(instrument: int, volume: int, keyoff_gap: int, notes: list[tuple[int, int]]) -> list[int]:
    stream = [0xFC, instrument & 0x7F, 0xF9, volume & 0x7F, 0xFB, keyoff_gap & 0x7F]
    for note, duration in notes:
        stream.extend([note & 0x7F, duration & 0x7F])
    stream.extend([0, 0, 0])
    return stream


def _write_soviet_vietnam_intro_music(out_dir: Path) -> list[dict[str, Any]]:
    music_dir = out_dir / "sounds"
    music_dir.mkdir(parents=True, exist_ok=True)
    path = music_dir / "intro_music.asound.json"
    intro_patterns = [
        [(45, 10), (48, 10), (50, 12), (45, 8), (52, 16), (50, 10), (48, 10), (45, 18)],
        [(33, 20), (33, 20), (36, 20), (33, 20), (38, 20), (36, 20)],
        [(57, 8), (60, 8), (62, 8), (64, 12), (62, 8), (60, 8), (57, 16)],
        [(69, 6), (72, 6), (74, 12), (72, 6), (69, 6), (67, 12), (69, 18)],
        [(40, 14), (43, 14), (45, 14), (43, 14), (40, 22)],
        [(52, 7), (55, 7), (57, 14), (55, 7), (52, 7), (50, 20)],
    ]
    release_patterns = [[pattern[-1]] for pattern in intro_patterns]
    voices = []
    for phase, patterns in (("intro", intro_patterns), ("release", release_patterns)):
        for voice, pattern in enumerate(patterns):
            stream = _svn_music_stream(14 + (voice % 3), 34 + voice, 4 + (voice % 3), pattern)
            voices.append(
                {
                    "phase": phase,
                    "voice": voice,
                    "source_symbol": f"asound_{phase}_voice{voice}",
                    "stream_bytes": stream,
                    "notes": "Generated SVN campaign ASOUND command stream; editable bytecode source for runtime intro music replacement.",
                }
            )
    record = {
        "format": "F15_ASOUND_MUSIC",
        "version": 1,
        "source": "generated_soviet_vietnam_campaign",
        "tick_hz": 60,
        "runtime_authoritative": "intro_music.asound.json",
        "notes": "Campaign-local generated ASOUND intro music. Runtime loads this JSON instead of built-in intro/release streams while SVN is selected.",
        "voices": voices,
    }
    path.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return [
        {
            "file": "sounds/intro_music.asound.json",
            "replaces": "sounds/intro_music.asound.json",
            "kind": "generated_asound_intro_music",
            "license_status": "generated_placeholder_needs_replacement_or_review",
            "replacement_contract": _modern_replacement_contract(
                "campaign_local_asound_intro_music",
                "F15_ASOUND_MUSIC JSON",
                "built-in ASOUND intro/release streams",
            ),
            "notes": "Runtime-authoritative ASOUND stream JSON; replace this file to customize campaign intro music.",
        }
    ]


def _install_campaign_radio_dir(out_dir: Path, radio_dir: Path, existing: list[dict[str, str]]) -> list[dict[str, str]]:
    if not radio_dir.exists() or not radio_dir.is_dir():
        raise ValueError(f"--radio-dir must be an existing directory: {radio_dir}")
    by_cue = {item.get("replaces", ""): dict(item) for item in existing}
    installed = 0
    for cue_id, ru_text, en_text in SOVIET_VIETNAM_RADIO_CUE_SPECS:
        source = radio_dir / f"{cue_id}.wav"
        if not source.exists():
            continue
        if not source.is_file():
            raise ValueError(f"--radio-dir cue path is not a file: {source}")
        target = out_dir / "sounds" / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        script_path = out_dir / "sounds" / f"{cue_id}.txt"
        if not script_path.exists():
            script_path.write_text(f"{ru_text}\n{en_text}\n", encoding="utf-8")
        item = by_cue.get(cue_id, {})
        item.update(
            {
                "file": f"sounds/{cue_id}.wav",
                "script": f"sounds/{cue_id}.txt",
                "replaces": cue_id,
                "kind": "installed_recorded_or_tts_wav",
                "source": str(source),
                "license_status": "user_supplied_review_required",
                "replacement_contract": _modern_replacement_contract(
                    "campaign_local_wav_radio_cue",
                    "WAV PCM",
                    "legacy digitized cue blob",
                ),
                "text_ru": ru_text,
                "text_en": en_text,
            }
        )
        by_cue[cue_id] = item
        installed += 1
    if installed == 0:
        raise ValueError("--radio-dir did not contain any expected voice_cue_*.wav files")
    return [by_cue[cue_id] for cue_id, _, _ in SOVIET_VIETNAM_RADIO_CUE_SPECS if cue_id in by_cue]


def _install_campaign_radio_tts(out_dir: Path, command_template: str, existing: list[dict[str, str]]) -> list[dict[str, str]]:
    by_cue = {item.get("replaces", ""): dict(item) for item in existing}
    for cue_index, (cue_id, ru_text, en_text) in enumerate(SOVIET_VIETNAM_RADIO_CUE_SPECS):
        target = out_dir / "sounds" / f"{cue_id}.wav"
        script_path = out_dir / "sounds" / f"{cue_id}.txt"
        target.parent.mkdir(parents=True, exist_ok=True)
        script_path.write_text(f"{ru_text}\n{en_text}\n", encoding="utf-8")
        command = command_template.format(
            cue=cue_id,
            text=shlex.quote(ru_text),
            output=shlex.quote(str(target)),
            script=shlex.quote(str(script_path)),
        )
        subprocess.run(shlex.split(command), check=True)
        if not target.exists() or target.stat().st_size == 0:
            raise ValueError(f"--radio-tts-command did not create a non-empty WAV for {cue_id}: {target}")
        radio_processed = _rewrite_tts_wav_as_radio_pcm8(target, cue_index)
        item = by_cue.get(cue_id, {})
        item.update(
            {
                "file": f"sounds/{cue_id}.wav",
                "script": f"sounds/{cue_id}.txt",
                "replaces": cue_id,
                "kind": "generated_tts_wav",
                "postprocess": "mono unsigned PCM8 radio at project sample rate" if radio_processed else "left as generated by TTS command",
                "sample_rate_hz": DEFAULT_SAMPLE_RATE if radio_processed else None,
                "tts_command_template": command_template,
                "license_status": "tts_output_review_required",
                "replacement_contract": _modern_replacement_contract(
                    "campaign_local_wav_radio_cue",
                    "WAV PCM",
                    "legacy digitized cue blob",
                ),
                "text_ru": ru_text,
                "text_en": en_text,
            }
        )
        by_cue[cue_id] = item
    return [by_cue[cue_id] for cue_id, _, _ in SOVIET_VIETNAM_RADIO_CUE_SPECS if cue_id in by_cue]


def _soviet_vietnam_font_overrides() -> list[dict[str, Any]]:
    return [
        {
            "font_id": 1,
            "file": "fonts/font_1.ttf",
            "replacement_contract": _modern_replacement_contract(
                "campaign_local_scalable_font",
                "TTF/OTF",
                "legacy bitmap font 1",
            ),
            "purpose": "pilot/menu text; use a Cyrillic-capable TTF/OTF for Russian pilot names and campaign UI",
        },
        {
            "font_id": 3,
            "file": "fonts/font_3.ttf",
            "replacement_contract": _modern_replacement_contract(
                "campaign_local_scalable_font",
                "TTF/OTF",
                "legacy bitmap font 3",
            ),
            "purpose": "briefing/menu body text; use the same family as font_1 if possible",
        },
        {
            "font_id": 4,
            "file": "fonts/font_4.ttf",
            "replacement_contract": _modern_replacement_contract(
                "campaign_local_scalable_font",
                "TTF/OTF",
                "legacy bitmap font 4",
            ),
            "purpose": "large menu/header text; use a bold Cyrillic-capable face",
        },
    ]


def _install_campaign_font(out_dir: Path, font_path: Path | None) -> list[str]:
    if not font_path:
        return []
    if not font_path.exists() or not font_path.is_file():
        raise ValueError(f"font file does not exist: {font_path}")
    ext = font_path.suffix.lower()
    if ext not in {".ttf", ".otf"}:
        raise ValueError(f"--font expects a .ttf or .otf file: {font_path}")
    installed = []
    font_dir = out_dir / "fonts"
    font_dir.mkdir(parents=True, exist_ok=True)
    for font_id in (1, 3, 4):
        target = font_dir / f"font_{font_id}{ext}"
        shutil.copyfile(font_path, target)
        installed.append(f"fonts/{target.name}")
    return installed



def _write_svn_placeholder_glb(path: Path, slot: int, label: str) -> None:
    """Write a tiny self-contained GLB placeholder for SVN campaign aircraft slots.

    These models are intentionally simple and procedural. They provide a modern,
    Blender-editable source of truth for a fully customized campaign while still
    keeping the stable shape_### slot prefix that runtime lookup depends on.
    """
    def pad4(data: bytes, fill: bytes = b"\x00") -> bytes:
        return data + fill * ((4 - len(data) % 4) % 4)

    bin_data = bytearray()
    buffer_views: list[dict[str, Any]] = []
    accessors: list[dict[str, Any]] = []
    materials: list[dict[str, Any]] = []
    meshes: list[dict[str, Any]] = []
    nodes: list[dict[str, Any]] = []

    def material(name: str, rgba: tuple[float, float, float, float]) -> int:
        idx = len(materials)
        materials.append(
            {
                "name": name,
                "pbrMetallicRoughness": {
                    "baseColorFactor": [round(value, 4) for value in rgba],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.88,
                },
                "doubleSided": True,
            }
        )
        return idx

    def append_buffer(data: bytes, target: int) -> int:
        offset = len(bin_data)
        bin_data.extend(data)
        while len(bin_data) % 4:
            bin_data.append(0)
        buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(buffer_views) - 1

    def position_accessor(points: list[tuple[float, float, float]]) -> int:
        data = b"".join(struct.pack("<3f", *point) for point in points)
        view = append_buffer(data, 34962)
        accessors.append(
            {
                "bufferView": view,
                "componentType": 5126,
                "count": len(points),
                "type": "VEC3",
                "min": [min(point[i] for point in points) for i in range(3)],
                "max": [max(point[i] for point in points) for i in range(3)],
            }
        )
        return len(accessors) - 1

    def index_accessor(indices: list[int]) -> int:
        data = b"".join(struct.pack("<H", index) for index in indices)
        view = append_buffer(data, 34963)
        accessors.append({"bufferView": view, "componentType": 5123, "count": len(indices), "type": "SCALAR"})
        return len(accessors) - 1

    def primitive(points: list[tuple[float, float, float]], indices: list[int], mat: int, mode: int) -> dict[str, Any]:
        return {"attributes": {"POSITION": position_accessor(points)}, "indices": index_accessor(indices), "material": mat, "mode": mode}

    primary_palette = [
        (0.27, 0.39, 0.24, 1.0),
        (0.34, 0.43, 0.29, 1.0),
        (0.23, 0.32, 0.36, 1.0),
        (0.42, 0.36, 0.24, 1.0),
    ]
    primary = material("mat_svn_camouflage", primary_palette[slot % len(primary_palette)])
    underside = material("mat_svn_pale_underside", (0.70, 0.74, 0.62, 1.0))
    accent = material("mat_red_campaign_markings", (0.82, 0.10, 0.08, 1.0))
    line_mat = material("mat_lines_antennas", (0.92, 0.94, 0.86, 1.0))
    dark = material("mat_dark_equipment", (0.16, 0.19, 0.16, 1.0))

    scale = 0.82 + (slot % 5) * 0.06
    primitives: list[dict[str, Any]] = []
    if slot == 1:
        hull_points = [
            (-2.8, -0.35, -1.2), (2.8, -0.35, -1.2), (3.3, 0.0, 0.0),
            (2.3, 0.25, 1.1), (-2.3, 0.25, 1.1), (-3.3, 0.0, 0.0),
        ]
        deck_points = [(-2.4, 0.28, -1.0), (2.45, 0.28, -0.85), (2.45, 0.28, 0.85), (-2.4, 0.28, 1.0)]
        line_points = [(-2.0, 0.31, 0.0), (2.0, 0.31, 0.0), (0.9, 0.32, -0.75), (0.9, 0.32, 0.75)]
        primitives.extend(
            [
                primitive(hull_points, [0, 1, 2, 0, 2, 5, 5, 2, 3, 5, 3, 4], primary, 4),
                primitive(deck_points, [0, 1, 2, 0, 2, 3], dark, 4),
                primitive(line_points, [0, 1, 2, 3], line_mat, 1),
            ]
        )
    elif slot in {3, 4, 5, 16, 20}:
        s2 = scale
        tower = [(-0.22 * s2, 0, -1.2 * s2), (0.22 * s2, 0, -1.2 * s2), (0.18 * s2, 0, 1.4 * s2), (-0.18 * s2, 0, 1.4 * s2), (0, 0.55 * s2, 1.9 * s2)]
        dish = [(-1.2 * s2, 0, 1.55 * s2), (1.2 * s2, 0, 1.55 * s2), (0, 0.34 * s2, 2.35 * s2), (0, -0.12 * s2, 1.18 * s2)]
        lines = [(-1.5 * s2, 0, 1.55 * s2), (1.5 * s2, 0, 1.55 * s2), (0, -0.2 * s2, -1.2 * s2), (0, 0.9 * s2, 2.25 * s2)]
        primitives.extend(
            [
                primitive(tower, [0, 1, 2, 0, 2, 3, 2, 4, 3], dark, 4),
                primitive(dish, [0, 1, 2, 0, 3, 1], underside, 4),
                primitive(lines, [0, 1, 2, 3], accent, 1),
            ]
        )
    else:
        s2 = scale
        sweep = 0.86 + (slot % 4) * 0.08
        body = [(0, 0, 4.2 * s2), (-0.42 * s2, -0.28 * s2, 1.8 * s2), (0.42 * s2, -0.28 * s2, 1.8 * s2), (-0.55 * s2, 0.25 * s2, -2.6 * s2), (0.55 * s2, 0.25 * s2, -2.6 * s2), (0, 0.56 * s2, -3.25 * s2), (0, -0.42 * s2, -3.0 * s2)]
        wings = [(-0.18 * s2, 0.0, 1.0 * s2), (-3.0 * s2 * sweep, 0.02 * s2, -0.8 * s2), (-0.42 * s2, 0.02 * s2, -1.25 * s2), (0.18 * s2, 0.0, 1.0 * s2), (3.0 * s2 * sweep, 0.02 * s2, -0.8 * s2), (0.42 * s2, 0.02 * s2, -1.25 * s2), (-0.24 * s2, 0.09 * s2, -2.1 * s2), (-1.25 * s2, 0.14 * s2, -3.25 * s2), (-0.18 * s2, 0.15 * s2, -2.95 * s2), (0.24 * s2, 0.09 * s2, -2.1 * s2), (1.25 * s2, 0.14 * s2, -3.25 * s2), (0.18 * s2, 0.15 * s2, -2.95 * s2)]
        marks = [(-0.42 * s2, 0.035 * s2, -0.58 * s2), (-0.82 * s2, 0.04 * s2, -0.86 * s2), (-0.42 * s2, 0.04 * s2, -1.08 * s2), (0.42 * s2, 0.035 * s2, -0.58 * s2), (0.82 * s2, 0.04 * s2, -0.86 * s2), (0.42 * s2, 0.04 * s2, -1.08 * s2)]
        lines = [(0, 0, 4.2 * s2), (0, 0, 5.05 * s2), (-0.32 * s2, 0.28 * s2, -2.5 * s2), (-0.32 * s2, 1.25 * s2, -3.0 * s2), (0.32 * s2, 0.28 * s2, -2.5 * s2), (0.32 * s2, 1.25 * s2, -3.0 * s2)]
        primitives.extend(
            [
                primitive(body, [0, 1, 2, 1, 3, 6, 2, 6, 4, 1, 5, 3, 2, 4, 5, 3, 5, 6, 4, 6, 5, 1, 6, 2], primary, 4),
                primitive(wings, [0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 11, 10], underside, 4),
                primitive(marks, [0, 1, 2, 3, 5, 4], accent, 4),
                primitive(lines, [0, 1, 2, 3, 4, 5], line_mat, 1),
            ]
        )

    meshes.append({"name": f"shape_{slot:03d}_{label}", "primitives": primitives})
    nodes.append({"name": f"shape_{slot:03d}_{label}", "mesh": 0})
    gltf = {
        "asset": {"version": "2.0", "generator": "f15assets SVN procedural campaign model generator"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "buffers": [{"byteLength": len(bin_data)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "extras": {
            "f15se2_campaign": "SVN",
            "shape_slot": slot,
            "license_status": "generated_procedural_placeholder_review_required",
            "notes": "Campaign-native editable GLB placeholder; replace in Blender for final art.",
        },
    }
    json_chunk = pad4(json.dumps(gltf, separators=(",", ":")).encode("utf-8"), b" ")
    bin_chunk = pad4(bytes(bin_data), b"\x00")
    total_size = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)
    glb = bytearray(b"glTF" + struct.pack("<II", 2, total_size))
    glb.extend(struct.pack("<I4s", len(json_chunk), b"JSON"))
    glb.extend(json_chunk)
    glb.extend(struct.pack("<I4s", len(bin_chunk), b"BIN\x00"))
    glb.extend(bin_chunk)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(glb)


def _write_soviet_vietnam_starter_aircraft_glbs(campaign_dir: Path) -> list[dict[str, Any]]:
    labels = {
        0: "LiSiCin_Stolen_F15",
        1: "Tonkin_Carrier_Target",
        2: "DaNang_Strike_Jet",
        3: "Hanoi_Radar",
        4: "Haiphong_Radar",
        5: "Tonkin_CAP_Radar",
        10: "MiG21_Escort",
        15: "MiG19_Patrol",
        16: "SAM_Radar",
        20: "Long_Range_Radar",
        22: "F4_Phantom_Opponent",
    }
    aircraft_dir = campaign_dir / "15FLT"
    installed: list[dict[str, Any]] = []
    for slot in range(23):
        label = labels.get(slot, f"SVN_Custom_Aircraft_{slot:03d}")
        target = aircraft_dir / _shape_slot_glb_name(slot, label)
        for stale in aircraft_dir.glob(f"shape_{slot:03d}*.glb"):
            if stale != target:
                stale.unlink()
        _write_svn_placeholder_glb(target, slot, label)
        installed.append(
            {
                "slot": slot,
                "file": str(target.relative_to(campaign_dir)),
                "source": "procedural_svn_campaign_generator",
                "kind": "campaign_native_procedural_glb_placeholder",
                "license_status": "generated_procedural_placeholder_review_required",
                "replacement_contract": _modern_replacement_contract(
                    "campaign_local_shape_glb",
                    "GLB",
                    "shared converted GLB or original 15FLT.3D3 shape slot",
                ),
                "notes": "Editable campaign-native GLB placeholder with colored surfaces and line primitives; replace with reviewed CC/free Blender-exported GLB for final distribution.",
            }
        )
    (aircraft_dir / "README.md").write_text(
        "# SVN campaign-local aircraft GLB overrides\n\n"
        "Each `shape_###_*.glb` keeps the 15FLT shape slot prefix used by runtime lookup, "
        "but the file contents are campaign-native procedural placeholders rather than converted original geometry.\n\n"
        "The placeholders intentionally use simple colored surfaces plus line primitives for antennas/masts "
        "so Blender users can inspect and replace them easily. Replace any one file with a Blender-exported GLB "
        "as long as the `shape_###` prefix remains stable.\n\n"
        "These files are campaign-local: they should affect `--campaign SVN` only, before falling back to shared "
        "`converted_assets_all/15FLT` or the original `15FLT.3D3`.\n\n"
        "License note: these generated placeholders still need review before a free/CC asset distribution.\n",
        encoding="utf-8",
    )
    return installed


def _merge_campaign_aircraft_entries(base: list[dict[str, Any]], overrides: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_slot = {int(item.get("slot", -1)): item for item in base if isinstance(item, dict)}
    for item in overrides:
        if isinstance(item, dict):
            by_slot[int(item.get("slot", -1))] = item
    return [by_slot[slot] for slot in sorted(by_slot) if slot >= 0]

def _install_campaign_aircraft_glbs(output_root: Path, campaign_dir: Path, specs: list[str]) -> list[dict[str, Any]]:
    installed = []
    aircraft_dir = campaign_dir / "15FLT"
    cache_dir = aircraft_dir / "cache"
    for spec in specs:
        if "=" not in spec:
            raise ValueError("--aircraft-glb expects SLOT=PATH, for example --aircraft-glb 10=/tmp/mig21.glb")
        slot_text, path_text = spec.split("=", 1)
        try:
            slot = int(slot_text, 10)
        except ValueError as exc:
            raise ValueError(f"invalid aircraft slot in --aircraft-glb: {slot_text}") from exc
        source = Path(path_text)
        if slot < 0 or slot > 999:
            raise ValueError("--aircraft-glb slot must be in 0..999")
        if not source.exists() or not source.is_file():
            raise ValueError(f"aircraft GLB does not exist: {source}")
        if source.suffix.lower() != ".glb":
            raise ValueError(f"--aircraft-glb expects a .glb file: {source}")
        aircraft_dir.mkdir(parents=True, exist_ok=True)
        target = aircraft_dir / _shape_slot_glb_name(slot, source.stem)
        for stale in aircraft_dir.glob(f"shape_{slot:03d}*.glb"):
            if stale != target:
                stale.unlink()
        shutil.copyfile(source, target)
        removed = 0
        for stale in list(aircraft_dir.glob(f"shape_{slot:03d}*.glmesh")) + list(cache_dir.glob(f"shape_{slot:03d}*.glmesh")):
            stale.unlink()
            removed += 1
        installed.append(
            {
                "slot": slot,
                "file": str(target.relative_to(campaign_dir)),
                "source": str(source),
                "license_status": "user_supplied_review_required",
                "replacement_contract": _modern_replacement_contract(
                    "campaign_local_shape_glb",
                    "GLB",
                    "shared converted GLB or original 15FLT.3D3 shape slot",
                ),
                "removed_stale_caches": removed,
            }
        )
    return installed


def _write_campaign_briefings(out_dir: Path, sorties: list[dict[str, Any]], objectives: list[dict[str, Any]], world_name: str) -> list[dict[str, str]]:
    objective_by_id = {str(item.get("id", "")): item for item in objectives if isinstance(item, dict)}
    written = []
    briefing_dir = out_dir / "briefings"
    briefing_dir.mkdir(parents=True, exist_ok=True)
    for index, sortie in enumerate(sorties, start=1):
        if not isinstance(sortie, dict):
            continue
        sortie_id = _safe_output_stem(sortie.get("id") or f"sortie_{index:02d}")
        path = briefing_dir / f"{index:02d}_{sortie_id}.md"
        objective_ids = [str(value) for value in sortie.get("objective_ids", [])]
        lines = [
            f"# {sortie.get('title') or sortie_id}",
            "",
            f"Sortie id: `{sortie.get('id') or sortie_id}`",
            f"Phase: `{sortie.get('phase', index)}`",
            f"Player aircraft: {sortie.get('player_aircraft', 'campaign default')}",
            "",
            str(sortie.get("briefing") or ""),
            "",
            "## Objectives",
            "",
        ]
        for objective_id in objective_ids:
            objective = objective_by_id.get(objective_id, {})
            lines.append(f"- `{objective_id}`: {objective.get('name', objective_id)}")
            if objective.get("intent"):
                lines.append(f"  {objective.get('intent')}")
            if objective.get("target_type") or objective.get("threat_level"):
                lines.append(f"  Target: `{objective.get('target_type', 'unknown')}`, threat `{objective.get('threat_level', 'unknown')}`.")
            if objective.get("desired_effect"):
                lines.append(f"  Desired effect: {objective.get('desired_effect')}")
            if objective.get("suggested_loadout"):
                lines.append(f"  Suggested loadout: {objective.get('suggested_loadout')}")
            if objective.get("success_criteria"):
                criteria = "; ".join(str(value) for value in objective.get("success_criteria") or [])
                lines.append(f"  Success criteria: {criteria}")
        lines.extend([
            "",
            "## Authoring notes",
            "",
            "Edit this Markdown file for human-facing campaign text.",
            f"Keep objective ids aligned with campaign.json and {world_name or 'the campaign WLD JSON'} if you rename them.",
            "",
        ])
        path.write_text("\n".join(lines), encoding="utf-8")
        written.append(
            {
                "file": f"briefings/{path.name}",
                "sortie_id": str(sortie.get("id") or sortie_id),
                "kind": "editable_markdown_briefing",
            }
        )
    return written


def _write_campaign_target_plan(out_dir: Path, manifest: Dict[str, Any]) -> None:
    """Write a human-editable mission target planning document.

    The authoritative data remains campaign.json and the WLD JSON, but target
    packages are too important to hide inside nested JSON. This document gives
    campaign authors a compact checklist of object slots, attack order, victory
    logic, and route anchors to review after map-editor changes.
    """
    objectives = manifest.get("mission_objectives") or []
    sorties = manifest.get("sortie_sequence") or []
    target_sets = manifest.get("mission_target_sets") or []
    route_plan = manifest.get("route_plan") or {}
    objective_by_id = {str(item.get("id", "")): item for item in objectives if isinstance(item, dict)}
    sortie_by_id = {str(item.get("id", "")): item for item in sorties if isinstance(item, dict)}
    world_objects: list[Any] = []
    world_name = str(manifest.get("world") or "")
    if world_name:
        try:
            world_payload = json.loads((out_dir / world_name).read_text(encoding="utf-8"))
            world_objects = world_payload.get("world_objects") or world_payload.get("worldObjects") or []
            if not isinstance(world_objects, list):
                world_objects = []
        except Exception:
            world_objects = []

    lines = [
        f"# {manifest.get('id', out_dir.name)} mission target plan",
        "",
        "This file is generated for campaign authors. Edit authoritative values in `campaign.json` and the WLD JSON, then re-run `package-campaign`.",
        "",
        "## Runtime target selection",
        "",
        "The game currently uses the selected sortie's `primary_objective_ids` and `secondary_objective_ids` as mission target hints.",
        "The legacy generator still validates the chosen target against preserved mission tables, distance, bases, and fallback behavior.",
        "",
        "## Objectives and WLD slots",
        "",
    ]
    for objective in objectives:
        if not isinstance(objective, dict):
            continue
        slot = objective.get("object_slot")
        slot_note = ""
        if isinstance(slot, int) and 0 <= slot < len(world_objects) and isinstance(world_objects[slot], dict):
            obj = world_objects[slot]
            scenario_label = obj.get("scenario_label") or obj.get("label") or obj.get("name") or "unnamed"
            legacy_label = obj.get("legacy_label") or obj.get("label") or obj.get("name") or ""
            legacy_note = f", legacy `{legacy_label}`" if legacy_label and legacy_label != scenario_label else ""
            slot_note = f" WLD `{scenario_label}`{legacy_note} at `{obj.get('x_coord')},{obj.get('y_coord')}`"
        lines.append(
            f"- `{objective.get('id')}` slot `{slot}`:{slot_note} - "
            f"{objective.get('name')} [{objective.get('faction')}/{objective.get('domain')}/{objective.get('action')}]"
        )
        if objective.get("target_type") or objective.get("threat_level"):
            lines.append(f"  Target type: `{objective.get('target_type', 'unknown')}`; threat: `{objective.get('threat_level', 'unknown')}`.")
        if objective.get("desired_effect"):
            lines.append(f"  Desired effect: {objective.get('desired_effect')}")
        if objective.get("success_criteria"):
            lines.append("  Success criteria:")
            for criterion in objective.get("success_criteria") or []:
                lines.append(f"  - {criterion}")
        if objective.get("radio_cue_ids"):
            lines.append(f"  Radio cues: `{', '.join(str(value) for value in objective.get('radio_cue_ids') or [])}`.")

    lines.extend(["", "## Sorties", ""])
    for sortie in sorties:
        if not isinstance(sortie, dict):
            continue
        lines.append(f"- `{sortie.get('id')}` phase `{sortie.get('phase')}`: {sortie.get('title')}")
        lines.append(f"  Launch selector: `--campaign-sortie {sortie.get('id')}` or `--campaign-sortie {sortie.get('phase')}`")
        lines.append(f"  Primary: `{', '.join(str(value) for value in sortie.get('primary_objective_ids', []))}`")
        lines.append(f"  Secondary: `{', '.join(str(value) for value in sortie.get('secondary_objective_ids', []))}`")
        if sortie.get("briefing"):
            lines.append(f"  START-board briefing: {sortie.get('briefing')}")

    if target_sets:
        lines.extend(["", "## Target packages", ""])
        for target_set in target_sets:
            if not isinstance(target_set, dict):
                continue
            sortie = sortie_by_id.get(str(target_set.get("sortie_id") or ""), {})
            lines.append(f"### `{target_set.get('id')}`")
            lines.append("")
            lines.append(f"- Sortie: `{target_set.get('sortie_id')}` / {sortie.get('title', 'unknown')}")
            lines.append(f"- Role: {target_set.get('package_role')}")
            lines.append(f"- Attack order: `{', '.join(str(value) for value in target_set.get('attack_order', []))}`")
            victory = target_set.get("victory_logic") or {}
            if victory:
                lines.append(f"- Victory logic: `{json.dumps(victory, ensure_ascii=False, sort_keys=True)}`")
            if target_set.get("editor_notes"):
                lines.append(f"- Editor notes: {target_set.get('editor_notes')}")
            lines.extend(["", "Target elements:", ""])
            for element in target_set.get("target_elements") or []:
                if not isinstance(element, dict):
                    continue
                objective = objective_by_id.get(str(element.get("objective_id") or ""), {})
                lines.append(
                    f"- `{element.get('element_id')}` -> `{element.get('objective_id')}` "
                    f"({objective.get('name', 'unknown objective')}): {element.get('role')}; "
                    f"weapon `{element.get('recommended_weapon', 'unspecified')}`; effect {element.get('effect', 'unspecified')}."
                )
                if element.get("placement_hint"):
                    lines.append(f"  Placement: {element.get('placement_hint')}")
                if element.get("defense_behavior"):
                    lines.append(f"  Defense behavior: {element.get('defense_behavior')}")
            lines.append("")

    if route_plan.get("waypoints"):
        lines.extend(["## Route anchors", ""])
        for waypoint in route_plan.get("waypoints", []):
            lines.append(
                f"- `{waypoint.get('id')}` -> `{waypoint.get('objective_id')}`: "
                f"{waypoint.get('label')} at WLD `{waypoint.get('x_coord')},{waypoint.get('y_coord')}`"
            )

    lines.append("")
    (out_dir / "MISSION_TARGETS.md").write_text("\n".join(lines), encoding="utf-8")


def _write_campaign_custom_assets_readme(out_dir: Path, manifest: Dict[str, Any]) -> None:
    """Write the customizer-focused asset replacement guide for this campaign."""
    campaign_id = str(manifest.get("id") or out_dir.name)
    world_name = str(manifest.get("world") or f"{campaign_id}.WLD.json")
    lines = [
        f"# {campaign_id} custom asset guide",
        "",
        "This pack is a full modern-format customization test campaign.",
        "",
        "Current fiction hook: the F-15 was stolen by Soviet pilot Lisicin; Vietnamese allies call him Li Si Cin.",
        "",
        "## Fresh one-command build",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py build-soviet-vietnam-pack /path/to/F15_GAME converted_assets_all --package --pretty",
        "```",
        "",
        "Package validation runs by default when `--package` is used. Use `--skip-package-validation` only for draft ZIPs; use `--validate` for full replacement validation.",
        "",
        "## Graphics",
        "",
        "- Replace campaign-local PNG files directly: `TITLE640.png`, `TITLE.png`, `DESK.png`, `WALL.png`, `HISCORE.png`, `ARMPIECE.png`, and theater map `VN.png`.",
        "- PNGs may be higher resolution and truecolor; runtime scales them into the original game rectangles instead of treating source pixels as game-space size.",
        "- Generated metadata records both source pixels (`width`/`height`) and in-game logical target size (`target_width`/`target_height`).",
        "- Current target rectangles: `TITLE640.png` -> 640x350, page/backdrop PNGs -> 320x200, `VN.png` theater map -> 224x168.",
        "- `runtime_path` and `legacy_limits_removed` in `campaign.json` tell tools that these PNGs are not constrained to original PIC/SPR resolution or palette size.",
        "- Replace `start/menu/arm/*.png` for high-resolution transparent briefing pointer-arm cels.",
        "",
        "## Audio",
        "",
        "- Replace `sounds/voice_cue_*.wav` one-by-one for radio cues.",
        "- Edit matching UTF-8 `sounds/voice_cue_*.txt` scripts so package docs describe the new cue text.",
        "- Fresh SVN builds use local `espeak-ng`/`espeak` Russian TTS automatically when available; otherwise they fall back to deterministic radio-tone placeholders.",
        f"- TTS-generated WAVs are post-processed to mono unsigned 8-bit PCM at `{DEFAULT_SAMPLE_RATE}` Hz with light radio static when the TTS command emits a readable WAV.",
        "- Pass `--radio-dir /path/to/wavs` or `--radio-tts-command '...'` to force reviewed recorded or TTS cue sources.",
        "- Replace `sounds/intro_music.asound.json` to customize campaign intro music.",
        "",
        "## Models",
        "",
        "- Replace `15FLT/shape_###_*.glb` files from Blender or another GLB authoring tool.",
        "- Keep the `shape_###` prefix stable; runtime slot lookup depends on it.",
        "- Delete stale generated `.glmesh` caches if you edit GLB files manually outside the helper commands.",
        "",
        "## Fonts",
        "",
        "- Replace `fonts/font_1.ttf`, `fonts/font_3.ttf`, and `fonts/font_4.ttf` with Cyrillic-capable TTF/OTF files.",
        "- Runtime uses scalable fonts where the text path supports TTF/OTF, with legacy bitmap fonts as fallback.",
        "",
        "## Scenario and mission targets",
        "",
        f"- Edit `{world_name}` for world objects, object placement, route anchors, and scenario metadata.",
        f"- Launch the full campaign with `--campaign {campaign_id}`; this also selects the base theater and campaign-local media.",
        f"- Launch just the custom world JSON with `--scenario {campaign_id} --scenario-base {manifest.get('base_theater') or 'VN'}` when testing WLD placement without the campaign wrapper.",
        "- Equivalent low-level environment variables are `F15_WORLD_SCENARIO` and `F15_WORLD_SCENARIO_BASE`; both still require `F15_REPLACEMENT_ROOT` to point at the converted/custom asset root.",
        "- Review `MISSION_TARGETS.md` after changing target slots or sortie packages.",
        "- Edit `briefings/*.md`; `package-campaign` syncs Markdown title/summary back into the WLD fields displayed on the START mission board.",
        "",
        "## Capability map and validation",
        "",
        "- `campaign.json` has an `objective_capabilities` block that maps the requested custom-campaign features to concrete files.",
        "- Every provided capability lists campaign-relative `artifacts`; package validation rejects missing, absolute, or `..` artifact paths.",
        "- Use this block as the machine-readable checklist for launchers, installers, and release review.",
        "- If you add a new modern source file type or remove a generated file, update `objective_capabilities` before packaging.",
        "- After running the game or full replacement validation externally, record that evidence with `record-campaign-verification` instead of hand-editing `verification_status`.",
        "- `passed` and `failed` records require the matching `--*-command` argument; use `not_applicable` only when a validation category genuinely does not apply.",
        "",
        "Example after external validation:",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py record-campaign-verification converted_assets_all/{campaign_id} --runtime-launch passed --runtime-launch-command './f15se2-ex --game /path/to/F15_GAME --campaign {campaign_id}' --full-asset-validation passed --full-asset-validation-command 'python3 tools/f15assets/cli.py validate-replacements /path/to/F15_GAME converted_assets_all'",
        "```",
        "",
        "## Free/CC redistribution checklist",
        "",
        "- Review or replace generated PNG art before distributing a free asset pack.",
        "- Review or replace generated procedural GLBs with reviewed Blender-exported models if you need clean licensing provenance.",
        "- Review or replace generated/TTS WAV radio cues; keep `sounds/*.txt` scripts aligned with the final audio.",
        "- Use fonts whose licenses explicitly allow redistribution with the campaign.",
        "- Keep `campaign.json`, `inventory.json`, and this guide updated before publishing a package.",
        "",
        "## Repackage",
        "",
        "From this campaign directory:",
        "",
        "```bash",
        f"python3 \"${{F15SE2_EX_ROOT:-../..}}/tools/f15assets/cli.py\" package-campaign . ../{campaign_id}.zip --pretty",
        "```",
        "",
        "From the repository root:",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py package-campaign converted_assets_all/{campaign_id} converted_assets_all/{campaign_id}.zip --pretty",
        "```",
        "",
        "Run package validation by omitting `--skip-validation`; use `--skip-validation` only for draft packages.",
        "",
        "## Install packaged ZIP",
        "",
        "Inspect before installing:",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py inspect-campaign-package converted_assets_all/{campaign_id}.zip",
        "```",
        "",
        "Install into another converted/custom asset root:",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py install-campaign-package converted_assets_all/{campaign_id}.zip /path/to/converted_assets_all --replace",
        "```",
        "",
        "Then run with:",
        "",
        "```bash",
        f"F15_REPLACEMENT_ROOT=/path/to/converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign {campaign_id}",
        "```",
        "",
    ]
    (out_dir / "CUSTOM_ASSETS_README.md").write_text("\n".join(lines), encoding="utf-8")


def _write_campaign_inventory(out_dir: Path, manifest: Dict[str, Any], pretty: bool) -> dict[str, Any]:
    files: list[dict[str, Any]] = []
    files.append({"file": f"{manifest.get('id')}.WLD.json", "role": "world_scenario", "source_of_truth": True})
    files.append({"file": "campaign.json", "role": "campaign_manifest", "source_of_truth": True})
    files.append({"file": "SUMMARY.md", "role": "human_summary", "source_of_truth": False})
    files.append({"file": "MISSION_TARGETS.md", "role": "mission_target_plan", "source_of_truth": False})
    files.append({"file": "CUSTOM_ASSETS_README.md", "role": "custom_asset_guide", "source_of_truth": False})
    files.append({"file": "README.md", "role": "human_readme", "source_of_truth": False})
    files.append({"file": "run_campaign.sh", "role": "campaign_launcher", "source_of_truth": False})
    for item in manifest.get("campaign_art") or []:
        files.append({"file": item.get("file"), "role": "campaign_art", "source_of_truth": True, "license_status": item.get("license_status")})
    for item in manifest.get("campaign_sounds") or []:
        if isinstance(item, str):
            item = {"file": item}
        files.append({"file": item.get("file"), "role": "campaign_sound", "source_of_truth": True, "license_status": item.get("license_status")})
        if item.get("script"):
            files.append({"file": item.get("script"), "role": "campaign_sound_script", "source_of_truth": True})
    for item in manifest.get("campaign_music") or []:
        if isinstance(item, dict) and item.get("file"):
            files.append({"file": item.get("file"), "role": "campaign_music", "source_of_truth": True, "license_status": item.get("license_status")})
    for item in manifest.get("briefings") or []:
        files.append({"file": item.get("file"), "role": "sortie_briefing", "source_of_truth": True})
    for item in manifest.get("installed_fonts") or []:
        files.append({"file": item, "role": "campaign_font", "source_of_truth": True, "license_status": "user_supplied_review_required"})
    for item in manifest.get("installed_aircraft") or []:
        if isinstance(item, dict) and item.get("file"):
            files.append({"file": item.get("file"), "role": "campaign_aircraft_model", "source_of_truth": True, "license_status": item.get("license_status")})
    free_assets_path = out_dir / "free-assets.json"
    if free_assets_path.exists():
        files.append({"file": "free-assets.json", "role": "generated_asset_manifest", "source_of_truth": True})
        free_assets = json.loads(free_assets_path.read_text(encoding="utf-8"))
        for item in free_assets.get("generated") or []:
            if isinstance(item, dict) and item.get("file"):
                files.append({
                    "file": item["file"],
                    "role": "generated_free_asset",
                    "source_of_truth": True,
                    "license_status": item.get("license"),
                })
    unique_files: list[dict[str, Any]] = []
    seen_paths: set[str] = set()
    for item in files:
        rel = str(item.get("file") or "")
        if not rel or rel in seen_paths:
            continue
        seen_paths.add(rel)
        unique_files.append(item)
    files = unique_files
    for item in files:
        rel = str(item.get("file") or "")
        file_path = out_dir / rel
        if file_path.exists() and file_path.is_file():
            data = file_path.read_bytes()
            item["sha256"] = hashlib.sha256(data).hexdigest()
            item["bytes"] = len(data)
    inventory = {
        "format": "F15SE2_CAMPAIGN_INVENTORY",
        "format_version": 1,
        "campaign_id": manifest.get("id"),
        "display_name": manifest.get("display_name") or manifest.get("title"),
        "files": files,
    }
    _write_json(out_dir / "inventory.json", inventory, pretty=pretty, media_sidecar=False)
    return inventory


def _write_campaign_launcher(out_dir: Path, campaign_id: str) -> None:
    path = out_dir / "run_campaign.sh"
    script = "\n".join([
        "#!/bin/sh",
        "set -eu",
        "",
        'SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)',
        'REPLACEMENT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)',
        'GAME_DIR=${1:-${F15_GAME_DIR:-$SCRIPT_DIR}}',
        'SORTIE=${2:-${F15_CAMPAIGN_SORTIE:-}}',
        'REPO_ROOT=$(CDPATH= cd -- "$REPLACEMENT_ROOT/.." 2>/dev/null && pwd || printf "%s\\n" "$PWD")',
        'EXE=${F15_EXE:-}',
        "",
        'if [ -z "$EXE" ]; then',
        "    for candidate in \\",
        '        "$REPO_ROOT/build/f15se2-ex" \\',
        '        "$REPO_ROOT/f15se2-ex" \\',
        '        "$PWD/f15se2-ex" \\',
        '        "f15se2-ex"',
        "    do",
        '        if command -v "$candidate" >/dev/null 2>&1; then',
        "            EXE=$candidate",
        "            break",
        "        fi",
        "    done",
        "fi",
        "",
        'if [ -z "$EXE" ]; then',
        '    echo "could not find f15se2-ex executable" >&2',
        '    echo "set F15_EXE=/path/to/f15se2-ex" >&2',
        "    exit 2",
        "fi",
        "",
        'if [ -z "${F15_ASSET_TOOL:-}" ] && [ -f "$REPO_ROOT/tools/f15assets/cli.py" ]; then',
        '    export F15_ASSET_TOOL="python3 $REPO_ROOT/tools/f15assets/cli.py"',
        "fi",
        "",
        'set -- "$EXE" --game "$GAME_DIR" --campaign ' + shlex.quote(campaign_id),
        'if [ -n "$SORTIE" ]; then',
        '    set -- "$@" --campaign-sortie "$SORTIE"',
        "fi",
        'F15_REPLACEMENT_ROOT="$REPLACEMENT_ROOT" F15_REPLACEMENT_ROOT_ONLY=1 exec "$@"',
        "",
    ])
    path.write_text(script, encoding="utf-8")
    path.chmod(0o755)


def _make_campaign_launcher_executable(campaign_dir: Path) -> None:
    launcher = campaign_dir / "run_campaign.sh"
    if launcher.exists() and launcher.is_file():
        launcher.chmod(launcher.stat().st_mode | 0o755)


def _write_campaign_summary(out_dir: Path, manifest: Dict[str, Any]) -> None:
    objectives = manifest.get("mission_objectives") or []
    sorties = manifest.get("sortie_sequence") or []
    target_sets = manifest.get("mission_target_sets") or []
    route_plan = manifest.get("route_plan") or {}
    world_objects: list[Any] = []
    world_name = str(manifest.get("world") or "")
    if world_name:
        try:
            world_payload = json.loads((out_dir / world_name).read_text(encoding="utf-8"))
            world_objects = world_payload.get("world_objects") or world_payload.get("worldObjects") or []
            if not isinstance(world_objects, list):
                world_objects = []
        except Exception:
            world_objects = []
    lines = [
        f"# {manifest.get('title', manifest.get('id', 'Campaign'))}",
        "",
        f"Campaign id: `{manifest.get('id')}`",
        f"Base theater: `{manifest.get('base_theater')}`",
        f"Run: `{(manifest.get('launch') or {}).get('example', '')}`",
        "",
        "## Premise",
        "",
        "Soviet spy pilot Lisicin has stolen an F-15 plane and flies in Vietnam as Li Si Cin against USA air and naval targets.",
        "",
        "## Sorties",
        "",
    ]
    for sortie in sorties:
        objective_ids = ", ".join(str(value) for value in sortie.get("objective_ids", []))
        lines.append(f"- `{sortie.get('id')}`: {sortie.get('title')} ({objective_ids})")
    lines.extend(["", "## Mission targets", ""])
    for objective in objectives:
        object_slot = objective.get("object_slot")
        object_note = ""
        if isinstance(object_slot, int) and 0 <= object_slot < len(world_objects) and isinstance(world_objects[object_slot], dict):
            world_object = world_objects[object_slot]
            label = world_object.get("label") or world_object.get("name") or "unnamed WLD object"
            object_note = f" at {label} `{world_object.get('x_coord')},{world_object.get('y_coord')}`"
        lines.append(
            f"- `{objective.get('id')}` slot {objective.get('object_slot')}: "
            f"{objective.get('name')} [{objective.get('faction')}/{objective.get('domain')}/{objective.get('action')}]{object_note}"
        )
    if sorties:
        selectable_ids = []
        for sortie in sorties:
            for group_key in ("primary_objective_ids", "secondary_objective_ids"):
                selectable_ids.extend(str(value) for value in sortie.get(group_key, []))
        lines.extend([
            "",
            "Runtime uses the selected sortie's primary and secondary objective slots as mission target hints:",
            f"`{', '.join(dict.fromkeys(selectable_ids))}`. The legacy generator still validates mission-table compatibility, base distance, and fallback behavior.",
        ])
    if target_sets:
        lines.extend(["", "## Mission target packages", ""])
        for target_set in target_sets:
            lines.append(
                f"- `{target_set.get('id')}`: {target_set.get('title')} "
                f"({', '.join(str(value) for value in target_set.get('attack_order', []))})"
            )
    if route_plan.get("waypoints"):
        lines.extend(["", "## Route plan and real-world anchors", ""])
        lines.append(
            f"`{world_name or 'campaign WLD JSON'}` contains `campaign_route_plan`, an authoring-only route layer "
            "that ties the first sorties to approximate Vietnam OSM anchors transformed into legacy WLD coordinates."
        )
        lines.append("")
        for waypoint in route_plan.get("waypoints", []):
            lines.append(
                f"- `{waypoint.get('id')}` / `{waypoint.get('objective_id')}`: "
                f"{waypoint.get('label')} (`{waypoint.get('x_coord')},{waypoint.get('y_coord')}`)."
            )
        lines.extend([
            "",
            "The map editor can draw these route legs with `Routes on` and synchronizes routed waypoint coordinates when you drag or numerically edit the matching object.",
        ])
    lines.extend([
        "",
        "## Editable modern media",
        "",
        "- `TITLE640.png`, `TITLE.png`, `DESK.png`, `WALL.png`, `HISCORE.png`, `ARMPIECE.png`: campaign-local high-resolution PNG screen art.",
        "- `start/menu/arm/0.png` through `start/menu/arm/6.png`: transparent briefing pointer-arm cels for the native overlay path.",
        "- `VN.png`: campaign-local high-resolution Vietnam theater/debrief map replacement.",
        "- `sounds/*.wav`, `sounds/*.txt`, and `sounds/intro_music.asound.json`: radio cues and campaign intro music.",
        "- `fonts/*.ttf`: scalable Cyrillic-capable font overrides.",
        "- `15FLT/shape_###_*.glb`: campaign-local procedural GLB placeholders; replace individual slots from Blender as needed.",
        "- `MISSION_TARGETS.md`: generated target-package checklist for objective slots, attack order, victory logic, and route anchors.",
        "- `CUSTOM_ASSETS_README.md`: customizer guide for replacing PNG/WAV/font/GLB/scenario files.",
        "",
        "## Edit workflow",
        "",
        f"1. Edit `{world_name or 'campaign WLD JSON'}` in the map editor for target placement and objective ids.",
        "2. Review `MISSION_TARGETS.md` after changing objectives or target packages.",
        "3. Edit `briefings/*.md` for human-facing mission text; `package-campaign` syncs the title/summary back into the WLD fields used by the in-game START board.",
        "4. Replace PNG/WAV/font/GLB files with reviewed custom media.",
        "5. Run `python3 -m tools.f15assets.cli refresh-campaign-inventory . --pretty` from this directory to sync docs/inventory without zipping.",
        "6. Run `python3 -m tools.f15assets.cli package-campaign . ../SVN.zip --pretty` from this directory to sync docs, refresh inventory, validate, and zip.",
        "7. Launch with `--campaign SVN` for the full campaign, or `--scenario SVN --scenario-base VN` when testing only the custom WLD JSON redirect.",
        "",
        "`--pretty` is accepted both globally and on `package-campaign`; omit it only when you intentionally want compact JSON.",
        "",
        f"See `campaign.json`, `{world_name or 'campaign WLD JSON'}`, `MISSION_TARGETS.md`, `CUSTOM_ASSETS_README.md`, `briefings/*.md`, and `inventory.json` for editable details.",
        "",
    ])
    (out_dir / "SUMMARY.md").write_text("\n".join(lines), encoding="utf-8")


def _write_campaign_readme(out_dir: Path, campaign_id: str, base_theater: str, manifest: Dict[str, Any]) -> None:
    art = manifest.get("campaign_art") or []
    sounds = manifest.get("campaign_sounds") or []
    music = manifest.get("campaign_music") or []
    fonts = manifest.get("installed_fonts") or []
    font_overrides = manifest.get("font_overrides") or []
    aircraft = manifest.get("installed_aircraft") or []
    objectives = manifest.get("mission_objectives") or []
    sorties = manifest.get("sortie_sequence") or []
    target_sets = manifest.get("mission_target_sets") or []
    briefings = manifest.get("briefings") or []
    route_plan = manifest.get("route_plan") or {}
    launch = manifest.get("launch") or {}
    runtime_modes = manifest.get("runtime_modes") or []
    media_policy = manifest.get("media_policy") or {}
    runtime_contract = manifest.get("runtime_contract") or {}
    objective_capabilities = manifest.get("objective_capabilities") or {}
    verification_status = manifest.get("verification_status") or {}
    runtime_selection = manifest.get("runtime_selection") or {}
    license_review = manifest.get("asset_license_review") or {}
    launch_example = launch.get("example") or f"F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign {campaign_id}"
    raw_scenario_argv = runtime_selection.get("argv") or ["--scenario", campaign_id, "--scenario-base", base_theater]
    raw_scenario_example = (
        f"F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME "
        + " ".join(str(arg) for arg in raw_scenario_argv)
    )

    lines = [
        f"# {campaign_id} custom campaign pack",
        "",
        "This directory is a modern F-15 SE2 EX campaign replacement pack.",
        "",
        "Run:",
        "",
        "```bash",
        launch_example,
        "```",
        "",
        "Or from this campaign directory:",
        "",
        "```bash",
        "./run_campaign.sh /path/to/F15_GAME",
        "./run_campaign.sh /path/to/F15_GAME carrier_strike",
        "./run_campaign.sh /path/to/F15_GAME 3",
        "```",
        "",
        "Set `F15_EXE=/path/to/f15se2-ex` if the executable is not in the current directory. Set `F15_CAMPAIGN_SORTIE` or pass the optional second argument to select a sortie by id or phase.",
        "",
        f"The manifest redirects `{base_theater}.WLD` to `{campaign_id}.WLD.json` while this campaign is selected.",
        "",
        "## Custom world loading",
        "",
        "Use the campaign wrapper for normal play because it selects the WLD, campaign-local media, sortie briefing, and mission target hints together:",
        "",
        "```bash",
        launch_example,
        f"{launch_example} --campaign-sortie carrier_strike",
        "```",
        "",
        "Use the lower-level scenario loader when testing only the custom WLD JSON redirection against the base theater:",
        "",
        "```bash",
        raw_scenario_example,
        "```",
        "",
        "Equivalent environment variables:",
        "",
        "```bash",
        f"F15_REPLACEMENT_ROOT=converted_assets_all F15_WORLD_SCENARIO={campaign_id} F15_WORLD_SCENARIO_BASE={base_theater} ./f15se2-ex --game /path/to/F15_GAME",
        "```",
        "",
        "`F15_REPLACEMENT_ROOT` must point at the directory containing this campaign directory, not at the campaign directory itself.",
        "",
        "Launch metadata:",
        "",
        f"- argv: `{' '.join(str(arg) for arg in launch.get('argv', ['--campaign', campaign_id]))}`",
        f"- replacement root: `{(launch.get('environment') or {}).get('F15_REPLACEMENT_ROOT', 'converted_assets_all')}`",
        f"- raw scenario argv: `{' '.join(str(arg) for arg in raw_scenario_argv)}`",
        "",
    ]
    if runtime_modes:
        lines.extend(["Runtime modes:", ""])
        for mode in runtime_modes:
            if not isinstance(mode, dict):
                continue
            lines.append(
                f"- `{mode.get('id')}`: `{' '.join(str(arg) for arg in mode.get('argv', []))}` - {mode.get('title', '')}"
            )
        lines.append("")
    lines.extend([
        "Editable files:",
        "",
        f"- `{campaign_id}.WLD.json`: authoritative world/scenario data.",
        "- `MISSION_TARGETS.md`: generated mission objective and target-package checklist.",
        "- `CUSTOM_ASSETS_README.md`: customizer guide for replacing modern media/source files.",
        "- `SUMMARY.md`: short human-readable campaign overview.",
        "- `inventory.json`: generated file inventory for review/distribution tooling.",
    ])
    lines.extend([
        "",
        "## Installing this ZIP on another setup",
        "",
        "Inspect the package before extracting:",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py inspect-campaign-package converted_assets_all/{campaign_id}.zip",
        "```",
        "",
        "Install into a replacement asset root:",
        "",
        "```bash",
        f"python3 tools/f15assets/cli.py install-campaign-package converted_assets_all/{campaign_id}.zip /path/to/converted_assets_all --replace",
        "```",
        "",
        "Run after install:",
        "",
        "```bash",
        f"F15_REPLACEMENT_ROOT=/path/to/converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign {campaign_id}",
        "```",
        "",
        "Use `--dry-run` on `install-campaign-package` to preview extraction without changing files.",
    ])
    if media_policy:
        images = media_policy.get("images") or {}
        sound_policy = media_policy.get("sounds") or {}
        fonts_policy = media_policy.get("fonts") or {}
        models = media_policy.get("models") or {}
        lines.extend([
            "",
            "Modern media policy:",
            "",
            f"- Images: {images.get('source_of_truth', 'PNG')} source, {images.get('scaling', 'fit_original_game_rectangle')} scaling.",
            f"- Sounds: {sound_policy.get('source_of_truth', 'individual WAV cue files')}.",
            f"- Fonts: {fonts_policy.get('source_of_truth', 'TTF/OTF, then BDF/PNG fallback')}.",
            f"- Models: {models.get('source_of_truth', 'per-shape GLB files')}.",
        ])
    if objective_capabilities:
        lines.extend(["", "Objective capability map:"])
        for capability_id, capability in objective_capabilities.items():
            if not isinstance(capability, dict):
                continue
            status = "provided" if capability.get("provided") else "not provided"
            artifacts = capability.get("artifacts") or []
            artifact_text = ", ".join(f"`{artifact}`" for artifact in artifacts[:4])
            if len(artifacts) > 4:
                artifact_text += f", ... ({len(artifacts)} total)"
            lines.append(f"- `{capability_id}`: {status}" + (f" via {artifact_text}." if artifact_text else "."))
    if verification_status:
        lines.extend(["", "Verification status:"])
        for key in (
            "package_validation",
            "capability_artifact_validation",
            "wld_json_buildability",
            "replacement_loadability_validation",
            "runtime_launch_validation",
            "full_asset_replacement_validation",
        ):
            if key in verification_status:
                lines.append(f"- `{key}`: {verification_status.get(key)}")
                evidence = verification_status.get(f"{key}_evidence") or {}
                if isinstance(evidence, dict) and evidence.get("command"):
                    lines.append(f"  Evidence command: `{evidence.get('command')}`")
                if isinstance(evidence, dict) and evidence.get("recorded_at"):
                    lines.append(f"  Recorded at: `{evidence.get('recorded_at')}`")
        if verification_status.get("notes"):
            lines.append(f"- notes: {verification_status.get('notes')}")
    if runtime_contract:
        lines.extend(["", "Runtime contract:", "", "Modern-editable now:"])
        for item in runtime_contract.get("modern_editable", []):
            lines.append(f"- {item}")
        lines.extend(["", "Still legacy-driven or fallback-backed:"])
        for item in runtime_contract.get("legacy_driven", []):
            lines.append(f"- {item}")
        if runtime_contract.get("notes"):
            lines.extend(["", str(runtime_contract.get("notes"))])
    if license_review:
        lines.extend([
            "",
            "Asset license review:",
            "",
            f"- status: `{license_review.get('status')}`",
            f"- notes: {license_review.get('notes')}",
        ])
    if art:
        lines.extend(["", "Campaign art:"])
        for item in art:
            dims = f"{item.get('width')}x{item.get('height')} {item.get('color_model')}" if item.get("width") and item.get("height") else str(item.get("kind"))
            target = ""
            if item.get("target_width") and item.get("target_height"):
                target = f", target {item.get('target_width')}x{item.get('target_height')}"
            runtime_path = f", runtime `{item.get('runtime_path')}`" if item.get("runtime_path") else ""
            source_format = (item.get("replacement_contract") or {}).get("source_format") or "PNG"
            lines.append(f"- `{item.get('file')}` replaces `{item.get('replaces')}` ({dims}{target}, source `{source_format}`{runtime_path}).")
    else:
        lines.extend(["", "Campaign art: none generated. Add campaign-local PNG files such as `TITLE.png` or `DESK.png` if needed."])

    if sounds:
        lines.extend(["", "Campaign radio cues:"])
        for item in sounds:
            if isinstance(item, str):
                item = {"file": item}
            script = item.get("script")
            suffix = f"; script `{script}`" if script else ""
            contract = item.get("replacement_contract") or {}
            source_format = f", source `{contract.get('source_format')}`" if contract.get("source_format") else ""
            runtime_path = f", runtime `{contract.get('runtime_path')}`" if contract.get("runtime_path") else ""
            audio_format = ""
            if item.get("sample_rate_hz") or item.get("postprocess"):
                audio_bits = []
                if item.get("sample_rate_hz"):
                    audio_bits.append(f"{item.get('sample_rate_hz')} Hz")
                if item.get("postprocess"):
                    audio_bits.append(str(item.get("postprocess")))
                audio_format = f" ({', '.join(audio_bits)})"
            lines.append(f"- `{item.get('file')}` replaces `{item.get('replaces')}`{suffix}{source_format}{runtime_path}{audio_format}.")
    else:
        lines.extend(["", "Campaign radio cues: none generated. Add `sounds/*.wav` cue overrides if needed."])

    if music:
        lines.extend(["", "Campaign intro music:"])
        for item in music:
            contract = item.get("replacement_contract") or {}
            source_format = f", source `{contract.get('source_format')}`" if contract.get("source_format") else ""
            runtime_path = f", runtime `{contract.get('runtime_path')}`" if contract.get("runtime_path") else ""
            lines.append(f"- `{item.get('file')}` replaces `{item.get('replaces')}` ({item.get('kind')}{source_format}{runtime_path}).")
    else:
        lines.extend(["", "Campaign intro music: none generated. Add `sounds/intro_music.asound.json` if needed."])

    lines.extend(["", "Fonts:"])
    if fonts:
        for item in fonts:
            lines.append(f"- `{item}` installed from `--font`; replace it with another TTF/OTF to change campaign text rendering.")
    else:
        for item in font_overrides:
            contract = item.get("replacement_contract") or {}
            source_format = f", source `{contract.get('source_format')}`" if contract.get("source_format") else ""
            runtime_path = f", runtime `{contract.get('runtime_path')}`" if contract.get("runtime_path") else ""
            lines.append(f"- `{item.get('file')}` optional for font {item.get('font_id')}{source_format}{runtime_path}: {item.get('purpose')}")

    lines.extend(["", "Aircraft GLB replacements:"])
    if aircraft:
        for item in aircraft:
            contract = item.get("replacement_contract") or {}
            runtime_path = f", runtime `{contract.get('runtime_path')}`" if contract.get("runtime_path") else ""
            lines.append(
                f"- slot {item.get('slot'):03d}: `{item.get('file')}` copied from `{item.get('source')}`{runtime_path}."
            )
    else:
        lines.append("- none installed. Use `--aircraft-glb SLOT=/path/model.glb` or edit campaign-local `15FLT/shape_###*.glb` later.")

    if objectives:
        lines.extend(["", "Mission objective starter plan:"])
        for item in objectives:
            lines.append(
                f"- `{item.get('id')}` slot {item.get('object_slot')}: "
                f"{item.get('name')} [{item.get('priority')}] - {item.get('intent')}"
            )
        lines.append("Edit object placement and objective labels in the WLD JSON or map editor.")

    if route_plan.get("waypoints"):
        lines.extend(["", "Route plan:"])
        lines.append(
            f"- `{campaign_id}.WLD.json` includes `campaign_route_plan` with approximate OSM-derived anchors."
        )
        for item in route_plan.get("waypoints", []):
            lines.append(
                f"- `{item.get('id')}`: {item.get('label')} at `{item.get('x_coord')},{item.get('y_coord')}`."
            )
        lines.append(
            "- The map editor can show these legs with `Routes on`; dragging or numerically editing a routed object updates the matching waypoint/annotation coordinates."
        )

    if sorties:
        lines.extend(["", "Starter sortie sequence:"])
        for item in sorties:
            objective_ids = ", ".join(str(value) for value in item.get("objective_ids", []))
            lines.append(f"- `{item.get('id')}`: {item.get('title')} ({objective_ids}) - {item.get('briefing')}")
        lines.extend(["", "Launch a specific sortie:"])
        for item in sorties:
            lines.append(
                f"- `{item.get('title')}`: `./run_campaign.sh /path/to/F15_GAME {item.get('id')}` "
                f"or `./run_campaign.sh /path/to/F15_GAME {item.get('phase')}`"
            )

    if target_sets:
        lines.extend(["", "Mission target packages:"])
        for item in target_sets:
            lines.append(
                f"- `{item.get('id')}`: {item.get('title')} - "
                f"{item.get('package_role')}."
            )
        lines.append(
            "Each package defines target elements, attack order, and victory logic for editor/future runtime mission logic. "
            "The map editor shows matching packages in the selected-object details panel, highlights related package objectives "
            "on the map, and lets you edit package role, attack order, and victory JSON."
        )

    if briefings:
        lines.extend(["", "Editable briefing files:"])
        for item in briefings:
            lines.append(f"- `{item.get('file')}` for sortie `{item.get('sortie_id')}`.")

    lines.extend([
        "",
        "Edit workflow:",
        "",
        "1. Edit WLD placement/objectives in `tools/f15assets/map_editor.html`.",
        "2. Review generated `MISSION_TARGETS.md` for objective slots, target packages, attack order, and victory logic.",
        "3. Edit `briefings/*.md` for mission text; packaging syncs each Markdown title/summary into the selected sortie fields shown in-game.",
        "4. Replace PNG/WAV/font/GLB media files as needed.",
        "5. Run `python3 -m tools.f15assets.cli refresh-campaign-inventory converted_assets_all/SVN --pretty` when you want to sync docs/inventory without zipping.",
        "6. Run `python3 -m tools.f15assets.cli package-campaign converted_assets_all/SVN converted_assets_all/SVN.zip --pretty` to sync docs, refresh inventory, validate, and zip.",
        "7. Optionally run full replacement validation with the command below.",
        "",
        "`--pretty` is accepted both globally and on `package-campaign`; omit it only when you intentionally want compact JSON.",
        "",
        "Validation:",
        "",
        "```bash",
        "python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --loadability-only",
        "```",
        "",
    ])
    out_dir.joinpath("README.md").write_text("\n".join(lines), encoding="utf-8")


def cmd_new_campaign(args: argparse.Namespace) -> int:
    template_path = Path(args.template)
    output_root = Path(args.output)
    if not template_path.exists():
        raise ValueError(f"template WLD JSON does not exist: {template_path}")

    payload = json.loads(template_path.read_text(encoding="utf-8"))
    campaign = args.kind.lower()
    if campaign not in {"soviet-vietnam"}:
        raise ValueError(f"unsupported campaign kind: {args.kind}")

    out = _apply_soviet_vietnam_campaign(payload)
    campaign_id = str(out.get("campaign", {}).get("id", "SVN"))
    base_theater = str(args.base_theater or template_path.stem.split(".")[0]).upper()
    out.setdefault("campaign", {})["base_theater"] = base_theater
    out["runtime_selection"] = {
        "scenario": campaign_id,
        "scenario_base": base_theater,
        "argv": ["--scenario", campaign_id, "--scenario-base", base_theater],
        "environment": {
            "F15_WORLD_SCENARIO": campaign_id,
            "F15_WORLD_SCENARIO_BASE": base_theater,
        },
        "notes": (
            "Use with F15_REPLACEMENT_ROOT pointing at converted_assets_all. "
            "The scenario base limits WLD redirection to the intended source theater, "
            "and --campaign selects that base theater automatically in START."
        ),
    }
    out_dir = output_root / campaign_id
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / f"{campaign_id}.WLD.json"
    _write_json(json_path, out, pretty=args.pretty, media_sidecar=False)
    starter_art = [] if args.no_starter_art else _write_soviet_vietnam_starter_art(out_dir)
    starter_radio = [] if args.no_starter_radio else _write_soviet_vietnam_radio_cues(out_dir)
    starter_music = [] if args.no_starter_music else _write_soviet_vietnam_intro_music(out_dir)
    auto_radio_tts_command = ""
    if args.radio_tts_command:
        starter_radio = _install_campaign_radio_tts(out_dir, args.radio_tts_command, starter_radio)
    elif args.radio_dir:
        starter_radio = _install_campaign_radio_dir(out_dir, Path(args.radio_dir), starter_radio)
    elif not args.no_starter_radio:
        auto_radio_tts_command = _default_radio_tts_command()
        if auto_radio_tts_command:
            starter_radio = _install_campaign_radio_tts(out_dir, auto_radio_tts_command, starter_radio)
    installed_fonts = _install_campaign_font(out_dir, Path(args.font) if args.font else None)
    if not installed_fonts and not args.font:
        # Preserve customizer-supplied campaign-local scalable fonts across
        # regeneration. The build command rewrites generated campaign files but
        # should not make existing modern font replacements invisible in the
        # manifest just because they were placed manually.
        font_dir = out_dir / "fonts"
        for font_id in (1, 3, 4):
            for ext in (".ttf", ".otf"):
                candidate = font_dir / f"font_{font_id}{ext}"
                if candidate.exists() and candidate.is_file():
                    installed_fonts.append(f"fonts/{candidate.name}")
                    break
    installed_aircraft = [] if args.no_starter_aircraft else _write_soviet_vietnam_starter_aircraft_glbs(out_dir)
    installed_aircraft = _merge_campaign_aircraft_entries(
        installed_aircraft,
        _install_campaign_aircraft_glbs(output_root, out_dir, args.aircraft_glb),
    )
    mission_objectives = out.get("mission_plan", {}).get("objectives", [])
    sortie_sequence = out.get("mission_plan", {}).get("sortie_sequence", [])
    briefings = _write_campaign_briefings(out_dir, sortie_sequence, mission_objectives, f"{campaign_id}.WLD.json")
    manifest = {
        "format": "F15SE2_CAMPAIGN",
        "format_version": 1,
        "scenario_schema": out.get("campaign", {}).get("schema", "F15SE2_MODERN_CAMPAIGN_WLD"),
        "scenario_schema_version": out.get("campaign", {}).get("schema_version", 1),
        "id": campaign_id,
        "campaign_id": campaign_id,
        "title": out.get("campaign", {}).get("title", campaign_id),
        "display_name": out.get("campaign", {}).get("title", campaign_id),
        "base_theater": base_theater,
        "world": f"{campaign_id}.WLD.json",
        "runtime_selection": out.get("runtime_selection", {}),
        "launch": {
            "replacement_root": "converted_assets_all",
            "argv": ["--campaign", campaign_id],
            "environment": {
                "F15_REPLACEMENT_ROOT": "converted_assets_all",
            },
            "example": f"F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign {campaign_id}",
            "notes": "Launchers can use this block directly; runtime_selection is the lower-level WLD redirect detail.",
        },
        "runtime_modes": [
            {
                "id": "campaign",
                "title": "Full campaign",
                "argv": ["--campaign", campaign_id],
                "environment": {"F15_REPLACEMENT_ROOT": "converted_assets_all"},
                "uses_campaign_media": True,
                "uses_sortie_hints": True,
                "notes": "Normal play mode. Loads campaign.json, redirects the base theater WLD, enables campaign-local media, and applies selected sortie briefing/targets.",
            },
            {
                "id": "campaign_sortie",
                "title": "Full campaign, selected sortie",
                "argv": ["--campaign", campaign_id, "--campaign-sortie", "<sortie-id-or-phase>"],
                "environment": {
                    "F15_REPLACEMENT_ROOT": "converted_assets_all",
                    "F15_CAMPAIGN_SORTIE": "<sortie-id-or-phase>",
                },
                "uses_campaign_media": True,
                "uses_sortie_hints": True,
                "notes": "Use a sortie id such as carrier_strike, or a phase number such as 2.",
            },
            {
                "id": "raw_scenario",
                "title": "Raw custom WLD scenario",
                "argv": ["--scenario", campaign_id, "--scenario-base", base_theater],
                "environment": {
                    "F15_REPLACEMENT_ROOT": "converted_assets_all",
                    "F15_WORLD_SCENARIO": campaign_id,
                    "F15_WORLD_SCENARIO_BASE": base_theater,
                },
                "uses_campaign_media": False,
                "uses_sortie_hints": False,
                "notes": "Testing mode for only the WLD JSON redirect. Use --campaign for the complete pack.",
            },
        ],
        "media_policy": {
            "images": {
                "source_of_truth": "PNG",
                "allowed_color_models": ["indexed_with_embedded_palette", "RGBA8888", "RGB888"],
                "scaling": "fit_original_game_rectangle",
                "notes": "Custom PNGs may use higher resolution and truecolor; runtime maps them into the legacy draw target instead of treating pixels as game-world size.",
            },
            "sounds": {
                "source_of_truth": "individual WAV cue files",
                "notes": "Replace per-cue WAV files directly; the original digitized sound blob is not required for custom cue authoring.",
            },
            "fonts": {
                "source_of_truth": "TTF/OTF, then BDF/PNG fallback",
                "notes": "Use scalable Unicode fonts for localized campaign text where runtime text paths support TTF/OTF.",
            },
            "models": {
                "source_of_truth": "per-shape GLB files",
                "notes": "Keep the shape slot prefix stable so WLD/object references still resolve to the edited model.",
            },
        },
        "runtime_contract": {
            "modern_editable": [
                "campaign WLD JSON selection via --campaign/--scenario",
                "selected campaign sortie objective slots as runtime mission target hints",
                "selected campaign sortie title on the START mission briefing board",
                "selected campaign sortie briefing text on the START mission briefing board",
                "campaign-local PNG art replacements",
                "campaign-local high-resolution briefing room PNGs for native overlay",
                "campaign-local high-resolution theater map PNGs for debrief/native overlay",
                "campaign-local WAV cue replacements",
                "campaign-local ASOUND intro music stream JSON",
                "campaign-local TTF/OTF/BDF/PNG font overrides",
                "campaign-local per-shape GLB visual model replacements",
                "Markdown briefing text for authoring tools",
            ],
            "legacy_driven": [
                "aircraft flight model, weapons, AI, and hardcoded behavior tables",
                "deep mission generation rules beyond selected-sortie objective hints and WLD object/table data",
                "software 3D backend model drawing for GLB replacements",
                "unsupported original binary assets not yet mapped to modern replacements",
            ],
            "notes": "The pack is intentionally media-first and editable; original game data remains the fallback for behavior not represented by modern assets yet.",
        },
        "objective_capabilities": {
            "custom_world_scenario": {
                "provided": True,
                "artifacts": [f"{campaign_id}.WLD.json", "campaign.json"],
                "runtime_modes": ["campaign", "campaign_sortie", "raw_scenario"],
                "notes": f"`--campaign {campaign_id}` selects the campaign wrapper; `--scenario {campaign_id} --scenario-base {base_theater}` tests only WLD redirection.",
            },
            "soviet_vietnam_campaign": {
                "provided": True,
                "artifacts": ["campaign.json", "SUMMARY.md", "MISSION_TARGETS.md"],
                "theme": "Soviet/Vietnamese side against USA air, naval, logistics, and air-defense targets",
                "player_identity": "Lisicin / Li Si Cin",
            },
            "modern_png_images": {
                "provided": bool(starter_art),
                "artifacts": [str(item.get("file")) for item in starter_art if isinstance(item, dict) and item.get("file")],
                "limits_removed": ["source_resolution", "source_color_count", "legacy_palette_required"],
                "source_format": "PNG",
            },
            "modern_wav_radio": {
                "provided": bool(starter_radio),
                "artifacts": [str(item.get("file")) for item in starter_radio if isinstance(item, dict) and item.get("file")],
                "source_format": "WAV PCM",
                "language": "Russian",
                "generated_sample_rate_hz": DEFAULT_SAMPLE_RATE,
                "generated_postprocess": "local TTS WAVs are normalized to mono unsigned PCM8 radio cues when possible",
            },
            "modern_asound_music": {
                "provided": bool(starter_music),
                "artifacts": [str(item.get("file")) for item in starter_music if isinstance(item, dict) and item.get("file")],
                "source_format": "F15_ASOUND_MUSIC JSON",
            },
            "modern_fonts": {
                "provided": bool(installed_fonts),
                "artifacts": installed_fonts,
                "override_slots": [str(item.get("file")) for item in _soviet_vietnam_font_overrides() if isinstance(item, dict) and item.get("file")],
                "source_format": "TTF/OTF",
                "notes": "Font files are campaign-local override points; generated packs copy a supplied font when --font is used, otherwise these paths document where customizers place Unicode fonts.",
            },
            "modern_glb_models": {
                "provided": bool(installed_aircraft),
                "artifacts": [str(item.get("file")) for item in installed_aircraft if isinstance(item, dict) and item.get("file")],
                "source_format": "GLB",
            },
            "mission_targets": {
                "provided": bool(mission_objectives and out.get("mission_plan", {}).get("mission_target_sets")),
                "artifacts": ["campaign.json", "MISSION_TARGETS.md", f"{campaign_id}.WLD.json"],
                "objective_count": len(mission_objectives),
                "target_set_count": len(out.get("mission_plan", {}).get("mission_target_sets", [])),
            },
        },
        "verification_status": {
            "generated_by": "build-soviet-vietnam-pack",
            "package_validation": "run_during_packaging",
            "capability_artifact_validation": "all provided objective_capabilities artifacts must exist and be campaign-relative",
            "wld_json_buildability": "checked by campaign manifest validation",
            "replacement_loadability_validation": "not_recorded_by_generator",
            "runtime_launch_validation": "not_recorded_by_generator",
            "full_asset_replacement_validation": "not_recorded_by_generator",
            "notes": "This block records generator/package-level evidence only. A release should also record a game build, launch, and replacement validation result after running them.",
        },
        "mission_objectives": mission_objectives,
        "sortie_sequence": sortie_sequence,
        "mission_target_sets": out.get("mission_plan", {}).get("mission_target_sets", []),
        "route_plan": {
            "source": f"{campaign_id}.WLD.json#campaign_route_plan",
            "waypoints": (out.get("campaign_route_plan") or {}).get("waypoints", []),
            "sortie_routes": (out.get("campaign_route_plan") or {}).get("sortie_routes", []),
        },
        "briefings": briefings,
        "campaign_art": starter_art,
        "campaign_sounds": starter_radio,
        "campaign_music": starter_music,
        "font_overrides": _soviet_vietnam_font_overrides(),
        "installed_fonts": installed_fonts,
        "installed_aircraft": installed_aircraft,
        "customization_status": {
            "scenario": f"campaign-local {campaign_id}.WLD.json with route/objective metadata",
            "graphics": "high-resolution campaign-local PNG replacements",
            "models": "campaign-local procedural GLB placeholders plus optional per-slot user overrides",
            "sounds": "individual Russian radio WAV cue replacements",
            "fonts": "campaign-local TTF/OTF/BDF/PNG font override slots",
            "remaining_free_asset_work": "review or replace generated placeholders before free/CC redistribution",
        },
        "asset_license_review": {
            "status": "required_before_free_asset_distribution",
            "generated_placeholders": ["campaign_art"]
            + ([] if args.no_starter_music else ["campaign_music"])
            + ([] if args.no_starter_aircraft else ["installed_aircraft"])
            + ([] if args.no_starter_radio or args.radio_dir or args.radio_tts_command or auto_radio_tts_command else ["campaign_sounds generated without --radio-dir, --radio-tts-command, or local espeak/espeak-ng"]),
            "user_supplied_assets": [
                "installed_fonts",
            ]
            + (["installed_aircraft overrides"] if args.aircraft_glb else [])
            + (["campaign_sounds installed with --radio-dir"] if args.radio_dir else [])
            + (["campaign_sounds generated with --radio-tts-command"] if args.radio_tts_command else [])
            + (["campaign_sounds generated with local espeak/espeak-ng"] if auto_radio_tts_command else []),
            "notes": "Replace placeholders with reviewed CC/free assets before distributing a free custom asset pack.",
        },
        "notes": [
            "campaign.json is the modern custom-pack entry point for launchers and editor tools.",
            "The WLD JSON remains the authoritative editable world/scenario data.",
            "Aircraft visuals are campaign-local SVN/15FLT/shape_###_*.glb procedural placeholders unless disabled or overridden with --aircraft-glb.",
            "Campaign-local PNGs are used before global replacements while this campaign is selected.",
            "Campaign-local WAV cues are used before global cue replacements while this campaign is selected.",
            "Font override entries document where to copy Cyrillic-capable TTF/OTF files; no font file is bundled by default.",
            "Installed aircraft entries are copied into the campaign-local 15FLT shape slot directory and override shared aircraft only while the campaign is selected.",
        ],
    }
    _write_json(out_dir / "campaign.json", manifest, pretty=args.pretty, media_sidecar=False)
    manifest = _sync_campaign_manifest_sources(out_dir / "campaign.json", manifest, args.pretty)
    _write_json(out_dir / "campaign.json", manifest, pretty=args.pretty, media_sidecar=False)
    _write_campaign_target_plan(out_dir, manifest)
    _write_campaign_custom_assets_readme(out_dir, manifest)
    _write_campaign_summary(out_dir, manifest)
    _write_campaign_readme(out_dir, campaign_id, base_theater, manifest)
    _write_campaign_launcher(out_dir, campaign_id)
    _write_campaign_inventory(out_dir, manifest, args.pretty)

    # Prove the generated WLD replacement remains bridge-loadable. The binary is
    # not written by default because modern JSON is the customization source.
    build_wld(out)

    print(f"wrote custom campaign WLD JSON: {json_path}")
    print(f"wrote campaign manifest: {out_dir / 'campaign.json'}")
    print(f"wrote campaign summary: {out_dir / 'SUMMARY.md'}")
    print(f"wrote campaign inventory: {out_dir / 'inventory.json'}")
    print(f"wrote campaign readme: {out_dir / 'README.md'}")
    print(f"wrote campaign launcher: {out_dir / 'run_campaign.sh'}")
    if starter_art:
        print(f"wrote starter campaign art: {', '.join(item['file'] for item in starter_art)}")
    if starter_radio:
        print(f"wrote starter radio cues: {', '.join((item.get('file') if isinstance(item, dict) else str(item)) for item in starter_radio)}")
    if args.radio_tts_command:
        print("generated radio cue WAVs with --radio-tts-command")
    if auto_radio_tts_command:
        print("generated radio cue WAVs with local espeak/espeak-ng")
    if args.radio_dir:
        print(f"installed recorded/TTS radio cue overrides from: {args.radio_dir}")
    if installed_fonts:
        print(f"installed campaign fonts: {', '.join(installed_fonts)}")
    if installed_aircraft:
        print(f"installed aircraft GLBs: {', '.join(item['file'] for item in installed_aircraft)}")
    print(f"run with: --campaign {campaign_id}")
    print("next: edit in tools/f15assets/map_editor.html or validate with --loadability-only")
    return 0


def _converted_assets_root(path: Path) -> Path:
    if path.name != "converted_assets_all" and (path / "converted_assets_all").is_dir():
        return path / "converted_assets_all"
    return path


def _shape_slot_glb_name(slot: int, label: str) -> str:
    safe_label = _safe_output_stem(label)
    if safe_label:
        return f"shape_{slot:03d}_{safe_label}.glb"
    return f"shape_{slot:03d}.glb"


def cmd_install_aircraft_glb(args: argparse.Namespace) -> int:
    source = Path(args.glb)
    output_root = _converted_assets_root(Path(args.output))
    group_dir = output_root / args.container
    cache_dir = group_dir / "cache"
    slot = int(args.slot)

    if not source.exists() or not source.is_file():
        raise ValueError(f"input GLB does not exist: {source}")
    if source.suffix.lower() != ".glb":
        raise ValueError(f"input must be a .glb file: {source}")
    if slot < 0 or slot > 999:
        raise ValueError("--slot must be in 0..999")

    group_dir.mkdir(parents=True, exist_ok=True)
    existing = sorted(group_dir.glob(f"shape_{slot:03d}*.glb"))
    target = existing[0] if existing else group_dir / _shape_slot_glb_name(slot, args.label or source.stem)
    shutil.copyfile(source, target)

    # GLMESH is generated from GLB. Remove old caches for this slot so runtime
    # cannot keep drawing stale aircraft geometry after a custom model install.
    removed = 0
    for stale in list(group_dir.glob(f"shape_{slot:03d}*.glmesh")) + list(cache_dir.glob(f"shape_{slot:03d}*.glmesh")):
        stale.unlink()
        removed += 1

    if not args.skip_validate:
        glb_to_glmesh_bytes(target)

    print(f"installed aircraft GLB slot {slot:03d}: {target}")
    if removed:
        print(f"removed stale generated cache files: {removed}")
    return 0




















def _structured_json_path(src: Path, relative: Path, input_root: Path, output_root: Path, fmt: str) -> Path:
    if fmt in {"WLD", "3D3", "3DT", "3DG"}:
        output_base = _asset_output_dir(src, relative, input_root, output_root, fmt) / src.name
        return output_base.with_name(output_base.name + ".json")
    return (output_root / relative).with_suffix(".json")






























def cmd_build_glmesh(args: argparse.Namespace) -> int:
    sys.stdout.buffer.write(glb_to_glmesh_bytes(Path(args.glb)))
    return 0


def _sync_campaign_manifest_from_world(manifest_path: Path, manifest: Dict[str, Any]) -> Dict[str, Any]:
    """Keep duplicated campaign manifest authoring fields in sync with WLD JSON.

    The map editor edits the WLD JSON directly. campaign.json intentionally
    duplicates mission/route summaries for launchers, package inspection, and
    distribution metadata, so refresh/package must pull those editable fields
    from the WLD before hashing or zipping.
    """
    world_name = str(manifest.get("world") or "").strip()
    if not world_name:
        return manifest
    world_path = manifest_path.parent / world_name
    if not world_path.exists() or not world_path.is_file():
        return manifest
    try:
        world_payload = json.loads(world_path.read_text(encoding="utf-8"))
    except Exception:
        return manifest
    mission_plan = world_payload.get("mission_plan") or {}
    if isinstance(mission_plan, dict):
        if isinstance(mission_plan.get("objectives"), list):
            manifest["mission_objectives"] = mission_plan.get("objectives")
        if isinstance(mission_plan.get("sortie_sequence"), list):
            manifest["sortie_sequence"] = mission_plan.get("sortie_sequence")
        if isinstance(mission_plan.get("mission_target_sets"), list):
            manifest["mission_target_sets"] = mission_plan.get("mission_target_sets")
    route_plan = world_payload.get("campaign_route_plan") or {}
    if isinstance(route_plan, dict) and (route_plan.get("waypoints") or route_plan.get("sortie_routes")):
        campaign_id = str(manifest.get("id") or manifest_path.parent.name)
        manifest["route_plan"] = {
            "source": f"{campaign_id}.WLD.json#campaign_route_plan",
            "waypoints": route_plan.get("waypoints") or [],
            "sortie_routes": route_plan.get("sortie_routes") or [],
        }
    return manifest


def _briefing_metadata_from_markdown(path: Path, existing: Dict[str, Any]) -> Dict[str, Any]:
    result = dict(existing)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except Exception:
        return result
    for line in lines:
        stripped = line.strip()
        if not stripped.startswith("Sortie id:"):
            continue
        ids = re.findall(r"`([^`]+)`", stripped)
        if ids:
            result["sortie_id"] = ids[0]
        break
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("# "):
            result["title"] = stripped[2:].strip()
            break
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.startswith("Sortie id:") or stripped.startswith("Phase:") or stripped.startswith("Player aircraft:") or stripped.startswith("Radio cues:"):
            continue
        result["summary"] = stripped
        break
    for line in lines:
        stripped = line.strip()
        if not stripped.startswith("Radio cues:"):
            continue
        cues = re.findall(r"`([^`]+)`", stripped)
        if cues:
            result["radio_cue_ids"] = cues
        break
    return result


def _sync_campaign_manifest_from_briefings(manifest_path: Path, manifest: Dict[str, Any], pretty: bool) -> Dict[str, Any]:
    briefings = manifest.get("briefings")
    by_sortie_id: Dict[str, Dict[str, Any]] = {}
    if not isinstance(briefings, list):
        return manifest
    synced = []
    for item in briefings:
        if not isinstance(item, dict):
            synced.append(item)
            continue
        rel = str(item.get("file") or "").strip()
        if not rel:
            synced.append(item)
            continue
        updated = _briefing_metadata_from_markdown(manifest_path.parent / rel, item)
        synced.append(updated)
        sortie_id = str(updated.get("sortie_id") or "").strip()
        if sortie_id:
            by_sortie_id[sortie_id] = updated
    manifest["briefings"] = synced
    if not by_sortie_id:
        return manifest

    def apply_to_sortie_sequence(sorties: Any) -> None:
        if not isinstance(sorties, list):
            return
        for sortie in sorties:
            if not isinstance(sortie, dict):
                continue
            update = by_sortie_id.get(str(sortie.get("id") or ""))
            if not update:
                continue
            if update.get("title"):
                sortie["title"] = update["title"]
            if update.get("summary"):
                sortie["briefing"] = update["summary"]
            if update.get("radio_cue_ids"):
                sortie["radio_cue_ids"] = update["radio_cue_ids"]

    # Runtime selected-sortie title/briefing is loaded from the WLD JSON, not
    # from campaign.json. Keep both copies synchronized when authors edit
    # briefing Markdown files and run refresh/package.
    apply_to_sortie_sequence(manifest.get("sortie_sequence"))
    world_name = str(manifest.get("world") or "").strip()
    if world_name:
        world_path = manifest_path.parent / world_name
        try:
            world_payload = json.loads(world_path.read_text(encoding="utf-8"))
            mission_plan = world_payload.get("mission_plan") or {}
            if isinstance(mission_plan, dict):
                apply_to_sortie_sequence(mission_plan.get("sortie_sequence"))
                # Keep formatting under the same command-level control as the
                # manifest. This path runs when packaging syncs briefings/*.md
                # back into runtime-visible sortie fields in the WLD JSON.
                _write_json(world_path, world_payload, pretty=pretty, media_sidecar=False)
        except Exception:
            pass
    return manifest


def _sync_campaign_manifest_sources(manifest_path: Path, manifest: Dict[str, Any], pretty: bool) -> Dict[str, Any]:
    manifest = _sync_campaign_manifest_from_world(manifest_path, manifest)
    manifest = _sync_campaign_manifest_from_briefings(manifest_path, manifest, pretty)
    return manifest


def _validate_campaign_manifest(path: Path, output_root: Path) -> list[str]:
    errors: list[str] = []
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"{path}: failed to parse campaign manifest: {exc}"]

    if manifest.get("format") != "F15SE2_CAMPAIGN":
        errors.append(f"{path}: format must be F15SE2_CAMPAIGN")
    if manifest.get("scenario_schema") and manifest.get("scenario_schema") != "F15SE2_MODERN_CAMPAIGN_WLD":
        errors.append(f"{path}: unsupported scenario_schema: {manifest.get('scenario_schema')}")
    if manifest.get("scenario_schema_version") and int(manifest.get("scenario_schema_version") or 0) != 1:
        errors.append(f"{path}: unsupported scenario_schema_version: {manifest.get('scenario_schema_version')}")

    def require_replacement_contract(item: Dict[str, Any], label: str) -> None:
        contract = item.get("replacement_contract") or {}
        if not isinstance(contract, dict):
            errors.append(f"{path}: {label} replacement_contract must be an object")
            return
        for required_key in ("source_of_truth", "runtime_path", "source_format", "fallback"):
            if required_key == "source_of_truth":
                if contract.get(required_key) is not True:
                    errors.append(f"{path}: {label} replacement_contract.source_of_truth should be true")
            elif not str(contract.get(required_key) or "").strip():
                errors.append(f"{path}: {label} replacement_contract missing {required_key}")

    campaign_id = str(manifest.get("id") or "").strip()
    campaign_id_alias = str(manifest.get("campaign_id") or "").strip()
    title = str(manifest.get("title") or "").strip()
    display_name = str(manifest.get("display_name") or "").strip()
    base_theater = str(manifest.get("base_theater") or "").strip()
    world_name = str(manifest.get("world") or "").strip()
    world_payload: Dict[str, Any] = {}
    if not campaign_id:
        errors.append(f"{path}: missing id")
    if not campaign_id_alias:
        errors.append(f"{path}: missing campaign_id")
    elif campaign_id and campaign_id_alias != campaign_id:
        errors.append(f"{path}: campaign_id should match id {campaign_id}")
    if not title:
        errors.append(f"{path}: missing title")
    if not display_name:
        errors.append(f"{path}: missing display_name")
    elif title and display_name != title:
        errors.append(f"{path}: display_name should match title")
    verification_status = manifest.get("verification_status") or {}
    if verification_status:
        if not isinstance(verification_status, dict):
            errors.append(f"{path}: verification_status must be an object")
        else:
            for required_key in (
                "package_validation",
                "capability_artifact_validation",
                "wld_json_buildability",
                "replacement_loadability_validation",
                "runtime_launch_validation",
                "full_asset_replacement_validation",
            ):
                if not str(verification_status.get(required_key) or "").strip():
                    errors.append(f"{path}: verification_status missing {required_key}")
            allowed_package_statuses = {"run_during_packaging", "passed", "failed", "skipped_by_request"}
            allowed_runtime_statuses = {"not_recorded_by_generator", "passed", "failed", "not_applicable"}
            if verification_status.get("package_validation") not in allowed_package_statuses:
                errors.append(f"{path}: verification_status.package_validation has unsupported value")
            if verification_status.get("runtime_launch_validation") not in allowed_runtime_statuses:
                errors.append(f"{path}: verification_status.runtime_launch_validation has unsupported value")
            if verification_status.get("replacement_loadability_validation") not in allowed_runtime_statuses:
                errors.append(f"{path}: verification_status.replacement_loadability_validation has unsupported value")
            if verification_status.get("full_asset_replacement_validation") not in allowed_runtime_statuses:
                errors.append(f"{path}: verification_status.full_asset_replacement_validation has unsupported value")
            evidence_required = {
                "package_validation": {"passed", "failed", "skipped_by_request"},
                "replacement_loadability_validation": {"passed", "failed"},
                "runtime_launch_validation": {"passed", "failed"},
                "full_asset_replacement_validation": {"passed", "failed"},
            }
            for key, statuses in evidence_required.items():
                if verification_status.get(key) in statuses:
                    evidence = verification_status.get(f"{key}_evidence") or {}
                    if not isinstance(evidence, dict):
                        errors.append(f"{path}: verification_status.{key}_evidence must be an object")
                        continue
                    if evidence.get("status") != verification_status.get(key):
                        errors.append(f"{path}: verification_status.{key}_evidence.status should match {key}")
                    if not str(evidence.get("command") or "").strip():
                        errors.append(f"{path}: verification_status.{key}_evidence missing command")
                    if not str(evidence.get("recorded_at") or "").strip():
                        errors.append(f"{path}: verification_status.{key}_evidence missing recorded_at")
    objective_capabilities = manifest.get("objective_capabilities") or {}
    if objective_capabilities:
        if not isinstance(objective_capabilities, dict):
            errors.append(f"{path}: objective_capabilities must be an object")
            objective_capabilities = {}

        def require_capability_artifacts_exist(capability: Dict[str, Any], capability_id: str) -> None:
            artifacts = capability.get("artifacts") or []
            if not isinstance(artifacts, list) or not artifacts:
                errors.append(f"{path}: objective_capabilities.{capability_id}.artifacts must be a non-empty list")
                return
            for artifact in artifacts:
                rel = str(artifact or "").strip()
                if not rel:
                    errors.append(f"{path}: objective_capabilities.{capability_id}.artifacts contains an empty path")
                    continue
                if Path(rel).is_absolute() or ".." in Path(rel).parts:
                    errors.append(f"{path}: objective_capabilities.{capability_id}.artifacts path must be campaign-relative: {rel}")
                    continue
                if not (path.parent / rel).exists():
                    errors.append(f"{path}: objective_capabilities.{capability_id}.artifacts references missing file: {rel}")

        for capability_id in (
            "custom_world_scenario",
            "soviet_vietnam_campaign",
            "modern_png_images",
            "modern_wav_radio",
            "modern_asound_music",
            "modern_glb_models",
            "mission_targets",
        ):
            capability = objective_capabilities.get(capability_id)
            if not isinstance(capability, dict):
                errors.append(f"{path}: objective_capabilities missing {capability_id}")
                continue
            if capability.get("provided") is not True:
                if capability_id in {"modern_png_images", "modern_wav_radio", "modern_asound_music", "modern_glb_models"}:
                    continue
                errors.append(f"{path}: objective_capabilities.{capability_id}.provided should be true")
            else:
                require_capability_artifacts_exist(capability, capability_id)
        font_capability = objective_capabilities.get("modern_fonts")
        if not isinstance(font_capability, dict):
            errors.append(f"{path}: objective_capabilities missing modern_fonts")
        else:
            override_slots = font_capability.get("override_slots") or []
            if not isinstance(override_slots, list) or not override_slots:
                errors.append(f"{path}: objective_capabilities.modern_fonts.override_slots must be a non-empty list")
            if font_capability.get("provided") is True:
                artifacts = font_capability.get("artifacts") or []
                if not isinstance(artifacts, list) or not artifacts:
                    errors.append(f"{path}: objective_capabilities.modern_fonts.artifacts must list bundled font files when provided is true")
                else:
                    require_capability_artifacts_exist(font_capability, "modern_fonts")
        png_capability = objective_capabilities.get("modern_png_images") or {}
        if isinstance(png_capability, dict):
            limits_removed = png_capability.get("limits_removed") or []
            for limit in ("source_resolution", "source_color_count", "legacy_palette_required"):
                if limit not in limits_removed:
                    errors.append(f"{path}: objective_capabilities.modern_png_images missing limits_removed {limit}")
        target_capability = objective_capabilities.get("mission_targets") or {}
        if isinstance(target_capability, dict):
            if int(target_capability.get("objective_count") or 0) <= 0:
                errors.append(f"{path}: objective_capabilities.mission_targets missing objective_count")
            if int(target_capability.get("target_set_count") or 0) <= 0:
                errors.append(f"{path}: objective_capabilities.mission_targets missing target_set_count")
    if not base_theater:
        errors.append(f"{path}: missing base_theater")
    if not world_name:
        errors.append(f"{path}: missing world")
    else:
        if campaign_id and world_name != f"{campaign_id}.WLD.json":
            errors.append(f"{path}: world should be {campaign_id}.WLD.json for campaign id {campaign_id}")
        world_path = path.parent / world_name
        if not world_path.exists():
            errors.append(f"{path}: referenced world does not exist: {world_name}")
        else:
            try:
                world_payload = json.loads(world_path.read_text(encoding="utf-8"))
                if world_payload.get("format") != "WLD":
                    errors.append(f"{path}: referenced world is not WLD JSON: {world_name}")
                else:
                    build_wld(world_payload)
            except Exception as exc:
                errors.append(f"{path}: referenced world is not build-loadable: {world_name}: {exc}")

    runtime_selection = manifest.get("runtime_selection") or {}
    if runtime_selection:
        scenario = str(runtime_selection.get("scenario") or "").strip()
        scenario_base = str(runtime_selection.get("scenario_base") or "").strip()
        if campaign_id and scenario != campaign_id:
            errors.append(f"{path}: runtime_selection.scenario should be {campaign_id}")
        if base_theater and scenario_base != base_theater:
            errors.append(f"{path}: runtime_selection.scenario_base should be {base_theater}")
        runtime_argv = runtime_selection.get("argv") or []
        expected_runtime_argv = ["--scenario", campaign_id, "--scenario-base", base_theater]
        if campaign_id and base_theater and runtime_argv != expected_runtime_argv:
            errors.append(f"{path}: runtime_selection.argv should be {expected_runtime_argv}")
        runtime_env = runtime_selection.get("environment") or {}
        if campaign_id and runtime_env.get("F15_WORLD_SCENARIO") != campaign_id:
            errors.append(f"{path}: runtime_selection.environment.F15_WORLD_SCENARIO should be {campaign_id}")
        if base_theater and runtime_env.get("F15_WORLD_SCENARIO_BASE") != base_theater:
            errors.append(f"{path}: runtime_selection.environment.F15_WORLD_SCENARIO_BASE should be {base_theater}")

    launch = manifest.get("launch") or {}
    if launch:
        argv = launch.get("argv") or []
        if campaign_id and ["--campaign", campaign_id] != argv:
            errors.append(f"{path}: launch.argv should be ['--campaign', '{campaign_id}']")
        replacement_root = str(launch.get("replacement_root") or "").strip()
        if replacement_root and replacement_root != "converted_assets_all":
            errors.append(f"{path}: launch.replacement_root should be converted_assets_all for portable campaign packs")
        env = launch.get("environment") or {}
        if not env.get("F15_REPLACEMENT_ROOT"):
            errors.append(f"{path}: launch.environment missing F15_REPLACEMENT_ROOT")

    runtime_modes = manifest.get("runtime_modes") or []
    if runtime_modes:
        modes_by_id = {
            str(item.get("id") or ""): item
            for item in runtime_modes
            if isinstance(item, dict)
        }
        for required_mode in ("campaign", "campaign_sortie", "raw_scenario"):
            if required_mode not in modes_by_id:
                errors.append(f"{path}: runtime_modes missing {required_mode}")
        campaign_mode = modes_by_id.get("campaign") or {}
        if campaign_mode and campaign_mode.get("argv") != ["--campaign", campaign_id]:
            errors.append(f"{path}: runtime_modes campaign argv should be ['--campaign', '{campaign_id}']")
        raw_mode = modes_by_id.get("raw_scenario") or {}
        if raw_mode and raw_mode.get("argv") != ["--scenario", campaign_id, "--scenario-base", base_theater]:
            errors.append(f"{path}: runtime_modes raw_scenario argv should be ['--scenario', '{campaign_id}', '--scenario-base', '{base_theater}']")
        for mode_id, mode in modes_by_id.items():
            env = mode.get("environment") or {}
            if not env.get("F15_REPLACEMENT_ROOT"):
                errors.append(f"{path}: runtime_modes {mode_id} missing F15_REPLACEMENT_ROOT")

    launcher_path = path.parent / "run_campaign.sh"
    if launcher_path.exists():
        try:
            launcher_text = launcher_path.read_text(encoding="utf-8")
        except Exception as exc:
            errors.append(f"{path}: run_campaign.sh is not readable: {exc}")
        else:
            if "--campaign" not in launcher_text:
                errors.append(f"{path}: run_campaign.sh does not pass --campaign to the game")
            if "F15_REPLACEMENT_ROOT" not in launcher_text:
                errors.append(f"{path}: run_campaign.sh does not set F15_REPLACEMENT_ROOT")
        try:
            if (launcher_path.stat().st_mode & 0o111) == 0:
                errors.append(f"{path}: run_campaign.sh is not executable")
        except OSError as exc:
            errors.append(f"{path}: run_campaign.sh mode is not readable: {exc}")

    required_doc_sections = {
        "README.md": ["Custom world loading", "raw scenario argv", "Installing this ZIP"],
        "SUMMARY.md": ["Mission targets"],
        "MISSION_TARGETS.md": ["Runtime target selection"],
        "CUSTOM_ASSETS_README.md": ["Scenario and mission targets", "Free/CC redistribution checklist", "Install packaged ZIP"],
    }
    for doc_name, required_texts in required_doc_sections.items():
        doc_path = path.parent / doc_name
        if not doc_path.exists():
            errors.append(f"{path}: missing generated campaign doc: {doc_name}")
            continue
        try:
            doc_text = doc_path.read_text(encoding="utf-8")
        except Exception as exc:
            errors.append(f"{path}: generated campaign doc is not readable: {doc_name}: {exc}")
            continue
        for required_text in required_texts:
            if required_text not in doc_text:
                errors.append(f"{path}: generated campaign doc {doc_name} is missing section text: {required_text}")
        if doc_name == "MISSION_TARGETS.md" and campaign_id:
            if f"# {campaign_id} mission target plan" not in doc_text:
                errors.append(f"{path}: MISSION_TARGETS.md does not match campaign id {campaign_id}")
            for sortie in manifest.get("sortie_sequence") or []:
                if not isinstance(sortie, dict):
                    continue
                sortie_id = str(sortie.get("id") or "").strip()
                if sortie_id and f"`{sortie_id}`" not in doc_text:
                    errors.append(f"{path}: MISSION_TARGETS.md is missing sortie id {sortie_id}")
                phase = str(sortie.get("phase") or "").strip()
                if sortie_id and phase and f"--campaign-sortie {sortie_id}" not in doc_text:
                    errors.append(f"{path}: MISSION_TARGETS.md is missing launch selector for sortie {sortie_id}")
                if phase and f"--campaign-sortie {phase}" not in doc_text:
                    errors.append(f"{path}: MISSION_TARGETS.md is missing phase selector for sortie phase {phase}")
        if doc_name == "CUSTOM_ASSETS_README.md" and campaign_id:
            if f"# {campaign_id} custom asset guide" not in doc_text:
                errors.append(f"{path}: CUSTOM_ASSETS_README.md does not match campaign id {campaign_id}")

    media_policy = manifest.get("media_policy") or {}
    image_policy = media_policy.get("images") or {}
    if image_policy:
        if image_policy.get("source_of_truth") != "PNG":
            errors.append(f"{path}: media_policy.images.source_of_truth should be PNG")
        if image_policy.get("scaling") != "fit_original_game_rectangle":
            errors.append(f"{path}: media_policy.images.scaling should be fit_original_game_rectangle")
    runtime_contract = manifest.get("runtime_contract") or {}
    if runtime_contract:
        if not runtime_contract.get("modern_editable"):
            errors.append(f"{path}: runtime_contract.modern_editable is empty")
        if not runtime_contract.get("legacy_driven"):
            errors.append(f"{path}: runtime_contract.legacy_driven is empty")
    license_review = manifest.get("asset_license_review") or {}
    if license_review and license_review.get("status") != "required_before_free_asset_distribution":
        errors.append(f"{path}: asset_license_review.status should be required_before_free_asset_distribution")

    objective_ids = set()
    objective_slots: dict[str, int] = {}
    for idx, item in enumerate(manifest.get("mission_objectives") or []):
        if not isinstance(item, dict):
            errors.append(f"{path}: mission_objectives[{idx}] must be an object")
            continue
        objective_id = str(item.get("id") or "").strip()
        if not objective_id:
            errors.append(f"{path}: mission_objectives[{idx}] missing id")
        elif objective_id in objective_ids:
            errors.append(f"{path}: mission_objectives[{idx}] duplicates objective id: {objective_id}")
        objective_ids.add(objective_id)
        if "object_slot" not in item:
            errors.append(f"{path}: mission_objectives[{idx}] missing object_slot")
        else:
            try:
                objective_slots[objective_id] = int(item.get("object_slot"))
            except (TypeError, ValueError):
                errors.append(f"{path}: mission_objectives[{idx}] object_slot is not numeric")
        for required_key in ("faction", "domain", "action", "target_type", "threat_level", "desired_effect", "suggested_loadout"):
            if not str(item.get(required_key) or "").strip():
                errors.append(f"{path}: mission_objectives[{idx}] missing {required_key}")
        if not isinstance(item.get("success_criteria"), list) or not item.get("success_criteria"):
            errors.append(f"{path}: mission_objectives[{idx}] missing success_criteria")
        if not isinstance(item.get("radio_cue_ids"), list) or not item.get("radio_cue_ids"):
            errors.append(f"{path}: mission_objectives[{idx}] missing radio_cue_ids")
    for idx, item in enumerate(manifest.get("sortie_sequence") or []):
        if not isinstance(item, dict):
            errors.append(f"{path}: sortie_sequence[{idx}] must be an object")
            continue
        if not str(item.get("id") or "").strip():
            errors.append(f"{path}: sortie_sequence[{idx}] missing id")
        if int(item.get("phase") or 0) <= 0:
            errors.append(f"{path}: sortie_sequence[{idx}] missing positive phase")
        if not str(item.get("player_aircraft") or "").strip():
            errors.append(f"{path}: sortie_sequence[{idx}] missing player_aircraft")
        if not isinstance(item.get("radio_cue_ids"), list) or not item.get("radio_cue_ids"):
            errors.append(f"{path}: sortie_sequence[{idx}] missing radio_cue_ids")
        sortie_objective_ids = set(str(objective_id) for objective_id in item.get("objective_ids") or [])
        for objective_id in item.get("objective_ids") or []:
            if objective_id not in objective_ids:
                errors.append(f"{path}: sortie_sequence[{idx}] references unknown objective id: {objective_id}")
        for group_key in ("primary_objective_ids", "secondary_objective_ids"):
            if group_key in item and not isinstance(item.get(group_key), list):
                errors.append(f"{path}: sortie_sequence[{idx}] {group_key} must be a list")
                continue
            for objective_id in item.get(group_key) or []:
                if objective_id not in objective_ids:
                    errors.append(f"{path}: sortie_sequence[{idx}] {group_key} references unknown objective id: {objective_id}")
                if str(objective_id) not in sortie_objective_ids:
                    errors.append(f"{path}: sortie_sequence[{idx}] {group_key} objective {objective_id} is not listed in objective_ids")
        for singular_key, plural_key in (
            ("primary_objective_id", "primary_objective_ids"),
            ("secondary_objective_id", "secondary_objective_ids"),
        ):
            singular_value = str(item.get(singular_key) or "").strip()
            plural_values = [str(value) for value in item.get(plural_key) or []]
            if singular_value and singular_value not in objective_ids:
                errors.append(f"{path}: sortie_sequence[{idx}] {singular_key} references unknown objective id: {singular_value}")
            if singular_value and plural_values and singular_value != plural_values[0]:
                errors.append(f"{path}: sortie_sequence[{idx}] {singular_key} differs from first {plural_key}")

    sortie_ids = {
        str(item.get("id") or "").strip()
        for item in manifest.get("sortie_sequence") or []
        if isinstance(item, dict)
    }
    for idx, item in enumerate(manifest.get("mission_target_sets") or []):
        if not isinstance(item, dict):
            errors.append(f"{path}: mission_target_sets[{idx}] must be an object")
            continue
        target_set_id = str(item.get("id") or "").strip()
        if not target_set_id:
            errors.append(f"{path}: mission_target_sets[{idx}] missing id")
        sortie_id = str(item.get("sortie_id") or "").strip()
        if sortie_id not in sortie_ids:
            errors.append(f"{path}: mission_target_sets[{idx}] references unknown sortie id: {sortie_id}")
        if not isinstance(item.get("target_elements"), list) or not item.get("target_elements"):
            errors.append(f"{path}: mission_target_sets[{idx}] missing target_elements")
        if not isinstance(item.get("attack_order"), list) or not item.get("attack_order"):
            errors.append(f"{path}: mission_target_sets[{idx}] missing attack_order")
        for objective_id in item.get("objective_ids") or []:
            if objective_id not in objective_ids:
                errors.append(f"{path}: mission_target_sets[{idx}] references unknown objective id: {objective_id}")
        element_ids = set()
        for element_idx, element in enumerate(item.get("target_elements") or []):
            if not isinstance(element, dict):
                errors.append(f"{path}: mission_target_sets[{idx}].target_elements[{element_idx}] must be an object")
                continue
            element_id = str(element.get("element_id") or "").strip()
            if not element_id:
                errors.append(f"{path}: mission_target_sets[{idx}].target_elements[{element_idx}] missing element_id")
            else:
                element_ids.add(element_id)
            objective_id = str(element.get("objective_id") or "").strip()
            if objective_id not in objective_ids:
                errors.append(
                    f"{path}: mission_target_sets[{idx}].target_elements[{element_idx}] references unknown objective id: {objective_id}"
                )
        for element_id in item.get("attack_order") or []:
            if element_id not in element_ids:
                errors.append(f"{path}: mission_target_sets[{idx}].attack_order references unknown element id: {element_id}")

    route_plan = manifest.get("route_plan") or {}
    if route_plan:
        if not isinstance(route_plan, dict):
            errors.append(f"{path}: route_plan must be an object")
        else:
            waypoints = route_plan.get("waypoints") or []
            routes = route_plan.get("sortie_routes") or []
            if not isinstance(waypoints, list) or not waypoints:
                errors.append(f"{path}: route_plan.waypoints is empty")
                waypoints = []
            if not isinstance(routes, list) or not routes:
                errors.append(f"{path}: route_plan.sortie_routes is empty")
                routes = []
            waypoint_ids = set()
            for idx, waypoint in enumerate(waypoints):
                if not isinstance(waypoint, dict):
                    errors.append(f"{path}: route_plan.waypoints[{idx}] must be an object")
                    continue
                waypoint_id = str(waypoint.get("id") or "").strip()
                objective_id = str(waypoint.get("objective_id") or "").strip()
                if not waypoint_id:
                    errors.append(f"{path}: route_plan.waypoints[{idx}] missing id")
                waypoint_ids.add(waypoint_id)
                if objective_id and objective_id not in objective_ids:
                    errors.append(f"{path}: route_plan.waypoints[{idx}] references unknown objective id: {objective_id}")
                for coord_key in ("x_coord", "y_coord"):
                    try:
                        coord = int(waypoint.get(coord_key))
                    except (TypeError, ValueError):
                        errors.append(f"{path}: route_plan.waypoints[{idx}] missing numeric {coord_key}")
                        continue
                    if coord < 0 or coord >= 32768:
                        errors.append(f"{path}: route_plan.waypoints[{idx}] {coord_key} out of WLD range: {coord}")
                for geo_key in ("lon", "lat"):
                    if geo_key in waypoint:
                        try:
                            float(waypoint.get(geo_key))
                        except (TypeError, ValueError):
                            errors.append(f"{path}: route_plan.waypoints[{idx}] has non-numeric {geo_key}")
            sortie_ids = {str(item.get("id") or "") for item in manifest.get("sortie_sequence") or [] if isinstance(item, dict)}
            for idx, route in enumerate(routes):
                if not isinstance(route, dict):
                    errors.append(f"{path}: route_plan.sortie_routes[{idx}] must be an object")
                    continue
                sortie_id = str(route.get("sortie_id") or "").strip()
                if sortie_id and sortie_id not in sortie_ids:
                    errors.append(f"{path}: route_plan.sortie_routes[{idx}] references unknown sortie id: {sortie_id}")
                leg = route.get("route") or []
                if not isinstance(leg, list) or len(leg) < 2:
                    errors.append(f"{path}: route_plan.sortie_routes[{idx}].route must contain at least two waypoint ids")
                    continue
                for waypoint_id in leg:
                    if str(waypoint_id) not in waypoint_ids:
                        errors.append(f"{path}: route_plan.sortie_routes[{idx}] references unknown waypoint id: {waypoint_id}")
    if world_payload:
        world_objects = world_payload.get("world_objects") or world_payload.get("worldObjects") or []
        read_item_size = int(world_payload.get("read_item_size") or world_payload.get("readItemSize") or len(world_objects) or 0)
        if not isinstance(world_objects, list):
            errors.append(f"{path}: referenced WLD world_objects must be a list")
            world_objects = []
        if len(world_objects) != read_item_size:
            errors.append(f"{path}: referenced WLD read_item_size {read_item_size} differs from world_objects length {len(world_objects)}")
        runtime_hint_ids = set()
        for sortie in manifest.get("sortie_sequence") or []:
            if not isinstance(sortie, dict):
                continue
            for group_key in ("primary_objective_ids", "secondary_objective_ids"):
                for objective_id in sortie.get(group_key) or []:
                    runtime_hint_ids.add(str(objective_id))
        for objective_id, object_slot in objective_slots.items():
            if object_slot < 0 or object_slot >= len(world_objects):
                errors.append(f"{path}: mission objective {objective_id} object_slot {object_slot} is outside referenced WLD world_objects")
                continue
            world_object = world_objects[object_slot]
            if not isinstance(world_object, dict):
                errors.append(f"{path}: mission objective {objective_id} object_slot {object_slot} does not point to a WLD object")
                continue
            if objective_id in runtime_hint_ids and object_slot < 3:
                errors.append(
                    f"{path}: selectable runtime target objective {objective_id} points at scratch slot {object_slot}; use WLD object slot 3 or later"
                )
            for coord_key in ("x_coord", "y_coord"):
                try:
                    coord = int(world_object.get(coord_key))
                except (TypeError, ValueError):
                    errors.append(f"{path}: mission objective {objective_id} WLD object slot {object_slot} missing numeric {coord_key}")
                    continue
                if coord < 0 or coord >= 32768:
                    errors.append(f"{path}: mission objective {objective_id} WLD object slot {object_slot} {coord_key} out of range: {coord}")
        world_mission_plan = world_payload.get("mission_plan") or {}
        if isinstance(world_mission_plan, dict):
            for manifest_key, world_key in [
                ("mission_objectives", "objectives"),
                ("sortie_sequence", "sortie_sequence"),
                ("mission_target_sets", "mission_target_sets"),
            ]:
                world_value = world_mission_plan.get(world_key)
                if isinstance(world_value, list) and manifest.get(manifest_key) != world_value:
                    errors.append(f"{path}: {manifest_key} differs from referenced WLD mission_plan.{world_key}")
        world_route_plan = world_payload.get("campaign_route_plan") or {}
        manifest_waypoints = (manifest.get("route_plan") or {}).get("waypoints") or []
        world_waypoints = world_route_plan.get("waypoints") or []
        if manifest.get("route_plan") and not world_route_plan:
            errors.append(f"{path}: manifest route_plan exists but referenced WLD has no campaign_route_plan")
        if manifest_waypoints and world_waypoints:
            manifest_by_id = {str(item.get("id") or ""): item for item in manifest_waypoints if isinstance(item, dict)}
            world_by_id = {str(item.get("id") or ""): item for item in world_waypoints if isinstance(item, dict)}
            for waypoint_id, manifest_waypoint in manifest_by_id.items():
                world_waypoint = world_by_id.get(waypoint_id)
                if not world_waypoint:
                    errors.append(f"{path}: route_plan waypoint missing from WLD campaign_route_plan: {waypoint_id}")
                    continue
                for coord_key in ("x_coord", "y_coord"):
                    if manifest_waypoint.get(coord_key) != world_waypoint.get(coord_key):
                        errors.append(
                            f"{path}: route_plan waypoint {waypoint_id} {coord_key} differs from WLD campaign_route_plan"
                        )

    for key, file_key, base in [
        ("campaign_art", "file", path.parent),
        ("campaign_sounds", "file", path.parent),
        ("campaign_music", "file", path.parent),
        ("briefings", "file", path.parent),
        ("font_overrides", "file", path.parent),
    ]:
        for idx, item in enumerate(manifest.get(key) or []):
            if not isinstance(item, dict):
                errors.append(f"{path}: {key}[{idx}] must be an object")
                continue
            rel = str(item.get(file_key) or "").strip()
            if rel and key != "font_overrides" and not (base / rel).exists():
                errors.append(f"{path}: {key}[{idx}] references missing file: {rel}")
            if key == "campaign_art":
                require_replacement_contract(item, f"{key}[{idx}]")
                if int(item.get("width") or 0) <= 0 or int(item.get("height") or 0) <= 0:
                    errors.append(f"{path}: {key}[{idx}] missing positive width/height")
                if str(item.get("kind") or "").startswith("high_resolution_"):
                    if int(item.get("target_width") or 0) <= 0 or int(item.get("target_height") or 0) <= 0:
                        errors.append(f"{path}: {key}[{idx}] missing positive target_width/target_height")
                    if item.get("scaling") != "fit_original_game_rectangle":
                        errors.append(f"{path}: {key}[{idx}] scaling should be fit_original_game_rectangle")
                    if not str(item.get("runtime_path") or "").strip():
                        errors.append(f"{path}: {key}[{idx}] missing runtime_path")
                    limits = item.get("legacy_limits_removed") or {}
                    if not isinstance(limits, dict):
                        errors.append(f"{path}: {key}[{idx}] legacy_limits_removed must be an object")
                    else:
                        if limits.get("source_resolution") is not True:
                            errors.append(f"{path}: {key}[{idx}] legacy_limits_removed.source_resolution should be true")
                        if limits.get("source_pixels_are_game_pixels") is not False:
                            errors.append(f"{path}: {key}[{idx}] legacy_limits_removed.source_pixels_are_game_pixels should be false")
                if not item.get("color_model"):
                    errors.append(f"{path}: {key}[{idx}] missing color_model")
                art_path = base / rel
                if rel and art_path.exists():
                    try:
                        png_meta = _read_png_metadata(art_path)
                    except Exception as exc:
                        errors.append(f"{path}: {key}[{idx}] is not readable PNG data: {rel}: {exc}")
                    else:
                        if item.get("width") is not None and int(item.get("width") or 0) != int(png_meta["width"]):
                            errors.append(
                                f"{path}: {key}[{idx}] width {item.get('width')} differs from PNG width {png_meta['width']}: {rel}"
                            )
                        if item.get("height") is not None and int(item.get("height") or 0) != int(png_meta["height"]):
                            errors.append(
                                f"{path}: {key}[{idx}] height {item.get('height')} differs from PNG height {png_meta['height']}: {rel}"
                            )
                        actual_color_model = _png_color_model_from_metadata(png_meta)
                        if item.get("color_model") and item.get("color_model") != actual_color_model:
                            errors.append(
                                f"{path}: {key}[{idx}] color_model {item.get('color_model')} differs from PNG {actual_color_model}: {rel}"
                            )
            if key == "campaign_sounds":
                require_replacement_contract(item, f"{key}[{idx}]")
                sound_path = base / rel
                if rel and sound_path.exists() and rel.lower().endswith(".wav"):
                    try:
                        wav_meta = _read_wav_metadata(sound_path)
                    except Exception as exc:
                        errors.append(f"{path}: {key}[{idx}] is not readable WAV data: {rel}: {exc}")
                    else:
                        if int(wav_meta.get("audio_format") or 0) != 1:
                            errors.append(f"{path}: {key}[{idx}] WAV is not PCM format: {rel}")
                        if item.get("channels") is not None and int(item.get("channels") or 0) != int(wav_meta["channels"]):
                            errors.append(
                                f"{path}: {key}[{idx}] channels {item.get('channels')} differs from WAV channels {wav_meta['channels']}: {rel}"
                            )
                        if item.get("sample_rate_hz") is not None and int(item.get("sample_rate_hz") or 0) != int(wav_meta["sample_rate_hz"]):
                            errors.append(
                                f"{path}: {key}[{idx}] sample_rate_hz {item.get('sample_rate_hz')} differs from WAV rate {wav_meta['sample_rate_hz']}: {rel}"
                            )
                        if item.get("duration_seconds") is not None:
                            declared_duration = float(item.get("duration_seconds") or 0)
                            if abs(declared_duration - float(wav_meta["duration_seconds"])) > 0.02:
                                errors.append(
                                    f"{path}: {key}[{idx}] duration_seconds {declared_duration:.3f} differs from WAV duration {wav_meta['duration_seconds']:.3f}: {rel}"
                                )
            if key == "campaign_music" and rel.endswith(".asound.json") and (base / rel).exists():
                require_replacement_contract(item, f"{key}[{idx}]")
                try:
                    music_payload = json.loads((base / rel).read_text(encoding="utf-8"))
                    for error in _validate_asound_music_payload(music_payload, base / rel):
                        errors.append(f"{path}: {key}[{idx}] {error}")
                except Exception as exc:
                    errors.append(f"{path}: {key}[{idx}] has invalid ASOUND JSON: {rel}: {exc}")
            if key == "font_overrides" and rel:
                require_replacement_contract(item, f"{key}[{idx}]")
                font_path = base / rel
                if font_path.exists():
                    try:
                        mismatch = _validate_font_container_for_extension(font_path)
                    except Exception as exc:
                        errors.append(f"{path}: {key}[{idx}] is not a readable font override: {rel}: {exc}")
                    else:
                        if mismatch:
                            errors.append(f"{path}: {key}[{idx}] {mismatch}: {rel}")

    for idx, rel_value in enumerate(manifest.get("installed_fonts") or []):
        rel = str(rel_value or "").strip()
        if not rel:
            errors.append(f"{path}: installed_fonts[{idx}] is empty")
            continue
        font_path = path.parent / rel
        if not font_path.exists():
            errors.append(f"{path}: installed_fonts[{idx}] references missing file: {rel}")
            continue
        try:
            mismatch = _validate_font_container_for_extension(font_path)
        except Exception as exc:
            errors.append(f"{path}: installed_fonts[{idx}] is not a readable font: {rel}: {exc}")
        else:
            if mismatch:
                errors.append(f"{path}: installed_fonts[{idx}] {mismatch}: {rel}")

    for idx, item in enumerate(manifest.get("briefings") or []):
        if not isinstance(item, dict):
            continue
        rel = str(item.get("file") or "").strip()
        if not rel:
            continue
        briefing_path = path.parent / rel
        if not briefing_path.exists() or not briefing_path.is_file():
            continue
        synced = _briefing_metadata_from_markdown(briefing_path, item)
        for field in ("title", "summary", "radio_cue_ids"):
            if field in synced and item.get(field) != synced.get(field):
                errors.append(f"{path}: briefings[{idx}].{field} differs from Markdown source: {rel}")
        sortie_id = str(synced.get("sortie_id") or item.get("sortie_id") or "").strip()
        mission_plan = world_payload.get("mission_plan") if isinstance(world_payload, dict) else {}
        sortie_sequence = mission_plan.get("sortie_sequence") if isinstance(mission_plan, dict) else []
        if sortie_id and isinstance(sortie_sequence, list):
            world_sortie = next(
                (sortie for sortie in sortie_sequence if isinstance(sortie, dict) and str(sortie.get("id") or "") == sortie_id),
                None,
            )
            if world_sortie:
                if synced.get("title") and world_sortie.get("title") != synced.get("title"):
                    errors.append(f"{path}: WLD sortie {sortie_id} title differs from Markdown source: {rel}")
                if synced.get("summary") and world_sortie.get("briefing") != synced.get("summary"):
                    errors.append(f"{path}: WLD sortie {sortie_id} briefing differs from Markdown source: {rel}")
                if synced.get("radio_cue_ids") and world_sortie.get("radio_cue_ids") != synced.get("radio_cue_ids"):
                    errors.append(f"{path}: WLD sortie {sortie_id} radio_cue_ids differ from Markdown source: {rel}")

    aircraft_slots: set[int] = set()
    for idx, item in enumerate(manifest.get("installed_aircraft") or []):
        if not isinstance(item, dict):
            errors.append(f"{path}: installed_aircraft[{idx}] must be an object")
            continue
        rel = str(item.get("file") or "").strip()
        try:
            slot = int(item.get("slot"))
        except (TypeError, ValueError):
            errors.append(f"{path}: installed_aircraft[{idx}] missing numeric slot")
            slot = -1
        if slot >= 0:
            if slot in aircraft_slots:
                errors.append(f"{path}: installed_aircraft[{idx}] duplicates slot {slot}")
            aircraft_slots.add(slot)
        require_replacement_contract(item, f"installed_aircraft[{idx}]")
        if not rel:
            errors.append(f"{path}: installed_aircraft[{idx}] missing file")
            continue
        aircraft_path = path.parent / rel
        if not aircraft_path.exists():
            aircraft_path = output_root / rel
        if not aircraft_path.exists():
            errors.append(f"{path}: installed_aircraft[{idx}] references missing file: {rel}")
            continue
        if aircraft_path.suffix.lower() != ".glb":
            errors.append(f"{path}: installed_aircraft[{idx}] file is not .glb: {rel}")
            continue
        try:
            glb_meta = _read_glb_metadata(aircraft_path)
        except Exception as exc:
            errors.append(f"{path}: installed_aircraft[{idx}] is not readable GLB data: {rel}: {exc}")
        else:
            if int(glb_meta.get("mesh_count") or 0) <= 0:
                errors.append(f"{path}: installed_aircraft[{idx}] GLB has no meshes: {rel}")
    inventory_path = path.parent / "inventory.json"
    if inventory_path.exists():
        try:
            inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
            if inventory.get("format") != "F15SE2_CAMPAIGN_INVENTORY":
                errors.append(f"{inventory_path}: format must be F15SE2_CAMPAIGN_INVENTORY")
            for idx, item in enumerate(inventory.get("files") or []):
                rel = str(item.get("file") or "").strip()
                if not rel:
                    continue
                file_path = path.parent / rel
                if not file_path.exists():
                    errors.append(f"{inventory_path}: files[{idx}] references missing file: {rel}")
                    continue
                if item.get("sha256") and file_path.is_file():
                    data = file_path.read_bytes()
                    digest = hashlib.sha256(data).hexdigest()
                    if digest != item.get("sha256"):
                        errors.append(f"{inventory_path}: files[{idx}] sha256 mismatch: {rel}")
                    if item.get("bytes") is not None and int(item.get("bytes")) != len(data):
                        errors.append(f"{inventory_path}: files[{idx}] byte size mismatch: {rel}")
        except Exception as exc:
            errors.append(f"{inventory_path}: failed to parse inventory: {exc}")
    return errors


def validate_campaign_manifests(output_root: Path) -> tuple[int, int]:
    checked = 0
    failed = 0
    for manifest_path in sorted(output_root.rglob("campaign.json")):
        checked += 1
        errors = _validate_campaign_manifest(manifest_path, output_root)
        if errors:
            failed += 1
            for error in errors:
                print(error, file=sys.stderr)
    return checked, failed


def cmd_list_campaigns(args: argparse.Namespace) -> int:
    output_root = _converted_assets_root(Path(args.output))
    if not output_root.exists() or not output_root.is_dir():
        raise ValueError(f"output must be an existing converted asset directory: {args.output}")
    found = []
    for manifest_path in sorted(output_root.rglob("campaign.json")):
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except Exception as exc:
            print(f"{manifest_path}: failed to parse campaign.json: {exc}", file=sys.stderr)
            continue
        if manifest.get("format") != "F15SE2_CAMPAIGN":
            continue
        found.append((manifest_path, manifest))
    if args.json:
        payload = []
        for manifest_path, manifest in found:
            item = dict(manifest)
            item["manifest_path"] = str(manifest_path)
            verification_status = manifest.get("verification_status") or {}
            item["verification_summary"] = {
                "runtime_launch_validation": verification_status.get("runtime_launch_validation", "unknown"),
                "replacement_loadability_validation": verification_status.get("replacement_loadability_validation", "unknown"),
                "full_asset_replacement_validation": verification_status.get("full_asset_replacement_validation", "unknown"),
                "runtime_launch_recorded_at": (verification_status.get("runtime_launch_validation_evidence") or {}).get("recorded_at"),
                "replacement_loadability_recorded_at": (verification_status.get("replacement_loadability_validation_evidence") or {}).get("recorded_at"),
                "full_asset_replacement_recorded_at": (verification_status.get("full_asset_replacement_validation_evidence") or {}).get("recorded_at"),
            }
            payload.append(item)
        _write_json(Path(args.json), {"campaigns": payload}, pretty=args.pretty, media_sidecar=False)
    else:
        print("id\ttitle\truntime_launch_validation\treplacement_loadability_validation\tfull_asset_replacement_validation\tmanifest_path\tlaunch_example")
        for manifest_path, manifest in found:
            launch = manifest.get("launch") or {}
            example = launch.get("example") or " ".join(str(arg) for arg in launch.get("argv") or [])
            verification_status = manifest.get("verification_status") or {}
            runtime_status = verification_status.get("runtime_launch_validation", "unknown")
            loadability_status = verification_status.get("replacement_loadability_validation", "unknown")
            replacement_status = verification_status.get("full_asset_replacement_validation", "unknown")
            print(
                f"{manifest.get('id', manifest_path.parent.name)}\t{manifest.get('title', '')}\t{runtime_status}\t{loadability_status}\t{replacement_status}\t{manifest_path}\t{example}"
            )
    if not found:
        print(f"no campaign.json manifests found under {output_root}", file=sys.stderr)
        return 1
    return 0


def cmd_refresh_campaign_inventory(args: argparse.Namespace) -> int:
    manifest_path = Path(args.manifest)
    if manifest_path.is_dir():
        manifest_path = manifest_path / "campaign.json"
    if not manifest_path.exists():
        raise ValueError(f"campaign manifest does not exist: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "F15SE2_CAMPAIGN":
        raise ValueError(f"not an F15SE2 campaign manifest: {manifest_path}")
    manifest = _sync_campaign_manifest_sources(manifest_path, manifest, args.pretty)
    _write_json(manifest_path, manifest, pretty=args.pretty, media_sidecar=False)
    _write_campaign_target_plan(manifest_path.parent, manifest)
    _write_campaign_custom_assets_readme(manifest_path.parent, manifest)
    _write_campaign_summary(manifest_path.parent, manifest)
    _write_campaign_readme(
        manifest_path.parent,
        str(manifest.get("id") or manifest_path.parent.name),
        str(manifest.get("base_theater") or ""),
        manifest,
    )
    inventory = _write_campaign_inventory(manifest_path.parent, manifest, args.pretty)
    print(f"wrote campaign inventory: {manifest_path.parent / 'inventory.json'} ({len(inventory['files'])} files)")
    return 0


def cmd_record_campaign_verification(args: argparse.Namespace) -> int:
    """Record external validation evidence after a human or CI runs the game/tests.

    The campaign generator only proves package-level invariants. This command is
    intentionally separate so runtime/build validation is recorded only after it
    has actually been performed outside the generator.
    """
    manifest_path = Path(args.manifest)
    if manifest_path.is_dir():
        manifest_path = manifest_path / "campaign.json"
    if not manifest_path.exists():
        raise ValueError(f"campaign manifest does not exist: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "F15SE2_CAMPAIGN":
        raise ValueError(f"not an F15SE2 campaign manifest: {manifest_path}")
    if not args.runtime_launch and not args.replacement_loadability and not args.full_asset_validation:
        raise ValueError("record-campaign-verification requires --runtime-launch, --replacement-loadability, or --full-asset-validation")
    if args.runtime_launch in {"passed", "failed"} and not args.runtime_launch_command:
        raise ValueError("--runtime-launch passed/failed requires --runtime-launch-command")
    if args.replacement_loadability in {"passed", "failed"} and not args.replacement_loadability_command:
        raise ValueError("--replacement-loadability passed/failed requires --replacement-loadability-command")
    if args.full_asset_validation in {"passed", "failed"} and not args.full_asset_validation_command:
        raise ValueError("--full-asset-validation passed/failed requires --full-asset-validation-command")

    verification = manifest.setdefault("verification_status", {})
    timestamp = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()

    def record(kind: str, status: str, command: str) -> None:
        if not status:
            return
        verification[kind] = status
        verification[f"{kind}_evidence"] = {
            "recorded_at": timestamp,
            "status": status,
            "command": command,
            "notes": args.notes,
        }

    record("runtime_launch_validation", args.runtime_launch, args.runtime_launch_command)
    record("replacement_loadability_validation", args.replacement_loadability, args.replacement_loadability_command)
    record("full_asset_replacement_validation", args.full_asset_validation, args.full_asset_validation_command)
    if args.notes:
        verification["latest_notes"] = args.notes

    _write_json(manifest_path, manifest, pretty=args.pretty, media_sidecar=False)
    _write_campaign_custom_assets_readme(manifest_path.parent, manifest)
    _write_campaign_summary(manifest_path.parent, manifest)
    _write_campaign_readme(
        manifest_path.parent,
        str(manifest.get("id") or manifest_path.parent.name),
        str(manifest.get("base_theater") or ""),
        manifest,
    )
    inventory = _write_campaign_inventory(manifest_path.parent, manifest, args.pretty)
    validation_errors = _validate_campaign_manifest(manifest_path, manifest_path.parent.parent)
    if validation_errors:
        for error in validation_errors:
            print(error, file=sys.stderr)
        return 1
    print(f"recorded campaign verification in {manifest_path}")
    print(f"wrote campaign inventory: {manifest_path.parent / 'inventory.json'} ({len(inventory['files'])} files)")
    return 0


def _campaign_package_command(args: argparse.Namespace, output: Path) -> str:
    command = ["python3", "tools/f15assets/cli.py", "package-campaign", str(args.campaign)]
    if str(output):
        command.append(str(output))
    if getattr(args, "allow_external_assets", False):
        command.append("--allow-external-assets")
    if getattr(args, "include_external_assets", False):
        command.append("--include-external-assets")
    if getattr(args, "skip_validation", False):
        command.append("--skip-validation")
    if getattr(args, "pretty", False):
        command.append("--pretty")
    return " ".join(shlex.quote(part) for part in command)


def cmd_package_campaign(args: argparse.Namespace) -> int:
    campaign_dir = Path(args.campaign)
    if campaign_dir.is_file():
        campaign_dir = campaign_dir.parent
    manifest_path = campaign_dir / "campaign.json"
    inventory_path = campaign_dir / "inventory.json"
    if not manifest_path.exists():
        raise ValueError(f"campaign.json does not exist: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "F15SE2_CAMPAIGN":
        raise ValueError(f"not an F15SE2 campaign manifest: {manifest_path}")
    manifest = _sync_campaign_manifest_sources(manifest_path, manifest, args.pretty)
    _write_json(manifest_path, manifest, pretty=args.pretty, media_sidecar=False)
    _write_campaign_target_plan(campaign_dir, manifest)
    _write_campaign_custom_assets_readme(campaign_dir, manifest)
    _write_campaign_summary(campaign_dir, manifest)
    _write_campaign_readme(
        campaign_dir,
        str(manifest.get("id") or campaign_dir.name),
        str(manifest.get("base_theater") or ""),
        manifest,
    )
    external_assets = [
        item for item in (manifest.get("installed_aircraft") or [])
        if isinstance(item, dict) and item.get("file") and not (campaign_dir / str(item.get("file"))).exists()
    ]
    if external_assets and not args.allow_external_assets and not args.include_external_assets:
        raise ValueError(
            "campaign references shared installed_aircraft files outside the campaign directory; "
            "pass --include-external-assets to include them, or --allow-external-assets to create a campaign-only ZIP"
        )
    output = Path(args.output) if args.output else campaign_dir.with_suffix(".zip")
    inventory = _write_campaign_inventory(campaign_dir, manifest, args.pretty)
    if not args.skip_validation:
        errors = _validate_campaign_manifest(manifest_path, campaign_dir.parent)
        if errors:
            for error in errors:
                print(error, file=sys.stderr)
            raise ValueError("campaign package validation failed; fix the manifest/assets or pass --skip-validation for a draft ZIP")
    verification = manifest.setdefault("verification_status", {})
    timestamp = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()
    package_status = "skipped_by_request" if args.skip_validation else "passed"
    verification["package_validation"] = package_status
    verification["package_validation_evidence"] = {
        "recorded_at": timestamp,
        "status": package_status,
        "command": _campaign_package_command(args, output),
        "notes": "Manifest/media validation was skipped by request." if args.skip_validation else "Campaign manifest, referenced media, inventory, WLD buildability, and package inputs validated before ZIP creation.",
    }
    _write_json(manifest_path, manifest, pretty=args.pretty, media_sidecar=False)
    _write_campaign_custom_assets_readme(campaign_dir, manifest)
    _write_campaign_summary(campaign_dir, manifest)
    _write_campaign_readme(
        campaign_dir,
        str(manifest.get("id") or campaign_dir.name),
        str(manifest.get("base_theater") or ""),
        manifest,
    )
    inventory = _write_campaign_inventory(campaign_dir, manifest, args.pretty)
    output.parent.mkdir(parents=True, exist_ok=True)
    prefix = str(manifest.get("id") or campaign_dir.name)
    package_manifest = {
        "format": "F15SE2_CAMPAIGN_PACKAGE",
        "format_version": 1,
        "campaign_id": manifest.get("id"),
        "campaign_prefix": prefix,
        "install": {
            "command": f"python3 tools/f15assets/cli.py install-campaign-package {output.name} /path/to/converted_assets_all",
            "replace_command": f"python3 tools/f15assets/cli.py install-campaign-package {output.name} /path/to/converted_assets_all --replace",
            "dry_run_command": f"python3 tools/f15assets/cli.py install-campaign-package {output.name} /path/to/converted_assets_all --dry-run",
            "output_root": "converted_assets_all",
            "notes": "Install into the replacement asset root that contains campaign directories, not into the game data directory.",
        },
        "run_after_install": {
            "command": f"F15_REPLACEMENT_ROOT=/path/to/converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign {manifest.get('id')}",
            "raw_scenario_command": f"F15_REPLACEMENT_ROOT=/path/to/converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --scenario {manifest.get('id')} --scenario-base {manifest.get('base_theater')}",
        },
        "includes_external_assets": bool(args.include_external_assets),
        "allows_external_assets": bool(args.allow_external_assets),
        "external_assets": external_assets,
    }
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(f"{prefix}/package.json", json.dumps(package_manifest, indent=2, sort_keys=True) + "\n")
        for item in inventory.get("files", []):
            rel = str(item.get("file") or "").strip()
            if not rel:
                continue
            path = campaign_dir / rel
            if not path.exists() or not path.is_file():
                raise ValueError(f"inventory references missing package file: {rel}")
            archive.write(path, f"{prefix}/{rel}")
        archive.write(inventory_path, f"{prefix}/inventory.json")
        if args.include_external_assets:
            converted_root = campaign_dir.parent
            for item in external_assets:
                rel = str(item.get("file") or "").strip()
                if not rel:
                    continue
                path = converted_root / rel
                if not path.exists() or not path.is_file():
                    raise ValueError(f"manifest references missing external asset: {rel}")
                archive.write(path, rel)
    print(f"wrote campaign package: {output}")
    return 0


def _inspect_campaign_archive(archive_path: Path) -> dict[str, Any]:
    errors: list[str] = []
    with zipfile.ZipFile(archive_path, "r") as archive:
        names = archive.namelist()
        duplicate_names = sorted(name for name in set(names) if names.count(name) > 1)
        allowed_shared_roots = {"15FLT"}
        top_levels = {name.split("/", 1)[0] for name in names if name}
        campaign_roots = sorted(root for root in top_levels if root not in allowed_shared_roots)
        package_name = next((name for name in names if name.endswith("/package.json")), "")
        campaign_name = next((name for name in names if name.endswith("/campaign.json")), "")
        inventory_name = next((name for name in names if name.endswith("/inventory.json")), "")
        for name in duplicate_names:
            errors.append(f"duplicate ZIP entry: {name}")
        if len(campaign_roots) != 1:
            errors.append("package must contain exactly one campaign top-level directory plus optional shared roots")
        for name in names:
            parts = name.rstrip("/").split("/")
            if "\\" in name or ":" in name or any(part in {"", ".", ".."} for part in parts):
                errors.append(f"unsafe ZIP path: {name}")
                continue
            if parts[0] not in campaign_roots and parts[0] not in allowed_shared_roots:
                errors.append(f"unsupported ZIP top-level directory: {name}")
        if not package_name:
            errors.append("missing package.json")
        if not campaign_name:
            errors.append("missing campaign.json")
        if not inventory_name:
            errors.append("missing inventory.json")
        package_manifest = json.loads(archive.read(package_name).decode("utf-8")) if package_name else {}
        campaign_manifest = json.loads(archive.read(campaign_name).decode("utf-8")) if campaign_name else {}
        inventory = json.loads(archive.read(inventory_name).decode("utf-8")) if inventory_name else {}
        prefix = package_manifest.get("campaign_prefix") or (campaign_name.split("/", 1)[0] if campaign_name else "")
        if package_manifest and package_manifest.get("format") != "F15SE2_CAMPAIGN_PACKAGE":
            errors.append("package.json has unsupported format")
        if campaign_manifest and campaign_manifest.get("format") != "F15SE2_CAMPAIGN":
            errors.append("campaign.json has unsupported format")
        if inventory and inventory.get("format") != "F15SE2_CAMPAIGN_INVENTORY":
            errors.append("inventory.json has unsupported format")
        if prefix and campaign_roots and prefix != campaign_roots[0]:
            errors.append(f"package prefix {prefix} does not match campaign directory {campaign_roots[0]}")
        if campaign_manifest:
            objective_ids = {
                str(item.get("id") or "")
                for item in campaign_manifest.get("mission_objectives") or []
                if isinstance(item, dict)
            }
            sortie_ids = {
                str(item.get("id") or "")
                for item in campaign_manifest.get("sortie_sequence") or []
                if isinstance(item, dict)
            }
            route_plan = campaign_manifest.get("route_plan") or {}
            waypoint_ids = {
                str(item.get("id") or "")
                for item in route_plan.get("waypoints") or []
                if isinstance(item, dict)
            }
            for index, waypoint in enumerate(route_plan.get("waypoints") or []):
                if not isinstance(waypoint, dict):
                    errors.append(f"route_plan waypoint is not an object: {index}")
                    continue
                objective_id = str(waypoint.get("objective_id") or "")
                if objective_id and objective_id not in objective_ids:
                    errors.append(f"route_plan waypoint references unknown objective id: {objective_id}")
                for coord_key in ("x_coord", "y_coord"):
                    try:
                        coord = int(waypoint.get(coord_key))
                    except (TypeError, ValueError):
                        errors.append(f"route_plan waypoint missing numeric {coord_key}: {waypoint.get('id', index)}")
                        continue
                    if coord < 0 or coord >= 32768:
                        errors.append(f"route_plan waypoint {coord_key} out of range: {waypoint.get('id', index)}")
            for index, route in enumerate(route_plan.get("sortie_routes") or []):
                if not isinstance(route, dict):
                    errors.append(f"route_plan route is not an object: {index}")
                    continue
                sortie_id = str(route.get("sortie_id") or "")
                if sortie_id and sortie_id not in sortie_ids:
                    errors.append(f"route_plan route references unknown sortie id: {sortie_id}")
                legs = route.get("route") or []
                if not isinstance(legs, list) or len(legs) < 2:
                    errors.append(f"route_plan route has fewer than two waypoints: {sortie_id or index}")
                for waypoint_id in legs if isinstance(legs, list) else []:
                    if str(waypoint_id) not in waypoint_ids:
                        errors.append(f"route_plan route references unknown waypoint id: {waypoint_id}")
            world_name = str(campaign_manifest.get("world") or "")
            world_entry = f"{prefix}/{world_name}" if prefix and world_name else ""
            if route_plan and world_entry in names:
                world_payload = json.loads(archive.read(world_entry).decode("utf-8"))
                world_route_plan = world_payload.get("campaign_route_plan") or {}
                world_waypoints = {
                    str(item.get("id") or ""): item
                    for item in world_route_plan.get("waypoints") or []
                    if isinstance(item, dict)
                }
                for waypoint in route_plan.get("waypoints") or []:
                    if not isinstance(waypoint, dict):
                        continue
                    waypoint_id = str(waypoint.get("id") or "")
                    world_waypoint = world_waypoints.get(waypoint_id)
                    if not world_waypoint:
                        errors.append(f"route_plan waypoint missing from WLD campaign_route_plan: {waypoint_id}")
                        continue
                    for coord_key in ("x_coord", "y_coord"):
                        if waypoint.get(coord_key) != world_waypoint.get(coord_key):
                            errors.append(f"route_plan waypoint {waypoint_id} {coord_key} differs from WLD campaign_route_plan")
        expected_entries = {package_name, inventory_name}
        for index, item in enumerate(inventory.get("files", [])):
            rel = str(item.get("file") or "").strip()
            if not rel:
                continue
            entry_name = f"{prefix}/{rel}"
            expected_entries.add(entry_name)
            if entry_name not in names:
                errors.append(f"inventory file missing from ZIP: {entry_name}")
                continue
            data = archive.read(entry_name)
            if item.get("sha256") and hashlib.sha256(data).hexdigest() != item.get("sha256"):
                errors.append(f"inventory sha256 mismatch: {entry_name}")
            if item.get("bytes") is not None and int(item.get("bytes")) != len(data):
                errors.append(f"inventory byte size mismatch: {entry_name}")
            if rel == "run_campaign.sh":
                text = data.decode("utf-8", errors="replace")
                info = archive.getinfo(entry_name)
                mode = (info.external_attr >> 16) & 0o777
                if "--campaign" not in text or "F15_REPLACEMENT_ROOT" not in text:
                    errors.append(f"campaign launcher missing expected launch command: {entry_name}")
                if mode and (mode & 0o111) == 0:
                    errors.append(f"campaign launcher is not executable in ZIP metadata: {entry_name}")
        if package_manifest.get("includes_external_assets"):
            for item in package_manifest.get("external_assets") or []:
                rel = str(item.get("file") or "").strip()
                if rel and rel not in names:
                    errors.append(f"external asset missing from ZIP: {rel}")
                if rel:
                    expected_entries.add(rel)
        for name in names:
            if name.endswith("/"):
                continue
            if name not in expected_entries:
                errors.append(f"ZIP entry is not declared by package/inventory metadata: {name}")
    return {
        "package": package_manifest,
        "campaign": campaign_manifest,
        "inventory": inventory,
        "entries": names,
        "errors": errors,
        "prefix": prefix,
    }


def cmd_install_campaign_package(args: argparse.Namespace) -> int:
    archive_path = Path(args.archive)
    output_root = _converted_assets_root(Path(args.output))
    if not archive_path.exists() or not archive_path.is_file():
        raise ValueError(f"campaign package does not exist: {archive_path}")
    inspection = _inspect_campaign_archive(archive_path)
    if inspection["errors"]:
        for error in inspection["errors"]:
            print(error, file=sys.stderr)
        return 1
    output_root.mkdir(parents=True, exist_ok=True)
    top_level = str(inspection.get("prefix") or "")
    if not top_level:
        raise ValueError(f"campaign package has no campaign prefix: {archive_path}")
    with zipfile.ZipFile(archive_path, "r") as archive:
        names = list(inspection["entries"])
        # Check existing parents before replacing anything: extractall follows
        # filesystem symlinks even when the ZIP member paths are relative.
        for name in names:
            target = output_root
            for part in name.rstrip("/").split("/"):
                target = target / part
                if target.is_symlink():
                    raise ValueError(f"refusing to install through symlink: {target}")
        install_dir = output_root / top_level
        planned_shared = []
        allowed_shared_roots = {"15FLT"}
        for name in names:
            parts = Path(name).parts
            if parts and parts[0] in allowed_shared_roots:
                target = output_root / name
                planned_shared.append(str(target))
                if target.exists() and not args.replace and not args.dry_run:
                    raise ValueError(f"shared asset already exists: {target}; pass --replace to overwrite it")
        if args.dry_run:
            print(f"would install campaign package: {archive_path} -> {install_dir}")
            if install_dir.exists():
                print(f"would replace campaign directory: {install_dir}" if args.replace else f"campaign directory exists: {install_dir}")
            for target in planned_shared:
                target_path = Path(target)
                if target_path.exists():
                    print(f"would replace shared asset: {target}" if args.replace else f"shared asset exists: {target}")
                else:
                    print(f"would install shared asset: {target}")
            return 0
        if install_dir.exists():
            if not args.replace:
                raise ValueError(f"campaign already exists: {install_dir}; pass --replace to overwrite it")
            shutil.rmtree(install_dir)
        archive.extractall(output_root)
    _make_campaign_launcher_executable(output_root / top_level)
    manifest_path = output_root / top_level / "campaign.json"
    if not manifest_path.exists():
        raise ValueError(f"installed package is missing campaign.json: {manifest_path}")
    package_path = output_root / top_level / "package.json"
    if package_path.exists():
        package_manifest = json.loads(package_path.read_text(encoding="utf-8"))
        if package_manifest.get("format") != "F15SE2_CAMPAIGN_PACKAGE":
            raise ValueError(f"invalid campaign package manifest: {package_path}")
        if package_manifest.get("campaign_prefix") and package_manifest.get("campaign_prefix") != top_level:
            raise ValueError(f"campaign package prefix mismatch: {package_path}")
    errors = _validate_campaign_manifest(manifest_path, output_root)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for item in manifest.get("installed_aircraft") or []:
        rel = str(item.get("file") or "").strip()
        if rel and not (output_root / top_level / rel).exists() and not (output_root / rel).exists():
            print(f"{manifest_path}: installed_aircraft references missing shared asset after install: {rel}", file=sys.stderr)
            return 1
    print(f"installed campaign package: {archive_path} -> {output_root / top_level}")
    return 0


def cmd_inspect_campaign_package(args: argparse.Namespace) -> int:
    archive_path = Path(args.archive)
    if not archive_path.exists() or not archive_path.is_file():
        raise ValueError(f"campaign package does not exist: {archive_path}")
    inspection = _inspect_campaign_archive(archive_path)
    package_manifest = inspection["package"]
    campaign_manifest = inspection["campaign"]
    inventory = inspection["inventory"]
    names = inspection["entries"]
    errors = inspection["errors"]
    if args.json:
        route_plan = campaign_manifest.get("route_plan") or {}
        target_sets = campaign_manifest.get("mission_target_sets") or []
        verification_status = campaign_manifest.get("verification_status") or {}
        _write_json(
            Path(args.json),
            {
                "archive": str(archive_path),
                "package": package_manifest,
                "campaign": campaign_manifest,
                "verification_summary": {
                    "runtime_launch_validation": verification_status.get("runtime_launch_validation", "unknown"),
                    "replacement_loadability_validation": verification_status.get("replacement_loadability_validation", "unknown"),
                    "full_asset_replacement_validation": verification_status.get("full_asset_replacement_validation", "unknown"),
                    "runtime_launch_recorded_at": (verification_status.get("runtime_launch_validation_evidence") or {}).get("recorded_at"),
                    "replacement_loadability_recorded_at": (verification_status.get("replacement_loadability_validation_evidence") or {}).get("recorded_at"),
                    "full_asset_replacement_recorded_at": (verification_status.get("full_asset_replacement_validation_evidence") or {}).get("recorded_at"),
                },
                "route_summary": {
                    "waypoints": len(route_plan.get("waypoints") or []),
                    "sortie_routes": len(route_plan.get("sortie_routes") or []),
                },
                "target_summary": {
                    "mission_target_sets": len(target_sets),
                },
                "inventory": inventory,
                "entries": names,
                "errors": errors,
            },
            pretty=args.pretty,
            media_sidecar=False,
        )
    else:
        print(f"archive: {archive_path}")
        print(f"campaign: {campaign_manifest.get('id', '?')} - {campaign_manifest.get('title', '')}")
        print(f"package format: {package_manifest.get('format', 'missing')}")
        print(f"includes external assets: {package_manifest.get('includes_external_assets', False)}")
        verification_status = campaign_manifest.get("verification_status") or {}
        print(f"runtime launch validation: {verification_status.get('runtime_launch_validation', 'unknown')}")
        print(f"replacement loadability validation: {verification_status.get('replacement_loadability_validation', 'unknown')}")
        print(f"full asset replacement validation: {verification_status.get('full_asset_replacement_validation', 'unknown')}")
        route_plan = campaign_manifest.get("route_plan") or {}
        if route_plan:
            print(f"route waypoints: {len(route_plan.get('waypoints') or [])}")
            print(f"sortie routes: {len(route_plan.get('sortie_routes') or [])}")
        target_sets = campaign_manifest.get("mission_target_sets") or []
        if target_sets:
            print(f"mission target sets: {len(target_sets)}")
        print(f"inventory files: {len(inventory.get('files', []))}")
        print(f"zip entries: {len(names)}")
        if errors:
            print("errors:")
            for error in errors:
                print(f"- {error}")
    return 0 if not errors else 1


def cmd_verify_campaign_package(args: argparse.Namespace) -> int:
    verify_args = argparse.Namespace(
        archive=args.archive,
        json="",
        pretty=False,
    )
    return cmd_inspect_campaign_package(verify_args)




def cmd_validate_replacements(args: argparse.Namespace) -> int:
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        if not args.loadability_only:
            raise ValueError(f"input must be an existing directory: {args.input}")
        if args.require_all:
            raise ValueError("--require-all needs an existing original input directory; omit it for source-free --loadability-only checks")
        print(
            f"warning: original input directory is missing: {args.input}; running source-free loadability checks only",
            file=sys.stderr,
        )
    if output_root.exists() and output_root.is_dir() and output_root.name != "converted_assets_all" and (output_root / "converted_assets_all").is_dir():
        output_root = output_root / "converted_assets_all"
        print(f"using converted output directory: {output_root}", file=sys.stderr)
    if not output_root.exists() or not output_root.is_dir():
        raise ValueError(f"output must be an existing directory: {args.output}")
    if args.loadability_only and args.strict_original_proof:
        raise ValueError("--loadability-only and --strict-original-proof are incompatible validation modes")
    if args.loadability_only and args.require_source_proof:
        raise ValueError("--loadability-only and --require-source-proof are incompatible validation modes")
    if args.loadability_only and args.allow_custom_glb_differences:
        raise ValueError("--loadability-only already allows custom GLB content differences; do not combine it with --allow-custom-glb-differences")

    checked = 0
    failed = 0
    pic_checked = 0
    checked_png_paths: set[Path] = set()
    if input_root.exists():
        for src in _iter_asset_paths(input_root, recursive=args.recursive):
            fmt = _detect_format(src)
            if fmt != "PIC":
                continue
            relative = src.relative_to(input_root)
            png_path = output_root / relative.with_suffix(".png")
            if not png_path.exists():
                if args.require_all:
                    print(f"missing replacement PNG: {png_path}", file=sys.stderr)
                    failed += 1
                continue
            checked += 1
            pic_checked += 1
            checked_png_paths.add(png_path.resolve())
            try:
                errors = validate_pic_png_replacement(src, png_path, _read_binary, args.loadability_only)
            except Exception as exc:
                errors = [f"{src}: {exc}"]
            if errors:
                failed += 1
                for error in errors:
                    print(error, file=sys.stderr)
    if args.loadability_only:
        for png_path in sorted(output_root.rglob("*.png")):
            if png_path.resolve() in checked_png_paths:
                continue
            if "fonts" in {part.lower() for part in png_path.relative_to(output_root).parts[:-1]}:
                continue
            checked += 1
            pic_checked += 1
            errors = validate_png_replacement_loadability(png_path)
            if errors:
                failed += 1
                for error in errors:
                    print(error, file=sys.stderr)

    sound_checked, sound_failed = validate_sound_replacements(input_root, output_root, args.require_all, args.loadability_only)
    checked += sound_checked
    failed += sound_failed
    font_checked, font_failed = validate_font_replacements(Path(args.repo_root), output_root, args.require_all, args.loadability_only)
    checked += font_checked
    failed += font_failed
    structured_checked, structured_failed = validate_structured_json_replacements(
        input_root,
        output_root,
        args.recursive,
        args.require_all,
        _iter_asset_paths,
        _detect_format,
        _structured_json_path,
        args.loadability_only,
    )
    checked += structured_checked
    failed += structured_failed
    glb_checked, glb_failed = validate_3d3_glb_replacements(
        input_root,
        output_root,
        args.recursive,
        args.require_all,
        _iter_asset_paths,
        _detect_format,
        _asset_output_dir,
        _load_shape_names_for_3d3,
        args.require_generated_cache or args.strict_original_proof,
        args.require_source_proof or args.strict_original_proof,
        args.allow_custom_glb_differences and not args.strict_original_proof,
        args.loadability_only,
    )
    checked += glb_checked
    failed += glb_failed
    campaign_checked, campaign_failed = validate_campaign_manifests(output_root)
    checked += campaign_checked
    failed += campaign_failed

    if args.loadability_only and checked == 0:
        print(
            f"warning: no replacement files were checked under {output_root}; expected PNG, WAV, BDF, WLD/3DT/3DG JSON, 3D3 JSON, GLB, GLMESH, or campaign.json replacements",
            file=sys.stderr,
        )

    print(
        f"validated replacements: PIC/SPR PNG={pic_checked}, "
        f"sound WAV={sound_checked}, font BDF/PNG={font_checked}, "
        f"WLD/3DT/3DG JSON={structured_checked}, 3D3 JSON/GLB/GLMESH={glb_checked}, "
        f"campaign manifests={campaign_checked}; failures={failed}"
    )
    return 0 if failed == 0 else 1


def _repo_root_from_tool() -> Path:
    return Path(__file__).resolve().parents[2]


def cmd_convert_all(args: argparse.Namespace) -> int:
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        raise ValueError(f"input must be an existing directory: {args.input}")

    output_root.mkdir(parents=True, exist_ok=True)

    convert_args = argparse.Namespace(
        input=str(input_root),
        output=str(output_root),
        recursive=True,
        models="glb",
        no_png=False,
        include_image_json=getattr(args, "include_image_json", False),
        include_3d3_model_data=getattr(args, "include_3d3_model_data", True),
        include_glmesh_cache=getattr(args, "include_glmesh_cache", False),
        continue_on_error=False,
        pretty=args.pretty,
    )
    result = cmd_convert_tree(convert_args)
    if result != 0:
        return result

    repo_root = _repo_root_from_tool()
    font_args = argparse.Namespace(
        repo_root=str(repo_root),
        output=str(output_root / "fonts"),
        no_bdf=False,
        include_metadata=getattr(args, "include_metadata", False),
        pretty=args.pretty,
    )
    result = cmd_export_fonts(font_args)
    if result != 0:
        return result

    sound_args = argparse.Namespace(
        input=str(input_root),
        output=str(output_root / "sounds"),
        sample_rate=DEFAULT_SAMPLE_RATE,
        include_raw_blob=getattr(args, "include_raw_blob", False),
        include_metadata=getattr(args, "include_metadata", False),
        pretty=args.pretty,
    )
    return cmd_export_sounds(sound_args)


def cmd_build_soviet_vietnam_pack(args: argparse.Namespace) -> int:
    output_root = Path(args.output)
    convert_args = argparse.Namespace(
        input=args.input,
        output=args.output,
        include_image_json=False,
        include_3d3_model_data=False,
        include_glmesh_cache=False,
        include_metadata=False,
        include_raw_blob=False,
        pretty=args.pretty,
    )
    result = cmd_convert_all(convert_args)
    if result != 0:
        return result

    candidates = [
        output_root / "VN" / "VN.WLD.json",
        output_root / "VN.WLD.json",
    ]
    try:
        candidates.extend(sorted(output_root.rglob("VN.WLD.json")))
    except OSError:
        pass
    template = next((path for path in candidates if path.exists()), None)
    if template is None:
        raise ValueError(f"converted VN.WLD.json was not found under {output_root}")
    template_payload = json.loads(template.read_text(encoding="utf-8"))
    campaign_id = str(_apply_soviet_vietnam_campaign(template_payload).get("campaign", {}).get("id") or "SVN")

    campaign_args = argparse.Namespace(
        template=str(template),
        output=str(output_root),
        kind="soviet-vietnam",
        base_theater=args.base_theater or "VN",
        font=args.font,
        aircraft_glb=args.aircraft_glb,
        radio_dir=args.radio_dir,
        radio_tts_command=args.radio_tts_command,
        no_starter_art=args.no_starter_art,
        no_starter_radio=args.no_starter_radio,
        no_starter_music=args.no_starter_music,
        no_starter_aircraft=args.no_starter_aircraft,
        pretty=args.pretty,
    )
    result = cmd_new_campaign(campaign_args)
    if result != 0:
        return result

    # Free placeholders are additive: custom art and locally supplied media win.
    from f15assets.free_pack import generate_missing_free_assets

    generate_missing_free_assets(output_root / campaign_id)
    cmd_refresh_campaign_inventory(
        argparse.Namespace(manifest=str(output_root / campaign_id), pretty=args.pretty)
    )

    if args.package:
        package_args = argparse.Namespace(
            campaign=str(output_root / campaign_id),
            output=args.package_output or str(output_root / f"{campaign_id}.zip"),
            allow_external_assets=False,
            include_external_assets=False,
            skip_validation=args.skip_package_validation,
            pretty=args.pretty,
        )
        result = cmd_package_campaign(package_args)
        if result != 0:
            return result

    if args.validate:
        validate_args = argparse.Namespace(
            input=args.input,
            output=args.output,
            repo_root=".",
            recursive=True,
            require_all=False,
            loadability_only=True,
            require_generated_cache=False,
            require_source_proof=False,
            strict_original_proof=False,
            allow_custom_glb_differences=False,
        )
        return cmd_validate_replacements(validate_args)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="F-15 asset converter (forward-only: game binary -> modern editable outputs)"
    )
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output")
    sub = parser.add_subparsers(dest="command", required=True)

    convert_all = sub.add_parser(
        "convert-all",
        help="convert game assets, in-repo fonts, and digitized sounds with default settings",
    )
    convert_all.add_argument("input", help="Installed game folder containing original assets")
    convert_all.add_argument("output", help="Output folder for all converted assets")
    convert_all.add_argument(
        "--include-image-json",
        action="store_true",
        help="Also write bulky PIC/SPR decode JSON; default output uses PNG as the image source",
    )
    convert_all.add_argument(
        "--include-3d3-model-data",
        action="store_true",
        default=True,
        help="Write .3D3 model_data base64 into JSON so modern-only packs can rebuild shape tables (default)",
    )
    convert_all.add_argument(
        "--minimize-3d3-json",
        dest="include_3d3_model_data",
        action="store_false",
        help="Omit bulky .3D3 model_data; requires original .3D3 files at runtime for shape tables",
    )
    convert_all.add_argument(
        "--include-glmesh-cache",
        action="store_true",
        help="Also pre-generate cache/*.glmesh runtime meshes; default output relies on runtime rebuild from GLB",
    )
    convert_all.add_argument(
        "--include-metadata",
        action="store_true",
        help="Also export optional font JSON, sound cue JSON, driver sidecars, and index metadata",
    )
    convert_all.add_argument(
        "--include-raw-blob",
        action="store_true",
        help="Also export f15dgtl_raw.wav/f15dgtl_raw.json for reverse-engineering reference",
    )
    convert_all.set_defaults(func=cmd_convert_all)

    build_svn = sub.add_parser(
        "build-soviet-vietnam-pack",
        help="one-command conversion plus generated modern SVN custom campaign pack",
    )
    build_svn.add_argument("input", help="Installed game folder containing original assets")
    build_svn.add_argument("output", help="Output converted_assets_all/custom asset root with modern PNG/WAV/TTF/GLB/WLD JSON campaign files")
    build_svn.add_argument("--base-theater", default="VN", help="Legacy WLD stem this scenario replaces")
    build_svn.add_argument("--font", default="", help="Optional Cyrillic-capable .ttf/.otf to copy into campaign font override slots")
    build_svn.add_argument("--aircraft-glb", action="append", default=[], metavar="SLOT=PATH", help="Copy a custom aircraft GLB into the campaign-local 15FLT shape slot; may be repeated")
    build_svn.add_argument("--radio-dir", default="", help="Directory containing recorded/TTS voice_cue_*.wav files to install over generated radio placeholders")
    build_svn.add_argument("--radio-tts-command", default="", help="Command template to generate each Russian radio WAV; placeholders: {text}, {output}, {cue}, {script}")
    build_svn.add_argument("--no-starter-art", action="store_true", help="Do not generate campaign-local high-resolution PNG starter art")
    build_svn.add_argument("--no-starter-radio", action="store_true", help="Do not generate campaign-local generated radio-style WAV cues")
    build_svn.add_argument("--no-starter-music", action="store_true", help="Do not generate campaign-local ASOUND intro music JSON")
    build_svn.add_argument("--no-starter-aircraft", action="store_true", help="Do not generate campaign-local procedural GLB aircraft placeholders")
    build_svn.add_argument("--package", action="store_true", help="Also create the generated SVN.zip campaign package")
    build_svn.add_argument("--package-output", default="", help="Output ZIP path when --package is used; defaults to OUTPUT/SVN.zip")
    build_svn.add_argument("--validate-package", action="store_true", help="Deprecated compatibility flag; campaign package validation now runs by default when --package is used")
    build_svn.add_argument("--skip-package-validation", action="store_true", help="Create a draft SVN.zip without campaign package validation")
    build_svn.add_argument("--validate", action="store_true", help="Run source-free loadability validation after generating the pack")
    build_svn.add_argument(
        "--pretty",
        action="store_true",
        default=argparse.SUPPRESS,
        help="Pretty-print generated JSON; accepted here as a convenience alias for global --pretty",
    )
    build_svn.set_defaults(func=cmd_build_soviet_vietnam_pack)

    decode = sub.add_parser(
        "decode",
        help="decode one game asset to full diagnostic JSON and optional media files",
    )
    decode.add_argument("input", help="Input binary asset")
    decode.add_argument("json", help="Output JSON sidecar")
    decode.add_argument(
        "--format",
        choices=["PIC", "PIC".lower(), "3D3", "3d3", "3DT", "3dt", "3DG", "3dg", "WLD", "wld"],
        help="Force input format",
    )
    decode.add_argument(
        "--png",
        help="Optional indexed PNG output for PIC/SPR (unsupported for other formats)",
    )
    decode.add_argument(
        "--gltf",
        help="Optional .gltf or .glb output for 3D3 models",
    )
    decode.set_defaults(func=cmd_decode)

    convert = sub.add_parser(
        "convert-tree",
        help="convert supported assets in one folder; use convert-all for recursive default conversion",
    )
    convert.add_argument("input", help="Input folder containing game assets")
    convert.add_argument("output", help="Output folder for converted assets")
    convert.add_argument("--recursive", action="store_true", help="Recurse into subdirectories")
    convert.add_argument(
        "--models",
        choices=["gltf", "glb", "none"],
        default="glb",
        help="Output format for 3D3 models (`glb` is self-contained by default)",
    )
    convert.add_argument("--no-png", action="store_true", help="Skip PNG output for PIC/SPR")
    convert.add_argument(
        "--include-image-json",
        action="store_true",
        help="Also write bulky PIC/SPR decode JSON; by default PNG is the image replacement source",
    )
    convert.add_argument(
        "--include-3d3-model-data",
        action="store_true",
        help="Also write bulky .3D3 model_data base64 into JSON; by default GLB is the geometry source",
    )
    convert.add_argument(
        "--include-glmesh-cache",
        action="store_true",
        help="Also pre-generate cache/*.glmesh runtime meshes; default output relies on runtime rebuild from GLB",
    )
    convert.add_argument(
        "--continue-on-error",
        action="store_true",
        help="Continue conversion when a file fails",
    )
    convert.set_defaults(func=cmd_convert_tree)

    fonts = sub.add_parser(
        "export-fonts",
        help="export extracted in-repo font tables to BDF; metadata is optional",
    )
    fonts.add_argument("repo_root", help="Repository root containing src/fontdata.h and src/gfx_impl.c")
    fonts.add_argument("output", help="Output folder for BDF font assets")
    fonts.add_argument("--no-bdf", action="store_true", help="Skip BDF font files")
    fonts.add_argument(
        "--include-metadata",
        action="store_true",
        help="Also write font_<id>.json sidecars and fonts.json; default output is BDF only",
    )
    fonts.set_defaults(func=cmd_export_fonts)

    sounds = sub.add_parser(
        "export-sounds",
        help="export digitized cue WAVs; optional metadata sidecars are opt-in",
    )
    sounds.add_argument("input", help="Installed game folder containing F15DGTL.BIN and sound drivers")
    sounds.add_argument("output", help="Output folder for sound assets")
    sounds.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help="Sample rate for exported unsigned 8-bit PCM cue WAVs",
    )
    sounds.add_argument(
        "--include-raw-blob",
        action="store_true",
        help="Also export f15dgtl_raw.wav/f15dgtl_raw.json for reverse-engineering reference",
    )
    sounds.add_argument(
        "--include-metadata",
        action="store_true",
        help="Also export cue JSON, driver sidecars, and sounds.json metadata for reverse engineering",
    )
    sounds.set_defaults(func=cmd_export_sounds)

    build_binary = sub.add_parser(
        "build-binary",
        help="rebuild runtime binary bytes from structured replacement JSON",
    )
    build_binary.add_argument("json", help="Input 3D3/WLD/3DT/3DG JSON replacement")
    build_binary.add_argument(
        "--format",
        choices=["3D3", "3d3", "WLD", "wld", "3DT", "3dt", "3DG", "3dg"],
        help="Force JSON format instead of reading payload.format",
    )
    build_binary.set_defaults(func=cmd_build_binary)

    new_campaign = sub.add_parser(
        "new-campaign",
        help="scaffold a custom modern-format campaign from a WLD JSON template",
    )
    new_campaign.add_argument(
        "--kind",
        default="soviet-vietnam",
        choices=["soviet-vietnam"],
        help="Campaign scaffold to generate",
    )
    new_campaign.add_argument("template", help="Template WLD JSON, for example converted_assets_all/VN/VN.WLD.json")
    new_campaign.add_argument("output", help="Converted/custom asset output root with editable PNG/WAV/TTF/GLB/WLD JSON campaign files")
    new_campaign.add_argument("--base-theater", default="", help="Legacy WLD stem this scenario replaces; defaults to the template stem")
    new_campaign.add_argument("--font", default="", help="Optional Cyrillic-capable .ttf/.otf to copy into campaign font_1/font_3/font_4 override slots")
    new_campaign.add_argument("--aircraft-glb", action="append", default=[], metavar="SLOT=PATH", help="Copy a custom aircraft GLB into the campaign-local 15FLT shape slot; may be repeated")
    new_campaign.add_argument("--radio-dir", default="", help="Directory containing recorded/TTS voice_cue_*.wav files to install over generated radio placeholders")
    new_campaign.add_argument("--radio-tts-command", default="", help="Command template to generate each Russian radio WAV; placeholders: {text}, {output}, {cue}, {script}")
    new_campaign.add_argument("--no-starter-art", action="store_true", help="Do not generate campaign-local high-resolution PNG starter art")
    new_campaign.add_argument("--no-starter-radio", action="store_true", help="Do not generate campaign-local generated radio-style WAV cues")
    new_campaign.add_argument("--no-starter-music", action="store_true", help="Do not generate campaign-local ASOUND intro music JSON")
    new_campaign.add_argument("--no-starter-aircraft", action="store_true", help="Do not generate campaign-local procedural GLB aircraft placeholders")
    new_campaign.add_argument(
        "--pretty",
        action="store_true",
        default=argparse.SUPPRESS,
        help="Pretty-print generated campaign JSON; accepted here as a convenience alias for global --pretty",
    )
    new_campaign.set_defaults(func=cmd_new_campaign)

    install_aircraft_glb = sub.add_parser(
        "install-aircraft-glb",
        help="install a custom aircraft GLB into converted_assets_all/15FLT/shape_###*.glb and clear stale caches",
    )
    install_aircraft_glb.add_argument("glb", help="Custom .glb model to install")
    install_aircraft_glb.add_argument("output", help="converted_assets_all directory, or a parent containing it")
    install_aircraft_glb.add_argument("--slot", type=int, required=True, help="15FLT shape slot number to replace")
    install_aircraft_glb.add_argument("--label", default="", help="Human-readable filename label when the slot does not already exist")
    install_aircraft_glb.add_argument("--container", default="15FLT", help="3D container directory; default is 15FLT for aircraft")
    install_aircraft_glb.add_argument("--skip-validate", action="store_true", help="Copy without proving GLB can be converted to GLMESH")
    install_aircraft_glb.set_defaults(func=cmd_install_aircraft_glb)

    build_glmesh = sub.add_parser(
        "build-glmesh",
        help="expand a replacement GLB into a simple runtime mesh stream for the OpenGL backend",
    )
    build_glmesh.add_argument("glb", help="Input per-shape GLB")
    build_glmesh.set_defaults(func=cmd_build_glmesh)

    list_campaigns = sub.add_parser(
        "list-campaigns",
        help="list modern custom campaign manifests in a converted asset root",
    )
    list_campaigns.add_argument("output", help="converted_assets_all directory, or a parent containing it")
    list_campaigns.add_argument("--json", default="", help="Optional output JSON file containing discovered campaign manifests")
    list_campaigns.set_defaults(func=cmd_list_campaigns)

    refresh_inventory = sub.add_parser(
        "refresh-campaign-inventory",
        help="rebuild inventory.json hashes for a generated campaign pack",
    )
    refresh_inventory.add_argument("manifest", help="Path to campaign.json or its containing campaign directory")
    refresh_inventory.add_argument(
        "--pretty",
        action="store_true",
        default=argparse.SUPPRESS,
        help="Pretty-print refreshed campaign JSON; accepted here as a convenience alias for global --pretty",
    )
    refresh_inventory.set_defaults(func=cmd_refresh_campaign_inventory)

    record_verification = sub.add_parser(
        "record-campaign-verification",
        help="record external runtime/build validation evidence in campaign.json",
    )
    record_verification.add_argument("manifest", help="Path to campaign.json or its containing campaign directory")
    record_verification.add_argument(
        "--runtime-launch",
        choices=["passed", "failed", "not_applicable"],
        default="",
        help="Record whether a game runtime launch was externally validated",
    )
    record_verification.add_argument(
        "--runtime-launch-command",
        default="",
        help="Command used for runtime launch validation",
    )
    record_verification.add_argument(
        "--replacement-loadability",
        choices=["passed", "failed", "not_applicable"],
        default="",
        help="Record whether source-free replacement loadability validation was externally run",
    )
    record_verification.add_argument(
        "--replacement-loadability-command",
        default="",
        help="Command used for replacement loadability validation",
    )
    record_verification.add_argument(
        "--full-asset-validation",
        choices=["passed", "failed", "not_applicable"],
        default="",
        help="Record whether full replacement validation was externally run",
    )
    record_verification.add_argument(
        "--full-asset-validation-command",
        default="",
        help="Command used for full replacement validation",
    )
    record_verification.add_argument("--notes", default="", help="Free-form validation note")
    record_verification.add_argument(
        "--pretty",
        action="store_true",
        default=argparse.SUPPRESS,
        help="Pretty-print refreshed campaign JSON; accepted here as a convenience alias for global --pretty",
    )
    record_verification.set_defaults(func=cmd_record_campaign_verification)

    package_campaign = sub.add_parser(
        "package-campaign",
        help="create a ZIP archive from a campaign inventory",
    )
    package_campaign.add_argument("campaign", help="Path to campaign.json or its containing campaign directory")
    package_campaign.add_argument("output", nargs="?", default="", help="Output .zip path; defaults beside the campaign directory")
    package_campaign.add_argument("--allow-external-assets", action="store_true", help="Create a campaign-only ZIP even if manifest references shared assets such as 15FLT aircraft GLBs")
    package_campaign.add_argument("--include-external-assets", action="store_true", help="Include manifest-referenced shared assets such as 15FLT aircraft GLBs in the ZIP")
    package_campaign.add_argument("--skip-validation", action="store_true", help="Package a draft campaign without running campaign manifest/media validation")
    package_campaign.add_argument(
        "--pretty",
        action="store_true",
        default=argparse.SUPPRESS,
        help="Pretty-print campaign JSON while packaging; accepted here as a convenience alias for global --pretty",
    )
    package_campaign.set_defaults(func=cmd_package_campaign)

    install_campaign = sub.add_parser(
        "install-campaign-package",
        help="install a packaged campaign ZIP into a converted asset root",
    )
    install_campaign.add_argument("archive", help="Campaign ZIP created by package-campaign")
    install_campaign.add_argument("output", help="converted_assets_all directory, or a parent containing it")
    install_campaign.add_argument("--replace", action="store_true", help="Replace an existing installed campaign directory")
    install_campaign.add_argument("--dry-run", action="store_true", help="Print what would be installed without extracting files")
    install_campaign.set_defaults(func=cmd_install_campaign_package)

    inspect_campaign = sub.add_parser(
        "inspect-campaign-package",
        help="inspect a packaged campaign ZIP before installing it",
    )
    inspect_campaign.add_argument("archive", help="Campaign ZIP created by package-campaign")
    inspect_campaign.add_argument("--json", default="", help="Optional JSON output path for package/campaign/inventory metadata")
    inspect_campaign.set_defaults(func=cmd_inspect_campaign_package)

    verify_campaign = sub.add_parser(
        "verify-campaign-package",
        help="verify campaign package integrity and return success/failure",
    )
    verify_campaign.add_argument("archive", help="Campaign ZIP created by package-campaign")
    verify_campaign.set_defaults(func=cmd_verify_campaign_package)

    validate = sub.add_parser(
        "validate-replacements",
        help="compare original asset loads against modern replacements; use flags for strict proof or custom GLB workflows",
    )
    validate.add_argument("input", help="Installed game folder containing original assets")
    validate.add_argument("output", help="Converted asset folder")
    validate.add_argument(
        "--repo-root",
        default=".",
        help="Repository root for validating in-repo original font tables",
    )
    validate.add_argument(
        "--no-recursive",
        dest="recursive",
        action="store_false",
        default=True,
        help="Only validate assets directly under the input folder",
    )
    validate.add_argument(
        "--require-all",
        action="store_true",
        help="Fail if a supported original asset has no modern authoring replacement",
    )
    validate.add_argument(
        "--require-generated-cache",
        action="store_true",
        help="Also fail if generated 3D cache/*.glmesh runtime caches are missing",
    )
    validate.add_argument(
        "--require-source-proof",
        action="store_true",
        help="Fail if GLB/GLMESH source primitive metadata is missing or no longer matches the original export",
    )
    validate.add_argument(
        "--strict-original-proof",
        action="store_true",
        help="Strict converter-regression mode: require generated 3D caches and GLB/GLMESH source proof metadata",
    )
    validate.add_argument(
        "--allow-custom-glb-differences",
        action="store_true",
        help="Warn instead of failing when per-shape GLB geometry/source proof differs from the original .3D3 export",
    )
    validate.add_argument(
        "--loadability-only",
        action="store_true",
        help="Validate modern files parse and have required structure, but skip equality checks against original assets",
    )
    validate.set_defaults(func=cmd_validate_replacements)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:  # pragma: no cover
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
