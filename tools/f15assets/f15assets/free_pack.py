"""Generate redistributable placeholder art for a source-free campaign pack."""

from __future__ import annotations

import hashlib
import json
import shutil
import base64
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw


FREE_ASSET_FORMAT = "F15SE2_GENERATED_FREE_ASSETS"
LOGICAL_WIDTH = 320
LOGICAL_HEIGHT = 200


def _write_if_missing(path: Path, image: Image.Image) -> bool:
    """Write a generated image without replacing customizer-owned artwork."""
    if path.exists():
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG", optimize=True)
    return True


def _write_json_if_missing(path: Path, payload: dict[str, object]) -> bool:
    """Write generated runtime data without replacing campaign-owned files."""
    if path.exists():
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return True


def _empty_3d3(shape_count: int) -> dict[str, object]:
    """Create a valid shape table whose visible geometry is supplied by GLB files."""
    empty_model = bytes((0, 0, 0, 0, 0))
    return {
        "format": "3D3",
        "version": 1,
        "signature": 0x3333,
        "shape_offsets": [0] * shape_count,
        "model_data_size": len(empty_model),
        "model_data": base64.b64encode(empty_model).decode("ascii"),
        "shape_names": {},
        "trailing_bytes": "",
    }


def _flat_3dg() -> dict[str, object]:
    """Create a valid hierarchy that maps every terrain cell to flat tile zero."""
    return {
        "format": "3DG",
        "version": 1,
        "signature": 0x3232,
        "level4_top_grid": [0] * 16,
        "level3_grid": [0] * 256,
        "level2_subgrid": [0] * 512,
        "level1_subgrid": [0] * 512,
        "level0_subgrid": [0] * 512,
        "trailing_bytes": "",
    }


def _empty_3dt() -> dict[str, object]:
    """Create flat terrain with a generic target used by legacy mission selection."""
    levels = [
        {"level": level, "objects": [
            {
                "tile_index": tile,
                "objects": ([{"x": 0, "y": 0, "z": 0, "shape_word": 21}]
                            if tile == 0 and level in (1, 2) else []),
            }
            for tile in range(32)
        ]}
        for level in range(5)
    ]
    return {
        "format": "3DT",
        "version": 1,
        "signature": 0x3131,
        "tile_counts": [32] * 5,
        "levels": levels,
        "trailing_bytes": "",
    }


