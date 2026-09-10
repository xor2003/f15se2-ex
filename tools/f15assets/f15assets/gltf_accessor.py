"""Bounds-checked access to the embedded geometry buffer of a GLB."""

from __future__ import annotations

import math
import struct

COMPONENT_UNSIGNED_BYTE = 5121
COMPONENT_UNSIGNED_SHORT = 5123
COMPONENT_UNSIGNED_INT = 5125
COMPONENT_FLOAT = 5126
UNSIGNED_COMPONENT_TYPES = (COMPONENT_UNSIGNED_BYTE, COMPONENT_UNSIGNED_SHORT, COMPONENT_UNSIGNED_INT)
COMPONENT_FORMATS = {
    COMPONENT_UNSIGNED_BYTE: "B",
    COMPONENT_UNSIGNED_SHORT: "H",
    COMPONENT_UNSIGNED_INT: "I",
    COMPONENT_FLOAT: "f",
}
MIN_VERTEX_STRIDE_BYTES = 4
MAX_VERTEX_STRIDE_BYTES = 252
VERTEX_ALIGNMENT_BYTES = 4


def _integer(value: object, label: str, minimum: int = 0) -> int:
    """Reject booleans, fractional values, and negative offsets or counts."""
    if type(value) is not int or value < minimum:
        raise ValueError(f"{label} must be an integer >= {minimum}")
    return value


def _entry(doc: dict, table: str, index: object) -> dict:
    """Resolve a JSON table index without Python's negative-index semantics."""
    index = _integer(index, table + " index")
    entries = doc.get(table, [])
    if not isinstance(entries, list) or index >= len(entries):
        raise ValueError(f"{table} index {index} is out of range")
    entry = entries[index]
    if not isinstance(entry, dict):
        raise ValueError(f"{table}[{index}] must be an object")
    return entry


def accessor_values(doc: dict, blob: bytes, index: int) -> list[int | float | tuple]:
    """Read supported scalar/vector data, bounded by its view and buffer.

    The runtime cache supports plain unsigned indices and float positions.
    Sparse or normalized accessors must not silently become different geometry.
    """
    accessor = _entry(doc, "accessors", index)
    if "sparse" in accessor or accessor.get("normalized", False):
        raise ValueError("sparse/normalized geometry accessors are unsupported")
    view = _entry(doc, "bufferViews", accessor.get("bufferView"))
    if _integer(view.get("buffer"), "buffer index") != 0:
        raise ValueError("geometry must use the embedded GLB buffer")
    buffer = _entry(doc, "buffers", 0)
    if "uri" in buffer:
        raise ValueError("external geometry buffers are unsupported")
    buffer_size = _integer(buffer.get("byteLength"), "buffer byteLength")
    view_offset = _integer(view.get("byteOffset", 0), "view byteOffset")
    view_size = _integer(view.get("byteLength"), "view byteLength")
    offset = _integer(accessor.get("byteOffset", 0), "accessor byteOffset")
    count = _integer(accessor.get("count"), "accessor count", 1)
    component = _integer(accessor.get("componentType"), "componentType")
    dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    kind = accessor.get("type")
    if component not in COMPONENT_FORMATS or not isinstance(kind, str) or kind not in dimensions:
        raise ValueError("unsupported geometry accessor type")
    decoder = struct.Struct("<" + COMPONENT_FORMATS[component] * dimensions[kind])
    component_size = struct.calcsize("<" + COMPONENT_FORMATS[component])
    stride = _integer(view.get("byteStride", decoder.size), "byteStride", 1)
    invalid_component_stride = stride < decoder.size or stride % component_size != 0
    invalid_vertex_stride = "byteStride" in view and (
        not MIN_VERTEX_STRIDE_BYTES <= stride <= MAX_VERTEX_STRIDE_BYTES
        or stride % VERTEX_ALIGNMENT_BYTES != 0
    )
    if invalid_component_stride or invalid_vertex_stride:
        raise ValueError("invalid geometry accessor stride")
    if offset % component_size or (view_offset + offset) % component_size:
        raise ValueError("unaligned geometry accessor")
    # The BIN chunk may have padding, but neither that padding nor the next
    # buffer view belongs to this accessor. Check before allocating or reading.
    end = offset + (count - 1) * stride + decoder.size
    if (buffer_size > len(blob) or view_offset + view_size > buffer_size or
            end > view_size):
        raise ValueError("geometry accessor exceeds its buffer view")
    values = [decoder.unpack_from(blob, view_offset + offset + i * stride)
              for i in range(count)]
    if component == COMPONENT_FLOAT and any(not math.isfinite(v) for row in values for v in row):
        raise ValueError("non-finite geometry coordinate")
    # Index consumers expect numbers; positions retain their vector tuples.
    return [row[0] for row in values] if kind == "SCALAR" else values
