import json
import struct
import tempfile
import unittest
from pathlib import Path

from f15assets.validation_model3d import _validate_glb_loadability


class GltfLoadabilityTest(unittest.TestCase):
    def validate(self, vertices=3, vertex_colors=False):
        payload = struct.pack("<" + "3f" * vertices, *([1, 0, 0] * vertices))
        attributes = {"POSITION": 0}
        if vertex_colors:
            attributes["COLOR_0"] = 0
        doc = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(payload)}],
               "bufferViews": [{"buffer": 0, "byteLength": len(payload)}],
               "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}],
               "meshes": [{"primitives": [{"attributes": attributes, "mode": 4}]}],
               "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
        encoded = json.dumps(doc).encode()
        encoded += b" " * (-len(encoded) % 4)
        data = struct.pack("<III", 0x46546c67, 2, 28 + len(encoded) + len(payload))
        data += struct.pack("<II", len(encoded), 0x4e4f534a) + encoded
        data += struct.pack("<II", len(payload), 0x004e4942) + payload
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shape.glb"
            path.write_bytes(data)
            return _validate_glb_loadability(path)

    def test_valid_geometry(self):
        self.assertEqual(self.validate(), (1, 0))

    def test_truncated_geometry_is_not_loadable(self):
        with self.assertRaises((ValueError, struct.error)):
            self.validate(vertices=1)

    def test_unsupported_color_data_is_not_loadable(self):
        with self.assertRaises(ValueError):
            self.validate(vertex_colors=True)
