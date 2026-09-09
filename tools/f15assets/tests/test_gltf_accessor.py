from __future__ import annotations

import copy
import struct
import unittest

from f15assets.gltf_accessor import accessor_values
from f15assets.validation_model3d import _gltf_accessor_values


class GltfAccessorTest(unittest.TestCase):
    def setUp(self):
        self.blob = struct.pack("<6f", 1, 2, 3, 4, 5, 6)
        self.doc = {
            "buffers": [{"byteLength": 24}],
            "bufferViews": [{"buffer": 0, "byteLength": 24}],
            "accessors": [{"bufferView": 0, "componentType": 5126,
                           "count": 2, "type": "VEC3"}],
        }

    def test_runtime_bridge_uses_checked_reader(self):
        self.assertEqual(_gltf_accessor_values(self.doc, self.blob, 0),
                         [(1, 2, 3), (4, 5, 6)])
        self.doc["bufferViews"][0]["byteLength"] = 12
        with self.assertRaises(ValueError):
            _gltf_accessor_values(self.doc, self.blob, 0)

    def test_interleaved_positions(self):
        self.blob = struct.pack("<8f", 1, 2, 3, 99, 4, 5, 6, 99)
        self.doc["buffers"][0]["byteLength"] = 32
        self.doc["bufferViews"][0].update(byteLength=32, byteStride=16)
        self.assertEqual(accessor_values(self.doc, self.blob, 0),
                         [(1, 2, 3), (4, 5, 6)])

    def test_invalid_metadata(self):
        cases = [
            ("accessors", "bufferView", -1),
            ("accessors", "byteOffset", -4),
            ("accessors", "byteOffset", 2),
            ("accessors", "count", 0),
            ("accessors", "count", 3),
            ("accessors", "count", 1.5),
            ("accessors", "count", True),
            ("accessors", "sparse", {}),
            ("accessors", "normalized", True),
            ("accessors", "componentType", 5122),
            ("accessors", "type", "MAT4"),
            ("bufferViews", "buffer", 1),
            ("bufferViews", "byteLength", 12),
            ("bufferViews", "byteOffset", -1),
            ("bufferViews", "byteOffset", 4),
            ("bufferViews", "byteStride", 8),
            ("bufferViews", "byteStride", 14),
            ("bufferViews", "byteStride", 256),
            ("buffers", "byteLength", 12),
            ("buffers", "byteLength", 28),
            ("buffers", "uri", "external.bin"),
        ]
        for table, field, value in cases:
            with self.subTest(table=table, field=field, value=value):
                doc = copy.deepcopy(self.doc)
                doc[table][0][field] = value
                with self.assertRaises(ValueError):
                    accessor_values(doc, self.blob, 0)

    def test_invalid_index(self):
        for index in (-1, 1, True, 0.5):
            with self.subTest(index=index), self.assertRaises(ValueError):
                accessor_values(self.doc, self.blob, index)

    def test_non_finite_positions(self):
        for value in (float("nan"), float("inf"), -float("inf")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                accessor_values(self.doc, struct.pack("<6f", value, 2, 3, 4, 5, 6), 0)

    def test_unsigned_indices(self):
        for component, fmt in ((5121, "B"), (5123, "H"), (5125, "I")):
            with self.subTest(component=component):
                blob = struct.pack("<3" + fmt, 0, 1, 2)
                self.doc["buffers"][0]["byteLength"] = len(blob)
                self.doc["bufferViews"][0]["byteLength"] = len(blob)
                self.doc["accessors"][0].update(componentType=component, count=3, type="SCALAR")
                self.assertEqual(accessor_values(self.doc, blob, 0), [0, 1, 2])
