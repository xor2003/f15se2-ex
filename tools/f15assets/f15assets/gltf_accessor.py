"""Bounds-checked access to the embedded geometry buffer of a GLB."""

from __future__ import annotations

import math
import struct


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
    formats = {5121: "B", 5123: "H", 5125: "I", 5126: "f"}
    dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    kind = accessor.get("type")
    if component not in formats or not isinstance(kind, str) or kind not in dimensions:
        raise ValueError("unsupported geometry accessor type")
    decoder = struct.Struct("<" + formats[component] * dimensions[kind])
    component_size = struct.calcsize("<" + formats[component])
    stride = _integer(view.get("byteStride", decoder.size), "byteStride", 1)
    if (stride < decoder.size or stride % component_size or
            ("byteStride" in view and (stride < 4 or stride > 252 or stride % 4))):
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
    if component == 5126 and any(not math.isfinite(v) for row in values for v in row):
        raise ValueError("non-finite geometry coordinate")
    # Index consumers expect numbers; positions retain their vector tuples.
    return [row[0] for row in values] if kind == "SCALAR" else values