def _panel_art(title: str, accent: tuple[int, int, int]) -> Image.Image:
    """Create neutral menu art that leaves all labels to the game renderer."""
    width, height = 1280, 800
    image = Image.new("RGBA", (width, height), (12, 24, 29, 255))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        shade = int(18 + 32 * y / height)
        draw.line((0, y, width, y), fill=(shade // 2, shade, shade + 8, 255))
    draw.rectangle((56, 48, width - 56, height - 48), outline=accent + (255,), width=5)
    draw.rectangle((82, 74, width - 82, height - 74), outline=(88, 112, 116, 255), width=2)
    draw.text((106, 96), title, fill=accent + (210,))
    return image


def _flight_backdrop(label: str) -> Image.Image:
    """Create a functional transparent flight-page fallback on the 320x200 grid."""
    image = Image.new("RGBA", (LOGICAL_WIDTH, LOGICAL_HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 150, 319, 199), fill=(16, 25, 28, 255))
    draw.line((0, 150, 319, 150), fill=(96, 150, 147, 255), width=2)
    draw.text((8, 180), label, fill=(130, 190, 180, 255))
    return image


def _radar_symbol(kind: str) -> Image.Image:
    """Create a high-contrast transparent radar/HUD symbol."""
    size = 128
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    color = (232, 246, 225, 255)
    warning = (255, 190, 56, 255)
    if kind in {"self", "plane-level", "plane-high", "plane-low"}:
        draw.polygon(((64, 8), (75, 49), (116, 69), (75, 72), (69, 116),
                      (59, 116), (53, 72), (12, 69), (53, 49)), fill=color)
        if kind == "plane-high":
            draw.line((22, 18, 106, 18), fill=warning, width=7)
        elif kind == "plane-low":
            draw.line((22, 110, 106, 110), fill=warning, width=7)
    elif kind == "sam":
        draw.polygon(((64, 5), (78, 47), (70, 47), (70, 105), (58, 105),
                      (58, 47), (50, 47)), fill=warning)
    elif kind == "boat":
        draw.polygon(((20, 77), (108, 77), (91, 105), (37, 105)), fill=color)
        draw.rectangle((53, 48, 82, 77), fill=color)
    elif kind == "bullseye":
        draw.ellipse((16, 16, 112, 112), outline=warning, width=8)
        draw.ellipse((43, 43, 85, 85), outline=warning, width=7)
        draw.ellipse((59, 59, 69, 69), fill=warning)
    elif kind == "gun-reticle":
        draw.ellipse((18, 18, 110, 110), outline=color, width=4)
        draw.line((64, 0, 64, 38), fill=color, width=4)
        draw.line((64, 90, 64, 127), fill=color, width=4)
        draw.line((0, 64, 38, 64), fill=color, width=4)
        draw.line((90, 64, 127, 64), fill=color, width=4)
    else:
        draw.polygon(((64, 8), (116, 108), (64, 86), (12, 108)), outline=color, width=6)
    return image


def _generated_records(paths: Iterable[Path], root: Path) -> list[dict[str, object]]:
    """Describe generated files so release tooling can audit provenance."""
    records = []
    for path in sorted(paths):
        data = path.read_bytes()
        record = {
            "file": path.relative_to(root).as_posix(),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "license": "CC0-1.0",
            "generator": "tools/f15assets/f15assets/free_pack.py",
        }
        if record["file"] == "fonts/font_0.ttf":
            # Copying a supplied font does not change its license.
            record["license"] = "LicenseRef-Unknown"
            record["source_file"] = "fonts/font_1.ttf"
        records.append(record)
    return records


def generate_missing_free_assets(campaign_dir: Path) -> list[Path]:
    """Fill absent visual replacements while preserving every existing file."""
    campaign_dir = campaign_dir.resolve()
    manifest_path = campaign_dir / "free-assets.json"
    previous_manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {}
    if previous_manifest and previous_manifest.get("format") != FREE_ASSET_FORMAT:
        raise ValueError(f"unsupported free-asset manifest: {manifest_path}")
    created: list[Path] = []

    menu_assets = {
        "ADV.png": ("FLIGHT OPERATIONS", (105, 206, 187)),
        "LABS.png": ("INTELLIGENCE", (118, 202, 210)),
        "DEATH.png": ("MISSION LOST", (193, 75, 62)),
        "MEDAL.png": ("SERVICE RECORD", (218, 176, 67)),
        "PROMO.png": ("PROMOTION", (218, 176, 67)),
        "TITLE16.png": ("LI SI CIN", (224, 186, 70)),
    }
    for filename, (title, accent) in menu_assets.items():
        path = campaign_dir / filename
        if _write_if_missing(path, _panel_art(title, accent)):
            created.append(path)

    required_pages = (
        "1.png", "2.png", "3.png", "4.png", "WALL.png",
        "CEUROPE.png", "JP.png", "LIBYA.png", "ME.png", "NCAPE.png", "PERSIAN.png",
    )
    for filename in required_pages:
        path = campaign_dir / filename
        if _write_if_missing(path, _panel_art(filename.removesuffix(".png"), (105, 206, 187))):
            created.append(path)

    supplied_cockpit = campaign_dir / "COCKPIT.png"
    for filename in ("256LEFT.png", "256PIT.png", "256REAR.png", "256RIGHT.png",
                     "LEFT.png", "REAR.png", "RIGHT.png"):
        path = campaign_dir / filename
        # VGA gameplay requests 256PIT.PIC rather than COCKPIT.PIC. Reuse the
        # custom cockpit for that filename; the runtime retains source resolution
        # while maintaining its own indexed backing page for legacy save-unders.
        if filename == "256PIT.png" and supplied_cockpit.exists():
            image = Image.open(supplied_cockpit).convert("RGBA")
        else:
            image = _flight_backdrop(filename.removesuffix(".png"))
        if _write_if_missing(path, image):
            created.append(path)

    radar_dir = campaign_dir / "assets" / "flight" / "radar"
    supplied_aircraft = campaign_dir / "image.png"
    for symbol in ("self", "plane-level", "plane-high", "plane-low", "sam", "boat", "bullseye"):
        path = radar_dir / f"{symbol}.png"
        if supplied_aircraft.exists() and symbol in {"self", "plane-level"}:
            image = Image.open(supplied_aircraft).convert("RGBA")
        else:
            image = _radar_symbol(symbol)
        if _write_if_missing(path, image):
            created.append(path)

    hud_dir = campaign_dir / "assets" / "flight" / "hud"
    for symbol in ("gun-reticle", "aam-seeker"):
        path = hud_dir / f"{symbol}.png"
        if _write_if_missing(path, _radar_symbol(symbol)):
            created.append(path)

    font_zero = campaign_dir / "fonts" / "font_0.ttf"
    font_one = campaign_dir / "fonts" / "font_1.ttf"
    if not font_zero.exists() and font_one.exists():
        font_zero.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(font_one, font_zero)
        created.append(font_zero)

    for stem, shape_count in (("15FLT", 23), ("PHOTO", 13)):
        path = campaign_dir / stem / f"{stem}.3D3.json"
        if _write_json_if_missing(path, _empty_3d3(shape_count)):
            created.append(path)

    theater_stems = ("CE", "JP", "LB", "ME", "NC", "PG", "VN")
    for stem in theater_stems:
        directory = campaign_dir / stem
        for suffix, payload in (
            ("3D3", _empty_3d3(96)),
            ("3DG", _flat_3dg()),
            ("3DT", _empty_3dt()),
        ):
            path = directory / f"{stem}.{suffix}.json"
            if _write_json_if_missing(path, payload):
                created.append(path)

    scenario_path = campaign_dir / "SVN.WLD.json"
    if scenario_path.exists():
        scenario = json.loads(scenario_path.read_text(encoding="utf-8"))
        for stem in ("CE", "GULF", "JP", "LIBYA", "ME", "NC", "PG"):
            path = campaign_dir / f"{stem}.WLD.json"
            if _write_json_if_missing(path, scenario):
                created.append(path)

    records = {record["file"]: record for record in previous_manifest.get("generated", [])}
    records.update({record["file"]: record for record in _generated_records(created, campaign_dir)})
    copied_font = records.get("fonts/font_0.ttf")
    if copied_font and copied_font.get("generator") == "tools/f15assets/f15assets/free_pack.py":
        copied_font["license"] = "LicenseRef-Unknown"
        copied_font["source_file"] = "fonts/font_1.ttf"
    payload = dict(previous_manifest)
    payload.update({
        "format": FREE_ASSET_FORMAT,
        "format_version": 1,
        "policy": "Existing files are customizer-owned and are never overwritten.",
        "cockpit": "COCKPIT.png is intentionally supplied separately by the customizer.",
        "generated": [records[name] for name in sorted(records)],
    })
    manifest_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return created
