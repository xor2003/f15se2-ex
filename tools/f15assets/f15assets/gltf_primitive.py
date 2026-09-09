"""Validate the geometry/material subset representable by the flat GLMESH cache."""

from __future__ import annotations

import math


def _entry(doc, table, index):
    """Resolve a required accessor or material reference with a useful error."""
    entries = doc.get(table, [])
    if (not isinstance(entries, list) or type(index) is not int or
            index < 0 or index >= len(entries) or not isinstance(entries[index], dict)):
        raise ValueError(f"invalid {table} reference: {index!r}")
    return entries[index]


def validate_primitive(doc, primitive):
    """Reject data the current importer would silently drop or reinterpret.

    The game uses flat material colors, independent triangles, lines and points.
    Unused normal/UV attributes are harmless, but an actual texture or vertex
    color cannot be represented by a single RGBA value per primitive.
    """
    if not isinstance(primitive, dict):
        raise ValueError("mesh primitive must be an object")
    mode = primitive.get("mode", 4)
    if type(mode) is not int or mode not in (0, 1, 4):
        raise ValueError(f"unsupported primitive mode {mode!r}; export points, lines or triangles")
    attributes = primitive.get("attributes")
    if not isinstance(attributes, dict) or "POSITION" not in attributes:
        raise ValueError("primitive requires POSITION")
    if any(key.startswith("COLOR_") for key in attributes):
        raise ValueError("vertex colors are unsupported; use flat material colors")
    position = _entry(doc, "accessors", attributes["POSITION"])
    if position.get("type") != "VEC3" or position.get("componentType") != 5126:
        raise ValueError("POSITION must be float VEC3")
    vertices = position.get("count")
    if type(vertices) is not int or vertices <= 0:
        raise ValueError("POSITION must contain vertices")
    if "indices" in primitive:
        indices = _entry(doc, "accessors", primitive["indices"])
        if indices.get("type") != "SCALAR" or indices.get("componentType") not in (5121, 5123, 5125):
            raise ValueError("indices must be unsigned integer SCALAR values")
        vertices = indices.get("count")
    if type(vertices) is not int or vertices <= 0 or vertices % {0: 1, 1: 2, 4: 3}[mode]:
        raise ValueError("primitive has an incomplete point, line or triangle")
    if "material" not in primitive:
        return
    material = _entry(doc, "materials", primitive["material"])
    pbr = material.get("pbrMetallicRoughness", {})
    if not isinstance(pbr, dict):
        raise ValueError("invalid material properties")
    if "baseColorTexture" in pbr:
        raise ValueError("base-color textures are unsupported; use flat material colors")
    factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
    if (not isinstance(factor, list) or len(factor) != 4 or
            any(type(v) not in (int, float) or not math.isfinite(v) or v < 0 or v > 1
                for v in factor)):
        raise ValueError("baseColorFactor must contain four finite values in [0, 1]")
