"""Resolve static GLB scene instances into the runtime's flat mesh space."""

from __future__ import annotations

import math
import struct
from copy import deepcopy

from .gltf_primitive import validate_primitive

IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)


def _entry(entries, index, label):
    """Resolve a scene reference without accepting negative Python indices."""
    if type(index) is not int or index < 0 or index >= len(entries):
        raise ValueError(f"invalid {label} index: {index!r}")
    item = entries[index]
    if not isinstance(item, dict):
        raise ValueError(f"{label} must be an object")
    return item


def _vector(value, count, label):
    """Read a finite transform vector of the specified dimension."""
    if (not isinstance(value, (list, tuple)) or len(value) != count or
            any(type(v) not in (int, float) or not math.isfinite(v) for v in value)):
        raise ValueError(f"invalid {label}")
    return value


def _local_matrix(node):
    """Build glTF's column-major T*R*S transform, or use its explicit matrix."""
    if "matrix" in node:
        if any(key in node for key in ("translation", "rotation", "scale")):
            raise ValueError("node cannot contain both matrix and TRS")
        matrix = _vector(node["matrix"], 16, "node matrix")
        if tuple(matrix[i] for i in (3, 7, 11, 15)) != (0, 0, 0, 1):
            raise ValueError("node matrix must be affine")
        return matrix
    tx, ty, tz = _vector(node.get("translation", (0, 0, 0)), 3, "translation")
    sx, sy, sz = _vector(node.get("scale", (1, 1, 1)), 3, "scale")
    x, y, z, w = _vector(node.get("rotation", (0, 0, 0, 1)), 4, "rotation")
    norm = math.sqrt(x*x + y*y + z*z + w*w)
    if not math.isfinite(norm) or abs(norm - 1) > 0.001:
        raise ValueError("node rotation must be a unit quaternion")
    x, y, z, w = x/norm, y/norm, z/norm, w/norm
    return (
        (1-2*(y*y+z*z))*sx, 2*(x*y+z*w)*sx, 2*(x*z-y*w)*sx, 0,
        2*(x*y-z*w)*sy, (1-2*(x*x+z*z))*sy, 2*(y*z+x*w)*sy, 0,
        2*(x*z+y*w)*sz, 2*(y*z-x*w)*sz, (1-2*(x*x+y*y))*sz, 0,
        tx, ty, tz, 1,
    )


def _multiply(left, right):
    """Compose parent and local column-major transforms."""
    return tuple(sum(left[k*4+r] * right[c*4+k] for k in range(4))
                 for c in range(4) for r in range(4))


def flatten_scene(doc, blob, read_accessor):
    """Bake the selected static scene into meshes without changing primitives.

    Identity transforms retain the original accessors byte-for-byte. Distinct
    nodes using one mesh remain distinct instances, while unused meshes do not
    leak into the selected shape. Animation/skinning cannot be baked silently.
    """
    nodes = doc.get("nodes", [])
    scenes = doc.get("scenes", [])
    if not isinstance(nodes, list) or not isinstance(scenes, list):
        raise ValueError("nodes and scenes must be arrays")
    if doc.get("animations"):
        raise ValueError("animated GLBs require a static export for this importer")
    if scenes:
        roots = _entry(scenes, doc.get("scene", 0), "scene").get("nodes", [])
    else:
        # Scene-less assets can still have a node hierarchy. Infer only roots,
        # not every node (which would duplicate all descendants).
        children = set()
        for node in nodes:
            if not isinstance(node, dict) or not isinstance(node.get("children", []), list):
                raise ValueError("invalid node children")
            for child in node.get("children", []):
                _entry(nodes, child, "child node")
                children.add(child)
        roots = [i for i in range(len(nodes)) if i not in children]
    if not isinstance(roots, list) or not roots:
        raise ValueError("GLB has no scene roots")
    result = deepcopy(doc)
    meshes = []
    data = bytearray(blob)
    transformed = {}
    visited = set()
    stack = [(index, IDENTITY) for index in reversed(roots)]
    while stack:
        index, parent = stack.pop()
        node = _entry(nodes, index, "node")
        if index in visited:
            raise ValueError("scene contains a cycle or repeated node reference")
        visited.add(index)
        if "skin" in node or "weights" in node:
            raise ValueError("skinning and morph weights require a static export")
        matrix = _multiply(parent, _local_matrix(node))
        if any(not math.isfinite(v) for v in matrix):
            raise ValueError("scene transform overflow")
        if "mesh" in node:
            mesh = deepcopy(_entry(doc.get("meshes", []), node["mesh"], "mesh"))
            if "weights" in mesh:
                raise ValueError("morph weights require a static export")
            for primitive in mesh.get("primitives", []):
                validate_primitive(doc, primitive)
                if "targets" in primitive:
                    raise ValueError("morph targets require a static export")
                if matrix == IDENTITY:
                    continue
                attributes = primitive.get("attributes", {})
                accessor_index = attributes.get("POSITION")
                accessor = _entry(doc.get("accessors", []), accessor_index, "POSITION")
                if accessor.get("type") != "VEC3" or accessor.get("componentType") != 5126:
                    raise ValueError("POSITION must contain float VEC3 values")
                key = (accessor_index, matrix)
                if key not in transformed:
                    positions = read_accessor(doc, blob, accessor_index)
                    packed = bytearray()
                    for x, y, z in positions:
                        values = tuple(matrix[r]*x + matrix[4+r]*y +
                                       matrix[8+r]*z + matrix[12+r] for r in range(3))
                        if any(not math.isfinite(v) or abs(v) > 3.4028234663852886e38 for v in values):
                            raise ValueError("transformed POSITION exceeds float32 range")
                        packed.extend(struct.pack("<3f", *values))
                    data.extend(b"\0" * (-len(data) % 4))
                    view = len(result.setdefault("bufferViews", []))
                    result["bufferViews"].append({"buffer": 0, "byteOffset": len(data),
                                                   "byteLength": len(packed)})
                    transformed[key] = len(result.setdefault("accessors", []))
                    result["accessors"].append({"bufferView": view, "componentType": 5126,
                                                 "count": len(positions), "type": "VEC3"})
                    data.extend(packed)
                attributes["POSITION"] = transformed[key]
            meshes.append(mesh)
        children = node.get("children", [])
        if not isinstance(children, list):
            raise ValueError("node children must be an array")
        stack.extend((child, matrix) for child in reversed(children))
    if not meshes:
        raise ValueError("selected scene has no meshes")
    result["meshes"] = meshes
    # The caller consumes the flat mesh list; publish a matching scene too so
    # this intermediate document cannot accidentally retain stale mesh indices.
    result["nodes"] = [{"mesh": i} for i in range(len(meshes))]
    result["scenes"] = [{"nodes": list(range(len(meshes)))}]
    result["scene"] = 0
    if len(data) != len(blob):
        _entry(result.get("buffers", []), 0, "buffer")["byteLength"] = len(data)
    return result, bytes(data)
